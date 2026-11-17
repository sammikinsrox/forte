#include "Presets.h"

#include <algorithm>

namespace forte::presets
{
Values clamped (Values values) noexcept
{
    for (const auto& f : fields)
        values.*f.member = std::clamp (values.*f.member, f.minimum, f.maximum);
    return values;
}

Controls applyTo (const Values& values, Controls controls) noexcept
{
    controls.hardness = values.hardness;
    controls.brightness = values.brightness;
    controls.resonance = values.resonance;
    controls.touch = values.touch;
    controls.tuning.stretch = values.stretch;
    controls.tuning.spread = values.spread;
    controls.tuning.detune = values.detune;
    controls.width = values.width;
    controls.room = roomOf (values);
    return controls;
}

RoomControls roomOf (const Values& values) noexcept
{
    RoomControls r;
    r.amount = values.roomAmount;
    r.size = values.roomSize;
    r.decaySeconds = values.roomDecay / 10.0;
    r.damping = values.roomDamping;
    r.predelayMs = values.roomPredelay;
    r.early = values.roomEarly;
    return r;
}

const std::vector<Room>& roomPresets()
{
    // First-build values, to be set by ear. Amount, size, decay (tenths of s), damping,
    // pre-delay (ms), early.
    static const std::vector<Room> list {
        { "Dry", 0, 30, 8, 55, 4, 60 },
        { "Studio", 22, 30, 8, 55, 4, 60 },
        { "Chamber", 30, 45, 14, 50, 10, 55 },
        { "Recital Hall", 32, 62, 19, 45, 18, 50 },
        { "Concert Hall", 35, 80, 24, 45, 25, 45 },
        { "Cathedral", 45, 100, 55, 60, 40, 35 },
    };
    return list;
}

Values withRoom (Values values, const Room& room) noexcept
{
    values.roomAmount = room.amount;
    values.roomSize = room.size;
    values.roomDecay = room.decay;
    values.roomDamping = room.damping;
    values.roomPredelay = room.predelay;
    values.roomEarly = room.early;
    return values;
}

const Room* matchingRoom (const Values& values) noexcept
{
    for (const auto& room : roomPresets())
        if (withRoom (values, room) == values)
            return &room;
    return nullptr;
}

ListeningPerspective perspectiveOf (const Values& values) noexcept
{
    return values.perspective == 1 ? ListeningPerspective::pianist : ListeningPerspective::microphones;
}

const std::vector<Factory>& factoryPresets()
{
    // First-build values (SPEC §7.1, §10): the user sets the final list and numbers by ear.
    // Field order: hardness, brightness, resonance, touch, stretch, spread, width, perspective;
    // then a room preset by name (the default is Studio).
    const auto room = [] (const char* name)
    {
        for (const auto& r : roomPresets())
            if (juce::String (r.name) == name)
                return r;
        jassertfalse;
        return roomPresets().front();
    };

    static const std::vector<Factory> list {
        { "Concert Grand", {} },
        { "Pianist's Seat", withRoom ({ 50, 50, 50, 50, 50, 50, 100, 1 }, room ("Chamber")) },
        { "Mellow", withRoom ({ 35, 40, 55, 50, 50, 50, 100, 0 }, room ("Recital Hall")) },
        { "Bright", { 65, 62, 50, 50, 50, 50, 100, 0 } },
        { "Intimate", withRoom ({ 40, 45, 40, 50, 50, 40, 70, 1 }, room ("Dry")) },
        { "Honest Equal", { 50, 50, 50, 50, 0, 50, 100, 0 } },
        { "Concert Hall", withRoom ({ 50, 52, 55, 50, 50, 50, 100, 0 }, room ("Concert Hall")) },
    };
    return list;
}

//==============================================================================
juce::String toJson (const juce::String& name, const Values& values)
{
    auto* valuesObject = new juce::DynamicObject();
    for (const auto& f : fields)
        valuesObject->setProperty (f.id, values.*f.member);

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", fileFormat);
    root->setProperty ("version", fileFormatVersion);
    root->setProperty ("model", modelId);
    root->setProperty ("name", name);
    root->setProperty ("values", juce::var (valuesObject));
    return juce::JSON::toString (juce::var (root));
}

std::optional<Values> fromJson (const juce::String& text)
{
    const auto parsed = juce::JSON::parse (text);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr || root->getProperty ("format").toString() != fileFormat)
        return std::nullopt;

    // A newer format may add fields: read the ones this Forte knows.
    Values values;
    if (const auto* object = root->getProperty ("values").getDynamicObject())
    {
        for (const auto& f : fields)
        {
            const auto v = object->getProperty (f.id);
            if (v.isInt() || v.isInt64() || v.isDouble())
                values.*f.member = juce::roundToInt (static_cast<double> (v));
        }
    }
    return clamped (values);
}

juce::File defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Forte").getChildFile ("Presets");
}

//==============================================================================
Library::Library (juce::File f)
    : folder (std::move (f))
{
    rescan();
}

void Library::rescan()
{
    ids.clear();
    for (const auto& p : factoryPresets())
        ids.push_back ({ true, p.name });

    std::vector<juce::String> names;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        if (fromJson (file.loadFileAsString()).has_value())
            names.push_back (file.getFileNameWithoutExtension());

    std::sort (names.begin(), names.end(), [] (const juce::String& a, const juce::String& b)
    {
        const int c = a.compareIgnoreCase (b);
        return c != 0 ? c < 0 : a < b;
    });

    for (const auto& name : names)
        ids.push_back ({ false, name });
}

std::vector<juce::String> Library::userNames() const
{
    std::vector<juce::String> names;
    for (const auto& id : ids)
        if (! id.factory)
            names.push_back (id.name);
    return names;
}

Id Library::step (const Id& current, int delta) const
{
    const auto n = static_cast<int> (ids.size());
    const auto found = std::find (ids.begin(), ids.end(), current);
    if (found == ids.end())
        return delta >= 0 ? ids.front() : ids.back();

    const int at = static_cast<int> (found - ids.begin());
    return ids[static_cast<std::size_t> (((at + delta) % n + n) % n)];
}

std::optional<Values> Library::load (const Id& id) const
{
    if (id.factory)
    {
        for (const auto& p : factoryPresets())
            if (id.name == p.name)
                return p.values;
        return std::nullopt;
    }

    const auto file = fileFor (id.name);
    if (! file.existsAsFile())
        return std::nullopt;
    return fromJson (file.loadFileAsString());
}

juce::File Library::fileFor (const juce::String& name) const
{
    return folder.getChildFile (name + fileExtension);
}

bool Library::userPresetExists (const juce::String& name) const
{
    return fileFor (name).existsAsFile();
}

juce::Result Library::save (const juce::String& name, const Values& values)
{
    if (cleanName (name) != name || name.isEmpty())
        return juce::Result::fail ("That name can't be a file name");

    if (const auto made = folder.createDirectory(); made.failed())
        return made;

    if (! fileFor (name).replaceWithText (toJson (name, values)))
        return juce::Result::fail ("Couldn't write " + fileFor (name).getFullPathName());

    rescan();
    return juce::Result::ok();
}

juce::Result Library::rename (const juce::String& from, const juce::String& to)
{
    if (cleanName (to) != to || to.isEmpty())
        return juce::Result::fail ("That name can't be a file name");

    const auto values = load ({ false, from });
    if (! values.has_value())
        return juce::Result::fail ("The preset \"" + from + "\" is gone");

    // Written to a temporary file (with the new name inside it), the old file removed, then the
    // temporary one moved into place. On Windows a rename that only changes case is the same
    // file, so writing the target directly would write over the source.
    const auto source = fileFor (from);
    const auto target = fileFor (to);
    const auto temporary = folder.getNonexistentChildFile ("rename", ".tmp", false);
    if (! temporary.replaceWithText (toJson (to, *values)))
        return juce::Result::fail ("Couldn't write " + temporary.getFullPathName());
    if (! source.deleteFile())
    {
        temporary.deleteFile();
        return juce::Result::fail ("Couldn't remove " + source.getFullPathName());
    }
    if (! temporary.moveFileTo (target))
        return juce::Result::fail ("Couldn't write " + target.getFullPathName());

    rescan();
    return juce::Result::ok();
}

juce::Result Library::remove (const juce::String& name)
{
    const auto file = fileFor (name);
    if (file.existsAsFile() && ! file.deleteFile())
        return juce::Result::fail ("Couldn't delete " + file.getFullPathName());

    rescan();
    return juce::Result::ok();
}

juce::String Library::cleanName (const juce::String& typed)
{
    // createLegalFileName drops \ / : * ? " < > |; a trailing dot or space isn't kept by Windows.
    return juce::File::createLegalFileName (typed.trim()).trimCharactersAtEnd (". ").trim();
}
} // namespace forte::presets
