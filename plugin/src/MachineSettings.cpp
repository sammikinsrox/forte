#include "MachineSettings.h"

namespace forte
{
juce::File MachineSettings::file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Forte")
        .getChildFile ("settings.json");
}

MachineSettings MachineSettings::load()
{
    MachineSettings settings;
    const auto parsed = juce::JSON::parse (file());
    if (auto* object = parsed.getDynamicObject())
    {
        settings.darkTheme = object->getProperty ("theme").toString() == "dark";
        settings.invertSustain = static_cast<bool> (object->getProperty ("invertSustain"));
        settings.invertSoft = static_cast<bool> (object->getProperty ("invertSoft"));
    }
    return settings;
}

void MachineSettings::save() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("theme", darkTheme ? "dark" : "light");
    object->setProperty ("invertSustain", invertSustain);
    object->setProperty ("invertSoft", invertSoft);

    const auto target = file();
    if (target.getParentDirectory().createDirectory().wasOk())
        target.replaceWithText (juce::JSON::toString (juce::var (object)));
}
} // namespace forte
