#include "PluginProcessor.h"

#include "VoicingExport.h"

#include "forte/bridge/RenderBlock.h"

namespace forte
{
namespace
{
const juce::Identifier stateType { "FORTE" };
const juce::Identifier stateVersionAttribute { "stateVersion" };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (LabParameters::createGroup());
    return layout;
}
} // namespace

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, createLayout())
{
    lab.attach (parameters);
    startTimerHz (10);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
}

void PluginProcessor::timerCallback()
{
    auto& button = lab.exportParameter();
    if (button.getValue() < 0.5f)
        return;

    if (juce::SystemStats::getEnvironmentVariable ("FORTE_NO_VOICING_EXPORT", {}).isEmpty())
    {
        const auto result = exportVoicing (engine, lab.read(), defaultVoicingFile());
        if (result.failed())
            DBG ("Forte: Export voicing failed: " << result.getErrorMessage());
    }

    button.setValueNotifyingHost (0.0f);
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Lab values land at once here, without the tape's glide.
    engine.setVoicingLab (lab.read());
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
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

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // The lab's values apply from this block's start (SPEC §6.5). Cheap when none changed.
    engine.setVoicingLab (lab.read());
    bridge::renderBlock (engine, buffer, midi);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    // The host-style generic editor until the custom one exists (SPEC §6.1, §6.5), so the
    // Standalone shows the lab too.
    return new juce::GenericAudioProcessorEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionAttribute, stateVersion, nullptr);

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

    parameters.replaceState (juce::ValueTree::fromXml (*xml));

    // A project saved mid-export must not export again when it opens.
    lab.exportParameter().setValueNotifyingHost (0.0f);
}
} // namespace forte

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new forte::PluginProcessor();
}
