#include "SympatheticBank.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace forte::detail
{
namespace
{
constexpr double maxModeHz = 20000.0;  // SPEC §2.3, as the strings
constexpr double maxModeFractionOfRate = 0.45;
constexpr double physicalResonance = 50.0;  // SPEC §6.3

/** The number of bank modes a key has below maxFrequency. */
int bankModes (const NoteParameters& p, double maxFrequency) noexcept
{
    int modes = 0;
    while (modes < SympatheticBank::modesPerKey && partialFrequency (p, modes + 1) < maxFrequency)
        ++modes;
    return modes;
}
} // namespace

void SympatheticBank::prepare (const PianoModel& model, double sampleRate) noexcept
{
    const double samplePeriod = 1.0 / sampleRate;
    const double maxFrequency = std::min (maxModeHz, maxModeFractionOfRate * sampleRate);
    pitchRatios.fill (1.0);

    for (int k = 0; k < numNotes; ++k)
    {
        const auto& p = model.notes[static_cast<std::size_t> (k)];
        auto& key = keys[static_cast<std::size_t> (k)];
        key = Key {};
        key.numModes = bankModes (p, maxFrequency);
        key.usedLanes = (key.numModes + laneGroup - 1) / laneGroup * laneGroup;
        key.restingLift = p.hasDamper ? 0.0f : 1.0f;
        key.region = static_cast<int> (p.bridgeRegion);

        for (int n = 0; n < key.numModes; ++n)
        {
            const auto i = static_cast<std::size_t> (n);
            const double frequency = partialFrequency (p, n + 1);
            const double theta = 2.0 * std::numbers::pi * frequency * samplePeriod;
            const double sigma = promptDecayOfPartial (p, n + 1);

            key.theta[i] = theta;
            key.cosStep[i] = static_cast<float> (std::cos (theta));
            key.sinStep[i] = static_cast<float> (std::sin (theta));
            key.stringDecay[i] = static_cast<float> (-sigma * samplePeriod);
            key.damperDecay[i] = p.hasDamper ? static_cast<float> (-damperDecay (p, frequency) * samplePeriod) : 0.0f;

            // The mode is w ← p·w + x with p = r·e^{iθ}, x real, read as Im w. At its own
            // frequency that's (1/2i)·(1/(1 − r) − 1/(1 − r·e^{−2iθ})); the weight scales it to
            // the model's level with the damper clear.
            const double r = std::exp (-sigma * samplePeriod);
            const std::complex<double> unitGain = (1.0 / (1.0 - r) - 1.0 / (1.0 - r * std::polar (1.0, -2.0 * theta)))
                                                / std::complex<double> (0.0, 2.0);
            key.outputWeight[i] = static_cast<float> (model.sympatheticLevel / std::abs (unitGain));
        }
    }

    reset();
}

void SympatheticBank::silenceKey (Key& key) noexcept
{
    key.stateRe.fill (0.0f);
    key.stateIm.fill (0.0f);
    key.active = false;
    key.driven = false;
}

void SympatheticBank::silence() noexcept
{
    for (auto& key : keys)
        silenceKey (key);
}

void SympatheticBank::reset() noexcept
{
    for (auto& key : keys)
    {
        silenceKey (key);
        key.lift = key.restingLift;
        updateMultipliers (key);
    }
}

void SympatheticBank::setResonance (double resonance) noexcept
{
    level = static_cast<float> (std::clamp (resonance, 0.0, maxResonance) / physicalResonance);
}

void SympatheticBank::setPitchRatio (int k, double ratio) noexcept
{
    auto& key = keys[static_cast<std::size_t> (k)];
    auto& current = pitchRatios[static_cast<std::size_t> (k)];
    if (ratio == current)
        return;

    current = ratio;
    for (int n = 0; n < key.numModes; ++n)
    {
        const auto i = static_cast<std::size_t> (n);
        key.cosStep[i] = static_cast<float> (std::cos (key.theta[i] * ratio));
        key.sinStep[i] = static_cast<float> (std::sin (key.theta[i] * ratio));
    }
    updateMultipliers (key);
}

void SympatheticBank::updateMultipliers (Key& key) noexcept
{
    const float damping = 1.0f - key.lift;

    for (int i = 0; i < lanes; ++i)
    {
        const auto l = static_cast<std::size_t> (i);
        const float r = std::exp (key.stringDecay[l] + damping * key.damperDecay[l]);
        key.stepRe[l] = r * key.cosStep[l];
        key.stepIm[l] = r * key.sinStep[l];
    }
}

void SympatheticBank::controlTick (int k, float lift) noexcept
{
    auto& key = keys[static_cast<std::size_t> (k)];

    if (lift != key.lift)
    {
        key.lift = lift;
        updateMultipliers (key);
    }

    if (! key.active)
        return;

    if (level == 0.0f)
    {
        silenceKey (key);  // nothing reaches the output, so nothing needs to ring
        return;
    }

    if (key.driven)
    {
        key.driven = false;
        return;
    }

    const float threshold = restingAmplitude * restingAmplitude / (level * level);

    for (int i = 0; i < lanes; ++i)
    {
        const auto l = static_cast<std::size_t> (i);
        const float w = key.outputWeight[l];
        if ((key.stateRe[l] * key.stateRe[l] + key.stateIm[l] * key.stateIm[l]) * (w * w) >= threshold)
            return;
    }

    silenceKey (key);
}

template <int Groups>
void SympatheticBank::renderKey (Key& key, const float* x, __m128* acc, int numSamples) noexcept
{
    // The key's lanes stay in registers for the whole stretch, and its groups are independent
    // chains, so they overlap in the pipeline. Each sample's output joins the region's 4-wide
    // accumulator, summed across once per region.
    __m128 re[Groups], im[Groups], mr[Groups], mi[Groups], w[Groups];
    const __m128 gain = _mm_set1_ps (level);

    for (int g = 0; g < Groups; ++g)
    {
        re[g] = _mm_load_ps (key.stateRe.data() + laneGroup * g);
        im[g] = _mm_load_ps (key.stateIm.data() + laneGroup * g);
        mr[g] = _mm_load_ps (key.stepRe.data() + laneGroup * g);
        mi[g] = _mm_load_ps (key.stepIm.data() + laneGroup * g);
        w[g] = _mm_mul_ps (_mm_load_ps (key.outputWeight.data() + laneGroup * g), gain);
    }

    for (int t = 0; t < numSamples; ++t)
    {
        const __m128 input = _mm_set1_ps (x[t]);
        __m128 sum = _mm_setzero_ps();

        for (int g = 0; g < Groups; ++g)
        {
            const __m128 r = _mm_add_ps (_mm_sub_ps (_mm_mul_ps (re[g], mr[g]), _mm_mul_ps (im[g], mi[g])), input);
            const __m128 m = _mm_add_ps (_mm_mul_ps (re[g], mi[g]), _mm_mul_ps (im[g], mr[g]));
            re[g] = r;
            im[g] = m;
            sum = _mm_add_ps (sum, _mm_mul_ps (w[g], m));
        }

        acc[t] = _mm_add_ps (acc[t], sum);
    }

    for (int g = 0; g < Groups; ++g)
    {
        _mm_store_ps (key.stateRe.data() + laneGroup * g, re[g]);
        _mm_store_ps (key.stateIm.data() + laneGroup * g, im[g]);
    }
}

void SympatheticBank::render (const float* drive, const std::array<const float*, numNotes>& own,
                              const std::array<float*, numBridgeRegions>& out, int numSamples) noexcept
{
    const int n = std::min (numSamples, maxRenderSamples);
    __m128 acc[numBridgeRegions][maxRenderSamples];
    bool regionUsed[numBridgeRegions] {};

    for (std::size_t k = 0; k < keys.size(); ++k)
    {
        auto& key = keys[k];
        const bool driven = drive != nullptr && key.lift > 0.0f && key.numModes > 0 && level > 0.0f;

        if (! driven && ! key.active)
            continue;

        key.active = true;
        key.driven = key.driven || driven;

        // The drive: every played note but the key's own, through its damper's gate.
        alignas (16) float x[maxRenderSamples];
        if (! driven)
            std::fill_n (x, n, 0.0f);
        else if (own[k] == nullptr)
            for (int t = 0; t < n; ++t)
                x[t] = key.lift * drive[t];
        else
            for (int t = 0; t < n; ++t)
                x[t] = key.lift * (drive[t] - own[k][t]);

        auto* regionAcc = acc[key.region];
        if (! regionUsed[key.region])
        {
            std::fill_n (regionAcc, n, _mm_setzero_ps());
            regionUsed[key.region] = true;
        }

        switch (key.usedLanes / laneGroup)
        {
            case 1:  renderKey<1> (key, x, regionAcc, n); break;
            case 2:  renderKey<2> (key, x, regionAcc, n); break;
            case 3:  renderKey<3> (key, x, regionAcc, n); break;
            case 4:  renderKey<4> (key, x, regionAcc, n); break;
            default: break;  // no modes
        }
    }

    for (int r = 0; r < numBridgeRegions; ++r)
    {
        if (! regionUsed[r])
            continue;

        float* o = out[static_cast<std::size_t> (r)];
        for (int t = 0; t < n; ++t)
        {
            __m128 s = acc[r][t];
            s = _mm_add_ps (s, _mm_movehl_ps (s, s));
            s = _mm_add_ss (s, _mm_shuffle_ps (s, s, 1));
            o[t] += _mm_cvtss_f32 (s);
        }
    }
}

double sympatheticTailSeconds (const PianoModel& model, double notesTailSeconds, double slowestStringDecay)
{
    // A bank mode b (gain G at resonance, decay σ_b) driven at its own frequency by a string
    // mode of amplitude A decaying at σ_s answers with G·σ_b·A·(e^{−σ_s·t} − e^{−σ_b·t}) / (σ_b − σ_s).
    // If σ_b > σ_s, once the string mode rests (A·e^{−σ_s·t} at the resting level, within the
    // strings' tail) the bank mode is below g = 2·G·σ_b / (σ_b − σ_s) times that, and decays on
    // from there at σ_b. If not, the e^{−σ_b·t} term outlasts the string: from A = 1 at the
    // strike, it rests after ln(g / resting) / σ_b. The slowest string mode is the worst case.
    const double gain = model.sympatheticLevel * SympatheticBank::maxResonance / physicalResonance;
    const double logResting = std::log (static_cast<double> (SympatheticBank::restingAmplitude));
    const double sigmaS = slowestStringDecay;
    double tail = notesTailSeconds;

    for (const auto& p : model.notes)
    {
        const int modes = bankModes (p, maxModeHz);
        for (int n = 1; n <= modes; ++n)
        {
            const double sigmaB = promptDecayOfPartial (p, n);
            // Equal decays would make the bound infinite; it's then G·σ·t·e^{−σt}, which this
            // overestimates for any realistic t.
            const double separation = std::max (std::abs (sigmaB - sigmaS), 0.01 * sigmaB);
            const double g = 2.0 * gain * sigmaB / separation;

            if (sigmaB > sigmaS)
                tail = std::max (tail, notesTailSeconds + std::max (0.0, std::log (g)) / sigmaB);
            else
                tail = std::max (tail, (std::log (g) - logResting) / sigmaB);
        }
    }

    return tail;
}
} // namespace forte::detail
