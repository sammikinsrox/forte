// One note model on its own (SPEC §2.3, §2.7): coupled unisons, phantom partials and the soft
// pedal's hammer shift, each against the same note without it.

#include "support/EngineRender.h"

#include "NoteModel.h"
#include "PianoModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
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
    // the same, and the coupled one rings far longer.
    const int midiNote = GENERATE (40, 60, 72);
    const auto coupled = seedModel().note (midiNote);
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
