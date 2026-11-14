#include "PluginEditor.h"

#include "PluginProcessor.h"

#include "forte/Version.h"

namespace forte
{
PluginEditor::PluginEditor (PluginProcessor& processor)
    : juce::AudioProcessorEditor (processor)
{
    setSize (720, 300);
}

void PluginEditor::paint (juce::Graphics& g)
{
    const auto lacquer = juce::Colour (0xff111111);
    const auto ivory = juce::Colour (0xfff2ede3);
    const auto felt = juce::Colour (0xff9e1b32);  // SPEC §6.2

    auto area = getLocalBounds();
    g.fillAll (lacquer);

    g.setColour (felt);
    g.fillRect (area.removeFromBottom (12));

    g.setColour (ivory);
    g.setFont (juce::FontOptions (44.0f));
    g.drawText ("FORTE", area.reduced (24), juce::Justification::centred);

    g.setColour (ivory.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String ("v") + versionString + "  -  engine skeleton (silent)",
                area.reduced (16), juce::Justification::bottomRight);
}
} // namespace forte
