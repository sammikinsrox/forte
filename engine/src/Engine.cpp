#include "forte/engine/Engine.h"

#include "Kernels.h"
#include "LabMapping.h"
#include "NoteModel.h"
#include "PianoModel.h"
#include "Room.h"
#include "ScopedFlushDenormals.h"
#include "Soundboard.h"
#include "SoundboardIrs.h"
#include "SympatheticBank.h"
#include "Tuner.h"
#include "VelocityCurve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace forte
{
namespace
{
constexpr int controlInterval = detail::NoteModel::controlInterval;
constexpr int partitionSize = detail::Soundboard::partitionSize;
constexpr double pedalSmoothingSeconds = 0.02;  // SPEC §4.2
constexpr double lowestSampleRate = 44100.0;    // SPEC §2.2
constexpr double outputGlideSeconds = 0.02;     // SPEC §2.9: output controls, and the tape
constexpr double tuningGlideSeconds = 0.05;     // SPEC §2.9, §5
constexpr int ticksPerTuningStep = 8;           // a retune recomputes every ringing mode, so it steps every 256 samples
constexpr double hardnessSpan = 4.0;            // Hardness 0 and 100: the felt ÷ and × this (SPEC §10: by ear)
constexpr double volumeOffDb = -60.0;           // SPEC §6.3: shows as "Off", silent

static_assert (partitionSize % controlInterval == 0, "a partition holds whole control intervals");
static_assert (controlInterval <= detail::SympatheticBank::maxRenderSamples, "the banks render a control interval at once");

/** A pedal CC value as a 0–1 position, with the dead zones from SPEC §4.2:
    0–12 is fully down (dampers resting), 116–127 is fully lifted. */
float pedalPosition (int value) noexcept
{
    constexpr int restingUpTo = 12;
    constexpr int liftedFrom = 116;
    return std::clamp (static_cast<float> (value - restingUpTo) / static_cast<float> (liftedFrom - restingUpTo), 0.0f, 1.0f);
}

double hardnessScaleOf (double hardness) noexcept
{
    hardness = std::clamp (hardness, 0.0, 100.0);
    return hardness == 50.0 ? 1.0 : std::pow (hardnessSpan, (hardness - 50.0) / 50.0);
}

double volumeGainOf (double decibels) noexcept
{
    return decibels <= volumeOffDb ? 0.0 : std::pow (10.0, std::min (decibels, 6.0) / 20.0);
}

double widthOf (double width) noexcept
{
    return std::clamp (width, 0.0, 100.0) / 100.0;
}

/** A value gliding linearly to its target over a number of samples. */
struct Ramp
{
    double now = 0.0, target = 0.0, step = 0.0;
    int remaining = 0;

    void jump (double value) noexcept
    {
        now = target = value;
        step = 0.0;
        remaining = 0;
    }

    void glideTo (double value, int samples) noexcept
    {
        target = value;
        remaining = value == now ? 0 : std::max (1, samples);
        step = remaining > 0 ? (target - now) / remaining : 0.0;
    }

    void finish() noexcept { jump (target); }

    void advance (int samples) noexcept
    {
        if (remaining <= 0)
            return;

        if (samples >= remaining)
        {
            now = target;
            remaining = 0;
        }
        else
        {
            now += step * samples;
            remaining -= samples;
        }
    }

    [[nodiscard]] bool moving() const noexcept { return remaining > 0; }
};

namespace cc
{
constexpr int sustain = 64;
constexpr int soft = 67;
constexpr int allSoundOff = 120;
constexpr int resetAllControllers = 121;
constexpr int allNotesOff = 123;
} // namespace cc
} // namespace

struct Engine::State
{
    detail::PianoModel model = detail::makeSeedPianoModel();
    detail::StretchCurve stretchCurve = detail::calculateStretchCurve (model);

    /** The 88 note models (SPEC §2.2). Held by pointer so a note can take re-prepared strings
        (SPEC §6.5) by exchanging pointers on the audio thread. */
    std::array<std::unique_ptr<detail::NoteModel>, detail::numNotes> notes = [] {
        std::array<std::unique_ptr<detail::NoteModel>, detail::numNotes> made;
        for (auto& n : made)
            n = std::make_unique<detail::NoteModel>();
        return made;
    }();

    /** Each note's bridge region, as an index into regionBus. */
    std::array<std::uint8_t, detail::numNotes> noteRegion {};

    /** The IRs as shipped, at 48 kHz; prepare() resamples them for the host rate. */
    detail::SoundboardIrs shippedIrs = detail::loadEmbeddedIrs();
    detail::Soundboard soundboard;

    /** The room (SPEC §2.11), after the tape; and its tail, for getTailSeconds(). */
    detail::Room room;
    std::atomic<double> roomTailSeconds { 0.0 };

    /** The voicing lab's board (SPEC §6.5): the parts it rebuilds the IRs from, the shipped IRs
        at the prepared rate (what it plays at the Board group's 50s), and, off the audio thread,
        the board of the soundboard's newest bank (offered or taken) and of the one it played
        before that offer. */
    detail::BoardParts boardParts = detail::loadEmbeddedBoardParts();
    detail::SoundboardIrs shippedAtRate;
    detail::BoardVoicing builtBoard = detail::committedBoardVoicing();
    detail::BoardVoicing playingBoard = detail::committedBoardVoicing();

    [[nodiscard]] detail::SoundboardIrs boardIrs (const detail::BoardVoicing& board, double rate) const
    {
        if (board == detail::committedBoardVoicing())
            return shippedAtRate;

        return detail::resampleIrs (detail::buildBoardIrs (boardParts, board), rate);
    }

    detail::SympatheticBank sympathetic;

    /** The hot loops' instruction set (Kernels.h), shared by every note and the soundboard. */
    const detail::Kernels* kernels = &detail::bestKernels();

    /** The notes' and banks' bridge force per region, for the current stretch of one partition. */
    std::array<std::array<float, partitionSize>, detail::numBridgeRegions> regionBus {};

    /** Each note's own bridge force over the current stretch of a control interval, and whether
        it sounded (if not, its buffer is stale). Each bank hears every note but its own. */
    std::array<std::array<float, controlInterval>, detail::numNotes> noteOutput {};
    std::array<bool, detail::numNotes> noteSounded {};

    /** Every note's bridge force summed, over the same stretch: the banks' drive. */
    std::array<float, controlInterval> bridgeDrive {};

    /** Note-ons still waiting for their note-off, per key (SPEC §4.3: overlaps are counted). */
    std::array<std::uint32_t, detail::numNotes> keyDownCount {};

    float sustainTarget = 0.0f;  // the pedal as last received
    float sustainLift = 0.0f;    // smoothed, drives the dampers
    float sustainStepPerTick = 1.0f;
    float softPedal = 0.0f;      // the action's shift, read at each strike (SPEC §2.7)

    /** Engine::getTailSeconds(), from each note's strings as built (the lab's included). */
    std::atomic<double> tailSeconds { 0.0 };
    std::array<double, detail::numNotes> noteTail {}, noteSlowestDecay {};

    std::atomic<int> soundingNotes { 0 };

    /** The voicing lab's strings (SPEC §6.5) and the tuning (SPEC §5). Per note: a model built
        off the audio thread, waiting for the note to start from rest, and the model it
        replaced, waiting to be freed off the audio thread. Each slot has one writer and one
        reader and changes hands by atomic exchange, so the audio thread never allocates, frees
        or waits. */
    std::array<std::atomic<detail::NoteModel*>, detail::numNotes> incomingStrings {};
    std::array<std::atomic<detail::NoteModel*>, detail::numNotes> retiredStrings {};

    /** What a note's model is built from, beyond the committed piano. */
    struct BuiltStrings
    {
        detail::StringVoicing lab;
        double tuning = 1.0;       // the tuner's ratio (Tuner.h)
        double spreadCents = 0.0;  // the unison spread

        friend bool operator== (const BuiltStrings&, const BuiltStrings&) = default;
    };

    /** Off the audio thread: what each note's newest model (current or incoming) is built from. */
    std::array<BuiltStrings, detail::numNotes> builtStrings {};

    ~State()
    {
        for (auto* slots : { &incomingStrings, &retiredStrings })
            for (auto& slot : *slots)
                delete slot.exchange (nullptr);
    }

    [[nodiscard]] double spreadCentsOf (const Tuning& tuning) const noexcept
    {
        return model.unisonSpreadCents * (std::clamp (tuning.spread, 0.0, 100.0) / 50.0);
    }

    [[nodiscard]] double tuningOf (std::size_t i, const Tuning& tuning) const noexcept
    {
        return detail::tuningRatio (stretchCurve, static_cast<int> (i), std::clamp (tuning.a4Hz, detail::lowestA4, detail::highestA4),
                                    std::clamp (tuning.stretch, 0.0, 100.0), std::clamp (tuning.detune, 0.0, 100.0));
    }

    [[nodiscard]] BuiltStrings stringsFor (std::size_t i, const lab::Settings& labSettings, const Tuning& tuning) const noexcept
    {
        // A single string has no unison to spread.
        const double spread = model.notes[i].numStrings > 1 ? spreadCentsOf (tuning) : model.unisonSpreadCents;
        return { detail::labStringVoicing (labSettings, model.notes[i]), tuningOf (i, tuning), spread };
    }

    /** The note's parameters, tuned, under its strings' lab voicing. */
    [[nodiscard]] detail::NoteParameters voicedNote (std::size_t i, const BuiltStrings& strings) const noexcept
    {
        auto tuned = model.notes[i];
        if (strings.tuning != 1.0)
            tuned.fundamental *= strings.tuning;
        return detail::applyStringVoicing (tuned, strings.lab);
    }

    void measureNoteTail (std::size_t i, const detail::NoteParameters& p, double spreadCents)
    {
        noteTail[i] = detail::noteTailSeconds (p, spreadCents);
        noteSlowestDecay[i] = detail::slowestStringDecay (p, spreadCents);
    }

    void updateTail()
    {
        double notesTail = 0.0;
        double slowestDecay = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < noteTail.size(); ++i)
        {
            notesTail = std::max (notesTail, noteTail[i]);
            slowestDecay = std::min (slowestDecay, noteSlowestDecay[i]);
        }

        // The sympathetic bank can ring on past the strings that drive it. The soundboard rings
        // on for the IR's length after the strings stop. The convolution's partition grid can
        // carry rounding residue up to two partitions further, at most this long at the lowest
        // host rate.
        const double stringsTail = detail::sympatheticTailSeconds (model, notesTail, slowestDecay);
        const double irSeconds = static_cast<double> (shippedIrs.length()) / shippedIrs.sampleRate;
        tailSeconds.store (stringsTail + irSeconds + 2.0 * partitionSize / lowestSampleRate, std::memory_order_relaxed);
    }

    /** At a strike from rest, the note takes its incoming strings, if any and if the model they
        replace before has been freed. Realtime-safe: an exchange and a swap of buffers. */
    void adoptIncomingStrings (std::size_t i) noexcept
    {
        if (incomingStrings[i].load (std::memory_order_acquire) == nullptr
            || retiredStrings[i].load (std::memory_order_acquire) != nullptr)
            return;

        auto* incoming = incomingStrings[i].exchange (nullptr, std::memory_order_acq_rel);
        if (incoming == nullptr)
            return;

        incoming->takeControlStateFrom (*notes[i]);
        detail::NoteModel* replaced = notes[i].release();
        notes[i].reset (incoming);
        retiredStrings[i].store (replaced, std::memory_order_release);

        // Built on another thread for the tuning as it then stood.
        notes[i]->setPitchRatio (tuningNow[i] / notes[i]->getBuiltTuning());
    }

    /** The sound controls (SPEC §6.3). */
    Controls controls;
    double touch = detail::touchCalibrated;
    double hardnessScale = 1.0;

    /** The tuning as setControls() last published it, for rebuildStrings() on another thread. */
    std::atomic<double> latestA4 { 440.0 }, latestStretch { 50.0 }, latestSpread { 50.0 }, latestDetune { 0.0 };

    void publishTuning() noexcept
    {
        latestA4.store (controls.tuning.a4Hz, std::memory_order_relaxed);
        latestStretch.store (controls.tuning.stretch, std::memory_order_relaxed);
        latestSpread.store (controls.tuning.spread, std::memory_order_relaxed);
        latestDetune.store (controls.tuning.detune, std::memory_order_relaxed);
    }

    /** The tuning glide (SPEC §5): each note's tuner ratio as it sounds now, and where it's going.
        It steps every ticksPerTuningStep control ticks, tuningSteps times. bankTuning is the
        ratio the sympathetic bank was prepared at. */
    std::array<double, detail::numNotes> tuningNow {}, tuningFrom {}, tuningTo {}, bankTuning {};
    int tuningStep = 0, tuningSteps = 0, tuningTick = 0;
    int tuningStepsPerGlide = 1;

    void applyTuning() noexcept
    {
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            notes[i]->setPitchRatio (tuningNow[i] / notes[i]->getBuiltTuning());
            sympathetic.setPitchRatio (static_cast<int> (i), tuningNow[i] / bankTuning[i]);
        }
    }

    void retune (bool glide) noexcept
    {
        for (std::size_t i = 0; i < tuningTo.size(); ++i)
            tuningTo[i] = tuningOf (i, controls.tuning);

        if (glide)
        {
            tuningFrom = tuningNow;
            tuningStep = tuningTick = 0;
            tuningSteps = tuningStepsPerGlide;
            return;
        }

        tuningNow = tuningTo;
        tuningSteps = 0;
        applyTuning();
    }

    void tuningTickWork() noexcept
    {
        if (tuningSteps == 0 || ++tuningTick < ticksPerTuningStep)
            return;

        tuningTick = 0;
        ++tuningStep;
        const bool last = tuningStep >= tuningSteps;
        const double t = static_cast<double> (tuningStep) / tuningSteps;

        for (std::size_t i = 0; i < tuningNow.size(); ++i)
            tuningNow[i] = last ? tuningTo[i] : tuningFrom[i] + (tuningTo[i] - tuningFrom[i]) * t;

        applyTuning();
        if (last)
            tuningSteps = 0;
    }

    /** Samples rendered since prepare() or reset(). Control ticks fall on multiples of
        controlInterval, so they don't depend on how blocks are split. */
    std::int64_t sampleClock = 0;

    std::array<float, controlInterval> noteScratch {};

    /** The voicing lab (SPEC §6.5). */
    lab::Settings lab;

    /** The output controls' glides (SPEC §2.9), and the tape's drive (the lab's Tape drive). */
    Ramp tape, brightness, resonance, width, volume;
    int glideSamples = static_cast<int> (outputGlideSeconds * 48000.0);

    void applyStrikeVoicing() noexcept
    {
        for (int midiNote = detail::lowestNote; midiNote <= detail::highestNote; ++midiNote)
            note (midiNote).setStrikeVoicing (detail::labStrikeVoicing (lab, model.note (midiNote)));
    }

    /** The output stage's settled values, at once. */
    void finishGlides() noexcept
    {
        for (auto* ramp : { &tape, &brightness, &resonance, &width, &volume })
            ramp->finish();

        soundboard.setBrightness (brightness.now);
        sympathetic.setResonance (resonance.now);
    }

    /** The tape (see render()) while its drive glides: the drive moves every sample. Near 0 the
        curve is the identity. */
    void glideTape (float* left, float* right, int numSamples) noexcept
    {
        const float t = std::tanh (0.15f);

        for (int i = 0; i < numSamples; ++i)
        {
            tape.advance (1);

            const auto drive = static_cast<float> (tape.now);
            if (drive < 1e-4f)
                continue;

            for (float* channel : { left, right })
            {
                const float u = std::tanh (drive * channel[i]);
                channel[i] = u / (drive * (1.0f + t * u));
            }
        }
    }

    /** Width (mid/side, written so that full width passes the channels through exactly), then
        Volume, each gliding per sample. */
    void widthAndVolume (float* left, float* right, int numSamples) noexcept
    {
        if (! width.moving() && ! volume.moving() && width.now == 1.0 && volume.now == 1.0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            width.advance (1);
            volume.advance (1);
            const auto gain = static_cast<float> (volume.now);
            const auto direct = static_cast<float> (0.5 * (1.0 + width.now));
            const auto cross = static_cast<float> (0.5 * (1.0 - width.now));
            const float l = left[i], r = right[i];
            left[i] = gain * (direct * l + cross * r);
            right[i] = gain * (cross * l + direct * r);
        }
    }

    detail::NoteModel& note (int midiNote) noexcept { return *notes[static_cast<std::size_t> (midiNote - detail::lowestNote)]; }
    std::uint32_t& keyCount (int midiNote) noexcept { return keyDownCount[static_cast<std::size_t> (midiNote - detail::lowestNote)]; }

    void noteOn (int midiNote, int velocity) noexcept
    {
        if (! detail::isOnKeyboard (midiNote))
            return;

        auto& count = keyCount (midiNote);
        if (count < UINT32_MAX)
            ++count;

        if (! note (midiNote).isSounding())
            adoptIncomingStrings (static_cast<std::size_t> (midiNote - detail::lowestNote));

        auto& n = note (midiNote);
        n.setKeyDown (true);
        n.strike (velocity, touch, softPedal, hardnessScale);
    }

    void noteOff (int midiNote) noexcept
    {
        if (! detail::isOnKeyboard (midiNote))
            return;

        auto& count = keyCount (midiNote);
        if (count == 0)
            return;

        if (--count == 0)
            note (midiNote).setKeyDown (false);
    }

    void allNotesOff() noexcept
    {
        for (int midiNote = detail::lowestNote; midiNote <= detail::highestNote; ++midiNote)
        {
            if (keyCount (midiNote) != 0)
            {
                keyCount (midiNote) = 0;
                note (midiNote).setKeyDown (false);
            }
        }
    }

    void controller (int number, int value) noexcept
    {
        switch (number)
        {
            case cc::sustain:             sustainTarget = pedalPosition (value); break;
            case cc::soft:                softPedal = pedalPosition (value); break;
            case cc::allSoundOff:
                for (auto& n : notes)
                    n->silence();
                sympathetic.silence();
                soundboard.silence();
                room.reset();  // asleep: it wakes cleared
                break;

            case cc::resetAllControllers: sustainTarget = 0.0f; softPedal = 0.0f; break;
            case cc::allNotesOff:         allNotesOff(); break;
            default:                      break;  // mod wheel, sostenuto (CC66) and the rest
        }
    }

    void controlTick() noexcept
    {
        if (sustainLift < sustainTarget)
            sustainLift = std::min (sustainTarget, sustainLift + sustainStepPerTick);
        else if (sustainLift > sustainTarget)
            sustainLift = std::max (sustainTarget, sustainLift - sustainStepPerTick);

        if (resonance.moving())
        {
            resonance.advance (controlInterval);
            sympathetic.setResonance (resonance.now);
        }

        tuningTickWork();

        for (int i = 0; i < detail::numNotes; ++i)
        {
            auto& n = *notes[static_cast<std::size_t> (i)];
            n.controlTick (sustainLift);
            sympathetic.controlTick (i, n.getLift());
        }
    }

    /** Renders length samples (within one control interval) of every note and bank into the
        region buses, starting at offset into them. */
    void renderInterval (int offset, int length) noexcept
    {
        bool anySounded = false;

        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            noteSounded[i] = notes[i]->isSounding();
            if (! noteSounded[i])
                continue;

            float* own = noteOutput[i].data();
            std::fill_n (own, length, 0.0f);
            notes[i]->render (own, noteScratch.data(), length);

            float* bus = regionBus[noteRegion[i]].data() + offset;
            for (int t = 0; t < length; ++t)
                bus[t] += own[t];

            anySounded = true;
        }

        // The banks hear only the played strings, never each other (SPEC §2.1).
        if (anySounded)
        {
            for (int t = 0; t < length; ++t)
            {
                const auto at = static_cast<std::size_t> (offset + t);
                bridgeDrive[static_cast<std::size_t> (t)] = regionBus[0][at] + regionBus[1][at] + regionBus[2][at] + regionBus[3][at];
            }
        }

        std::array<const float*, detail::numNotes> own {};
        for (std::size_t i = 0; i < notes.size(); ++i)
            own[i] = noteSounded[i] ? noteOutput[i].data() : nullptr;

        const std::array<float*, detail::numBridgeRegions> buses { regionBus[0].data() + offset, regionBus[1].data() + offset,
                                                                   regionBus[2].data() + offset, regionBus[3].data() + offset };
        sympathetic.render (anySounded ? bridgeDrive.data() : nullptr, own, buses, length);
    }
};

Engine::Engine()
    : state (std::make_unique<State>())
{
    auto& s = *state;
    const auto& tuning = s.controls.tuning;

    for (std::size_t i = 0; i < s.model.notes.size(); ++i)
    {
        s.noteRegion[i] = static_cast<std::uint8_t> (s.model.notes[i].bridgeRegion);
        s.builtStrings[i] = s.stringsFor (i, s.lab, tuning);
        s.measureNoteTail (i, s.voicedNote (i, s.builtStrings[i]), s.builtStrings[i].spreadCents);
        s.tuningNow[i] = s.tuningFrom[i] = s.tuningTo[i] = s.bankTuning[i] = s.builtStrings[i].tuning;
    }

    s.tape.jump (s.model.tapeDrive);
    s.brightness.jump (s.controls.brightness);
    s.resonance.jump (s.controls.resonance);
    s.width.jump (widthOf (s.controls.width));
    s.volume.jump (volumeGainOf (s.controls.volumeDb));
    s.room.setControls (s.controls.room, true);
    s.roomTailSeconds.store (detail::Room::tailSeconds (s.controls.room), std::memory_order_relaxed);
    s.publishTuning();
    s.updateTail();
}

Engine::~Engine() = default;

double Engine::getTailSeconds() const noexcept
{
    return state->tailSeconds.load (std::memory_order_relaxed) + state->roomTailSeconds.load (std::memory_order_relaxed);
}

int Engine::getNumSoundingNotes() const noexcept
{
    return state->soundingNotes.load (std::memory_order_relaxed);
}

InstructionSet Engine::getInstructionSet() const noexcept
{
    return state->kernels == &detail::avx2Kernels ? InstructionSet::avx2 : InstructionSet::sse2;
}

void Engine::setInstructionSet (InstructionSet set) noexcept
{
    auto& s = *state;
    s.kernels = set == InstructionSet::avx2 && detail::cpuSupportsAvx2() ? &detail::avx2Kernels : &detail::sse2Kernels;

    // A model waiting to be taken copies them from the note it replaces (takeControlStateFrom).
    for (auto& n : s.notes)
        n->setKernels (*s.kernels);
    s.soundboard.setKernels (*s.kernels);
}

bool Engine::cpuSupports (InstructionSet set) noexcept
{
    return set == InstructionSet::sse2 || detail::cpuSupportsAvx2();
}

void Engine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    auto& s = *state;
    const auto& model = s.model;
    const auto& tuning = s.controls.tuning;

    // Every note is built with the lab's strings and the tuning as they stand (SPEC §5, §6.5),
    // and nothing older is left waiting.
    bool tailChanged = false;
    for (std::size_t i = 0; i < s.notes.size(); ++i)
    {
        delete s.incomingStrings[i].exchange (nullptr);
        delete s.retiredStrings[i].exchange (nullptr);

        const auto strings = s.stringsFor (i, s.lab, tuning);
        const auto voiced = s.voicedNote (i, strings);
        s.notes[i]->prepare (voiced, strings.spreadCents, model.outputGain, sampleRate);
        s.notes[i]->setBuiltTuning (strings.tuning);

        if (strings != s.builtStrings[i])
        {
            s.builtStrings[i] = strings;
            s.measureNoteTail (i, voiced, strings.spreadCents);
            tailChanged = true;
        }

        s.tuningNow[i] = s.tuningFrom[i] = s.tuningTo[i] = s.bankTuning[i] = strings.tuning;
    }

    if (tailChanged)
        s.updateTail();

    s.tuningSteps = 0;
    s.tuningStepsPerGlide = std::max (1, static_cast<int> (std::lround (tuningGlideSeconds * sampleRate / (ticksPerTuningStep * controlInterval))));
    s.publishTuning();

    s.applyStrikeVoicing();
    s.glideSamples = static_cast<int> (outputGlideSeconds * sampleRate);

    // The sympathetic strings at the same tuning as the notes.
    {
        auto tuned = std::make_unique<detail::PianoModel> (model);
        for (std::size_t i = 0; i < tuned->notes.size(); ++i)
            if (s.bankTuning[i] != 1.0)
                tuned->notes[i].fundamental *= s.bankTuning[i];
        s.sympathetic.prepare (*tuned, sampleRate);
    }

    s.shippedAtRate = detail::resampleIrs (s.shippedIrs, sampleRate);
    s.builtBoard = s.playingBoard = detail::labBoardVoicing (s.lab);
    s.soundboard.prepare (s.boardIrs (s.builtBoard, sampleRate));

    s.sustainStepPerTick = static_cast<float> (controlInterval / (pedalSmoothingSeconds * sampleRate));
    s.room.prepare (sampleRate);
    reset();
}

void Engine::reset() noexcept
{
    auto& s = *state;
    for (auto& n : s.notes)
        n->reset();

    s.sympathetic.reset();
    s.soundboard.reset();
    s.room.reset();
    s.keyDownCount.fill (0);
    s.sustainTarget = s.sustainLift = 0.0f;
    s.softPedal = 0.0f;
    s.sampleClock = 0;
    s.finishGlides();

    if (s.tuningSteps > 0)
    {
        s.tuningNow = s.tuningTo;
        s.tuningSteps = 0;
        s.applyTuning();
    }

    s.soundingNotes.store (0, std::memory_order_relaxed);
}

void Engine::handleMidi (const MidiEvent& event) noexcept
{
    if (event.size < 1)
        return;

    const int status = event.bytes[0];
    if (status < 0x80 || status >= 0xf0)
        return;

    if (event.size < 3)
        return;  // every message the engine reads has two data bytes

    // Omni (SPEC §4.3): the channel nibble is ignored.
    const int type = status & 0xf0;
    const int data1 = event.bytes[1] & 0x7f;
    const int data2 = event.bytes[2] & 0x7f;

    const detail::ScopedFlushDenormals noDenormals;

    switch (type)
    {
        case 0x90:
            if (data2 == 0)
                state->noteOff (data1);
            else
                state->noteOn (data1, data2);
            break;

        case 0x80: state->noteOff (data1); break;  // release velocity is ignored
        case 0xb0: state->controller (data1, data2); break;
        default:   break;  // aftertouch, program change, pitch bend
    }
}

void Engine::render (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const detail::ScopedFlushDenormals noDenormals;
    auto& s = *state;

    // Stretches that end at the soundboard's partition boundaries. Both grids count from the
    // same sample clock, and a partition holds whole control intervals.
    for (int position = 0; position < numSamples;)
    {
        const int stretch = std::min (numSamples - position, s.soundboard.samplesToPartitionEnd());

        for (auto& bus : s.regionBus)
            std::fill_n (bus.begin(), stretch, 0.0f);

        for (int done = 0; done < stretch;)
        {
            const auto phase = static_cast<int> (s.sampleClock % controlInterval);

            if (phase == 0)
                s.controlTick();

            const int length = std::min (stretch - done, controlInterval - phase);

            s.renderInterval (done, length);
            done += length;
            s.sampleClock += length;
        }

        // Brightness glides in steps of a stretch (at most a partition, 128 samples).
        if (s.brightness.moving())
        {
            s.brightness.advance (stretch);
            s.soundboard.setBrightness (s.brightness.now);
        }

        const std::array<const float*, detail::numBridgeRegions> buses { s.regionBus[0].data(), s.regionBus[1].data(),
                                                                         s.regionBus[2].data(), s.regionBus[3].data() };
        s.soundboard.process (buses, left + position, right + position, stretch);

        // Tape-style saturation (PianoModel::tapeDrive): (tanh(d·x + b) − tanh b) / (d·(1 − tanh² b)),
        // unity gain for small signals, the bias giving even harmonics as well as odd. Written as
        // tanh(d·x) / (d·(1 + tanh b · tanh(d·x))), the same function, so tiny signals stay exact
        // instead of cancelling to zero in float. While the lab moves the drive, it glides.
        if (s.tape.moving())
        {
            s.glideTape (left + position, right + position, stretch);
        }
        else if (s.tape.now > 0.0)
        {
            const auto drive = static_cast<float> (s.tape.now);
            const float t = std::tanh (0.15f);
            for (float* channel : { left + position, right + position })
                for (int i = 0; i < stretch; ++i)
                {
                    const float u = std::tanh (drive * channel[i]);
                    channel[i] = u / (drive * (1.0f + t * u));
                }
        }

        s.room.process (left + position, right + position, stretch);
        s.widthAndVolume (left + position, right + position, stretch);
        position += stretch;
    }

    int sounding = 0;
    for (const auto& n : s.notes)
        sounding += n->isSounding() ? 1 : 0;
    s.soundingNotes.store (sounding, std::memory_order_relaxed);
}

void Engine::setListeningPerspective (ListeningPerspective perspective) noexcept
{
    state->soundboard.setPerspective (perspective == ListeningPerspective::pianist ? detail::pianistPerspective
                                                                                    : detail::microphonesPerspective);
}

void Engine::setControls (const Controls& controls) noexcept
{
    auto& s = *state;
    if (controls == s.controls)
        return;

    const auto previous = s.controls;
    s.controls = controls;
    const bool prepared = maxBlockSize > 0;

    s.hardnessScale = hardnessScaleOf (controls.hardness);
    s.touch = std::clamp (controls.touch, detail::touchLightest, detail::touchHeaviest);

    s.brightness.glideTo (std::clamp (controls.brightness, 0.0, 100.0), s.glideSamples);
    s.resonance.glideTo (std::clamp (controls.resonance, 0.0, 100.0), s.glideSamples);
    s.width.glideTo (widthOf (controls.width), s.glideSamples);
    s.volume.glideTo (volumeGainOf (controls.volumeDb), s.glideSamples);
    s.room.setControls (controls.room, ! prepared);
    s.roomTailSeconds.store (detail::Room::tailSeconds (controls.room), std::memory_order_relaxed);

    if (! prepared)
        s.finishGlides();  // prepare() starts from the settled values

    if (controls.tuning.a4Hz != previous.tuning.a4Hz || controls.tuning.stretch != previous.tuning.stretch
        || controls.tuning.detune != previous.tuning.detune)
        s.retune (prepared);

    s.publishTuning();
}

const Controls& Engine::getControls() const noexcept
{
    return state->controls;
}

Tuning Engine::getLatestTuning() const noexcept
{
    const auto& s = *state;
    return { s.latestA4.load (std::memory_order_relaxed), s.latestStretch.load (std::memory_order_relaxed),
             s.latestSpread.load (std::memory_order_relaxed), s.latestDetune.load (std::memory_order_relaxed) };
}

double Engine::getTuningCents (const Tuning& tuning, int midiNote) const noexcept
{
    const auto index = static_cast<std::size_t> (std::clamp (midiNote, detail::lowestNote, detail::highestNote) - detail::lowestNote);
    return std::clamp (tuning.stretch, 0.0, 100.0) / detail::calculatedStretch * state->stretchCurve.cents[index]
         + detail::detuneCents (static_cast<int> (index), std::clamp (tuning.detune, 0.0, 100.0));
}

void Engine::setVoicingLab (const lab::Settings& settings) noexcept
{
    auto& s = *state;
    if (settings == s.lab)
        return;

    const bool hammerChanged = [&] {
        for (const auto& c : lab::controls)
            if (c.group == lab::Group::hammer && s.lab.values[static_cast<std::size_t> (c.control)] != settings.values[static_cast<std::size_t> (c.control)])
                return true;
        return false;
    }();

    s.lab = settings;

    if (hammerChanged)
        s.applyStrikeVoicing();

    const double tape = detail::labTapeDrive (settings, s.model.tapeDrive);
    if (tape != s.tape.target)
    {
        s.tape.glideTo (tape, s.glideSamples);
        if (maxBlockSize == 0)
            s.tape.finish();
    }
}

const lab::Settings& Engine::getVoicingLab() const noexcept
{
    return state->lab;
}

int Engine::rebuildStrings (const lab::Settings& settings, const Tuning& tuning)
{
    auto& s = *state;
    collectRetired();

    if (maxBlockSize == 0)
        return 0;  // not prepared yet: prepare() builds them

    int built = 0;
    for (std::size_t i = 0; i < s.notes.size(); ++i)
    {
        const auto strings = s.stringsFor (i, settings, tuning);
        if (strings == s.builtStrings[i])
            continue;

        const auto voiced = s.voicedNote (i, strings);
        auto fresh = std::make_unique<detail::NoteModel>();
        fresh->prepare (voiced, strings.spreadCents, s.model.outputGain, sampleRate);
        fresh->setBuiltTuning (strings.tuning);

        // A model the note never took is this thread's again; the newer one replaces it.
        delete s.incomingStrings[i].exchange (fresh.release(), std::memory_order_acq_rel);

        s.builtStrings[i] = strings;
        s.measureNoteTail (i, voiced, strings.spreadCents);
        ++built;
    }

    if (built > 0)
        s.updateTail();

    return built;
}

bool Engine::rebuildBoard (const lab::Settings& settings)
{
    auto& s = *state;
    collectRetired();

    if (maxBlockSize == 0)
        return false;  // not prepared yet: prepare() builds it

    const auto board = detail::labBoardVoicing (settings);
    if (board == s.builtBoard)
        return false;

    // An offer the soundboard never took is withdrawn, so a board moved and put back before
    // then doesn't fade between two copies of the same IRs (which isn't bit-exact).
    if (! s.soundboard.withdrawBank())
        s.playingBoard = s.builtBoard;  // it took the last one (or there was none)

    s.builtBoard = s.playingBoard;
    if (board == s.playingBoard)
        return false;

    s.soundboard.offerBank (s.soundboard.makeBank (s.boardIrs (board, sampleRate)));
    s.builtBoard = board;
    return true;
}

void Engine::collectRetired() noexcept
{
    for (auto& slot : state->retiredStrings)
        if (slot.load (std::memory_order_acquire) != nullptr)
            delete slot.exchange (nullptr, std::memory_order_acq_rel);

    state->soundboard.collectRetiredBank();
}

bool Engine::hasIncomingBoard() const noexcept
{
    return state->soundboard.hasIncomingBank();
}

bool Engine::hasIncomingStrings() const noexcept
{
    for (const auto& slot : state->incomingStrings)
        if (slot.load (std::memory_order_acquire) != nullptr)
            return true;
    return false;
}

double Engine::getLabPhysicalValue (const lab::Settings& settings, lab::Control control, int midiNote) const noexcept
{
    return detail::labValueAtKey (settings, control, midiNote, detail::committedLabValue (control, state->model, midiNote));
}

Engine::LabBoardValues Engine::getLabBoardValues (const lab::Settings& settings) const
{
    const auto board = detail::labBoardVoicing (settings);
    LabBoardValues values;
    values.pickup = board.pickup;
    values.tiltDbPerOctave = board.tiltDbPerOctave;
    for (std::size_t r = 0; r < values.colourDbAtCentres.size(); ++r)
        values.colourDbAtCentres[r] = detail::labColourAtCentres (board.colourDb[r]);
    return values;
}

int Engine::getVoicingRevision() noexcept
{
    return detail::voicingRevision();
}
} // namespace forte
