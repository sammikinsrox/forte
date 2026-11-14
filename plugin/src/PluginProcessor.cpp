#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "forte/bridge/RenderBlock.h"

namespace forte
{
PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
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
    bridge::renderBlock (engine, buffer, midi);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // No saved state yet. The state format arrives with the parameters (SPEC §7.3).
    destData.reset();
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}
} // namespace forte

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new forte::PluginProcessor();
}
