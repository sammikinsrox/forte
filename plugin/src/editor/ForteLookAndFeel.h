#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace forte::ui
{
/** The editor's colours (SPEC §6.2): Light is white lacquer with ebony knobs, Dark ebony lacquer
    with ivory knobs. The felt strip and the keyboard are the same in both. */
struct Theme
{
    juce::Colour bg, bg2, panel, ink, muted, line, accent, knobHi, knobLo;

    static Theme light();
    static Theme dark();
};

inline const juce::Colour feltRed { 0xff9e1b32 };

/** The fixed size the editor is laid out at; it's scaled as a whole (SPEC §6.1). */
inline constexpr int baseWidth = 1100;
inline constexpr int baseHeight = 690;

/** Forte's native look (SPEC §6.1): rotary dials and faders, pill and segment buttons, Jost
    for text and Italiana for the wordmark, both embedded. */
class ForteLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ForteLookAndFeel();

    void setTheme (const Theme& theme);
    [[nodiscard]] const Theme& theme() const noexcept { return t; }
    [[nodiscard]] bool isDark() const noexcept { return dark; }
    void setDark (bool shouldBeDark);

    /** Jost at a CSS-style size (its em size), regular or medium. */
    [[nodiscard]] static juce::Font jost (float size, bool medium = false);
    [[nodiscard]] static juce::Font italiana (float height, float kerning = 0.3f);

    // Slider properties the drawing reads: "default" (the value double-click returns to, as
    // 0–1 of the range) marks the ring and starts the value arc there.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;

    // Button property "seg": a segment of a segmented control (no outline of its own).
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return jost (13.0f, true); }
    juce::Font getLabelFont (juce::Label& label) override { return label.getFont(); }
    juce::Font getPopupMenuFont() override { return jost (14.0f); }

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.75f;

private:
    Theme t;
    bool dark = false;
};

/** A rounded panel behind a group of controls. */
void paintPanel (juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme);
} // namespace forte::ui
