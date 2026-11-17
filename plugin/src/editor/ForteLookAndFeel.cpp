#include "ForteLookAndFeel.h"

#include "ForteAssets.h"

namespace forte::ui
{
Theme Theme::light()
{
    return { juce::Colour (0xfff4f4f1), juce::Colour (0xffeaeae6), juce::Colour (0xfffbfbf9), juce::Colour (0xff1e1c1b),
             juce::Colour (0xff6b6763), juce::Colour (0xffd6d6d1), juce::Colour (0xffb3203a), juce::Colour (0xff3a3532),
             juce::Colour (0xff141211) };
}

Theme Theme::dark()
{
    return { juce::Colour (0xff1e1c1b), juce::Colour (0xff161514), juce::Colour (0xff262423), juce::Colour (0xffedeae4),
             juce::Colour (0xff9d978f), juce::Colour (0xff3d3936), juce::Colour (0xffe5566c), juce::Colour (0xfffffdf8),
             juce::Colour (0xffd8d2c5) };
}

namespace
{
struct Typefaces
{
    juce::Typeface::Ptr italiana, jostRegular, jostMedium;

    Typefaces()
    {
        italiana = juce::Typeface::createSystemTypefaceFor (ForteAssets::ItalianaRegular_ttf, static_cast<size_t> (ForteAssets::ItalianaRegular_ttfSize));
        const auto jost = juce::Typeface::createSystemTypefaceFor (ForteAssets::Jost_ttf, static_cast<size_t> (ForteAssets::Jost_ttfSize));

        // Jost is a variable font whose default instance is Thin: clone the 400 and 500 weights.
        const juce::FontVariableSetting regular[] { { juce::FontFeatureTag ("wght"), 400.0f } };
        const juce::FontVariableSetting medium[] { { juce::FontFeatureTag ("wght"), 500.0f } };
        jostRegular = jost->cloneWithVariableSettings (regular);
        jostMedium = jost->cloneWithVariableSettings (medium);
    }

    static const Typefaces& get()
    {
        static const Typefaces faces;
        return faces;
    }
};
} // namespace

juce::Font ForteLookAndFeel::jost (float size, bool medium)
{
    const auto& faces = Typefaces::get();
    // withPointHeight matches a CSS font-size (the em size).
    return juce::Font (juce::FontOptions (medium ? faces.jostMedium : faces.jostRegular).withPointHeight (size));
}

juce::Font ForteLookAndFeel::italiana (float height, float kerning)
{
    return juce::Font (juce::FontOptions (Typefaces::get().italiana).withHeight (height).withKerningFactor (kerning));
}

ForteLookAndFeel::ForteLookAndFeel()
{
    setTheme (Theme::light());
}

void ForteLookAndFeel::setDark (bool shouldBeDark)
{
    dark = shouldBeDark;
    setTheme (dark ? Theme::dark() : Theme::light());
}

void ForteLookAndFeel::setTheme (const Theme& theme)
{
    t = theme;

    setColour (juce::Label::textColourId, t.ink);
    setColour (juce::Label::textWhenEditingColourId, t.ink);
    setColour (juce::Label::outlineWhenEditingColourId, t.accent);
    setColour (juce::Label::backgroundWhenEditingColourId, t.panel);
    setColour (juce::TextEditor::textColourId, t.ink);
    setColour (juce::TextEditor::backgroundColourId, t.panel);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, t.ink);
    setColour (juce::TextEditor::outlineColourId, t.line);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::CaretComponent::caretColourId, t.accent);
    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.ink);
    setColour (juce::PopupMenu::highlightedTextColourId, t.bg);
    setColour (juce::TooltipWindow::backgroundColourId, t.ink);
    setColour (juce::TooltipWindow::textColourId, t.bg);
    setColour (juce::TooltipWindow::outlineColourId, t.ink);
    setColour (juce::ResizableWindow::backgroundColourId, t.bg);
}

void ForteLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float, float, juce::Slider& slider)
{
    const auto centre = juce::Rectangle<int> (x, y, width, height).toFloat().getCentre();
    const float ringRadius = static_cast<float> (juce::jmin (width, height)) * 0.5f - 2.0f;
    const float radius = ringRadius - juce::jmax (5.0f, ringRadius * 0.16f);
    const auto angleAt = [] (float p) { return rotaryStart + p * (rotaryEnd - rotaryStart); };
    const float angle = angleAt (position);

    const float stroke = ringRadius > 50.0f ? 3.0f : 2.5f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotaryStart, rotaryEnd, true);
    g.setColour (t.line);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The value's arc runs from the control's default (the real piano) to where it is.
    const auto defaultProperty = slider.getProperties()["default"];
    const float from = defaultProperty.isVoid() ? 0.0f : static_cast<float> (static_cast<double> (defaultProperty));
    if (std::abs (position - from) > 1e-4f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, angleAt (juce::jmin (from, position)),
                             angleAt (juce::jmax (from, position)), true);
        g.setColour (t.accent);
        g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! defaultProperty.isVoid())
    {
        const float a = angleAt (from);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        g.setColour (t.muted);
        g.drawLine ({ centre + dir * (ringRadius + 3.0f), centre + dir * (ringRadius + 7.0f) }, 1.5f);
    }

    juce::Path knob;
    knob.addEllipse (centre.x - radius, centre.y - radius, 2.0f * radius, 2.0f * radius);
    juce::DropShadow (juce::Colours::black.withAlpha (dark ? 0.5f : 0.22f), static_cast<int> (radius * 0.3f),
                      { 0, static_cast<int> (radius * 0.12f) })
        .drawForPath (g, knob);
    g.setGradientFill (juce::ColourGradient (t.knobHi, centre.x - radius * 0.2f, centre.y - radius * 0.4f,
                                             t.knobLo, centre.x + radius * 0.7f, centre.y + radius * 0.7f, true));
    g.fillPath (knob);

    if (slider.isMouseOverOrDragging())
    {
        g.setColour (t.accent.withAlpha (0.35f));
        g.drawEllipse (centre.x - radius, centre.y - radius, 2.0f * radius, 2.0f * radius, 1.0f);
    }

    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
    g.setColour (t.accent);
    g.drawLine ({ centre + dir * (radius * 0.24f), centre + dir * (radius * 0.84f) }, radius > 40.0f ? 3.0f : 2.0f);
}

void ForteLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool over = slider.isMouseOverOrDragging();

    if (style == juce::Slider::LinearVertical)
    {
        const float cx = static_cast<float> (x) + static_cast<float> (width) * 0.5f;
        g.setColour (t.line);
        g.fillRoundedRectangle (cx - 2.0f, static_cast<float> (y), 4.0f, static_cast<float> (height), 2.0f);
        g.setColour (t.ink);
        g.fillRoundedRectangle (cx - 2.0f, position, 4.0f, static_cast<float> (y + height) - position, 2.0f);
        g.setColour (over ? t.accent : t.ink);
        g.fillRoundedRectangle (cx - 16.0f, position - 9.0f, 32.0f, 18.0f, 3.0f);
        g.setColour (t.bg);
        g.drawHorizontalLine (juce::roundToInt (position), cx - 9.0f, cx + 9.0f);
        return;
    }

    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    g.setColour (t.line);
    g.fillRect (static_cast<float> (x), cy - 1.0f, static_cast<float> (width), 2.0f);
    g.setColour (t.ink);
    g.fillRect (static_cast<float> (x), cy - 1.0f, position - static_cast<float> (x), 2.0f);
    g.setColour (over ? t.accent : t.ink);
    g.fillEllipse (position - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void ForteLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool)
{
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool segment = button.getProperties()["seg"];

    if (button.getToggleState())
    {
        g.setColour (t.ink);
        g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    }
    else if (segment)
    {
        if (over)
        {
            g.setColour (t.line.withAlpha (0.6f));
            g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
        }
    }
    else
    {
        if (over && button.isEnabled())
        {
            g.setColour (t.ink.withAlpha (0.08f));
            g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
        }
        g.setColour (button.isEnabled() ? t.ink : t.line);
        g.drawRoundedRectangle (r, r.getHeight() / 2.0f, 1.0f);
    }
}

void ForteLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool segment = button.getProperties()["seg"];
    const bool filled = button.getToggleState();
    g.setColour (filled ? t.bg : ! button.isEnabled() ? t.line : segment ? t.muted : t.ink);
    g.setFont (jost (segment ? 12.0f : 13.0f, true));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void ForteLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (t.panel);
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height), 3.0f);
}

void ForteLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (t.accent);
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f, 3.0f, 1.0f);
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (area.reduced (0.5f), 10.0f);
    g.setColour (theme.line);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);
}
} // namespace forte::ui
