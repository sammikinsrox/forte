// The simulated tuner (SPEC §5).

#include "PianoModel.h"
#include "Tuner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace forte::detail;

namespace
{
/** Partial n of a note under the tuner, in Hz. */
double tunedPartial (const PianoModel& model, const StretchCurve& curve, int midiNote, int partial, double stretch = 50.0)
{
    auto p = model.note (midiNote);
    p.fundamental *= tuningRatio (curve, midiNote - lowestNote, 440.0, stretch);
    return partialFrequency (p, partial);
}

double cents (const StretchCurve& curve, int midiNote)
{
    return curve.cents[static_cast<std::size_t> (midiNote - lowestNote)];
}
} // namespace

TEST_CASE ("Stretch 0 at A4 = 440 is the model's own equal temperament, exactly", "[tuner]")
{
    const auto model = makeSeedPianoModel();
    const auto curve = calculateStretchCurve (model);

    for (int i = 0; i < numNotes; ++i)
        REQUIRE (tuningRatio (curve, i, 440.0, 0.0) == 1.0);
}

TEST_CASE ("The temperament octave A3-A4 is a beatless 4:2 octave in equal steps", "[tuner]")
{
    const auto model = makeSeedPianoModel();
    const auto curve = calculateStretchCurve (model);

    CHECK (cents (curve, 69) == 0.0);
    CHECK (std::abs (tunedPartial (model, curve, 57, 4) / tunedPartial (model, curve, 69, 2) - 1.0) < 1e-12);
    CHECK (cents (curve, 57) < 0.0);

    for (int note = 58; note < 69; ++note)
    {
        INFO ("note " << note);
        CHECK (std::abs ((cents (curve, note) - cents (curve, note - 1)) - (cents (curve, 69) - cents (curve, 57)) / 12.0) < 1e-12);
    }
}

TEST_CASE ("Each octave is beatless on the partials the tuner matches", "[tuner]")
{
    // SPEC §5: 4:2 from about C3 to about C6, 6:3 below, 2:1 above; between them, across an octave
    // centred on each break, the tuner moves from one pair to the other.
    const auto model = makeSeedPianoModel();
    const auto curve = calculateStretchCurve (model);

    const auto beatless = [&] (int lowerNote, int lowerPartial)
    {
        const double a = tunedPartial (model, curve, lowerNote, lowerPartial);
        const double b = tunedPartial (model, curve, lowerNote + 12, lowerPartial / 2);
        return std::abs (a / b - 1.0) < 1e-12;
    };

    for (int note = 70; note <= highestNote; ++note)
    {
        INFO ("note " << note << " up");
        if (note <= 78)
            CHECK (beatless (note - 12, 4));
        else if (note >= 90)
            CHECK (beatless (note - 12, 2));
        else
            CHECK_FALSE ((beatless (note - 12, 4) || beatless (note - 12, 2)));
    }

    for (int note = lowestNote; note < 57; ++note)
    {
        INFO ("note " << note << " down");
        if (note >= 54)
            CHECK (beatless (note, 4));
        else if (note <= 42)
            CHECK (beatless (note, 6));
    }
}

TEST_CASE ("The calculated curve has the Railsback shape", "[tuner]")
{
    // Sharper and sharper up from A4, flatter and flatter down from A3, more in the outer octaves.
    const auto model = makeSeedPianoModel();
    const auto curve = calculateStretchCurve (model);

    for (int note = 58; note <= highestNote; ++note)
    {
        INFO ("note " << note << ": " << cents (curve, note) << " cents");
        CHECK (cents (curve, note) > cents (curve, note - 1));
    }

    // In the bass B hardly changes from note to note, so neighbours can sit within a few
    // hundredths of a cent of each other either way; octaves always go flatter.
    for (int note = lowestNote; note < 57; ++note)
    {
        INFO ("note " << note << ": " << cents (curve, note) << " cents");
        CHECK (cents (curve, note) < cents (curve, note + 1) + 0.05);
        CHECK (cents (curve, note) < cents (curve, note + 12));
    }

    INFO ("A0 " << cents (curve, 21) << ", C2 " << cents (curve, 36) << ", C6 " << cents (curve, 84) << ", C8 " << cents (curve, 108));
    CHECK (cents (curve, 108) > 10.0);
    CHECK (cents (curve, 108) < 60.0);
    CHECK (cents (curve, 21) < -5.0);
    CHECK (cents (curve, 21) > -60.0);
}

TEST_CASE ("Stretch scales the distance from equal temperament, and A4 moves every note", "[tuner]")
{
    const auto model = makeSeedPianoModel();
    const auto curve = calculateStretchCurve (model);
    const auto note = GENERATE (21, 40, 60, 69, 90, 108);
    const int i = note - lowestNote;

    const auto centsAt = [&] (double a4, double stretch) { return 1200.0 * std::log2 (tuningRatio (curve, i, a4, stretch)); };

    INFO ("note " << note);
    CHECK (std::abs (centsAt (440.0, 100.0) - 2.0 * cents (curve, note)) < 1e-9);
    CHECK (std::abs (centsAt (440.0, 25.0) - 0.5 * cents (curve, note)) < 1e-9);
    CHECK (std::abs (tuningRatio (curve, i, 466.0, 50.0) / tuningRatio (curve, i, 440.0, 50.0) - 466.0 / 440.0) < 1e-12);
}
