#include "VoicingExport.h"

#include "forte/BuildCommit.h"
#include "forte/Version.h"
#include "forte/bridge/VoicingFile.h"

namespace forte
{
juce::String makeVoicingFile (const Engine& engine, const lab::Settings& settings)
{
    using bridge::toJuceString;

    auto* anchors = new juce::DynamicObject();
    for (int r = 0; r < lab::numRegisters; ++r)
        anchors->setProperty (toJuceString (lab::registerIds[static_cast<std::size_t> (r)]),
                              lab::registerAnchorKeys[static_cast<std::size_t> (r)]);

    auto* physical = new juce::DynamicObject();
    auto* ranges = new juce::DynamicObject();

    for (const auto& c : lab::controls)
    {
        const auto id = toJuceString (c.id);
        ranges->setProperty (id, toJuceString (c.range));

        if (! c.perRegister)
        {
            physical->setProperty (id, engine.getLabPhysicalValue (settings, c.control, 60));
            continue;
        }

        auto* registers = new juce::DynamicObject();
        for (int r = 0; r < lab::numRegisters; ++r)
            registers->setProperty (toJuceString (lab::registerIds[static_cast<std::size_t> (r)]),
                                    engine.getLabPhysicalValue (settings, c.control, lab::registerAnchorKeys[static_cast<std::size_t> (r)]));
        physical->setProperty (id, juce::var (registers));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", bridge::voicingFormat);
    root->setProperty ("version", bridge::voicingFormatVersion);
    root->setProperty ("forteVersion", versionString);
    root->setProperty ("builtFrom", buildCommit);
    root->setProperty ("voicingRevision", Engine::getVoicingRevision());
    root->setProperty ("exported", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("registerAnchorKeys", juce::var (anchors));
    root->setProperty ("controls", bridge::voicingControlsToVar (settings));
    root->setProperty ("physical", juce::var (physical));
    root->setProperty ("ranges", juce::var (ranges));

    return juce::JSON::toString (juce::var (root));
}

juce::File defaultVoicingFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Forte").getChildFile ("voicing.json");
}

juce::Result exportVoicing (const Engine& engine, const lab::Settings& settings, const juce::File& target)
{
    if (const auto made = target.getParentDirectory().createDirectory(); made.failed())
        return made;

    if (target.existsAsFile())
    {
        const auto previous = target.getSiblingFile (target.getFileNameWithoutExtension() + ".previous.json");
        if (! target.copyFileTo (previous))
            return juce::Result::fail ("can't keep the old voicing as " + previous.getFullPathName());
    }

    juce::TemporaryFile temporary (target);
    if (! temporary.getFile().replaceWithText (makeVoicingFile (engine, settings)) || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("can't write " + target.getFullPathName());

    return juce::Result::ok();
}
} // namespace forte
