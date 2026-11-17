#pragma once

#include "Kernels.h"
#include "LabMapping.h"
#include "PianoModel.h"
#include "VelocityCurve.h"

#include <array>
#include <cstdint>
#include <vector>

namespace forte::detail
{
/** One of the 88 permanent note models (SPEC §2.2): the note's unison as modal strings, its
    hammer and its damper. There are no voices: a note-on always restrikes this model's current
    string state.

    Strings (SPEC §2.3): N resonators per partial for an N-string unison, up to 20 kHz or
    0.45 × the sample rate. Each string is detuned by its fixed unison offset, the same in cents
    on every partial. For partials 1–32 of 2- and 3-string unisons the strings are coupled
    through the bridge (Weinreich), so the N resonators are the unison's coupled modes and give
    the two-stage decay; see UnisonModes. The coupling makes up the difference between the
    partial's prompt-sound and aftersound decay. Above partial 32, and on single strings, each
    resonator is one string decaying at the prompt-sound rate.
    The output is the bridge force, taken from the string slope at the bridge end, so its sign
    alternates per partial. Notes above 100 Hz pass through a 2nd-order high-pass at f0/2.

    Phantom partials (SPEC §2.3): on notes with a phantom level, the strings' stretching pulls on
    the bridge (Bank, Zambon & Fontana 2010). The force is the string's uniform tension, the
    squared sum of partials 1–30's mean slopes, plus the dynamic response of the longitudinal
    modes (at k × the longitudinal frequency, up to 10) that resonate inside the tension's band.
    Odd modes share one drive and even modes another. The force passes a 40 Hz high-pass to the
    bridge. The path runs at every sample, including during hammer contact.

    Hammer (SPEC §2.4): a mass on a hysteretic felt, computed sample by sample at 4× oversampling
    while it's in contact with the strings. The felt is Stulov's hereditary model,
    F = Q0·[c^p − (ε/τ)·∫ c^p(ξ)·e^{−(t−ξ)/τ} dξ]: compression changes faster than τ meet the
    instantaneous stiffness Q0, slower ones the relaxed Q0·(1 − ε). The faster the blow, the
    stiffer the felt it meets, so brightness rises steeply with velocity, as a real felt's does.
    The force never exceeds Q0·c^p and never pulls. It is shared by the strings the hammer
    strikes, and it feels their mean displacement.

    Soft pedal (SPEC §2.7): a 0–1 shift of the action, read at each strike. On 2- and 3-string
    unisons the hammer's share of string 0 fades to nothing as the shift goes to 1, so it strikes
    one string of two or two of three, and the missed string rings only through the coupling.
    On every note the felt stiffness moves towards softPedalFeltScale × K.

    Damper (SPEC §2.5): a 0–1 lift adds per-partial damping (1 − lift)·σ_damper(f). It damps
    every string of a unison alike, so it doesn't change the coupled modes' shapes. Key-down
    lifts it at once; key-up lowers it over ~20 ms. The half-pedal gap model is linear in lift
    for now; its real shape is fitted and then set by ear.

    Resting (SPEC §2.2): a mode below −100 dBFS stops. Once every mode has stopped, the
    longitudinal modes have died away and the hammer has left the strings, the note is idle and
    costs nothing.

    Retuning (SPEC §5): setPitchRatio() moves every mode's frequency by one ratio, keeping the
    strings' state, so a ringing note bends to a new pitch with no click. The hammer drives and
    the high-pass stay as prepared; the engine re-prepares the note exactly the next time it
    starts from rest.

    Each mode is a complex one-pole w ← w·e^{(−σ + iω)T} + drive·T·F. Uncoupled, Im w is the
    string's displacement; coupled, the strings' displacements are Im(shape·w), so the hammer's
    drive into a mode and its view of it are complex. The bridge weights are real either way
    (see UnisonModes), so free-running modes cost the same as uncoupled strings. That form lets
    the hammer step at T/4 and the damper change σ without disturbing a mode's phase or amplitude.

    Free-running modes advance mode by mode through each render (Kernels::freeModes), each held in
    registers across the samples, so a note's state passes through memory once per render rather
    than once per sample. Modes something reads every sample (the hammer's, while it's in
    contact; the phantom partials' drivers) advance sample by sample.

    Threading: prepare() allocates and runs off the audio thread. Everything else is realtime-safe.
*/
class NoteModel
{
public:
    /** Control-rate work (damper ramps, resting) runs every this many samples. */
    static constexpr int controlInterval = 32;

    /** A mode quieter than this at the output stops computing: −100 dBFS (SPEC §2.2). */
    static constexpr float restingAmplitude = 1e-5f;

    /** Partials 1 to this many are coupled across the unison (SPEC §2.3). */
    static constexpr int coupledPartials = 32;

    /** Partials 1 to this many drive the phantom partials. */
    static constexpr int phantomDrivingPartials = 30;

    /** Longitudinal modes behind the phantom partials. */
    static constexpr int maxLongitudinalModes = 10;

    /** Rebuilds the note for a sample rate, and silences it. Allocates. */
    void prepare (const NoteParameters& parameters, double unisonSpreadCents, double outputGain, double sampleRate);

    /** Stops the strings, the phantom partials and the hammer at once (CC120). Key and damper
        state are unchanged. */
    void silence() noexcept;

    /** Silences the note and puts its key and damper back at rest, with no ramp. */
    void reset() noexcept;

    /** Key-down lifts the damper at once. Key-up starts the damper's ~20 ms ramp down. */
    void setKeyDown (bool down) noexcept;

    /** A hammer blow at a MIDI velocity (1–127, fractional allowed), on the current string state,
        with the soft pedal's action shift (0–1) at this moment and the Hardness control's felt
        stiffness multiplier. */
    void strike (double velocity, double touch, double softShift, double hardnessScale = 1.0) noexcept;

    /** Every mode at ratio × its prepared frequency. A sounding note moves now; one at rest
        moves at its next strike, so retuning silent notes costs nothing. Realtime-safe. */
    void setPitchRatio (double ratio) noexcept;

    [[nodiscard]] double getPitchRatio() const noexcept { return pitchRatio; }

    /** The tuning the note was prepared at, as the engine's tuner ratio (see Tuner.h). The engine
        keeps it with the model, so a model built on another thread brings its own. */
    void setBuiltTuning (double tuning) noexcept { builtTuning = tuning; }
    [[nodiscard]] double getBuiltTuning() const noexcept { return builtTuning; }

    /** The voicing lab's hammer (SPEC §6.5), read at the next strike and held for that strike's
        whole contact. prepare() sets the committed one. */
    void setStrikeVoicing (const StrikeVoicing& voicing) noexcept { strikeVoicing = voicing; }

    /** What a re-prepared note (the voicing lab's strings, SPEC §6.5) keeps from the model it
        replaces: the key, the damper and the hammer's voicing. Both must be at rest. */
    void takeControlStateFrom (const NoteModel& other) noexcept
    {
        keyDown = other.keyDown;
        keyLift = other.keyLift;
        lift = other.lift;
        strikeVoicing = other.strikeVoicing;
        kernels = other.kernels;
    }

    /** The hot loops' instruction set (Kernels.h): the CPU's best until set. Not while rendering. */
    void setKernels (const Kernels& k) noexcept { kernels = &k; }
    [[nodiscard]] const Kernels& getKernels() const noexcept { return *kernels; }

    /** Runs the note's control-rate work. Call at every controlInterval boundary of absolute
        sample time, so that output doesn't depend on how the host splits blocks. */
    void controlTick (float sustainLift) noexcept;

    /** Adds numSamples (≤ controlInterval) of this note's output to out. scratch must hold
        numSamples floats. */
    void render (float* out, float* scratch, int numSamples) noexcept;

    /** True while the note makes any sound or its hammer is moving. */
    [[nodiscard]] bool isSounding() const noexcept { return sounding; }

    /** The damper's effective lift as of the last control tick (or key-down): 0 = resting,
        1 = clear. The sympathetic bank is gated by it. */
    [[nodiscard]] float getLift() const noexcept { return lift; }

    [[nodiscard]] int getNumModes() const noexcept { return numModes; }
    [[nodiscard]] bool hasPhantomPartials() const noexcept { return numDrivingPartials > 0; }

private:
    void updateMultipliers() noexcept;
    void retuneSteps() noexcept;
    void retuneSubsteps() noexcept;
    float renderContactSample (float& knock) noexcept;
    void renderFree (float* out, int numSamples) noexcept;
    float phantomSample() noexcept;
    void restQuietModes() noexcept;
    [[nodiscard]] bool phantomsAtRest() const noexcept;
    [[nodiscard]] float strikePointDisplacement() const noexcept;

    const Kernels* kernels = &bestKernels();

    // Fixed after prepare()
    int numModes = 0;
    int paddedModes = 0;  // numModes rounded up to kernelModeAlignment; the per-mode arrays' size
    int numStrings = 1;
    int numTails = 0;     // partials with a tail mode (the second polarisation), 1 to numTails
    int tailModes = 0;    // the tail block at the front of the modes: numTails, padded to a group
    bool hasDamper = true;
    double samplePeriod = 1.0 / 48000.0;
    float liftStepPerTick = 1.0f;
    double hammerMass = 3e-3;
    double feltStiffness = 0.0;
    double feltExponent = 2.5;
    bool feltRelaxes = false;      // τ > 0: ε has an effect
    double feltMemoryDecay = 0.0;  // e^{−(T/4)/τ}
    double strikePosition = 0.125;
    StrikeVoicing strikeVoicing;
    double softPedalFeltScale = 1.0;
    float knockGain = 0.0f;                       // knockLevel × output gain
    float knockPole = 0.0f;                       // the knock low-passes' e^{−2π·fc·T}, 0 = none
    float knockHighPole = 1.0f;                   // the knock high-passes' e^{−2π·fc·T}, 1 = none
    double mfHammerSpeed = 2.0;                   // velocity 64's hammer speed, m/s
    int maxContactSubsteps = 0;
    VelocityCurve velocityCurve;

    bool hasHighPass = false;
    float hpB0 = 1.0f, hpB1 = 0.0f, hpB2 = 0.0f, hpA1 = 0.0f, hpA2 = 0.0f;

    // Per mode, fixed after prepare(). Modes are ordered by partial, then by mode within the
    // partial, so the upper partials, which rest first, sit at the end.
    std::vector<double> modeOmega;                // ω as prepared, rad/s
    std::vector<float> cosStep, sinStep;          // e^{iωT}, ω × the pitch ratio
    std::vector<float> cosSubstep, sinSubstep;    // e^{iωT/4}

    // Retuning: the ratio cosStep and friends are at (or will be, once stepsStale and
    // substepsStale are cleared), and the engine's tuning the note was prepared at.
    double pitchRatio = 1.0;
    bool stepsStale = false;
    bool substepsStale = false;
    double builtTuning = 1.0;
    std::vector<float> stringDecay;               // −σ·T
    std::vector<float> damperDecay;               // −σ_damper·T
    std::vector<float> outputWeight;              // bridge-end slope × bridge weight × output gain

    // Per mode, the hammer's complex drive (displacement per N·s) and view (strike-point
    // displacement per unit of the mode), with every string struck and for string 0 alone.
    // strike() mixes them for the soft pedal's shift.
    std::vector<float> driveAllRe, driveAllIm, driveMissedRe, driveMissedIm;
    std::vector<float> viewAllRe, viewAllIm, viewMissedRe, viewMissedIm;

    // Per partial: sin(nπ·x) at the committed strike position x (the drives and views above
    // include it), and the lab's strike's over it, filled at a strike with a moved position.
    std::vector<double> partialStrikeShape;
    std::vector<float> strikeShapeRatio;

    // Per mode, live
    std::vector<float> stateRe, stateIm;
    std::vector<float> stepRe, stepIm;            // multiplier per sample
    std::vector<float> substepRe, substepIm;      // multiplier per hammer substep
    std::vector<float> driveRe, driveIm;          // this strike's drive
    std::vector<float> strikeWeightRe, strikeWeightIm;  // this strike's view
    int activeEnd = 0;                            // modes at and above this index are at rest

    // Phantom partials, fixed after prepare()
    int numLongitudinalModes = 0;                 // 0 = this note has none
    int numDrivingPartials = 0;
    std::vector<float> phantomWeight;             // per driving mode: partial number × bridge weight
    float phantomForceScale = 0.0f;               // Σ P_m·P_n → longitudinal force, in output units
    float phantomHpB0 = 1.0f, phantomHpA1 = 0.0f; // 1st-order 40 Hz high-pass, b1 = −b0

    // The phantom path's sums and longitudinal modes run over fixed, zero-padded widths, so they
    // vectorise. Unused lanes have zero coefficients and stay at zero.
    static constexpr int motionLanes = 32;        // ≥ phantomDrivingPartials, a multiple of 8
    static constexpr int longitudinalLanes = 16;  // ≥ maxLongitudinalModes, a multiple of 8
    static_assert (motionLanes >= phantomDrivingPartials && motionLanes % 8 == 0);
    static_assert (longitudinalLanes >= maxLongitudinalModes && longitudinalLanes % 8 == 0);
    std::array<float, longitudinalLanes> lmB0 {}, lmB1 {}, lmA1 {}, lmA2 {};  // b2 = b0
    std::array<float, longitudinalLanes> lmOdd {}, lmEven {};  // 1 where the mode takes that drive
    std::array<float, longitudinalLanes> lmSign {};            // the dynamic response's sign, 0 = unused

    // Phantom partials, live
    std::array<float, motionLanes + 8> partialMotion {};  // P_n at [n − 1], zero beyond the driving partials
    std::array<float, longitudinalLanes> lmState1 {}, lmState2 {};
    float phantomHpInput = 0.0f, phantomHpOutput = 0.0f;

    // Key and damper
    bool keyDown = false;
    float keyLift = 0.0f;
    float lift = 0.0f;                            // effective damper lift, 0 = resting, 1 = clear

    // Hammer
    bool inContact = false;
    double hammerPosition = 0.0;  // m, along the strings' displacement
    double hammerVelocity = 0.0;  // m/s
    double strikeFeltStiffness = 0.0;  // this strike's, as are the next three
    double strikeFeltRelaxation = 0.0;
    double strikeHammerMass = 3e-3;
    float strikeKnockGain = 0.0f;
    double feltMemory = 0.0;      // c^p low-passed with the felt's relaxation time
    // The knock's band-pass: two one-pole low-passes, then two one-pole high-passes.
    float knockLow1 = 0.0f, knockLow2 = 0.0f;
    float knockHighIn1 = 0.0f, knockHighOut1 = 0.0f, knockHighIn2 = 0.0f, knockHighOut2 = 0.0f;
    [[nodiscard]] float knockFilter (float x) noexcept;
    [[nodiscard]] bool knockQuiet() const noexcept;
    void clearKnock() noexcept;
    float strikeGain = 1.0f;      // the real blow's speed over the contact's
    int contactSubsteps = 0;
    int separatedSubsteps = 0;

    // High-pass state (transposed direct form II)
    float hpState1 = 0.0f, hpState2 = 0.0f;

    bool sounding = false;
};

/** How long the note can ring once struck, from full scale to rest with its damper lifted, at
    any soft-pedal shift. Depends only on the note's parameters, not on the sample rate. Not
    realtime: it solves the coupled modes. */
[[nodiscard]] double noteTailSeconds (const NoteParameters& parameters, double unisonSpreadCents);

/** The slowest amplitude decay rate (1/s) of any of the note's string modes up to 20 kHz, with
    its damper lifted. Not realtime: it solves the coupled modes. */
[[nodiscard]] double slowestStringDecay (const NoteParameters& parameters, double unisonSpreadCents);
} // namespace forte::detail
