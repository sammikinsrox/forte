#include "VelocityCurve.h"

#include <algorithm>
#include <cmath>

namespace forte::detail
{
namespace
{
double decibelsToRatio (double decibels) noexcept
{
    return std::pow (10.0, decibels / 20.0);
}
} // namespace

VelocityCurve::VelocityCurve (HammerSpeeds calibrated, double maxHammerSpeed) noexcept
{
    const double hardest = std::min (calibrated.ff * decibelsToRatio (hardestAboveFfDecibels), maxHammerSpeed);
    const double speeds[numPoints] {
        calibrated.pp / decibelsToRatio (softestBelowPpDecibels),
        calibrated.pp,
        calibrated.mf,
        calibrated.ff,
        hardest,
    };

    for (int i = 0; i < numPoints; ++i)
        logSpeed[i] = std::log (speeds[i]);

    // Monotone piecewise-cubic Hermite (Fritsch–Butland, as in PCHIP). With strictly rising
    // points every tangent is positive and at most three times its neighbouring secants, which
    // keeps each piece rising.
    double secant[numPoints - 1];
    double width[numPoints - 1];

    for (int i = 0; i < numPoints - 1; ++i)
    {
        width[i] = velocities[i + 1] - velocities[i];
        secant[i] = (logSpeed[i + 1] - logSpeed[i]) / width[i];
    }

    slope[0] = secant[0];
    slope[numPoints - 1] = secant[numPoints - 2];

    for (int i = 1; i < numPoints - 1; ++i)
    {
        const double before = secant[i - 1];
        const double after = secant[i];

        if (before <= 0.0 || after <= 0.0)
        {
            slope[i] = 0.0;
            continue;
        }

        const double w1 = 2.0 * width[i] + width[i - 1];
        const double w2 = width[i] + 2.0 * width[i - 1];
        slope[i] = (w1 + w2) / (w1 / before + w2 / after);
    }
}

double VelocityCurve::hammerSpeed (double velocity, double touch) const noexcept
{
    velocity = std::clamp (velocity, velocities[0], velocities[numPoints - 1]);
    touch = std::clamp (touch, touchLightest, touchHeaviest);

    // Touch: a power law on normalised velocity. 0 and 1 stay put, so velocity 1 and 127 are
    // always the softest and hardest blows. Exponent 1/3 (lightest) to 3 (heaviest); the span is
    // a seed until it's set by ear.
    const double span = velocities[numPoints - 1] - velocities[0];
    const double normalised = (velocity - velocities[0]) / span;
    const double exponent = std::pow (3.0, (touch - touchCalibrated) / (touchHeaviest - touchCalibrated));
    const double bent = velocities[0] + span * std::pow (normalised, exponent);

    int segment = 0;
    while (segment < numPoints - 2 && bent > velocities[segment + 1])
        ++segment;

    const double h = velocities[segment + 1] - velocities[segment];
    const double t = (bent - velocities[segment]) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;

    const double logValue = (2.0 * t3 - 3.0 * t2 + 1.0) * logSpeed[segment]
                          + (t3 - 2.0 * t2 + t) * h * slope[segment]
                          + (-2.0 * t3 + 3.0 * t2) * logSpeed[segment + 1]
                          + (t3 - t2) * h * slope[segment + 1];

    return std::exp (logValue);
}
} // namespace forte::detail
