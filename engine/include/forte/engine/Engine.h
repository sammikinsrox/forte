#pragma once

#include "forte/engine/MidiEvent.h"

#include <memory>

namespace forte
{
/** The Forte piano engine: 88 permanent note models and the MIDI that plays them (SPEC §2, §4).

    Threading contract:
    - prepare() runs off the audio thread, while audio is stopped. It may allocate.
    - reset(), handleMidi() and render() run on the audio thread. They never allocate, lock or block.

    Sample-accurate timing (SPEC §4.3) is the caller's job: render up to an event's offset,
    hand the event over, then carry on rendering. See forte::bridge::renderBlock().

    Output depends only on the events and their sample times, never on how the samples are split
    into render() calls, so offline and realtime renders are bit-identical (SPEC §7.4).

    For now the output is the strings' summed bridge force on both channels. The soundboard,
    sympathetic bank and output controls come later (SPEC §2.1).
*/
class Engine
{
public:
    static constexpr int numOutputChannels = 2;

    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    /** Rebuilds the engine for a sample rate and maximum block size, then resets it.
        Not realtime-safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Returns to the just-prepared state: silent, every key up and both pedals up. Realtime-safe. */
    void reset() noexcept;

    /** Applies one MIDI message at the current render position (SPEC §4). Realtime-safe. */
    void handleMidi (const MidiEvent& event) noexcept;

    /** Renders numSamples of stereo output, overwriting left and right. Realtime-safe. */
    void render (float* left, float* right, int numSamples) noexcept;

    /** The longest a note can go on sounding after the last MIDI event: an undamped (or
        pedalled) note decaying from full scale to rest at the piano model's slowest decay.
        Depends only on the piano model, not on the sample rate. Safe from any thread. */
    [[nodiscard]] double getTailSeconds() const noexcept;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    struct State;

    double sampleRate = 48000.0;
    int maxBlockSize = 0;
    std::unique_ptr<State> state;
};
} // namespace forte
