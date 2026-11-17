#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "ForteLookAndFeel.h"

#include <functional>
#include <memory>
#include <vector>

namespace forte::ui
{
/** A number that can be typed (SPEC §6.3): click to edit, Enter commits, Esc cancels, ↑/↓
    step while editing (Shift: ×10). It shows text(), and set() gets what was typed. */
class ValueField final : public juce::Label
{
public:
    ValueField();

    std::function<juce::String()> text;
    std::function<double()> value;           // for ↑/↓
    std::function<void (double)> set;        // a typed or stepped value
    std::function<double (const juce::String&)> parse = [] (const juce::String& s) { return s.getDoubleValue(); };
    double step = 1.0;

    void refresh();

private:
    void editorShown (juce::TextEditor*) override;
    void textWasEdited() override;
    juce::TextEditor* createEditorComponent() override;
};

/** A dial or knob on a parameter, with its caption and typed value (SPEC §6.3): drag, wheel,
    double-click for the default, or click the value and type. */
class Knob final : public juce::Component
{
public:
    enum class Size { dial, knob };

    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& caption, Size size);

    void resized() override;
    void lookAndFeelChanged() override;

    juce::Slider slider;

private:
    juce::RangedAudioParameter& parameter;
    Size size;
    juce::Label caption;
    ValueField value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** The Volume fader of the simple view. */
class Fader final : public juce::Component
{
public:
    Fader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& caption);
    void resized() override;
    void lookAndFeelChanged() override;

    juce::Slider slider;

private:
    juce::RangedAudioParameter& parameter;
    juce::Label caption;
    ValueField value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A row of pill segments, one of them on. */
class Segmented final : public juce::Component
{
public:
    explicit Segmented (const juce::StringArray& labels);

    std::function<void (int)> onChange;
    void setSelected (int index);
    [[nodiscard]] int getSelected() const noexcept { return selected; }
    void setTooltips (const juce::StringArray& tips);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    int selected = 0;
};

/** The Sustain slider (SPEC §6.3): the raw pedal, Up (0) to Down (100), live from MIDI. */
class SustainSlider final : public juce::Component
{
public:
    std::function<void (int)> onMove;  // a pedal value 0–127 the user set

    SustainSlider();
    void showPedal (int midiValue);    // the live pedal, unless it's being dragged
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
};

/** The playable 88-key keyboard (SPEC §6.2). */
class PianoKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit PianoKeyboard (juce::MidiKeyboardState& state);
    void resized() override;

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour) override;
};

/** A plain icon button drawn in the theme's ink: the Settings gear. */
class GearButton final : public juce::Button
{
public:
    GearButton();
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** "C4", "F#6", … */
[[nodiscard]] juce::String noteName (int midiNote);

/** The look-and-feel a component draws with, as Forte's. */
[[nodiscard]] ForteLookAndFeel& forteLook (juce::Component& component);
} // namespace forte::ui
