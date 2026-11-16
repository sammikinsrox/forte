#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "VoicingExport.h"

#include "forte/bridge/RenderBlock.h"

namespace forte
{
namespace
{
const juce::Identifier stateType { "FORTE" };
const juce::Identifier stateVersionAttribute { "stateVersion" };
const juce::Identifier voicingRevisionAttribute { "voicingRevision" };  // absent: revision 0
const juce::Identifier viewAttribute { "view" };                        // absent: simple

constexpr int sustainController = 64;
constexpr int softController = 67;
constexpr int resetAllControllers = 121;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (SoundParameters::createGroup());
    layout.add (LabParameters::createGroup());
    return layout;
}

const char* viewName (PluginProcessor::View view)
{
    switch (view)
    {
        case PluginProcessor::View::advanced: return "advanced";
        case PluginProcessor::View::lab:      return "lab";
        case PluginProcessor::View::simple:   break;
    }
    return "simple";
}

PluginProcessor::View viewFromName (const juce::String& name)
{
    if (name == "advanced")
        return PluginProcessor::View::advanced;
    if (name == "lab")
        return PluginProcessor::View::lab;
    return PluginProcessor::View::simple;
}
} // namespace

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, createLayout())
{
    sound.attach (parameters);
    lab.attach (parameters);
    invertSustain = machine.invertSustain;
    invertSoft = machine.invertSoft;
    startTimerHz (10);
    voicingWorker.start();
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    voicingWorker.stop();
}

lab::Settings PluginProcessor::labForEngine() const noexcept
{
    return comparingCommitted.load (std::memory_order_relaxed) ? lab::Settings {} : lab.read();
}

void PluginProcessor::setMachineSettings (const MachineSettings& settings)
{
    machine = settings;
    invertSustain = settings.invertSustain;
    invertSoft = settings.invertSoft;
    machine.save();
}

void PluginProcessor::requestExport()
{
    lab.exportParameter().setValueNotifyingHost (1.0f);
}

void PluginProcessor::timerCallback()
{
    auto& button = lab.exportParameter();
    if (button.getValue() < 0.5f)
        return;

    lastExport.when = juce::Time::getCurrentTime();

    if (juce::SystemStats::getEnvironmentVariable ("FORTE_NO_VOICING_EXPORT", {}).isEmpty())
    {
        const auto target = defaultVoicingFile();
        const auto result = exportVoicing (engine, lab.read(), target);
        lastExport.ok = result.wasOk();
        lastExport.message = result.wasOk() ? "Saved " + target.getFullPathName() : "Export failed: " + result.getErrorMessage();
        if (result.failed())
        {
            DBG ("Forte: Export voicing failed: " << result.getErrorMessage());
        }
    }
    else
    {
        lastExport.ok = false;
        lastExport.message = "Export is turned off here (FORTE_NO_VOICING_EXPORT)";
    }

    button.setValueNotifyingHost (0.0f);
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Lab values and controls land at once here, without their glides, and every note is built
    // with the lab's strings and the tuning. The voicing worker stays out meanwhile.
    const juce::ScopedLock locked (engineLock);
    engine.setVoicingLab (labForEngine());
    engine.setControls (sound.read());
    engine.setListeningPerspective (sound.readPerspective());
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);

    blockSecondsPerSample = 1.0 / sampleRate;
    pedalMidi.ensureSize (4096);
    keyboardState.reset();
}

void PluginProcessor::releaseResources()
{
}

void PluginProcessor::reset()
{
    engine.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty()
        && layouts.outputBuses.size() == 1
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::applyPedalMidi (juce::MidiBuffer& midi, int numSamples) noexcept
{
    const int sustainFromScreen = pendingSustain.exchange (-1, std::memory_order_relaxed);
    const int softFromScreen = pendingSoft.exchange (-1, std::memory_order_relaxed);
    const bool invertingSustain = invertSustain.load (std::memory_order_relaxed);
    const bool invertingSoft = invertSoft.load (std::memory_order_relaxed);

    if (sustainFromScreen < 0 && softFromScreen < 0 && ! invertingSustain && ! invertingSoft)
    {
        // Nothing to change: only watch the pedals.
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isController())
            {
                const int number = message.getControllerNumber();
                if (number == sustainController)
                    liveSustain.store (message.getControllerValue(), std::memory_order_relaxed);
                else if (number == softController)
                    liveSoft.store (message.getControllerValue(), std::memory_order_relaxed);
                else if (number == resetAllControllers)
                    liveSustain = liveSoft = 0;
            }
        }
        return;
    }

    // The on-screen pedals come first, so a pedal message from the host in the same block wins.
    pedalMidi.clear();
    if (sustainFromScreen >= 0)
    {
        pedalMidi.addEvent (juce::MidiMessage::controllerEvent (1, sustainController, sustainFromScreen), 0);
        liveSustain.store (sustainFromScreen, std::memory_order_relaxed);
    }
    if (softFromScreen >= 0)
    {
        pedalMidi.addEvent (juce::MidiMessage::controllerEvent (1, softController, softFromScreen), 0);
        liveSoft.store (softFromScreen, std::memory_order_relaxed);
    }

    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        if (message.isController())
        {
            const int number = message.getControllerNumber();
            if (number == sustainController || number == softController)
            {
                const bool invert = number == sustainController ? invertingSustain : invertingSoft;
                const int value = invert ? 127 - message.getControllerValue() : message.getControllerValue();
                message = juce::MidiMessage::controllerEvent (message.getChannel(), number, value);
                (number == sustainController ? liveSustain : liveSoft).store (value, std::memory_order_relaxed);
            }
            else if (number == resetAllControllers)
            {
                liveSustain = liveSoft = 0;
            }
        }
        pedalMidi.addEvent (message, juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition));
    }

    midi.swapWith (pedalMidi);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto started = juce::Time::getHighResolutionTicks();
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    applyPedalMidi (midi, numSamples);

    // The lab's values and the controls apply from this block's start (SPEC §2.9, §6.5). Cheap
    // when none changed.
    engine.setVoicingLab (labForEngine());
    engine.setControls (sound.read());
    engine.setListeningPerspective (sound.readPerspective());
    bridge::renderBlock (engine, buffer, midi);

    if (numSamples > 0)
    {
        const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
        const auto load = static_cast<float> (seconds / (numSamples * blockSecondsPerSample));
        const float previous = cpuLoad.load (std::memory_order_relaxed);
        cpuLoad.store (previous + 0.1f * (load - previous), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionAttribute, stateVersion, nullptr);
    state.setProperty (voicingRevisionAttribute, Engine::getVoicingRevision(), nullptr);
    state.setProperty (viewAttribute, viewName (view.load()), nullptr);
    state.appendChild (presets.toState(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    // replaceState() leaves parameters the state doesn't mention at their current values, and
    // ignores ones this Forte doesn't know (SPEC §7.3). Reset first, so a missing one is at its
    // default rather than at whatever the last project left it.
    for (auto* parameter : getParameters())
        parameter->setValueNotifyingHost (parameter->getDefaultValue());

    auto state = juce::ValueTree::fromXml (*xml);

    // The preset rides beside the parameters, not in the parameters' own tree.
    const auto preset = state.getChildWithName (PresetManager::stateType);
    state.removeChild (preset, nullptr);
    presets.fromState (preset);

    parameters.replaceState (state);
    view.store (viewFromName (state.getProperty (viewAttribute, "simple").toString()));

    // Lab values move the voicing they were set on (SPEC §6.5). Once that voicing has been
    // folded into a newer one, they'd move it twice, so the lab starts again at its 50s.
    if (static_cast<int> (state.getProperty (voicingRevisionAttribute, 0)) != Engine::getVoicingRevision())
        for (auto* parameter : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ranged->getParameterID().startsWith ("lab."))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    // A project saved mid-export must not export again when it opens. (An open editor notices
    // the view on its own timer: hosts may call this off the message thread.)
    lab.exportParameter().setValueNotifyingHost (0.0f);
}
} // namespace forte

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new forte::PluginProcessor();
}
