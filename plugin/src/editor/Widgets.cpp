#include "Widgets.h"

namespace forte::ui
{
ForteLookAndFeel& forteLook (juce::Component& component)
{
    return static_cast<ForteLookAndFeel&> (component.getLookAndFeel());
}

juce::String noteName (int midiNote)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12 - 1);
}

//==============================================================================
namespace
{
/** The field's editor: ↑/↓ step the value while it's open. */
class StepEditor final : public juce::TextEditor
{
public:
    std::function<void (int)> onStep;

    bool keyPressed (const juce::KeyPress& key) override
    {
        const int direction = key.isKeyCode (juce::KeyPress::upKey) ? 1 : key.isKeyCode (juce::KeyPress::downKey) ? -1 : 0;
        if (direction != 0 && onStep)
        {
            onStep (key.getModifiers().isShiftDown() ? 10 * direction : direction);
            return true;
        }
        return juce::TextEditor::keyPressed (key);
    }
};

/** The text a parameter shows for a value in its own units, with its unit if it has one. */
juce::String parameterText (const juce::RangedAudioParameter& parameter, double value)
{
    auto text = parameter.getText (parameter.convertTo0to1 (static_cast<float> (value)), 0);
    const auto unit = parameter.getLabel();
    if (unit.isNotEmpty() && ! text.endsWith (unit))
        text << " " << unit;
    return text;
}

void wireField (ValueField& field, juce::Slider& slider, juce::RangedAudioParameter& parameter)
{
    field.text = [&slider, &parameter] { return parameterText (parameter, slider.getValue()); };
    field.value = [&slider] { return slider.getValue(); };
    field.set = [&slider] (double v) { slider.setValue (v, juce::sendNotificationSync); };
    field.parse = [&parameter] (const juce::String& s)
    {
        return static_cast<double> (parameter.convertFrom0to1 (parameter.getValueForText (s.upToFirstOccurrenceOf (" ", false, false))));
    };
    field.step = parameter.getNormalisableRange().interval >= 1.0f ? 1.0 : 0.5;
}

void setUpSlider (juce::Slider& slider, juce::RangedAudioParameter& parameter)
{
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
    slider.getProperties().set ("default", parameter.getDefaultValue());
    slider.setScrollWheelEnabled (true);
}
} // namespace

ValueField::ValueField()
{
    setEditable (true, false, true);
    setJustificationType (juce::Justification::centred);
    setFont (ForteLookAndFeel::jost (14.0f, true));
    setMouseCursor (juce::MouseCursor::IBeamCursor);
    setTooltip ("Click to type a value. Up and down step it.");
}

void ValueField::refresh()
{
    if (! isBeingEdited() && text)
        setText (text(), juce::dontSendNotification);
}

juce::TextEditor* ValueField::createEditorComponent()
{
    auto* textEditor = new StepEditor();
    textEditor->applyFontToAllText (getLookAndFeel().getLabelFont (*this));
    copyAllExplicitColoursTo (*textEditor);
    textEditor->setJustification (juce::Justification::centred);
    textEditor->setInputRestrictions (8, "0123456789.-+OoFf ");
    textEditor->onStep = [this, textEditor] (int steps)
    {
        if (! value || ! set)
            return;
        set (value() + steps * step);
        textEditor->setText (text ? text().upToFirstOccurrenceOf (" ", false, false) : juce::String(), false);
        textEditor->selectAll();
    };
    return textEditor;
}

void ValueField::editorShown (juce::TextEditor* textEditor)
{
    // Edit the bare number, without its unit.
    textEditor->setText (getText().upToFirstOccurrenceOf (" ", false, false), false);
    textEditor->selectAll();
}

void ValueField::textWasEdited()
{
    if (set && parse)
        set (parse (getText()));
    refresh();
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& captionText, Size s)
    : parameter (*state.getParameter (parameterId)), size (s)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (ForteLookAndFeel::rotaryStart, ForteLookAndFeel::rotaryEnd, true);
    slider.setMouseDragSensitivity (s == Size::dial ? 260 : 200);
    setUpSlider (slider, parameter);
    slider.setTooltip (captionText + ": drag or scroll. Double-click for the default.");
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);

    caption.setText (captionText, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setInterceptsMouseClicks (false, false);
    caption.setFont (ForteLookAndFeel::jost (s == Size::dial ? 17.0f : 12.5f, s == Size::dial));
    addAndMakeVisible (caption);

    wireField (value, slider, parameter);
    addAndMakeVisible (value);
    slider.onValueChange = [this] { value.refresh(); };
    value.refresh();
}

void Knob::lookAndFeelChanged()
{
    caption.setColour (juce::Label::textColourId, forteLook (*this).theme().ink);
}

void Knob::resized()
{
    auto b = getLocalBounds();
    if (size == Size::dial)
    {
        slider.setBounds (b.removeFromTop (b.getWidth()));
        b.removeFromTop (8);
        caption.setBounds (b.removeFromTop (24));
    }
    else
    {
        slider.setBounds (b.removeFromTop (54).withSizeKeepingCentre (54, 54));
        caption.setBounds (b.removeFromTop (18));
    }
    value.setBounds (b.removeFromTop (22).withSizeKeepingCentre (juce::jmin (b.getWidth(), 76), 22));
}

//==============================================================================
Fader::Fader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& captionText)
    : parameter (*state.getParameter (parameterId))
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    setUpSlider (slider, parameter);
    slider.setTooltip (captionText + ": drag or scroll. Double-click for the default.");
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);

    caption.setText (captionText, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setInterceptsMouseClicks (false, false);
    caption.setFont (ForteLookAndFeel::jost (12.5f));
    addAndMakeVisible (caption);

    wireField (value, slider, parameter);
    addAndMakeVisible (value);
    slider.onValueChange = [this] { value.refresh(); };
    value.refresh();
}

void Fader::lookAndFeelChanged()
{
    caption.setColour (juce::Label::textColourId, forteLook (*this).theme().ink);
}

void Fader::resized()
{
    auto b = getLocalBounds();
    slider.setBounds (b.removeFromTop (170).withSizeKeepingCentre (44, 170));
    b.removeFromTop (6);
    caption.setBounds (b.removeFromTop (18));
    value.setBounds (b.removeFromTop (22).withSizeKeepingCentre (76, 22));
}

//==============================================================================
Segmented::Segmented (const juce::StringArray& labels)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto button = std::make_unique<juce::TextButton> (labels[i]);
        button->getProperties().set ("seg", true);
        button->setClickingTogglesState (false);
        button->onClick = [this, i]
        {
            setSelected (i);
            if (onChange)
                onChange (i);
        };
        addAndMakeVisible (*button);
        buttons.push_back (std::move (button));
    }
    setSelected (0);
}

void Segmented::setTooltips (const juce::StringArray& tips)
{
    for (std::size_t i = 0; i < buttons.size() && static_cast<int> (i) < tips.size(); ++i)
        buttons[i]->setTooltip (tips[static_cast<int> (i)]);
}

void Segmented::setSelected (int index)
{
    selected = index;
    for (std::size_t i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (static_cast<int> (i) == index, juce::dontSendNotification);
}

void Segmented::paint (juce::Graphics& g)
{
    g.setColour (forteLook (*this).theme().line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), static_cast<float> (getHeight()) / 2.0f, 1.0f);
}

void Segmented::resized()
{
    // Each segment as wide as its text needs, spread over the whole width.
    auto b = getLocalBounds().reduced (2);
    std::vector<float> widths;
    float total = 0.0f;
    for (auto& button : buttons)
    {
        const float w = juce::GlyphArrangement::getStringWidth (ForteLookAndFeel::jost (12.0f, true), button->getButtonText()) + 20.0f;
        widths.push_back (w);
        total += w;
    }

    const float scale = static_cast<float> (b.getWidth()) / juce::jmax (1.0f, total);
    float x = static_cast<float> (b.getX());
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        const float w = widths[i] * scale;
        buttons[i]->setBounds (juce::roundToInt (x), b.getY(), juce::roundToInt (x + w) - juce::roundToInt (x), b.getHeight());
        x += w;
    }
}

//==============================================================================
SustainSlider::SustainSlider()
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 100.0, 1.0);
    slider.setTooltip ("Sustain pedal: Up, Half or Down. Follows the MIDI pedal; the latest wins.");
    slider.onValueChange = [this]
    {
        if (slider.isMouseButtonDown() && onMove)
            onMove (juce::roundToInt (slider.getValue() * 127.0 / 100.0));
    };
    slider.onDragEnd = slider.onValueChange;
    addAndMakeVisible (slider);
}

void SustainSlider::showPedal (int midiValue)
{
    if (! slider.isMouseButtonDown())
        slider.setValue (juce::roundToInt (midiValue * 100.0 / 127.0), juce::dontSendNotification);
}

void SustainSlider::paint (juce::Graphics& g)
{
    const auto& t = forteLook (*this).theme();
    auto b = getLocalBounds();
    g.setColour (t.ink);
    g.setFont (ForteLookAndFeel::jost (12.5f));
    g.drawText ("Sustain", b.removeFromLeft (64), juce::Justification::centredRight);

    const auto track = slider.getBounds();
    g.setColour (t.muted);
    g.setFont (ForteLookAndFeel::jost (11.0f));
    const int labelY = track.getBottom() - 4;
    g.drawText ("Up", track.getX(), labelY, 40, 14, juce::Justification::left);
    g.drawText ("Half", track.getCentreX() - 20, labelY, 40, 14, juce::Justification::centred);
    g.drawText ("Down", track.getRight() - 40, labelY, 40, 14, juce::Justification::right);
}

void SustainSlider::resized()
{
    auto b = getLocalBounds();
    b.removeFromLeft (76);
    slider.setBounds (b.removeFromTop (getHeight() - 12));
}

//==============================================================================
PianoKeyboard::PianoKeyboard (juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent (state, horizontalKeyboard)
{
    setAvailableRange (21, 108);
    setScrollButtonsVisible (false);
    setVelocity (0.7f, true);
    setBlackNoteLengthProportion (0.62f);
    setColour (shadowColourId, juce::Colours::transparentBlack);
    setColour (keySeparatorLineColourId, juce::Colour (0xffc9c8c2));
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void PianoKeyboard::resized()
{
    setKeyWidth (static_cast<float> (getWidth()) / 52.0f);
    juce::MidiKeyboardComponent::resized();
    setLowestVisibleKey (21);
}

void PianoKeyboard::drawWhiteNote (int, juce::Graphics& g, juce::Rectangle<float> a, bool isDown, bool isOver, juce::Colour, juce::Colour)
{
    const auto accent = Theme::light().accent;
    g.setColour (juce::Colour (0xfffbfaf6).interpolatedWith (accent, isDown ? 0.45f : isOver ? 0.08f : 0.0f));
    g.fillRect (a);
    g.setColour (juce::Colour (0xffe4e3dd));
    g.fillRect (a.removeFromBottom (juce::jmin (6.0f, a.getHeight() * 0.06f)));
    g.setColour (juce::Colour (0xffc9c8c2));
    g.fillRect (a.withX (a.getRight() - 1.0f).withWidth (1.0f).withBottom (a.getBottom() + 6.0f));
}

void PianoKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> a, bool isDown, bool isOver, juce::Colour)
{
    const auto accent = Theme::light().accent;
    g.setColour (juce::Colour (0xff171413).interpolatedWith (accent, isDown ? 0.75f : isOver ? 0.15f : 0.0f));
    g.fillRoundedRectangle (a, 2.0f);
    g.setColour (juce::Colour (0xff3a3532));
    g.fillRect (a.removeFromBottom (juce::jmin (4.0f, a.getHeight() * 0.06f)));
}

//==============================================================================
GearButton::GearButton()
    : juce::Button ("Settings")
{
    setTooltip ("Settings");
}

void GearButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto& t = forteLook (*this).theme();
    const auto c = getLocalBounds().toFloat().getCentre();
    const float outer = static_cast<float> (juce::jmin (getWidth(), getHeight())) * 0.36f;
    const float inner = outer * 0.74f;

    juce::Path gear;
    constexpr int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * static_cast<float> (i) / (teeth * 2);
        const float a1 = juce::MathConstants<float>::twoPi * static_cast<float> (i + 1) / (teeth * 2);
        const float r = i % 2 == 0 ? outer : inner;
        const juce::Point<float> p0 (c.x + r * std::sin (a0), c.y - r * std::cos (a0));
        const juce::Point<float> p1 (c.x + r * std::sin (a1), c.y - r * std::cos (a1));
        if (i == 0)
            gear.startNewSubPath (p0);
        else
            gear.lineTo (p0);
        gear.lineTo (p1);
    }
    gear.closeSubPath();
    gear.addEllipse (c.x - outer * 0.36f, c.y - outer * 0.36f, outer * 0.72f, outer * 0.72f);
    gear.setUsingNonZeroWinding (false);

    g.setColour (over ? t.accent : t.ink);
    g.fillPath (gear);
}
} // namespace forte::ui
