#include "PianoModel.h"

#include "LabMapping.h"
#include "VoicingOverrides.h"

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

/** One note's second polarisation (tools/fitting/fit_tail.py): per tail partial, the fast first
    stage's rate, the tail's rate (1/s) and the tail's level (dB re the partial's strike). */
struct TailEntry
{
    int midiNote;
    int partials;
    double fast[NoteParameters::maxTailPartials];
    double slow[NoteParameters::maxTailPartials];
    double levelDb[NoteParameters::maxTailPartials];
};

constexpr TailEntry tailTable[] {
#include "TailFit.inc"
};

/** Each note's level, dB, A0 to C8 (tools/fitting/fit_level.py). */
constexpr double levelTable[] {
#include "LevelFit.inc"
};
static_assert (std::size (levelTable) == numNotes);

const TailEntry* findTail (int midiNote) noexcept
{
    for (const auto& e : tailTable)
        if (e.midiNote == midiNote)
            return &e;
    return nullptr;
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

    // Inharmonicity: measured on the user's Keyscape sweep (issue 0021 §14), smoothed, its G6
    // slope held to C8. Lower than the old seed curve in the bass (C#1-E2 about half), higher from
    // the tenor up (1.2-1.8×).
    constexpr double bKeys[] { 21.0, 26.0, 31.0, 36.0, 41.0, 45.0, 91.0, 108.0 };
    constexpr double bLogs[] { -3.76, -3.90, -4.15, -4.12, -4.10, -4.04, -2.32, -1.68 };
    p.inharmonicity = std::pow (10.0, interpolate (key, bKeys, bLogs));

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

    // Decay, fitted to the user's long Keyscape sweep (tools/fitting/fit_decay.py, SPEC §3.3,
    // issue 0021 §15): the measured partials' prompt and aftersound rates, and the laws above them.
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

    // Voiced even: each note's strike and early ring as loud against its neighbours as Keyscape's
    // (issue 0022 §6).
    p.levelDb = levelTable[static_cast<std::size_t> (index)];

    // The second polarisation (issue 0022 §6), fitted to the same sweep: a fast first stage on
    // the vertical strings, and a slow tail. Each tail partial's vertical rates are the fast
    // stage's (prompt and aftersound alike: uncoupled), so the tail is the only aftersound.
    if (const auto* tail = findTail (midiNote))
    {
        p.numTailPartials = std::min ({ tail->partials, NoteParameters::maxTailPartials, p.numMeasuredPartials });
        for (int n = 0; n < p.numTailPartials; ++n)
        {
            const auto i = static_cast<std::size_t> (n);
            p.measuredPromptDecay[i] = tail->fast[n];
            p.measuredAftersoundDecay[i] = tail->fast[n];
            p.tailDecay[i] = tail->slow[n];
            p.tailLevel[i] = std::pow (10.0, tail->levelDb[n] / 20.0);
        }

        // A fixed few tenths of a cent, different on every note: the horizontal motion meets a
        // slightly different bridge, so the tail beats slowly against what's left of the strike.
        p.tailOffsetCents = 0.3 * std::sin (2.3 * midiNote);
    }

    // The knock per register, fitted to the Keyscape sweep's attack crest factor (peak in 50 ms
    // over the rms of 0.5 s, velocities 64 and 96): the strings alone already match Keyscape's up
    // to C4, and Keyscape's attacks get sharper from C5 up (issue 0021 §14).
    constexpr double knockKeys[] { 64.0, 78.0, 90.0, 102.0 };
    constexpr double knockLevels[] { 0.0, 0.14, 0.45, 0.17 };
    p.knockLevel = interpolate (key, knockKeys, knockLevels);
    p.knockLowPassHz = 2000.0;
    p.knockHighPassHz = 300.0;

    // The board's weak radiation of the lowest fundamentals (issue 0021 §18): A0's -27 dB, F1's
    // -14, C2's -3; the overtones, the bass's growl, are untouched.
    p.radiationCornerHz = 65.0;

    // The unison spread narrows from C4 up (fit_decay.py's spread_scale() must agree). With the
    // full spread, the treble's upper partials beat into a sustained glassy ring that Keyscape's
    // don't have; narrower, the decay fit matches the sweep 2-3× closer (issue 0021 §15).
    constexpr double spreadKeys[] { 60.0, 76.0, 88.0 };
    constexpr double spreadScales[] { 1.0, 0.35, 0.15 };
    p.unisonSpreadScale = interpolate (key, spreadKeys, spreadScales);

    // How far the contact follows the blow: the felt meets mf × (v / mf)^0.8 while the strings
    // still take the real blow's loudness. With the old voicing a physical felt (1) brightened far
    // faster with velocity than Keyscape (issue 0021 §15), so it was 0.5; against Keyscape after
    // the board of §19 that left the middle ~10 dB too bright in partials 8-15 at velocity 32 and
    // a few dB too dark at 127, and 0.8 fits best (§20).
    p.contactSpeedExponent = 0.8;

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
static_assert (std::size (overrides::notes) == numNotes);
static_assert (overrides::notes[0].midiNote == lowestNote && overrides::notes[numNotes - 1].midiNote == highestNote);
} // namespace

NoteParameters applyVoicingOverrides (const NoteParameters& p, const overrides::NoteOverride& o) noexcept
{
    // The hammer, as the lab's StrikeVoicing applies it at each strike (NoteModel::strike()).
    NoteParameters q = p;
    q.feltStiffness *= o.feltStiffnessScale;
    if (o.feltRelaxation >= 0.0)
        q.feltRelaxation = o.feltRelaxation;
    if (o.contactSpeedExponent >= 0.0)
        q.contactSpeedExponent = o.contactSpeedExponent;
    if (o.strikePositionScale != 1.0)
        q.strikePosition = std::clamp (p.strikePosition * o.strikePositionScale, 0.01, 0.5);
    q.hammerMass *= o.hammerMassScale;
    q.knockLevel *= o.knockScale;

    // The strings, as the lab re-prepares a note.
    StringVoicing strings;
    strings.inharmonicityScale = o.inharmonicityScale;
    strings.unisonSpreadScale = o.unisonSpreadScale;
    strings.sustainRateScale = o.sustainRateScale;
    strings.overtoneDecayExponent = o.overtoneDecayExponent;
    strings.aftersoundRateScale = o.aftersoundRateScale;
    strings.phantomLevelScale = o.phantomLevelScale;
    return applyStringVoicing (q, strings);
}

int voicingRevision() noexcept
{
    return overrides::revision;
}

PianoModel makeSeedPianoModel()
{
    PianoModel model;

    for (int midiNote = lowestNote; midiNote <= highestNote; ++midiNote)
    {
        const auto i = static_cast<std::size_t> (midiNote - lowestNote);
        model.notes[i] = applyVoicingOverrides (seedNote (midiNote), overrides::notes[i]);
    }

    model.unisonSpreadCents = 0.7;

    // The output's tape saturation, as the user's Keyscape renders have it (its default tape
    // saturator; issue 0021 §15): 0 = none, chosen by ear. The voicing lab may have replaced it.
    model.tapeDrive = overrides::tapeDrive >= 0.0 ? overrides::tapeDrive : 8.0;

    // Placeholder level until the soundboard and the fitted levels exist: an ff C4 peaks around
    // −15 dBFS, and a full 88-key sustain glissando at velocity 90 peaks just under 0 dBFS.
    model.outputGain = 5.6e-3;

    // Placeholder until Piano in 162 calibrates it (SPEC §3.3). 0.1 put a held note's pedal-down
    // minus pedal-up difference about 12–28 dB under the note from E1 to C6, and the bank at
    // −16.5 dB of a pedalled piece; by ear twice that (the old Resonance 100, with longer sustain
    // below A6) "fixes almost all the issues" (issue 0022 §6).
    model.sympatheticLevel = 0.2;
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
