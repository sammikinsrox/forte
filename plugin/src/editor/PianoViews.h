#pragma once

#include "Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace forte
{
class PluginProcessor;
}

namespace forte::ui
{
/** The pedals as both views show them (SPEC §6.3): the Soft toggle and the Sustain slider,
    mirroring MIDI. */
class Pedals final
{
public:
    Pedals (PluginProcessor& processor, juce::Component& parent, const juce::String& softLabel);
    void tick();

    juce::TextButton soft;
    SustainSlider sustain;

private:
    PluginProcessor& processor;
};

/** The simple view (SPEC §6.4): three dials, the Volume fader, the pedals and the keyboard. */
class SimpleView final : public juce::Component
{
public:
    explicit SimpleView (PluginProcessor& processor);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    PluginProcessor& processor;
    std::vector<std::unique_ptr<Knob>> dials;
    Fader volume;
    Pedals pedals;
    PianoKeyboard keyboard;
};

/** Every note's tuning (SPEC §6.2): cents from equal temperament across the keyboard, as the
    tuner sets them at the current A4 and Stretch. */
class TuningCurve final : public juce::Component
{
public:
    explicit TuningCurve (PluginProcessor& processor);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void tick();

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    [[nodiscard]] float xOf (int key) const;

    PluginProcessor& processor;
    double a4 = 440.0, stretch = 50.0;
    int hoverKey = -1;
};

/** The advanced view (SPEC §6.4): the tuning curve and keyboard, and every control in five
    groups. */
class AdvancedView final : public juce::Component
{
public:
    explicit AdvancedView (PluginProcessor& processor);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    struct Group
    {
        juce::String title;
        int y;
    };

    PluginProcessor& processor;
    TuningCurve curve;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Group> groups;
    Pedals pedals;
    PianoKeyboard keyboard;
};
} // namespace forte::ui
