// PROTOTYPE (throwaway)
#include "WebEditor.h"

static constexpr int baseW = 1100, baseH = 690;

static std::vector<std::byte> toBytes (const char* data, int size)
{
    std::vector<std::byte> v ((size_t) size);
    std::memcpy (v.data(), data, (size_t) size);
    return v;
}

juce::WebBrowserComponent::Options WebEditor::makeOptions()
{
    using namespace juce;
    auto opts = WebBrowserComponent::Options{}
        .withBackend (WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (WebBrowserComponent::Options::WinWebView2{}
                                    .withUserDataFolder (File::getSpecialLocation (File::tempDirectory).getChildFile ("ForteProtoWebView2"))
                                    .withStatusBarDisabled()
                                    .withBackgroundColour (Colour (0xfff4f4f1)))
        .withNativeIntegrationEnabled()
        .withOptionsFrom (softRelay)
        .withOptionsFrom (perspectiveRelay)
        .withNativeFunction ("noteOn", [this] (const Array<var>& a, auto complete)
        {
            proc.keyboard.noteOn (1, (int) a[0], (float) (double) a[1]);
            complete ({});
        })
        .withNativeFunction ("noteOff", [this] (const Array<var>& a, auto complete)
        {
            proc.keyboard.noteOff (1, (int) a[0], 0.0f);
            complete ({});
        })
        .withNativeFunction ("uiState", [this] (const Array<var>& a, auto complete)
        {
            proc.uiAdvanced = (bool) a[0]; proc.uiDark = (bool) a[1];
            complete ({});
        })
        .withNativeFunction ("ready", [this] (const Array<var>&, auto complete)
        {
            // startup timing: editor constructed -> page fully running
            Logger::writeToLog ("FORTE_PROTO web editor ready after "
                                + String (Time::currentTimeMillis() - proc.editorOpenedMs) + " ms");
            File::getSpecialLocation (File::tempDirectory).getChildFile ("forte_proto_startup.txt")
                .appendText ("web " + String (Time::currentTimeMillis() - proc.editorOpenedMs) + "\n");
            DynamicObject::Ptr o = new DynamicObject();
            o->setProperty ("advanced", proc.uiAdvanced);
            o->setProperty ("dark", proc.uiDark);
            complete (var (o.get()));
        })
        .withResourceProvider ([this] (const auto& url) { return getResource (url); });

    for (auto& r : sliderRelays) opts = opts.withOptionsFrom (*r);
    return opts;
}

WebEditor::WebEditor (ProtoProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto& k : ids::knobs)
        sliderRelays.push_back (std::make_unique<juce::WebSliderRelay> (k.id));

    browser = std::make_unique<juce::WebBrowserComponent> (makeOptions());

    for (size_t i = 0; i < sliderRelays.size(); ++i)
        sliderAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (
            *proc.state.getParameter (ids::knobs[i].id), *sliderRelays[i]));
    softAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment> (*proc.state.getParameter ("soft"), softRelay);
    perspectiveAttachment = std::make_unique<juce::WebComboBoxParameterAttachment> (*proc.state.getParameter ("perspective"), perspectiveRelay);

    addAndMakeVisible (*browser);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 7 / 10, baseH * 7 / 10, baseW * 3 / 2, baseH * 3 / 2);
    setSize (baseW, baseH);
    startTimerHz (30);
}

WebEditor::~WebEditor() { stopTimer(); }

void WebEditor::resized() { browser->setBounds (getLocalBounds()); }

void WebEditor::timerCallback()
{
    using namespace juce;
    DynamicObject::Ptr o = new DynamicObject();
    Array<var> energy;
    for (auto& e : proc.viz.energy) energy.add (e.load());
    o->setProperty ("energy", energy);
    o->setProperty ("m", proc.viz.lastMidi.load());
    o->setProperty ("f0", proc.viz.lastF0.load());
    o->setProperty ("voices", proc.viz.activeVoices.load());
    Array<var> parts;
    const int np = proc.viz.numParts.load();
    for (int k = 0; k < np; ++k)
        parts.add (Array<var> { proc.viz.partFreq[(size_t) k].load(), proc.viz.partAmp[(size_t) k].load() });
    o->setProperty ("parts", parts);
    browser->emitEventIfBrowserIsVisible ("viz", var (o.get()));
}

std::optional<juce::WebBrowserComponent::Resource> WebEditor::getResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String ("index.html") : url.fromFirstOccurrenceOf ("/", false, false);
    if (path == "index.html")   return juce::WebBrowserComponent::Resource { toBytes (BinaryData::index_html, BinaryData::index_htmlSize), "text/html" };
    if (path == "juce/index.js") return juce::WebBrowserComponent::Resource { toBytes (BinaryData::index_js, BinaryData::index_jsSize), "text/javascript" };
    if (path == "fonts/Italiana.ttf") return juce::WebBrowserComponent::Resource { toBytes (BinaryData::ItalianaRegular_ttf, BinaryData::ItalianaRegular_ttfSize), "font/ttf" };
    if (path == "fonts/Jost.ttf") return juce::WebBrowserComponent::Resource { toBytes (BinaryData::Jost_ttf, BinaryData::Jost_ttfSize), "font/ttf" };
    return std::nullopt;
}
