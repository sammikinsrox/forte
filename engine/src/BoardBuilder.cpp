#include "BoardBuilder.h"

#include "BoardSettings.h"
#include "EmbeddedFiles.h"
#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace forte::detail
{
namespace
{
constexpr const char* perspectiveNames[numPerspectives] { "microphones", "pianist" };
constexpr const char* regionNames[numBridgeRegions] { "bass", "tenor", "middle", "treble" };

/** The lab's colour shelves: each crossover blends over one octave, centred on its frequency,
    in log frequency. */
constexpr double colourLowMidHz = 250.0;
constexpr double colourMidHighHz = 2500.0;

using Signal = std::vector<double>;

double smoothStep (double frequency, double centre) noexcept
{
    const double x = std::clamp (std::log2 (frequency / centre) + 0.5, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

/** np.interp(log f, log centres, gains): linear in log frequency, held flat beyond the ends. */
template <std::size_t N>
double interpolateColour (double frequency, const std::array<double, N>& centres, const std::array<double, N>& gains) noexcept
{
    if (frequency <= centres.front())
        return gains.front();
    if (frequency >= centres.back())
        return gains.back();

    const auto upper = static_cast<std::size_t> (std::upper_bound (centres.begin(), centres.end(), frequency) - centres.begin());
    const auto lower = upper - 1;
    const double t = (std::log (frequency) - std::log (centres[lower])) / (std::log (centres[upper]) - std::log (centres[lower]));
    return gains[lower] + t * (gains[upper] - gains[lower]);
}

/** A gain curve for equalise(): gainAt(f) is the linear gain at f Hz, for each of fft size n's
    bins 0..n/2 (bin 0 read at 1 Hz, as make_seed_irs.py does). */
template <typename GainAt>
Signal gainCurve (std::size_t n, GainAt&& gainAt)
{
    const double binHz = irFileSampleRate / static_cast<double> (n);
    Signal gain (n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k)
        gain[k] = gainAt (std::max (static_cast<double> (k) * binHz, 1.0));
    return gain;
}

/** Zero-phase EQ, as numpy's irfft(rfft(x, n) · gain, n)[:len(x)], with gain from gainCurve().
    The curves are smooth, so their short acausal part lands in the padding and is cut. */
void equalise (std::span<Signal* const> signals, RealFft<double>& fft, const Signal& gain)
{
    const auto n = fft.getSize();
    const auto half = n / 2;

    Signal buffer (n), re (half), im (half);
    for (auto* x : signals)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0);
        std::copy_n (x->begin(), std::min (x->size(), n), buffer.begin());
        fft.forward (buffer.data(), re.data(), im.data());

        re[0] *= gain[0];
        im[0] *= gain[half];  // the packed Nyquist bin
        for (std::size_t k = 1; k < half; ++k)
        {
            re[k] *= gain[k];
            im[k] *= gain[k];
        }

        fft.inverse (re.data(), im.data(), buffer.data());
        const double scale = 1.0 / static_cast<double> (n);
        for (std::size_t i = 0; i < x->size(); ++i)
            (*x)[i] = buffer[i] * scale;
    }
}

/** make_seed_irs.py's low_shelf(): an RBJ-cookbook low shelf's impulse response. */
Signal lowShelf (double gainDb, double cornerHz, std::size_t taps)
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * cornerHz / irFileSampleRate;
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
    const double c = std::cos (w0);
    const double b0 = a * ((a + 1) - (a - 1) * c + 2 * std::sqrt (a) * alpha);
    const double b1 = 2 * a * ((a - 1) - (a + 1) * c);
    const double b2 = a * ((a + 1) - (a - 1) * c - 2 * std::sqrt (a) * alpha);
    const double d0 = (a + 1) + (a - 1) * c + 2 * std::sqrt (a) * alpha;
    const double d1 = -2 * ((a - 1) + (a + 1) * c);
    const double d2 = (a + 1) + (a - 1) * c - 2 * std::sqrt (a) * alpha;

    Signal h (taps);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (std::size_t i = 0; i < taps; ++i)
    {
        const double x0 = i == 0 ? 1.0 : 0.0;
        const double y0 = (b0 * x0 + b1 * x1 + b2 * x2 - d1 * y1 - d2 * y2) / d0;
        h[i] = y0;
        x2 = x1; x1 = x0; y2 = y1; y1 = y0;
    }
    return h;
}

double energy (const Signal& x) noexcept
{
    double sum = 0.0;
    for (const double v : x)
        sum += v * v;
    return sum;
}

/** make_seed_irs.py's band_power_gain(): both channels' mean power gain over the band, each
    octave weighted equally. */
double bandPowerGain (const Signal& left, const Signal& right, RealFft<double>& fft)
{
    const auto n = fft.getSize();
    const double binHz = irFileSampleRate / static_cast<double> (n);
    Signal buffer (n), re (n / 2), im (n / 2);
    double total = 0.0;

    for (const auto* x : { &left, &right })
    {
        std::fill (buffer.begin(), buffer.end(), 0.0);
        std::copy_n (x->begin(), std::min (x->size(), n), buffer.begin());
        fft.forward (buffer.data(), re.data(), im.data());

        double power = 0.0, weights = 0.0;
        for (std::size_t k = 1; k < n / 2; ++k)
        {
            const double f = static_cast<double> (k) * binHz;
            if (f < board::bandLowHz || f > board::bandHighHz)
                continue;
            power += (re[k] * re[k] + im[k] * im[k]) / f;
            weights += 1.0 / f;
        }
        total += power / weights;
    }

    return total / 2.0;
}

Signal toSignal (const std::vector<float>& x, std::size_t taps)
{
    Signal s (taps, 0.0);
    std::copy_n (x.begin(), std::min (x.size(), taps), s.begin());
    return s;
}
} // namespace

BoardVoicing committedBoardVoicing() noexcept
{
    BoardVoicing v;
    v.pickup.fill (board::directGain);
    v.tiltDbPerOctave = board::modeTiltDbPerOctave;
    return v;
}

double regionCentreKey (int region) noexcept
{
    const auto& keys = board::regionKeys[static_cast<std::size_t> (region)];
    return 0.5 * (keys[0] + keys[1]);
}

BoardParts loadEmbeddedBoardParts()
{
    BoardParts parts;
    for (int p = 0; p < numPerspectives; ++p)
        for (int r = 0; r < numBridgeRegions; ++r)
        {
            const auto name = std::string ("parts/") + perspectiveNames[p] + "-" + regionNames[r] + "-modes.wav";
            std::optional<StereoIr> ir;
            if (const auto* file = findEmbeddedFile (name))
                ir = parseIrWav ({ file->data, file->size });

            const auto silent = std::vector<float> (static_cast<std::size_t> (board::irTaps), 0.0f);
            parts.modes[static_cast<std::size_t> (p)][static_cast<std::size_t> (r)] = ir ? std::move (*ir) : StereoIr { silent, silent };
        }
    return parts;
}

SoundboardIrs buildBoardIrs (const BoardParts& parts, const BoardVoicing& voicing)
{
    const auto taps = static_cast<std::size_t> (board::irTaps);

    RealFft<double> colourFft, bandFft;
    colourFft.prepare (static_cast<std::size_t> (board::colourFftSize));
    bandFft.prepare (static_cast<std::size_t> (board::bandFftSize));

    const auto shelf = lowShelf (board::directLowShelfDb, board::directLowShelfHz, taps);

    // make_seed_irs.py's fade_out(): a raised cosine that reaches exactly 0.
    Signal fade (taps, 1.0);
    const auto fadeTaps = static_cast<std::size_t> (std::lround (board::fadeSeconds * irFileSampleRate));
    for (std::size_t i = 0; i < fadeTaps; ++i)
        fade[taps - fadeTaps + i] = 0.5 * (1.0 + std::cos (std::numbers::pi * static_cast<double> (i + 1) / static_cast<double> (fadeTaps)));

    // The tilt beyond the committed one, as a smooth EQ on the modes (make_seed_irs.py tilts
    // each mode's gain; the modes are narrow, so on the sum that's the same curve).
    const double extraTilt = voicing.tiltDbPerOctave - board::modeTiltDbPerOctave;
    const auto colourSize = colourFft.getSize();
    Signal tiltGain;
    if (extraTilt != 0.0)
        tiltGain = gainCurve (colourSize, [&] (double f) {
            return std::pow (10.0, extraTilt * std::log2 (std::max (f / board::modeTiltFromHz, 1.0)) / 20.0);
        });

    // Each region's colour: the fitted curve plus the lab's shelves.
    std::array<Signal, numBridgeRegions> colourGain;
    for (int r = 0; r < numBridgeRegions; ++r)
    {
        const auto& lab = voicing.colourDb[static_cast<std::size_t> (r)];
        const auto& fitted = board::colourDb[static_cast<std::size_t> (r)];
        const bool labColour = lab != std::array<double, 3> {};

        if (board::hasColour || labColour)
            colourGain[static_cast<std::size_t> (r)] = gainCurve (colourSize, [&] (double f) {
                double db = board::hasColour ? interpolateColour (f, board::colourCentresHz, fitted) : 0.0;
                if (labColour)
                {
                    const double low = smoothStep (f, colourLowMidHz), high = smoothStep (f, colourMidHighHz);
                    db += lab[0] * (1.0 - low) + lab[1] * (low - high) + lab[2] * high;
                }
                return std::pow (10.0, db / 20.0);
            });
    }

    SoundboardIrs result;
    result.sampleRate = irFileSampleRate;

    for (int p = 0; p < numPerspectives; ++p)
    {
        std::array<std::array<Signal, 2>, numBridgeRegions> built;

        for (int r = 0; r < numBridgeRegions; ++r)
        {
            const auto& modes = parts.modes[static_cast<std::size_t> (p)][static_cast<std::size_t> (r)];
            auto& [left, right] = built[static_cast<std::size_t> (r)];
            left = toSignal (modes.left, taps);
            right = toSignal (modes.right, taps);
            Signal* sides[] { &left, &right };

            if (extraTilt != 0.0)
            {
                equalise (sides, colourFft, tiltGain);

                const double peakEnergy = std::max (energy (left), energy (right));
                if (peakEnergy > 0.0)
                {
                    const double scale = 1.0 / std::sqrt (peakEnergy);
                    for (auto* side : sides)
                        for (auto& v : *side)
                            v *= scale;
                }
            }

            const double pickup = voicing.pickup[static_cast<std::size_t> (r)];
            for (auto* side : sides)
                for (std::size_t i = 0; i < taps; ++i)
                    (*side)[i] += pickup * shelf[i];

            if (const auto& gain = colourGain[static_cast<std::size_t> (r)]; ! gain.empty())
                equalise (sides, colourFft, gain);

            // make_seed_irs.py's placement_gains(): constant power, unity per channel at the centre.
            const auto& keys = board::regionKeys[static_cast<std::size_t> (r)];
            const double position = board::perspectiveSpread[static_cast<std::size_t> (p)] * (0.5 * (keys[0] + keys[1]) - 64.5) / 43.5;
            const double theta = (position + 1.0) * std::numbers::pi / 4.0;
            const double gains[] { std::sqrt (2.0) * std::cos (theta), std::sqrt (2.0) * std::sin (theta) };

            for (int side = 0; side < 2; ++side)
                for (std::size_t i = 0; i < taps; ++i)
                    (*sides[side])[i] *= gains[side] * fade[i];
        }

        double meanGain = 0.0;
        for (const auto& [left, right] : built)
            meanGain += bandPowerGain (left, right, bandFft);
        meanGain /= numBridgeRegions;
        const double scale = 1.0 / std::sqrt (meanGain);

        for (int r = 0; r < numBridgeRegions; ++r)
        {
            auto& ir = result.sets[static_cast<std::size_t> (p)][static_cast<std::size_t> (r)];
            for (int side = 0; side < 2; ++side)
            {
                const auto& from = built[static_cast<std::size_t> (r)][static_cast<std::size_t> (side)];
                auto& to = side == 0 ? ir.left : ir.right;
                to.resize (taps);
                for (std::size_t i = 0; i < taps; ++i)
                    to[i] = static_cast<float> (from[i] * scale);
            }
        }
    }

    return result;
}
} // namespace forte::detail
