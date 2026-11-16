#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace forte::lab
{
/** The voicing lab's controls (SPEC §6.5): the user voices the model by ear from the host.
    Every control runs 0–100, and 50 is the committed voicing, so a lab left at its 50s changes
    nothing (renders stay bit-identical). This table is the one list of them: the plugin builds
    its parameters from it, and the export, forte-render's --voicing and tools/fitting's fold read
    the same ids.

    A per-register control has three values, Bass / Middle / Treble, anchored at C2, C4 and C7
    and interpolated across the keys (log for multipliers, linear otherwise), held flat outside. */
enum class Group
{
    hammer,
    strings,
    board,
    output
};

enum class Control
{
    // Hammer: on the next strike
    feltHardness,        // relaxed stiffness ÷4 … fitted … ×4 (log)
    feltSpike,           // ε: none … fitted … 0.95
    velocityBrightness,  // contact speed exponent: 0 … 0.5 … 1 (physical)
    strikePosition,      // ×0.6 … fitted … ×1.4
    hammerMass,          // ×0.5 … fitted … ×2 (log)
    knockLevel,          // none … fitted … ×3

    // Strings: a note is re-prepared the next time it starts from rest
    inharmonicity,       // B ×0.5 … fitted … ×2 (log)
    unisonSpread,        // ×0 … fitted … ×2
    sustain,             // every rate ×2 (shorter) … fitted … ×0.5 (longer), log
    overtoneDecay,       // partial n's rates × (f_n / f_1)^k, k = −0.5 … 0 … +0.5
    secondStage,         // aftersound rate ×2 … fitted … ×0.5, log
    phantomLevel,        // none … fitted … ×2

    // Board: the IRs are rebuilt and crossfaded
    pickup,              // direct-path gain 0 … 0.5 … 2
    boardTilt,           // modes above 1 kHz: −4 … +8 … +16 dB/octave
    colourLow,           // −12 … 0 … +12 dB below 250 Hz, over the fitted colour
    colourMid,           // −12 … 0 … +12 dB from 250 Hz to 2.5 kHz
    colourHigh,          // −12 … 0 … +12 dB above 2.5 kHz

    // Output: immediate
    tapeDrive,           // 0 (off) … 8 … 24

    count
};

inline constexpr int numControls = static_cast<int> (Control::count);

enum class Register
{
    bass,
    middle,
    treble
};

inline constexpr int numRegisters = 3;

/** The keys a register's value sits at (C2, C4, C7). */
inline constexpr std::array<int, numRegisters> registerAnchorKeys { 36, 60, 96 };

struct ControlInfo
{
    Control control;
    std::string_view id;     // permanent (SPEC §7.2): the parameter id's stem and the export's key
    std::string_view name;   // as the host shows it
    Group group;
    bool perRegister;
    std::string_view range;  // what 0, 50 and 100 mean, for the export and the docs
};

inline constexpr std::array<ControlInfo, numControls> controls { {
    { Control::feltHardness,       "feltHardness",       "Felt hardness",       Group::hammer,  true,  "relaxed stiffness /4 ... fitted ... x4 (log)" },
    { Control::feltSpike,          "feltSpike",          "Felt spike",          Group::hammer,  true,  "epsilon 0 ... fitted ... 0.95" },
    { Control::velocityBrightness, "velocityBrightness", "Velocity brightness", Group::hammer,  false, "contact speed exponent 0 ... 0.5 ... 1" },
    { Control::strikePosition,     "strikePosition",     "Strike position",     Group::hammer,  true,  "x0.6 ... fitted ... x1.4" },
    { Control::hammerMass,         "hammerMass",         "Hammer mass",         Group::hammer,  true,  "x0.5 ... fitted ... x2 (log)" },
    { Control::knockLevel,         "knockLevel",         "Knock level",         Group::hammer,  true,  "0 ... fitted ... x3" },
    { Control::inharmonicity,      "inharmonicity",      "Inharmonicity",       Group::strings, true,  "B x0.5 ... fitted ... x2 (log)" },
    { Control::unisonSpread,       "unisonSpread",       "Unison spread",       Group::strings, true,  "x0 ... fitted ... x2" },
    { Control::sustain,            "sustain",            "Sustain",             Group::strings, true,  "every rate x2 ... fitted ... x0.5 (log)" },
    { Control::overtoneDecay,      "overtoneDecay",      "Overtone decay",      Group::strings, true,  "rates x (f_n/f_1)^k, k -0.5 ... 0 ... +0.5" },
    { Control::secondStage,        "secondStage",        "Second stage",        Group::strings, true,  "aftersound rate x2 ... fitted ... x0.5 (log)" },
    { Control::phantomLevel,       "phantomLevel",       "Phantom level",       Group::strings, false, "0 ... fitted ... x2" },
    { Control::pickup,             "pickup",             "Pickup",              Group::board,   true,  "direct-path gain 0 ... 0.5 ... 2" },
    { Control::boardTilt,          "boardTilt",          "Board tilt",          Group::board,   false, "modes above 1 kHz -4 ... +8 ... +16 dB/octave" },
    { Control::colourLow,          "colourLow",          "Colour low",          Group::board,   true,  "-12 ... 0 ... +12 dB below 250 Hz" },
    { Control::colourMid,          "colourMid",          "Colour mid",          Group::board,   true,  "-12 ... 0 ... +12 dB 250 Hz - 2.5 kHz" },
    { Control::colourHigh,         "colourHigh",         "Colour high",         Group::board,   true,  "-12 ... 0 ... +12 dB above 2.5 kHz" },
    { Control::tapeDrive,          "tapeDrive",          "Tape drive",          Group::output,  false, "0 (off) ... 8 ... 24" },
} };

static_assert ([] {
    for (std::size_t i = 0; i < controls.size(); ++i)
        if (static_cast<std::size_t> (controls[i].control) != i)
            return false;
    return true;
}(), "controls[] is in Control's order");

[[nodiscard]] constexpr const ControlInfo& info (Control control) noexcept
{
    return controls[static_cast<std::size_t> (control)];
}

inline constexpr std::array<std::string_view, numRegisters> registerIds { "bass", "middle", "treble" };
inline constexpr std::array<std::string_view, numRegisters> registerNames { "Bass", "Middle", "Treble" };

inline constexpr float neutralValue = 50.0f;
inline constexpr float minimumValue = 0.0f;
inline constexpr float maximumValue = 100.0f;

/** Every lab value. A control without registers keeps its one value at Register::bass. */
struct Settings
{
    std::array<std::array<float, numRegisters>, numControls> values = allNeutral();

    [[nodiscard]] float get (Control control, Register reg = Register::bass) const noexcept
    {
        return values[static_cast<std::size_t> (control)][static_cast<std::size_t> (reg)];
    }

    void set (Control control, Register reg, float value) noexcept
    {
        values[static_cast<std::size_t> (control)][static_cast<std::size_t> (reg)] = value;
    }

    /** True when the control sits at the committed voicing in every register it has. */
    [[nodiscard]] bool isNeutral (Control control) const noexcept
    {
        const auto& v = values[static_cast<std::size_t> (control)];
        return info (control).perRegister ? (v[0] == neutralValue && v[1] == neutralValue && v[2] == neutralValue)
                                          : v[0] == neutralValue;
    }

    /** True when every control in the group is at the committed voicing. */
    [[nodiscard]] bool isNeutral (Group group) const noexcept
    {
        for (const auto& c : controls)
            if (c.group == group && ! isNeutral (c.control))
                return false;
        return true;
    }

    friend bool operator== (const Settings&, const Settings&) = default;

private:
    static constexpr std::array<std::array<float, numRegisters>, numControls> allNeutral() noexcept
    {
        std::array<std::array<float, numRegisters>, numControls> v {};
        for (auto& c : v)
            c = { neutralValue, neutralValue, neutralValue };
        return v;
    }
};
} // namespace forte::lab
