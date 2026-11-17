#pragma once

#include <juce_core/juce_core.h>

#include "forte/engine/Engine.h"
#include "forte/engine/VoicingLab.h"

namespace forte
{
/** Export voicing (SPEC §6.5): the lab's values as the voicing file that forte-render --voicing
    and tools/fitting read (forte::bridge::parseVoicing). On top of the values ("controls") it
    records, for people reading it, what each one means in physical terms at the register
    anchors ("physical"; a control without registers at C4), the ranges, and the commit the
    build came from ("builtFrom"), which tells the fold which committed voicing the 50s meant. */
[[nodiscard]] juce::String makeVoicingFile (const Engine& engine, const lab::Settings& settings);

/** Documents/Forte/voicing.json. */
[[nodiscard]] juce::File defaultVoicingFile();

/** Writes the voicing file to target, first keeping any file already there as
    <name>.previous.json, so one stray export can't lose a voicing. Not realtime. */
juce::Result exportVoicing (const Engine& engine, const lab::Settings& settings, const juce::File& target);
} // namespace forte
