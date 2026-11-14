#pragma once

#include "VelocityCurve.h"

#include <array>

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

/** Everything the engine needs to build one note model (SPEC §2.3–2.5). */
struct NoteParameters
{
    int midiNote = 69;

    // Strings
    double fundamental = 440.0;      // Hz. The average string's first partial is fundamental·√(1 + B).
    double inharmonicity = 0.0;      // B: partial n sits at n·fundamental·√(1 + B·n²)
    int numStrings = 3;              // strings in the unison
    double stringLength = 0.62;      // m
    double stringTension = 700.0;    // N

    // Decay (SPEC §2.3, §3.3), as amplitude decay rates σ = c1 + c3·f² in 1/s. The prompt sound
    // is how fast a partial decays on its own: the whole decay of a single string or an
    // uncoupled partial, and the first stage of a coupled one. The aftersound is one string's
    // own, slower decay, and the coupling through the bridge (SPEC §3.3) makes up the difference.
    double decayConstant = 1.0;                  // prompt sound: c1
    double decayPerHertzSquared = 0;             // prompt sound: c3, s
    double aftersoundDecayConstant = 0.25;       // aftersound: c1
    double aftersoundDecayPerHertzSquared = 0;   // aftersound: c3, s

    // Phantom partials (SPEC §2.3): longitudinal modes driven by the strings' stretching
    double phantomLevel = 0.0;              // 0 = none
    double longitudinalFrequency = 1100.0;  // Hz, the first longitudinal mode; mode k is at k times this
    double longitudinalQ = 60.0;
    double axialStiffness = 3.6e5;          // EA, N

    // Hammer
    double strikePosition = 0.125;   // fraction of the string length from the agraffe
    double hammerMass = 3e-3;        // kg
    double feltStiffness = 4.5e9;    // K in F = K·compression^p
    double feltExponent = 2.5;       // p
    double feltHysteresis = 1e-5;    // s
    HammerSpeeds hammerSpeeds;       // at velocities 32, 64 and 112 (SPEC §4.1)
    double maxHammerSpeed = 8.0;     // m/s, velocity 127 never exceeds it

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

    [[nodiscard]] const NoteParameters& note (int midiNote) const noexcept { return notes[static_cast<std::size_t> (midiNote - lowestNote)]; }
};

/** Seed values standing in for data/model/concert-grand.json until the fitting pipeline
    (SPEC §3) produces it. They come from the listening-test prototype (Chaigne & Askenfelt seeds)
    and the CPU benchmark's Steinway-D-like curves, and are smooth across the keyboard.
    Tuning is plain equal temperament at A4 = 440 Hz until the tuner (SPEC §5) exists.
*/
[[nodiscard]] PianoModel makeSeedPianoModel();

/** Per-string tuning offsets in cents for a unison (SPEC §2.3). Fills offsets[0..numStrings). */
void unisonOffsetsCents (int numStrings, double spreadCents, double (&offsets)[3]) noexcept;
} // namespace forte::detail
