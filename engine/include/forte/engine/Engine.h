#pragma once

#include "forte/engine/MidiEvent.h"
#include "forte/engine/VoicingLab.h"

#include <memory>

namespace forte
{
/** Where the listener is (SPEC §6.3): it selects the soundboard's IR set. */
enum class ListeningPerspective
{
    microphones,  // a close stereo pair over the open lid (the default)
    pianist       // the player's seat
};

/** The Forte piano engine: 88 permanent note models and the MIDI that plays them (SPEC §2, §4).

    Threading contract:
    - prepare() runs off the audio thread, while audio is stopped. It may allocate.
    - reset(), handleMidi() and render() run on the audio thread. They never allocate, lock or block.

    Sample-accurate timing (SPEC §4.3) is the caller's job: render up to an event's offset,
    hand the event over, then carry on rendering. See forte::bridge::renderBlock().

    Output depends only on the events and their sample times, never on how the samples are split
    into render() calls, so offline and realtime renders are bit-identical (SPEC §7.4).

    Output (SPEC §2.1, §2.6, §2.8): each note's bridge force feeds one of four bridge regions, and
    every key's sympathetic bank, driven by all the other notes' force and gated by its damper,
    feeds the key's own region. Each region is convolved with its stereo soundboard IR from the
    active Listening perspective, with no added latency. Resonance, Brightness, Width and Volume
    sit at their defaults until the parameters exist.
*/
class Engine
{
public:
    static constexpr int numOutputChannels = 2;

    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    /** Rebuilds the engine for a sample rate and maximum block size, then resets it.
        Not realtime-safe. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Returns to the just-prepared state: silent, every key up and both pedals up. Realtime-safe. */
    void reset() noexcept;

    /** Applies one MIDI message at the current render position (SPEC §4). Realtime-safe. */
    void handleMidi (const MidiEvent& event) noexcept;

    /** Renders numSamples of stereo output, overwriting left and right. Realtime-safe. */
    void render (float* left, float* right, int numSamples) noexcept;

    /** Switches the Listening perspective at the current render position. The new IR set fades in
        over ~50 ms from the next 128-sample boundary of the engine's sample clock. It survives
        reset() and prepare(), where it applies at once. Realtime-safe. */
    void setListeningPerspective (ListeningPerspective perspective) noexcept;

    /** The voicing lab's values (SPEC §6.5), at the current render position. Cheap when nothing
        changed, so it can be called every block. Hammer controls take effect at each note's next
        strike; Tape drive glides there over ~20 ms. At every 50 the engine is exactly the
        committed voicing. It survives reset() and prepare(), and prepare() builds every note
        with its String controls and the soundboard with its Board controls. Realtime-safe.

        The String and Board controls need modes or IRs recomputed, which isn't realtime: see
        rebuildStrings() and rebuildBoard(). */
    void setVoicingLab (const lab::Settings& settings) noexcept;

    /** The voicing lab's strings (SPEC §6.5): builds a new model for every note whose strings
        under settings differ from the ones it last built. A note takes its new model the next
        time it starts from rest (at a strike, never while it rings), so nothing clicks. Also
        frees the models notes have let go. Returns how many notes it built.

        Not realtime: it allocates. Call it from one thread at a time, never during prepare()
        or before the first prepare(); render() and handleMidi() may run meanwhile. */
    int rebuildStrings (const lab::Settings& settings);

    /** The voicing lab's board (SPEC §6.5): if the Board controls under settings differ from the
        ones the soundboard last got, builds both perspectives' IRs again from their kept parts
        (the shipped IRs themselves at the Board group's 50s), which the soundboard crossfades to
        over ~50 ms from its next 128-sample boundary. Also frees what the engine has let go.
        Returns whether it built. Same thread rules as rebuildStrings(); it takes tens of ms. */
    bool rebuildBoard (const lab::Settings& settings);

    /** Frees the strings models and soundboard IRs the engine has let go since. Same thread
        rules as rebuildStrings(). */
    void collectRetired() noexcept;

    /** True while any note has new strings it hasn't taken yet. Safe from any thread. */
    [[nodiscard]] bool hasIncomingStrings() const noexcept;

    /** True while the soundboard has new IRs it hasn't started fading to. Safe from any thread. */
    [[nodiscard]] bool hasIncomingBoard() const noexcept;

    [[nodiscard]] const lab::Settings& getVoicingLab() const noexcept;

    /** What a control means on a note under settings, in its physical terms (a multiplier, ε,
        dB/octave, …; see lab::controls' ranges). Reads only the piano model, which never changes
        after construction, so it's safe from any thread. */
    [[nodiscard]] double getLabPhysicalValue (const lab::Settings& settings, lab::Control control, int midiNote) const noexcept;

    /** The longest the engine can go on sounding after the last MIDI event: an undamped (or
        pedalled) note decaying from full scale to rest, through its slowest coupled mode, then
        the soundboard IR's length. Depends only on the piano model, the lab's strings as last
        built and the IRs, not on the sample rate. Safe from any thread. */
    [[nodiscard]] double getTailSeconds() const noexcept;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    struct State;

    double sampleRate = 48000.0;
    int maxBlockSize = 0;
    std::unique_ptr<State> state;
};
} // namespace forte
