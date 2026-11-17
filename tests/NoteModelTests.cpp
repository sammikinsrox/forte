// One note model on its own (SPEC §2.3, §2.7): coupled unisons, phantom partials and the soft
// pedal's hammer shift, each against the same note without it.

#include "support/EngineRender.h"

#include "NoteModel.h"
#include "PianoModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using forte::detail::NoteModel;
using forte::detail::NoteParameters;
using namespace forte::test;

namespace
{
constexpr double noteSampleRate = 48000.0;
constexpr std::int64_t second = 48000;

const forte::detail::PianoModel& seedModel()
{
    static const auto model = forte::detail::makeSeedPianoModel();
    return model;
}

/** One strike on a held key, driven the way the engine drives a note. */
std::vector<float> strikeNote (const NoteParameters& p, double velocity, std::int64_t numSamples, double softShift = 0.0)
{
    NoteModel note;
    note.prepare (p, seedModel().unisonSpreadCents, seedModel().outputGain, noteSampleRate);
    note.setKeyDown (true);
    note.strike (velocity, forte::detail::touchCalibrated, softShift);

    std::vector<float> out (static_cast<std::size_t> (numSamples), 0.0f);
    std::vector<float> scratch (NoteModel::controlInterval);

    for (std::int64_t at = 0; at < numSamples; at += NoteModel::controlInterval)
    {
        note.controlTick (0.0f);
        const auto length = static_cast<int> (std::min<std::int64_t> (NoteModel::controlInterval, numSamples - at));
        note.render (out.data() + at, scratch.data(), length);
    }

    return out;
}

std::vector<float> difference (const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<float> d (a.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        d[i] = a[i] - b[i];
    return d;
}
} // namespace

TEST_CASE ("Coupled unisons ring on in a long aftersound", "[note][unison]")
{
    // Same note with the coupling taken out (aftersound = prompt sound): the early sound is about
    // the same, and the coupled one rings far longer. The mechanism, not the fitted data: both
    // follow the laws (the seed's, aftersound 4× slower), with no measured partials, and the
    // model's full unison spread: the voicing narrows it per note (issue 0022), and the aftersound
    // a coupled unison holds grows with its detuning.
    const int midiNote = GENERATE (40, 60, 72);
    auto coupled = seedModel().note (midiNote);
    coupled.numMeasuredPartials = 0;
    coupled.numTailPartials = 0;
    coupled.unisonSpreadScale = 1.0;
    coupled.decayConstant = std::log (1000.0) / (20.0 * std::pow (0.1, (midiNote - 21) / 87.0));
    coupled.decayPerHertzSquared = 1.5e-7;
    coupled.aftersoundDecayConstant = 0.25 * coupled.decayConstant;
    coupled.aftersoundDecayPerHertzSquared = 0.25 * coupled.decayPerHertzSquared;
    auto uncoupled = coupled;
    uncoupled.aftersoundDecayConstant = uncoupled.decayConstant;
    uncoupled.aftersoundDecayPerHertzSquared = uncoupled.decayPerHertzSquared;

    const auto withCoupling = strikeNote (coupled, 90, 6 * second);
    const auto without = strikeNote (uncoupled, 90, 6 * second);

    INFO ("note " << midiNote);
    const double earlyRatio = rms (withCoupling, 0, second / 10) / rms (without, 0, second / 10);
    CHECK (earlyRatio > 0.8);
    CHECK (earlyRatio < 1.25);
    CHECK (rms (withCoupling, 5 * second, 6 * second) > 2.0 * rms (without, 5 * second, 6 * second));
}

TEST_CASE ("The second polarisation's tail holds the aftersound", "[note][unison]")
{
    // Issue 0022 §6. Partials 1-6 strike fast (3 /s) on uncoupled strings; with a tail at -20 dB
    // decaying at 0.3 /s, the early sound is the same and the late sound is the tail's.
    const int midiNote = GENERATE (25, 60);
    auto withTail = seedModel().note (midiNote);
    withTail.numMeasuredPartials = 6;
    withTail.numTailPartials = 6;
    withTail.decayTiltExponent = 0.0;
    withTail.decayConstant = withTail.aftersoundDecayConstant = 3.0;  // everything else dies fast too
    withTail.decayPerHertzSquared = withTail.aftersoundDecayPerHertzSquared = 0.0;
    for (std::size_t n = 0; n < 6; ++n)
    {
        withTail.measuredPromptDecay[n] = withTail.measuredAftersoundDecay[n] = 3.0;
        withTail.tailDecay[n] = 0.3;
        withTail.tailLevel[n] = 0.1;
    }
    auto without = withTail;
    without.numTailPartials = 0;

    const auto a = strikeNote (withTail, 90, 6 * second);
    const auto b = strikeNote (without, 90, 6 * second);

    INFO ("note " << midiNote);
    const double earlyRatio = rms (a, 0, second / 10) / rms (b, 0, second / 10);
    CHECK (earlyRatio > 0.85);
    CHECK (earlyRatio < 1.2);
    CHECK (rms (a, 5 * second, 6 * second) > 3.0 * rms (b, 5 * second, 6 * second));
    CHECK (forte::detail::slowestStringDecay (withTail, 0.7) <= 0.3 + 1e-9);
}

TEST_CASE ("Single-string notes are not coupled", "[note][unison]")
{
    const auto p = seedModel().note (25);
    REQUIRE (p.numStrings == 1);

    auto changedAftersound = p;
    changedAftersound.aftersoundDecayConstant *= 0.1;
    changedAftersound.phantomLevel = 0.0;
    auto plain = p;
    plain.phantomLevel = 0.0;

    CHECK (strikeNote (plain, 100, second / 2) == strikeNote (changedAftersound, 100, second / 2));
}

TEST_CASE ("Phantom partials sound on the lowest notes only", "[note][phantom]")
{
    for (int midiNote = forte::detail::lowestNote; midiNote <= forte::detail::highestNote; ++midiNote)
    {
        NoteModel note;
        note.prepare (seedModel().note (midiNote), seedModel().unisonSpreadCents, seedModel().outputGain, noteSampleRate);
        INFO ("note " << midiNote);
        CHECK (note.hasPhantomPartials() == (midiNote <= 50));
    }
}

TEST_CASE ("Phantom partials grow with the square of the strings' motion", "[note][phantom]")
{
    // They come from the strings stretching, which goes as displacement squared, so they matter
    // much more at ff than at pp (SPEC §2.3, the glossary's "loud bass notes").
    const int midiNote = GENERATE (21, 36, 50);
    const auto withPhantoms = seedModel().note (midiNote);
    auto without = withPhantoms;
    without.phantomLevel = 0.0;

    const auto share = [&] (double velocity)
    {
        const auto a = strikeNote (withPhantoms, velocity, second / 2);
        const auto b = strikeNote (without, velocity, second / 2);
        return rms (difference (a, b), 0, second / 2) / rms (b, 0, second / 2);
    };

    const double pp = share (32);
    const double ff = share (112);

    INFO ("note " << midiNote << ": phantom share pp " << pp << ", ff " << ff);
    CHECK (pp > 0.0);
    CHECK (ff > 2.0 * pp);
}

TEST_CASE ("Phantom partials run through the hammer contact", "[note][phantom]")
{
    // SPEC §2.3: pausing the path during contact caused pops. The contact lasts a few ms, so the
    // phantoms must already be in the first millisecond.
    const auto withPhantoms = seedModel().note (28);
    auto without = withPhantoms;
    without.phantomLevel = 0.0;

    const auto d = difference (strikeNote (withPhantoms, 127, second / 100), strikeNote (without, 127, second / 100));
    CHECK_FALSE (allZero (slice (d, 0, second / 1000)));
}

TEST_CASE ("A phantom-partial note goes idle and renders exact silence", "[note][phantom]")
{
    // The longitudinal modes rest too (SPEC §2.2). Key released at once, so the damper ends it.
    NoteModel note;
    note.prepare (seedModel().note (33), seedModel().unisonSpreadCents, seedModel().outputGain, noteSampleRate);
    note.setKeyDown (true);
    note.strike (127, forte::detail::touchCalibrated, 0.0);
    note.setKeyDown (false);

    std::vector<float> scratch (NoteModel::controlInterval), out (NoteModel::controlInterval);

    for (int tick = 0; tick < 10 * second / NoteModel::controlInterval && note.isSounding(); ++tick)
    {
        note.controlTick (0.0f);
        note.render (out.data(), scratch.data(), NoteModel::controlInterval);
    }

    CHECK_FALSE (note.isSounding());
}

TEST_CASE ("The soft pedal darkens the strike", "[note][soft-pedal]")
{
    // SPEC §2.7: softer felt on every note, and on 2- and 3-string unisons fewer strings struck,
    // which also makes it a little quieter. Brightness here is the first difference's RMS over
    // the signal's: a slope that rises with frequency. From about C6 up the output is nearly all
    // fundamental, so only the level is checked there.
    const int midiNote = GENERATE (25, 40, 60, 72, 84);
    const auto p = seedModel().note (midiNote);

    const auto brightness = [] (const std::vector<float>& x)
    {
        std::vector<float> slope (x.size(), 0.0f);
        for (std::size_t i = 1; i < x.size(); ++i)
            slope[i] = x[i] - x[i - 1];
        return rms (slope, 0, second / 2) / rms (x, 0, second / 2);
    };

    const auto normal = strikeNote (p, 90, second / 2);
    const auto halfway = strikeNote (p, 90, second / 2, 0.5);
    const auto soft = strikeNote (p, 90, second / 2, 1.0);

    INFO ("note " << midiNote << ": brightness " << brightness (normal) << " → " << brightness (halfway) << " → " << brightness (soft)
                  << ", rms " << rms (normal, 0, second / 2) << " → " << rms (soft, 0, second / 2));
    CHECK (allFinite (soft));

    if (midiNote <= 72)
    {
        CHECK (brightness (halfway) < brightness (normal));
        CHECK (brightness (soft) < brightness (halfway));
    }

    if (p.numStrings > 1)
        CHECK (rms (soft, 0, second / 2) < rms (normal, 0, second / 2));
}

TEST_CASE ("The hysteretic felt is stiffer than its relaxed self and brightens with velocity", "[note][hammer]")
{
    // SPEC §2.4: Stulov's felt meets fast compression with Q0, not the relaxed Q0·(1 − ε), so
    // with a short relaxation time only the fastest compression meets the stiff felt: the strike
    // gains air over an elastic felt at the relaxed stiffness, without the middle partials
    // rising (no twang). And ff has more air than pp, up to E5 (issue 0021 §7). Air is the rms of
    // the third difference (+18 dB/octave, so the top dominates) over the signal's. The notes are
    // ones the Keyscape-fitted voicing gives a strongly relaxing felt; from F#6 up it has none
    // (§14), and C6's (ε 0.6 on a felt hardened in §18) adds under 10%.
    // The felt alone: without the second polarisation's tails, whose added sound is the same on
    // both sides and only dilutes the ratio.
    const int midiNote = GENERATE (36, 60, 72, 76);
    auto stulov = seedModel().note (midiNote);
    stulov.numTailPartials = 0;
    REQUIRE (stulov.feltRelaxation > 0.5);

    auto relaxed = stulov;
    relaxed.feltStiffness *= 1.0 - stulov.feltRelaxation;
    relaxed.feltRelaxation = 0.0;

    constexpr std::int64_t window = second / 6;

    const auto air = [] (const std::vector<float>& x)
    {
        std::vector<float> d (x.begin(), x.end());
        for (int pass = 0; pass < 3; ++pass)
            for (std::size_t i = d.size() - 1; i > 0; --i)
                d[i] -= d[i - 1];
        return rms (d, 3, window) / rms (x, 0, window);
    };

    const auto pp = air (strikeNote (stulov, 32, window));
    const auto mf = air (strikeNote (stulov, 64, window));
    const auto ff = air (strikeNote (stulov, 112, window));
    const auto mfRelaxed = air (strikeNote (relaxed, 64, window));

    INFO ("note " << midiNote << ": air pp " << pp << ", mf " << mf << ", ff " << ff << "; mf relaxed " << mfRelaxed);
    CHECK (mf > 1.1 * mfRelaxed);
    if (midiNote <= 76)
        CHECK (pp < ff);
}

TEST_CASE ("The hammer table is calibrated and smooth across the keyboard", "[note][hammer]")
{
    // SPEC §3.3, §4.1: the hammer table (engine/src/HammerFit.inc, from fit_hammer.py), checked
    // for what the velocity curve and the felt need, and for jumps between neighbouring notes.
    const auto& model = seedModel();

    for (int midiNote = forte::detail::lowestNote; midiNote <= forte::detail::highestNote; ++midiNote)
    {
        const auto& p = model.note (midiNote);
        INFO ("note " << midiNote);
        CHECK (p.hammerSpeeds.pp > 0.0);
        CHECK (p.hammerSpeeds.pp < p.hammerSpeeds.mf);
        CHECK (p.hammerSpeeds.mf < p.hammerSpeeds.ff);
        CHECK (p.hammerSpeeds.ff < p.maxHammerSpeed);
        CHECK (p.feltRelaxation >= 0.0);
        CHECK (p.feltRelaxation < 1.0);
        CHECK (p.feltRelaxationTime > 0.0);
        CHECK (p.feltExponent >= 1.5);
        CHECK (p.feltExponent <= 5.0);

        if (midiNote > forte::detail::lowestNote)
        {
            const auto& below = model.note (midiNote - 1);
            CHECK (p.feltStiffness / below.feltStiffness < 1.5);
            CHECK (below.feltStiffness / p.feltStiffness < 1.5);
            // The voicing's ε falls from 0.6 to 0 over F#6's half-octave: 25% steps at most.
            CHECK (std::abs ((1.0 - p.feltRelaxation) / (1.0 - below.feltRelaxation) - 1.0) < 0.3);
        }
    }
}

TEST_CASE ("Measured partials keep their own decay rates; the laws take over above them", "[note][decay]")
{
    // SPEC §3.3. A single string: each mode decays at its partial's prompt rate. Measured
    // partials at 2 and 3 /s under a law of 10 /s: the slowest mode is the measured 2 /s. Without
    // the second polarisation's tails, which have their own test.
    auto single = seedModel().note (25);
    REQUIRE (single.numStrings == 1);
    single.numTailPartials = 0;
    single.decayConstant = 10.0;
    single.decayPerHertzSquared = 0.0;
    single.numMeasuredPartials = 2;
    single.measuredPromptDecay[0] = 2.0;
    single.measuredPromptDecay[1] = 3.0;
    CHECK (std::abs (forte::detail::slowestStringDecay (single, 0.7) - 2.0) < 1e-9);
    CHECK (forte::detail::promptDecayOfPartial (single, 3) == 10.0);
    single.numMeasuredPartials = 0;
    CHECK (std::abs (forte::detail::slowestStringDecay (single, 0.7) - 10.0) < 1e-9);

    // A triple unison: a measured partial's slow aftersound outlasts everything the law sets.
    auto triple = seedModel().note (60);
    REQUIRE (triple.numStrings == 3);
    triple.numTailPartials = 0;
    for (auto* law : { &triple.decayConstant, &triple.aftersoundDecayConstant })
        *law = 10.0;
    triple.decayPerHertzSquared = triple.aftersoundDecayPerHertzSquared = 0.0;
    triple.numMeasuredPartials = 1;
    triple.measuredPromptDecay[0] = 4.0;
    triple.measuredAftersoundDecay[0] = 0.5;
    const double slowest = forte::detail::slowestStringDecay (triple, 0.7);
    CHECK (slowest >= 0.5 - 1e-9);
    CHECK (slowest < 1.0);
}

TEST_CASE ("The decay table is in range across the keyboard", "[note][decay]")
{
    // The fitted decays (engine/src/DecayFit.inc, from fit_decay.py) under the voicing: every
    // rate positive and no slower than a 115 s T60 (the fit's floor, a 57 s T60, halved by the
    // voicing's Sustain below A6, issue 0022 §6), every aftersound no faster than its prompt
    // sound, and the measured partials where Iowa has notes.
    constexpr double slowestRate = 0.06;
    const auto& model = seedModel();

    for (int midiNote = forte::detail::lowestNote; midiNote <= forte::detail::highestNote; ++midiNote)
    {
        const auto& p = model.note (midiNote);
        INFO ("note " << midiNote);
        CHECK (p.numMeasuredPartials >= 0);
        CHECK (p.numMeasuredPartials <= NoteParameters::maxMeasuredPartials);
        CHECK (p.decayConstant >= slowestRate - 1e-9);
        CHECK (p.aftersoundDecayConstant >= slowestRate - 1e-9);
        CHECK (p.decayPerHertzSquared >= 0.0);
        CHECK (p.aftersoundDecayPerHertzSquared >= 0.0);
        if (midiNote >= 36 && midiNote <= 81)
            CHECK (p.numMeasuredPartials >= 3);

        for (int n = 1; n <= p.numMeasuredPartials; ++n)
        {
            INFO ("partial " << n);
            CHECK (forte::detail::aftersoundDecayOfPartial (p, n) >= slowestRate - 1e-9);
            CHECK (forte::detail::aftersoundDecayOfPartial (p, n) <= forte::detail::promptDecayOfPartial (p, n));
            CHECK (forte::detail::promptDecayOfPartial (p, n) <= 60.0);
        }
    }
}

TEST_CASE ("The soft pedal is read at each strike", "[engine][soft-pedal]")
{
    // SPEC §2.7: pressing or releasing it doesn't touch a note that's already ringing.
    const std::vector<TimedMidi> plain { noteOn (0, 60, 90), noteOn (second, 48, 90) };
    const std::vector<TimedMidi> pressedWhileRinging { noteOn (0, 60, 90), controller (100, 67, 127),
                                                       controller (second / 2, 67, 0), noteOn (second, 48, 90) };
    const std::vector<TimedMidi> pressedBefore { controller (0, 67, 127), noteOn (0, 60, 90), noteOn (second, 48, 90) };

    const auto reference = renderFresh (plain, 2 * second).left;
    CHECK (renderFresh (pressedWhileRinging, 2 * second).left == reference);
    CHECK (slice (renderFresh (pressedBefore, 2 * second).left, 0, second) != slice (reference, 0, second));
}
