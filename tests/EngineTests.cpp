#include "support/AudioThreadGuard.h"
#include "support/EngineRender.h"

#include "NoteModel.h"
#include "PianoModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>
#include <xmmintrin.h>

using forte::Engine;
using forte::MidiEvent;
using forte::test::heapUseOnAudioThread;
using namespace forte::test;

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

constexpr std::int64_t second = 48000;
} // namespace

TEST_CASE ("Engine is silent until a key is struck", "[engine]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);

    Engine engine;
    engine.prepare (sampleRate, 512);

    std::vector<float> left (512, 1.0f);
    std::vector<float> right (512, -1.0f);
    engine.render (left.data(), right.data(), 512);

    CHECK (allZero (left));
    CHECK (allZero (right));
}

TEST_CASE ("A struck note sounds, the same on both channels", "[engine]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    const auto note = GENERATE (21, 60, 108);

    const auto out = renderFresh ({ noteOn (0, note, 100) }, static_cast<std::int64_t> (sampleRate / 4), 256, sampleRate);

    CHECK (out.left == out.right);
    CHECK (allFinite (out.left));
    CHECK (peak (out.left) > 1e-3f);
    CHECK (peak (out.left) < 1.0f);
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
        // A full 88-key glissando with the sustain pedal, in blocks of uneven sizes, with
        // restrikes, overlapping note-ons, half-pedal and every special controller.
        engine.handleMidi (shortMessage (0xb0, 64, 127));

        for (std::uint8_t note = 21; note <= 108; ++note)
        {
            engine.handleMidi (shortMessage (0x90, note, static_cast<std::uint8_t> (1 + note % 127)));
            engine.handleMidi (shortMessage (0x91, note, 127));
            engine.render (left.data(), right.data(), 1 + (note * 37) % 1024);
            engine.handleMidi (shortMessage (0x80, note, 0));
            engine.handleMidi (shortMessage (0xb0, 64, note));
            engine.handleMidi (shortMessage (0xb0, 67, static_cast<std::uint8_t> (127 - note)));
        }

        engine.handleMidi (shortMessage (0x90, 10, 100));   // off the keyboard
        engine.handleMidi (shortMessage (0xe0, 0, 64));     // pitch bend
        engine.handleMidi (shortMessage (0xb0, 123, 0));
        engine.handleMidi (shortMessage (0xb0, 121, 0));
        engine.render (left.data(), right.data(), 1024);
        engine.handleMidi (shortMessage (0xb0, 120, 0));
        engine.render (left.data(), right.data(), 1024);
        engine.reset();
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}

TEST_CASE ("Engine restores the caller's floating-point state", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 64);
    std::vector<float> left (64);
    std::vector<float> right (64);

    const auto before = _mm_getcsr();
    engine.handleMidi (shortMessage (0x90, 60, 100));
    CHECK (_mm_getcsr() == before);

    engine.render (left.data(), right.data(), 64);
    CHECK (_mm_getcsr() == before);
}

TEST_CASE ("Hammer is stable on every note at maximum velocity at every sample rate", "[engine][hammer]")
{
    // SPEC §2.4. The hardest felt arrives with the Hardness control; this runs the seed felt.
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);

    Engine engine;
    engine.prepare (sampleRate, 512);

    for (int note = 21; note <= 108; ++note)
    {
        engine.reset();
        const auto out = render (engine, { noteOn (0, note, 127) }, static_cast<std::int64_t> (sampleRate * 0.05), 512);

        INFO ("note " << note << " at " << sampleRate << " Hz");
        REQUIRE (allFinite (out.left));
        REQUIRE (peak (out.left) < 1.0f);
        REQUIRE (peak (out.left) > 1e-3f);
    }
}

TEST_CASE ("A released note goes idle and renders exact silence", "[engine]")
{
    // SPEC §2.2: modes stop below −100 dB, and the note stops once they all have.
    const auto out = renderFresh ({ noteOn (0, 84, 100), noteOff (second / 10, 84) }, 3 * second);

    CHECK_FALSE (allZero (slice (out.left, 0, second / 10)));
    CHECK (allZero (slice (out.left, 2 * second, 3 * second)));
}

TEST_CASE ("The reported tail covers the longest-ringing note", "[engine]")
{
    // The longest-ringing note is the one whose slowest coupled mode takes longest to rest.
    // With the pedal down, struck as hard as possible and released at once, with or without the
    // soft pedal, it must be silent by the tail's end. And the tail isn't padded: the note still
    // sounds a third of the way through.
    constexpr double sampleRate = 44100.0;

    const auto model = forte::detail::makeSeedPianoModel();
    int longest = forte::detail::lowestNote;
    double longestTail = 0.0;

    for (int note = forte::detail::lowestNote; note <= forte::detail::highestNote; ++note)
    {
        const double noteTail = forte::detail::noteTailSeconds (model.note (note), model.unisonSpreadCents);
        if (noteTail > longestTail)
        {
            longestTail = noteTail;
            longest = note;
        }
    }

    Engine engine;
    engine.prepare (sampleRate, 4096);
    const double tail = engine.getTailSeconds();
    REQUIRE (tail == longestTail);
    REQUIRE (tail > 10.0);
    REQUIRE (tail < 120.0);

    const auto length = static_cast<std::int64_t> (tail * sampleRate);
    bool soundsAtAThird = false;

    for (int soft : { 0, 127 })
    {
        engine.reset();
        const auto out = render (engine, { controller (0, 64, 127), controller (0, 67, soft), noteOn (0, longest, 127), noteOff (1, longest) },
                                 length, 4096);

        INFO ("note " << longest << ", soft pedal " << soft << ", tail " << tail << " s");
        CHECK (allZero (slice (out.left, length - 4096, length)));
        soundsAtAThird = soundsAtAThird || ! allZero (slice (out.left, length / 3, length / 3 + 4096));
    }

    CHECK (soundsAtAThird);
}

TEST_CASE ("A restrike strikes the strings that are still ringing", "[engine]")
{
    // SPEC §2.2: every note-on is a restrike of the note's current string state.
    const std::int64_t restrike = second / 2;
    const auto twice = renderFresh ({ noteOn (0, 60, 100), noteOn (restrike, 60, 100) }, second);
    const auto once = renderFresh ({ noteOn (restrike, 60, 100) }, second);

    CHECK (slice (twice.left, restrike, second) != slice (once.left, restrike, second));
    CHECK (allFinite (twice.left));
}
