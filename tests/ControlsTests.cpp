// The sound controls (SPEC §2.9, §6.3) as the engine applies them.

#include "support/AudioThreadGuard.h"
#include "support/EngineRender.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>

using forte::Controls;
using forte::Engine;
using namespace forte::test;

namespace
{
constexpr std::int64_t second = 48000;

/** Power of a sine at frequency in v[from, to), by correlation. */
double powerAt (const std::vector<float>& v, std::int64_t from, std::int64_t to, double frequency, double sampleRate = 48000.0)
{
    double re = 0.0, im = 0.0;
    for (auto i = from; i < to; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate;
        re += v[static_cast<std::size_t> (i)] * std::cos (phase);
        im += v[static_cast<std::size_t> (i)] * std::sin (phase);
    }
    return re * re + im * im;
}

float largestStep (const std::vector<float>& v, std::int64_t from, std::int64_t to)
{
    float step = 0.0f;
    for (auto i = from + 1; i < to; ++i)
        step = std::max (step, std::abs (v[static_cast<std::size_t> (i)] - v[static_cast<std::size_t> (i - 1)]));
    return step;
}

/** Renders events with controls changed to `after` at sample `at` (a block boundary). */
Rendered renderWithChange (const std::vector<TimedMidi>& events, std::int64_t length, const Controls& before,
                           const Controls& after, std::int64_t at)
{
    Engine engine;
    engine.setControls (before);
    engine.prepare (48000.0, 128);

    std::vector<TimedMidi> first, second_;
    for (const auto& e : events)
        (e.sample < at ? first : second_).push_back (e);
    for (auto& e : second_)
        e.sample -= at;

    auto out = render (engine, first, at);
    engine.setControls (after);
    const auto rest = render (engine, second_, length - at);
    out.left.insert (out.left.end(), rest.left.begin(), rest.left.end());
    out.right.insert (out.right.end(), rest.right.begin(), rest.right.end());
    return out;
}

const std::vector<TimedMidi> pedalledChord { controller (0, 64, 127), noteOn (0, 36, 100), noteOn (0, 60, 90),
                                             noteOn (0, 67, 80), noteOn (0, 84, 80), noteOn (0, 96, 70) };
} // namespace

TEST_CASE ("The controls at their defaults change nothing", "[controls]")
{
    const auto plain = renderFresh (pedalledChord, second / 2);

    Engine engine;
    engine.setControls (Controls {});
    engine.prepare (48000.0, 128);
    CHECK (render (engine, pedalledChord, second / 2).left == plain.left);

    // Set away and back before prepare(): nothing left over.
    Engine moved;
    Controls far;
    far.hardness = 0.0;
    far.brightness = 100.0;
    far.resonance = 0.0;
    far.width = 0.0;
    far.volumeDb = -60.0;
    far.tuning = { 415.0, 100.0, 0.0 };
    moved.setControls (far);
    moved.setControls (Controls {});
    moved.prepare (48000.0, 128);
    CHECK (render (moved, pedalledChord, second / 2).left == plain.left);
}

TEST_CASE ("A4 itself sounds the same at any Stretch, and the treble doesn't", "[controls][tuning]")
{
    // Resonance 0: the undamped treble keys' sympathetic strings would ring with A4, and they're
    // stretched.
    Controls stretched;
    stretched.resonance = 0.0;
    Controls flat = stretched;
    flat.tuning.stretch = 0.0;

    const auto render69 = [&] (const Controls& c, int note)
    {
        Engine engine;
        engine.setControls (c);
        engine.prepare (48000.0, 128);
        return render (engine, { noteOn (0, note, 90) }, second / 4).left;
    };

    CHECK (render69 (flat, 69) == render69 (stretched, 69));
    CHECK (render69 (flat, 100) != render69 (stretched, 100));
}

TEST_CASE ("A ringing note glides to a new A4 without a click", "[controls][tuning]")
{
    // A4 = 440 → 466 while A4 rings (key held). After the ~50 ms glide the note sounds at 466 Hz.
    Controls sharp;
    sharp.tuning.a4Hz = 466.0;
    const std::int64_t at = second / 4;
    const std::int64_t length = second;
    const auto out = renderWithChange ({ noteOn (0, 69, 90) }, length, Controls {}, sharp, at);
    const auto held = renderFresh ({ noteOn (0, 69, 90) }, length);

    REQUIRE (allFinite (out.left));
    const std::int64_t from = second / 2, to = length;
    INFO ("440 Hz: " << powerAt (out.left, from, to, 440.0) << ", 466 Hz: " << powerAt (out.left, from, to, 466.0));
    CHECK (powerAt (out.left, from, to, 466.0) > 30.0 * powerAt (out.left, from, to, 440.0));
    CHECK (powerAt (held.left, from, to, 440.0) > 30.0 * powerAt (held.left, from, to, 466.0));

    // No step at the change bigger than the note's own.
    CHECK (largestStep (out.left, at - 2000, at + 4000) <= 1.2f * largestStep (held.left, at - 2000, at + 4000));
}

TEST_CASE ("Volume scales the output after the tape, and -60 dB is silence", "[controls]")
{
    const auto plain = renderFresh (pedalledChord, second / 2);

    Controls quiet;
    quiet.volumeDb = -20.0;
    Engine engine;
    engine.setControls (quiet);
    engine.prepare (48000.0, 128);
    const auto out = render (engine, pedalledChord, second / 2);

    for (std::size_t i = 0; i < out.left.size(); ++i)
        REQUIRE (std::abs (out.left[i] - 0.1f * plain.left[i]) <= 1e-6f * peak (plain.left));

    Controls off;
    off.volumeDb = -60.0;
    const auto silent = renderWithChange (pedalledChord, second / 2, Controls {}, off, second / 8);
    CHECK (allZero (slice (silent.left, second / 8 + second / 40, second / 2)));
    CHECK_FALSE (allZero (slice (silent.left, 0, second / 8)));
}

TEST_CASE ("Width 0 is mono, gliding there from full width", "[controls]")
{
    Controls mono;
    mono.width = 0.0;
    const auto out = renderWithChange (pedalledChord, second / 2, Controls {}, mono, second / 8);
    const auto plain = renderFresh (pedalledChord, second / 2);

    CHECK (slice (out.left, 0, second / 8) == slice (plain.left, 0, second / 8));
    const auto settled = second / 8 + second / 40;  // past the ~20 ms glide
    for (auto i = settled; i < second / 2; ++i)
    {
        const auto at = static_cast<std::size_t> (i);
        REQUIRE (out.left[at] == out.right[at]);
        REQUIRE (std::abs (out.left[at] - 0.5f * (plain.left[at] + plain.right[at])) <= 1e-6f * peak (plain.left));
    }
}

TEST_CASE ("Hardness and Touch change the next strike only", "[controls]")
{
    const std::vector<TimedMidi> twice { noteOn (0, 60, 90), noteOn (second / 4, 64, 90) };

    Controls hard;
    hard.hardness = 100.0;
    hard.touch = 80.0;
    const auto changed = renderWithChange (twice, second / 2, Controls {}, hard, second / 8);
    const auto plain = renderFresh (twice, second / 2);

    // The ringing C4 doesn't change; the E4 struck after does.
    CHECK (slice (changed.left, 0, second / 4) == slice (plain.left, 0, second / 4));
    CHECK (slice (changed.left, second / 4, second / 2) != slice (plain.left, second / 4, second / 2));
}

TEST_CASE ("Hammer is stable on every note at maximum velocity with the hardest felt", "[controls][hammer]")
{
    // SPEC §2.4: every note at velocity 127, Hardness 100, at every supported rate.
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);

    Controls hardest;
    hardest.hardness = 100.0;
    Engine engine;
    engine.setControls (hardest);
    engine.prepare (sampleRate, 512);

    for (int note = 21; note <= 108; ++note)
    {
        engine.reset();
        const auto out = render (engine, { noteOn (0, note, 127) }, static_cast<std::int64_t> (sampleRate * 0.05), 512);

        INFO ("note " << note << " at " << sampleRate << " Hz");
        REQUIRE (allFinite (out.left));
        REQUIRE (allFinite (out.right));
        REQUIRE (std::max (peak (out.left), peak (out.right)) < 1.0f);
    }
}

TEST_CASE ("Resonance 0 removes the sympathetic strings, 100 doubles them", "[controls]")
{
    // A struck chord with the pedal down, then a second chord's keys' strings left ringing alone.
    const auto level = [] (double resonance)
    {
        Controls c;
        c.resonance = resonance;
        Engine engine;
        engine.setControls (c);

        // No tape, so the output is linear in the bank's level.
        forte::lab::Settings noTape;
        noTape.set (forte::lab::Control::tapeDrive, forte::lab::Register::bass, 0.0f);
        engine.setVoicingLab (noTape);
        engine.prepare (48000.0, 128);
        return render (engine, pedalledChord, second / 2).left;
    };

    const auto none = level (0.0), physical = level (50.0), doubled = level (100.0);
    CHECK (none != physical);
    CHECK (doubled != physical);

    // The bank's share: physical − none, and doubled − none is twice it.
    double share = 0.0, twice = 0.0;
    for (std::size_t i = 0; i < none.size(); ++i)
    {
        share += std::pow (physical[i] - none[i], 2.0);
        twice += std::pow (doubled[i] - none[i], 2.0);
    }
    CHECK (std::sqrt (twice / share) > 1.9);
    CHECK (std::sqrt (twice / share) < 2.1);
}

TEST_CASE ("Spread waits for the strings to be rebuilt, then for a note's next start from rest", "[controls][tuning]")
{
    Engine engine;
    engine.prepare (48000.0, 128);

    Controls unison;
    unison.tuning.spread = 0.0;
    engine.setControls (unison);
    CHECK (engine.getLatestTuning().spread == 0.0);

    const auto before = render (engine, { noteOn (0, 60, 90), noteOff (second / 8, 60) }, second).left;
    CHECK (before == renderFresh ({ noteOn (0, 60, 90), noteOff (second / 8, 60) }, second).left);

    // The notes with a unison (2 or 3 strings) get new strings.
    CHECK (engine.rebuildStrings (engine.getVoicingLab()) == 78);
    CHECK (engine.rebuildStrings (engine.getVoicingLab()) == 0);

    engine.reset();
    const auto after = render (engine, { noteOn (0, 60, 90) }, second / 2).left;
    CHECK (after != slice (before, 0, second / 2));
}

TEST_CASE ("The controls never touch the heap on the audio thread", "[controls][audio-thread]")
{
    Engine engine;
    engine.prepare (48000.0, 512);
    std::vector<float> left (512), right (512);

    const auto use = forte::test::heapUseOnAudioThread ([&]
    {
        forte::MidiEvent pedal;
        pedal.bytes[0] = 0xb0;
        pedal.bytes[1] = 64;
        pedal.bytes[2] = 127;
        pedal.size = 3;
        engine.handleMidi (pedal);

        for (int note = 21; note <= 108; ++note)
        {
            forte::MidiEvent on;
            on.bytes[0] = 0x90;
            on.bytes[1] = static_cast<std::uint8_t> (note);
            on.bytes[2] = 100;
            on.size = 3;
            engine.handleMidi (on);

            Controls c;
            c.hardness = note % 100;
            c.brightness = (note * 7) % 101;
            c.resonance = (note * 3) % 101;
            c.touch = (note * 11) % 101;
            c.width = (note * 13) % 101;
            c.volumeDb = -60.0 + (note % 66);
            c.tuning = { 415.0 + (note % 52), static_cast<double> ((note * 5) % 101), static_cast<double> ((note * 17) % 101) };
            engine.setControls (c);
            engine.render (left.data(), right.data(), 1 + (note * 37) % 512);
        }

        engine.reset();
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}
