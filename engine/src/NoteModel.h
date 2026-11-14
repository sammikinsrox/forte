#pragma once

#include "PianoModel.h"
#include "VelocityCurve.h"

#include <cstdint>
#include <vector>

namespace forte::detail
{
/** One of the 88 permanent note models (SPEC §2.2): the note's unison as modal strings, its
    hammer and its damper. There are no voices: a note-on always restrikes this model's current
    string state.

    Strings (SPEC §2.3): one resonator per partial per string, up to 20 kHz or 0.45 × the sample
    rate. Each string is detuned by its fixed unison offset, the same in cents on every partial.
    The output is the bridge force, taken from the string slope at the bridge end, so its sign
    alternates per partial. Notes above 100 Hz pass through a 2nd-order high-pass at f0/2.

    Hammer (SPEC §2.4): a mass on a felt spring F = K·(c^p + α·d(c^p)/dt), computed sample by
    sample at 4× oversampling while it's in contact with the strings.

    Damper (SPEC §2.5): a 0–1 lift adds per-partial damping (1 − lift)·σ_damper(f). Key-down
    lifts it at once; key-up lowers it over ~20 ms. The half-pedal gap model is linear in lift
    for now; its real shape is fitted and then set by ear.

    Resting (SPEC §2.2): a mode below −100 dBFS stops. Once every mode has stopped and the
    hammer has left the strings, the note is idle and costs nothing.

    Not yet modelled: coupled unisons (two-stage decay), phantom partials, the soft pedal's
    hammer shift, tuning glides.

    Each mode is a complex one-pole z ← z·e^{(−σ + iω)T} + drive·T·F, whose imaginary part is the
    modal displacement. That form lets the hammer step at T/4 and the damper change σ without
    disturbing the mode's phase or amplitude.

    Threading: prepare() allocates and runs off the audio thread. Everything else is realtime-safe.
*/
class NoteModel
{
public:
    /** Control-rate work (damper ramps, resting) runs every this many samples. */
    static constexpr int controlInterval = 32;

    /** A mode quieter than this at the output stops computing: −100 dBFS (SPEC §2.2). */
    static constexpr float restingAmplitude = 1e-5f;

    /** Rebuilds the note for a sample rate, and silences it. Allocates. */
    void prepare (const NoteParameters& parameters, double unisonSpreadCents, double outputGain, double sampleRate);

    /** Stops the strings and the hammer at once (CC120). Key and damper state are unchanged. */
    void silence() noexcept;

    /** Silences the note and puts its key and damper back at rest, with no ramp. */
    void reset() noexcept;

    /** Key-down lifts the damper at once. Key-up starts the damper's ~20 ms ramp down. */
    void setKeyDown (bool down) noexcept;

    /** A hammer blow at a MIDI velocity (1–127, fractional allowed), on the current string state. */
    void strike (double velocity, double touch) noexcept;

    /** Runs the note's control-rate work. Call at every controlInterval boundary of absolute
        sample time, so that output doesn't depend on how the host splits blocks. */
    void controlTick (float sustainLift) noexcept;

    /** Adds numSamples (≤ controlInterval) of this note's output to out. scratch must hold
        numSamples floats. */
    void render (float* out, float* scratch, int numSamples) noexcept;

    /** True while the note makes any sound or its hammer is moving. */
    [[nodiscard]] bool isSounding() const noexcept { return sounding; }

    [[nodiscard]] int getNumModes() const noexcept { return numModes; }

private:
    void updateMultipliers() noexcept;
    float renderContactSample() noexcept;
    void renderFree (float* out, int numSamples) noexcept;
    void restQuietModes() noexcept;
    [[nodiscard]] float strikePointDisplacement() const noexcept;

    // Fixed after prepare()
    int numModes = 0;
    bool hasDamper = true;
    double samplePeriod = 1.0 / 48000.0;
    float liftStepPerTick = 1.0f;
    double hammerMass = 3e-3;
    double feltStiffness = 0.0;
    double feltExponent = 2.5;
    double feltHysteresis = 0.0;
    int maxContactSubsteps = 0;
    VelocityCurve velocityCurve;

    bool hasHighPass = false;
    float hpB0 = 1.0f, hpB1 = 0.0f, hpB2 = 0.0f, hpA1 = 0.0f, hpA2 = 0.0f;

    // Per mode, fixed after prepare(). Modes are ordered by partial, then string, so the
    // upper partials, which rest first, sit at the end.
    std::vector<float> cosStep, sinStep;          // e^{iωT}
    std::vector<float> cosSubstep, sinSubstep;    // e^{iωT/4}
    std::vector<float> stringDecay;               // −σ_string·T
    std::vector<float> damperDecay;               // −σ_damper·T
    std::vector<float> drive;                     // displacement per (N·s) of hammer force
    std::vector<float> strikeWeight;              // share in the hammer's view of the strings
    std::vector<float> outputWeight;              // bridge-end slope × tension × output gain

    // Per mode, live
    std::vector<float> stateRe, stateIm;
    std::vector<float> stepRe, stepIm;            // multiplier per sample
    std::vector<float> substepRe, substepIm;      // multiplier per hammer substep
    int activeEnd = 0;                            // modes at and above this index are at rest

    // Key and damper
    bool keyDown = false;
    float keyLift = 0.0f;
    float lift = 0.0f;                            // effective damper lift, 0 = resting, 1 = clear

    // Hammer
    bool inContact = false;
    double hammerPosition = 0.0;  // m, along the strings' displacement
    double hammerVelocity = 0.0;  // m/s
    double previousCompressionPower = 0.0;
    int contactSubsteps = 0;
    int separatedSubsteps = 0;

    // High-pass state (transposed direct form II)
    float hpState1 = 0.0f, hpState2 = 0.0f;

    bool sounding = false;
};
} // namespace forte::detail
