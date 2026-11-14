// PROTOTYPE (throwaway): toy additive "piano" used only to drive the two editor variants.
// Not Forte's physical model.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "BinaryData.h"

namespace ids
{
    // id, display name, min, max, default (all whole-number steps)
    struct Knob { const char* id; const char* name; float lo, hi, def; const char* unit; };
    inline constexpr Knob knobs[] = {
        { "hardness",    "Felt hardness", 0,   100, 55,  "" },
        { "curve",       "Touch curve",   0,   100, 50,  "" },
        { "a4",          "A4 pitch",      430, 450, 440, "Hz" },
        { "stretch",     "Stretch",       0,   100, 50,  "" },
        { "brightness",  "Brightness",    0,   100, 50,  "" },
        { "sympathetic", "Sympathetic",   0,   100, 40,  "" },
        { "sustain",     "Sustain",       0,   100, 0,   "" },
        { "volume",      "Volume",        -60, 1,   0,   "dB" },
        { "width",       "Width",         0,   100, 100, "" },
    };
}

struct Viz   // written by the audio thread, read by editors
{
    std::array<std::atomic<float>, 88> energy {};
    std::atomic<int> lastMidi { 36 };
    std::atomic<float> lastF0 { 65.4f };
    std::atomic<int> numParts { 0 };
    std::array<std::atomic<float>, 20> partFreq {}, partAmp {};
    std::atomic<int> activeVoices { 0 };
};

class ProtoProcessor : public juce::AudioProcessor
{
public:
    ProtoProcessor();
    void prepareToPlay (double sr, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Concert Grand"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float value (const char* id) const { return state.getRawParameterValue (id)->load(); }
    static float inharm (int m)    { return 0.00008f * std::pow (10.0f, (m - 21) / 87.0f * 2.3f); }
    static float railsback (int m) { return m > 69 ? 30.0f * std::pow ((m - 69) / 39.0f, 2.2f) : -15.0f * std::pow ((69 - m) / 48.0f, 2.0f); }

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboard;
    Viz viz;
    // editor-only UI state, kept here so it survives closing/reopening the editor
    bool uiAdvanced = false, uiDark = false;
    juce::int64 editorOpenedMs = 0;   // for startup timing

private:
    struct Partial { float c = 1, s = 0, cr = 1, sr = 0, amp = 0, d1 = 1, d2 = 1, initAmp = 0; int t1Samples = 0; };
    struct Voice { int midi = -1; int age = 0; bool held = false, damped = false; float damp = 1, dampMul = 1, vel = 0; int numParts = 0; std::array<Partial, 40> p; };
    void noteOn (int m, float vel);
    void noteOff (int m);
    void damp (Voice&);
    std::array<Voice, 24> voices;
    double sr = 48000;
    int autoplayCounter = 0, autoplayStep = 0;
    bool autoplay = false;
    float lastSustain = 0;
};
