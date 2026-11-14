#include "NoteModel.h"

#include "UnisonModes.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
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
constexpr double longitudinalHighPassHz = 40.0;  // SPEC §2.3
constexpr float phantomRestingState = 1e-9f;     // in output units, like the high-pass's

double aftersoundDecay (const NoteParameters& p, double frequency) noexcept
{
    return p.aftersoundDecayConstant + p.aftersoundDecayPerHertzSquared * frequency * frequency;
}

int stringsInUnison (const NoteParameters& p) noexcept
{
    return std::clamp (p.numStrings, 1, UnisonModes::maxStrings);
}

double stringFrequency (double partialHz, double offsetCents) noexcept
{
    return partialHz * std::pow (2.0, offsetCents / 1200.0);
}

/** One partial's modes. Coupled partials run each string at its aftersound decay, and the
    coupling ε = (σ_prompt − σ_aftersound) / N brings the in-phase motion up to the prompt
    sound's: with the strings in perfect unison, that mode decays at exactly σ_prompt. */
UnisonModes modesOfPartial (const NoteParameters& p, int partial, const double (&offsetsCents)[3]) noexcept
{
    const int strings = stringsInUnison (p);
    const double frequency = partialFrequency (p, partial);

    double coupling = 0.0;
    if (strings > 1 && partial <= NoteModel::coupledPartials)
        coupling = std::max (0.0, (promptDecay (p, frequency) - aftersoundDecay (p, frequency)) / strings);

    std::complex<double> exponents[UnisonModes::maxStrings];

    for (int k = 0; k < strings; ++k)
    {
        const double f = stringFrequency (frequency, offsetsCents[k]);
        const double sigma = coupling > 0.0 ? aftersoundDecay (p, f) : promptDecay (p, f);
        exponents[k] = { -sigma, 2.0 * std::numbers::pi * f };
    }

    return solveUnisonModes (strings, exponents, coupling);
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
    softPedalFeltScale = p.softPedalFeltScale;
    maxContactSubsteps = static_cast<int> (maxContactSeconds * sampleRate) * hammerOversampling;
    liftStepPerTick = static_cast<float> (controlInterval * samplePeriod / damperRampSeconds);
    velocityCurve = VelocityCurve (p.hammerSpeeds, p.maxHammerSpeed);

    const double maxFrequency = std::min (maxPartialHz, maxPartialFractionOfRate * sampleRate);
    int numPartials = 0;
    while (partialFrequency (p, numPartials + 1) < maxFrequency)
        ++numPartials;

    numStrings = stringsInUnison (p);
    numModes = numPartials * numStrings;

    for (auto* v : { &cosStep, &sinStep, &cosSubstep, &sinSubstep, &stringDecay, &damperDecay, &outputWeight,
                     &driveAllRe, &driveAllIm, &driveMissedRe, &driveMissedIm,
                     &viewAllRe, &viewAllIm, &viewMissedRe, &viewMissedIm,
                     &stateRe, &stateIm, &stepRe, &stepIm, &substepRe, &substepIm,
                     &driveRe, &driveIm, &strikeWeightRe, &strikeWeightIm })
        v->assign (static_cast<std::size_t> (numModes), 0.0f);

    const bool phantoms = p.phantomLevel > 0.0 && p.longitudinalFrequency > 0.0;
    numDrivingPartials = phantoms ? std::min (phantomDrivingPartials, numPartials) : 0;
    phantomWeight.assign (static_cast<std::size_t> (numDrivingPartials * numStrings), 0.0f);

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
        const UnisonModes modes = modesOfPartial (p, partial, offsets);

        // Displacement per N·s of hammer force into each string. Modal mass is half the string's.
        double stringDrive[UnisonModes::maxStrings] {};
        for (int k = 0; k < numStrings; ++k)
            stringDrive[k] = 2.0 * strikeShape / (stringMass * 2.0 * std::numbers::pi * stringFrequency (partialFrequency (p, partial), offsets[k]));

        for (int j = 0; j < numStrings; ++j)
        {
            const auto i = static_cast<std::size_t> ((partial - 1) * numStrings + j);
            const double omega = modes.exponent[j].imag();
            const double sigma = -modes.exponent[j].real();
            const double frequency = omega / (2.0 * std::numbers::pi);
            const double sigmaDamper = detail::damperDecay (p, frequency);  // not the member

            cosStep[i] = static_cast<float> (std::cos (omega * samplePeriod));
            sinStep[i] = static_cast<float> (std::sin (omega * samplePeriod));
            cosSubstep[i] = static_cast<float> (std::cos (omega * substepPeriod));
            sinSubstep[i] = static_cast<float> (std::sin (omega * substepPeriod));
            stringDecay[i] = static_cast<float> (-sigma * samplePeriod);
            damperDecay[i] = static_cast<float> (-sigmaDamper * samplePeriod);

            std::complex<double> driveAll = 0.0, viewAll = 0.0;
            for (int k = 0; k < numStrings; ++k)
            {
                driveAll += modes.projection[j][k] * stringDrive[k];
                viewAll += modes.shape[j][k] * strikeShape;
            }

            const auto driveMissed = modes.projection[j][0] * stringDrive[0];
            const auto viewMissed = modes.shape[j][0] * strikeShape;

            driveAllRe[i] = static_cast<float> (driveAll.real());
            driveAllIm[i] = static_cast<float> (driveAll.imag());
            driveMissedRe[i] = static_cast<float> (driveMissed.real());
            driveMissedIm[i] = static_cast<float> (driveMissed.imag());
            viewAllRe[i] = static_cast<float> (viewAll.real());
            viewAllIm[i] = static_cast<float> (viewAll.imag());
            viewMissedRe[i] = static_cast<float> (viewMissed.real());
            viewMissedIm[i] = static_cast<float> (viewMissed.imag());

            outputWeight[i] = static_cast<float> (bridgeSlope * modes.bridgeWeight[j] * outputGain);

            if (partial <= numDrivingPartials)
                phantomWeight[i] = static_cast<float> (partial * modes.bridgeWeight[j]);
        }
    }

    // Phantom partials (Bank, Zambon & Fontana 2010, §V). With Q_n the mean string displacement
    // of partial n and P_n = n·Q_n, the longitudinal bridge force is C·Σ P_n², the string's
    // spatially uniform tension, with C = (π/L)²·EA/4. That is exact while the longitudinal
    // modes are driven below their resonances. Modes resonating inside the tension's band (twice
    // the driving partials') add their dynamic response on top. Each of the N strings adds its
    // own force. The modes carry the bridge sum N·Q_n, hence the 1/N² inside and N outside.
    numLongitudinalModes = 0;
    for (auto* lanes : { &lmB0, &lmB1, &lmA1, &lmA2, &lmOdd, &lmEven, &lmSign })
        lanes->fill (0.0f);

    if (numDrivingPartials > 0)
    {
        const double tensionBand = std::min (maxFrequency, 2.0 * partialFrequency (p, numDrivingPartials));
        while (numLongitudinalModes < maxLongitudinalModes
               && (numLongitudinalModes + 1) * p.longitudinalFrequency < tensionBand)
            ++numLongitudinalModes;

        const double piOverLength = std::numbers::pi / p.stringLength;
        phantomForceScale = static_cast<float> (piOverLength * piOverLength * p.axialStiffness / 4.0
                                                / numStrings * p.phantomLevel * outputGain);

        // Each longitudinal mode: a resonant 2-pole low-pass with unity gain at DC (RBJ
        // cookbook, the bilinear transform prewarped to the mode's frequency). Its dynamic
        // response is that minus its input: a resonant high-pass.
        for (int k = 0; k < numLongitudinalModes; ++k)
        {
            const double w0 = 2.0 * std::numbers::pi * (k + 1) * p.longitudinalFrequency * samplePeriod;
            const double alpha = std::sin (w0) / (2.0 * p.longitudinalQ);
            const double cosW0 = std::cos (w0);
            const double a0 = 1.0 + alpha;
            const auto i = static_cast<std::size_t> (k);
            lmB0[i] = static_cast<float> ((1.0 - cosW0) / 2.0 / a0);
            lmB1[i] = static_cast<float> ((1.0 - cosW0) / a0);
            lmA1[i] = static_cast<float> (-2.0 * cosW0 / a0);
            lmA2[i] = static_cast<float> ((1.0 - alpha) / a0);

            // Mode number k + 1. See phantomSample() for the drives and the sign.
            const bool odd = (k + 1) % 2 == 1;
            lmOdd[i] = odd ? 1.0f : 0.0f;
            lmEven[i] = odd ? 0.0f : 1.0f;
            lmSign[i] = odd ? 1.0f : -1.0f;
        }

        // 1st-order Butterworth high-pass: no slow tension "thump" (SPEC §2.3).
        const double kHp = std::tan (std::numbers::pi * longitudinalHighPassHz * samplePeriod);
        phantomHpB0 = static_cast<float> (1.0 / (1.0 + kHp));
        phantomHpA1 = static_cast<float> ((kHp - 1.0) / (kHp + 1.0));
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
    partialMotion.fill (0.0f);
    lmState1.fill (0.0f);
    lmState2.fill (0.0f);
    phantomHpInput = phantomHpOutput = 0.0f;
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

void NoteModel::strike (double velocity, double touch, double softShift) noexcept
{
    if (numModes == 0)
        return;

    // The soft pedal fades the hammer off string 0 (SPEC §2.7). The force is shared by what it
    // still strikes, and it feels their mean displacement.
    const double shift = std::clamp (softShift, 0.0, 1.0);
    const auto missed = static_cast<float> (numStrings > 1 ? shift : 0.0);
    const float share = 1.0f / (static_cast<float> (numStrings) - missed);

    for (int i = 0; i < numModes; ++i)
    {
        driveRe[i] = (driveAllRe[i] - missed * driveMissedRe[i]) * share;
        driveIm[i] = (driveAllIm[i] - missed * driveMissedIm[i]) * share;
        strikeWeightRe[i] = (viewAllRe[i] - missed * viewMissedRe[i]) * share;
        strikeWeightIm[i] = (viewAllIm[i] - missed * viewMissedIm[i]) * share;
    }

    strikeFeltStiffness = feltStiffness * std::pow (softPedalFeltScale, shift);

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

    if (activeEnd == 0 && std::abs (hpState1) < highPassRestingState && std::abs (hpState2) < highPassRestingState
        && phantomsAtRest())
    {
        hpState1 = hpState2 = 0.0f;
        partialMotion.fill (0.0f);
        lmState1.fill (0.0f);
        lmState2.fill (0.0f);
        phantomHpInput = phantomHpOutput = 0.0f;
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
    // Im((wRe + i·wIm)·(re + i·im)), summed over the modes.
    const float* wr = strikeWeightRe.data();
    const float* wi = strikeWeightIm.data();
    const float* re = stateRe.data();
    const float* im = stateIm.data();
    float sum = 0.0f;

    for (int i = 0; i < numModes; ++i)
        sum += wr[i] * im[i] + wi[i] * re[i];

    return sum;
}

float NoteModel::renderContactSample() noexcept
{
    const double substepPeriod = samplePeriod / hammerOversampling;
    float* re = stateRe.data();
    float* im = stateIm.data();
    const float* mr = substepRe.data();
    const float* mi = substepIm.data();
    const float* dr = driveRe.data();
    const float* di = driveIm.data();

    for (int substep = 0; substep < hammerOversampling; ++substep)
    {
        const double compression = hammerPosition - strikePointDisplacement();
        double force = 0.0;
        double compressionPower = 0.0;

        if (compression > 0.0)
        {
            compressionPower = std::pow (compression, feltExponent);
            const double rate = (compressionPower - previousCompressionPower) / substepPeriod;
            force = std::max (0.0, strikeFeltStiffness * (compressionPower + feltHysteresis * rate));
        }

        previousCompressionPower = compressionPower;
        const auto impulse = static_cast<float> (force * substepPeriod);

        for (int i = 0; i < numModes; ++i)
        {
            const float r = re[i] * mr[i] - im[i] * mi[i] + dr[i] * impulse;
            const float m = re[i] * mi[i] + im[i] * mr[i] + di[i] * impulse;
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

    // The phantom path keeps running through the contact: pausing it caused pops (SPEC §2.3).
    if (numDrivingPartials > 0)
        sum += phantomSample();

    return sum;
}

namespace
{
/** Advances modes [0, end) one sample and returns their output. Kept apart from the phantom
    path so it stays a plain loop the compiler vectorises. */
float stepModes (float* re, float* im, const float* mr, const float* mi, const float* w, int end) noexcept
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

    return sum;
}
} // namespace

void NoteModel::renderFree (float* out, int numSamples) noexcept
{
    float* re = stateRe.data();
    float* im = stateIm.data();
    const float* mr = stepRe.data();
    const float* mi = stepIm.data();
    const float* w = outputWeight.data();
    const int end = activeEnd;

    if (numDrivingPartials == 0)
    {
        for (int t = 0; t < numSamples; ++t)
            out[t] += stepModes (re, im, mr, mi, w, end);
        return;
    }

    for (int t = 0; t < numSamples; ++t)
    {
        const float sum = stepModes (re, im, mr, mi, w, end);
        out[t] += sum + phantomSample();
    }
}

float NoteModel::phantomSample() noexcept
{
    // P_n (at [n − 1]): partial number × the bridge sum of partial n's displacement. Modes at
    // rest are zero, and so is everything past the driving partials.
    const float* im = stateIm.data();
    const float* pw = phantomWeight.data();
    float* motion = partialMotion.data();
    const int partials = numDrivingPartials;

    switch (numStrings)
    {
        case 1:
            for (int n = 0; n < partials; ++n)
                motion[n] = pw[n] * im[n];
            break;
        case 2:
        {
            // Weigh in one contiguous (vectorised) pass, then add each partial's pair.
            float weighted[2 * phantomDrivingPartials];
            for (int i = 0; i < 2 * partials; ++i)
                weighted[i] = pw[i] * im[i];
            for (int n = 0; n < partials; ++n)
                motion[n] = weighted[2 * n] + weighted[2 * n + 1];
            break;
        }
        default:
        {
            float weighted[3 * phantomDrivingPartials];
            for (int i = 0; i < 3 * partials; ++i)
                weighted[i] = pw[i] * im[i];
            for (int n = 0; n < partials; ++n)
                motion[n] = weighted[3 * n] + weighted[3 * n + 1] + weighted[3 * n + 2];
            break;
        }
    }

    // The uniform tension is Σ P_n². Longitudinal mode k is driven by the pairs with m ± n = k.
    // Its dynamic response only passes frequencies near and above its resonance, so the
    // m + n = k pairs (near k·f0) are left out. The rest, 2·Σ P_m·P_{m+k}, is nearly the same for
    // every odd k, and for every even k, so odd modes share mode 3's drive and even modes share
    // mode 4's (Bank et al. 2010). Each is a plain fixed-length reduction, which vectorises.
    float tension = 0.0f, oddDrive = 0.0f, evenDrive = 0.0f;

    for (int m = 0; m < motionLanes; ++m)
        tension += motion[m] * motion[m];
    for (int m = 0; m < motionLanes; ++m)
        oddDrive += motion[m] * motion[m + 3];
    for (int m = 0; m < motionLanes; ++m)
        evenDrive += motion[m] * motion[m + 4];

    oddDrive *= 2.0f;
    evenDrive *= 2.0f;

    // Mode k's static response reaches the bridge as −(−1)^k·C·drive, already inside the uniform
    // tension (with one partial: 2C·P₁² from the slope at the bridge, −C·P₁² from mode 2). Its
    // dynamic response, low-pass minus static, adds (−1)^k·C·(drive − low-pass): lmSign is
    // +1 on odd modes and −1 on even ones, on (low-pass − drive).
    float dynamicLanes[longitudinalLanes];
    const float *b0 = lmB0.data(), *b1 = lmB1.data(), *a1 = lmA1.data(), *a2 = lmA2.data();
    const float *odd = lmOdd.data(), *even = lmEven.data(), *sign = lmSign.data();
    float *s1 = lmState1.data(), *s2 = lmState2.data();
    const int lanes = numLongitudinalModes <= 8 ? 8 : longitudinalLanes;

    for (int i = 0; i < lanes; ++i)
    {
        const float x = odd[i] * oddDrive + even[i] * evenDrive;
        const float y = b0[i] * x + s1[i];
        s1[i] = b1[i] * x - a1[i] * y + s2[i];
        s2[i] = b0[i] * x - a2[i] * y;
        dynamicLanes[i] = sign[i] * (y - x);
    }

    float dynamic = 0.0f;
    for (int i = 0; i < lanes; ++i)
        dynamic += dynamicLanes[i];

    const float force = (tension + dynamic) * phantomForceScale;
    const float y = phantomHpB0 * (force - phantomHpInput) - phantomHpA1 * phantomHpOutput;
    phantomHpInput = force;
    phantomHpOutput = y;
    return y;
}

bool NoteModel::phantomsAtRest() const noexcept
{
    for (int k = 0; k < numLongitudinalModes; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        if (std::abs (lmState1[i]) >= phantomRestingState || std::abs (lmState2[i]) >= phantomRestingState)
            return false;
    }

    return std::abs (phantomHpInput) < phantomRestingState && std::abs (phantomHpOutput) < phantomRestingState;
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

double noteTailSeconds (const NoteParameters& p, double unisonSpreadCents)
{
    // Every mode's share of its partial's initial bridge motion, for a full-scale partial, then
    // the time for the slowest-decaying share to reach the resting level. Higher partials decay
    // faster, so the 20 kHz limit is the sample rate's worst case.
    const int strings = stringsInUnison (p);
    double offsets[3];
    unisonOffsetsCents (strings, unisonSpreadCents, offsets);

    const double logResting = std::log (static_cast<double> (NoteModel::restingAmplitude));
    double tail = 0.0;

    for (int partial = 1; partialFrequency (p, partial) < maxPartialHz; ++partial)
    {
        const UnisonModes modes = modesOfPartial (p, partial, offsets);

        for (double shift : { 0.0, 0.5, 1.0 })
        {
            const double missed = strings > 1 ? shift : 0.0;
            std::complex<double> amplitude[UnisonModes::maxStrings];
            std::complex<double> total = 0.0;

            for (int j = 0; j < modes.numModes; ++j)
            {
                std::complex<double> drive = 0.0;
                for (int k = 0; k < modes.numModes; ++k)
                    drive += modes.projection[j][k] * (k == 0 ? 1.0 - missed : 1.0);

                amplitude[j] = drive * modes.bridgeWeight[j];
                total += amplitude[j];
            }

            // total is the number of strings struck, never zero.
            for (int j = 0; j < modes.numModes; ++j)
            {
                const double share = std::abs (amplitude[j]) / std::abs (total);
                const double sigma = -modes.exponent[j].real();

                if (share > 0.0 && sigma > 0.0)
                    tail = std::max (tail, (std::log (share) - logResting) / sigma);
            }
        }
    }

    return tail;
}

double slowestStringDecay (const NoteParameters& p, double unisonSpreadCents)
{
    double offsets[3];
    unisonOffsetsCents (stringsInUnison (p), unisonSpreadCents, offsets);

    double slowest = std::numeric_limits<double>::infinity();

    for (int partial = 1; partialFrequency (p, partial) < maxPartialHz; ++partial)
    {
        const UnisonModes modes = modesOfPartial (p, partial, offsets);
        for (int j = 0; j < modes.numModes; ++j)
            slowest = std::min (slowest, -modes.exponent[j].real());
    }

    return slowest;
}
} // namespace forte::detail
