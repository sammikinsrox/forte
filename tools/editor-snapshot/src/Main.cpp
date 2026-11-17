// forte-editor-snapshot <output folder> [scale]: the editor's three views, light and dark, as
// <view>-<theme>.png, plus the lab with some values moved and the Advanced view with the tuning
// changed. Nothing is played; the editor is drawn as it first opens.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

namespace
{
void save (juce::Component& editor, const juce::File& file, float scale)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
}

void setParameter (forte::PluginProcessor& processor, const juce::String& id, float value)
{
    if (auto* p = processor.getState().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

/** Lets the editor's async work (view switches, attachments) and timers run. */
void settle()
{
    for (int i = 0; i < 8; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "Usage: forte-editor-snapshot <output folder> [scale]\n");
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]));
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    folder.createDirectory();

    // The snapshots mustn't change the user's own settings file.
    const auto settingsFile = forte::MachineSettings::file();
    const auto savedSettings = settingsFile.existsAsFile() ? settingsFile.loadFileAsString() : juce::String();

    for (const bool dark : { false, true })
    {
        auto processor = std::make_unique<forte::PluginProcessor>();
        auto machine = processor->getMachineSettings();
        machine.darkTheme = dark;
        processor->setMachineSettings (machine);

        const char* views[] { "simple", "advanced", "lab" };
        for (int v = 0; v < 3; ++v)
        {
            processor->view.store (static_cast<forte::PluginProcessor::View> (v));
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
            settle();
            save (*editor, folder.getChildFile (juce::String (views[v]) + "-" + (dark ? "dark" : "light") + ".png"), scale);
        }

        // The lab mid-voicing, and the tuning moved.
        setParameter (*processor, "lab.feltHardness.treble", 30.0f);
        setParameter (*processor, "lab.feltHardness.bass", 62.0f);
        setParameter (*processor, "lab.sustain.middle", 70.0f);
        setParameter (*processor, "lab.pickup.treble", 20.0f);
        setParameter (*processor, "lab.tapeDrive", 40.0f);
        setParameter (*processor, "stretch", 80.0f);
        setParameter (*processor, "a4", 442.0f);
        setParameter (*processor, "brightness", 64.0f);

        for (int v = 1; v < 3; ++v)
        {
            processor->view.store (static_cast<forte::PluginProcessor::View> (v));
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
            settle();
            save (*editor, folder.getChildFile (juce::String (views[v]) + "-moved-" + (dark ? "dark" : "light") + ".png"), scale);
        }
    }

    if (savedSettings.isNotEmpty())
        settingsFile.replaceWithText (savedSettings);
    else
        settingsFile.deleteFile();

    return 0;
}
