#include "forte/engine/Engine.h"

#include "support/AudioThreadGuard.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <vector>
#include <xmmintrin.h>

using forte::Engine;
using forte::MidiEvent;
using forte::test::heapUseOnAudioThread;

namespace
{
MidiEvent shortMessage (std::uint8_t status, std::uint8_t data1, std::uint8_t data2)
{
    MidiEvent event;
    event.bytes[0] = status;
    event.bytes[1] = data1;
    event.bytes[2] = data2;
    event.size = 3;
    return event;
}

bool allZero (const std::vector<float>& samples)
{
    return std::all_of (samples.begin(), samples.end(), [] (float s) { return s == 0.0f; });
}
} // namespace

TEST_CASE ("Engine renders silence", "[engine]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);

    Engine engine;
    engine.prepare (sampleRate, 512);

    std::vector<float> left (512, 1.0f);
    std::vector<float> right (512, -1.0f);

    engine.handleMidi (shortMessage (0x90, 60, 100));
    engine.render (left.data(), right.data(), 512);

    CHECK (allZero (left));
    CHECK (allZero (right));
}

TEST_CASE ("Engine render of zero samples writes nothing", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 128);

    std::vector<float> left (4, 1.0f);
    std::vector<float> right (4, 1.0f);
    engine.render (left.data(), right.data(), 0);

    CHECK (left == std::vector<float> (4, 1.0f));
    CHECK (right == std::vector<float> (4, 1.0f));
}

TEST_CASE ("Engine audio-thread calls never touch the heap", "[engine][audio-thread]")
{
    Engine engine;
    engine.prepare (48000.0, 1024);  // prepare() may allocate; it runs off the audio thread

    std::vector<float> left (1024);
    std::vector<float> right (1024);

    const auto use = heapUseOnAudioThread ([&]
    {
        // A full 88-key glissando with the sustain pedal, in blocks of uneven sizes.
        engine.handleMidi (shortMessage (0xb0, 64, 127));

        for (std::uint8_t note = 21; note <= 108; ++note)
        {
            engine.handleMidi (shortMessage (0x90, note, static_cast<std::uint8_t> (1 + note % 127)));
            engine.render (left.data(), right.data(), 1 + (note * 37) % 1024);
            engine.handleMidi (shortMessage (0x80, note, 0));
        }

        engine.handleMidi (shortMessage (0xb0, 64, 0));
        engine.handleMidi (shortMessage (0xb0, 120, 0));
        engine.render (left.data(), right.data(), 1024);
        engine.reset();
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}

TEST_CASE ("Engine restores the caller's floating-point state after rendering", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 64);
    std::vector<float> left (64);
    std::vector<float> right (64);

    const auto before = _mm_getcsr();
    engine.render (left.data(), right.data(), 64);

    CHECK (_mm_getcsr() == before);
}
