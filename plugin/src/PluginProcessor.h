#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "LabParameters.h"
#include "MachineSettings.h"
#include "SoundParameters.h"
#include "VoicingWorker.h"

#include "forte/engine/Engine.h"

#include <atomic>

namespace forte
{
/** The VST3/Standalone wrapper around forte::Engine. It holds no DSP of its own.

    Parameters: the 10 sound controls (SPEC §6.3) and the voicing lab's (SPEC §6.5). The project
    state is their values, the editor's view and a state-format version (SPEC §7.3).

    MIDI: the host's, with the pedal inverts applied (SPEC §4.2), plus the editor's keyboard and
    on-screen pedals. The on-screen pedals and MIDI share one value per pedal, and the latest
    message wins.

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
        ignores what it doesn't know. 2: the sound controls and the editor's view. */
    static constexpr int stateVersion = 2;

    // --- For the editor (message thread unless noted) -------------------------------------

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return parameters; }
    [[nodiscard]] const Engine& getEngine() const noexcept { return engine; }
    [[nodiscard]] const LabParameters& getLab() const noexcept { return lab; }
    juce::MidiKeyboardState keyboardState;

    enum class View { simple, advanced, lab };
    std::atomic<View> view { View::simple };  // saved with the project; any thread

    /** The pedals as the engine last got them, 0–127 after the inverts. Any thread. */
    [[nodiscard]] int getSustain() const noexcept { return liveSustain.load (std::memory_order_relaxed); }
    [[nodiscard]] int getSoft() const noexcept { return liveSoft.load (std::memory_order_relaxed); }

    /** The on-screen pedals: sent at the next block's start, as if from MIDI. Any thread. */
    void sendSustain (int value) noexcept { pendingSustain.store (juce::jlimit (0, 127, value), std::memory_order_relaxed); }
    void sendSoft (int value) noexcept { pendingSoft.store (juce::jlimit (0, 127, value), std::memory_order_relaxed); }

    /** The share of one core the last blocks took, 0–1, smoothed. Any thread. */
    [[nodiscard]] float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }

    [[nodiscard]] const MachineSettings& getMachineSettings() const noexcept { return machine; }
    void setMachineSettings (const MachineSettings& settings);

    /** The voicing lab's A/B (SPEC §6.5): while comparing, the engine plays the committed
        voicing (every lab value at 50) and the lab's values wait. Not saved. */
    void setComparingCommitted (bool comparing) noexcept { comparingCommitted.store (comparing, std::memory_order_relaxed); }
    [[nodiscard]] bool isComparingCommitted() const noexcept { return comparingCommitted.load (std::memory_order_relaxed); }

    /** Asks for an Export voicing (SPEC §6.5), as the Export voicing parameter does. */
    void requestExport();

    struct ExportResult
    {
        bool ok = false;
        juce::String message;  // what happened, for the editor
        juce::Time when;
    };

    /** The last Export voicing's result, or an empty message if there's been none. */
    [[nodiscard]] const ExportResult& getLastExport() const noexcept { return lastExport; }

private:
    void timerCallback() override;

    /** The lab values the engine plays: the lab's, or all 50 while comparing. Realtime-safe. */
    [[nodiscard]] lab::Settings labForEngine() const noexcept;

    void applyPedalMidi (juce::MidiBuffer& midi, int numSamples) noexcept;

    Engine engine;
    juce::AudioProcessorValueTreeState parameters;
    SoundParameters sound;
    LabParameters lab;

    MachineSettings machine = MachineSettings::load();
    std::atomic<bool> invertSustain { false }, invertSoft { false };
    std::atomic<int> liveSustain { 0 }, liveSoft { 0 };
    std::atomic<int> pendingSustain { -1 }, pendingSoft { -1 };
    std::atomic<bool> comparingCommitted { false };
    std::atomic<float> cpuLoad { 0.0f };
    double blockSecondsPerSample = 1.0 / 48000.0;
    juce::MidiBuffer pedalMidi;  // reused, sized in prepareToPlay()

    ExportResult lastExport;

    // Held by prepareToPlay() and the voicing worker, never by the audio thread.
    juce::CriticalSection engineLock;
    VoicingWorker voicingWorker { engine, [this] { return labForEngine(); }, engineLock };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace forte
