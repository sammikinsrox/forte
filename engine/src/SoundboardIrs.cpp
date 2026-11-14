#include "SoundboardIrs.h"

#include "EmbeddedFiles.h"
#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>

namespace forte::detail
{
namespace
{
std::uint32_t readU32 (const unsigned char* p) noexcept
{
    return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8)
         | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24);
}

std::uint16_t readU16 (const unsigned char* p) noexcept
{
    return static_cast<std::uint16_t> (p[0] | (p[1] << 8));
}

constexpr const char* perspectiveNames[numPerspectives] { "microphones", "pianist" };
constexpr const char* regionNames[numBridgeRegions] { "bass", "tenor", "middle", "treble" };

/** Zeroth-order modified Bessel function of the first kind, for the Kaiser window. */
double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64 && term > 1e-17 * sum; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/** A minimum-phase lowpass interpolation kernel h(τ), τ in input samples, nonzero on [0, 2W).
    Designed as a Kaiser-windowed sinc on a grid of `oversampling` points per input sample, then
    made minimum phase by folding its real cepstrum. Looked up with cubic interpolation. */
class MinimumPhaseKernel
{
public:
    static constexpr int oversampling = 64;

    MinimumPhaseKernel (double fromRate, double toRate)
    {
        const double lower = std::min (fromRate, toRate);
        const double passEdge = std::min (20000.0, 0.4 * lower);
        const double cutoff = 0.5 * lower;
        const double transition = 2.0 * (cutoff - passEdge) / fromRate;  // full width, cycles per input sample
        constexpr double stopbandDb = 100.0;
        const double beta = 0.1102 * (stopbandDb - 8.7);
        // Kaiser's length estimate, with a margin: at the estimate itself, what aliases from just
        // past the stopband edge still moved the top of the band by ~0.07 dB.
        const double taps = 1.5 * (stopbandDb - 7.95) / (14.36 * transition);
        halfWidth = std::max (8, static_cast<int> (std::ceil (0.5 * taps)));

        const int n = 2 * halfWidth * oversampling + 1;
        const double fc = cutoff / fromRate;

        std::size_t fftSize = 1;
        while (fftSize < 8 * static_cast<std::size_t> (n))
            fftSize <<= 1;

        std::vector<double> re (fftSize, 0.0), im (fftSize, 0.0);
        const double i0Beta = besselI0 (beta);

        for (int i = 0; i < n; ++i)
        {
            const double tau = static_cast<double> (i - halfWidth * oversampling) / oversampling;
            const double x = 2.0 * fc * tau;
            const double sinc = x == 0.0 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double r = tau / halfWidth;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
            re[static_cast<std::size_t> (i)] = 2.0 * fc * sinc * window;
        }

        // Real cepstrum of the log magnitude, folded onto positive quefrencies, exponentiated back.
        ComplexFft<double> fft;
        fft.prepare (fftSize);
        fft.forward (re.data(), im.data());

        double peak = 0.0;
        for (std::size_t k = 0; k < fftSize; ++k)
        {
            re[k] = std::hypot (re[k], im[k]);
            peak = std::max (peak, re[k]);
        }

        const double floor = 1e-9 * peak;
        for (std::size_t k = 0; k < fftSize; ++k)
        {
            re[k] = std::log (std::max (re[k], floor));
            im[k] = 0.0;
        }

        fft.inverse (re.data(), im.data());

        const double scale = 1.0 / static_cast<double> (fftSize);
        for (std::size_t k = 0; k < fftSize; ++k)
        {
            const double fold = k == 0 || k == fftSize / 2 ? 1.0 : k < fftSize / 2 ? 2.0 : 0.0;
            re[k] *= scale * fold;
            im[k] = 0.0;
        }

        fft.forward (re.data(), im.data());

        for (std::size_t k = 0; k < fftSize; ++k)
        {
            const double magnitude = std::exp (re[k]);
            const double phase = im[k];
            re[k] = magnitude * std::cos (phase);
            im[k] = magnitude * std::sin (phase);
        }

        fft.inverse (re.data(), im.data());

        // One zero of padding either side for the cubic lookup.
        table.assign (static_cast<std::size_t> (n) + 3, 0.0);
        for (int i = 0; i < n; ++i)
            table[static_cast<std::size_t> (i) + 1] = re[static_cast<std::size_t> (i)] * scale;
    }

    [[nodiscard]] int span() const noexcept { return 2 * halfWidth; }

    /** h(τ) for 0 ≤ τ < span(), by Catmull-Rom interpolation of the table. */
    [[nodiscard]] double at (double tau) const noexcept
    {
        const double x = tau * oversampling;
        const auto i = static_cast<std::size_t> (x);
        const double f = x - static_cast<double> (i);
        const double p0 = table[i], p1 = table[i + 1], p2 = table[i + 2];
        const double p3 = i + 3 < table.size() ? table[i + 3] : 0.0;
        return p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    }

private:
    int halfWidth = 8;
    std::vector<double> table;  // h at τ = (i − 1) / oversampling
};

StereoIr unitImpulse()
{
    return { { 1.0f }, { 1.0f } };
}
} // namespace

std::size_t SoundboardIrs::length() const noexcept
{
    std::size_t longest = 0;
    for (const auto& set : sets)
        for (const auto& ir : set)
            longest = std::max ({ longest, ir.left.size(), ir.right.size() });

    return longest;
}

std::optional<StereoIr> parseIrWav (std::span<const unsigned char> bytes)
{
    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0 || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;

    bool formatOk = false;
    const unsigned char* samples = nullptr;
    std::size_t sampleBytes = 0;

    for (std::size_t at = 12; at + 8 <= bytes.size();)
    {
        const unsigned char* chunk = bytes.data() + at;
        const std::size_t chunkSize = readU32 (chunk + 4);
        const unsigned char* body = chunk + 8;

        if (chunkSize > bytes.size() - at - 8)
            return std::nullopt;

        if (std::memcmp (chunk, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
                return std::nullopt;

            auto tag = readU16 (body);
            if (tag == 0xfffe && chunkSize >= 40)  // WAVE_FORMAT_EXTENSIBLE: the subformat GUID starts with the tag
                tag = readU16 (body + 24);

            formatOk = tag == 3 && readU16 (body + 2) == 2 && readU32 (body + 4) == static_cast<std::uint32_t> (irFileSampleRate)
                    && readU16 (body + 14) == 32;
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            samples = body;
            sampleBytes = chunkSize;
        }

        at += 8 + chunkSize + (chunkSize & 1);
    }

    if (! formatOk || samples == nullptr || sampleBytes == 0 || sampleBytes % 8 != 0)
        return std::nullopt;

    const auto frames = sampleBytes / 8;
    StereoIr ir;
    ir.left.resize (frames);
    ir.right.resize (frames);

    for (std::size_t i = 0; i < frames; ++i)
    {
        std::memcpy (&ir.left[i], samples + 8 * i, 4);
        std::memcpy (&ir.right[i], samples + 8 * i + 4, 4);
    }

    return ir;
}

SoundboardIrs loadEmbeddedIrs()
{
    SoundboardIrs irs;
    irs.sampleRate = irFileSampleRate;

    for (int p = 0; p < numPerspectives; ++p)
    {
        for (int r = 0; r < numBridgeRegions; ++r)
        {
            const auto name = std::string (perspectiveNames[p]) + "-" + regionNames[r] + ".wav";
            std::optional<StereoIr> ir;

            if (const auto* file = findEmbeddedFile (name))
                ir = parseIrWav ({ file->data, file->size });

            irs.sets[static_cast<std::size_t> (p)][static_cast<std::size_t> (r)] = ir ? std::move (*ir) : unitImpulse();
        }
    }

    return irs;
}

std::vector<std::vector<float>> resampleSignals (std::span<const std::vector<float>* const> inputs,
                                                 double fromRate, double toRate, std::size_t outLength)
{
    const MinimumPhaseKernel kernel (fromRate, toRate);
    const int span = kernel.span();
    const double step = fromRate / toRate;  // input samples per output sample
    const double gain = step;

    std::vector<std::vector<float>> outputs (inputs.size(), std::vector<float> (outLength, 0.0f));
    std::vector<double> taps (static_cast<std::size_t> (span) + 1);

    for (std::size_t m = 0; m < outLength; ++m)
    {
        // out[m] = gain · Σ_k in[k]·h(u − k), over the k with 0 ≤ u − k < span.
        const double u = static_cast<double> (m) * step;
        const auto last = static_cast<std::int64_t> (std::floor (u));
        const auto first = std::max<std::int64_t> (0, static_cast<std::int64_t> (std::floor (u - span)) + 1);

        if (first > last)
            continue;

        const auto count = static_cast<std::size_t> (last - first + 1);
        for (std::size_t j = 0; j < count; ++j)
            taps[j] = kernel.at (u - static_cast<double> (first + static_cast<std::int64_t> (j)));

        for (std::size_t c = 0; c < inputs.size(); ++c)
        {
            const auto& in = *inputs[c];
            double sum = 0.0;
            for (std::size_t j = 0; j < count; ++j)
            {
                const auto k = static_cast<std::size_t> (first) + j;
                if (k < in.size())
                    sum += taps[j] * in[k];
            }
            outputs[c][m] = static_cast<float> (gain * sum);
        }
    }

    return outputs;
}

SoundboardIrs resampleIrs (const SoundboardIrs& irs, double newRate)
{
    if (newRate == irs.sampleRate)
        return irs;

    const auto inLength = irs.length();
    const auto outLength = static_cast<std::size_t> (std::ceil (static_cast<double> (inLength) * newRate / irs.sampleRate - 1e-9));

    std::vector<const std::vector<float>*> inputs;
    for (const auto& set : irs.sets)
        for (const auto& ir : set)
        {
            inputs.push_back (&ir.left);
            inputs.push_back (&ir.right);
        }

    auto outputs = resampleSignals (inputs, irs.sampleRate, newRate, outLength);

    SoundboardIrs result;
    result.sampleRate = newRate;
    std::size_t next = 0;

    for (auto& set : result.sets)
        for (auto& ir : set)
        {
            ir.left = std::move (outputs[next++]);
            ir.right = std::move (outputs[next++]);
        }

    return result;
}
} // namespace forte::detail
