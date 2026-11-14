#pragma once

#include <complex>

namespace forte::detail
{
/** One partial of a unison as independent modes, with Weinreich's bridge coupling (SPEC §2.3).

    String k's partial, in complex modal form z_k (displacement Im z_k), obeys

        dz_k/dt = s_k·z_k − ε·Σ_l z_l

    where s_k = −σ_k + iω_k is the string on its own and ε is the coupling through the bridge's
    resistance: every string pushes on the same bridge, so the coupling has rank one. With ε = 0
    the modes are the strings themselves.

    The solution is z = Σ_j shape_j · w_j with each w_j an independent one-pole,
    w_j' = exponent_j · w_j, and w_j = Σ_k projection_jk · z_k. Because the coupling has rank one,
    a mode's shape is known in closed form from its exponent λ: shape_k ∝ 1 / (s_k − λ). Only the
    N exponents need solving, as the roots of the unison's secular equation ε·Σ_k 1/(s_k − λ) = 1.

    Shapes are normalised to unit length and phased so that their bridge sum Σ_k shape_jk is real
    and non-negative. A mode's contribution to the bridge, and to the mean string displacement
    the phantom partials feel, is then its bridge weight times Im w_j: real weights, as for an
    uncoupled string.

    Exceptional points: where the detuning between strings matches the coupling (for two
    strings, δω/2 = ε), two modes merge and the system has no modal form. Near one, the modes
    become nearly parallel and their amplitudes cancel. The solver then moves ε by 1% steps,
    up to ±25%, to the nearest value where every mode is well-conditioned. Sound is continuous in
    ε, so the change is inaudible. Strings tuned exactly alike (zero spread) are pulled apart by
    a negligible amount for the same reason.

    Not realtime: run it while preparing.
*/
struct UnisonModes
{
    static constexpr int maxStrings = 3;

    int numModes = 0;

    /** The coupling actually used: the requested one, or a nudged one near an exceptional point. */
    double coupling = 0.0;

    /** λ_j, so that w_j(t) = w_j(0)·e^{λ_j t}. Real part −σ (1/s), imaginary part ω (rad/s). */
    std::complex<double> exponent[maxStrings] {};

    /** shape[j][k]: string k's displacement in mode j. */
    std::complex<double> shape[maxStrings][maxStrings] {};

    /** projection[j][k]: mode j's amplitude per unit of string k's state (the inverse of shape). */
    std::complex<double> projection[maxStrings][maxStrings] {};

    /** Σ_k shape[j][k], real and non-negative: how strongly mode j moves the bridge. */
    double bridgeWeight[maxStrings] {};
};

/** Solves a unison's coupled modes for one partial. stringExponents[k] is s_k for the first
    numStrings (1–3) strings; coupling is ε in 1/s, and 0 gives the strings uncoupled. */
[[nodiscard]] UnisonModes solveUnisonModes (int numStrings,
                                            const std::complex<double> (&stringExponents)[UnisonModes::maxStrings],
                                            double coupling) noexcept;
} // namespace forte::detail
