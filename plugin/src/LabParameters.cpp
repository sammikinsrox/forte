#include "LabParameters.h"

namespace forte
{
namespace
{
juce::String toString (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

constexpr std::array<std::string_view, 4> groupIds { "hammer", "strings", "board", "output" };
constexpr std::array<std::string_view, 4> groupNames { "Hammer", "Strings", "Board", "Output" };

/** The registers a control has: three for a per-register one, otherwise just the first slot. */
int registersOf (const lab::ControlInfo& c) noexcept
{
    return c.perRegister ? lab::numRegisters : 1;
}
} // namespace

juce::String LabParameters::parameterId (lab::Control control, lab::Register reg)
{
    const auto& c = lab::info (control);
    auto id = "lab." + toString (c.id);
    if (c.perRegister)
        id << "." << toString (lab::registerIds[static_cast<std::size_t> (reg)]);
    return id;
}

std::unique_ptr<juce::AudioProcessorParameterGroup> LabParameters::createGroup()
{
    auto labGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("lab", "Lab", " | ");

    for (std::size_t g = 0; g < groupIds.size(); ++g)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("lab." + toString (groupIds[g]), toString (groupNames[g]), " | ");

        for (const auto& c : lab::controls)
        {
            if (static_cast<std::size_t> (c.group) != g)
                continue;

            for (int r = 0; r < registersOf (c); ++r)
            {
                const auto reg = static_cast<lab::Register> (r);
                auto name = toString (c.name);
                if (c.perRegister)
                    name << " (" << toString (lab::registerNames[static_cast<std::size_t> (r)]) << ")";

                group->addChild (std::make_unique<juce::AudioParameterInt> (
                    juce::ParameterID { parameterId (c.control, reg), 1 }, name,
                    static_cast<int> (lab::minimumValue), static_cast<int> (lab::maximumValue),
                    static_cast<int> (c.resting)));
            }
        }

        labGroup->addChild (std::move (group));
    }

    labGroup->addChild (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { exportParameterId, 1 }, "Export voicing", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    return labGroup;
}

void LabParameters::attach (juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& c : lab::controls)
        for (int r = 0; r < registersOf (c); ++r)
        {
            auto* value = apvts.getRawParameterValue (parameterId (c.control, static_cast<lab::Register> (r)));
            jassert (value != nullptr);
            values[static_cast<std::size_t> (c.control)][static_cast<std::size_t> (r)] = value;
        }

    exportVoicing = apvts.getParameter (exportParameterId);
    jassert (exportVoicing != nullptr);
}

lab::Settings LabParameters::read() const noexcept
{
    lab::Settings settings;

    for (const auto& c : lab::controls)
        for (int r = 0; r < registersOf (c); ++r)
            if (const auto* value = values[static_cast<std::size_t> (c.control)][static_cast<std::size_t> (r)])
                settings.set (c.control, static_cast<lab::Register> (r), value->load (std::memory_order_relaxed));

    return settings;
}
} // namespace forte
