#include "PianoViews.h"

#include "../PluginProcessor.h"
#include "../SoundParameters.h"

#include <cmath>

namespace forte::ui
{
using Ids = SoundParameters::Ids;

//==============================================================================
Pedals::Pedals (PluginProcessor& p, juce::Component& parent, const juce::String& softLabel)
    : processor (p)
{
    soft.setButtonText (softLabel);
    soft.setClickingTogglesState (false);
    soft.setTooltip ("Soft pedal: on once the pedal is past halfway. Click to press or release it.");
    soft.onClick = [this] { processor.sendSoft (processor.getSoft() >= 64 ? 0 : 127); };
    sustain.onMove = [this] (int value) { processor.sendSustain (value); };
    parent.addAndMakeVisible (soft);
    parent.addAndMakeVisible (sustain);
    tick();
}

void Pedals::tick()
{
    soft.setToggleState (processor.getSoft() >= 64, juce::dontSendNotification);
    sustain.showPedal (processor.getSustain());
}

//==============================================================================
SimpleView::SimpleView (PluginProcessor& p)
    : processor (p),
      volume (p.getState(), Ids::volume, "Volume"),
      pedals (p, *this, "Soft"),
      keyboard (p.keyboardState)
{
    const std::array<std::pair<const char*, const char*>, 3> controls { { { Ids::hardness, "Hardness" },
                                                                          { Ids::brightness, "Brightness" },
                                                                          { Ids::resonance, "Resonance" } } };
    for (const auto& [id, caption] : controls)
    {
        dials.push_back (std::make_unique<Knob> (p.getState(), id, caption, Knob::Size::dial));
        addAndMakeVisible (*dials.back());
    }

    addAndMakeVisible (volume);
    addAndMakeVisible (keyboard);
}

void SimpleView::tick()
{
    pedals.tick();
}

void SimpleView::paint (juce::Graphics& g)
{
    const auto& t = forteLook (*this).theme();

    g.setColour (t.ink);
    g.setFont (ForteLookAndFeel::italiana (72.0f, 0.32f));
    g.drawText ("FORTE", juce::Rectangle<int> (24, 30, baseWidth, 80), juce::Justification::centredTop);

    g.setColour (t.muted);
    g.setFont (ForteLookAndFeel::jost (14.0f));
    g.drawText ("Concert Grand", juce::Rectangle<int> (0, 106, baseWidth, 24), juce::Justification::centred);

    g.setColour (feltRed);
    g.fillRect (0, 404, baseWidth, 10);
}

void SimpleView::resized()
{
    for (int i = 0; i < 3; ++i)
        dials[static_cast<std::size_t> (i)]->setBounds (550 - 77 + (i - 1) * 218, 184, 154, 210);

    volume.setBounds (976, 192, 96, 206);
    keyboard.setBounds (0, 414, baseWidth, 196);
    pedals.soft.setBounds (318, 626, 70, 32);
    pedals.sustain.setBounds (400, 622, 430, 48);
}

//==============================================================================
TuningCurve::TuningCurve (PluginProcessor& p)
    : processor (p)
{
    tick();
}

void TuningCurve::tick()
{
    const double newA4 = processor.getState().getRawParameterValue (Ids::a4)->load();
    const double newStretch = processor.getState().getRawParameterValue (Ids::stretch)->load();

    if (newA4 != a4 || newStretch != stretch)
    {
        a4 = newA4;
        stretch = newStretch;
        repaint();
    }
}

juce::Rectangle<float> TuningCurve::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (52.0f).withTrimmedRight (20.0f).withTrimmedTop (48.0f).withTrimmedBottom (46.0f);
}

float TuningCurve::xOf (int key) const
{
    const auto p = plotArea();
    return p.getX() + static_cast<float> (key - 21) / 87.0f * p.getWidth();
}

void TuningCurve::paint (juce::Graphics& g)
{
    const auto& t = forteLook (*this).theme();
    paintPanel (g, getLocalBounds().toFloat(), t);

    const auto& engine = processor.getEngine();
    const Tuning now { a4, stretch, 50.0 };
    const Tuning calculated { a4, 50.0, 50.0 };

    std::array<double, 88> cents {}, calculatedCents {};
    double lo = -10.0, hi = 10.0;
    for (int key = 21; key <= 108; ++key)
    {
        const auto i = static_cast<std::size_t> (key - 21);
        cents[i] = engine.getTuningCents (now, key);
        calculatedCents[i] = engine.getTuningCents (calculated, key);
        lo = std::min ({ lo, cents[i], calculatedCents[i] });
        hi = std::max ({ hi, cents[i], calculatedCents[i] });
    }
    lo = std::floor ((lo - 2.0) / 5.0) * 5.0;
    hi = std::ceil ((hi + 2.0) / 5.0) * 5.0;

    g.setColour (t.ink);
    g.setFont (ForteLookAndFeel::jost (14.0f, true));
    g.drawText ("Tuning", 16, 10, 80, 20, juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (ForteLookAndFeel::jost (12.0f));
    g.drawText ("Each note's pitch in cents from equal temperament, as the tuner sets it from the strings' stiffness. A4 = "
                    + juce::String (juce::roundToInt (a4)) + " Hz, Stretch " + juce::String (juce::roundToInt (stretch)) + ".",
                78, 11, getWidth() - 94, 20, juce::Justification::centredLeft);

    const auto plot = plotArea();
    const auto yOf = [&] (double c) { return plot.getBottom() - static_cast<float> ((c - lo) / (hi - lo)) * plot.getHeight(); };

    // Where the tuner matches which partials.
    const auto band = [&] (int from, int to, const juce::String& label, float alpha)
    {
        const float x0 = xOf (from), x1 = xOf (to);
        g.setColour (t.line.withAlpha (alpha));
        g.fillRect (x0, plot.getY(), x1 - x0, plot.getHeight());
        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (10.5f));
        g.drawText (label, juce::Rectangle<float> (x0, plot.getBottom() + 18.0f, x1 - x0, 14.0f), juce::Justification::centred);
    };
    band (21, 42, "6:3 octaves", 0.0f);
    band (42, 54, "6:3 to 4:2", 0.22f);
    band (54, 57, "", 0.0f);
    band (57, 69, "temperament", 0.4f);
    band (69, 78, "4:2 octaves", 0.0f);
    band (78, 90, "4:2 to 2:1", 0.22f);
    band (90, 108, "2:1 octaves", 0.0f);

    g.setFont (ForteLookAndFeel::jost (10.0f));
    for (int c = static_cast<int> (lo); c <= static_cast<int> (hi); c += hi - lo > 40.0 ? 10 : 5)
    {
        const float y = yOf (c);
        g.setColour (c == 0 ? t.muted : t.line.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (t.muted);
        g.drawText ((c > 0 ? "+" : "") + juce::String (c), 8, juce::roundToInt (y) - 7, 38, 14, juce::Justification::centredRight);
    }

    for (int key = 24; key <= 108; key += 12)
    {
        g.setColour (t.muted);
        g.drawText (noteName (key), juce::roundToInt (xOf (key)) - 14, juce::roundToInt (plot.getBottom()) + 3, 28, 12, juce::Justification::centred);
    }

    const auto pathOf = [&] (const std::array<double, 88>& values)
    {
        juce::Path p;
        for (int key = 21; key <= 108; ++key)
        {
            const juce::Point<float> pt (xOf (key), yOf (values[static_cast<std::size_t> (key - 21)]));
            if (key == 21)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        return p;
    };

    if (std::abs (stretch - 50.0) > 0.5)
    {
        juce::Path dashed;
        const float pattern[] { 3.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, pathOf (calculatedCents), pattern, 2);
        g.setColour (t.muted);
        g.fillPath (dashed);
    }

    g.setColour (t.accent);
    g.strokePath (pathOf (cents), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int key = 21; key <= 108; ++key)
        g.fillEllipse (xOf (key) - 1.6f, yOf (cents[static_cast<std::size_t> (key - 21)]) - 1.6f, 3.2f, 3.2f);

    if (hoverKey >= 21)
    {
        const double c = cents[static_cast<std::size_t> (hoverKey - 21)];
        const double hz = 440.0 * std::pow (2.0, (hoverKey - 69) / 12.0) * (a4 / 440.0) * std::pow (2.0, c / 1200.0);
        const float x = xOf (hoverKey);
        g.setColour (t.ink.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        const auto text = noteName (hoverKey) + "   " + (c >= 0.0 ? "+" : "") + juce::String (c, 1) + " cents   " + juce::String (hz, 2) + " Hz";
        g.setFont (ForteLookAndFeel::jost (12.0f, true));
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16;
        const int bx = juce::jlimit (juce::roundToInt (plot.getX()), juce::roundToInt (plot.getRight()) - w, juce::roundToInt (x) + 8);
        g.setColour (t.ink);
        g.fillRoundedRectangle (static_cast<float> (bx), plot.getY() + 4.0f, static_cast<float> (w), 22.0f, 5.0f);
        g.setColour (t.bg);
        g.drawText (text, bx, juce::roundToInt (plot.getY()) + 4, w, 22, juce::Justification::centred);
    }
}

void TuningCurve::mouseMove (const juce::MouseEvent& e)
{
    const auto p = plotArea();
    const int key = p.contains (e.position) ? juce::jlimit (21, 108, 21 + juce::roundToInt ((e.position.x - p.getX()) / p.getWidth() * 87.0f)) : -1;
    if (key != hoverKey)
    {
        hoverKey = key;
        repaint();
    }
}

void TuningCurve::mouseExit (const juce::MouseEvent&)
{
    hoverKey = -1;
    repaint();
}

//==============================================================================
AdvancedView::AdvancedView (PluginProcessor& p)
    : processor (p), curve (p), pedals (p, *this, "Soft pedal"), keyboard (p.keyboardState)
{
    addAndMakeVisible (curve);
    addAndMakeVisible (keyboard);

    struct Spec
    {
        const char* title;
        std::vector<std::pair<const char*, const char*>> controls;
    };
    const std::vector<Spec> specs {
        { "Hammer", { { Ids::hardness, "Hardness" }, { Ids::touch, "Touch" } } },
        { "Tuning", { { Ids::a4, "A4" }, { Ids::stretch, "Stretch" } } },
        { "Strings", { { Ids::spread, "Spread" }, { Ids::resonance, "Resonance" } } },
        { "Output", { { Ids::brightness, "Brightness" }, { Ids::width, "Width" }, { Ids::volume, "Volume" } } },
    };

    int y = 70;
    for (const auto& spec : specs)
    {
        groups.push_back ({ spec.title, y });
        const int n = static_cast<int> (spec.controls.size());
        constexpr int w = 80;
        const int gap = (248 - n * w) / (n + 1);
        for (int i = 0; i < n; ++i)
        {
            const auto& [id, caption] = spec.controls[static_cast<std::size_t> (i)];
            knobs.push_back (std::make_unique<Knob> (p.getState(), id, caption, Knob::Size::knob));
            knobs.back()->setBounds (828 + gap + i * (w + gap), y + 26, w, 96);
            addAndMakeVisible (*knobs.back());
        }
        y += 124;
    }
    groups.push_back ({ "Pedals", y });
}

void AdvancedView::tick()
{
    pedals.tick();
    curve.tick();
}

void AdvancedView::paint (juce::Graphics& g)
{
    const auto& t = forteLook (*this).theme();

    for (const auto& group : groups)
    {
        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (13.0f, true));
        g.drawText (group.title, 828, group.y, 248, 20, juce::Justification::centredLeft);
        g.setColour (t.line);
        g.drawHorizontalLine (group.y + 22, 828.0f, 1076.0f);
    }

    g.setColour (feltRed);
    g.fillRoundedRectangle (24.0f, 488.0f, 780.0f, 6.0f, 2.0f);
}

void AdvancedView::resized()
{
    curve.setBounds (24, 70, 780, 408);
    keyboard.setBounds (24, 494, 780, 172);

    const int pedalsY = groups.back().y;
    pedals.sustain.setBounds (828 - 8, pedalsY + 32, 256, 48);
    pedals.soft.setBounds (828 + 84, pedalsY + 84, 100, 28);
}
} // namespace forte::ui
