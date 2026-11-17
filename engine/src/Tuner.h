#pragma once

#include "PianoModel.h"

#include <array>

namespace forte::detail
{
/** The simulated tuner (SPEC §5): every note's first partial, set from the model's
    inharmonicity B the way a piano tuner sets it by ear.

    It starts at A4 and sets the temperament octave A3–A4 in equal temperament: the octave is a
    beatless 4:2 one, divided into 12 equal steps. Then it tunes outward one note at a time
    against the note an octave inside, matching a pair of partials so that pair doesn't beat:
    - going up: 4:2 (the lower note's 4th partial on the upper's 2nd) to about C6, then 2:1;
    - going down: 4:2 to about C3, then 6:3, which pulls the low bass flatter.
    Across an octave centred on C6 and on C3 it moves gradually from one pair to the next, so
    the curve has no step there (a switch at one note leaves 2–3 cents).

    The result is the calculated curve: each note's distance from equal temperament, in cents.
    Stretch scales that distance (0 = none, 50 = the curve, 100 = double) and A4 moves every note
    with it. Both are smooth, so the tuner never switches partials as they move. */
struct StretchCurve
{
    /** Cents from equal temperament at Stretch 50, per note (A0 at [0]). 0 at A4. */
    std::array<double, numNotes> cents {};
};

/** The calculated curve for a model's B. Not realtime-critical, but it doesn't allocate. */
[[nodiscard]] StretchCurve calculateStretchCurve (const PianoModel& model) noexcept;

inline constexpr double referenceA4 = 440.0;
inline constexpr double lowestA4 = 415.0;
inline constexpr double highestA4 = 466.0;
inline constexpr double calculatedStretch = 50.0;

/** A note's first partial over its equal-tempered pitch at A4 = 440 (the model's own tuning),
    for an A4 in Hz, a Stretch of 0–100 and a Detune of 0–100. Exactly 1 at A4 = 440, Stretch 0
    and Detune 0. Realtime-safe. */
[[nodiscard]] double tuningRatio (const StretchCurve& curve, int noteIndex, double a4Hz, double stretch,
                                  double detune = 0.0) noexcept;

/** Detune (SPEC §5): a piano a few weeks after its tuning. Every note but A4 sits off its tuned
    pitch by its own fixed amount, the same every session: detuneShape(note) × Detune / 100 ×
    maxDetuneCents. */
inline constexpr double maxDetuneCents = 3.0;

/** Note noteIndex's fixed share of the detuning, −1 to 1 (0 for A4): a hash of the note, spread
    evenly. */
[[nodiscard]] double detuneShape (int noteIndex) noexcept;

/** The cents Detune moves a note by. */
[[nodiscard]] inline double detuneCents (int noteIndex, double detune) noexcept
{
    return detuneShape (noteIndex) * detune / 100.0 * maxDetuneCents;
}
} // namespace forte::detail
