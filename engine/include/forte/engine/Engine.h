#pragma once

#include "forte/engine/MidiEvent.h"

namespace forte
{
/** The Forte piano engine.

    Threading contract:
    - prepare() runs off the audio thread, while audio is stopped. It may allocate.
    - handleMidi() and render() run on the audio thread. They never allocate, lock or block.

    Sample-accurate timing (SPEC §4.3) is the caller's job: render up to an event's offset,
    hand the event over, then carry on rendering. See forte::bridge::renderBlock().

    For now the engine is silent: render() writes zeros.
*/
class Engine
{
public:
    static constexpr int numOutputChannels = 2;

    Engine() = default;
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    /** Rebuilds the engine for a sample rate and maximum block size. Not realtime-safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Silences everything immediately, as CC120 does. Realtime-safe. */
    void reset() noexcept;

    /** Applies one MIDI message at the current render position. Realtime-safe. */
    void handleMidi (const MidiEvent& event) noexcept;

    /** Renders numSamples of stereo output, overwriting left and right. Realtime-safe. */
    void render (float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    double sampleRate = 48000.0;
    int maxBlockSize = 0;
};
} // namespace forte
