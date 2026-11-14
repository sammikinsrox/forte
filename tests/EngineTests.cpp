#include "support/AudioThreadGuard.h"
#include "support/EngineRender.h"

#include "NoteModel.h"
#include "PianoModel.h"
#include "Soundboard.h"
#include "SoundboardIrs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
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

TEST_CASE ("A struck note sounds on both channels, bass to the left and treble to the right", "[engine]")
{
    // SPEC §2.8: placement comes from the bridge regions' IRs, with bass on the left.
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    const auto note = GENERATE (21, 60, 108);

    const auto length = static_cast<std::int64_t> (sampleRate / 4);
    const auto out = renderFresh ({ noteOn (0, note, 100) }, length, 256, sampleRate);

    INFO ("note " << note << " at " << sampleRate << " Hz");
    for (const auto* channel : { &out.left, &out.right })
    {
        CHECK (allFinite (*channel));
        CHECK (peak (*channel) > 1e-3f);
        CHECK (peak (*channel) < 1.0f);
    }

    // Single notes scatter around their region's place, by up to ~10 dB with the seed IRs: each
    // channel's modes are independent, so a partial can sit on a strong mode in one only.
    const double balanceDb = 20.0 * std::log10 (rms (out.left, 0, length) / rms (out.right, 0, length));
    if (note == 21)
        CHECK (balanceDb > 3.0);
    if (note == 108)
        CHECK (balanceDb < -3.0);
}

TEST_CASE ("The Listening perspective selects the IR set, from the next partition, without a jump", "[engine][soundboard]")
{
    const std::vector<TimedMidi> chord { controller (0, 64, 127), noteOn (0, 36, 100), noteOn (0, 60, 90), noteOn (0, 84, 80) };
    const std::int64_t length = second / 2;

    const auto microphones = renderFresh (chord, length);

    Engine engine;
    engine.prepare (48000.0, 128);
    engine.setListeningPerspective (forte::ListeningPerspective::pianist);
    engine.reset();  // a switch with nothing playing applies at once
    const auto pianist = render (engine, chord, length);

    CHECK (slice (microphones.left, 0, length) != slice (pianist.left, 0, length));
    CHECK (rms (pianist.left, 0, length) > 0.5 * rms (microphones.left, 0, length));
    CHECK (rms (pianist.left, 0, length) < 2.0 * rms (microphones.left, 0, length));

    // A switch mid-render: nothing changes until the next 128-sample boundary, then a crossfade
    // with no step larger than the signal's own.
    engine.setListeningPerspective (forte::ListeningPerspective::microphones);
    engine.reset();
    const std::int64_t switchAt = 10000;  // not on a boundary: the fade starts at 10112
    auto first = render (engine, chord, switchAt);
    engine.setListeningPerspective (forte::ListeningPerspective::pianist);

    std::vector<float> left (static_cast<std::size_t> (length - switchAt)), right (left.size());
    engine.render (left.data(), right.data(), static_cast<int> (left.size()));
    first.left.insert (first.left.end(), left.begin(), left.end());

    // The fade starts at gain 0, so 10112 itself is still all Microphones; it lasts 19
    // partitions (~50 ms), after which the output is exactly Pianist's.
    const std::int64_t fadeEnd = 10112 + 19 * 128;
    CHECK (slice (first.left, 0, 10113) == slice (microphones.left, 0, 10113));
    CHECK (slice (first.left, 10113, 10200) != slice (microphones.left, 10113, 10200));
    CHECK (slice (first.left, fadeEnd - 128, fadeEnd) != slice (pianist.left, fadeEnd - 128, fadeEnd));
    CHECK (slice (first.left, fadeEnd, length) == slice (pianist.left, fadeEnd, length));

    float largestStep = 0.0f, largestNaturalStep = 0.0f;
    for (std::size_t i = 9000; i < 14000; ++i)
    {
        largestStep = std::max (largestStep, std::abs (first.left[i] - first.left[i - 1]));
        largestNaturalStep = std::max ({ largestNaturalStep, std::abs (microphones.left[i] - microphones.left[i - 1]),
                                         std::abs (pianist.left[i] - pianist.left[i - 1]) });
    }
    CHECK (largestStep <= 1.05f * largestNaturalStep);
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
            if (note % 9 == 0)  // perspective crossfades, some reversed mid-fade
                engine.setListeningPerspective (note % 2 == 0 ? forte::ListeningPerspective::pianist
                                                              : forte::ListeningPerspective::microphones);

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

    // Then the soundboard rings on for its IR, plus two partitions of rounding at 44.1 kHz.
    const auto irs = forte::detail::loadEmbeddedIrs();
    const double irSeconds = static_cast<double> (irs.length()) / irs.sampleRate;

    Engine engine;
    engine.prepare (sampleRate, 4096);
    const double tail = engine.getTailSeconds();
    REQUIRE (tail == longestTail + irSeconds + 2.0 * forte::detail::Soundboard::partitionSize / 44100.0);
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
