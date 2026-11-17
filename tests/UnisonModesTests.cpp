// Weinreich coupled unison modes (SPEC §2.3), checked against the coupled-string equations.

#include "UnisonModes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using forte::detail::solveUnisonModes;
using forte::detail::UnisonModes;
using Complex = std::complex<double>;

namespace
{
constexpr int maxStrings = UnisonModes::maxStrings;

/** Exponents for strings at a partial's frequency with the SPEC §2.3 unison offsets. */
void unisonStrings (int numStrings, double hz, double spreadCents, double sigma, Complex (&s)[maxStrings])
{
    const double twoStrings[] { -0.5, 0.5, 0.0 };
    const double threeStrings[] { -1.0, 0.2, 0.85 };

    for (int k = 0; k < maxStrings; ++k)
    {
        const double offset = numStrings == 2 ? twoStrings[k] : numStrings == 3 ? threeStrings[k] : 0.0;
        s[k] = { -sigma, 2.0 * std::numbers::pi * hz * std::pow (2.0, offset * spreadCents / 1200.0) };
    }
}

/** Largest error of shape·diag(exponent)·projection against diag(s) − ε·J, and of
    shape·projection against the identity, relative to scale. */
double reconstructionError (const UnisonModes& modes, const Complex (&s)[maxStrings], double scale)
{
    double worst = 0.0;
    const int n = modes.numModes;

    for (int k = 0; k < n; ++k)
    {
        for (int l = 0; l < n; ++l)
        {
            Complex system = 0.0, identity = 0.0;

            for (int j = 0; j < n; ++j)
            {
                system += modes.shape[j][k] * modes.exponent[j] * modes.projection[j][l];
                identity += modes.shape[j][k] * modes.projection[j][l];
            }

            const Complex expected = (k == l ? s[k] : Complex (0.0)) - modes.coupling;
            worst = std::max (worst, std::abs (system - expected) / scale);
            worst = std::max (worst, std::abs (identity - (k == l ? 1.0 : 0.0)));
        }
    }

    return worst;
}

/** The bridge sum's envelope t seconds after all strings start equal (an even hammer blow,
    or string 0 left out). */
double bridgeEnvelope (const UnisonModes& modes, double t, bool missString0 = false)
{
    Complex sum = 0.0;

    for (int j = 0; j < modes.numModes; ++j)
    {
        Complex start = 0.0;
        for (int k = 0; k < modes.numModes; ++k)
            start += modes.projection[j][k] * ((missString0 && k == 0) ? 0.0 : 1.0);

        // Remove the carrier: only the envelope matters here.
        const Complex relative = modes.exponent[j] - Complex (0.0, modes.exponent[0].imag());
        sum += modes.bridgeWeight[j] * start * std::exp (relative * t);
    }

    return std::abs (sum);
}
} // namespace

TEST_CASE ("Coupled modes solve the coupled-string equations", "[unison]")
{
    const int strings = GENERATE (2, 3);
    const double hz = GENERATE (30.0, 65.0, 262.0, 1000.0, 4000.0);
    const double spread = GENERATE (0.0, 0.1, 0.7, 1.4);
    const double prompt = 1.0 + 1.5e-7 * hz * hz;
    const double aftersound = 0.25 * prompt;
    const double coupling = (prompt - aftersound) / strings;

    Complex s[maxStrings];
    unisonStrings (strings, hz, spread, aftersound, s);
    const auto modes = solveUnisonModes (strings, s, coupling);

    INFO (strings << " strings at " << hz << " Hz, spread " << spread << " cents");
    REQUIRE (modes.numModes == strings);
    CHECK (std::abs (modes.coupling / coupling - 1.0) <= 0.25);
    // Zero spread is pulled apart by 1e-6 of the scale, so allow a little more than rounding.
    CHECK (reconstructionError (modes, s, strings * coupling + 2.0 * std::numbers::pi * hz * 1e-3) < 1e-5);

    for (int j = 0; j < strings; ++j)
    {
        // Real bridge weights, and coupling only ever adds loss.
        CHECK (modes.bridgeWeight[j] >= 0.0);
        CHECK (-modes.exponent[j].real() >= aftersound - 1e-9);
        CHECK (-modes.exponent[j].real() <= aftersound + strings * modes.coupling + 1e-9);
    }
}

TEST_CASE ("In perfect unison, the in-phase mode has the prompt decay and the rest the aftersound", "[unison]")
{
    const int strings = GENERATE (2, 3);
    const double prompt = 1.2, aftersound = 0.3;

    Complex s[maxStrings];
    unisonStrings (strings, 100.0, 0.0, aftersound, s);
    const auto modes = solveUnisonModes (strings, s, (prompt - aftersound) / strings);

    int inPhase = 0;
    for (int j = 1; j < strings; ++j)
        if (modes.bridgeWeight[j] > modes.bridgeWeight[inPhase])
            inPhase = j;

    for (int j = 0; j < strings; ++j)
    {
        INFO ("mode " << j);

        if (j == inPhase)
        {
            CHECK (std::abs (-modes.exponent[j].real() - prompt) < 1e-6);
            CHECK (std::abs (modes.bridgeWeight[j] - std::sqrt (static_cast<double> (strings))) < 1e-6);
        }
        else
        {
            CHECK (std::abs (-modes.exponent[j].real() - aftersound) < 1e-6);
            CHECK (modes.bridgeWeight[j] < 1e-3);
        }
    }
}

TEST_CASE ("A detuned unison decays in two stages", "[unison]")
{
    // SPEC §2.3 and the glossary's two-stage decay: fast while the strings move together, then
    // a long quiet tail once they drift apart.
    const int strings = GENERATE (2, 3);
    const double prompt = 1.0, aftersound = 0.2;

    Complex s[maxStrings];
    unisonStrings (strings, 65.0, 0.7, aftersound, s);
    const auto modes = solveUnisonModes (strings, s, (prompt - aftersound) / strings);

    const auto rate = [&] (double from, double to)
    { return std::log (bridgeEnvelope (modes, from) / bridgeEnvelope (modes, to)) / (to - from); };

    const double early = rate (0.0, 0.5);
    const double late = rate (20.0, 30.0);

    INFO (strings << " strings: early " << early << "/s, late " << late << "/s");
    CHECK (early > 0.7 * prompt);
    CHECK (late < 1.2 * aftersound);
    CHECK (bridgeEnvelope (modes, 20.0) > bridgeEnvelope (modes, 0.0) * std::exp (-prompt * 20.0) * 100.0);
}

TEST_CASE ("A string the hammer misses still rings through the coupling", "[unison]")
{
    // The soft pedal's mechanism (SPEC §2.7): strike strings 1 and 2 (or 1) only.
    const int strings = GENERATE (2, 3);

    Complex s[maxStrings];
    unisonStrings (strings, 262.0, 0.7, 0.25, s);
    const auto modes = solveUnisonModes (strings, s, 0.75 / strings);

    Complex missed = 0.0, struck = 0.0;
    const double t = 1.0;

    for (int j = 0; j < strings; ++j)
    {
        Complex start = 0.0;
        for (int k = 1; k < strings; ++k)
            start += modes.projection[j][k];

        const Complex evolved = start * std::exp ((modes.exponent[j] - Complex (0.0, modes.exponent[0].imag())) * t);
        missed += modes.shape[j][0] * evolved;
        struck += modes.shape[j][1] * evolved;
    }

    INFO (strings << " strings: missed " << std::abs (missed) << ", struck " << std::abs (struck));
    CHECK (std::abs (missed) > 0.05 * std::abs (struck));

    // And it starts still.
    Complex atStrike = 0.0;
    for (int j = 0; j < strings; ++j)
    {
        Complex start = 0.0;
        for (int k = 1; k < strings; ++k)
            start += modes.projection[j][k];
        atStrike += modes.shape[j][0] * start;
    }
    CHECK (std::abs (atStrike) < 1e-9);
}

TEST_CASE ("Near an exceptional point the modes stay well-conditioned", "[unison]")
{
    // Two strings with half their detuning equal to the coupling: the modes merge exactly.
    const double coupling = 0.5;
    const double halfDetuning = coupling;
    const Complex s[maxStrings] { { -0.2, 1000.0 - halfDetuning }, { -0.2, 1000.0 + halfDetuning }, {} };

    for (double scale : { 1.0, 1.0 + 1e-9, 1.0 - 1e-9, 1.001 })
    {
        const auto modes = solveUnisonModes (2, s, coupling * scale);
        INFO ("coupling × " << scale << " → used " << modes.coupling);

        REQUIRE (modes.numModes == 2);
        CHECK (modes.coupling > 0.0);
        CHECK (std::abs (modes.coupling / (coupling * scale) - 1.0) <= 0.25);
        CHECK (reconstructionError (modes, s, 2.0 * coupling) < 1e-6);

        for (int j = 0; j < 2; ++j)
        {
            const Complex selfProduct = modes.shape[j][0] * modes.shape[j][0] + modes.shape[j][1] * modes.shape[j][1];
            CHECK (std::abs (selfProduct) >= 1.0 / 30.0);
            CHECK (std::isfinite (modes.projection[j][0].real()));
        }
    }
}

TEST_CASE ("Without coupling the modes are the strings", "[unison]")
{
    const Complex s[maxStrings] { { -1.0, 100.0 }, { -1.1, 101.0 }, { -1.2, 102.0 } };
    const auto modes = solveUnisonModes (3, s, 0.0);

    REQUIRE (modes.numModes == 3);
    for (int j = 0; j < 3; ++j)
    {
        CHECK (modes.exponent[j] == s[j]);
        CHECK (modes.bridgeWeight[j] == 1.0);
        for (int k = 0; k < 3; ++k)
        {
            CHECK (modes.shape[j][k] == Complex (j == k ? 1.0 : 0.0));
            CHECK (modes.projection[j][k] == Complex (j == k ? 1.0 : 0.0));
        }
    }
}
