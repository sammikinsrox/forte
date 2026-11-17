#include "Room.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace forte::detail
{
namespace
{
// Line lengths at Size 50 (ms): mutually prime-ish, spread over 1:2.5.
constexpr std::array<double, Room::numLines> lineMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
constexpr std::array<double, Room::numLines> lfoHz { 0.13, 0.29, 0.37, 0.47, 0.53, 0.67, 0.79, 0.89 };

// Early reflections at Size 50 (ms), left side then right; gains fall with time, signs mixed, and
// odd taps come from the other channel so the image widens.
constexpr std::array<double, Room::tapsPerSide * 2> tapMs {
    7.1, 11.9, 17.3, 19.7, 23.9, 29.3, 33.7, 41.1, 47.3, 55.9,
    8.3, 12.7, 15.1, 21.1, 26.3, 30.7, 37.9, 43.3, 51.1, 58.7 };
constexpr std::array<float, Room::tapsPerSide * 2> tapSign {
    1, -1, 1, 1, -1, 1, -1, 1, 1, -1,
    1, 1, -1, 1, -1, -1, 1, 1, -1, 1 };

constexpr std::array<double, Room::numAllpasses> allpassMs { 4.7, 3.6, 12.7, 9.3 };
constexpr double allpassRightStretch = 1.13;
constexpr float allpassGain = 0.6f;

constexpr double maxScale = 1.6;           // Size 100
constexpr double minScale = 0.4;           // Size 0
constexpr double maxPredelayMs = 100.0;
constexpr double modulationMs = 0.25;
constexpr double glideSeconds = 0.05;
constexpr double highPassHz = 40.0;
constexpr double maxWetGain = 1.5;

double scaleOf (double size) noexcept { return minScale + (maxScale - minScale) * std::clamp (size, 0.0, 100.0) / 100.0; }

/** Room 0-100 to the wet gain: quadratic, so the low half is fine-grained. */
double wetGainOf (double amount) noexcept
{
    const double a = std::clamp (amount, 0.0, 100.0) / 100.0;
    return maxWetGain * a * a;
}

/** Damping 0-100 to how much shorter the highs ring than the lows (1 = as long). */
double highRatioOf (double damping) noexcept { return 1.0 - 0.85 * std::clamp (damping, 0.0, 100.0) / 100.0; }
} // namespace

void Room::DelayLine::allocate (int minimumLength)
{
    int size = 1;
    while (size < minimumLength)
        size <<= 1;
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void Room::DelayLine::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float Room::DelayLine::read (double delay) const noexcept
{
    const auto whole = static_cast<int> (delay);
    const auto fraction = static_cast<float> (delay - whole);
    const float a = buffer[static_cast<std::size_t> ((write - whole) & mask)];
    const float b = buffer[static_cast<std::size_t> ((write - whole - 1) & mask)];
    return a + fraction * (b - a);
}

Room::Params Room::paramsOf (const RoomControls& c) noexcept
{
    return { std::clamp (c.amount, 0.0, 100.0), std::clamp (c.size, 0.0, 100.0), std::clamp (c.decaySeconds, 0.2, 10.0),
             std::clamp (c.damping, 0.0, 100.0), std::clamp (c.predelayMs, 0.0, maxPredelayMs), std::clamp (c.early, 0.0, 100.0) };
}

double Room::tailSeconds (const RoomControls& controls) noexcept
{
    const auto p = paramsOf (controls);
    if (p.amount <= 0.0)
        return 0.0;
    // From the wet gain's peak to −100 dB re full scale: the RT60 slope over 100 dB, plus the
    // pre-delay and the longest early tap.
    return (p.predelayMs + tapMs.back() * maxScale) / 1000.0 + p.decay * 100.0 / 60.0;
}

void Room::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int extra = 4 + 2 * static_cast<int> (std::ceil (modulationMs * samplesPerMs));  // the wander reaches 2× its depth

    const int inputLength = static_cast<int> (std::ceil ((maxPredelayMs + tapMs.back() * maxScale) * samplesPerMs)) + extra;
    inputLeft.allocate (inputLength);
    inputRight.allocate (inputLength);

    for (int i = 0; i < numAllpasses * 2; ++i)
    {
        const double ms = allpassMs[static_cast<std::size_t> (i % numAllpasses)] * (i < numAllpasses ? 1.0 : allpassRightStretch);
        allpasses[static_cast<std::size_t> (i)].line.allocate (static_cast<int> (std::ceil (ms * std::sqrt (maxScale) * samplesPerMs)) + extra);
    }

    for (int i = 0; i < numLines; ++i)
    {
        lines[static_cast<std::size_t> (i)].allocate (static_cast<int> (std::ceil (lineMs[static_cast<std::size_t> (i)] * maxScale * samplesPerMs)) + extra);
        const double w = 2.0 * std::numbers::pi * lfoHz[static_cast<std::size_t> (i)] / sampleRate;
        lfoStepRe[static_cast<std::size_t> (i)] = std::cos (w);
        lfoStepIm[static_cast<std::size_t> (i)] = std::sin (w);
    }

    modulationDepth = modulationMs * samplesPerMs;
    glideCoefficient = 1.0 - std::exp (-tickSamples / (glideSeconds * sampleRate));
    highPassA = static_cast<float> (std::exp (-2.0 * std::numbers::pi * highPassHz / sampleRate));
    tailSamples = static_cast<std::int64_t> (targetTailSeconds * sampleRate);
    prepared = true;
    reset();
}

void Room::reset() noexcept
{
    now = target;
    wetGain = wetGainFrom = static_cast<float> (wetGainOf (now.amount));
    tickPosition = 0;
    if (prepared)
        updateCoefficients();
    asleep = true;
    silentSamples = 0;
}

void Room::setControls (const RoomControls& controls, bool jump) noexcept
{
    target = paramsOf (controls);
    targetTailSeconds = tailSeconds (controls);
    tailSamples = static_cast<std::int64_t> (targetTailSeconds * sampleRate);

    if (jump || ! prepared)
    {
        now = target;
        wetGain = wetGainFrom = static_cast<float> (wetGainOf (now.amount));
        if (prepared)
            updateCoefficients();
    }
}

void Room::wake() noexcept
{
    inputLeft.clear();
    inputRight.clear();
    for (auto& a : allpasses)
        a.line.clear();
    for (auto& l : lines)
        l.clear();
    lineDampState.fill (0.0f);
    for (int i = 0; i < numLines; ++i)
    {
        // Phases spread round the circle, the same on every wake.
        const double phase = 2.0 * std::numbers::pi * i / numLines;
        lfoRe[static_cast<std::size_t> (i)] = std::cos (phase);
        lfoIm[static_cast<std::size_t> (i)] = std::sin (phase);
    }
    earlyStateL = earlyStateR = 0.0f;
    highPassStateL = highPassStateR = highPassInL = highPassInR = 0.0f;
    silentSamples = 0;
    asleep = false;
}

void Room::glide() noexcept
{
    const auto step = [this] (double& value, double goal)
    {
        if (value == goal)
            return false;
        value += (goal - value) * glideCoefficient;
        if (std::abs (goal - value) < 1e-6 * std::max (1.0, std::abs (goal)))
            value = goal;
        return true;
    };

    bool changed = false;
    changed |= step (now.amount, target.amount);
    changed |= step (now.size, target.size);
    changed |= step (now.decay, target.decay);
    changed |= step (now.damping, target.damping);
    changed |= step (now.predelayMs, target.predelayMs);
    changed |= step (now.early, target.early);

    wetGainFrom = wetGain;
    wetGain = static_cast<float> (wetGainOf (now.amount));
    if (changed)
        updateCoefficients();
}

void Room::updateCoefficients() noexcept
{
    const double samplesPerMs = sampleRate / 1000.0;
    scale = scaleOf (now.size);
    predelaySamples = std::max (1.0, now.predelayMs * samplesPerMs);

    for (std::size_t i = 0; i < tapDelay.size(); ++i)
    {
        tapDelay[i] = predelaySamples + tapMs[i] * scale * samplesPerMs;
        // −1.5 dB per tap, then the 1/√taps that keeps the early level near the input's.
        tapGain[i] = static_cast<float> (tapSign[i] * std::pow (10.0, -1.5 * static_cast<double> (i % tapsPerSide) / 20.0) / std::sqrt (tapsPerSide));
    }

    for (std::size_t a = 0; a < allpasses.size(); ++a)
    {
        const double ms = allpassMs[a % numAllpasses] * (a < numAllpasses ? 1.0 : allpassRightStretch);
        allpasses[a].delay = std::max (1.0, ms * std::sqrt (scale) * samplesPerMs);
    }

    const double highRatio = highRatioOf (now.damping);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const double delay = lineMs[i] * scale * samplesPerMs;
        lineDelay[i] = delay;
        // Jot: the loop's gain gives RT60 = decay at DC, and the low-pass's Nyquist gain the
        // shorter high-frequency decay, decay × highRatio.
        const double gainLow = std::pow (10.0, -3.0 * delay / (sampleRate * now.decay));
        const double gainHigh = std::pow (10.0, -3.0 * delay / (sampleRate * now.decay * highRatio));
        const double q = gainHigh / gainLow;  // the low-pass's gain at Nyquist, DC gain 1
        lineGain[i] = static_cast<float> (gainLow);
        lineDampA[i] = static_cast<float> ((1.0 - q) / (1.0 + q));
    }

    // The early reflections darken with Damping too, a little.
    earlyDampA = static_cast<float> (0.15 + 0.6 * now.damping / 100.0);
    earlyGain = static_cast<float> (now.early / 50.0);  // 50 = the taps' own level
    // The tail's level grows with its length; keep the wet sound's loudness near even as Decay
    // and Size move.
    lateGain = static_cast<float> (std::sqrt (0.35 / now.decay) * std::sqrt (scale));
}

void Room::process (float* left, float* right, int numSamples) noexcept
{
    if (! prepared)
        return;

    constexpr float hadamardScale = 0.35355339059327373f;  // 1/√8

    for (int n = 0; n < numSamples; ++n)
    {
        if (tickPosition == 0)
            glide();

        const float ramp = static_cast<float> (tickPosition) / tickSamples;
        const float gain = wetGainFrom + (wetGain - wetGainFrom) * ramp;
        tickPosition = (tickPosition + 1) % tickSamples;

        const float dryL = left[n], dryR = right[n];

        if (asleep)
        {
            if (gain == 0.0f || (dryL == 0.0f && dryR == 0.0f))
                continue;
            wake();
        }

        // Asleep again once the room has rung out, or Room has reached 0.
        if (dryL == 0.0f && dryR == 0.0f)
        {
            if (++silentSamples > tailSamples)
            {
                asleep = true;
                continue;
            }
        }
        else
        {
            silentSamples = 0;
        }

        if (gain == 0.0f && wetGainFrom == 0.0f && target.amount == 0.0)
        {
            asleep = true;
            continue;
        }

        // A gentle high-pass on what the room hears: no rumble or DC building up in the tail.
        const float inL = highPassA * (highPassStateL + dryL - highPassInL);
        const float inR = highPassA * (highPassStateR + dryR - highPassInR);
        highPassInL = dryL;
        highPassInR = dryR;
        highPassStateL = inL;
        highPassStateR = inR;

        inputLeft.push (inL);
        inputRight.push (inR);

        // Early reflections.
        float eL = 0.0f, eR = 0.0f;
        for (int k = 0; k < tapsPerSide; ++k)
        {
            const auto i = static_cast<std::size_t> (k);
            const auto j = static_cast<std::size_t> (k + tapsPerSide);
            eL += tapGain[i] * ((k & 1) ? inputRight.read (tapDelay[i]) : inputLeft.read (tapDelay[i]));
            eR += tapGain[j] * ((k & 1) ? inputLeft.read (tapDelay[j]) : inputRight.read (tapDelay[j]));
        }
        earlyStateL = eL + earlyDampA * (earlyStateL - eL);
        earlyStateR = eR + earlyDampA * (earlyStateR - eR);
        eL = earlyStateL;
        eR = earlyStateR;

        // Diffusion into the tail: the pre-delayed input with the early reflections.
        float dL = inputLeft.read (predelaySamples) + 0.5f * eL;
        float dR = inputRight.read (predelaySamples) + 0.5f * eR;
        for (int a = 0; a < numAllpasses; ++a)
        {
            for (int side = 0; side < 2; ++side)
            {
                auto& ap = allpasses[static_cast<std::size_t> (a + side * numAllpasses)];
                float& x = side == 0 ? dL : dR;
                const float delayed = ap.line.read (ap.delay);
                const float v = x + allpassGain * delayed;
                ap.line.push (v);
                x = delayed - allpassGain * v;
            }
        }

        // The feedback delay network.
        std::array<float, numLines> out {};
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            // The slow wander: a unit phasor rotated a step per sample.
            const double re = lfoRe[i] * lfoStepRe[i] - lfoIm[i] * lfoStepIm[i];
            const double im = lfoRe[i] * lfoStepIm[i] + lfoIm[i] * lfoStepRe[i];
            const double norm = 1.5 - 0.5 * (re * re + im * im);  // keeps it on the circle
            lfoRe[i] = re * norm;
            lfoIm[i] = im * norm;

            const float y = lines[i].read (lineDelay[i] + modulationDepth * (1.0 + im));
            lineDampState[i] = y + lineDampA[i] * (lineDampState[i] - y);
            out[i] = lineGain[i] * lineDampState[i];
        }

        // Hadamard 8 (fast Walsh-Hadamard), scaled to be orthonormal.
        std::array<float, numLines> h = out;
        for (std::size_t span = 1; span < numLines; span <<= 1)
            for (std::size_t i = 0; i < numLines; i += span << 1)
                for (std::size_t j = i; j < i + span; ++j)
                {
                    const float a = h[j], b = h[j + span];
                    h[j] = a + b;
                    h[j + span] = a - b;
                }

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const float input = i < numLines / 2 ? dL : dR;
            lines[i].push (hadamardScale * h[i] + ((i & 1) ? -input : input));
        }

        // Two orthogonal taps of the lines: decorrelated left and right.
        const float lateL = out[0] - out[1] + out[2] - out[3] + out[4] - out[5] + out[6] - out[7];
        const float lateR = out[0] + out[1] - out[2] - out[3] + out[4] + out[5] - out[6] - out[7];

        left[n] = dryL + gain * (earlyGain * eL + lateGain * hadamardScale * lateL);
        right[n] = dryR + gain * (earlyGain * eR + lateGain * hadamardScale * lateR);
    }
}
} // namespace forte::detail
