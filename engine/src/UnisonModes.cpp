#include "UnisonModes.h"

#include <algorithm>
#include <cmath>

namespace forte::detail
{
namespace
{
using Complex = std::complex<double>;
constexpr int maxStrings = UnisonModes::maxStrings;

/** A mode is well-conditioned when ‖v‖² / |vᵀv| stays under this. At an exceptional point
    vᵀv → 0 and the modes' amplitudes would cancel each other without bound. */
constexpr double maxConditionNumber = 30.0;
constexpr double nudgeStep = 0.01;
constexpr int maxNudgeSteps = 25;

/** Strings closer than this, relative to the problem's scale, are pulled apart. */
constexpr double minRelativeSeparation = 1e-6;

constexpr int maxRootIterations = 500;
constexpr double rootTolerance = 1e-14;

UnisonModes uncoupled (int numStrings, const Complex (&s)[maxStrings]) noexcept
{
    UnisonModes modes;
    modes.numModes = numStrings;

    for (int j = 0; j < numStrings; ++j)
    {
        modes.exponent[j] = s[j];
        modes.shape[j][j] = 1.0;
        modes.projection[j][j] = 1.0;
        modes.bridgeWeight[j] = 1.0;
    }

    return modes;
}

/** The unison's secular polynomial, monic: Π_k (μ − a_k) + ε·Σ_k Π_{l≠k} (μ − a_l). Its roots
    are the eigenvalues of diag(a) − ε·J. */
Complex secular (int n, const Complex* a, double coupling, Complex mu) noexcept
{
    Complex product = 1.0;
    Complex sum = 0.0;

    for (int k = 0; k < n; ++k)
    {
        Complex others = 1.0;
        for (int l = 0; l < n; ++l)
            if (l != k)
                others *= mu - a[l];

        sum += others;
        product *= mu - a[k];
    }

    return product + coupling * sum;
}

/** Durand–Kerner iteration: deterministic, and quadratic near simple roots. Returns false if it
    doesn't converge, which happens only close to a double root. */
bool solveRoots (int n, const Complex* a, double coupling, double scale, Complex* mu) noexcept
{
    const Complex seed (0.4, 0.9);
    Complex start = 1.0;

    for (int i = 0; i < n; ++i)
    {
        mu[i] = scale * start;
        start *= seed;
    }

    for (int iteration = 0; iteration < maxRootIterations; ++iteration)
    {
        double largestStep = 0.0;

        for (int i = 0; i < n; ++i)
        {
            Complex denominator = 1.0;
            for (int j = 0; j < n; ++j)
                if (j != i)
                    denominator *= mu[i] - mu[j];

            if (denominator == 0.0)
                return false;

            const Complex step = secular (n, a, coupling, mu[i]) / denominator;
            mu[i] -= step;
            largestStep = std::max (largestStep, std::abs (step));
        }

        if (largestStep <= rootTolerance * scale)
            return true;
    }

    return false;
}

/** The modes at one coupling, or false if any of them is ill-conditioned. */
bool solveAt (int n, const Complex* a, Complex centre, double coupling, double scale, UnisonModes& modes) noexcept
{
    Complex mu[maxStrings];
    if (! solveRoots (n, a, coupling, scale, mu))
        return false;

    modes = {};
    modes.numModes = n;
    modes.coupling = coupling;

    for (int j = 0; j < n; ++j)
    {
        Complex v[maxStrings];
        double lengthSquared = 0.0;

        for (int k = 0; k < n; ++k)
        {
            const Complex gap = a[k] - mu[j];
            if (gap == 0.0)
                return false;

            v[k] = 1.0 / gap;
            lengthSquared += std::norm (v[k]);
        }

        // Unit length, then phased so the bridge sum is real and non-negative.
        const double length = std::sqrt (lengthSquared);
        Complex bridge = 0.0;
        for (int k = 0; k < n; ++k)
            bridge += v[k] / length;

        const double bridgeWeight = std::abs (bridge);
        const Complex phase = bridgeWeight > 0.0 ? std::conj (bridge) / bridgeWeight : Complex (1.0);
        Complex selfProduct = 0.0;

        for (int k = 0; k < n; ++k)
        {
            v[k] *= phase / length;
            selfProduct += v[k] * v[k];
        }

        if (std::abs (selfProduct) * maxConditionNumber < 1.0)
            return false;

        // diag(a) − ε·J is complex symmetric, so the left eigenvectors are the right ones
        // transposed: V⁻¹'s row j is v_jᵀ / (v_jᵀ·v_j).
        for (int k = 0; k < n; ++k)
        {
            modes.shape[j][k] = v[k];
            modes.projection[j][k] = v[k] / selfProduct;
        }

        modes.exponent[j] = centre + mu[j];
        modes.bridgeWeight[j] = bridgeWeight;
    }

    return true;
}
} // namespace

UnisonModes solveUnisonModes (int numStrings, const Complex (&stringExponents)[maxStrings], double coupling) noexcept
{
    const int n = std::clamp (numStrings, 1, maxStrings);

    if (! (coupling > 0.0))
        return uncoupled (n, stringExponents);

    if (n == 1)
    {
        // A single string's coupling to the bridge is only extra loss.
        const Complex damped[maxStrings] { stringExponents[0] - coupling };
        auto modes = uncoupled (1, damped);
        modes.coupling = coupling;
        return modes;
    }

    // Solve about the strings' mean, where the numbers are small (detunings, not frequencies).
    Complex centre = 0.0;
    for (int k = 0; k < n; ++k)
        centre += stringExponents[k];
    centre /= static_cast<double> (n);

    Complex a[maxStrings];
    double scale = n * coupling;

    for (int k = 0; k < n; ++k)
    {
        a[k] = stringExponents[k] - centre;
        scale = std::max (scale, std::abs (a[k]));
    }

    // Identical strings have a mode in which the bridge doesn't move and the shape formula has
    // no answer. Detune them by a negligible amount instead.
    const double separation = minRelativeSeparation * scale;

    for (int k = 1; k < n; ++k)
        for (int l = 0; l < k; ++l)
            while (std::abs (a[k] - a[l]) < separation)
                a[k] += Complex (0.0, separation);

    UnisonModes modes;

    for (int step = 0; step <= 2 * maxNudgeSteps; ++step)
    {
        // 0, +1%, −1%, +2%, −2%, …
        const double nudge = nudgeStep * ((step + 1) / 2) * (step % 2 == 1 ? 1.0 : -1.0);

        if (solveAt (n, a, centre, coupling * (1.0 + nudge), scale, modes))
            return modes;
    }

    // Nothing well-conditioned within ±25%. Not seen in practice; fall back to uncoupled strings
    // carrying the coupling's average loss.
    Complex damped[maxStrings];
    for (int k = 0; k < n; ++k)
        damped[k] = stringExponents[k] - coupling;

    return uncoupled (n, damped);
}
} // namespace forte::detail
