#pragma once

#include "forte/engine/Engine.h"

#include <array>
#include <cstdint>
#include <vector>

namespace forte::detail
{
/** The room (SPEC §2.11): early reflections and a late reverberant tail around the piano, after
    the tape and before Width and Volume. The soundboard is the instrument; the room is where it
    stands.

    - Pre-delay, then early reflections: ten taps per side from both channels, spaced by Size,
      through a one-pole low-pass set by Damping, at the Early level.
    - The late tail: the pre-delayed input and the early reflections, through four series
      all-passes per side (diffusion), into an 8-line feedback delay network with a Hadamard
      mix. Each line's gain gives the Decay time (RT60 at low frequencies) for its length, and a
      one-pole low-pass in the loop shortens the highs by Damping (Jot). Each line's length
      wanders slowly (±0.25 ms, 0.1-0.9 Hz) so no mode rings on: no metallic ring.
    - The wet sound is added to the dry at a gain set by Room (0 = the dry signal, untouched).

    Every sample's work depends only on the samples before it and on a parameter clock that
    counts from reset(), so the output doesn't depend on how blocks are split (SPEC §7.4).
    Parameters glide (~50 ms) on that clock, every tickSamples samples.

    Asleep (no work, output = input) while Room is 0, and once the input has been silent for
    longer than the tail; it wakes with cleared state on the first non-silent sample.

    Threading: prepare() allocates; the rest is realtime-safe. */
class Room
{
public:
    static constexpr int numLines = 8;
    static constexpr int tapsPerSide = 10;
    static constexpr int numAllpasses = 4;
    static constexpr int tickSamples = 32;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** New targets; they glide. Before prepare(), or with jump, they're taken at once. */
    void setControls (const RoomControls& controls, bool jump) noexcept;

    /** Adds the room to left and right in place. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** How long the room rings after its input stops, for these controls (s). 0 when Room is 0. */
    [[nodiscard]] static double tailSeconds (const RoomControls& controls) noexcept;

private:
    struct Params
    {
        double amount = 0.0, size = 50.0, decay = 1.2, damping = 50.0, predelayMs = 10.0, early = 50.0;
    };

    struct DelayLine
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;

        void allocate (int minimumLength);
        void clear() noexcept;
        void push (float x) noexcept { buffer[static_cast<std::size_t> (write)] = x; write = (write + 1) & mask; }
        /** The sample pushed delay samples ago (delay ≥ 1), linearly interpolated. */
        [[nodiscard]] float read (double delay) const noexcept;
    };

    struct Allpass
    {
        DelayLine line;
        double delay = 1.0;
    };

    static Params paramsOf (const RoomControls& c) noexcept;
    void glide() noexcept;
    void updateCoefficients() noexcept;
    void wake() noexcept;

    double sampleRate = 0.0;
    bool prepared = false;

    Params target, now;
    double glideCoefficient = 0.0;
    float wetGain = 0.0f, wetGainFrom = 0.0f;  // per-sample ramp within a tick
    int tickPosition = 0;

    // Derived, per tick.
    double scale = 1.0;
    std::array<double, tapsPerSide * 2> tapDelay {};  // left taps, then right taps (samples)
    std::array<float, tapsPerSide * 2> tapGain {};
    std::array<double, numLines> lineDelay {};
    std::array<float, numLines> lineGain {}, lineDampA {};
    float earlyDampA = 0.0f, earlyGain = 0.0f, lateGain = 0.0f;
    double predelaySamples = 0.0;

    // State.
    DelayLine inputLeft, inputRight;
    std::array<Allpass, numAllpasses * 2> allpasses;
    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> lineDampState {};
    std::array<double, numLines> lfoRe {}, lfoIm {}, lfoStepRe {}, lfoStepIm {};
    float earlyStateL = 0.0f, earlyStateR = 0.0f;
    float highPassStateL = 0.0f, highPassStateR = 0.0f, highPassInL = 0.0f, highPassInR = 0.0f;
    float highPassA = 0.0f;
    double modulationDepth = 0.0;

    bool asleep = true;
    std::int64_t silentSamples = 0;
    std::int64_t tailSamples = 0;
    double targetTailSeconds = 0.0;
};
} // namespace forte::detail
