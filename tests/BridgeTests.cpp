#include <juce_audio_basics/juce_audio_basics.h>

#include "forte/bridge/RenderBlock.h"

#include "support/AudioThreadGuard.h"

#include <catch2/catch_test_macros.hpp>

using forte::Engine;
using forte::test::heapUseOnAudioThread;

namespace
{
bool isSilent (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (buffer.getSample (channel, i) != 0.0f)
                return false;

    return true;
}

void fill (juce::AudioBuffer<float>& buffer, float value)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), value, buffer.getNumSamples());
}
} // namespace

TEST_CASE ("renderBlock writes every sample of every channel", "[bridge]")
{
    Engine engine;
    engine.prepare (48000.0, 256);

    juce::AudioBuffer<float> buffer (4, 256);
    fill (buffer, 1.0f);
    forte::bridge::renderBlock (engine, buffer, {});
    CHECK (isSilent (buffer));

    fill (buffer, 1.0f);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
    forte::bridge::renderBlock (engine, buffer, midi);

    for (int i = 0; i < 256; ++i)
    {
        INFO ("sample " << i);
        REQUIRE (buffer.getSample (0, i) != 1.0f);
        REQUIRE (buffer.getSample (1, i) == buffer.getSample (0, i));
        REQUIRE (buffer.getSample (2, i) == 0.0f);
        REQUIRE (buffer.getSample (3, i) == 0.0f);
    }
}

TEST_CASE ("renderBlock clears a buffer with too few channels", "[bridge]")
{
    Engine engine;
    engine.prepare (48000.0, 64);

    juce::AudioBuffer<float> mono (1, 64);
    fill (mono, 1.0f);

    forte::bridge::renderBlock (engine, mono, {});

    CHECK (isSilent (mono));
}

TEST_CASE ("renderBlock tolerates events outside the block", "[bridge]")
{
    Engine engine;
    engine.prepare (48000.0, 64);

    juce::AudioBuffer<float> buffer (2, 64);
    fill (buffer, 1.0f);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), -5);
    midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 500);
    midi.addEvent (juce::MidiMessage (0xf0, 0x7e, 0xf7), 10);  // sysex is skipped

    forte::bridge::renderBlock (engine, buffer, midi);

    // The early event is applied at sample 0; the late one is clamped to the block's end.
    CHECK (buffer.getSample (0, 0) != 0.0f);
    CHECK (buffer.getSample (0, 63) == buffer.getSample (1, 63));
}

TEST_CASE ("renderBlock never touches the heap", "[bridge][audio-thread]")
{
    Engine engine;
    engine.prepare (48000.0, 128);

    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;

    for (int i = 0; i < 88; ++i)
    {
        midi.addEvent (juce::MidiMessage::noteOn (1 + i % 16, 21 + i, (juce::uint8) 90), i);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, i), i);
    }

    const auto use = heapUseOnAudioThread ([&]
    {
        for (int block = 0; block < 100; ++block)
            forte::bridge::renderBlock (engine, buffer, midi);
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}
