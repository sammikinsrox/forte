// Tier 1 engine checks (SPEC §8.1) that run inside the unit-test executable. They're never
// waivable. The determinism check also runs end to end through forte-render (tests/CMakeLists.txt).
// Live changes are checked here under automation, and in PresetTests.cpp on a preset change.
//
// Still to come, as the parts they check arrive:
// - Velocity calibration: needs each note's fitted pp/mf/ff loudness (SPEC §3.3).
// - State compatibility: needs the saved state and the previous release's fixtures (SPEC §7.3).

#include "support/EngineRender.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace forte::test;

namespace
{
constexpr std::int64_t second = 48000;
} // namespace

TEST_CASE ("Overlapping note-ons never cut a note short", "[tier1][midi]")
{
    // Two note-ons for one key, on different channels as a piano roll might send them. The key
    // stays down until both have had their note-off.
    const auto note = GENERATE (21, 60, 90);
    const std::int64_t lastOff = second;

    const auto overlapping = renderFresh ({ noteOn (0, note, 90, 1),
                                            noteOn (second / 10, note, 70, 2),
                                            noteOff (second / 5, note, 1),
                                            noteOff (lastOff, note, 2) },
                                          2 * second).left;

    const auto held = renderFresh ({ noteOn (0, note, 90, 1), noteOn (second / 10, note, 70, 2) }, 2 * second).left;

    INFO ("note " << note);
    CHECK (slice (overlapping, 0, lastOff) == slice (held, 0, lastOff));

    // ...and the last note-off does release it.
    CHECK (rms (overlapping, lastOff + second / 4, 2 * second) < 0.1 * rms (held, lastOff + second / 4, 2 * second));
}

TEST_CASE ("A note's onset lands on its event's exact sample", "[tier1][midi]")
{
    const auto blockSize = GENERATE (1, 7, 32, 128, 500);
    const auto note = GENERATE (21, 60, 108);

    forte::Engine engine;
    engine.prepare (48000.0, blockSize);

    for (std::int64_t at : { 0, 1, 31, 32, 33, 127, 128, 129, 499, 500, 1001, 4800 })
    {
        for (int velocity : { 1, 64, 127 })
        {
            engine.reset();
            const auto out = render (engine, { noteOn (at, note, velocity) }, at + 256, blockSize);

            INFO ("block " << blockSize << ", note " << note << ", velocity " << velocity << ", event at " << at);
            REQUIRE (firstNonZero (out.left) == at);
        }
    }
}

TEST_CASE ("Renders are bit-identical whatever the block size", "[tier1][determinism]")
{
    // SPEC §7.4: an offline bounce renders bit-identically to real-time playback, and hosts use
    // different block sizes for the two. Everything control-rate in the engine runs on a fixed
    // grid of absolute sample time, so splitting the blocks differently changes nothing.
    std::vector<TimedMidi> events {
        controller (0, 64, 127),
        noteOn (0, 36, 100), noteOn (0, 48, 80), noteOn (5, 60, 64), noteOn (37, 108, 127),
        noteOff (1200, 36), noteOff (1201, 48),
        controller (2000, 64, 64),                            // half pedal
        noteOn (2001, 60, 30),                                // restrike
        noteOn (3333, 72, 90), noteOn (3333, 72, 50, 2),      // overlapping note-ons
        noteOff (4000, 72),
        controller (6000, 64, 0),
        noteOff (6400, 60), noteOff (6401, 72, 2),
        noteOn (9000, 84, 110), controller (9100, 123, 0),
        controller (9200, 64, 127), noteOn (9300, 30, 90), controller (12000, 121, 0),
        noteOn (15000, 66, 90), controller (20000, 120, 0),
        noteOn (20001, 77, 100),
    };

    constexpr std::int64_t length = second / 2;
    const auto reference = renderFresh (events, length, 128).left;

    REQUIRE_FALSE (allZero (reference));

    for (int blockSize : { 1, 7, 32, 100, 512, 4096 })
    {
        INFO ("block size " << blockSize);
        CHECK (renderFresh (events, length, blockSize).left == reference);
    }
}

TEST_CASE ("Automation while notes ring causes no level drop and no click", "[tier1][controls]")
{
    // SPEC §2.9, §8.1: every sound control but Volume moves at once, mid-chord, with the pedal
    // down. Ringing notes keep sounding and nothing steps.
    const std::vector<TimedMidi> chord { controller (0, 64, 127), noteOn (0, 33, 100), noteOn (0, 52, 90),
                                         noteOn (0, 60, 80), noteOn (0, 76, 80), noteOn (0, 91, 70) };
    constexpr std::int64_t at = second / 4, length = second;

    forte::Controls moved;
    moved.hardness = 80.0;
    moved.touch = 30.0;
    moved.brightness = 60.0;
    moved.resonance = 70.0;
    moved.width = 70.0;
    moved.tuning = { 442.0, 70.0, 60.0 };

    forte::Engine engine;
    engine.prepare (48000.0, 128);
    auto changed = render (engine, chord, at);
    engine.setControls (moved);
    const auto rest = render (engine, {}, length - at);
    changed.left.insert (changed.left.end(), rest.left.begin(), rest.left.end());

    const auto steady = renderFresh (chord, length).left;

    float changedStep = 0.0f, steadyStep = 0.0f;
    for (auto i = at - 1000; i < at + 6000; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        changedStep = std::max (changedStep, std::abs (changed.left[n] - changed.left[n - 1]));
        steadyStep = std::max (steadyStep, std::abs (steady[n] - steady[n - 1]));
    }

    REQUIRE (allFinite (changed.left));
    CHECK (changedStep <= 1.5f * steadyStep);
    CHECK (rms (changed.left, at, length) > 0.7 * rms (steady, at, length));
}

TEST_CASE ("Two renders of the same events are bit-identical", "[tier1][determinism]")
{
    const std::vector<TimedMidi> events { controller (0, 64, 127), noteOn (0, 40, 100), noteOn (100, 52, 60),
                                          noteOn (200, 64, 90), noteOn (3000, 76, 110), controller (9000, 64, 30) };

    CHECK (renderFresh (events, second / 2).left == renderFresh (events, second / 2).left);

    // A reused engine matches a fresh one after reset().
    forte::Engine engine;
    engine.prepare (48000.0, 128);
    (void) render (engine, events, second / 4);
    engine.reset();
    CHECK (render (engine, events, second / 2).left == renderFresh (events, second / 2).left);
}
