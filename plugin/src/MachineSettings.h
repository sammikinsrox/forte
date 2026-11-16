#pragma once

#include <juce_core/juce_core.h>

namespace forte
{
/** The settings that belong to the computer, not the project (SPEC §7): the editor's theme and
    the pedal inverts, in %APPDATA%\Forte\settings.json. Every Forte instance reads them when it
    starts; a change is written at once. Message thread only. */
struct MachineSettings
{
    bool darkTheme = false;
    bool invertSustain = false;
    bool invertSoft = false;

    [[nodiscard]] static juce::File file();
    [[nodiscard]] static MachineSettings load();
    void save() const;
};
} // namespace forte
