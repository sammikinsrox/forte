#include "forte/engine/Engine.h"

#include "NoteModel.h"
#include "PianoModel.h"
#include "ScopedFlushDenormals.h"
#include "Soundboard.h"
#include "SoundboardIrs.h"
#include "VelocityCurve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace forte
{
namespace
{
constexpr int controlInterval = detail::NoteModel::controlInterval;
constexpr int partitionSize = detail::Soundboard::partitionSize;
constexpr double pedalSmoothingSeconds = 0.02;  // SPEC §4.2
constexpr double lowestSampleRate = 44100.0;    // SPEC §2.2

static_assert (partitionSize % controlInterval == 0, "a partition holds whole control intervals");

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

    /** The notes' bridge force per region, for the current stretch of one partition. */
    std::array<std::array<float, partitionSize>, detail::numBridgeRegions> regionBus {};

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
            case cc::allSoundOff:         for (auto& n : notes) n.silence(); soundboard.silence(); break;
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

        for (auto& n : notes)
            n.controlTick (sustainLift);
    }
};

Engine::Engine()
    : state (std::make_unique<State>())
{
    double notesTail = 0.0;

    for (const auto& note : state->model.notes)
    {
        notesTail = std::max (notesTail, detail::noteTailSeconds (note, state->model.unisonSpreadCents));
        state->noteRegion[static_cast<std::size_t> (note.midiNote - detail::lowestNote)] = static_cast<std::uint8_t> (note.bridgeRegion);
    }

    // The soundboard rings on for the IR's length after the strings stop. The convolution's
    // partition grid can carry rounding residue up to two partitions further, at most this long
    // at the lowest host rate.
    const double irSeconds = static_cast<double> (state->shippedIrs.length()) / state->shippedIrs.sampleRate;
    state->tailSeconds = notesTail + irSeconds + 2.0 * partitionSize / lowestSampleRate;
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

    state->soundboard.prepare (detail::resampleIrs (state->shippedIrs, sampleRate));

    state->sustainStepPerTick = static_cast<float> (controlInterval / (pedalSmoothingSeconds * sampleRate));
    reset();
}

void Engine::reset() noexcept
{
    for (auto& n : state->notes)
        n.reset();

    state->soundboard.reset();
    state->keyDownCount.fill (0);
    state->sustainTarget = state->sustainLift = 0.0f;
    state->softPedal = 0.0f;
    state->sampleClock = 0;
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

            for (std::size_t i = 0; i < s.notes.size(); ++i)
                s.notes[i].render (s.regionBus[s.noteRegion[i]].data() + done, s.noteScratch.data(), length);

            done += length;
            s.sampleClock += length;
        }

        const std::array<const float*, detail::numBridgeRegions> buses { s.regionBus[0].data(), s.regionBus[1].data(),
                                                                         s.regionBus[2].data(), s.regionBus[3].data() };
        s.soundboard.process (buses, left + position, right + position, stretch);
        position += stretch;
    }
}

void Engine::setListeningPerspective (ListeningPerspective perspective) noexcept
{
    state->soundboard.setPerspective (perspective == ListeningPerspective::pianist ? detail::pianistPerspective
                                                                                    : detail::microphonesPerspective);
}
} // namespace forte
