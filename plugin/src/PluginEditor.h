#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "editor/ForteLookAndFeel.h"

#include <memory>

namespace forte
{
/** Forte's editor (SPEC §6): native JUCE drawing at a fixed 1100 × 690 layout, scaled as a whole
    to the window with its aspect ratio kept. Three views share the header: Simple, Advanced and
    the voicing lab. The view is saved with the project; the theme and the pedal inverts belong
    to the computer. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showView (PluginProcessor::View view);

private:
    class Content;

    void timerCallback() override;

    PluginProcessor& processor;
    PluginProcessor::View shown = PluginProcessor::View::simple;
    ui::ForteLookAndFeel look;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltips;
};
} // namespace forte
