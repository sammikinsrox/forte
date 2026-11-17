#include "LabMapping.h"

#include <algorithm>
#include <cmath>

namespace forte::detail
{
namespace
{
constexpr double neutral = lab::neutralValue;

/** at0 at 0, fitted at 50, at100 at 100, linear on each side. */
double piecewise (double value, double at0, double fitted, double at100) noexcept
{
    if (value == neutral)
        return fitted;

    return value < neutral ? at0 + (fitted - at0) * (value / neutral)
                           : fitted + (at100 - fitted) * ((value - neutral) / neutral);
}

/** fitted × factorAt100^((value − 50) / 50): fitted ÷ factor at 0, × factor at 100. */
double logScale (double value, double fitted, double factorAt100) noexcept
{
    if (value == neutral)
        return fitted;

    return fitted * std::pow (factorAt100, (value - neutral) / neutral);
}

double interpolate (double a, double b, double t, bool inLog) noexcept
{
    if (a == b)
        return a;

    if (inLog && a > 0.0 && b > 0.0)
        return std::exp (std::log (a) + (std::log (b) - std::log (a)) * t);

    return a + (b - a) * t;
}
} // namespace

double labMap (lab::Control control, double value, double fitted) noexcept
{
    using C = lab::Control;
    value = std::clamp (value, static_cast<double> (lab::minimumValue), static_cast<double> (lab::maximumValue));

    switch (control)
    {
        case C::feltHardness:       return logScale (value, fitted, 4.0);
        case C::feltSpike:          return piecewise (value, 0.0, fitted, 0.95);
        case C::velocityBrightness: return piecewise (value, 0.0, fitted, 1.0);
        case C::strikePosition:     return piecewise (value, 0.6 * fitted, fitted, 1.4 * fitted);
        case C::hammerMass:         return logScale (value, fitted, 2.0);
        case C::knockLevel:         return piecewise (value, 0.0, fitted, 3.0 * fitted);
        case C::inharmonicity:      return logScale (value, fitted, 2.0);
        case C::unisonSpread:       return piecewise (value, 0.0, fitted, 2.0 * fitted);
        case C::sustain:            return logScale (value, fitted, 0.5);   // rates ×2 at 0, ×0.5 at 100
        case C::overtoneDecay:      return piecewise (value, fitted - 0.5, fitted, fitted + 0.5);
        case C::secondStage:        return logScale (value, fitted, 0.5);
        case C::phantomLevel:       return piecewise (value, 0.0, fitted, 2.0 * fitted);
        case C::pickup:             return piecewise (value, 0.0, fitted, 2.0);
        case C::boardTilt:          return piecewise (value, -4.0, fitted, 16.0);
        case C::colourLow:
        case C::colourMid:
        case C::colourHigh:         return piecewise (value, fitted - 12.0, fitted, fitted + 12.0);
        case C::tapeDrive:          return piecewise (value, 0.0, fitted, 24.0);
        case C::count:              break;
    }

    return fitted;
}

bool labInterpolatesInLog (lab::Control control) noexcept
{
    using C = lab::Control;

    switch (control)
    {
        case C::feltHardness:
        case C::strikePosition:
        case C::hammerMass:
        case C::inharmonicity:
        case C::sustain:
        case C::secondStage:
            return true;

        default:
            return false;
    }
}

double labValueAtKey (const lab::Settings& settings, lab::Control control, double midiNote, double fitted) noexcept
{
    if (! lab::info (control).perRegister)
        return labMap (control, settings.get (control), fitted);

    const auto& anchors = lab::registerAnchorKeys;
    int lower = 0;
    double t = 0.0;

    if (midiNote <= anchors[0])
    {
        lower = 0;
    }
    else if (midiNote >= anchors[lab::numRegisters - 1])
    {
        lower = lab::numRegisters - 1;
    }
    else
    {
        lower = midiNote < anchors[1] ? 0 : 1;
        t = (midiNote - anchors[static_cast<std::size_t> (lower)])
          / (anchors[static_cast<std::size_t> (lower + 1)] - anchors[static_cast<std::size_t> (lower)]);
    }

    const double a = labMap (control, settings.get (control, static_cast<lab::Register> (lower)), fitted);
    if (t == 0.0)
        return a;

    const double b = labMap (control, settings.get (control, static_cast<lab::Register> (lower + 1)), fitted);
    return interpolate (a, b, t, labInterpolatesInLog (control));
}

double committedLabValue (lab::Control control, const PianoModel& model, int midiNote) noexcept
{
    using C = lab::Control;
    const auto& p = model.note (std::clamp (midiNote, lowestNote, highestNote));

    switch (control)
    {
        case C::feltSpike:          return p.feltRelaxation;
        case C::velocityBrightness: return p.contactSpeedExponent;
        case C::overtoneDecay:
        case C::colourLow:
        case C::colourMid:
        case C::colourHigh:         return 0.0;
        case C::pickup:             return board::directGain[static_cast<std::size_t> (p.bridgeRegion)];
        case C::boardTilt:          return committedBoardTilt;
        case C::tapeDrive:          return model.tapeDrive;
        default:                    return 1.0;  // the multipliers
    }
}

StrikeVoicing committedStrikeVoicing (const NoteParameters& p) noexcept
{
    StrikeVoicing v;
    v.feltRelaxation = p.feltRelaxation;
    v.contactSpeedExponent = p.contactSpeedExponent;
    return v;
}

StrikeVoicing labStrikeVoicing (const lab::Settings& settings, const NoteParameters& p) noexcept
{
    using C = lab::Control;

    if (settings.isNeutral (lab::Group::hammer))
        return committedStrikeVoicing (p);

    const double key = p.midiNote;
    StrikeVoicing v;
    v.feltStiffnessScale = labValueAtKey (settings, C::feltHardness, key, 1.0);
    v.feltRelaxation = labValueAtKey (settings, C::feltSpike, key, p.feltRelaxation);
    v.contactSpeedExponent = labValueAtKey (settings, C::velocityBrightness, key, p.contactSpeedExponent);
    v.strikePositionScale = labValueAtKey (settings, C::strikePosition, key, 1.0);
    v.hammerMassScale = labValueAtKey (settings, C::hammerMass, key, 1.0);
    v.knockScale = labValueAtKey (settings, C::knockLevel, key, 1.0);
    return v;
}

StringVoicing labStringVoicing (const lab::Settings& settings, const NoteParameters& p) noexcept
{
    using C = lab::Control;

    if (settings.isNeutral (lab::Group::strings))
        return {};

    const double key = p.midiNote;
    StringVoicing v;
    v.inharmonicityScale = labValueAtKey (settings, C::inharmonicity, key, 1.0);
    v.unisonSpreadScale = labValueAtKey (settings, C::unisonSpread, key, 1.0);
    v.sustainRateScale = labValueAtKey (settings, C::sustain, key, 1.0);
    v.overtoneDecayExponent = labValueAtKey (settings, C::overtoneDecay, key, 0.0);
    v.aftersoundRateScale = labValueAtKey (settings, C::secondStage, key, 1.0);
    v.phantomLevelScale = labValueAtKey (settings, C::phantomLevel, key, 1.0);
    return v;
}

NoteParameters applyStringVoicing (const NoteParameters& p, const StringVoicing& v) noexcept
{
    if (v == StringVoicing {})
        return p;

    NoteParameters q = p;

    if (v.inharmonicityScale != 1.0)
    {
        // The tuner put the first partial, f0·√(1 + B), on pitch; keep it there.
        q.inharmonicity = p.inharmonicity * v.inharmonicityScale;
        q.fundamental = p.fundamental * std::sqrt ((1.0 + p.inharmonicity) / (1.0 + q.inharmonicity));
    }

    q.unisonSpreadScale = p.unisonSpreadScale * v.unisonSpreadScale;

    const double prompt = v.sustainRateScale;
    const double aftersound = v.sustainRateScale * v.aftersoundRateScale;
    q.decayConstant *= prompt;
    q.decayPerHertzSquared *= prompt;
    q.aftersoundDecayConstant *= aftersound;
    q.aftersoundDecayPerHertzSquared *= aftersound;
    for (int n = 0; n < q.numMeasuredPartials && n < NoteParameters::maxMeasuredPartials; ++n)
    {
        q.measuredPromptDecay[static_cast<std::size_t> (n)] *= prompt;
        q.measuredAftersoundDecay[static_cast<std::size_t> (n)] *= aftersound;
    }
    // The tail is the aftersound, wherever a partial has one.
    for (int n = 0; n < q.numTailPartials && n < NoteParameters::maxTailPartials; ++n)
        q.tailDecay[static_cast<std::size_t> (n)] *= aftersound;

    q.decayTiltExponent = p.decayTiltExponent + v.overtoneDecayExponent;
    q.phantomLevel = p.phantomLevel * v.phantomLevelScale;
    return q;
}

BoardVoicing labBoardVoicing (const lab::Settings& settings) noexcept
{
    using C = lab::Control;
    auto v = committedBoardVoicing();

    if (settings.isNeutral (lab::Group::board))
        return v;

    v.tiltDbPerOctave = labValueAtKey (settings, C::boardTilt, 0.0, v.tiltDbPerOctave);
    for (int r = 0; r < numBridgeRegions; ++r)
    {
        const auto i = static_cast<std::size_t> (r);
        const double key = regionCentreKey (r);
        v.pickup[i] = labValueAtKey (settings, C::pickup, key, v.pickup[i]);
        v.colourDb[i] = { labValueAtKey (settings, C::colourLow, key, 0.0), labValueAtKey (settings, C::colourMid, key, 0.0),
                          labValueAtKey (settings, C::colourHigh, key, 0.0) };
    }
    return v;
}

double labTapeDrive (const lab::Settings& settings, double fitted) noexcept
{
    return labMap (lab::Control::tapeDrive, settings.get (lab::Control::tapeDrive), fitted);
}
} // namespace forte::detail
