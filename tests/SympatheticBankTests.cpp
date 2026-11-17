#include "support/EngineRender.h"

#include "NoteModel.h"
#include "PianoModel.h"
#include "SympatheticBank.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using forte::detail::SympatheticBank;
using namespace forte::test;

namespace
{
constexpr double rate = 48000.0;
constexpr std::int64_t second = 48000;
constexpr int interval = forte::detail::NoteModel::controlInterval;

int keyOf (int midiNote) { return midiNote - forte::detail::lowestNote; }

/** Runs one key's bank over drive in control intervals, as the engine does, with its damper at
    lift and every other key's resting (undamped keys included, so they stay out of it). drive
    may be shorter than numSamples; the rest is silence (nullptr drive). */
std::vector<float> runBank (SympatheticBank& bank, int key, const std::vector<float>& drive, std::int64_t numSamples,
                            float lift, const std::vector<float>* own = nullptr)
{
    const auto model = forte::detail::makeSeedPianoModel();
    const auto region = static_cast<std::size_t> (model.notes[static_cast<std::size_t> (key)].bridgeRegion);

    std::vector<float> out (static_cast<std::size_t> (numSamples), 0.0f);
    std::array<float, interval> elsewhere {};

    for (std::int64_t at = 0; at < numSamples; at += interval)
    {
        for (int k = 0; k < forte::detail::numNotes; ++k)
            bank.controlTick (k, k == key ? lift : 0.0f);

        const auto n = static_cast<int> (std::min<std::int64_t> (interval, numSamples - at));
        const bool driven = at < static_cast<std::int64_t> (drive.size());

        std::array<const float*, forte::detail::numNotes> owns {};
        if (driven && own != nullptr)
            owns[static_cast<std::size_t> (key)] = own->data() + at;

        std::array<float*, forte::detail::numBridgeRegions> buses { elsewhere.data(), elsewhere.data(), elsewhere.data(), elsewhere.data() };
        buses[region] = out.data() + at;

        bank.render (driven ? drive.data() + at : nullptr, owns, buses, n);
    }

    REQUIRE (std::all_of (elsewhere.begin(), elsewhere.end(), [] (float s) { return s == 0.0f; }));
    return out;
}

std::vector<float> sine (double frequency, std::int64_t numSamples)
{
    std::vector<float> x (static_cast<std::size_t> (numSamples));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / rate));
    return x;
}

double peakOf (const std::vector<float>& v, std::int64_t from, std::int64_t to)
{
    float p = 0.0f;
    for (auto i = from; i < to; ++i)
        p = std::max (p, std::abs (v[static_cast<std::size_t> (i)]));
    return p;
}
} // namespace

TEST_CASE ("A bank has its key's first 12 partials, below the rate's limit", "[sympathetic]")
{
    const auto model = forte::detail::makeSeedPianoModel();
    SympatheticBank bank;
    bank.prepare (model, rate);

    CHECK (bank.getNumModes (keyOf (21)) == 12);
    CHECK (bank.getNumModes (keyOf (60)) == 12);
    CHECK (bank.getNumModes (keyOf (108)) < 12);  // C8's partials pass 0.45 × 48 kHz early
    CHECK (bank.getNumModes (keyOf (108)) > 0);
}

TEST_CASE ("A lifted bank answers at its partials with the model's level, and a resting one not at all", "[sympathetic]")
{
    const auto model = forte::detail::makeSeedPianoModel();
    const int note = GENERATE (36, 60, 84);
    const auto& p = model.note (note);
    const int key = keyOf (note);

    SympatheticBank bank;
    bank.prepare (model, rate);

    INFO ("note " << note);

    // Driven at partial 2 long enough to reach its steady state (about 6 time constants).
    const double partialHz = forte::detail::partialFrequency (p, 2);
    const double settle = 6.0 / forte::detail::promptDecay (p, partialHz);
    const auto length = static_cast<std::int64_t> ((settle + 0.5) * rate);
    const auto drive = sine (partialHz, length);

    const auto lifted = runBank (bank, key, drive, length, 1.0f);
    CHECK (peakOf (lifted, length - second / 2, length) == Catch::Approx (model.sympatheticLevel).epsilon (0.05));

    // Between two partials, far outside both resonances, it hardly answers.
    bank.reset();
    const double betweenHz = 0.5 * (forte::detail::partialFrequency (p, 2) + forte::detail::partialFrequency (p, 3));
    const auto off = runBank (bank, key, sine (betweenHz, length), length, 1.0f);
    CHECK (peakOf (off, length - second / 2, length) < 0.05 * model.sympatheticLevel);

    // A resting damper lets nothing in.
    bank.reset();
    const auto resting = runBank (bank, key, drive, length, 0.0f);
    CHECK (allZero (resting));
    CHECK_FALSE (bank.isActive (key));

    // Half-lifted, it answers, but less.
    bank.reset();
    const auto half = runBank (bank, key, drive, length, 0.5f);
    CHECK (peakOf (half, length - second / 2, length) > 1e-3 * model.sympatheticLevel);
    CHECK (peakOf (half, length - second / 2, length) < 0.5 * model.sympatheticLevel);
}

TEST_CASE ("A bank never hears its own note", "[sympathetic]")
{
    const auto model = forte::detail::makeSeedPianoModel();
    SympatheticBank bank;
    bank.prepare (model, rate);

    const auto drive = sine (forte::detail::partialFrequency (model.note (60), 1), second);
    CHECK (allZero (runBank (bank, keyOf (60), drive, second, 1.0f, &drive)));
}

TEST_CASE ("A bank decays to exact silence and goes idle", "[sympathetic]")
{
    const auto model = forte::detail::makeSeedPianoModel();
    const auto& p = model.note (36);
    const int key = keyOf (36);

    SympatheticBank bank;
    bank.prepare (model, rate);

    // Drive for half a second, then let it ring with the damper lifted: it takes a while...
    const auto drive = sine (forte::detail::partialFrequency (p, 1), second / 2);
    // The drive at partial 1 rings every bank mode a little; the slowest of them rests last. The
    // mode the drive is tuned to rings longest at a level that counts: still sounding halfway
    // through its own time to rest.
    double slowest = 1e9;
    for (int n = 1; forte::detail::partialFrequency (p, n) < 20000.0; ++n)
        slowest = std::min (slowest, forte::detail::promptDecayOfPartial (p, n));
    const double levelToRest = std::log (model.sympatheticLevel / 1e-5);
    const auto ringing = static_cast<std::int64_t> ((levelToRest / slowest + 1.0) * rate);
    const auto ownRinging = static_cast<std::int64_t> (levelToRest / forte::detail::promptDecayOfPartial (p, 1) * rate);

    const auto lifted = runBank (bank, key, drive, ringing, 1.0f);
    CHECK_FALSE (allZero (slice (lifted, ownRinging / 2, ownRinging / 2 + second)));
    CHECK (allZero (slice (lifted, ringing - second / 2, ringing)));
    CHECK_FALSE (bank.isActive (key));

    // ...and with the damper let down after the drive, it rests far sooner.
    bank.reset();
    (void) runBank (bank, key, drive, second / 2, 1.0f);
    CHECK (bank.isActive (key));
    const auto damped = runBank (bank, key, {}, 2 * second, 0.0f);
    CHECK_FALSE (allZero (slice (damped, 0, second / 10)));
    CHECK (allZero (slice (damped, second, 2 * second)));
    CHECK_FALSE (bank.isActive (key));
}

TEST_CASE ("Resonance scales the bank: 0 is none, 62.5 is double, 100 is 16 times", "[sympathetic]")
{
    const auto model = forte::detail::makeSeedPianoModel();
    SympatheticBank bank;
    bank.prepare (model, rate);

    const int key = keyOf (48);
    const auto drive = sine (forte::detail::partialFrequency (model.note (48), 1), second);
    const auto physical = runBank (bank, key, drive, second, 1.0f);

    bank.reset();
    bank.setResonance (62.5);
    const auto doubled = runBank (bank, key, drive, second, 1.0f);

    bank.reset();
    bank.setResonance (100.0);
    const auto sixteen = runBank (bank, key, drive, second, 1.0f);

    bank.reset();
    bank.setResonance (0.0);
    const auto none = runBank (bank, key, drive, second, 1.0f);

    REQUIRE_FALSE (allZero (physical));
    for (std::size_t i = 0; i < physical.size(); ++i)
    {
        REQUIRE (doubled[i] == 2.0f * physical[i]);
        REQUIRE (sixteen[i] == 16.0f * physical[i]);
    }
    CHECK (allZero (none));
    CHECK_FALSE (bank.isActive (key));
}

TEST_CASE ("The bank rings after a pedal-down chord, and not with the pedal up", "[sympathetic][engine]")
{
    // The chord's keys are held throughout, so its own strings are the same either way: what the
    // pedal adds is the other keys' banks.
    const std::vector<TimedMidi> chord { noteOn (0, 48, 90), noteOn (0, 52, 90), noteOn (0, 55, 90) };
    auto down = chord;
    down.insert (down.begin(), controller (0, 64, 127));
    auto half = chord;
    half.insert (half.begin(), controller (0, 64, 64));

    const std::int64_t length = 2 * second;
    const auto up = renderFresh (chord, length).left;
    const auto withPedal = renderFresh (down, length).left;
    const auto withHalf = renderFresh (half, length).left;

    std::vector<float> banks (up.size()), halfBanks (up.size());
    for (std::size_t i = 0; i < up.size(); ++i)
    {
        banks[i] = withPedal[i] - up[i];
        halfBanks[i] = withHalf[i] - up[i];
    }

    const double note = rms (up, second / 2, length);
    const double sympathy = rms (banks, second / 2, length);
    INFO ("banks " << 20.0 * std::log10 (sympathy / note) << " dB against the chord");
    CHECK (sympathy > 0.01 * note);  // above −40 dB
    CHECK (sympathy < 0.5 * note);   // below −6 dB

    // Half-pedal lets some through.
    const double halfSympathy = rms (halfBanks, second / 2, length);
    CHECK (halfSympathy > 0.01 * sympathy);
    CHECK (halfSympathy < sympathy);
}

TEST_CASE ("A silently held key's bank answers other notes", "[sympathetic][engine]")
{
    // CC120 stops C4's strings but leaves its key down, so its damper stays lifted. With the key
    // let go instead, everything else is the same, bit for bit.
    const std::int64_t strikeAt = second / 4;
    const std::int64_t length = 2 * second;
    const std::vector<TimedMidi> held { noteOn (0, 60, 90), controller (100, 120, 0), noteOn (strikeAt, 48, 100), noteOff (strikeAt + second / 4, 48) };
    const std::vector<TimedMidi> released { noteOn (0, 60, 90), controller (100, 120, 0), noteOff (100, 60), noteOn (strikeAt, 48, 100), noteOff (strikeAt + second / 4, 48) };

    const auto withKey = renderFresh (held, length).left;
    const auto withoutKey = renderFresh (released, length).left;

    CHECK (slice (withKey, 0, strikeAt) == slice (withoutKey, 0, strikeAt));
    CHECK (slice (withKey, strikeAt, length) != slice (withoutKey, strikeAt, length));

    std::vector<float> answer (withKey.size());
    for (std::size_t i = 0; i < answer.size(); ++i)
        answer[i] = withKey[i] - withoutKey[i];

    // C4's bank answers C3 (its second partial is C4's first), and rings on after C3 is damped.
    const double c3 = rms (withoutKey, strikeAt, strikeAt + second / 4);
    const double whileHeld = rms (answer, strikeAt, strikeAt + second / 4);
    const double afterwards = rms (answer, strikeAt + second / 2, length);
    INFO ("answer " << 20.0 * std::log10 (whileHeld / c3) << " dB against C3, then " << 20.0 * std::log10 (afterwards / whileHeld) << " dB");
    CHECK (whileHeld > 1e-3 * c3);
    CHECK (afterwards > 0.1 * whileHeld);
}

TEST_CASE ("With the pedal lifted and then let go, the banks fall to exact silence", "[sympathetic][engine]")
{
    const auto out = renderFresh ({ controller (0, 64, 127), noteOn (0, 36, 110), noteOn (0, 60, 100),
                                    noteOff (second / 10, 36), noteOff (second / 10, 60), controller (second, 64, 0) },
                                  4 * second);

    CHECK_FALSE (allZero (slice (out.left, second / 2, second)));
    CHECK (allZero (slice (out.left, 3 * second, 4 * second)));
    CHECK (allZero (slice (out.right, 3 * second, 4 * second)));
}

TEST_CASE ("Sympathetic resonance is bit-identical whatever the block size", "[tier1][determinism][sympathetic]")
{
    // Pedal down and up, half-pedal, a silently held key, staccato notes into lifted banks, and
    // CC120 mid-ring.
    const std::vector<TimedMidi> events {
        controller (0, 64, 127), noteOn (3, 36, 100), noteOn (40, 55, 80), noteOff (500, 36), noteOff (501, 55),
        controller (4000, 64, 70), noteOn (4001, 72, 90), noteOff (4800, 72),
        noteOn (6000, 60, 60), controller (6050, 120, 0), noteOn (6100, 43, 110), noteOff (7000, 43),
        controller (9000, 64, 0), noteOn (9500, 67, 90), noteOff (9600, 67), noteOff (12000, 60),
        controller (13000, 64, 127), noteOn (13001, 30, 120),
    };

    constexpr std::int64_t length = second / 2;
    const auto reference = renderFresh (events, length, 128);
    REQUIRE_FALSE (allZero (reference.left));

    for (int blockSize : { 1, 7, 31, 33, 100, 512, 4096 })
    {
        INFO ("block size " << blockSize);
        const auto out = renderFresh (events, length, blockSize);
        CHECK (out.left == reference.left);
        CHECK (out.right == reference.right);
    }
}

TEST_CASE ("The bank's tail bound grows when a bank could outlast its drive", "[sympathetic]")
{
    auto model = forte::detail::makeSeedPianoModel();
    constexpr double notesTail = 50.0;
    constexpr double slowest = 0.05;

    // A quiet bank outlasts nothing: a level whose Resonance-100 gain (×16) is the old seed's top,
    // 0.1 × 2. (The strings' rate here is well clear of the slowest bank modes, the Keyscape fit's
    // floor of 0.12/s, issue 0021 §15: the bound grows as the two rates meet.)
    const double seedLevel = model.sympatheticLevel;
    model.sympatheticLevel = 0.2 / 16.0;
    CHECK (forte::detail::sympatheticTailSeconds (model, notesTail, slowest) == notesTail);
    model.sympatheticLevel = seedLevel;

    // Loud banks ring on past the strings.
    model.sympatheticLevel = 10.0;
    CHECK (forte::detail::sympatheticTailSeconds (model, notesTail, slowest) > notesTail);

    // A bank decaying slower than the strings sets its own tail from the strike.
    model.sympatheticLevel = 0.1;
    CHECK (forte::detail::sympatheticTailSeconds (model, 1.0, 10.0) > 1.0);
}
