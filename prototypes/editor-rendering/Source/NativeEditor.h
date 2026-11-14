// PROTOTYPE (throwaway): variant D drawn with JUCE's native graphics (Direct2D on Windows).
#pragma once
#include "Processor.h"

struct Theme
{
    juce::Colour bg, bg2, panel, ink, muted, line, accent, knobHi, knobLo;
    static Theme light() { return { juce::Colour (0xfff4f4f1), juce::Colour (0xffeaeae6), juce::Colour (0xfffbfbf9), juce::Colour (0xff1e1c1b), juce::Colour (0xff6b6763), juce::Colour (0xffd6d6d1), juce::Colour (0xffb3203a), juce::Colour (0xff3a3532), juce::Colour (0xff141211) }; }
    static Theme dark()  { return { juce::Colour (0xff1e1c1b), juce::Colour (0xff161514), juce::Colour (0xff262423), juce::Colour (0xffedeae4), juce::Colour (0xff9d978f), juce::Colour (0xff3d3936), juce::Colour (0xffe5566c), juce::Colour (0xfffffdf8), juce::Colour (0xffd8d2c5) }; }
};

class ForteLnf : public juce::LookAndFeel_V4
{
public:
    Theme t = Theme::light();
    juce::Typeface::Ptr italiana, jost;
    ForteLnf();
    juce::Font jostFont (float h, bool medium = false) const;
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return jostFont (13.0f, true); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
};

class HeatKeyboard : public juce::MidiKeyboardComponent
{
public:
    HeatKeyboard (ProtoProcessor& p, ForteLnf& l);
    void drawWhiteNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool, juce::Colour, juce::Colour) override;
    void drawBlackNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool, juce::Colour) override;
    void resized() override;
private:
    float heat (int m) const { return std::sqrt (proc.viz.energy[(size_t) juce::jlimit (0, 87, m - 21)].load()); }
    ProtoProcessor& proc; ForteLnf& lnf;
};

// A knob (or fader) with its caption and a click-to-type whole-number value.
class Control : public juce::Component
{
public:
    enum Kind { bigDial, smallKnob, vFader, hSlider };
    Control (ProtoProcessor&, ForteLnf&, const char* paramId, const juce::String& caption, Kind);
    void resized() override;
    juce::Slider slider;
    juce::Label caption, value, unit;
private:
    Kind kind;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class NativeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NativeEditor (ProtoProcessor&);
    ~NativeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Content;
    void timerCallback() override;
    void rebuild();
    ProtoProcessor& proc;
    ForteLnf lnf;
    std::unique_ptr<Content> content;
    bool firstPaintLogged = false;
};
