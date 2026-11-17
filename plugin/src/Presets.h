#pragma once

#include <juce_core/juce_core.h>

#include "forte/engine/Engine.h"

#include <array>
#include <optional>
#include <vector>

namespace forte::presets
{
/** A preset's contents (SPEC §7.1): the 8 character controls, the room's 6 and Detune, as whole
    numbers. Volume, A4 and the pedals are never in a preset. */
struct Values
{
    int hardness = 50;
    int brightness = 50;
    int resonance = 50;
    int touch = 50;
    int stretch = 50;
    int spread = 50;
    int width = 100;
    int perspective = 0;  // 0 = Microphones, 1 = Pianist

    // The room (SPEC §2.11), as its parameters hold it; the defaults are the Studio room.
    int roomAmount = 22;
    int roomSize = 30;
    int roomDecay = 8;      // tenths of a second, 2-100
    int roomDamping = 55;
    int roomPredelay = 4;   // ms
    int roomEarly = 60;

    // A piano a few weeks after its tuning (SPEC §5): 0 = as tuned. Last, so the lists above keep
    // their positions.
    int detune = 0;

    friend bool operator== (const Values&, const Values&) = default;
};

/** One of the 15 controls: its plugin parameter id (SoundParameters::Ids), its range and where it
    lives in Values. The order is the file's and the tests'. A file without the room's values
    (written before the room) loads them as defaults, and one without Detune loads it at 0. */
struct Field
{
    const char* id;
    int minimum, maximum;
    int Values::* member;
};

inline constexpr std::array<Field, 15> fields { {
    { "hardness", 0, 100, &Values::hardness },
    { "brightness", 0, 100, &Values::brightness },
    { "resonance", 0, 100, &Values::resonance },
    { "touch", 0, 100, &Values::touch },
    { "stretch", 0, 100, &Values::stretch },
    { "spread", 0, 100, &Values::spread },
    { "width", 0, 100, &Values::width },
    { "perspective", 0, 1, &Values::perspective },
    { "roomAmount", 0, 100, &Values::roomAmount },
    { "roomSize", 0, 100, &Values::roomSize },
    { "roomDecay", 2, 100, &Values::roomDecay },
    { "roomDamping", 0, 100, &Values::roomDamping },
    { "roomPredelay", 0, 100, &Values::roomPredelay },
    { "roomEarly", 0, 100, &Values::roomEarly },
    { "detune", 0, 100, &Values::detune },
} };

/** The room's 6 fields, from here: the room presets set these and nothing else. */
inline constexpr std::size_t firstRoomField = 8;
inline constexpr std::size_t numRoomFields = 6;

/** Every value clamped into its range. */
[[nodiscard]] Values clamped (Values values) noexcept;

/** A preset's values over the controls it doesn't hold (Volume, A4). */
[[nodiscard]] Controls applyTo (const Values& values, Controls controls) noexcept;
[[nodiscard]] ListeningPerspective perspectiveOf (const Values& values) noexcept;

/** The room's values as the engine takes them. */
[[nodiscard]] RoomControls roomOf (const Values& values) noexcept;

//==============================================================================
/** A room preset (SPEC §2.11): the 6 room values only, chosen from the Room panel's menu.
    "Studio" is the default room. */
struct Room
{
    const char* name;
    int amount, size, decay, damping, predelay, early;  // as Values holds them
};

[[nodiscard]] const std::vector<Room>& roomPresets();

/** values with room's 6 values in place of its own. */
[[nodiscard]] Values withRoom (Values values, const Room& room) noexcept;

/** The room preset values' room matches, if any. */
[[nodiscard]] const Room* matchingRoom (const Values& values) noexcept;

//==============================================================================
struct Factory
{
    const char* name;
    Values values;
};

/** The built-in, read-only presets, "Concert Grand" (every default) first. The list and its
    values are first-build ones, to be set by ear (SPEC §10). */
[[nodiscard]] const std::vector<Factory>& factoryPresets();

//==============================================================================
/** A user preset file (SPEC §7.1): Documents\Forte\Presets\<name>.fortepreset, as JSON with a
    format version, the model id, the name and the 8 values. */
inline constexpr const char* fileExtension = ".fortepreset";
inline constexpr const char* fileFormat = "forte-preset";
inline constexpr int fileFormatVersion = 1;
inline constexpr const char* modelId = "concert-grand";

[[nodiscard]] juce::String toJson (const juce::String& name, const Values& values);

/** The values in a preset file's text. A value that's missing or not a number is its default;
    one out of range is clamped. Nothing if it isn't a Forte preset at all. */
[[nodiscard]] std::optional<Values> fromJson (const juce::String& text);

[[nodiscard]] juce::File defaultFolder();

//==============================================================================
/** Which preset: a factory one by name, or a user file by its name (the file's stem). */
struct Id
{
    bool factory = true;
    juce::String name = "Concert Grand";

    friend bool operator== (const Id&, const Id&) = default;
};

/** The presets the ‹ › arrows step through (SPEC §7.1): the factory ones in order, then the
    user files alphabetically (ignoring case), wrapping at both ends. */
class Library
{
public:
    explicit Library (juce::File folder = defaultFolder());

    /** Lists the folder again. Files that aren't Forte presets are left out. */
    void rescan();

    [[nodiscard]] const juce::File& getFolder() const noexcept { return folder; }
    [[nodiscard]] const std::vector<Id>& all() const noexcept { return ids; }
    [[nodiscard]] std::vector<juce::String> userNames() const;

    /** The preset delta steps from current, wrapping. From a preset that isn't listed (a
        deleted file), › goes to the first and ‹ to the last. */
    [[nodiscard]] Id step (const Id& current, int delta) const;

    /** A preset's values: a factory one's, or a user file read now. Nothing if it's gone. */
    [[nodiscard]] std::optional<Values> load (const Id& id) const;

    [[nodiscard]] juce::File fileFor (const juce::String& name) const;
    [[nodiscard]] bool userPresetExists (const juce::String& name) const;

    /** Writes (or overwrites) a user preset, then rescans. */
    juce::Result save (const juce::String& name, const Values& values);
    juce::Result rename (const juce::String& from, const juce::String& to);
    juce::Result remove (const juce::String& name);

    /** A name the user typed, made safe as a file name: trimmed, illegal characters dropped.
        Empty if nothing usable is left. */
    [[nodiscard]] static juce::String cleanName (const juce::String& typed);

private:
    juce::File folder;
    std::vector<Id> ids;
};
} // namespace forte::presets
