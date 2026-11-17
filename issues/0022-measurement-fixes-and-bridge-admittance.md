---
id: 22
title: Fix the measurement tools, then one board with bridge admittance (plan, 2026-11-16)
labels: [voicing, measurement, model]
status: open
assignee:
parent: 1
blocked_by: []
---

## Where things stand

Committed and pushed on `hammer-felt-fit` (57f3855, not merged to `main`): issue 0021 §18-20.
The user rates the sound about 8/10 ("STELLAR … so much better than it ever has"): decay floor,
harder felt, bass radiation roll-off (§18); a board without the pickup path and a room with
parameters and presets (§19); fuller mid-to-loud notes from a partial-based colour fit, faster ff
hammer speeds and contact speed exponent 0.8 (§20).

The target is still the user's Keyscape renders (`C:\Users\sam\Downloads\note-sweep-keyscape.wav`,
3 s holds, and `note-sweep-long-keyscape.wav`, 11 s holds, which starts 2.25 s after the schedule).
Not Iowa. The user judges by ear in Reaper; ear beats reference fits.

This issue collects a review by another model, the user's bridge-admittance idea, and a verified
measurement bug, into one plan.

## 1. A measurement bug that affects §20 (verified)

Every partial-level measurement in §19-20 summed left and right to mono before the FFT:
`tools/fitting/analysis/partial_colour.py` (its `measure()`), and the session's scratch scripts for
per-velocity partial balance and treble decay. The two microphones hear each partial at a different
phase, so the mono sum cancels some partials. Checked at v64 on the 3 s sweeps, partial re partial
1, mono sum against the channels' mean power:

| | mono | stereo power |
|---|---|---|
| F#4 partial 6, Forte | −42.7 dB | −23.7 dB |
| F#4 partial 6, Keyscape | −20.5 | −22.8 |
| C4 partial 3, Forte | −22.4 | −12.6 |
| C5 partial 2, Keyscape | −34.3 | −21.8 |

Errors up to ~19 dB on single partials, both directions, both instruments. Medians over 12 notes
damp the noise, but cancellation biases levels down, so §20's numbers (the partial colour fit, the
per-velocity balance, "3-8 kHz too bright" in pieces from `ltas.py`, which also sums to mono)
need re-measuring. §20 sounds much better by ear, so it stands unless corrected measurements and
the user's ears both say otherwise.

**Fix:** measure each channel's spectrum and average the powers, in every script that reads
partials or bands (`partial_colour.py`, `colour_fit.py`, `ltas.py`, `sweep_compare.py`,
`decay_measure.py`, `hammer_measure.py`, and any others that call `x.mean(axis=1)` before an FFT).

## 2. The decay fitter tracks the wrong partial (to verify and fix)

The decay fitter predicts partial frequencies from `forte_audio.partial_frequency`, an older
stiffness curve; the engine moved to Keyscape-fitted inharmonicity in §14 (`PianoModel.cpp`,
`bKeys`/`bLogs`). The other model found its search for G1's partial 28 in the long Keyscape sweep
landing on partial 29. At high partial numbers in the bass the gap outgrows the search window.

**Fix:** follow each note's actual partials as found (re-estimating B from each found partial, as
`partial_colour.partials()` does), or use the engine's B curve; then re-measure
(`fit_decay.py measure-sweep … --hold 11 --gap 2`), refit, `floor`, build, and compare by ear.

## 3. Measure before building

With the fixed tools, three comparisons, Keyscape against Forte (room off: `forte-render` is dry
by default):

1. **Irregularity** (the bridge-admittance test): partial-to-partial decay-rate scatter within a
   note and across neighbouring notes; per-partial envelope wavering (beating) and how much it
   differs between partials. The fitted decays are smoothed (median over ±3 semitones, outliers
   replaced) and the unison spread is the same in cents on every partial, so Forte is expected
   to be more regular. In 0021 §17-era measurements Forte's lowest notes wavered 0.02 dB against
   Keyscape's 1-3 dB.
2. **Treble fundamentals:** how far partial 1 falls from the first 100 ms to 0.3-0.6 s against
   partial 2, C5-F6, at v64 and v96. The other model measured (mono, so re-check): C5-A5 partial 1
   falls 18.0 dB in Keyscape, 6.3 in Forte; C6-F6 partial 2 falls 8-9 dB in both while partial 1
   falls 22 (Keyscape) against 8 (Forte). This matches 0021 §18's open item.
3. **Region boundaries:** the partial balance across B2/C3, B3/C4 and D#5/E5 in both instruments.
   Forte's four regions (and two perspectives) use unrelated random boards; the other model
   measured a 10.7 dB difference at 1046.5 Hz between Forte's tenor and middle filters, but
   that's Forte against itself, not against Keyscape.

## 4. Then build: one board, with bridge admittance

**The idea (the user's):** the strings end at a bridge that moves, v_b(ω) = Y_b(ω)·F_b(ω). The
admittance Y_b sets, for every partial: its energy drain into the board (Re Y), a small pitch pull
(Im Y), and the unison's coupling (Weinreich's two-stage decay and beating happen because the
strings share a moving bridge). Forte has it only implicitly and smoothed: fitted per-partial
rates (an average bridge), one real coupling number per partial from those rates, and the board IR
as an output filter only.

**Design:**
- One shared modal board (mode frequencies, masses, damping), from which each note's bridge
  position sets how strongly it excites each mode (a smooth function of position along the
  bridge, so no jumps at region boundaries).
- Per note, at prepare time: Y_b at each partial's frequency gives extra damping, a frequency
  shift, and a complex unison coupling (extend `UnisonModes`/`solveUnisonModes` to complex
  coupling). Near zero runtime CPU: it only changes the numbers the resonators already use.
- Keep the Keyscape decay fit as each register's average; the admittance adds only the
  irregularity around it (zero mean), so the fitted voicing isn't lost.
- Radiation from the same board: the four region IRs built from the shared modes with
  position-dependent weights (or more regions, or each note split between its two nearest region
  buses), instead of unrelated random boards.
- Calibration: published grand-piano admittance measurements (Wogram, Conklin, Giordano) for
  magnitudes, mode density and peak heights; the Plath concert-grand dataset
  (https://zenodo.org/records/3271370: bridge vibration and stereo recordings for all 88 keys,
  per the other model; check its contents and licence first) for the board's structure. Keyscape
  stays the tone target.

**The treble fundamentals (§3.2):** refitting rates within today's model was tried in 0021 §15 and
gave a glassy, metallic ring. The tail's level isn't a free setting: it falls out of the unison's
fixed detuning, so a fast early drop leaves the fundamental collapsing and partials 2-4 ringing
alone. The fitter's early-window down-weighting and its prompt/aftersound cap of 3 are that fix,
not oversights. The physical route: the admittance gives the fundamental its strong drain; a
**second, horizontal polarisation** for the lower partials, weakly coupled to the bridge, holds the
tail. That costs CPU (more modes), so limit it to the partials that need it and measure against the
25% gate (SPEC §9.2; bracket old/new binaries, per the CPU benchmark method).

## 5. Order and listening tests

1. Fix the tools (§1, §2). Re-check §20's numbers. No sound changes unless corrected measurements
   and the user's ears agree.
2. Measure (§3). Build §4 only if the irregularity and boundary comparisons support it.
3. Build the shared board and admittance; then the treble fundamentals.
4. One change at a time, each as a short, level-matched A/B the user can hear in Reaper. The first
   one the other model proposed: a decay-only A/B on held C5-A5 notes, hammer, board and room
   fixed.

Rules that still hold: commit only when asked; `tools/lab-identity.ps1`-style identity checks and
Release `ctest` (pluginval included) after each step; Reaper locks the Release VST3, so ask the user
to close it before Release builds if a build fails to write it.
