#pragma once

#include "forte/engine/VoicingLab.h"

#include "PianoModel.h"

namespace forte::detail
{
/** What a lab value (0–100) means (SPEC §6.5), given the committed value it replaces at 50.
    At exactly 50 it returns `fitted` exactly, so a lab at its 50s changes nothing. Multipliers
    (fitted 1) and absolute values (fitted = the committed value) take the same form. */
[[nodiscard]] double labMap (lab::Control control, double value, double fitted) noexcept;

/** True when the control's physical value is a positive multiplier, interpolated across the
    keys in log; the others are interpolated linearly (SPEC §6.5). */
[[nodiscard]] bool labInterpolatesInLog (lab::Control control) noexcept;

/** The control's physical value on a note: its register values mapped with the note's own
    committed value, then interpolated across the keys between the anchors C2, C4 and C7 (log or
    linear, labInterpolatesInLog()), flat outside. A control without registers maps its one
    value. Exact `fitted` while the control is at 50. */
[[nodiscard]] double labValueAtKey (const lab::Settings& settings, lab::Control control, double midiNote, double fitted) noexcept;

/** The board's committed settings, as tools/seed-irs/make_seed_irs.py built the shipped IRs
    (DIRECT_GAIN, MODE_TILT_DB_PER_OCTAVE). */
inline constexpr double committedPickup = 0.5;
inline constexpr double committedBoardTilt = 8.0;

/** The committed value a control's 50 stands for on a note: 1 for multipliers, 0 for offsets,
    the model's own value for the absolute ones. */
[[nodiscard]] double committedLabValue (lab::Control control, const PianoModel& model, int midiNote) noexcept;

/** The hammer's lab voicing on one note, read at each strike (SPEC §2.9, §6.5). */
struct StrikeVoicing
{
    double feltStiffnessScale = 1.0;
    double feltRelaxation = 0.0;        // ε, absolute
    double contactSpeedExponent = 1.0;  // absolute
    double strikePositionScale = 1.0;
    double hammerMassScale = 1.0;
    double knockScale = 1.0;
};

/** The committed hammer: the note's own values, with every multiplier 1. */
[[nodiscard]] StrikeVoicing committedStrikeVoicing (const NoteParameters& p) noexcept;

/** The note's hammer under the lab. committedStrikeVoicing() while the Hammer group is at 50. */
[[nodiscard]] StrikeVoicing labStrikeVoicing (const lab::Settings& settings, const NoteParameters& p) noexcept;

/** The output's tape drive under the lab (fitted = the model's). */
[[nodiscard]] double labTapeDrive (const lab::Settings& settings, double fitted) noexcept;
} // namespace forte::detail
