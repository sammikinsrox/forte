#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Presets.h"

namespace forte
{
/** The presets as the plugin plays them (SPEC §7.1): which one is loaded, the values it loaded
    (so `*` can show unsaved changes), and loading, saving, renaming and deleting.

    Loading a preset moves the 8 parameters as one host gesture each, like a user's edit, so
    ringing notes glide to it (SPEC §2.9) and the host's undo and automation see it.

    Message thread, except toState() and fromState(), which hosts may call from anywhere: those
    and the getters share a lock. */
class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& parameters, juce::File folder = presets::defaultFolder());

    [[nodiscard]] presets::Library& library() noexcept { return lib; }
    [[nodiscard]] presets::Id current() const;

    /** The 14 controls as the parameters hold them now. */
    [[nodiscard]] presets::Values liveValues() const;

    /** Sets the room's 6 controls to a room preset's, leaving the rest (SPEC §2.11). The piano
        preset then shows as modified unless it already had that room. */
    void applyRoom (const presets::Room& room);

    /** True when the controls differ from what the current preset loaded: the `*`. */
    [[nodiscard]] bool isModified() const;

    /** "Concert Grand", or "Concert Grand*" when it has unsaved changes. */
    [[nodiscard]] juce::String displayName() const;

    /** Loads a preset, dropping unsaved changes. False (and nothing changes) if it's gone. */
    bool select (const presets::Id& id);

    /** The ‹ › arrows: the previous (−1) or next (+1) preset, wrapping. */
    bool step (int delta);

    /** Save: writes the controls over the current user preset. Fails on a factory one. */
    juce::Result save();

    /** Save as…: writes the controls as a user preset (overwriting one of that name) and makes
        it the current one. */
    juce::Result saveAs (const juce::String& name);

    juce::Result renameCurrent (const juce::String& to);

    /** Deletes the current user preset's file. Its values stay on the controls and its name
        stays shown (no longer among the arrows' stops), so Save puts it back. */
    juce::Result deleteCurrent();

    /** The project's preset (SPEC §7.3): the current preset and the values it loaded. */
    [[nodiscard]] juce::ValueTree toState() const;

    /** Restores toState()'s tree. The controls themselves come from the project's parameters,
        so this only says which preset they started from. A missing or broken tree (a project
        from before presets) means Concert Grand. */
    void fromState (const juce::ValueTree& tree);

    static const juce::Identifier stateType;

private:
    void apply (const presets::Values& values);

    juce::AudioProcessorValueTreeState& parameters;
    presets::Library lib;

    juce::CriticalSection lock;
    presets::Id id;
    presets::Values loaded;
};
} // namespace forte
