#pragma once

#include "Fft.h"
#include "Kernels.h"
#include "PianoModel.h"
#include "SoundboardIrs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace forte::detail
{
/** The soundboard (SPEC §2.8): each bridge region's bus is convolved with its stereo IR from
    the active Listening perspective, tilted by Brightness, and summed. The engine's output stage
    (tape, Width, Volume) follows.

    Convolution: uniformly partitioned, in the frequency domain, with no added latency. An IR's
    first partitionSize taps run directly in the time domain; the rest are partitions of
    partitionSize, each multiplied against a frequency-domain delay line of the region's past
    input blocks (overlap-save, FFT size 2 × partitionSize). The FFT work for a block happens when
    its last sample arrives and gives the next block's contribution from the later partitions,
    so output is never delayed. The partitions sit on a fixed grid of absolute sample time, like
    the engine's control ticks, so output doesn't depend on how the host splits blocks.

    A region whose input has been silent for the IR's whole length is skipped: its delay line
    holds only zeros, so skipping changes nothing. A silent piano costs nothing here.

    Perspective: only the active set's partitions run. A switch starts at the next partition
    boundary and crossfades (raised cosine, ~50 ms) from the old set's output to the new one's,
    both running on the same delay line meanwhile. Switching back mid-fade reverses it from
    where it is.

    New IRs (the voicing lab's Board controls, SPEC §6.5): a Bank holds both perspectives'
    partitioned IRs. One is built off the audio thread (makeBank()) and offered; at the next
    partition boundary with no fade running, the soundboard takes it and crossfades to it as it
    does to a perspective, and hands back the bank it last faded from, to be freed off the audio
    thread. A perspective switch asked for during that fade waits for it to end. The handover is
    by atomic exchange of pointers, so the audio thread never allocates, frees or waits.

    Brightness is a first-order high shelf on each region's output: 0 dB at 50, ±12 dB at
    10 kHz at 100 and 0 (the exact span is set by ear). It's linear, so it would be cheaper on
    each region's input, but after the convolution a change reaches sound that's already ringing.
    setBrightness() changes it at once; the engine glides it in small steps (SPEC §2.9). At 50
    the shelf is off, and its state follows the signal, so it comes back on without a step.

    Threading: prepare() allocates and runs off the audio thread; the rest is realtime-safe.
*/
class Soundboard
{
public:
    static constexpr int partitionSize = 128;
    static constexpr int fftSize = 2 * partitionSize;
    static constexpr double crossfadeSeconds = 0.05;

private:
    struct Channel
    {
        std::array<float, partitionSize> headReversed {};  // taps 0..partitionSize−1, last first
        std::vector<float> partitions;                      // numPartitions packed spectra, 1/fftSize scaled
        std::array<float, partitionSize> tail {};           // this block's output from the partitions
    };

public:
    /** Both perspectives' IRs, partitioned for the prepared rate and length. */
    struct Bank
    {
        std::vector<Channel> channels;  // [perspective][region][left, right]
    };

    Soundboard() = default;
    ~Soundboard();
    Soundboard (const Soundboard&) = delete;
    Soundboard& operator= (const Soundboard&) = delete;

    /** Builds the convolution for irs (at their own sample rate) and silences everything.
        Keeps the perspective and drops any offered bank; allocates. */
    void prepare (const SoundboardIrs& irs);

    /** A bank for new IRs, which must be at the prepared rate; longer ones are cut to the
        prepared length. Not realtime; safe while process() runs. */
    [[nodiscard]] std::unique_ptr<Bank> makeBank (const SoundboardIrs& irs) const;

    /** Offers a bank for the audio thread to fade to, replacing (and freeing) one it hasn't
        taken yet. Not realtime; one thread at a time. */
    void offerBank (std::unique_ptr<Bank> bank);

    /** Takes back (and frees) an offered bank the audio thread hasn't taken. Returns whether
        there was one. Same thread rules as offerBank(). */
    bool withdrawBank() noexcept;

    /** Frees the bank the audio thread last let go of, if any. Same thread rules as offerBank(). */
    void collectRetiredBank() noexcept;

    /** True while an offered bank hasn't been taken. Safe from any thread. */
    [[nodiscard]] bool hasIncomingBank() const noexcept { return incomingBank.load (std::memory_order_acquire) != nullptr; }

    /** Silences the soundboard and restarts its partition grid. A pending perspective switch,
        and an offered bank, complete at once, unfaded. */
    void reset() noexcept;

    /** Stops the soundboard ringing at once (CC120), keeping its grid and any crossfade. */
    void silence() noexcept;

    /** Switches to perspective (an index below numPerspectives) at the next partition boundary,
        with a crossfade. */
    void setPerspective (int perspective) noexcept;

    /** 0–100, 50 = no tilt (SPEC §6.3). Not smoothed. */
    void setBrightness (double brightness) noexcept;

    [[nodiscard]] double getBrightness() const noexcept { return brightness; }

    /** How many samples process() can take before it must stop at a partition boundary. */
    [[nodiscard]] int samplesToPartitionEnd() const noexcept { return partitionSize - position; }

    /** Overwrites left and right with numSamples of output for the regions' input (one buffer
        of numSamples per region). numSamples must not exceed samplesToPartitionEnd(). */
    void process (const std::array<const float*, numBridgeRegions>& input, float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] int getActivePerspective() const noexcept { return activeSet % numPerspectives; }
    [[nodiscard]] bool isCrossfading() const noexcept { return fadingFrom >= 0; }
    [[nodiscard]] int getNumPartitions() const noexcept { return numPartitions; }

    /** The IRs' length in samples at the prepared rate. */
    [[nodiscard]] int getIrLength() const noexcept { return irLength; }

    /** The hot loops' instruction set (Kernels.h): the CPU's best until set. Not while processing. */
    void setKernels (const Kernels& k) noexcept { kernels = &k; }

private:
    struct Region
    {
        std::array<float, fftSize> input {};  // the previous block, then this one
        std::vector<float> spectra;           // numPartitions packed spectra, a ring
        int silentBlocks = 0;                 // complete blocks of zeros in a row
        bool asleep = true;                   // its delay line holds only zeros
    };

    struct Tilt
    {
        bool active = false;
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    };

    /** A set is bank × numPerspectives + perspective: banks[set / numPerspectives]. */
    [[nodiscard]] Channel& channel (int set, int region, int side) noexcept
    {
        auto& bank = *banks[static_cast<std::size_t> (set / numPerspectives)];
        return bank.channels[static_cast<std::size_t> (((set % numPerspectives) * numBridgeRegions + region) * 2 + side)];
    }

    /** At a partition boundary: takes the offered bank into the slot not playing, retiring the
        bank that was there. False if there's none, or the last retired one isn't freed yet. */
    bool takeIncomingBank() noexcept;

    void endBlock() noexcept;
    void computeTails (int set) noexcept;
    void renderRegion (int set, int region, int start, int numSamples, float* outLeft, float* outRight) noexcept;

    const Kernels* kernels = &bestKernels();
    int numPartitions = 0;
    int irLength = 0;
    int newestSpectrum = 0;  // ring index of the latest block's spectrum
    int position = 0;        // samples into the current partition block

    std::array<std::unique_ptr<Bank>, 2> banks;  // the one playing, and the one faded from or to
    std::atomic<Bank*> incomingBank { nullptr }, retiredBank { nullptr };
    std::array<Region, numBridgeRegions> regions;
    RealFft<float> fft;
    std::array<float, fftSize> fftBuffer {};
    std::array<float, 2 * fftSize> accumulator {};  // left, then right, packed spectra

    // Perspective and bank
    int activeSet = microphonesPerspective;
    int requestedPerspective = microphonesPerspective;
    int fadingFrom = -1;      // the set being faded out, or −1
    int fadePosition = 0;     // samples into the fade
    std::vector<float> fadeIn;  // the incoming set's gain per fade sample

    // Output stage
    double sampleRate = irFileSampleRate;
    double brightness = 50.0;
    Tilt tilt;
    std::array<std::array<float, 2>, numBridgeRegions> tiltState {};  // [region][side]: previous input
    std::array<std::array<float, 2>, numBridgeRegions> tiltOutput {}; // [region][side]: previous output

    std::array<float, partitionSize> regionLeft {}, regionRight {};
    std::array<float, partitionSize> mixLeft {}, mixRight {};
};
} // namespace forte::detail
