#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LabParameters.h"

#include "forte/engine/Engine.h"

namespace forte
{
/** The VST3/Standalone wrapper around forte::Engine. It holds no DSP of its own.

    Parameters: so far only the voicing lab's (SPEC §6.5), shown by the host's generic editor
    until the custom editor exists (SPEC §6.1). The project state is their values plus a
    state-format version (SPEC §7.3).

    Export voicing (SPEC §6.5) is polled on the message thread, never handled on the audio
    thread: when it's on, the processor writes Documents/Forte/voicing.json and turns it off.
    With the environment variable FORTE_NO_VOICING_EXPORT set (the tests set it for pluginval,
    which flips every parameter), it only turns it off. */
class PluginProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Hosts use this to keep rendering after the last note, e.g. a pedalled A0 ringing out.
    double getTailLengthSeconds() const override { return engine.getTailSeconds(); }

    // One fixed program. MIDI program change is out of scope (SPEC §1), and presets are a separate
    // system (SPEC §7.1). VST3 requires every program to have a name.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** The saved state's format (SPEC §7.3). A newer Forte reads older states; an older one
        ignores what it doesn't know. */
    static constexpr int stateVersion = 1;

private:
    void timerCallback() override;

    Engine engine;
    juce::AudioProcessorValueTreeState parameters;
    LabParameters lab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace forte
