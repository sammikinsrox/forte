#include "NoteModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace forte::detail
{
namespace
{
constexpr int hammerOversampling = 4;          // SPEC §2.4
constexpr double maxContactSeconds = 0.02;     // a safety cap; real contact lasts 1–5 ms
constexpr int separatedSubstepsToEnd = 200;    // ~1 ms clear of the strings ends the contact
constexpr double damperRampSeconds = 0.02;     // SPEC §2.5, §4.2
constexpr double maxPartialHz = 20000.0;       // SPEC §2.3
constexpr double maxPartialFractionOfRate = 0.45;
constexpr double highPassAboveHz = 100.0;      // SPEC §2.3
constexpr float highPassRestingState = 1e-9f;

double partialFrequency (const NoteParameters& p, int partial) noexcept
{
    const double n = partial;
    return n * p.fundamental * std::sqrt (1.0 + p.inharmonicity * n * n);
}
} // namespace

void NoteModel::prepare (const NoteParameters& p, double unisonSpreadCents, double outputGain, double sampleRate)
{
    samplePeriod = 1.0 / sampleRate;
    hasDamper = p.hasDamper;
    hammerMass = p.hammerMass;
    feltStiffness = p.feltStiffness;
    feltExponent = p.feltExponent;
    feltHysteresis = p.feltHysteresis;
    maxContactSubsteps = static_cast<int> (maxContactSeconds * sampleRate) * hammerOversampling;
    liftStepPerTick = static_cast<float> (controlInterval * samplePeriod / damperRampSeconds);
    velocityCurve = VelocityCurve (p.hammerSpeeds, p.maxHammerSpeed);

    const double maxFrequency = std::min (maxPartialHz, maxPartialFractionOfRate * sampleRate);
    int numPartials = 0;
    while (partialFrequency (p, numPartials + 1) < maxFrequency)
        ++numPartials;

    const int numStrings = p.numStrings;
    numModes = numPartials * numStrings;

    for (auto* v : { &cosStep, &sinStep, &cosSubstep, &sinSubstep, &stringDecay, &damperDecay, &drive,
                     &strikeWeight, &outputWeight, &stateRe, &stateIm, &stepRe, &stepIm, &substepRe, &substepIm })
        v->assign (static_cast<std::size_t> (numModes), 0.0f);

    double offsets[3];
    unisonOffsetsCents (numStrings, unisonSpreadCents, offsets);

    // Mass of one string: from f0 = (1 / 2L)·√(T/μ).
    const double linearDensity = p.stringTension / (4.0 * p.stringLength * p.stringLength * p.fundamental * p.fundamental);
    const double stringMass = linearDensity * p.stringLength;
    const double substepPeriod = samplePeriod / hammerOversampling;

    for (int partial = 1; partial <= numPartials; ++partial)
    {
        const double strikeShape = std::sin (partial * std::numbers::pi * p.strikePosition);
        const double bridgeSlope = p.stringTension * partial * std::numbers::pi / p.stringLength * (partial % 2 == 0 ? 1.0 : -1.0);

        for (int string = 0; string < numStrings; ++string)
        {
            const auto i = static_cast<std::size_t> ((partial - 1) * numStrings + string);
            const double frequency = partialFrequency (p, partial) * std::pow (2.0, offsets[string] / 1200.0);
            const double omega = 2.0 * std::numbers::pi * frequency;

            const double sigmaString = p.decayConstant + p.decayPerHertzSquared * frequency * frequency;
            const double sigmaDamper = p.damperDecayConstant + p.damperDecayPerRootKilohertz * std::sqrt (frequency / 1000.0);

            cosStep[i] = static_cast<float> (std::cos (omega * samplePeriod));
            sinStep[i] = static_cast<float> (std::sin (omega * samplePeriod));
            cosSubstep[i] = static_cast<float> (std::cos (omega * substepPeriod));
            sinSubstep[i] = static_cast<float> (std::sin (omega * substepPeriod));
            stringDecay[i] = static_cast<float> (-sigmaString * samplePeriod);
            damperDecay[i] = static_cast<float> (-sigmaDamper * samplePeriod);

            // Modal mass is half the string's mass; the hammer's force is shared by the strings.
            drive[i] = static_cast<float> (2.0 * strikeShape / (stringMass * omega * numStrings));
            strikeWeight[i] = static_cast<float> (strikeShape / numStrings);
            outputWeight[i] = static_cast<float> (bridgeSlope * outputGain);
        }
    }

    const double f0 = partialFrequency (p, 1);
    hasHighPass = f0 > highPassAboveHz;

    if (hasHighPass)
    {
        // RBJ cookbook 2nd-order Butterworth high-pass at f0 / 2.
        const double w0 = 2.0 * std::numbers::pi * 0.5 * f0 * samplePeriod;
        const double alpha = std::sin (w0) / std::numbers::sqrt2;
        const double cosW0 = std::cos (w0);
        const double a0 = 1.0 + alpha;
        hpB0 = static_cast<float> ((1.0 + cosW0) / 2.0 / a0);
        hpB1 = static_cast<float> (-(1.0 + cosW0) / a0);
        hpB2 = hpB0;
        hpA1 = static_cast<float> (-2.0 * cosW0 / a0);
        hpA2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    reset();
}

void NoteModel::silence() noexcept
{
    std::fill (stateRe.begin(), stateRe.end(), 0.0f);
    std::fill (stateIm.begin(), stateIm.end(), 0.0f);
    activeEnd = 0;
    inContact = false;
    hpState1 = hpState2 = 0.0f;
    sounding = false;
}

void NoteModel::reset() noexcept
{
    silence();
    keyDown = false;
    keyLift = 0.0f;
    lift = hasDamper ? 0.0f : 1.0f;
}

void NoteModel::setKeyDown (bool down) noexcept
{
    keyDown = down;

    if (! down || ! hasDamper)
        return;

    keyLift = 1.0f;

    if (lift != 1.0f)
    {
        lift = 1.0f;
        if (sounding)
            updateMultipliers();
    }
}

void NoteModel::strike (double velocity, double touch) noexcept
{
    if (numModes == 0)
        return;

    activeEnd = numModes;
    sounding = true;
    inContact = true;
    updateMultipliers();

    // The hammer arrives just touching the strings, wherever they are.
    hammerPosition = strikePointDisplacement();
    hammerVelocity = velocityCurve.hammerSpeed (velocity, touch);
    previousCompressionPower = 0.0;
    contactSubsteps = 0;
    separatedSubsteps = 0;
}

void NoteModel::controlTick (float sustainLift) noexcept
{
    float newLift = 1.0f;

    if (hasDamper)
    {
        if (! keyDown)
            keyLift = std::max (0.0f, keyLift - liftStepPerTick);

        newLift = std::max (keyLift, sustainLift);
    }

    if (newLift != lift)
    {
        lift = newLift;
        if (sounding)
            updateMultipliers();
    }

    if (! sounding || inContact)
        return;

    restQuietModes();

    if (activeEnd == 0 && std::abs (hpState1) < highPassRestingState && std::abs (hpState2) < highPassRestingState)
    {
        hpState1 = hpState2 = 0.0f;
        sounding = false;
    }
}

void NoteModel::render (float* out, float* scratch, int numSamples) noexcept
{
    if (! sounding)
        return;

    int t = 0;
    for (; t < numSamples && inContact; ++t)
        scratch[t] = renderContactSample();

    if (t < numSamples)
    {
        std::fill (scratch + t, scratch + numSamples, 0.0f);
        renderFree (scratch + t, numSamples - t);
    }

    if (! hasHighPass)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] += scratch[i];
        return;
    }

    float s1 = hpState1, s2 = hpState2;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = scratch[i];
        const float y = hpB0 * x + s1;
        s1 = hpB1 * x - hpA1 * y + s2;
        s2 = hpB2 * x - hpA2 * y;
        out[i] += y;
    }

    hpState1 = s1;
    hpState2 = s2;
}

void NoteModel::updateMultipliers() noexcept
{
    const float damping = 1.0f - lift;
    const float* sd = stringDecay.data();
    const float* dd = damperDecay.data();

    for (int i = 0; i < activeEnd; ++i)
    {
        const float r = std::exp (sd[i] + damping * dd[i]);
        stepRe[i] = r * cosStep[i];
        stepIm[i] = r * sinStep[i];
    }

    if (! inContact)
        return;

    // Contact always runs with every mode active (strike() sets activeEnd = numModes).
    for (int i = 0; i < activeEnd; ++i)
    {
        const float r = std::exp ((sd[i] + damping * dd[i]) * (1.0f / hammerOversampling));
        substepRe[i] = r * cosSubstep[i];
        substepIm[i] = r * sinSubstep[i];
    }
}

float NoteModel::strikePointDisplacement() const noexcept
{
    const float* w = strikeWeight.data();
    const float* im = stateIm.data();
    float sum = 0.0f;

    for (int i = 0; i < numModes; ++i)
        sum += w[i] * im[i];

    return sum;
}

float NoteModel::renderContactSample() noexcept
{
    const double substepPeriod = samplePeriod / hammerOversampling;
    float* re = stateRe.data();
    float* im = stateIm.data();
    const float* mr = substepRe.data();
    const float* mi = substepIm.data();
    const float* d = drive.data();

    for (int substep = 0; substep < hammerOversampling; ++substep)
    {
        const double compression = hammerPosition - strikePointDisplacement();
        double force = 0.0;
        double compressionPower = 0.0;

        if (compression > 0.0)
        {
            compressionPower = std::pow (compression, feltExponent);
            const double rate = (compressionPower - previousCompressionPower) / substepPeriod;
            force = std::max (0.0, feltStiffness * (compressionPower + feltHysteresis * rate));
        }

        previousCompressionPower = compressionPower;
        const auto impulse = static_cast<float> (force * substepPeriod);

        for (int i = 0; i < numModes; ++i)
        {
            const float r = re[i] * mr[i] - im[i] * mi[i] + d[i] * impulse;
            const float m = re[i] * mi[i] + im[i] * mr[i];
            re[i] = r;
            im[i] = m;
        }

        hammerVelocity -= force / hammerMass * substepPeriod;
        hammerPosition += hammerVelocity * substepPeriod;

        ++contactSubsteps;
        separatedSubsteps = (compression <= 0.0 && hammerVelocity < 0.0) ? separatedSubsteps + 1 : 0;
    }

    if (separatedSubsteps >= separatedSubstepsToEnd || contactSubsteps >= maxContactSubsteps)
        inContact = false;

    const float* w = outputWeight.data();
    float sum = 0.0f;

    for (int i = 0; i < numModes; ++i)
        sum += w[i] * im[i];

    return sum;
}

void NoteModel::renderFree (float* out, int numSamples) noexcept
{
    float* re = stateRe.data();
    float* im = stateIm.data();
    const float* mr = stepRe.data();
    const float* mi = stepIm.data();
    const float* w = outputWeight.data();
    const int end = activeEnd;

    for (int t = 0; t < numSamples; ++t)
    {
        float sum = 0.0f;

        for (int i = 0; i < end; ++i)
        {
            const float r = re[i] * mr[i] - im[i] * mi[i];
            const float m = re[i] * mi[i] + im[i] * mr[i];
            re[i] = r;
            im[i] = m;
            sum += w[i] * m;
        }

        out[t] += sum;
    }
}

void NoteModel::restQuietModes() noexcept
{
    float* re = stateRe.data();
    float* im = stateIm.data();
    const float* w = outputWeight.data();
    int lastActive = -1;

    for (int i = 0; i < activeEnd; ++i)
    {
        const float amplitudeSquared = (re[i] * re[i] + im[i] * im[i]) * (w[i] * w[i]);

        if (amplitudeSquared < restingAmplitude * restingAmplitude)
        {
            re[i] = 0.0f;
            im[i] = 0.0f;
        }
        else
        {
            lastActive = i;
        }
    }

    activeEnd = lastActive + 1;
}
} // namespace forte::detail
