#include "forte/engine/Engine.h"

#include "LabMapping.h"
#include "NoteModel.h"
#include "PianoModel.h"
#include "ScopedFlushDenormals.h"
#include "Soundboard.h"
#include "SoundboardIrs.h"
#include "SympatheticBank.h"
#include "VelocityCurve.h"

#include <algorithm>
#include <array>
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
constexpr double tapeGlideSeconds = 0.02;       // SPEC §2.9

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
    std::array<detail::NoteModel, detail::numNotes> notes;

    /** Each note's bridge region, as an index into regionBus. */
    std::array<std::uint8_t, detail::numNotes> noteRegion {};

    /** The IRs as shipped, at 48 kHz; prepare() resamples them for the host rate. */
    detail::SoundboardIrs shippedIrs = detail::loadEmbeddedIrs();
    detail::Soundboard soundboard;

    detail::SympatheticBank sympathetic;

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

    /** Engine::getTailSeconds(), computed once from the model. */
    double tailSeconds = 0.0;

    /** Touch, until the parameter exists (SPEC §6.3). */
    double touch = detail::touchCalibrated;

    /** Samples rendered since prepare() or reset(). Control ticks fall on multiples of
        controlInterval, so they don't depend on how blocks are split. */
    std::int64_t sampleClock = 0;

    std::array<float, controlInterval> noteScratch {};

    /** The voicing lab (SPEC §6.5). */
    lab::Settings lab;

    /** The tape's drive as it glides to the lab's (SPEC §2.9: output controls are immediate,
        smoothed over ~20 ms), and its step per sample while it moves. */
    double tapeDrive = model.tapeDrive;
    double tapeTarget = model.tapeDrive;
    double tapeStep = 0.0;
    double tapeGlideSamples = tapeGlideSeconds * 48000.0;

    void applyStrikeVoicing() noexcept
    {
        for (int midiNote = detail::lowestNote; midiNote <= detail::highestNote; ++midiNote)
            note (midiNote).setStrikeVoicing (detail::labStrikeVoicing (lab, model.note (midiNote)));
    }

    void setTapeTarget (double target, bool glide) noexcept
    {
        tapeTarget = target;
        tapeStep = glide ? (tapeTarget - tapeDrive) / tapeGlideSamples : 0.0;
        if (! glide || tapeStep == 0.0)
            tapeDrive = tapeTarget;
    }

    /** The tape (see render()) while its drive glides: the drive moves every sample. Near 0 the
        curve is the identity. */
    void glideTape (float* left, float* right, int numSamples) noexcept
    {
        const float t = std::tanh (0.15f);

        for (int i = 0; i < numSamples; ++i)
        {
            tapeDrive += tapeStep;
            if ((tapeStep > 0.0 && tapeDrive >= tapeTarget) || (tapeStep < 0.0 && tapeDrive <= tapeTarget))
                tapeDrive = tapeTarget;

            const auto drive = static_cast<float> (tapeDrive);
            if (drive < 1e-4f)
                continue;

            for (float* channel : { left, right })
            {
                const float u = std::tanh (drive * channel[i]);
                channel[i] = u / (drive * (1.0f + t * u));
            }
        }
    }

    detail::NoteModel& note (int midiNote) noexcept { return notes[static_cast<std::size_t> (midiNote - detail::lowestNote)]; }
    std::uint32_t& keyCount (int midiNote) noexcept { return keyDownCount[static_cast<std::size_t> (midiNote - detail::lowestNote)]; }

    void noteOn (int midiNote, int velocity) noexcept
    {
        if (! detail::isOnKeyboard (midiNote))
            return;

        auto& count = keyCount (midiNote);
        if (count < UINT32_MAX)
            ++count;

        auto& n = note (midiNote);
        n.setKeyDown (true);
        n.strike (velocity, touch, softPedal);
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
                    n.silence();
                sympathetic.silence();
                soundboard.silence();
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

        for (int i = 0; i < detail::numNotes; ++i)
        {
            auto& n = notes[static_cast<std::size_t> (i)];
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
            noteSounded[i] = notes[i].isSounding();
            if (! noteSounded[i])
                continue;

            float* own = noteOutput[i].data();
            std::fill_n (own, length, 0.0f);
            notes[i].render (own, noteScratch.data(), length);

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
    double notesTail = 0.0;
    double slowestDecay = std::numeric_limits<double>::infinity();

    for (const auto& note : state->model.notes)
    {
        notesTail = std::max (notesTail, detail::noteTailSeconds (note, state->model.unisonSpreadCents));
        slowestDecay = std::min (slowestDecay, detail::slowestStringDecay (note, state->model.unisonSpreadCents));
        state->noteRegion[static_cast<std::size_t> (note.midiNote - detail::lowestNote)] = static_cast<std::uint8_t> (note.bridgeRegion);
    }

    // The sympathetic bank can ring on past the strings that drive it.
    const double stringsTail = detail::sympatheticTailSeconds (state->model, notesTail, slowestDecay);

    // The soundboard rings on for the IR's length after the strings stop. The convolution's
    // partition grid can carry rounding residue up to two partitions further, at most this long
    // at the lowest host rate.
    const double irSeconds = static_cast<double> (state->shippedIrs.length()) / state->shippedIrs.sampleRate;
    state->tailSeconds = stringsTail + irSeconds + 2.0 * partitionSize / lowestSampleRate;
}

Engine::~Engine() = default;

double Engine::getTailSeconds() const noexcept
{
    return state->tailSeconds;
}

void Engine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    const auto& model = state->model;

    for (int midiNote = detail::lowestNote; midiNote <= detail::highestNote; ++midiNote)
        state->note (midiNote).prepare (model.note (midiNote), model.unisonSpreadCents, model.outputGain, sampleRate);

    state->applyStrikeVoicing();
    state->tapeGlideSamples = tapeGlideSeconds * sampleRate;

    state->sympathetic.prepare (model, sampleRate);
    state->soundboard.prepare (detail::resampleIrs (state->shippedIrs, sampleRate));

    state->sustainStepPerTick = static_cast<float> (controlInterval / (pedalSmoothingSeconds * sampleRate));
    reset();
}

void Engine::reset() noexcept
{
    for (auto& n : state->notes)
        n.reset();

    state->sympathetic.reset();
    state->soundboard.reset();
    state->keyDownCount.fill (0);
    state->sustainTarget = state->sustainLift = 0.0f;
    state->softPedal = 0.0f;
    state->sampleClock = 0;
    state->setTapeTarget (state->tapeTarget, false);
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

        const std::array<const float*, detail::numBridgeRegions> buses { s.regionBus[0].data(), s.regionBus[1].data(),
                                                                         s.regionBus[2].data(), s.regionBus[3].data() };
        s.soundboard.process (buses, left + position, right + position, stretch);

        // Tape-style saturation (PianoModel::tapeDrive): (tanh(d·x + b) − tanh b) / (d·(1 − tanh² b)),
        // unity gain for small signals, the bias giving even harmonics as well as odd. Written as
        // tanh(d·x) / (d·(1 + tanh b · tanh(d·x))), the same function, so tiny signals stay exact
        // instead of cancelling to zero in float. While the lab moves the drive, it glides.
        if (s.tapeDrive != s.tapeTarget)
        {
            s.glideTape (left + position, right + position, stretch);
        }
        else if (s.tapeDrive > 0.0)
        {
            const auto drive = static_cast<float> (s.tapeDrive);
            const float t = std::tanh (0.15f);
            for (float* channel : { left + position, right + position })
                for (int i = 0; i < stretch; ++i)
                {
                    const float u = std::tanh (drive * channel[i]);
                    channel[i] = u / (drive * (1.0f + t * u));
                }
        }
        position += stretch;
    }
}

void Engine::setListeningPerspective (ListeningPerspective perspective) noexcept
{
    state->soundboard.setPerspective (perspective == ListeningPerspective::pianist ? detail::pianistPerspective
                                                                                    : detail::microphonesPerspective);
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
    if (tape != s.tapeTarget)
        s.setTapeTarget (tape, true);
}

const lab::Settings& Engine::getVoicingLab() const noexcept
{
    return state->lab;
}
} // namespace forte
