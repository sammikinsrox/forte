#pragma once

#include "PianoModel.h"

#include <array>
#include <xmmintrin.h>

namespace forte::detail
{
/** The sympathetic bank (SPEC §2.6): for each key, a separate set of up to 12 modes that stand in
    for that key's strings ringing in sympathy.

    Modes: the key's partials 1–12 on its average string (the model's B), up to 20 kHz or
    0.45 × the sample rate. Each decays at the prompt-sound law σ = c1 + c3·f². The bridge moves
    a unison's strings together, so it drives their in-phase motion, which is the mode that
    decays at the prompt rate (see NoteModel's coupling).

    Drive: the summed bridge force of every played note, in all four regions, less the key's own
    note. One soundboard carries the whole bridge, so a bass note rings a treble key's bank. A
    key's own strings are its played note, so its bank never hears them; that would only
    reinforce the note's own partials. The bank's output joins the key's own bridge region.
    Energy flows one way: banks never feed each other or the played strings (SPEC §2.1).

    Level: each mode's gain at its own frequency, from the drive to the bridge output, is the
    model's sympatheticLevel at Resonance 50 (0 = none, 62.5 = double, 100 = ×16, SPEC §6.3). A mode's
    response falls off away from its frequency within its decay's bandwidth.

    Damper gate: the key's damper lift (key and sustain pedal, half-pedal included) scales the
    drive and adds (1 − lift)·σ_damper(f) to each mode's decay, as on the strings. A resting
    damper lets nothing in, and the bank dies away at the damped rate.

    Resting: a bank that wasn't driven during a control interval and whose modes are all below
    −100 dBFS at the output stops, and costs nothing until it's driven again. A bank with its
    damper resting is never driven, so a damped, decayed bank is idle.

    Resonance: setResonance() changes it at once; the engine glides it (SPEC §2.9).

    Retuning (SPEC §5): setPitchRatio() moves a key's modes by one ratio, keeping their state.

    Threading: everything is fixed-size, so nothing here allocates. prepare() runs off the audio
    thread; the rest is realtime-safe.
*/
class SympatheticBank
{
public:
    static constexpr int modesPerKey = 12;  // SPEC §2.6

    /** Lanes per key: room for the modes. A key runs its modes rounded up to a whole group of
        lanes (one SSE register); the padding has zero coefficients and stays silent. The loop is
        written in SSE, x64's baseline: run per key and per sample, the compiler unrolls it to
        scalar code. */
    static constexpr int lanes = 16;
    static constexpr int laneGroup = 4;
    static_assert (lanes >= modesPerKey && lanes % laneGroup == 0);

    static constexpr float restingAmplitude = 1e-5f;  // −100 dBFS, as the strings (SPEC §2.2)
    static constexpr double maxResonance = 100.0;

    /** Builds every key's bank for a sample rate and silences it. */
    void prepare (const PianoModel& model, double sampleRate) noexcept;

    /** Stops every bank ringing at once (CC120). Damper state is unchanged. */
    void silence() noexcept;

    /** Silences every bank and puts its damper gate back at rest. */
    void reset() noexcept;

    /** 0–100, 50 = physical (SPEC §6.3). Not smoothed. */
    void setResonance (double resonance) noexcept;

    /** The bank's level for a Resonance: 0 at 0, 1 at 50 (linear), then ×2 every 12.5: 2 at
        62.5, 4 at 75, 16 at 100. */
    [[nodiscard]] static double levelOf (double resonance) noexcept;

    /** Every mode of key (0–87) at ratio × its prepared frequency. Realtime-safe. */
    void setPitchRatio (int key, double ratio) noexcept;

    /** Runs key's control-rate work with its damper's lift (0–1). Call at every control
        interval boundary of absolute sample time, after the notes' own ticks. key is 0–87. */
    void controlTick (int key, float lift) noexcept;

    /** The most samples render() takes at once: one control interval. */
    static constexpr int maxRenderSamples = 32;

    /** Adds numSamples (≤ maxRenderSamples) of every bank's output to its key's region in out.
        drive is the summed bridge force of every played note, or nullptr if no note sounded over
        these samples; own[key] is that key's own note's share of it, or nullptr if it didn't
        sound. */
    void render (const float* drive, const std::array<const float*, numNotes>& own,
                 const std::array<float*, numBridgeRegions>& out, int numSamples) noexcept;

    [[nodiscard]] bool isActive (int key) const noexcept { return keys[static_cast<std::size_t> (key)].active; }
    [[nodiscard]] int getNumModes (int key) const noexcept { return keys[static_cast<std::size_t> (key)].numModes; }

private:
    struct Key
    {
        // Per lane, fixed after prepare(). Unused lanes have zero coefficients and stay silent.
        alignas (32) std::array<float, lanes> cosStep {}, sinStep {};  // e^{iωT}
        alignas (32) std::array<float, lanes> stringDecay {};          // −σ·T
        alignas (32) std::array<float, lanes> damperDecay {};          // −σ_damper·T
        alignas (32) std::array<float, lanes> outputWeight {};         // sets the gain at resonance
        alignas (32) std::array<double, lanes> theta {};               // ωT as prepared

        // Per lane, live
        alignas (32) std::array<float, lanes> stepRe {}, stepIm {};
        alignas (32) std::array<float, lanes> stateRe {}, stateIm {};

        int numModes = 0;
        int usedLanes = 0;         // numModes rounded up to a laneGroup
        int region = 0;            // the key's bridge region
        float restingLift = 0.0f;  // 1 on undamped keys
        float lift = 0.0f;
        bool active = false;       // computing: its state isn't all zeros, or it's being driven
        bool driven = false;       // drive reached it since the last control tick
    };

    template <int Groups>
    void renderKey (Key& key, const float* x, __m128* accumulator, int numSamples) noexcept;

    static void updateMultipliers (Key& key) noexcept;
    static void silenceKey (Key& key) noexcept;

    std::array<Key, numNotes> keys {};
    std::array<double, numNotes> pitchRatios {};  // each key's, set by setPitchRatio()
    float level = 1.0f;  // Resonance / 50
};

/** How long the strings and the bank together can ring once struck, given the strings alone ring
    for notesTailSeconds at most and decay no slower than slowestStringDecay (1/s), at any
    Resonance up to 100. Each bank mode is taken against a string mode at its own frequency with
    the slowest decay, from full scale; several string modes landing on one bank mode at once are
    not covered. Not realtime. */
[[nodiscard]] double sympatheticTailSeconds (const PianoModel& model, double notesTailSeconds, double slowestStringDecay);
} // namespace forte::detail
