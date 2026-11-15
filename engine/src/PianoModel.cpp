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

/** One note's hammer: the Stulov felt and the velocity calibration. */
struct HammerEntry
{
    int midiNote;
    double stiffness, exponent, relaxation, relaxationTime;  // Q0, p, ε, τ (s)
    double pp, mf, ff;                                        // hammer speeds at velocities 32, 64, 112 (m/s)
    double hardest;                                           // velocity 127's speed (m/s)
};

constexpr HammerEntry hammerTable[] {
#include "HammerFit.inc"
};
static_assert (std::size (hammerTable) == numNotes);
static_assert (hammerTable[0].midiNote == lowestNote && hammerTable[numNotes - 1].midiNote == highestNote);

/** One note's decay: the laws σ = b1 + b3·f² for the prompt sound and the aftersound, and the
    measured partials' own rates (1/s). */
struct DecayEntry
{
    int midiNote;
    double promptB1, promptB3, aftersoundB1, aftersoundB3;
    int measured;
    double prompt[NoteParameters::maxMeasuredPartials];
    double aftersound[NoteParameters::maxMeasuredPartials];
};

constexpr DecayEntry decayTable[] {
#include "DecayFit.inc"
};
static_assert (std::size (decayTable) == numNotes);
static_assert (decayTable[0].midiNote == lowestNote && decayTable[numNotes - 1].midiNote == highestNote);

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

    // About 10 single, 18 double and the rest triple (SPEC §2.3). Exact boundaries come from the
    // Steinway D data.
    p.numStrings = midiNote <= 30 ? 1 : midiNote <= 48 ? 2 : 3;

    // Chaigne & Askenfelt seeds, as interpolated by the listening-test prototype.
    constexpr double anchors[] { 36.0, 60.0, 84.0, 96.0, 108.0 };
    constexpr double lengths[] { 1.90, 0.62, 0.18, 0.09, 0.051 };
    p.stringLength = interpolateLog (key, anchors, lengths);
    p.stringTension = 700.0 + 50.0 * std::cos (std::clamp ((key - 36.0) / 48.0, 0.0, 1.0) * std::numbers::pi);

    // Decay, fitted to Iowa mf (tools/fitting/fit_decay.py, SPEC §3.3, issue 0021 step 2): the
    // measured partials' prompt and aftersound rates, and the laws above them.
    const auto& decay = decayTable[static_cast<std::size_t> (index)];
    p.decayConstant = decay.promptB1;
    p.decayPerHertzSquared = decay.promptB3;
    p.aftersoundDecayConstant = decay.aftersoundB1;
    p.aftersoundDecayPerHertzSquared = decay.aftersoundB3;
    p.numMeasuredPartials = decay.measured;
    for (int n = 0; n < decay.measured; ++n)
    {
        p.measuredPromptDecay[static_cast<std::size_t> (n)] = decay.prompt[n];
        p.measuredAftersoundDecay[static_cast<std::size_t> (n)] = decay.aftersound[n];
    }

    // Phantom partials on the lowest 30 notes (A0–D3), at the listening-test prototype's level
    // and longitudinal modes until they're fitted against Iowa ff (SPEC §3.3).
    p.phantomLevel = midiNote <= 50 ? 0.3 : 0.0;
    p.longitudinalFrequency = midiNote < 45 ? 1100.0 : 1500.0;
    p.longitudinalQ = 60.0;
    p.axialStiffness = 2e11 * std::numbers::pi * 0.6e-3 * 0.6e-3 * 1.6;  // steel core plus winding, roughly

    // Hammer mass and strike position: Chaigne & Askenfelt seeds, as interpolated by the
    // listening-test prototype (SPEC §3.3: the mass is a smooth curve from the seeds, not fitted).
    constexpr double hammerAnchors[] { 36.0, 60.0, 84.0, 96.0, 108.0 };
    constexpr double strikePositions[] { 0.12, 0.12, 0.10, 0.0625, 0.0625 };
    constexpr double masses[] { 4.9e-3, 2.97e-3, 2.5e-3, 2.2e-3, 2.0e-3 };
    p.strikePosition = interpolate (key, hammerAnchors, strikePositions);
    p.hammerMass = interpolate (key, hammerAnchors, masses);

    // The felt and the velocity calibration, set by ear (tools/fitting/fit_hammer.py voice, issue
    // 0021 §7) until a fit that works by ear replaces them (SPEC §3.3, §4.1).
    const auto& hammer = hammerTable[static_cast<std::size_t> (index)];
    p.feltStiffness = hammer.stiffness;
    p.feltExponent = hammer.exponent;
    p.feltRelaxation = hammer.relaxation;
    p.feltRelaxationTime = hammer.relaxationTime;
    p.hammerSpeeds = { hammer.pp, hammer.mf, hammer.ff };
    p.maxHammerSpeed = hammer.hardest;

    // The shifted hammer meets less-compacted felt. A literature-style offset until it's set by
    // ear (SPEC §3.3, §10).
    p.softPedalFeltScale = 0.7;

    // Roughly the top 18 notes have no damper (SPEC §2.5): G#6 (MIDI 91) and up.
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
    // −15 dBFS, and a full 88-key sustain glissando at velocity 90 peaks just under 0 dBFS.
    model.outputGain = 5.6e-3;

    // Placeholder until Piano in 162 calibrates it (SPEC §3.3): a round gain that puts a held
    // note's pedal-down minus pedal-up difference about 12–28 dB under the note from E1 to C6,
    // closer in the sustain than in the attack.
    model.sympatheticLevel = 0.1;
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
