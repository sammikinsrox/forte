#include "Soundboard.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace forte::detail
{
namespace
{
constexpr int B = Soundboard::partitionSize;

/** The tilt's shelf: centred on this frequency, so its gain at 10 kHz is most of the shelf's. */
constexpr double tiltCentreHz = 3000.0;
constexpr double tiltReferenceHz = 10000.0;
constexpr double tiltDbAtReference = 12.0;  // at Brightness 0 and 100

float dot (const float* __restrict a, const float* __restrict b) noexcept
{
    float sum = 0.0f;
    for (int k = 0; k < B; ++k)
        sum += a[k] * b[k];
    return sum;
}

bool allZero (const float* x, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        if (x[i] != 0.0f)
            return false;
    return true;
}
} // namespace

void Soundboard::prepare (const SoundboardIrs& irs)
{
    sampleRate = irs.sampleRate;
    irLength = static_cast<int> (irs.length());
    numPartitions = std::max (0, (irLength - B + B - 1) / B);
    fft.prepare (fftSize);

    channels.assign (static_cast<std::size_t> (numPerspectives * numBridgeRegions * 2), Channel {});
    const float scale = 1.0f / static_cast<float> (fftSize);  // RealFft's inverse returns fftSize·x

    for (int set = 0; set < numPerspectives; ++set)
    {
        for (int region = 0; region < numBridgeRegions; ++region)
        {
            const auto& ir = irs.sets[static_cast<std::size_t> (set)][static_cast<std::size_t> (region)];

            for (int side = 0; side < 2; ++side)
            {
                const auto& taps = side == 0 ? ir.left : ir.right;
                const auto tap = [&] (int i) { return i < static_cast<int> (taps.size()) ? taps[static_cast<std::size_t> (i)] : 0.0f; };
                auto& ch = channel (set, region, side);

                for (int k = 0; k < B; ++k)
                    ch.headReversed[static_cast<std::size_t> (k)] = tap (B - 1 - k);

                ch.partitions.assign (static_cast<std::size_t> (numPartitions * fftSize), 0.0f);

                for (int p = 0; p < numPartitions; ++p)
                {
                    fftBuffer.fill (0.0f);
                    for (int k = 0; k < B; ++k)
                        fftBuffer[static_cast<std::size_t> (k)] = tap ((p + 1) * B + k) * scale;

                    float* spectrum = ch.partitions.data() + p * fftSize;
                    fft.forward (fftBuffer.data(), spectrum, spectrum + B);
                }
            }
        }
    }

    for (auto& region : regions)
        region.spectra.assign (static_cast<std::size_t> (numPartitions * fftSize), 0.0f);

    const int fadeBlocks = std::max (1, static_cast<int> (std::lround (crossfadeSeconds * sampleRate / B)));
    const int fadeLength = fadeBlocks * B;
    fadeIn.resize (static_cast<std::size_t> (fadeLength));
    for (int i = 0; i < fadeLength; ++i)
        fadeIn[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * i / fadeLength));

    setBrightness (brightness);
    reset();
}

void Soundboard::reset() noexcept
{
    position = 0;
    newestSpectrum = 0;
    silence();

    activeSet = requestedSet;
    fadingFrom = -1;
    fadePosition = 0;
}

void Soundboard::silence() noexcept
{
    for (auto& region : regions)
    {
        region.input.fill (0.0f);
        std::fill (region.spectra.begin(), region.spectra.end(), 0.0f);
        region.silentBlocks = numPartitions + 1;
        region.asleep = true;
    }

    for (auto& ch : channels)
        ch.tail.fill (0.0f);

    for (auto& state : tiltState)
        state.fill (0.0f);
    for (auto& state : tiltOutput)
        state.fill (0.0f);
}

void Soundboard::setPerspective (int perspective) noexcept
{
    requestedSet = std::clamp (perspective, 0, numPerspectives - 1);
}

void Soundboard::setBrightness (double newBrightness) noexcept
{
    brightness = std::clamp (newBrightness, 0.0, 100.0);
    const double targetDb = tiltDbAtReference * (brightness - 50.0) / 50.0;

    if (targetDb == 0.0)
    {
        tilt = {};
        return;
    }

    // A symmetric first-order shelf H(s) = (√G·s/ωc + 1) / (s/(√G·ωc) + 1): unity at DC, G at
    // the top, mapped with the bilinear transform prewarped at fc. |H|² at x = (Ω/Ωc)² is
    // G(Gx + 1)/(x + G); solve for the G that puts targetDb exactly at the reference frequency,
    // using its warped Ω = tan(πf/fs).
    const double k = std::tan (std::numbers::pi * tiltCentreHz / sampleRate);
    const double t2 = std::pow (10.0, targetDb / 10.0);
    const double ratio = std::tan (std::numbers::pi * std::min (tiltReferenceHz, 0.45 * sampleRate) / sampleRate) / k;
    const double x = ratio * ratio;
    const double gain = (-(1.0 - t2) + std::sqrt ((1.0 - t2) * (1.0 - t2) + 4.0 * x * x * t2)) / (2.0 * x);
    const double rootGain = std::sqrt (gain);
    const double d0 = 1.0 / rootGain + k;

    tilt.active = true;
    tilt.b0 = static_cast<float> ((rootGain + k) / d0);
    tilt.b1 = static_cast<float> ((k - rootGain) / d0);
    tilt.a1 = static_cast<float> ((k - 1.0 / rootGain) / d0);
}

void Soundboard::setWidth (double width) noexcept
{
    const double w = std::clamp (width, 0.0, 100.0) / 100.0;
    widthDirect = static_cast<float> (0.5 * (1.0 + w));
    widthCross = static_cast<float> (0.5 * (1.0 - w));
}

void Soundboard::setVolumeDb (double decibels) noexcept
{
    volume = static_cast<float> (std::pow (10.0, decibels / 20.0));
}

void Soundboard::process (const std::array<const float*, numBridgeRegions>& input, float* left, float* right, int numSamples) noexcept
{
    assert (numSamples >= 0 && numSamples <= samplesToPartitionEnd());

    if (numSamples <= 0)
        return;

    const auto n = static_cast<std::size_t> (numSamples);
    std::fill_n (mixLeft.begin(), n, 0.0f);
    std::fill_n (mixRight.begin(), n, 0.0f);

    for (int r = 0; r < numBridgeRegions; ++r)
    {
        auto& region = regions[static_cast<std::size_t> (r)];
        const float* in = input[static_cast<std::size_t> (r)];
        std::copy_n (in, n, region.input.begin() + B + position);

        if (region.asleep)
        {
            if (allZero (in, numSamples))
                continue;  // everything this region would add is exactly zero

            region.asleep = false;
        }

        if (fadingFrom < 0)
        {
            renderRegion (activeSet, r, position, numSamples, regionLeft.data(), regionRight.data());
        }
        else
        {
            // Both sets run on the same input; the fade mixes them before the tilt.
            std::array<float, B> newLeft, newRight;
            renderRegion (fadingFrom, r, position, numSamples, regionLeft.data(), regionRight.data());
            renderRegion (activeSet, r, position, numSamples, newLeft.data(), newRight.data());

            for (std::size_t i = 0; i < n; ++i)
            {
                const float g = fadeIn[static_cast<std::size_t> (fadePosition) + i];
                regionLeft[i] = (1.0f - g) * regionLeft[i] + g * newLeft[i];
                regionRight[i] = (1.0f - g) * regionRight[i] + g * newRight[i];
            }
        }

        if (tilt.active)
        {
            auto& x1 = tiltState[static_cast<std::size_t> (r)];
            auto& y1 = tiltOutput[static_cast<std::size_t> (r)];
            float* sides[2] { regionLeft.data(), regionRight.data() };

            for (std::size_t side = 0; side < 2; ++side)
            {
                float xPrev = x1[side], yPrev = y1[side];
                for (std::size_t i = 0; i < n; ++i)
                {
                    const float x = sides[side][i];
                    const float y = tilt.b0 * x + tilt.b1 * xPrev - tilt.a1 * yPrev;
                    sides[side][i] = y;
                    xPrev = x;
                    yPrev = y;
                }
                x1[side] = xPrev;
                y1[side] = yPrev;
            }
        }

        for (std::size_t i = 0; i < n; ++i)
        {
            mixLeft[i] += regionLeft[i];
            mixRight[i] += regionRight[i];
        }
    }

    // Width (mid/side, written so that full width passes the channels through exactly), then Volume.
    for (std::size_t i = 0; i < n; ++i)
    {
        const float l = mixLeft[i], r = mixRight[i];
        left[i] = volume * (widthDirect * l + widthCross * r);
        right[i] = volume * (widthCross * l + widthDirect * r);
    }

    position += numSamples;
    if (fadingFrom >= 0)
        fadePosition += numSamples;

    if (position == B)
    {
        endBlock();
        position = 0;
    }
}

void Soundboard::renderRegion (int set, int region, int start, int numSamples, float* outLeft, float* outRight) noexcept
{
    const auto& l = channel (set, region, 0);
    const auto& r = channel (set, region, 1);
    const float* history = regions[static_cast<std::size_t> (region)].input.data();

    for (int i = 0; i < numSamples; ++i)
    {
        // The head: taps 0..B−1 against this sample and the B−1 before it.
        const int at = start + i;
        const float* window = history + at + 1;
        outLeft[i] = l.tail[static_cast<std::size_t> (at)] + dot (l.headReversed.data(), window);
        outRight[i] = r.tail[static_cast<std::size_t> (at)] + dot (r.headReversed.data(), window);
    }
}

void Soundboard::endBlock() noexcept
{
    // Perspective: a fade ends on a block boundary; a new request starts or reverses one here.
    const auto fadeLength = static_cast<int> (fadeIn.size());

    if (fadingFrom >= 0 && fadePosition >= fadeLength)
        fadingFrom = -1;

    if (requestedSet != activeSet)
    {
        if (fadingFrom == requestedSet)
        {
            std::swap (activeSet, fadingFrom);
            fadePosition = fadeLength - fadePosition;
        }
        else if (fadingFrom < 0)
        {
            fadingFrom = activeSet;
            activeSet = requestedSet;
            fadePosition = 0;
        }
    }

    if (numPartitions > 0)
        newestSpectrum = (newestSpectrum + 1) % numPartitions;

    for (int r = 0; r < numBridgeRegions; ++r)
    {
        auto& region = regions[static_cast<std::size_t> (r)];
        const bool blockSilent = allZero (region.input.data() + B, B);
        region.silentBlocks = blockSilent ? std::min (region.silentBlocks + 1, numPartitions + 1) : 0;

        if (region.asleep)
            continue;  // its input, and every spectrum in its delay line, is still zero

        if (numPartitions > 0)
        {
            float* spectrum = region.spectra.data() + newestSpectrum * fftSize;
            fft.forward (region.input.data(), spectrum, spectrum + B);
        }

        // After numPartitions + 1 silent blocks every frame in the delay line is zero.
        if (region.silentBlocks > numPartitions)
        {
            region.asleep = true;
            tiltState[static_cast<std::size_t> (r)].fill (0.0f);
            tiltOutput[static_cast<std::size_t> (r)].fill (0.0f);
        }

        std::copy_n (region.input.begin() + B, B, region.input.begin());
    }

    computeTails (activeSet);
    if (fadingFrom >= 0)
        computeTails (fadingFrom);
}

void Soundboard::computeTails (int set) noexcept
{
    for (int r = 0; r < numBridgeRegions; ++r)
    {
        const auto& region = regions[static_cast<std::size_t> (r)];
        auto& l = channel (set, r, 0);
        auto& rt = channel (set, r, 1);

        if (region.asleep || numPartitions == 0)
        {
            l.tail.fill (0.0f);
            rt.tail.fill (0.0f);
            continue;
        }

        float* __restrict lRe = accumulator.data();
        float* __restrict lIm = lRe + B;
        float* __restrict rRe = lIm + B;
        float* __restrict rIm = rRe + B;
        std::fill (accumulator.begin(), accumulator.end(), 0.0f);

        for (int p = 0; p < numPartitions; ++p)
        {
            // Partition p + 1 meets the input from p blocks ago.
            const int slot = (newestSpectrum - p + numPartitions) % numPartitions;
            const float* __restrict xr = region.spectra.data() + slot * fftSize;
            const float* __restrict xi = xr + B;
            const float* __restrict hlr = l.partitions.data() + p * fftSize;
            const float* __restrict hli = hlr + B;
            const float* __restrict hrr = rt.partitions.data() + p * fftSize;
            const float* __restrict hri = hrr + B;

            // Bin 0 packs the real DC and Nyquist bins.
            lRe[0] += xr[0] * hlr[0];
            lIm[0] += xi[0] * hli[0];
            rRe[0] += xr[0] * hrr[0];
            rIm[0] += xi[0] * hri[0];

            for (int k = 1; k < B; ++k)
            {
                lRe[k] += xr[k] * hlr[k] - xi[k] * hli[k];
                lIm[k] += xr[k] * hli[k] + xi[k] * hlr[k];
                rRe[k] += xr[k] * hrr[k] - xi[k] * hri[k];
                rIm[k] += xr[k] * hri[k] + xi[k] * hrr[k];
            }
        }

        // Overlap-save: the second half of the circular result is the linear convolution.
        fft.inverse (lRe, lIm, fftBuffer.data());
        std::copy_n (fftBuffer.begin() + B, B, l.tail.begin());
        fft.inverse (rRe, rIm, fftBuffer.data());
        std::copy_n (fftBuffer.begin() + B, B, rt.tail.begin());
    }
}
} // namespace forte::detail
