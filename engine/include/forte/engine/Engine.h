#pragma once

#include "forte/engine/MidiEvent.h"
#include "forte/engine/VoicingLab.h"

#include <array>
#include <memory>
#include <vector>

namespace forte
{
/** Where the listener is (SPEC §6.3): it selects the soundboard's IR set. */
enum class ListeningPerspective
{
    microphones,  // a close stereo pair over the open lid (the default)
    pianist       // the player's seat
};

/** The instruction set the engine's hot loops run on (SPEC §9.2). One binary carries both. */
enum class InstructionSet
{
    sse2,  // every x64 CPU
    avx2   // AVX2 with FMA
};

/** The tuning controls (SPEC §5, §6.3). */
struct Tuning
{
    double a4Hz = 440.0;    // 415–466, whole Hz
    double stretch = 50.0;  // 0 = exact 2:1 octaves, 50 = the calculated curve, 100 = double
    double spread = 50.0;   // the unison spread: 0 = perfect unison, 50 = 0.7 cent, 100 = 1.4 cent

    friend bool operator== (const Tuning&, const Tuning&) = default;
};

/** The room (SPEC §2.11, §6.3): early reflections and a reverberant tail around the piano. The
    engine alone is dry (amount 0), the piano as committed; the rest are the "Studio" room
    preset's, which the plugin's parameters default to with its amount, 22. */
struct RoomControls
{
    double amount = 0.0;        // 0–100: how much room, 0 = none (the dry piano, untouched)
    double size = 30.0;         // 0–100: the reflections' spacing and the tail's density
    double decaySeconds = 0.8;  // 0.2–10: the tail's RT60 at low frequencies
    double damping = 55.0;      // 0–100: how much sooner the highs die (0 = as long as the lows)
    double predelayMs = 4.0;    // 0–100 ms before the first reflection
    double early = 60.0;        // 0–100: the early reflections' level (50 = their own)

    friend bool operator== (const RoomControls&, const RoomControls&) = default;
};

/** The sound controls (SPEC §6.3), as the plugin's parameters hold them. At their defaults the
    engine is the piano as committed. */
struct Controls
{
    double hardness = 50.0;    // 0–100: the felt stiffness ÷4 … fitted … ×4 (log), next strike
    double brightness = 50.0;  // 0–100: the treble tilt, −12 … 0 … +12 dB at 10 kHz
    double resonance = 50.0;   // 0–100: the sympathetic bank, 0 = none, 100 = double
    double touch = 50.0;       // 0–100: lighter … calibrated … heavier (SPEC §4.1)
    Tuning tuning;
    double width = 100.0;      // 0 = mono, 100 = as the soundboard places it
    double volumeDb = 0.0;     // −60 (off) to +6
    RoomControls room;

    friend bool operator== (const Controls&, const Controls&) = default;
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
    active Listening perspective, with no added latency, tilted by Brightness, then summed; the
    tape, Width and Volume follow (SPEC §2.1).
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

    /** The sound controls (SPEC §2.9, §6.3), at the current render position. Cheap when nothing
        changed, so it can be called every block. It survives reset() and prepare(), where
        everything applies at once.
        - Hardness and Touch: each note's next strike.
        - Brightness, Resonance, Width and Volume: glide there over ~20 ms.
        - A4 and Stretch: the tuner runs here (it's cheap and allocation-free), and ringing notes
          and sympathetic strings glide to their new pitch over ~50 ms.
        - Spread: changes the unison's coupled modes, so it waits, like the voicing lab's
          strings, for rebuildStrings() and a note's next start from rest.
        A4 and Stretch take a note's exact new modes the same way; until then a retuned note
        keeps its prepared hammer drives and high-pass, a few cents off. Realtime-safe. */
    void setControls (const Controls& controls) noexcept;

    [[nodiscard]] const Controls& getControls() const noexcept;

    /** The tuning the audio thread last got from setControls(), or the one prepare() used.
        Safe from any thread: rebuildStrings() reads it. */
    [[nodiscard]] Tuning getLatestTuning() const noexcept;

    /** A note's first partial in cents from equal temperament under a tuning, relative to A4's
        own equal-tempered scale (so A4 itself is 0 at any A4). For the editor's tuning curve.
        Reads only the piano model; safe from any thread. */
    [[nodiscard]] double getTuningCents (const Tuning& tuning, int midiNote) const noexcept;

    /** The voicing lab's values (SPEC §6.5), at the current render position. Cheap when nothing
        changed, so it can be called every block. Hammer controls take effect at each note's next
        strike; Tape drive glides there over ~20 ms. At every 50 the engine is exactly the
        committed voicing. It survives reset() and prepare(), and prepare() builds every note
        with its String controls and the soundboard with its Board controls. Realtime-safe.

        The String and Board controls need modes or IRs recomputed, which isn't realtime: see
        rebuildStrings() and rebuildBoard(). */
    void setVoicingLab (const lab::Settings& settings) noexcept;

    /** The voicing lab's strings (SPEC §6.5) and the tuning (SPEC §5): builds a new model for
        every note whose strings under settings and tuning differ from the ones it last built.
        A note takes its new model the next time it starts from rest (at a strike, never while
        it rings), so nothing clicks. Also frees the models notes have let go. Returns how many
        notes it built.

        Not realtime: it allocates. Call it from one thread at a time, never during prepare()
        or before the first prepare(); render() and handleMidi() may run meanwhile. */
    int rebuildStrings (const lab::Settings& settings, const Tuning& tuning);

    /** rebuildStrings() with getLatestTuning(). */
    int rebuildStrings (const lab::Settings& settings) { return rebuildStrings (settings, getLatestTuning()); }

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

    /** What the Board controls under settings mean (SPEC §6.5), for tools/fitting's fold: per
        bridge region (bass, tenor, middle, treble), the direct path's gain and what the colour
        shelves add, in dB, at each of the fitted colour curve's centres; and the modes' tilt in
        dB/octave. Safe from any thread. */
    struct LabBoardValues
    {
        std::array<double, 4> pickup {};
        double tiltDbPerOctave = 0.0;
        std::array<std::vector<double>, 4> colourDbAtCentres;
    };

    [[nodiscard]] LabBoardValues getLabBoardValues (const lab::Settings& settings) const;

    /** The committed voicing's revision: how many voicing lab exports have been folded into it
        (tools/fitting/fold_voicing.py). Lab values only mean what they did on the same revision. */
    [[nodiscard]] static int getVoicingRevision() noexcept;

    /** The longest the engine can go on sounding after the last MIDI event: an undamped (or
        pedalled) note decaying from full scale to rest, through its slowest coupled mode, then
        the soundboard IR's length, then the room's tail at the current Room controls. Depends
        only on the piano model, the lab's strings as last built, the IRs and the Room controls,
        not on the sample rate. Safe from any thread. */
    [[nodiscard]] double getTailSeconds() const noexcept;

    /** How many notes were sounding (not idle) at the end of the last render(), for the editor's
        Notes meter (SPEC §6.4). Safe from any thread. */
    [[nodiscard]] int getNumSoundingNotes() const noexcept;

    /** The instruction set the hot loops use: AVX2 when the CPU has it (with FMA), otherwise SSE2,
        unless setInstructionSet() asked for another. */
    [[nodiscard]] InstructionSet getInstructionSet() const noexcept;

    /** Runs the hot loops on an instruction set: for the performance gate and the tests (SPEC
        §9.2). One the CPU lacks falls back to SSE2. Renders under the two differ in their last
        bits; each is deterministic on its own. Not realtime: call while nothing renders. */
    void setInstructionSet (InstructionSet set) noexcept;

    [[nodiscard]] static bool cpuSupports (InstructionSet set) noexcept;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaxBlockSize() const noexcept { return maxBlockSize; }

private:
    struct State;

    double sampleRate = 48000.0;
    int maxBlockSize = 0;
    std::unique_ptr<State> state;
};
} // namespace forte
