#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace forte
{
class PluginProcessor;

/** Placeholder editor: the wordmark on lacquer above the felt strip. The real editor
    (SPEC §6) replaces this.
*/
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor& processor);

    void paint (juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace forte
