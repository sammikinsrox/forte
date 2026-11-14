#pragma once

namespace forte::detail
{
/** One note's three calibrated hammer speeds, in m/s (SPEC §4.1). */
struct HammerSpeeds
{
    double pp = 0.0;
    double mf = 0.0;
    double ff = 0.0;
};

/** Touch, 0–100. 50 is the calibrated curve, lower is lighter and higher is heavier (SPEC §4.1). */
inline constexpr double touchLightest = 0.0;
inline constexpr double touchCalibrated = 50.0;
inline constexpr double touchHeaviest = 100.0;

/** A note's velocity → hammer speed curve (SPEC §4.1).

    The curve passes through the note's pp, mf and ff hammer speeds at velocities 32, 64 and 112.
    It extends to velocity 1 at about 12 dB under pp and to velocity 127 at about 4.5 dB over ff,
    capped at the note's highest stable hammer speed. It's a monotone cubic on log hammer speed,
    so it always rises and is smooth.

    Touch bends normalised velocity before the curve with a power law that keeps velocity 1 and
    127 fixed, so the curve can't fold back on itself at any Touch.

    Velocity is a real number, so the host's full resolution is used. Realtime-safe once built.
*/
class VelocityCurve
{
public:
    VelocityCurve() = default;
    VelocityCurve (HammerSpeeds calibrated, double maxHammerSpeed) noexcept;

    /** Hammer speed in m/s. Velocity is clamped to 1–127 and Touch to 0–100. */
    [[nodiscard]] double hammerSpeed (double velocity, double touch) const noexcept;

    static constexpr int numPoints = 5;
    static constexpr double velocities[numPoints] { 1.0, 32.0, 64.0, 112.0, 127.0 };

    /** Loudness, relative to pp and ff, of the curve's ends. Seeds: assumes loudness ∝ speed. */
    static constexpr double softestBelowPpDecibels = 12.0;
    static constexpr double hardestAboveFfDecibels = 4.5;

private:
    double logSpeed[numPoints] {};
    double slope[numPoints] {};  // d(log speed)/d(velocity) at each point
};
} // namespace forte::detail
