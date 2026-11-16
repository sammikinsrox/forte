#pragma once

// Include after the JUCE module headers (juce_core at least).

#include "forte/engine/VoicingLab.h"

#include <optional>

namespace forte::bridge
{
/** The voicing file (SPEC §6.5): the lab's values, as Export voicing writes them to
    Documents/Forte/voicing.json and forte-render --voicing and tools/fitting read them.

    {
      "format": "forte-voicing", "version": 1,
      "controls": {
        "feltHardness": { "bass": 50, "middle": 62, "treble": 40 },
        "tapeDrive": 50,
        ...
      }
    }

    Values are the lab's 0–100. A control or register the file leaves out is at 50, and keys it
    doesn't know are ignored, so older and newer files both read. */
inline constexpr const char* voicingFormat = "forte-voicing";
inline constexpr int voicingFormatVersion = 1;

inline juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

/** The settings' "controls" object. */
inline juce::var voicingControlsToVar (const lab::Settings& settings)
{
    auto* controls = new juce::DynamicObject();

    for (const auto& c : lab::controls)
    {
        if (! c.perRegister)
        {
            controls->setProperty (toJuceString (c.id), settings.get (c.control));
            continue;
        }

        auto* registers = new juce::DynamicObject();
        for (int r = 0; r < lab::numRegisters; ++r)
            registers->setProperty (toJuceString (lab::registerIds[static_cast<std::size_t> (r)]),
                                    settings.get (c.control, static_cast<lab::Register> (r)));
        controls->setProperty (toJuceString (c.id), juce::var (registers));
    }

    return juce::var (controls);
}

/** Reads a voicing file's text. Returns nothing, with a reason in error, if it isn't one. */
inline std::optional<lab::Settings> parseVoicing (const juce::String& text, juce::String& error)
{
    const auto root = juce::JSON::parse (text);

    if (! root.isObject() || root["format"].toString() != voicingFormat)
    {
        error = "not a Forte voicing file (its \"format\" must be \"forte-voicing\")";
        return std::nullopt;
    }

    const auto controls = root["controls"];
    if (! controls.isObject())
    {
        error = "the voicing file has no \"controls\" object";
        return std::nullopt;
    }

    const auto readValue = [&error] (const juce::var& v, const juce::String& where, float& out)
    {
        if (v.isVoid())
            return true;  // left out: 50

        if (! (v.isDouble() || v.isInt() || v.isInt64()))
        {
            error = where + " must be a number";
            return false;
        }

        const auto value = static_cast<double> (v);
        if (! (value >= lab::minimumValue && value <= lab::maximumValue))
        {
            error = where + " must be between 0 and 100";
            return false;
        }

        out = static_cast<float> (value);
        return true;
    };

    lab::Settings settings;

    for (const auto& c : lab::controls)
    {
        const auto id = toJuceString (c.id);
        const auto entry = controls[juce::Identifier (id)];

        if (! c.perRegister)
        {
            float value = lab::neutralValue;
            if (! readValue (entry, id, value))
                return std::nullopt;
            settings.set (c.control, lab::Register::bass, value);
            continue;
        }

        if (entry.isVoid())
            continue;

        if (! entry.isObject())
        {
            error = id + " must be an object of bass, middle and treble";
            return std::nullopt;
        }

        for (int r = 0; r < lab::numRegisters; ++r)
        {
            const auto reg = toJuceString (lab::registerIds[static_cast<std::size_t> (r)]);
            float value = lab::neutralValue;
            if (! readValue (entry[juce::Identifier (reg)], id + "." + reg, value))
                return std::nullopt;
            settings.set (c.control, static_cast<lab::Register> (r), value);
        }
    }

    return settings;
}
} // namespace forte::bridge
