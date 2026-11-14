#include "PianoModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace forte::detail
{
namespace
{
/** Piecewise-linear interpolation through (xs, ys), held flat beyond the ends. */
template <std::size_t N>
double interpolate (double x, const double (&xs)[N], const double (&ys)[N]) noexcept
{
    if (x <= xs[0])
        return ys[0];

    for (std::size_t i = 1; i < N; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);

    return ys[N - 1];
}

/** As interpolate(), but linear in log(y). */
template <std::size_t N>
double interpolateLog (double x, const double (&xs)[N], const double (&ys)[N]) noexcept
{
    double logs[N];
    for (std::size_t i = 0; i < N; ++i)
        logs[i] = std::log (ys[i]);

    return std::exp (interpolate (x, xs, logs));
}

NoteParameters seedNote (int midiNote)
{
    const double key = midiNote;
    const double index = midiNote - lowestNote;  // 0 = A0, 87 = C8

    NoteParameters p;
    p.midiNote = midiNote;

    // Bridge regions (SPEC §2.8), as the listening-test prototype split them: bass up to B2,
    // tenor C3–B3, middle C4–D#5, treble E5 up. The real split comes from the Steinway D data.
    p.bridgeRegion = midiNote < 48 ? BridgeRegion::bass
                   : midiNote < 60 ? BridgeRegion::tenor
                   : midiNote < 76 ? BridgeRegion::middle
                                   : BridgeRegion::treble;

    // Inharmonicity: ~2e-4 at A0, ~1.3e-4 in the low tenor, ~1e-2 at C8 (CPU benchmark curve,
    // close to the prototype's Iowa measurements at C2 and C4).
    const double log10B = index < 26.0 ? -3.7 - 0.3 * index / 26.0 : -4.0 + 2.0 * (index - 26.0) / 61.0;
    p.inharmonicity = std::pow (10.0, log10B);

    const double equalTemperament = 440.0 * std::pow (2.0, (key - 69.0) / 12.0);
    p.fundamental = equalTemperament / std::sqrt (1.0 + p.inharmonicity);

    // About 10 single, 18 double and the rest triple (SPEC Â§2.3). Exact boundaries come from the
    // Steinway D data.
    p.numStrings = midiNote <= 30 ? 1 : midiNote <= 48 ? 2 : 3;

    // Chaigne & Askenfelt seeds, as interpolated by the listening-test prototype.
    constexpr double anchors[] { 36.0, 60.0, 84.0, 96.0, 108.0 };
    constexpr double lengths[] { 1.90, 0.62, 0.18, 0.09, 0.051 };
    p.stringLength = interpolateLog (key, anchors, lengths);
    p.stringTension = 700.0 + 50.0 * std::cos (std::clamp ((key - 36.0) / 48.0, 0.0, 1.0) * std::numbers::pi);

    // Decay: the prompt sound's T60 for the fundamental runs from 20 s (A0) to 2 s (C8), with
    // Ïƒ = c1 + c3Â·fÂ². The aftersound decays 4Ã— slower. The prototype's Iowa fits put it nearer
    // 10â€“20Ã— slower, but they were limited by the recordings' noise floor; the fit (SPEC Â§3.3)
    // sets the real values per partial.
    const double t60 = 20.0 * std::pow (0.1, index / 87.0);
    p.decayConstant = std::log (1000.0) / t60;
    p.decayPerHertzSquared = 1.5e-7;
    p.aftersoundDecayConstant = 0.25 * p.decayConstant;
    p.aftersoundDecayPerHertzSquared = 0.25 * p.decayPerHertzSquared;

    // Phantom partials on the lowest 30 notes (A0â€“D3), at the listening-test prototype's level
    // and longitudinal modes until they're fitted against Iowa ff (SPEC Â§3.3).
    p.phantomLevel = midiNote <= 50 ? 0.3 : 0.0;
    p.longitudinalFrequency = midiNote < 45 ? 1100.0 : 1500.0;
    p.longitudinalQ = 60.0;
    p.axialStiffness = 2e11 * std::numbers::pi * 0.6e-3 * 0.6e-3 * 1.6;  // steel core plus winding, roughly

    constexpr double hammerAnchors[] { 36.0, 60.0, 84.0, 96.0, 108.0 };
    constexpr double strikePositions[] { 0.12, 0.12, 0.10, 0.0625, 0.0625 };
    constexpr double masses[] { 4.9e-3, 2.97e-3, 2.5e-3, 2.2e-3, 2.0e-3 };
    constexpr double exponents[] { 2.3, 2.5, 2.8, 3.0, 3.0 };
    constexpr double stiffnesses[] { 4e8, 4.5e9, 3e10, 1e11, 3e11 };
    // The prototype's fits up to C6. Above, the felt stiffens back towards the unscaled seeds:
    // with the C6 fit held, the top octave's contact was so long that it barely excited C8.
    constexpr double stiffnessScales[] { 0.681, 0.681, 0.215, 0.6, 1.0 };
    p.strikePosition = interpolate (key, hammerAnchors, strikePositions);
    p.hammerMass = interpolate (key, hammerAnchors, masses);
    p.feltExponent = interpolate (key, hammerAnchors, exponents);
    p.feltStiffness = interpolateLog (key, hammerAnchors, stiffnesses) * interpolate (key, hammerAnchors, stiffnessScales);
    p.feltHysteresis = 1e-5;

    // Typical pp/mf/ff hammer speeds until the fit hands over each note's own (SPEC Â§3.3).
    p.hammerSpeeds = { 0.7, 2.0, 4.5 };
    p.maxHammerSpeed = 8.0;

    // The shifted hammer meets less-compacted felt. A literature-style offset until it's set by
    // ear (SPEC Â§3.3, Â§10).
    p.softPedalFeltScale = 0.7;

    // Roughly the top 18 notes have no damper (SPEC Â§2.5): G#6 (MIDI 91) and up.
    p.hasDamper = midiNote <= 90;
    p.damperDecayConstant = p.hasDamper ? 8.0 : 0.0;  // the prototype's damper law
    p.damperDecayPerRootKilohertz = p.hasDamper ? 15.0 : 0.0;

    return p;
}
} // namespace

PianoModel makeSeedPianoModel()
{
    PianoModel model;

    for (int midiNote = lowestNote; midiNote <= highestNote; ++midiNote)
        model.notes[static_cast<std::size_t> (midiNote - lowestNote)] = seedNote (midiNote);

    model.unisonSpreadCents = 0.7;

    // Placeholder level until the soundboard and the fitted levels exist: an ff C4 peaks around
    // âˆ’15 dBFS, and a full 88-key sustain glissando at velocity 90 peaks just under 0 dBFS.
    model.outputGain = 5.6e-3;
    return model;
}

void unisonOffsetsCents (int numStrings, double spreadCents, double (&offsets)[3]) noexcept
{
    offsets[0] = offsets[1] = offsets[2] = 0.0;

    if (numStrings == 2)
    {
        offsets[0] = -0.5 * spreadCents;
        offsets[1] = 0.5 * spreadCents;
    }
    else if (numStrings == 3)
    {
        offsets[0] = -1.0 * spreadCents;
        offsets[1] = 0.2 * spreadCents;
        offsets[2] = 0.85 * spreadCents;
    }
}
} // namespace forte::detail
