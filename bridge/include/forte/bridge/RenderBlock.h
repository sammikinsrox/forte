#pragma once

// Include after the JUCE module headers (juce_audio_basics at least).

#include "forte/engine/Engine.h"

#include <algorithm>

namespace forte::bridge
{
/** Renders one block: audio up to each MIDI event's sample offset, then the event, and so on
    to the end of the block. Short messages only; sysex and meta events are skipped.

    Writes the engine's stereo output to channels 0 and 1 and clears any others. If the buffer
    has fewer than two channels it's cleared and the engine isn't called. Realtime-safe.
*/
inline void renderBlock (Engine& engine, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < Engine::numOutputChannels)
    {
        buffer.clear();
        return;
    }

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    int position = 0;

    for (const auto metadata : midi)
    {
        const int offset = std::clamp (metadata.samplePosition, position, numSamples);

        if (offset > position)
        {
            engine.render (left + position, right + position, offset - position);
            position = offset;
        }

        if (metadata.numBytes < 1 || metadata.numBytes > 3 || metadata.data[0] < 0x80 || metadata.data[0] >= 0xf0)
            continue;

        MidiEvent event;
        event.size = static_cast<std::uint8_t> (metadata.numBytes);
        std::copy_n (metadata.data, metadata.numBytes, event.bytes);
        engine.handleMidi (event);
    }

    if (position < numSamples)
        engine.render (left + position, right + position, numSamples - position);

    for (int channel = Engine::numOutputChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
}
} // namespace forte::bridge
