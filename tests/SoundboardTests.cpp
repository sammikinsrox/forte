// The soundboard and output stage (SPEC §2.8), its IRs (§3.5) and the FFT under it.

#include "support/AudioThreadGuard.h"

#include "Fft.h"
#include "Soundboard.h"
#include "SoundboardIrs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace forte::detail;
using forte::test::heapUseOnAudioThread;

namespace
{
constexpr int B = Soundboard::partitionSize;
using Signals = std::array<std::vector<float>, numBridgeRegions>;

struct Stereo
{
    std::vector<float> left, right;
};

/** Random decaying IRs, different for every perspective, region and side. */
SoundboardIrs randomIrs (std::size_t length, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise;
    SoundboardIrs irs;

    for (auto& set : irs.sets)
        for (auto& ir : set)
            for (auto* taps : { &ir.left, &ir.right })
            {
                taps->resize (length);
                for (std::size_t i = 0; i < length; ++i)
                    (*taps)[i] = noise (rng) * std::exp (-3.0f * static_cast<float> (i) / static_cast<float> (length));
            }

    return irs;
}

/** Region inputs with bursts and gaps, so regions fall asleep and wake again. */
Signals burstyInput (std::size_t length, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise;
    Signals in;

    for (int r = 0; r < numBridgeRegions; ++r)
    {
        auto& x = in[static_cast<std::size_t> (r)];
        x.assign (length, 0.0f);

        for (std::size_t i = 0; i < length; ++i)
        {
            const bool on = r == 0 ? i < 700 || (i > 4000 && i < 4300)
                          : r == 1 ? false
                          : r == 2 ? (i / 900) % 2 == 1
                                   : i == 333 || i == 3001;
            if (on)
                x[i] = r == 3 ? 1.0f : noise (rng);
        }
    }

    return in;
}

/** Direct convolution in double precision: one perspective, every region, summed. */
Stereo convolveDirectly (const SoundboardIrs& irs, int set, const Signals& in)
{
    const auto length = in[0].size();
    std::vector<double> left (length, 0.0), right (length, 0.0);

    for (int r = 0; r < numBridgeRegions; ++r)
    {
        const auto& ir = irs.sets[static_cast<std::size_t> (set)][static_cast<std::size_t> (r)];
        const auto& x = in[static_cast<std::size_t> (r)];

        for (std::size_t t = 0; t < length; ++t)
        {
            if (x[t] == 0.0f)
                continue;
            for (std::size_t k = 0; k < ir.left.size() && t + k < length; ++k)
            {
                left[t + k] += static_cast<double> (ir.left[k]) * x[t];
                right[t + k] += static_cast<double> (ir.right[k]) * x[t];
            }
        }
    }

    Stereo out { std::vector<float> (length), std::vector<float> (length) };
    for (std::size_t t = 0; t < length; ++t)
    {
        out.left[t] = static_cast<float> (left[t]);
        out.right[t] = static_cast<float> (right[t]);
    }
    return out;
}

/** Runs the soundboard over the inputs in stretches that cycle through `sizes`, cut at the
    partition boundaries. beforeSample is called before each stretch with its start. */
template <typename BeforeSample>
Stereo run (Soundboard& soundboard, const Signals& in, std::vector<int> sizes, BeforeSample&& beforeSample)
{
    const auto length = in[0].size();
    Stereo out { std::vector<float> (length), std::vector<float> (length) };
    std::size_t next = 0;

    for (std::size_t at = 0; at < length;)
    {
        beforeSample (at);
        const auto want = static_cast<std::size_t> (sizes[next++ % sizes.size()]);
        const auto n = std::min ({ want, length - at, static_cast<std::size_t> (soundboard.samplesToPartitionEnd()) });
        const std::array<const float*, numBridgeRegions> buses { in[0].data() + at, in[1].data() + at, in[2].data() + at, in[3].data() + at };
        soundboard.process (buses, out.left.data() + at, out.right.data() + at, static_cast<int> (n));
        at += n;
    }

    return out;
}

Stereo run (Soundboard& soundboard, const Signals& in, std::vector<int> sizes = { B })
{
    return run (soundboard, in, std::move (sizes), [] (std::size_t) {});
}

double largestError (const std::vector<float>& actual, const std::vector<float>& expected)
{
    double error = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        error = std::max (error, std::abs (static_cast<double> (actual[i]) - expected[i]));
    return error;
}

float peakOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max (p, std::abs (v));
    return p;
}

bool allZero (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    return std::all_of (x.begin() + static_cast<std::ptrdiff_t> (from), x.begin() + static_cast<std::ptrdiff_t> (to), [] (float v) { return v == 0.0f; });
}

/** The crossfade's length at 48 kHz: ~50 ms in whole partitions. */
constexpr int fadeLength48k = 19 * B;

double fadeIn (int i)
{
    return 0.5 - 0.5 * std::cos (std::numbers::pi * i / fadeLength48k);
}
} // namespace

TEST_CASE ("The real FFT matches the DFT, and its inverse returns N times the input", "[soundboard][fft]")
{
    const auto size = GENERATE (4u, 16u, 256u);

    std::mt19937 rng (size);
    std::uniform_real_distribution<double> uniform (-1.0, 1.0);
    std::vector<double> x (size);
    for (auto& v : x)
        v = uniform (rng);

    RealFft<double> fft;
    fft.prepare (size);
    std::vector<double> re (size / 2), im (size / 2), back (size);
    fft.forward (x.data(), re.data(), im.data());

    for (std::size_t k = 0; k <= size / 2; ++k)
    {
        std::complex<double> dft;
        for (std::size_t n = 0; n < size; ++n)
            dft += x[n] * std::polar (1.0, -2.0 * std::numbers::pi * static_cast<double> (k * n) / size);

        const auto actual = k == 0 ? std::complex<double> (re[0], 0.0)
                          : k == size / 2 ? std::complex<double> (im[0], 0.0)
                                          : std::complex<double> (re[k], im[k]);
        INFO ("size " << size << ", bin " << k);
        CHECK (std::abs (actual - dft) < 1e-12 * size);
    }

    fft.inverse (re.data(), im.data(), back.data());
    for (std::size_t n = 0; n < size; ++n)
        CHECK (std::abs (back[n] - static_cast<double> (size) * x[n]) < 1e-12 * size);
}

TEST_CASE ("The soundboard convolution matches direct convolution, with no latency", "[soundboard]")
{
    // Lengths around the partition size: head only, head plus one partition, several.
    const auto irLength = GENERATE (1u, 50u, 128u, 129u, 1000u, 2049u);
    const auto irs = randomIrs (irLength, irLength);
    const auto in = burstyInput (6000, 7);

    for (int set : { microphonesPerspective, pianistPerspective })
    {
        Soundboard soundboard;
        soundboard.setPerspective (set);
        soundboard.prepare (irs);
        REQUIRE (soundboard.getActivePerspective() == set);

        const auto out = run (soundboard, in);
        const auto expected = convolveDirectly (irs, set, in);
        const double tolerance = 2e-6 * std::max (peakOf (expected.left), peakOf (expected.right));

        INFO ("IR length " << irLength << ", perspective " << set);
        CHECK (largestError (out.left, expected.left) < tolerance);
        CHECK (largestError (out.right, expected.right) < tolerance);
    }
}

TEST_CASE ("The soundboard's output is bit-identical however the samples are split", "[soundboard][determinism]")
{
    const auto irs = randomIrs (1000, 3);
    const auto in = burstyInput (6000, 11);

    Soundboard soundboard;
    soundboard.prepare (irs);
    const auto reference = run (soundboard, in);

    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 32 }, { 3, 50, 1, 90, 128, 17 } })
    {
        soundboard.reset();
        const auto out = run (soundboard, in, sizes);
        CHECK (out.left == reference.left);
        CHECK (out.right == reference.right);
    }
}

TEST_CASE ("A silent soundboard renders exact zeros, and a region rings for its IR's length", "[soundboard]")
{
    const std::size_t irLength = 1000;
    const auto irs = randomIrs (irLength, 5);

    Soundboard soundboard;
    soundboard.prepare (irs);

    Signals in;
    for (auto& x : in)
        x.assign (8000, 0.0f);

    const auto silent = run (soundboard, in);
    CHECK (allZero (silent.left, 0, 8000));
    CHECK (allZero (silent.right, 0, 8000));

    // One impulse in the treble: it rings for the IR, rounding residue lasts at most two
    // partitions more, then exact zeros, until the region wakes again.
    in[3][1000] = 1.0f;
    in[3][6000] = 1.0f;
    soundboard.reset();
    const auto out = run (soundboard, in, { 37 });

    CHECK (allZero (out.left, 0, 1000));
    CHECK (out.left[1000] == irs.sets[0][3].left[0]);
    CHECK (allZero (out.left, 1000 + irLength + 2 * B, 6000));
    CHECK (out.left[6000] == irs.sets[0][3].left[0]);

    // CC120: silence() stops the ringing at once, and the grid carries on.
    soundboard.reset();
    in[3][6000] = 0.0f;
    const auto silenced = run (soundboard, in, { 100 }, [&] (std::size_t at)
    {
        if (at == 1380)  // stretches start at 1280, 1380, 1408, ...
            soundboard.silence();
    });
    CHECK_FALSE (allZero (silenced.left, 1000, 1380));
    CHECK (allZero (silenced.left, 1380, 8000));
}

TEST_CASE ("Switching perspective crossfades from the next partition boundary, and can reverse mid-fade", "[soundboard]")
{
    const auto irs = randomIrs (1000, 9);
    Signals in;
    std::mt19937 rng (1);
    std::normal_distribution<float> noise;
    for (auto& x : in)
    {
        x.resize (12000);
        for (auto& v : x)
            v = noise (rng);
    }

    const auto microphones = convolveDirectly (irs, microphonesPerspective, in);
    const auto pianist = convolveDirectly (irs, pianistPerspective, in);
    const double tolerance = 1e-5 * peakOf (microphones.left);

    Soundboard soundboard;
    soundboard.prepare (irs);

    SECTION ("A full fade")
    {
        // Stretches start at 0, 100, 128, 228, 256, ...: requested at 996, mid-block, it starts at
        // the next boundary, 1024.
        const auto out = run (soundboard, in, { 100 }, [&] (std::size_t at)
        {
            if (at == 996)
                soundboard.setPerspective (pianistPerspective);
        });

        double error = 0.0;
        for (std::size_t t = 0; t < 12000; ++t)
        {
            const double g = t < 1024 ? 0.0 : t >= 1024 + fadeLength48k ? 1.0 : fadeIn (static_cast<int> (t) - 1024);
            error = std::max (error, std::abs (out.left[t] - ((1.0 - g) * microphones.left[t] + g * pianist.left[t])));
            error = std::max (error, std::abs (out.right[t] - ((1.0 - g) * microphones.right[t] + g * pianist.right[t])));
        }
        CHECK (error < tolerance);
        CHECK (soundboard.getActivePerspective() == pianistPerspective);
        CHECK_FALSE (soundboard.isCrossfading());
    }

    SECTION ("Reversed halfway: it fades back from where it got to")
    {
        // To Pianist from 1024; back to Microphones requested at 2000, taking effect at 2048,
        // 1024 samples into the fade. It returns over those 1024 samples.
        const auto out = run (soundboard, in, { B }, [&] (std::size_t at)
        {
            if (at == 896)
                soundboard.setPerspective (pianistPerspective);
            if (at == 1920)
                soundboard.setPerspective (microphonesPerspective);
        });

        double error = 0.0;
        for (std::size_t t = 0; t < 12000; ++t)
        {
            const int i = static_cast<int> (t);
            const double g = i < 1024 ? 0.0 : i < 2048 ? fadeIn (i - 1024) : i < 3072 ? 1.0 - fadeIn (fadeLength48k - 1024 + i - 2048) : 0.0;
            error = std::max (error, std::abs (out.left[t] - ((1.0 - g) * microphones.left[t] + g * pianist.left[t])));
        }
        CHECK (error < tolerance);
        CHECK (soundboard.getActivePerspective() == microphonesPerspective);
    }
}

TEST_CASE ("Width 0 is mono, and Volume scales the output", "[soundboard]")
{
    const auto irs = randomIrs (300, 2);
    const auto in = burstyInput (3000, 3);

    Soundboard soundboard;
    soundboard.prepare (irs);
    const auto full = run (soundboard, in);

    soundboard.setWidth (0.0);
    soundboard.reset();
    const auto mono = run (soundboard, in);
    CHECK (mono.left == mono.right);
    for (std::size_t i = 0; i < 3000; ++i)
        REQUIRE (std::abs (mono.left[i] - 0.5f * (full.left[i] + full.right[i])) <= 1e-6f * peakOf (full.left));

    soundboard.setWidth (100.0);
    soundboard.setVolumeDb (-20.0);
    soundboard.reset();
    const auto quiet = run (soundboard, in);
    for (std::size_t i = 0; i < 3000; ++i)
        REQUIRE (std::abs (quiet.left[i] - 0.1f * full.left[i]) <= 1e-6f * peakOf (full.left));
}

TEST_CASE ("Brightness tilts the treble by 12 dB at 10 kHz either way, and leaves the bass", "[soundboard]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);

    SoundboardIrs impulse;
    impulse.sampleRate = sampleRate;
    for (auto& set : impulse.sets)
        for (auto& ir : set)
            ir = { { 1.0f }, { 1.0f } };

    const auto gainDb = [&] (double brightness, double frequency)
    {
        Soundboard soundboard;
        soundboard.setBrightness (brightness);
        soundboard.prepare (impulse);

        Signals in;
        const std::size_t length = static_cast<std::size_t> (sampleRate / 2);
        for (auto& x : in)
            x.assign (length, 0.0f);
        for (std::size_t i = 0; i < length; ++i)
            in[1][i] = static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate));

        const auto out = run (soundboard, in);
        double inPower = 0.0, outPower = 0.0;
        for (std::size_t i = length / 2; i < length; ++i)
        {
            inPower += static_cast<double> (in[1][i]) * in[1][i];
            outPower += static_cast<double> (out.left[i]) * out.left[i];
        }
        return 10.0 * std::log10 (outPower / inPower);
    };

    INFO (sampleRate << " Hz");
    CHECK (std::abs (gainDb (50.0, 10000.0)) < 1e-4);
    CHECK (std::abs (gainDb (100.0, 10000.0) - 12.0) < 0.05);
    CHECK (std::abs (gainDb (0.0, 10000.0) + 12.0) < 0.05);
    CHECK (std::abs (gainDb (100.0, 100.0)) < 0.1);
    CHECK (std::abs (gainDb (0.0, 100.0)) < 0.1);
}

TEST_CASE ("The soundboard never touches the heap on the audio thread", "[soundboard][audio-thread]")
{
    const auto irs = randomIrs (2000, 4);
    const auto in = burstyInput (6000, 5);

    Soundboard soundboard;
    soundboard.prepare (irs);
    Stereo out { std::vector<float> (6000), std::vector<float> (6000) };

    const auto use = heapUseOnAudioThread ([&]
    {
        for (std::size_t at = 0; at < 6000;)
        {
            if (at % 1000 < 50)
                soundboard.setPerspective (static_cast<int> (at / 1000) % 2);

            const auto n = std::min<std::size_t> ({ 50, 6000 - at, static_cast<std::size_t> (soundboard.samplesToPartitionEnd()) });
            const std::array<const float*, numBridgeRegions> buses { in[0].data() + at, in[1].data() + at, in[2].data() + at, in[3].data() + at };
            soundboard.process (buses, out.left.data() + at, out.right.data() + at, static_cast<int> (n));
            at += n;
        }
        soundboard.silence();
        soundboard.reset();
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}

TEST_CASE ("The embedded IRs: both perspectives, four regions, 20000 taps at 48 kHz, bass on the left", "[soundboard][irs]")
{
    const auto irs = loadEmbeddedIrs();
    REQUIRE (irs.sampleRate == 48000.0);

    const auto energy = [] (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (float v : x)
            sum += static_cast<double> (v) * v;
        return sum;
    };

    for (int set = 0; set < numPerspectives; ++set)
    {
        for (int region = 0; region < numBridgeRegions; ++region)
        {
            const auto& ir = irs.sets[static_cast<std::size_t> (set)][static_cast<std::size_t> (region)];
            INFO ("perspective " << set << ", region " << region);
            REQUIRE (ir.left.size() == 20000);
            REQUIRE (ir.right.size() == 20000);
            CHECK (ir.left[0] != 0.0f);   // the direct path: onsets stay on their sample
            CHECK (ir.right[0] != 0.0f);
            CHECK (ir.left.back() == 0.0f);  // faded out, so the truncation doesn't click
            CHECK (ir.right.back() == 0.0f);
        }

        const auto& bass = irs.sets[static_cast<std::size_t> (set)][0];
        const auto& treble = irs.sets[static_cast<std::size_t> (set)][3];
        CHECK (energy (bass.left) > 2.0 * energy (bass.right));
        CHECK (energy (treble.right) > 2.0 * energy (treble.left));
    }

    CHECK (irs.sets[0][0].left != irs.sets[1][0].left);
}

TEST_CASE ("IR WAVs other than stereo 32-bit float at 48 kHz are rejected", "[soundboard][irs]")
{
    const auto wav = [] (std::uint16_t tag, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits)
    {
        std::vector<unsigned char> bytes;
        const auto put = [&] (const void* p, std::size_t n) { bytes.insert (bytes.end(), static_cast<const unsigned char*> (p), static_cast<const unsigned char*> (p) + n); };
        const auto u32 = [&] (std::uint32_t v) { put (&v, 4); };
        const auto u16 = [&] (std::uint16_t v) { put (&v, 2); };
        const float frames[4] { 0.5f, -0.25f, 0.125f, 1.0f };

        put ("RIFF", 4); u32 (4 + 8 + 16 + 8 + sizeof frames); put ("WAVE", 4);
        put ("fmt ", 4); u32 (16); u16 (tag); u16 (channels); u32 (rate);
        u32 (rate * channels * bits / 8); u16 (static_cast<std::uint16_t> (channels * bits / 8)); u16 (bits);
        put ("data", 4); u32 (sizeof frames); put (frames, sizeof frames);
        return bytes;
    };

    const auto good = parseIrWav (wav (3, 2, 48000, 32));
    REQUIRE (good.has_value());
    CHECK (good->left == std::vector<float> { 0.5f, 0.125f });
    CHECK (good->right == std::vector<float> { -0.25f, 1.0f });

    CHECK_FALSE (parseIrWav (wav (1, 2, 48000, 32)).has_value());   // integer PCM
    CHECK_FALSE (parseIrWav (wav (3, 1, 48000, 32)).has_value());   // mono
    CHECK_FALSE (parseIrWav (wav (3, 2, 44100, 32)).has_value());   // another rate
    CHECK_FALSE (parseIrWav (wav (3, 2, 48000, 64)).has_value());   // double
    CHECK_FALSE (parseIrWav ({}).has_value());

    auto truncated = wav (3, 2, 48000, 32);
    truncated.resize (truncated.size() - 4);
    CHECK_FALSE (parseIrWav (truncated).has_value());
}

TEST_CASE ("Resampled IRs keep their level and a flat response to 20 kHz, starting at once", "[soundboard][irs]")
{
    // An impulse is the hardest case: a linear-phase resampler would ring before it, which would
    // either add latency or, cut off, colour it.
    const auto rate = GENERATE (44100.0, 88200.0, 96000.0);

    std::vector<float> impulse (4800, 0.0f);
    impulse[0] = 1.0f;
    const std::vector<float>* inputs[] { &impulse };
    const auto outLength = static_cast<std::size_t> (4800 * rate / 48000.0);
    const auto out = resampleSignals (inputs, 48000.0, rate, outLength)[0];

    std::size_t fftSize = 1;
    while (fftSize < 4 * outLength)
        fftSize <<= 1;
    std::vector<double> re (fftSize, 0.0), im (fftSize, 0.0);
    std::copy (out.begin(), out.end(), re.begin());
    ComplexFft<double> fft;
    fft.prepare (fftSize);
    fft.forward (re.data(), im.data());

    // Convolving at the new rate gives the same level, so the response is the 48 kHz impulse's: 0 dB.
    const double expectedDb = 0.0;
    double worstDb = 0.0, worstAt = 0.0;
    double groupDelay = 0.0;  // in output samples, at 1 kHz

    for (std::size_t k = 1; k < fftSize / 2; ++k)
    {
        const double frequency = static_cast<double> (k) * rate / static_cast<double> (fftSize);
        if (frequency > 20000.0)
            break;
        const double deviation = std::abs (10.0 * std::log10 (re[k] * re[k] + im[k] * im[k]) - expectedDb);
        if (deviation > worstDb)
        {
            worstDb = deviation;
            worstAt = frequency;
        }

        if (groupDelay == 0.0 && frequency >= 1000.0)
        {
            const double phase = std::arg (std::complex<double> (re[k], im[k]));
            const double previous = std::arg (std::complex<double> (re[k - 1], im[k - 1]));
            const double step = std::remainder (phase - previous, 2.0 * std::numbers::pi);
            groupDelay = -step / (2.0 * std::numbers::pi / static_cast<double> (fftSize));
        }
    }

    const auto peakAt = std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - out.begin();

    INFO (rate << " Hz: worst deviation " << worstDb << " dB at " << worstAt << " Hz, group delay at 1 kHz " << groupDelay << " samples, peak at " << peakAt);
    CHECK (worstDb < 0.05);
    CHECK (out[0] != 0.0f);
    CHECK (groupDelay < 0.1 * rate / 1000.0);  // well under 0.1 ms
    CHECK (peakAt <= static_cast<std::ptrdiff_t> (rate / 10000.0));
}
