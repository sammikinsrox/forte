// PROTOTYPE (throwaway): variant D rendered as a web page inside a WebView2 window.
#pragma once
#include "Processor.h"

class WebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WebEditor (ProtoProcessor&);
    ~WebEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xfff4f4f1)); }

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    juce::WebBrowserComponent::Options makeOptions();

    ProtoProcessor& proc;
    std::vector<std::unique_ptr<juce::WebSliderRelay>> sliderRelays;
    juce::WebToggleButtonRelay softRelay { "soft" };
    juce::WebComboBoxRelay perspectiveRelay { "perspective" };
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> softAttachment;
    std::unique_ptr<juce::WebComboBoxParameterAttachment> perspectiveAttachment;
};
