// Velocity → hammer speed (SPEC §4.1), including the Tier 1 "Velocity curve" check (SPEC §8.1).

#include "PianoModel.h"
#include "VelocityCurve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using forte::detail::makeSeedPianoModel;
using forte::detail::VelocityCurve;
using Catch::Matchers::WithinRel;

namespace
{
VelocityCurve curveFor (const forte::detail::NoteParameters& p)
{
    return { p.hammerSpeeds, p.maxHammerSpeed };
}
} // namespace

TEST_CASE ("Velocity curve rises monotonically at every Touch value", "[tier1][velocity]")
{
    const auto model = makeSeedPianoModel();

    for (const auto& note : model.notes)
    {
        const auto curve = curveFor (note);

        for (int touch = 0; touch <= 100; ++touch)
        {
            double previous = curve.hammerSpeed (1.0, touch);
            bool rising = true;
            double failedAt = 0.0;

            // Fractional steps: velocity is used at full host resolution.
            for (double velocity = 1.125; velocity <= 127.0; velocity += 0.125)
            {
                const double speed = curve.hammerSpeed (velocity, touch);
                if (! (speed > previous))
                {
                    rising = false;
                    failedAt = velocity;
                    break;
                }
                previous = speed;
            }

            if (! rising)
            {
                INFO ("note " << note.midiNote << ", Touch " << touch << ", velocity " << failedAt);
                FAIL ("hammer speed doesn't rise");
            }
        }
    }
}

TEST_CASE ("Velocity 1 and 127 are the softest and hardest blows at every Touch", "[tier1][velocity]")
{
    const auto model = makeSeedPianoModel();

    for (const auto& note : model.notes)
    {
        const auto curve = curveFor (note);
        const double softest = curve.hammerSpeed (1.0, 50.0);
        const double hardest = curve.hammerSpeed (127.0, 50.0);

        for (int touch = 0; touch <= 100; ++touch)
        {
            INFO ("note " << note.midiNote << ", Touch " << touch);
            REQUIRE (curve.hammerSpeed (1.0, touch) == softest);
            REQUIRE (curve.hammerSpeed (127.0, touch) == hardest);
        }
    }
}

TEST_CASE ("At Touch 50 the curve passes through pp, mf and ff at velocities 32, 64 and 112", "[velocity]")
{
    // The ±1 dB loudness check (Tier 1 "Velocity calibration") needs the fitted loudness from
    // SPEC §3.3; this checks the hammer-speed half of it.
    const auto model = makeSeedPianoModel();

    for (const auto& note : model.notes)
    {
        INFO ("note " << note.midiNote);
        const auto curve = curveFor (note);
        CHECK_THAT (curve.hammerSpeed (32.0, 50.0), WithinRel (note.hammerSpeeds.pp, 1e-9));
        CHECK_THAT (curve.hammerSpeed (64.0, 50.0), WithinRel (note.hammerSpeeds.mf, 1e-9));
        CHECK_THAT (curve.hammerSpeed (112.0, 50.0), WithinRel (note.hammerSpeeds.ff, 1e-9));
    }
}

TEST_CASE ("The curve extends about 12 dB under pp and 4.5 dB over ff, capped at the stable speed", "[velocity]")
{
    const forte::detail::HammerSpeeds speeds { 0.7, 2.0, 4.5 };

    const VelocityCurve uncapped (speeds, 100.0);
    CHECK_THAT (uncapped.hammerSpeed (1.0, 50.0), WithinRel (0.7 * std::pow (10.0, -12.0 / 20.0), 1e-9));
    CHECK_THAT (uncapped.hammerSpeed (127.0, 50.0), WithinRel (4.5 * std::pow (10.0, 4.5 / 20.0), 1e-9));

    const VelocityCurve capped (speeds, 5.0);
    CHECK_THAT (capped.hammerSpeed (127.0, 50.0), WithinRel (5.0, 1e-9));
}

TEST_CASE ("Lower Touch is lighter, higher Touch is heavier", "[velocity]")
{
    const VelocityCurve curve ({ 0.7, 2.0, 4.5 }, 8.0);

    for (double velocity : { 16.0, 64.0, 100.0 })
    {
        INFO ("velocity " << velocity);
        CHECK (curve.hammerSpeed (velocity, 0.0) > curve.hammerSpeed (velocity, 50.0));
        CHECK (curve.hammerSpeed (velocity, 50.0) > curve.hammerSpeed (velocity, 100.0));
    }
}
