// MIDI handling (SPEC §4.2 pedal mapping, §4.3 notes and other MIDI).

#include "support/EngineRender.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace forte::test;

namespace
{
constexpr std::int64_t second = 48000;
constexpr int middleC = 60;

std::vector<float> play (const std::vector<TimedMidi>& events, std::int64_t length = second)
{
    return renderFresh (events, length).left;
}
} // namespace

// Test names stay ASCII: ctest passes them back to Catch2 as filters, and non-ASCII gets mangled.
TEST_CASE ("Notes outside A0-C8 are ignored", "[midi]")
{
    std::vector<TimedMidi> events;
    for (int note = 0; note <= 127; ++note)
        if (note < 21 || note > 108)
            events.push_back (noteOn (note, note, 100));

    for (int note = 0; note <= 127; ++note)
        if (note < 21 || note > 108)
            events.push_back (noteOff (200 + note, note));

    CHECK (allZero (play (events)));
}

TEST_CASE ("Every MIDI channel plays the same piano", "[midi]")
{
    const auto channel = GENERATE (2, 10, 16);
    CHECK (play ({ noteOn (0, middleC, 90, channel), noteOff (second / 4, middleC, channel) })
           == play ({ noteOn (0, middleC, 90), noteOff (second / 4, middleC) }));
}

TEST_CASE ("A note-on with velocity 0 is a note-off", "[midi]")
{
    CHECK (play ({ noteOn (0, middleC, 90), { second / 4, juce::MidiMessage (0x90, middleC, 0) } })
           == play ({ noteOn (0, middleC, 90), noteOff (second / 4, middleC) }));
}

TEST_CASE ("A note-off with no note-on is ignored", "[midi]")
{
    CHECK (play ({ noteOff (0, middleC), noteOn (100, middleC, 90) })
           == play ({ noteOn (100, middleC, 90) }));
}

TEST_CASE ("Pitch bend, mod wheel, aftertouch, sostenuto, program change and release velocity are ignored", "[midi]")
{
    const std::vector<TimedMidi> plain { noteOn (0, middleC, 90), noteOff (second / 2, middleC) };

    const std::vector<TimedMidi> busy {
        noteOn (0, middleC, 90),
        { 100, juce::MidiMessage::pitchWheel (1, 0) },
        controller (200, 1, 127),                                       // mod wheel
        { 300, juce::MidiMessage::aftertouchChange (1, middleC, 100) }, // polyphonic aftertouch
        { 400, juce::MidiMessage::channelPressureChange (1, 100) },
        controller (500, 66, 127),                                      // sostenuto
        { 600, juce::MidiMessage::programChange (1, 5) },
        { second / 2, juce::MidiMessage (0x80, middleC, 127) },         // release velocity 127
    };

    CHECK (play (busy) == play (plain));
}

TEST_CASE ("A released key's damper stops the note; a held key rings on", "[midi][damper]")
{
    const auto released = play ({ noteOn (0, middleC, 90), noteOff (second / 4, middleC) });
    const auto held = play ({ noteOn (0, middleC, 90) });

    CHECK (slice (released, 0, second / 4) == slice (held, 0, second / 4));
    CHECK (rms (released, second / 2, second) < 0.05 * rms (held, second / 2, second));
}

TEST_CASE ("The top notes have no dampers", "[midi][damper]")
{
    // SPEC §2.5: roughly the top 18 notes (G#6, MIDI 91, and up in the seed model).
    for (int note : { 91, 100, 108 })
    {
        INFO ("note " << note);
        CHECK (play ({ noteOn (0, note, 90), noteOff (second / 8, note) }) == play ({ noteOn (0, note, 90) }));
    }

    CHECK (play ({ noteOn (0, 90, 90), noteOff (second / 8, 90) }) != play ({ noteOn (0, 90, 90) }));
}

TEST_CASE ("The sustain pedal holds released notes", "[midi][pedal]")
{
    const auto pedalled = play ({ controller (0, 64, 127), noteOn (0, middleC, 90), noteOff (second / 4, middleC) });
    const auto held = play ({ controller (0, 64, 127), noteOn (0, middleC, 90) });

    CHECK (pedalled == held);
}

TEST_CASE ("Sustain pedal dead zones: 0-12 is resting, 116-127 is fully lifted", "[midi][pedal]")
{
    const auto withPedal = [] (int value)
    {
        return play ({ controller (0, 64, value), noteOn (0, middleC, 90), noteOff (second / 10, middleC) });
    };

    const auto resting = withPedal (0);
    const auto lifted = withPedal (127);
    const auto half = withPedal (64);

    CHECK (withPedal (12) == resting);
    CHECK (withPedal (116) == lifted);
    CHECK (withPedal (13) != resting);
    CHECK (withPedal (115) != lifted);

    // Half pedal sits in between.
    const auto tailRms = [] (const std::vector<float>& v) { return rms (v, second / 2, second); };
    CHECK (tailRms (resting) < tailRms (half));
    CHECK (tailRms (half) < tailRms (lifted));
}

TEST_CASE ("CC123 (All Notes Off) releases every key, and the sustain pedal still holds them", "[midi]")
{
    const auto chord = [] (std::int64_t at)
    {
        return std::vector<TimedMidi> { noteOn (at, 48, 90), noteOn (at, 60, 90), noteOn (at, 64, 90) };
    };

    auto offWithPedal = chord (0);
    offWithPedal.insert (offWithPedal.begin(), controller (0, 64, 127));
    auto heldWithPedal = offWithPedal;
    offWithPedal.push_back (controller (second / 4, 123, 0));

    CHECK (play (offWithPedal) == play (heldWithPedal));

    auto allOff = chord (0);
    allOff.push_back (controller (second / 4, 123, 0));

    auto eachOff = chord (0);
    for (int note : { 48, 60, 64 })
        eachOff.push_back (noteOff (second / 4, note));

    CHECK (play (allOff) == play (eachOff));
}

TEST_CASE ("CC120 (All Sound Off) silences everything at once", "[midi]")
{
    const std::int64_t at = second / 4 + 17;
    const auto out = play ({ controller (0, 64, 127), noteOn (0, 36, 110), noteOn (0, 60, 110), noteOn (0, 100, 110),
                             controller (at, 120, 0) });

    CHECK_FALSE (allZero (slice (out, 0, at)));
    CHECK (allZero (slice (out, at, second)));

    // Nothing is left behind to colour the next note. The first key is let go and its damper
    // has come down, or its sympathetic bank would rightly ring with the next note.
    const std::int64_t next = at + second / 20;
    CHECK (slice (play ({ noteOn (0, 60, 110), controller (at, 120, 0), noteOff (at, 60), noteOn (next, 64, 90) }), next, second)
           == slice (play ({ noteOn (next, 64, 90) }), next, second));
}

TEST_CASE ("CC121 (Reset All Controllers) sets the sustain pedal up", "[midi][pedal]")
{
    const std::vector<TimedMidi> start { controller (0, 64, 127), noteOn (0, middleC, 90), noteOff (second / 10, middleC) };

    auto reset = start;
    reset.push_back (controller (second / 2, 121, 0));
    auto pedalUp = start;
    pedalUp.push_back (controller (second / 2, 64, 0));

    CHECK (play (reset) == play (pedalUp));
}
