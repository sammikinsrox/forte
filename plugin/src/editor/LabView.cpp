#include "LabView.h"

#include "../LabParameters.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace forte::ui
{
namespace
{
using C = lab::Control;
using lab::Group;
using lab::Register;

// Layout, in the editor's base coordinates.
constexpr int left = 24;
constexpr int fullWidth = baseWidth - 2 * left;
constexpr int topRowY = 66;
constexpr int panelsY = 106;
constexpr int panelHeight = 104;
constexpr int panelGap = 6;
constexpr int cellWidth = 172;
constexpr int cellHeight = 78;
constexpr int bottomY = panelsY + 3 * (panelHeight + panelGap) + 2;
constexpr int bottomHeight = 588 - bottomY;
constexpr int keyboardY = 604;  // under a felt strip, as in the other views
constexpr int curveWidth = 660;

/** The physical values interpolated in log, as the engine does (LabMapping.cpp). */
bool isLog (C c)
{
    return c == C::feltHardness || c == C::strikePosition || c == C::hammerMass || c == C::inharmonicity
        || c == C::sustain || c == C::secondStage;
}

/** Sustain and Second stage are decay-rate multipliers; they read better as how much longer
    the strings ring. */
bool shownAsRing (C c)
{
    return c == C::sustain || c == C::secondStage;
}

double display (C c, double physical)
{
    return shownAsRing (c) && physical > 0.0 ? 1.0 / physical : physical;
}

double fromDisplay (C c, double shown)
{
    return shownAsRing (c) && shown > 0.0 ? 1.0 / shown : shown;
}

juce::String format (C c, double v, bool compact = false)
{
    const auto fixed = [] (double x, int places) { return juce::String (x, places); };
    const auto signedFixed = [] (double x, int places) { return (x >= 0.0 ? "+" : "") + juce::String (x, places); };

    switch (c)
    {
        case C::feltHardness:
        case C::strikePosition:
        case C::hammerMass:
        case C::knockLevel:
        case C::inharmonicity:
        case C::unisonSpread:
        case C::phantomLevel:       return juce::String::fromUTF8 ("\xc3\x97") + fixed (v, 2);
        case C::sustain:
        case C::secondStage:        return juce::String::fromUTF8 ("\xc3\x97") + fixed (v, 2) + (compact ? "" : " ring");
        case C::feltSpike:          return juce::String::fromUTF8 ("\xce\xb5 ") + fixed (v, 2);
        case C::velocityBrightness: return (compact ? "" : "exponent ") + fixed (v, 2);
        case C::overtoneDecay:      return "k " + signedFixed (v, 2);
        case C::pickup:             return (compact ? "" : "gain ") + fixed (v, 2);
        case C::boardTilt:          return signedFixed (v, 1) + (compact ? "" : " dB/oct");
        case C::colourLow:
        case C::colourMid:
        case C::colourHigh:         return signedFixed (v, 1) + " dB";
        case C::tapeDrive:          return v <= 1e-6 ? juce::String ("off") : (compact ? "" : "drive ") + fixed (v, 1);
        case C::count:              break;
    }
    return fixed (v, 2);
}

const char* description (C c)
{
    switch (c)
    {
        case C::feltHardness:       return "How hard the felt is under slow compression: the body of the tone. Harder brings up the 1-3.5 kHz layer, the twang; softer is rounder.";
        case C::feltSpike:          return "How much stiffer the felt is at the very start of the blow. It adds air above 4 kHz without hardening the body. 0 is an elastic felt.";
        case C::velocityBrightness: return "How much brighter a harder blow gets. 0 keeps every velocity at mf's colour; 100 is the physical felt.";
        case C::strikePosition:     return "Where the hammer strikes along the string. Nearer the end (lower) brings out the upper partials; further in dulls them.";
        case C::hammerMass:         return "A heavier hammer stays on the strings longer: darker and fuller for the same blow.";
        case C::knockLevel:         return "The thump of the hammer reaching the soundboard directly, under the fundamental. Fitted to Keyscape's attacks from C4 up.";
        case C::inharmonicity:      return "How stiff the strings are: how far the partials stretch above the harmonic series. The first partial keeps its pitch.";
        case C::unisonSpread:       return "How far the unison's strings are detuned from each other, on top of Spread. More beats and blooms; less is purer, and the decay is one stage.";
        case C::sustain:            return "How long every partial rings, prompt sound and aftersound alike.";
        case C::overtoneDecay:      return "How fast the upper partials die against the fundamental. Higher fades them sooner, for a rounder tail; lower lets them shimmer on.";
        case C::secondStage:        return "The aftersound: the quiet tail after the first, faster decay. Higher rings it longer.";
        case C::phantomLevel:       return "Phantom partials: the bass strings' stretching tones, the growl under the low notes (A0-D3).";
        case C::pickup:             return "The soundboard's direct path under its resonant modes. More is clearer and drier, towards an electric grand; less is woodier.";
        case C::boardTilt:          return "How bright the soundboard's resonant modes are above 1 kHz, in dB per octave.";
        case C::colourLow:          return "A shelf below 250 Hz on top of the fitted colour curve: the board's weight.";
        case C::colourMid:          return "A band from 250 Hz to 2.5 kHz on top of the fitted colour curve: the body, and where nasal and twang live.";
        case C::colourHigh:         return "A shelf above 2.5 kHz on top of the fitted colour curve: presence and air.";
        case C::tapeDrive:          return "The tape-style saturation on the output, as in Keyscape's default patch. 0 is off.";
        case C::count:              break;
    }
    return "";
}

const char* groupName (Group g)
{
    switch (g)
    {
        case Group::hammer:  return "Hammer";
        case Group::strings: return "Strings";
        case Group::board:   return "Board";
        case Group::output:  return "Output";
    }
    return "";
}

const char* groupHint (Group g)
{
    switch (g)
    {
        case Group::hammer:  return "on the next strike";
        case Group::strings: return "on a note's next start from rest";
        case Group::board:   return "rebuilds the board, then fades";
        case Group::output:  return "at once";
    }
    return "";
}

std::vector<Register> registersOf (C c)
{
    if (! lab::info (c).perRegister)
        return { Register::bass };
    return { Register::bass, Register::middle, Register::treble };
}

int anchorKey (Register r)
{
    return lab::registerAnchorKeys[static_cast<std::size_t> (r)];
}

/** The knob's ring: from the committed voicing's place (its resting value: 50 at the top, or 0 at
    the start for a control that only adds) to the value. */
void paintLabKnob (juce::Graphics& g, juce::Point<float> c, float ringRadius, int value, float resting, bool over, bool changed,
                   const Theme& t, bool dark)
{
    constexpr float start = ForteLookAndFeel::rotaryStart, end = ForteLookAndFeel::rotaryEnd;
    const auto angleAt = [] (float p) { return start + p * (end - start); };
    const float position = static_cast<float> (value) / 100.0f;
    const float rest = resting / 100.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, ringRadius, ringRadius, 0.0f, start, end, true);
    g.setColour (t.line);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (changed)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringRadius, ringRadius, 0.0f, angleAt (juce::jmin (rest, position)), angleAt (juce::jmax (rest, position)), true);
        g.setColour (t.accent);
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The tick at the committed voicing's place (arc angles run clockwise from 12 o'clock).
    const float tickAngle = angleAt (rest);
    const auto onRing = [&] (float radius) { return c + juce::Point<float> (radius * std::sin (tickAngle), -radius * std::cos (tickAngle)); };
    g.setColour (t.muted);
    g.drawLine (juce::Line<float> (onRing (ringRadius + 6.0f), onRing (ringRadius + 2.5f)), 1.5f);

    const float r = ringRadius - 6.5f;
    juce::Path knob;
    knob.addEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    juce::DropShadow (juce::Colours::black.withAlpha (dark ? 0.5f : 0.2f), 5, { 0, 2 }).drawForPath (g, knob);
    g.setGradientFill (juce::ColourGradient (t.knobHi, c.x - r * 0.2f, c.y - r * 0.4f, t.knobLo, c.x + r * 0.7f, c.y + r * 0.7f, true));
    g.fillPath (knob);
    if (over)
    {
        g.setColour (t.accent.withAlpha (0.45f));
        g.drawEllipse (c.x - r - 1.5f, c.y - r - 1.5f, 2.0f * r + 3.0f, 2.0f * r + 3.0f, 1.0f);
    }

    const float a = angleAt (position);
    const juce::Point<float> dir (std::sin (a), -std::cos (a));
    g.setColour (t.accent);
    g.drawLine ({ c + dir * (r * 0.25f), c + dir * (r * 0.86f) }, 2.0f);
}
} // namespace

//==============================================================================
/** One control: its knob, its value typed or read, and its registers. */
class LabView::Cell final : public juce::Component
{
public:
    Cell (LabView& v, C c)
        : view (v), control (c), meta (lab::info (c))
    {
        field.setFont (ForteLookAndFeel::jost (12.5f, true));
        field.text = [this] { return juce::String (shownValue()); };
        field.value = [this] { return static_cast<double> (shownValue()); };
        field.set = [this] (double typed)
        {
            const int v = juce::jlimit (0, 100, juce::roundToInt (typed));
            view.beginEdit (control);
            for (auto r : edited())
                view.setValue (control, r, v);
            view.endEdit (control);
        };
        addAndMakeVisible (field);
        setRepaintsOnMouseActivity (true);
        refresh();
    }

    void refresh()
    {
        field.refresh();
        repaint();
    }

    void resized() override
    {
        field.setBounds (4, 58, 56, 18);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& look = forteLook (*this);
        const auto& t = look.theme();
        const auto& s = view.values();
        const auto shown = view.shownRegister (control);
        const int value = shownValue();
        const float resting = lab::restingValue (control);
        const bool changed = static_cast<float> (value) != resting;

        paintLabKnob (g, knobArea().getCentre(), 25.0f, value, resting, isMouseOver (true) || dragging, changed, t, look.isDark());

        const int x = 64;
        const int w = getWidth() - x - 2;
        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (12.5f, true));
        const juce::String name (meta.name.data(), meta.name.size());
        g.drawText (name, x, 6, w, 16, juce::Justification::centredLeft, true);

        // Moved anywhere, even in a register the knob isn't showing.
        if (! s.isNeutral (control))
        {
            const float nameWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
            g.setColour (t.accent);
            g.fillEllipse (static_cast<float> (x) + nameWidth + 5.0f, 11.5f, 5.0f, 5.0f);
        }

        g.setColour (changed ? t.accent : t.ink);
        g.setFont (ForteLookAndFeel::jost (14.5f, true));
        g.drawText (format (control, display (control, view.physical (s, control, shown))), x, 22, w, 18, juce::Justification::centredLeft, true);

        if (! meta.perRegister)
        {
            g.setColour (t.muted);
            g.setFont (ForteLookAndFeel::jost (11.0f));
            g.drawText ("whole keyboard", x, 48, w, 14, juce::Justification::centredLeft);
            return;
        }

        // The three registers: a bar each, from the committed voicing's place to the value.
        static const char* letters[] { "B", "M", "T" };
        for (int r = 0; r < lab::numRegisters; ++r)
        {
            const auto area = barArea (r).toFloat();
            const int v = static_cast<int> (s.get (control, static_cast<Register> (r)));
            const bool selected = view.registerMode() == r || view.registerMode() == allRegisters;
            const float top = area.getY() + 1.0f, bottom = area.getY() + 21.0f;
            const float mid = bottom + (top - bottom) * resting / 100.0f;
            const float y = bottom + (top - bottom) * static_cast<float> (v) / 100.0f;

            g.setColour (t.line.withAlpha (0.6f));
            g.fillRoundedRectangle (area.getX(), top, 7.0f, bottom - top, 2.0f);
            g.setColour (static_cast<float> (v) == resting ? t.muted : t.accent);
            g.fillRect (area.getX(), juce::jmin (mid, y), 7.0f, juce::jmax (1.5f, std::abs (mid - y)));

            g.setColour (selected ? t.ink : t.muted);
            g.setFont (ForteLookAndFeel::jost (10.0f, selected));
            g.drawText (letters[r], juce::Rectangle<float> (area.getX() - 4.0f, bottom + 1.0f, 15.0f, 12.0f), juce::Justification::centred);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { view.setFocus (control); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (knobArea().contains (e.position) ? juce::MouseCursor::UpDownResizeCursor
                                                         : barAt (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor
                                                                                   : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        view.setFocus (control);

        if (const int r = barAt (e.position); r >= 0)
        {
            view.setRegisterMode (r);
            return;
        }

        if (! knobArea().contains (e.position))
            return;

        dragging = true;
        dragStart = e.position;
        for (auto r : edited())
            startValues[static_cast<std::size_t> (r)] = static_cast<int> (view.values().get (control, r));
        view.beginEdit (control);
        e.source.enableUnboundedMouseMovement (true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        // 160 px for the whole range; Shift for fine.
        const float travel = (dragStart.y - e.position.y) + 0.5f * (e.position.x - dragStart.x);
        const float units = travel * 100.0f / (e.mods.isShiftDown() ? 800.0f : 160.0f);
        for (auto r : edited())
            view.setValue (control, r, juce::jlimit (0, 100, startValues[static_cast<std::size_t> (r)] + juce::roundToInt (units)));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        dragging = false;
        view.endEdit (control);
        e.source.enableUnboundedMouseMovement (false);
        refresh();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! knobArea().contains (e.position))
            return;

        view.beginEdit (control);
        for (auto r : edited())
            view.setValue (control, r, static_cast<int> (lab::restingValue (control)));
        view.endEdit (control);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (delta == 0.0f)
            return;

        const int step = (delta > 0.0f ? 1 : -1) * (e.mods.isShiftDown() ? 5 : 1);
        view.beginEdit (control);
        for (auto r : edited())
            view.setValue (control, r, juce::jlimit (0, 100, static_cast<int> (view.values().get (control, r)) + step));
        view.endEdit (control);
    }

private:
    [[nodiscard]] juce::Rectangle<float> knobArea() const { return { 4.0f, 2.0f, 56.0f, 56.0f }; }
    [[nodiscard]] juce::Rectangle<int> barArea (int r) const { return { 66 + r * 17, 44, 8, 22 }; }

    [[nodiscard]] int barAt (juce::Point<float> p) const
    {
        if (! meta.perRegister)
            return -1;
        for (int r = 0; r < lab::numRegisters; ++r)
            if (barArea (r).expanded (4, 3).withBottom (barArea (r).getBottom() + 14).toFloat().contains (p))
                return r;
        return -1;
    }

    [[nodiscard]] int shownValue() const
    {
        return static_cast<int> (view.values().get (control, view.shownRegister (control)));
    }

    /** The registers an edit moves: the shown one, or all three under All. */
    [[nodiscard]] std::vector<Register> edited() const
    {
        if (meta.perRegister && view.registerMode() == allRegisters)
            return registersOf (control);
        return { view.shownRegister (control) };
    }

    LabView& view;
    C control;
    const lab::ControlInfo& meta;
    ValueField field;
    bool dragging = false;
    juce::Point<float> dragStart;
    std::array<int, lab::numRegisters> startValues {};
};

//==============================================================================
/** The focused control across the keyboard: committed (dashed) and now, with a handle at each
    register's anchor to drag. */
class LabView::Curve final : public juce::Component
{
public:
    explicit Curve (LabView& v) : view (v) {}

    void refresh()
    {
        const auto c = view.focus();
        const auto& s = view.values();
        lab::Settings committed, lowest = s, highest = s;
        for (auto r : registersOf (c))
        {
            lowest.set (c, r, 0.0f);
            highest.set (c, r, 100.0f);
        }

        lo = 1e30;
        hi = -1e30;
        for (int key = 21; key <= 108; ++key)
        {
            const auto i = static_cast<std::size_t> (key - 21);
            now[i] = display (c, view.physicalAtKey (s, c, key));
            fitted[i] = display (c, view.physicalAtKey (committed, c, key));
            for (double v : { now[i], fitted[i], display (c, view.physicalAtKey (lowest, c, key)), display (c, view.physicalAtKey (highest, c, key)) })
            {
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
        }

        logScale = isLog (c) && lo > 0.0;
        if (hi - lo < 1e-9)
        {
            hi += 0.5;
            lo -= 0.5;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = forteLook (*this).theme();
        paintPanel (g, getLocalBounds().toFloat(), t);

        const auto c = view.focus();
        const auto& meta = lab::info (c);
        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (13.0f, true));
        const auto title = juce::String (meta.name.data(), meta.name.size()) + " across the keyboard";
        g.drawText (title, 14, 8, 400, 18, juce::Justification::centredLeft);

        // Legend.
        g.setFont (ForteLookAndFeel::jost (11.0f));
        const float lx = static_cast<float> (getWidth()) - 190.0f;
        juce::Path dash, line;
        dash.startNewSubPath (lx, 17.0f);
        dash.lineTo (lx + 18.0f, 17.0f);
        const float pattern[] { 3.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dash, dash, pattern, 2);
        g.setColour (t.muted);
        g.fillPath (dash);
        g.drawText ("committed", juce::roundToInt (lx) + 22, 10, 70, 14, juce::Justification::centredLeft);
        g.setColour (t.accent);
        g.drawLine (lx + 96.0f, 17.0f, lx + 114.0f, 17.0f, 2.0f);
        g.setColour (t.muted);
        g.drawText ("now", juce::roundToInt (lx) + 118, 10, 40, 14, juce::Justification::centredLeft);

        const auto plot = plotArea();

        // Octaves, and the register anchors.
        g.setFont (ForteLookAndFeel::jost (10.0f));
        for (int key = 24; key <= 108; key += 12)
        {
            const float x = xOf (key);
            g.setColour (t.line.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (t.muted);
            g.drawText (noteName (key), juce::roundToInt (x) - 14, juce::roundToInt (plot.getBottom()) + 3, 28, 12, juce::Justification::centred);
        }

        // Value ticks.
        for (int i = 0; i <= 2; ++i)
        {
            const double v = valueAtFraction (i / 2.0);
            const float y = yOf (v);
            g.setColour (t.line.withAlpha (0.5f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (t.muted);
            g.drawText (format (c, v, true), 4, juce::roundToInt (y) - 7, juce::roundToInt (plot.getX()) - 8, 14, juce::Justification::centredRight);
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

        juce::Path committedPath = pathOf (fitted);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, committedPath, pattern, 2);
        g.setColour (t.muted);
        g.fillPath (dashed);

        const bool moved = now != fitted;
        g.setColour (moved ? t.accent : t.ink);
        g.strokePath (pathOf (now), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Handles.
        for (auto r : registersOf (c))
        {
            const int key = meta.perRegister ? anchorKey (r) : 60;
            const auto centre = handleCentre (r);
            const bool active = ! meta.perRegister || view.registerMode() == static_cast<int> (r) || view.registerMode() == allRegisters;
            const float radius = (dragging == static_cast<int> (r) || hovered == static_cast<int> (r)) ? 6.5f : 5.0f;
            g.setColour (t.panel);
            g.fillEllipse (centre.x - radius, centre.y - radius, 2.0f * radius, 2.0f * radius);
            g.setColour (active ? t.accent : t.muted);
            g.drawEllipse (centre.x - radius, centre.y - radius, 2.0f * radius, 2.0f * radius, 2.0f);
            if (active)
                g.fillEllipse (centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
            juce::ignoreUnused (key);
        }

        // The key under the mouse.
        if (hoverKey >= 21)
        {
            const float x = xOf (hoverKey);
            g.setColour (t.ink.withAlpha (0.25f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            const auto text = noteName (hoverKey) + "  " + format (c, now[static_cast<std::size_t> (hoverKey - 21)]);
            g.setFont (ForteLookAndFeel::jost (11.5f, true));
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 12;
            const int bx = juce::jlimit (juce::roundToInt (plot.getX()), juce::roundToInt (plot.getRight()) - w, juce::roundToInt (x) + 6);
            g.setColour (t.ink);
            g.fillRoundedRectangle (static_cast<float> (bx), plot.getY() + 2.0f, static_cast<float> (w), 18.0f, 4.0f);
            g.setColour (t.bg);
            g.drawText (text, bx, juce::roundToInt (plot.getY()) + 2, w, 18, juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int handle = handleAt (e.position);
        const int key = plotArea().contains (e.position) ? keyAt (e.position.x) : -1;
        if (handle != hovered || key != hoverKey)
        {
            hovered = handle;
            hoverKey = key;
            setMouseCursor (handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        hoverKey = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleAt (e.position);
        if (dragging < 0)
            return;

        const auto c = view.focus();
        if (lab::info (c).perRegister && view.registerMode() != allRegisters)
            view.setRegisterMode (dragging);
        view.beginEdit (c);
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;

        const auto c = view.focus();
        const auto reg = static_cast<Register> (dragging);
        const double target = fromDisplay (c, valueAtY (e.position.y));

        // The lab value whose physical value at the anchor is nearest the mouse.
        auto trial = view.values();
        int best = static_cast<int> (lab::restingValue (c));
        double bestError = 1e30;
        for (int v = 0; v <= 100; ++v)
        {
            trial.set (c, reg, static_cast<float> (v));
            const double p = view.physical (trial, c, reg);
            const double error = logScale && p > 0.0 && target > 0.0 ? std::abs (std::log (p / target)) : std::abs (p - target);
            if (error < bestError)
            {
                bestError = error;
                best = v;
            }
        }

        view.setValue (c, reg, best);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            view.endEdit (view.focus());
        dragging = -1;
        repaint();
    }

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (64.0f).withTrimmedRight (16.0f).withTrimmedTop (34.0f).withTrimmedBottom (20.0f);
    }

    [[nodiscard]] float xOf (int key) const
    {
        const auto p = plotArea();
        return p.getX() + static_cast<float> (key - 21) / 87.0f * p.getWidth();
    }

    [[nodiscard]] int keyAt (float x) const
    {
        const auto p = plotArea();
        return juce::jlimit (21, 108, 21 + juce::roundToInt ((x - p.getX()) / p.getWidth() * 87.0f));
    }

    [[nodiscard]] double toAxis (double v) const { return logScale ? std::log (v) : v; }
    [[nodiscard]] double fromAxis (double a) const { return logScale ? std::exp (a) : a; }

    [[nodiscard]] double valueAtFraction (double f) const
    {
        const double a = toAxis (lo), b = toAxis (hi);
        return fromAxis (a + (b - a) * f);
    }

    [[nodiscard]] float yOf (double v) const
    {
        const auto p = plotArea().reduced (0.0f, 6.0f);
        const double a = toAxis (lo), b = toAxis (hi);
        const double f = (toAxis (juce::jlimit (lo, hi, v)) - a) / (b - a);
        return p.getBottom() - static_cast<float> (f) * p.getHeight();
    }

    [[nodiscard]] double valueAtY (float y) const
    {
        const auto p = plotArea().reduced (0.0f, 6.0f);
        const double f = juce::jlimit (0.0, 1.0, static_cast<double> ((p.getBottom() - y) / p.getHeight()));
        return valueAtFraction (f);
    }

    [[nodiscard]] juce::Point<float> handleCentre (Register r) const
    {
        const auto c = view.focus();
        const int key = lab::info (c).perRegister ? anchorKey (r) : 60;
        return { xOf (key), yOf (now[static_cast<std::size_t> (key - 21)]) };
    }

    [[nodiscard]] int handleAt (juce::Point<float> p) const
    {
        for (auto r : registersOf (view.focus()))
            if (handleCentre (r).getDistanceFrom (p) < 10.0f)
                return static_cast<int> (r);
        return -1;
    }

    LabView& view;
    std::array<double, 88> now {}, fitted {};
    double lo = 0.0, hi = 1.0;
    bool logScale = false;
    int dragging = -1, hovered = -1, hoverKey = -1;
};

//==============================================================================
/** What the focused control does, where it stands, and the lab's state. */
class LabView::Info final : public juce::Component
{
public:
    explicit Info (LabView& v) : view (v)
    {
        resetControl.onClick = [this] { view.resetControl (view.focus()); };
        resetAll.onClick = [this] { view.resetAll(); };
        resetControl.setTooltip ("Every register of this control back to the committed voicing (its tick)");
        resetAll.setTooltip ("Every lab control back to the committed voicing");
        addAndMakeVisible (resetControl);
        addAndMakeVisible (resetAll);
    }

    void refresh()
    {
        const auto c = view.focus();
        resetControl.setEnabled (! view.values().isNeutral (c));
        bool anyChanged = false;
        for (const auto& meta : lab::controls)
            anyChanged = anyChanged || ! view.values().isNeutral (meta.control);
        resetAll.setEnabled (anyChanged);
        repaint();
    }

    void resized() override
    {
        resetControl.setBounds (14, getHeight() - 34, 104, 24);
        resetAll.setBounds (124, getHeight() - 34, 84, 24);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = forteLook (*this).theme();
        paintPanel (g, getLocalBounds().toFloat(), t);

        const auto c = view.focus();
        const auto& meta = lab::info (c);
        const auto& s = view.values();

        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (15.0f, true));
        g.drawText (juce::String (meta.name.data(), meta.name.size()), 14, 8, getWidth() - 28, 20, juce::Justification::centredLeft);

        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (11.0f));
        g.drawText (juce::String (groupName (meta.group)) + ", " + groupHint (meta.group), 14, 27, getWidth() - 28, 14,
                    juce::Justification::centredLeft);

        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (12.0f));
        g.drawFittedText (description (c), 14, 44, getWidth() - 28, 46, juce::Justification::topLeft, 3, 1.0f);

        // Where each register stands.
        juce::String where;
        if (meta.perRegister)
        {
            static const char* names[] { "Bass", "Middle", "Treble" };
            for (int r = 0; r < lab::numRegisters; ++r)
            {
                const auto reg = static_cast<Register> (r);
                where << names[r] << " " << juce::String (static_cast<int> (s.get (c, reg))) << " "
                      << format (c, display (c, view.physical (s, c, reg)), true) << (r < 2 ? "    " : "");
            }
        }
        else
        {
            where << juce::String (static_cast<int> (s.get (c))) << "  " << format (c, display (c, view.physical (s, c, Register::bass)));
        }
        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (11.5f, true));
        g.drawText (where, 14, 90, getWidth() - 28, 16, juce::Justification::centredLeft, true);

        // The lab's state: rebuilding, or the last export.
        if (view.status.isNotEmpty())
        {
            const auto area = juce::Rectangle<int> (216, getHeight() - 34, getWidth() - 230, 24);
            g.setColour (view.statusBusy ? t.accent : t.muted);
            g.fillEllipse (static_cast<float> (area.getX()), static_cast<float> (area.getCentreY()) - 3.0f, 6.0f, 6.0f);
            g.setFont (ForteLookAndFeel::jost (11.0f));
            g.drawFittedText (view.status, area.withTrimmedLeft (12), juce::Justification::centredLeft, 2, 0.9f);
        }
    }

private:
    LabView& view;
    juce::TextButton resetControl { "Reset control" }, resetAll { "Reset lab" };
};

//==============================================================================
LabView::LabView (PluginProcessor& p)
    : processor (p), keyboard (p.keyboardState)
{
    addAndMakeVisible (keyboard);

    auto& state = processor.getState();
    for (const auto& meta : lab::controls)
        for (auto r : registersOf (meta.control))
            parameters[static_cast<std::size_t> (meta.control)][static_cast<std::size_t> (r)]
                = state.getParameter (LabParameters::parameterId (meta.control, r));

    settings = readSettings();

    registers.setSelected (mode);
    registers.setTooltips ({ "The knobs show and move the bass (anchored at C2)", "The knobs show and move the middle (C4)",
                             "The knobs show and move the treble (C7)", "The knobs move all three registers together" });
    registers.onChange = [this] (int m) { setRegisterMode (m); };
    addAndMakeVisible (registers);

    compare.setSelected (processor.isComparingCommitted() ? 1 : 0);
    compare.setTooltips ({ "Hear the lab's voicing", "Hear the committed voicing, every control at its tick. The lab keeps its values." });
    compare.onChange = [this] (int m)
    {
        processor.setComparingCommitted (m == 1);
        for (auto& cell : cells)
            cell->setEnabled (m == 0);
        repaint();
    };
    addAndMakeVisible (compare);

    exportButton.setTooltip ("Write the lab's values to Documents\\Forte\\voicing.json, for tools/fitting/fold_voicing.py");
    exportButton.onClick = [this] { processor.requestExport(); };
    addAndMakeVisible (exportButton);

    for (std::size_t g = 0; g < groupResets.size(); ++g)
    {
        groupResets[g] = std::make_unique<juce::TextButton> ("Reset");
        groupResets[g]->setTooltip (juce::String ("Every ") + groupName (static_cast<Group> (g)) + " control back to the committed voicing");
        groupResets[g]->onClick = [this, g] { resetGroup (static_cast<Group> (g)); };
        addAndMakeVisible (*groupResets[g]);
    }

    for (const auto& meta : lab::controls)
    {
        cells[static_cast<std::size_t> (meta.control)] = std::make_unique<Cell> (*this, meta.control);
        cells[static_cast<std::size_t> (meta.control)]->setEnabled (! processor.isComparingCommitted());
        addAndMakeVisible (*cells[static_cast<std::size_t> (meta.control)]);
    }

    curve = std::make_unique<Curve> (*this);
    info = std::make_unique<Info> (*this);
    addAndMakeVisible (*curve);
    addAndMakeVisible (*info);

    refresh();
    startTimerHz (30);
}

LabView::~LabView()
{
    stopTimer();
}

juce::RangedAudioParameter* LabView::parameter (C control, Register reg) const noexcept
{
    const auto r = lab::info (control).perRegister ? reg : Register::bass;
    return parameters[static_cast<std::size_t> (control)][static_cast<std::size_t> (r)];
}

lab::Settings LabView::readSettings() const
{
    lab::Settings s;
    for (const auto& meta : lab::controls)
        for (auto r : registersOf (meta.control))
            if (auto* p = parameter (meta.control, r))
                s.set (meta.control, r, std::round (p->convertFrom0to1 (p->getValue())));
    return s;
}

Register LabView::shownRegister (C control) const noexcept
{
    if (! lab::info (control).perRegister)
        return Register::bass;
    return mode == allRegisters ? Register::middle : static_cast<Register> (mode);
}

double LabView::physical (const lab::Settings& at, C control, Register reg) const noexcept
{
    const int key = lab::info (control).perRegister ? anchorKey (reg) : 60;
    return processor.getEngine().getLabPhysicalValue (at, control, key);
}

double LabView::physicalAtKey (const lab::Settings& at, C control, int midiNote) const noexcept
{
    return processor.getEngine().getLabPhysicalValue (at, control, midiNote);
}

void LabView::setRegisterMode (int newMode)
{
    mode = juce::jlimit (0, allRegisters, newMode);
    registers.setSelected (mode);
    for (auto& cell : cells)
        cell->refresh();
    curve->repaint();
}

void LabView::setFocus (C control)
{
    if (control == focused)
        return;
    focused = control;
    curve->refresh();
    info->refresh();
}

void LabView::beginEdit (C control)
{
    for (auto r : registersOf (control))
        if (auto* p = parameter (control, r))
            p->beginChangeGesture();
}

void LabView::setValue (C control, Register reg, int value)
{
    if (auto* p = parameter (control, reg))
    {
        const float normalised = p->convertTo0to1 (static_cast<float> (value));
        if (p->getValue() != normalised)
            p->setValueNotifyingHost (normalised);
    }

    // Show it at once rather than at the next poll.
    settings.set (control, lab::info (control).perRegister ? reg : Register::bass, static_cast<float> (value));
    refresh();
}

void LabView::endEdit (C control)
{
    for (auto r : registersOf (control))
        if (auto* p = parameter (control, r))
            p->endChangeGesture();
}

void LabView::setValueNow (C control, Register reg, int value)
{
    beginEdit (control);
    setValue (control, reg, value);
    endEdit (control);
}

void LabView::resetControl (C control)
{
    beginEdit (control);
    for (auto r : registersOf (control))
        setValue (control, r, static_cast<int> (lab::restingValue (control)));
    endEdit (control);
}

void LabView::resetGroup (Group group)
{
    for (const auto& meta : lab::controls)
        if (meta.group == group)
            resetControl (meta.control);
}

void LabView::resetAll()
{
    for (const auto& meta : lab::controls)
        resetControl (meta.control);
}

void LabView::refresh()
{
    for (auto& cell : cells)
        cell->refresh();
    for (std::size_t g = 0; g < groupResets.size(); ++g)
        groupResets[g]->setEnabled (! settings.isNeutral (static_cast<Group> (g)));
    curve->refresh();
    info->refresh();
}

void LabView::timerCallback()
{
    // Automation, the host's generic editor and undo move the values too.
    if (const auto now = readSettings(); now != settings)
    {
        settings = now;
        refresh();
    }

    juce::String newStatus;
    bool busy = true;
    const auto& engine = processor.getEngine();
    const auto& lastExport = processor.getLastExport();

    if (engine.hasIncomingBoard())
        newStatus = "Building the board: it fades in within a moment.";
    else if (engine.hasIncomingStrings())
        newStatus = "New strings are ready: each note takes them when it next starts from rest.";
    else if (processor.isComparingCommitted())
        newStatus = "Hearing the committed voicing. The lab's values are kept.";
    else
    {
        busy = false;
        if (lastExport.message.isNotEmpty() && (juce::Time::getCurrentTime() - lastExport.when).inSeconds() < 30.0)
            newStatus = lastExport.message;
        else
            newStatus = "Voicing revision " + juce::String (Engine::getVoicingRevision()) + ". At its tick, every control is the committed voicing.";
    }

    if (newStatus != status || busy != statusBusy)
    {
        status = newStatus;
        statusBusy = busy;
        info->repaint();
    }
}

juce::Rectangle<int> LabView::groupArea (Group group) const
{
    const int boardWidth = 16 + 5 * cellWidth;
    switch (group)
    {
        case Group::hammer:  return { left, panelsY, fullWidth, panelHeight };
        case Group::strings: return { left, panelsY + panelHeight + panelGap, fullWidth, panelHeight };
        case Group::board:   return { left, panelsY + 2 * (panelHeight + panelGap), boardWidth, panelHeight };
        case Group::output:  return { left + boardWidth + 8, panelsY + 2 * (panelHeight + panelGap), fullWidth - boardWidth - 8, panelHeight };
    }
    return {};
}

void LabView::paint (juce::Graphics& g)
{
    const auto& t = forteLook (*this).theme();

    g.setColour (t.ink);
    g.setFont (ForteLookAndFeel::jost (18.0f, true));
    g.drawText ("Voicing lab", left, topRowY, 140, 30, juce::Justification::centredLeft);

    g.setColour (t.muted);
    g.setFont (ForteLookAndFeel::jost (11.5f));
    g.drawText ("Knobs show", 270, topRowY, 72, 30, juce::Justification::centredRight);

    for (int gi = 0; gi < 4; ++gi)
    {
        const auto group = static_cast<Group> (gi);
        const auto area = groupArea (group);
        paintPanel (g, area.toFloat(), t);

        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (13.0f, true));
        const juce::String name = groupName (group);
        const int nameWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name));
        g.drawText (name, area.getX() + 14, area.getY() + 6, nameWidth + 4, 18, juce::Justification::centredLeft);

        if (group != Group::output)
        {
            g.setColour (t.muted);
            g.setFont (ForteLookAndFeel::jost (11.0f));
            g.drawText (groupHint (group), area.getX() + 24 + nameWidth, area.getY() + 7, 260, 16, juce::Justification::centredLeft);
        }
    }

    g.setColour (feltRed);
    g.fillRect (0, keyboardY - 8, baseWidth, 8);
}

void LabView::paintOverChildren (juce::Graphics& g)
{
    if (! processor.isComparingCommitted())
        return;

    // Comparing: the knobs rest under a veil, and say so.
    const auto& t = forteLook (*this).theme();
    const auto area = juce::Rectangle<int> (left, panelsY, fullWidth, 3 * panelHeight + 2 * panelGap).toFloat();
    g.setColour (t.bg.withAlpha (0.62f));
    g.fillRoundedRectangle (area, 10.0f);

    const auto banner = area.withSizeKeepingCentre (460.0f, 40.0f);
    g.setColour (t.ink);
    g.fillRoundedRectangle (banner, 20.0f);
    g.setColour (t.bg);
    g.setFont (ForteLookAndFeel::jost (13.5f, true));
    g.drawText ("Hearing the committed voicing. Your lab values are kept.", banner, juce::Justification::centred);
}

void LabView::resized()
{
    registers.setBounds (346, topRowY + 2, 360, 26);
    compare.setBounds (728, topRowY + 2, 168, 26);
    exportButton.setBounds (baseWidth - left - 156, topRowY, 156, 30);

    for (int gi = 0; gi < 4; ++gi)
    {
        const auto area = groupArea (static_cast<Group> (gi));
        groupResets[static_cast<std::size_t> (gi)]->setBounds (area.getRight() - 62, area.getY() + 6, 52, 20);
    }

    std::array<int, 4> placed {};
    for (const auto& meta : lab::controls)
    {
        const auto gi = static_cast<std::size_t> (meta.group);
        const auto area = groupArea (meta.group);
        const int width = meta.group == Group::output ? area.getWidth() - 16 : cellWidth;
        cells[static_cast<std::size_t> (meta.control)]->setBounds (area.getX() + 8 + placed[gi] * cellWidth, area.getY() + 24, width, cellHeight);
        ++placed[gi];
    }

    curve->setBounds (left, bottomY, curveWidth, bottomHeight);
    info->setBounds (left + curveWidth + 8, bottomY, fullWidth - curveWidth - 8, bottomHeight);
    keyboard.setBounds (0, keyboardY, baseWidth, baseHeight - keyboardY);
}
} // namespace forte::ui
