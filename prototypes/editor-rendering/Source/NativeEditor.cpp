// PROTOTYPE (throwaway)
#include "NativeEditor.h"

using namespace juce;
static constexpr int baseW = 1100, baseH = 690;
static constexpr float startAng = MathConstants<float>::pi * 1.25f, endAng = MathConstants<float>::pi * 2.75f;

//============================================================================== look and feel
ForteLnf::ForteLnf()
{
    italiana = Typeface::createSystemTypefaceFor (BinaryData::ItalianaRegular_ttf, (size_t) BinaryData::ItalianaRegular_ttfSize);
    jost     = Typeface::createSystemTypefaceFor (BinaryData::Jost_ttf, (size_t) BinaryData::Jost_ttfSize);
}

Font ForteLnf::jostFont (float h, bool medium) const
{
    // Jost is a variable font whose default instance is Thin: clone fixed 400/500 weights, as in the web version.
    // withPointHeight matches CSS font-size (em size); withHeight would be the smaller ascent+descent size.
    static Typeface::Ptr regular, med;
    if (regular == nullptr)
    {
        const FontVariableSetting r[] { { FontFeatureTag ("wght"), 400.0f } }, m[] { { FontFeatureTag ("wght"), 500.0f } };
        regular = jost->cloneWithVariableSettings (r);
        med     = jost->cloneWithVariableSettings (m);
    }
    return Font (FontOptions (medium ? med : regular).withPointHeight (h));
}

void ForteLnf::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider&)
{
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float ringR = (float) jmin (w, h) * 0.5f - 2.0f;
    const float r = ringR - jmax (5.0f, ringR * 0.16f);
    const float ang = startAng + pos * (endAng - startAng);

    Path track; track.addCentredArc (c.x, c.y, ringR, ringR, 0, startAng, endAng, true);
    g.setColour (t.line); g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    Path val; val.addCentredArc (c.x, c.y, ringR, ringR, 0, startAng, ang, true);
    g.setColour (t.accent); g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path knob; knob.addEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    DropShadow (Colours::black.withAlpha (t.bg.getBrightness() > 0.5f ? 0.22f : 0.5f), (int) (r * 0.3f), { 0, (int) (r * 0.12f) }).drawForPath (g, knob);
    g.setGradientFill (ColourGradient (t.knobHi, c.x - r * 0.2f, c.y - r * 0.4f, t.knobLo, c.x + r * 0.7f, c.y + r * 0.7f, true));
    g.fillPath (knob);

    const auto dir = Point<float> (std::sin (ang), -std::cos (ang));
    g.setColour (t.accent);
    g.drawLine ({ c + dir * (r * 0.24f), c + dir * (r * 0.84f) }, r > 40 ? 3.0f : 2.0f);
}

void ForteLnf::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider&)
{
    if (style == Slider::LinearVertical)
    {
        const float cx = (float) x + (float) w * 0.5f;
        g.setColour (t.line); g.fillRoundedRectangle (cx - 2, (float) y, 4, (float) h, 2);
        g.setColour (t.ink);  g.fillRoundedRectangle (cx - 2, pos, 4, (float) (y + h) - pos, 2);
        g.fillRoundedRectangle (cx - 16, pos - 9, 32, 18, 3);
    }
    else
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (t.line); g.fillRect ((float) x, cy - 1, (float) w, 2.0f);
        g.setColour (t.ink);  g.fillRect ((float) x, cy - 1, pos - (float) x, 2.0f);
        g.fillEllipse (pos - 7, cy - 7, 14, 14);
    }
}

void ForteLnf::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool seg = b.getProperties()["seg"];
    if (b.getToggleState() || (over && ! seg)) { g.setColour (t.ink); g.fillRoundedRectangle (r, r.getHeight() / 2); }
    else if (! seg) { g.setColour (t.ink); g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f); }
}

void ForteLnf::drawButtonText (Graphics& g, TextButton& b, bool over, bool)
{
    const bool seg = b.getProperties()["seg"];
    const bool filled = b.getToggleState() || (over && ! seg);
    g.setColour (filled ? t.bg : (seg ? t.muted : t.ink));
    g.setFont (jostFont (seg ? 12.0f : 13.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

//============================================================================== keyboard
HeatKeyboard::HeatKeyboard (ProtoProcessor& p, ForteLnf& l)
    : MidiKeyboardComponent (p.keyboard, horizontalKeyboard), proc (p), lnf (l)
{
    setAvailableRange (21, 108);
    setScrollButtonsVisible (false);
    setVelocity (0.7f, true);
    setBlackNoteLengthProportion (0.62f);
    setColour (shadowColourId, Colours::transparentBlack);
    setColour (keySeparatorLineColourId, Colour (0xffc9c8c2));
    setWantsKeyboardFocus (false);
}

void HeatKeyboard::resized()
{
    setKeyWidth ((float) getWidth() / 52.0f);
    MidiKeyboardComponent::resized();
    setLowestVisibleKey (21);
}

void HeatKeyboard::drawWhiteNote (int m, Graphics& g, Rectangle<float> a, bool, bool, Colour, Colour)
{
    g.setColour (Colour (0xfffbfaf6).interpolatedWith (lnf.t.accent, heat (m) * 0.55f));
    g.fillRect (a);
    g.setColour (Colour (0xffe4e3dd)); g.fillRect (a.removeFromBottom (6.0f));
    g.setColour (Colour (0xffc9c8c2)); g.fillRect (a.withX (a.getRight() - 1).withWidth (1.0f).withBottom (a.getBottom() + 6));
}

void HeatKeyboard::drawBlackNote (int m, Graphics& g, Rectangle<float> a, bool, bool, Colour)
{
    g.setColour (Colour (0xff171413).interpolatedWith (lnf.t.accent, heat (m) * 0.85f));
    g.fillRoundedRectangle (a, 2.0f);
    g.setColour (Colour (0xff3a3532)); g.fillRect (a.removeFromBottom (4.0f));
}

//============================================================================== control
Control::Control (ProtoProcessor& p, ForteLnf& lnf, const char* id, const String& cap, Kind k) : kind (k)
{
    float def = 0; String u;
    for (auto& kn : ids::knobs) if (String (kn.id) == id) { def = kn.def; u = kn.unit; }
    slider.setSliderStyle (k == vFader ? Slider::LinearVertical : k == hSlider ? Slider::LinearHorizontal : Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (startAng, endAng, true);
    slider.setMouseDragSensitivity (200);
    slider.setDoubleClickReturnValue (true, def);
    addAndMakeVisible (slider);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.state, id, slider);

    caption.setText (cap, dontSendNotification);
    caption.setFont (lnf.jostFont (k == bigDial ? 17.0f : 12.5f, k == bigDial));
    caption.setJustificationType (k == hSlider ? Justification::centredRight : Justification::centred);
    caption.setColour (Label::textColourId, lnf.t.ink);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    value.setEditable (true, true, false);
    value.setFont (lnf.jostFont (14.0f, true));
    value.setJustificationType (Justification::centred);
    for (auto id2 : { Label::textColourId, Label::textWhenEditingColourId }) value.setColour (id2, lnf.t.ink);
    value.setColour (Label::outlineWhenEditingColourId, lnf.t.accent);
    value.setColour (TextEditor::highlightColourId, lnf.t.accent.withAlpha (0.3f));
    value.onTextChange = [this] { slider.setValue (value.getText().getDoubleValue(), sendNotificationSync);
                                  value.setText (String (roundToInt (slider.getValue())), dontSendNotification); };
    slider.onValueChange = [this] { value.setText (String (roundToInt (slider.getValue())), dontSendNotification); };
    slider.onValueChange();
    addAndMakeVisible (value);

    unit.setText (u, dontSendNotification);
    unit.setFont (lnf.jostFont (11.0f));
    unit.setColour (Label::textColourId, lnf.t.muted);
    addAndMakeVisible (unit);
}

void Control::resized()
{
    auto b = getLocalBounds();
    if (kind == hSlider)
    {
        caption.setBounds (b.removeFromLeft (64)); b.removeFromLeft (12);
        value.setBounds (b.removeFromRight (44));
        slider.setBounds (b);
        return;
    }
    if (kind == vFader)
    {
        slider.setBounds (b.removeFromTop (170).withSizeKeepingCentre (44, 170));
        b.removeFromTop (6);
        caption.setBounds (b.removeFromTop (18));
    }
    else if (kind == smallKnob)
    {
        slider.setBounds (b.removeFromTop (54).withSizeKeepingCentre (54, 54));
        caption.setBounds (b.removeFromTop (18));
    }
    else
    {
        slider.setBounds (b.removeFromTop (b.getWidth()));
        b.removeFromTop (kind == bigDial ? 8 : 2);
        caption.setBounds (b.removeFromTop (kind == bigDial ? 24 : 18));
    }
    auto row = b.removeFromTop (22);
    value.setBounds (row.withSizeKeepingCentre (40, 22));
    unit.setBounds (value.getRight(), row.getY() + 2, 24, 20);
}

//============================================================================== content (fixed 1100x690, scaled by the editor)
struct NativeEditor::Content : public Component
{
    Content (ProtoProcessor& p, ForteLnf& l, std::function<void()> onChange)
        : proc (p), lnf (l), keyboard (p, l), changed (std::move (onChange))
    {
        const bool adv = proc.uiAdvanced;
        auto pill = [this] (TextButton& b, const String& text, bool seg = false)
        {
            b.setButtonText (text); b.getProperties().set ("seg", seg); addAndMakeVisible (b);
        };
        pill (modeBtn, adv ? "Simple" : "Advanced");
        modeBtn.onClick = [this] { proc.uiAdvanced = ! proc.uiAdvanced; changed(); };
        pill (lightBtn, "Light", true); pill (darkBtn, "Dark", true);
        lightBtn.setToggleState (! proc.uiDark, dontSendNotification); darkBtn.setToggleState (proc.uiDark, dontSendNotification);
        lightBtn.onClick = [this] { proc.uiDark = false; changed(); };
        darkBtn.onClick  = [this] { proc.uiDark = true;  changed(); };
        pill (micBtn, "Microphones", true); pill (pianistBtn, "Pianist", true);
        perspAtt = std::make_unique<ParameterAttachment> (*proc.state.getParameter ("perspective"), [this] (float v)
        {
            micBtn.setToggleState (v < 0.5f, dontSendNotification); pianistBtn.setToggleState (v >= 0.5f, dontSendNotification);
        });
        perspAtt->sendInitialUpdate();
        micBtn.onClick     = [this] { perspAtt->setValueAsCompleteGesture (0); };
        pianistBtn.onClick = [this] { perspAtt->setValueAsCompleteGesture (1); };
        pill (softBtn, adv ? "Soft pedal" : "Soft");
        softBtn.setClickingTogglesState (true);
        softAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.state, "soft", softBtn);
        addAndMakeVisible (keyboard);

        auto add = [this] (const char* id, const String& cap, Control::Kind k, Rectangle<int> r)
        {
            auto c = std::make_unique<Control> (proc, lnf, id, cap, k);
            c->setBounds (r); addAndMakeVisible (*c); controls.push_back (std::move (c));
        };

        if (! adv)
        {
            modeBtn.setBounds (32, 22, 110, 34);
            lightBtn.setBounds (980, 26, 46, 26); darkBtn.setBounds (1026, 26, 46, 26);
            micBtn.setBounds (448, 140, 104, 26); pianistBtn.setBounds (552, 140, 80, 26);
            for (int i = 0; i < 3; ++i)
            {
                static const char* idz[] = { "hardness", "brightness", "sympathetic" };
                static const char* caps[] = { "Hammer", "Tone", "Resonance" };
                add (idz[i], caps[i], Control::bigDial, { 550 - 77 + (i - 1) * 218, 184, 154, 210 });
            }
            add ("volume", "Volume", Control::vFader, { 976, 192, 96, 214 });
            keyboard.setBounds (0, 414, 1100, 196);
            softBtn.setBounds (318, 626, 70, 32);
            add ("sustain", "Sustain", Control::hSlider, { 400, 622, 430, 40 });
        }
        else
        {
            modeBtn.setBounds (996, 12, 80, 34);
            micBtn.setBounds (392, 16, 104, 26); pianistBtn.setBounds (496, 16, 80, 26);
            lightBtn.setBounds (872, 16, 46, 26); darkBtn.setBounds (918, 16, 46, 26);
            overtones.setBounds (24, 70, 780, 286); addAndMakeVisible (overtones);
            curve.setBounds (24, 366, 780, 112);    addAndMakeVisible (curve);
            keyboard.setBounds (24, 494, 780, 172);
            struct G { const char* title; std::vector<std::pair<const char*, const char*>> knobs; };
            const G groups[] = { { "Hammer", { { "hardness", "Hardness" }, { "curve", "Touch" } } },
                                 { "Tuning", { { "a4", "A4 pitch" }, { "stretch", "Stretch" } } },
                                 { "Resonance", { { "brightness", "Brightness" }, { "sympathetic", "Sympathetic" } } },
                                 { "Pedals and output", { { "sustain", "Sustain" }, { "volume", "Volume" }, { "width", "Width" } } } };
            int y = 70;
            for (auto& gp : groups)
            {
                groupTitles.push_back ({ gp.title, y });
                const int n = (int) gp.knobs.size(), w = 80, gap = (248 - n * w) / (n + 1);
                for (int i = 0; i < n; ++i)
                    add (gp.knobs[(size_t) i].first, gp.knobs[(size_t) i].second, Control::smallKnob, { 828 + gap + i * (w + gap), y + 26, w, 96 });
                y += 124;
            }
            softBtn.setBounds (828 + 74, y - 4, 100, 32);
        }
        overtones.p = &proc; overtones.l = &lnf; curve.p = &proc; curve.l = &lnf;
    }

    void paint (Graphics& g) override
    {
        auto& t = lnf.t;
        g.setGradientFill (ColourGradient::vertical (t.bg, 0, t.bg2, (float) baseH));
        g.fillAll();
        const bool adv = proc.uiAdvanced;
        auto segOutline = [&] (Rectangle<int> r) { g.setColour (t.line); g.drawRoundedRectangle (r.toFloat().expanded (2.5f), 15.0f, 1.0f); };
        g.setColour (t.ink);
        if (! adv)
        {
            g.setFont (Font (FontOptions (lnf.italiana).withHeight (72.0f).withKerningFactor (0.32f)));
            g.drawText ("FORTE", Rectangle<int> (24, 30, baseW, 80), Justification::centredTop);
            g.setColour (t.muted); g.setFont (lnf.jostFont (14.0f));
            g.drawText (String::fromUTF8 ("\xe2\x80\xb9    Concert Grand    \xe2\x80\xba"), Rectangle<int> (0, 108, baseW, 22), Justification::centred);
            segOutline ({ 448, 140, 184, 26 }); segOutline ({ 980, 26, 92, 26 });
            g.setColour (Colour (0xff9e1b32)); g.fillRect (0, 404, baseW, 10);
            g.setColour (t.muted); g.setFont (lnf.jostFont (11.0f));
            const int sx = 476, sw = 310;
            g.drawText ("Up", sx - 10, 656, 30, 14, Justification::left);
            g.drawText ("Half", sx + sw / 2 - 20, 656, 40, 14, Justification::centred);
            g.drawText ("Down", sx + sw - 24, 656, 40, 14, Justification::right);
        }
        else
        {
            g.setFont (Font (FontOptions (lnf.italiana).withHeight (28.0f).withKerningFactor (0.28f)));
            g.drawText ("FORTE", 24, 14, 170, 32, Justification::centredLeft);
            g.setColour (t.muted); g.setFont (lnf.jostFont (14.0f));
            g.drawText (String::fromUTF8 ("\xe2\x80\xb9   Concert Grand   \xe2\x80\xba"), 196, 16, 180, 26, Justification::centredLeft);
            g.setFont (lnf.jostFont (13.0f));
            g.drawText ("Voices " + String (proc.viz.activeVoices.load()), 680, 16, 120, 26, Justification::centredRight);
            segOutline ({ 392, 16, 184, 26 }); segOutline ({ 872, 16, 92, 26 });
            g.setColour (t.line); g.drawHorizontalLine (58, 24, (float) baseW - 24);
            g.setColour (Colour (0xff9e1b32)); g.fillRoundedRectangle (24, 488, 780, 6, 2);
            for (auto& [title, y] : groupTitles)
            {
                g.setColour (t.muted); g.setFont (lnf.jostFont (13.0f, true));
                g.drawText (title, 828, y, 248, 20, Justification::centredLeft);
                g.setColour (t.line); g.drawHorizontalLine (y + 22, 828, 1076);
            }
        }
    }

    struct Panel : public Component
    {
        ProtoProcessor* p = nullptr; ForteLnf* l = nullptr;
        void panel (Graphics& g)
        {
            g.setColour (l->t.panel); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10);
            g.setColour (l->t.line);  g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10, 1);
        }
        void caption (Graphics& g, const String& a, const String& b)
        {
            g.setColour (l->t.ink); g.setFont (l->jostFont (14.0f, true));
            const int w = GlyphArrangement::getStringWidthInt (l->jostFont (14.0f, true), a);
            g.drawText (a, 14, 8, w + 4, 20, Justification::centredLeft);
            g.setColour (l->t.muted); g.setFont (l->jostFont (12.0f));
            g.drawText (b, 14 + w + 10, 9, 700, 20, Justification::centredLeft);
        }
    };

    struct Overtones : public Panel
    {
        void paint (Graphics& g) override
        {
            panel (g);
            auto& t = l->t; auto& v = p->viz;
            const float W = 780, H = 286, x0 = 40, x1 = W - 24, base = H - 40, top = 48;
            g.setColour (t.line); g.drawHorizontalLine ((int) base, x0, x1);
            const int np = v.numParts.load();
            if (np == 0) { caption (g, "Overtones", "Play a note to see its overtones"); return; }
            const float f0 = v.lastF0.load(), last = v.partFreq[(size_t) np - 1].load();
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int m = v.lastMidi.load();
            caption (g, "Overtones of " + String (names[m % 12]) + String (m / 12 - 1),
                     String (roundToInt (f0)) + " Hz. Overtone " + String (np) + " sits "
                     + String (roundToInt (1200.0f * std::log2 (last / (np * f0)))) + " cents above a perfect harmonic.");
            auto X = [&] (float f) { return x0 + f / (last * 1.06f) * (x1 - x0); };
            g.setFont (l->jostFont (11.0f));
            for (int k = 0; k < np; ++k)
            {
                const float xi = X ((k + 1) * f0), xa = X (v.partFreq[(size_t) k].load());
                Path dash, ln; float dl[] = { 3, 3 };
                ln.startNewSubPath (xi, top); ln.lineTo (xi, base);
                PathStrokeType (1.0f).createDashedStroke (dash, ln, dl, 2);
                g.setColour (t.muted.withAlpha (0.5f)); g.fillPath (dash);
                const float a = v.partAmp[(size_t) k].load();
                const float h = jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (jmax (a, 1e-6f) / 0.24f) / 60.0f) * (base - top);
                g.setColour (t.accent); g.fillRoundedRectangle (xa - 2.5f, base - h, 5, h, 2);
                g.setColour (t.muted); g.drawText (String (k + 1), (int) xa - 12, (int) base + 5, 24, 14, Justification::centred);
            }
        }
    };

    struct Curve : public Panel
    {
        void paint (Graphics& g) override
        {
            panel (g);
            caption (g, "Tuning curve", "Cents away from equal temperament, across all 88 notes");
            auto& t = l->t;
            const float x0 = 16, x1 = 780 - 16, zy = 84, sc = 0.75f, st = p->value ("stretch") / 50.0f;
            g.setFont (l->jostFont (10.0f));
            for (int ct = -20; ct <= 60; ct += 20)
            {
                g.setColour (ct ? t.line : t.muted); g.drawHorizontalLine ((int) (zy - ct * sc), x0, x1 - 26);
                g.setColour (t.muted); g.drawText ((ct > 0 ? "+" : "") + String (ct), (int) x1 - 30, (int) (zy - ct * sc) - 7, 30, 14, Justification::centredRight);
            }
            Path pth;
            for (int m = 21; m <= 108; ++m)
            {
                const Point<float> pt (x0 + (m - 21) / 87.0f * (x1 - x0), zy - ProtoProcessor::railsback (m) * st * sc);
                if (m == 21) pth.startNewSubPath (pt); else pth.lineTo (pt);
            }
            g.setColour (t.accent); g.strokePath (pth, PathStrokeType (2.0f));
        }
    };

    ProtoProcessor& proc; ForteLnf& lnf;
    HeatKeyboard keyboard;
    std::function<void()> changed;
    TextButton modeBtn, lightBtn, darkBtn, micBtn, pianistBtn, softBtn;
    std::unique_ptr<ParameterAttachment> perspAtt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> softAtt;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::pair<String, int>> groupTitles;
    Overtones overtones; Curve curve;
};

//============================================================================== editor
NativeEditor::NativeEditor (ProtoProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    rebuild();
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 7 / 10, baseH * 7 / 10, baseW * 3 / 2, baseH * 3 / 2);
    setSize (baseW, baseH);
    startTimerHz (30);
}

NativeEditor::~NativeEditor() { stopTimer(); content.reset(); setLookAndFeel (nullptr); }

void NativeEditor::rebuild()
{
    lnf.t = proc.uiDark ? Theme::dark() : Theme::light();
    // rebuilding after the click that triggered it, not inside it
    content.reset();
    content = std::make_unique<Content> (proc, lnf, [this] { MessageManager::callAsync ([sp = SafePointer<NativeEditor> (this)] { if (sp) sp->rebuild(); }); });
    content->setBounds (0, 0, baseW, baseH);
    addAndMakeVisible (*content);
    resized();
}

void NativeEditor::paint (Graphics& g)
{
    g.fillAll (lnf.t.bg);
    if (! firstPaintLogged)
    {
        firstPaintLogged = true;
        File::getSpecialLocation (File::tempDirectory).getChildFile ("forte_proto_startup.txt")
            .appendText ("native " + String (Time::currentTimeMillis() - proc.editorOpenedMs) + "\n");
    }
}

void NativeEditor::resized()
{
    if (content) content->setTransform (AffineTransform::scale ((float) getWidth() / baseW));
}

void NativeEditor::timerCallback()
{
    if (! content) return;
    content->keyboard.repaint();
    if (proc.uiAdvanced) { content->overtones.repaint(); content->curve.repaint(); content->repaint (680, 16, 120, 26); }
}
