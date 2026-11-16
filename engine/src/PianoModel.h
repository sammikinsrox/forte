#pragma once

#include "VelocityCurve.h"

#include <array>
#include <cmath>

namespace forte::detail
{
/** The keyboard: A0–C8 (SPEC §4.3). */
inline constexpr int lowestNote = 21;
inline constexpr int highestNote = 108;
inline constexpr int numNotes = highestNote - lowestNote + 1;

[[nodiscard]] constexpr bool isOnKeyboard (int midiNote) noexcept
{
    return midiNote >= lowestNote && midiNote <= highestNote;
}

/** The soundboard's bridge regions (SPEC §2.8). Each note's strings drive one of them. */
enum class BridgeRegion
{
    bass,
    tenor,
    middle,
    treble
};

inline constexpr int numBridgeRegions = 4;

/** Everything the engine needs to build one note model (SPEC §2.3–2.5, §2.8). */
struct NoteParameters
{
    int midiNote = 69;
    BridgeRegion bridgeRegion = BridgeRegion::middle;

    // Strings
    double fundamental = 440.0;      // Hz. The average string's first partial is fundamental·√(1 + B).
    double inharmonicity = 0.0;      // B: partial n sits at n·fundamental·√(1 + B·n²)
    int numStrings = 3;              // strings in the unison
    double stringLength = 0.62;      // m
    double stringTension = 700.0;    // N

    // Decay (SPEC §2.3, §3.3), as amplitude decay rates σ in 1/s. The prompt sound is how fast a
    // partial decays on its own: the whole decay of a single string or an uncoupled partial, and
    // the first stage of a coupled one. The aftersound is one string's own, slower decay, and the
    // coupling through the bridge (SPEC §3.3) makes up the difference. Partials 1 to
    // numMeasuredPartials have their own measured rates; above them, both follow σ = c1 + c3·f².
    double decayConstant = 1.0;                  // prompt sound: c1
    double decayPerHertzSquared = 0;             // prompt sound: c3, s
    double aftersoundDecayConstant = 0.25;       // aftersound: c1
    double aftersoundDecayPerHertzSquared = 0;   // aftersound: c3, s
    static constexpr int maxMeasuredPartials = 32;
    int numMeasuredPartials = 0;
    std::array<double, maxMeasuredPartials> measuredPromptDecay {};      // partial n at [n − 1]
    std::array<double, maxMeasuredPartials> measuredAftersoundDecay {};
    // Every rate above, at frequency f, × (f / f1)^decayTiltExponent, f1 the first partial's:
    // 0 = as given, > 0 makes the upper partials die faster (the lab's Overtone decay, SPEC §6.5).
    double decayTiltExponent = 0.0;

    // Phantom partials (SPEC §2.3): longitudinal modes driven by the strings' stretching
    double phantomLevel = 0.0;              // 0 = none
    double longitudinalFrequency = 1100.0;  // Hz, the first longitudinal mode; mode k is at k times this
    double longitudinalQ = 60.0;
    double axialStiffness = 3.6e5;          // EA, N

    // Hammer
    double strikePosition = 0.125;   // fraction of the string length from the agraffe
    double hammerMass = 3e-3;        // kg
    // Felt (Stulov): F = Q0·[c^p − ε·m], with m the compression power c^p low-passed with time
    // constant τ. Fast compression meets the instantaneous stiffness Q0, slow compression the
    // relaxed Q0·(1 − ε).
    double feltStiffness = 4.5e9;    // Q0
    double feltExponent = 2.5;       // p
    double feltRelaxation = 0.0;     // ε, 0–1; 0 = an elastic felt
    double feltRelaxationTime = 5e-4; // τ, s
    HammerSpeeds hammerSpeeds;       // at velocities 32, 64 and 112 (SPEC §4.1)
    double maxHammerSpeed = 8.0;     // m/s, velocity 127 never exceeds it

    // Knock: the hammer's impact reaching the bridge directly, not through the strings. While the
    // hammer touches them, knockLevel × its force is added to the note's bridge output (0 = none),
    // through a one-pole low-pass at knockLowPassHz (0 = none), and never through the note's
    // high-pass: the knock's thump lies under the fundamental.
    double knockLevel = 0.0;
    double knockLowPassHz = 0.0;

    // How far the felt's contact follows the blow (1 = physical): the hammer meets the strings at
    // mf × (v / mf)^contactSpeedExponent, and the strike's drive is scaled by v over that, so
    // the note keeps the real blow's loudness while its brightness changes less with velocity.
    double contactSpeedExponent = 1.0;

    // This note's share of the model's unison spread.
    double unisonSpreadScale = 1.0;

    // Soft pedal (SPEC §2.7)
    double softPedalFeltScale = 1.0; // felt stiffness multiplier with the action fully shifted

    // Damper
    bool hasDamper = true;
    double damperDecayConstant = 0.0;       // extra σ with the damper resting: d0 + d1·√(f / 1 kHz)
    double damperDecayPerRootKilohertz = 0.0;
};

/** The complete set of values that describes one piano to the engine (GLOSSARY: Piano model). */
struct PianoModel
{
    std::array<NoteParameters, numNotes> notes {};

    /** Unison spread in cents at the Spread control's default (SPEC §2.3, §5). */
    double unisonSpreadCents = 0.7;

    /** Bridge force (N) to output sample. */
    double outputGain = 1.0;

    /** Tape-style saturation on the output, after the soundboard: y = (tanh(d·x + b) − tanh b) /
        (d·(1 − tanh² b)), unity gain for small signals, b = 0.15 for even harmonics. 0 = none. */
    double tapeDrive = 0.0;

    /** The sympathetic bank's physical level, the Resonance control's 50 (SPEC §2.6, §6.3): each
        bank mode's gain at its own frequency, from the bridge drive to the bridge output. */
    double sympatheticLevel = 0.0;

    [[nodiscard]] const NoteParameters& note (int midiNote) const noexcept { return notes[static_cast<std::size_t> (midiNote - lowestNote)]; }
};

/** Seed values standing in for data/model/concert-grand.json until the fitting pipeline
    (SPEC §3) produces it. They come from the listening-test prototype (Chaigne & Askenfelt seeds)
    and the CPU benchmark's Steinway-D-like curves, and are smooth across the keyboard.
    Tuning is plain equal temperament at A4 = 440 Hz until the tuner (SPEC §5) exists.
*/
[[nodiscard]] PianoModel makeSeedPianoModel();

namespace overrides
{
struct NoteOverride;
}

/** A note's parameters with its voicing overrides (VoicingOverrides.h) folded in, as the voicing
    lab applied them: the hammer as at a strike, the strings as applyStringVoicing(). Exactly p
    when every override is neutral. makeSeedPianoModel() applies the committed ones. */
[[nodiscard]] NoteParameters applyVoicingOverrides (const NoteParameters& p, const overrides::NoteOverride& o) noexcept;

/** The committed voicing's revision: how many voicing lab exports have been folded into it. */
[[nodiscard]] int voicingRevision() noexcept;

/** Partial n's frequency on the unison's average string: n·f0·√(1 + B·n²). */
[[nodiscard]] inline double partialFrequency (const NoteParameters& p, int partial) noexcept
{
    const double n = partial;
    return n * p.fundamental * std::sqrt (1.0 + p.inharmonicity * n * n);
}

/** The decay tilt's factor at a frequency: (f / f1)^decayTiltExponent. */
[[nodiscard]] inline double decayTilt (const NoteParameters& p, double frequency) noexcept
{
    return std::pow (frequency / partialFrequency (p, 1), p.decayTiltExponent);
}

/** The prompt sound's law at a frequency: its amplitude decay rate above the measured partials, 1/s. */
[[nodiscard]] inline double promptDecay (const NoteParameters& p, double frequency) noexcept
{
    const double rate = p.decayConstant + p.decayPerHertzSquared * frequency * frequency;
    return p.decayTiltExponent == 0.0 ? rate : rate * decayTilt (p, frequency);
}

/** The aftersound's law at a frequency, 1/s. */
[[nodiscard]] inline double aftersoundDecay (const NoteParameters& p, double frequency) noexcept
{
    const double rate = p.aftersoundDecayConstant + p.aftersoundDecayPerHertzSquared * frequency * frequency;
    return p.decayTiltExponent == 0.0 ? rate : rate * decayTilt (p, frequency);
}

[[nodiscard]] inline bool isMeasuredPartial (const NoteParameters& p, int partial) noexcept
{
    return partial >= 1 && partial <= p.numMeasuredPartials && partial <= NoteParameters::maxMeasuredPartials;
}

/** Partial n's prompt-sound decay rate, 1/s: its measured rate, or the law at its frequency. */
[[nodiscard]] inline double promptDecayOfPartial (const NoteParameters& p, int partial) noexcept
{
    if (! isMeasuredPartial (p, partial))
        return promptDecay (p, partialFrequency (p, partial));

    const double rate = p.measuredPromptDecay[static_cast<std::size_t> (partial - 1)];
    return p.decayTiltExponent == 0.0 ? rate : rate * decayTilt (p, partialFrequency (p, partial));
}

/** Partial n's aftersound decay rate, 1/s: its measured rate, or the law at its frequency. */
[[nodiscard]] inline double aftersoundDecayOfPartial (const NoteParameters& p, int partial) noexcept
{
    if (! isMeasuredPartial (p, partial))
        return aftersoundDecay (p, partialFrequency (p, partial));

    const double rate = p.measuredAftersoundDecay[static_cast<std::size_t> (partial - 1)];
    return p.decayTiltExponent == 0.0 ? rate : rate * decayTilt (p, partialFrequency (p, partial));
}

/** The extra decay rate with the damper resting, 1/s: d0 + d1·√(f / 1 kHz). */
[[nodiscard]] inline double damperDecay (const NoteParameters& p, double frequency) noexcept
{
    return p.damperDecayConstant + p.damperDecayPerRootKilohertz * std::sqrt (frequency / 1000.0);
}

/** Per-string tuning offsets in cents for a unison (SPEC §2.3). Fills offsets[0..numStrings). */
void unisonOffsetsCents (int numStrings, double spreadCents, double (&offsets)[3]) noexcept;
} // namespace forte::detail
