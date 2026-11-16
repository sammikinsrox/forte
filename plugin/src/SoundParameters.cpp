#include "SoundParameters.h"

namespace forte
{
namespace
{
constexpr int parameterVersion = 1;

std::unique_ptr<juce::AudioParameterInt> wholeNumber (const char* id, const juce::String& name, int minimum, int maximum,
                                                      int defaultValue, const juce::String& unit = {})
{
    return std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { id, parameterVersion }, name, minimum, maximum, defaultValue,
        juce::AudioParameterIntAttributes().withLabel (unit));
}
} // namespace

juce::String SoundParameters::volumeText (float decibels)
{
    if (decibels <= -60.0f + 1e-3f)
        return "Off";

    return juce::String (decibels, 1) + " dB";
}

std::unique_ptr<juce::AudioProcessorParameterGroup> SoundParameters::createGroup()
{
    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("piano", "Piano", " | ");

    group->addChild (wholeNumber (Ids::hardness, "Hardness", 0, 100, 50));
    group->addChild (wholeNumber (Ids::brightness, "Brightness", 0, 100, 50));
    group->addChild (wholeNumber (Ids::resonance, "Resonance", 0, 100, 50));

    group->addChild (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { Ids::volume, parameterVersion }, "Volume",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return volumeText (v); })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.trim().equalsIgnoreCase ("off") ? -60.0f : text.getFloatValue();
            })));

    group->addChild (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { Ids::perspective, parameterVersion }, "Listening perspective",
        juce::StringArray { "Microphones", "Pianist" }, 0));

    group->addChild (wholeNumber (Ids::touch, "Touch", 0, 100, 50));
    group->addChild (wholeNumber (Ids::a4, "A4", 415, 466, 440, "Hz"));
    group->addChild (wholeNumber (Ids::stretch, "Stretch", 0, 100, 50));
    group->addChild (wholeNumber (Ids::spread, "Spread", 0, 100, 50));
    group->addChild (wholeNumber (Ids::width, "Width", 0, 100, 100));
    return group;
}

void SoundParameters::attach (juce::AudioProcessorValueTreeState& apvts)
{
    const auto find = [&] (const char* id)
    {
        auto* value = apvts.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };

    hardness = find (Ids::hardness);
    brightness = find (Ids::brightness);
    resonance = find (Ids::resonance);
    volume = find (Ids::volume);
    perspective = find (Ids::perspective);
    touch = find (Ids::touch);
    a4 = find (Ids::a4);
    stretch = find (Ids::stretch);
    spread = find (Ids::spread);
    width = find (Ids::width);
}

Controls SoundParameters::read() const noexcept
{
    const auto get = [] (const std::atomic<float>* v) { return static_cast<double> (v->load (std::memory_order_relaxed)); };

    Controls c;
    c.hardness = get (hardness);
    c.brightness = get (brightness);
    c.resonance = get (resonance);
    c.touch = get (touch);
    c.tuning.a4Hz = get (a4);
    c.tuning.stretch = get (stretch);
    c.tuning.spread = get (spread);
    c.width = get (width);
    c.volumeDb = get (volume);
    return c;
}

ListeningPerspective SoundParameters::readPerspective() const noexcept
{
    return perspective->load (std::memory_order_relaxed) >= 0.5f ? ListeningPerspective::pianist
                                                                 : ListeningPerspective::microphones;
}
} // namespace forte
