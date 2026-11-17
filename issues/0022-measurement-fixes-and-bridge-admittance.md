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

## 6. Results (2026-11-16/17)

**Steps 1-2 done.** The tools average the channels' powers, track each note's partials and follow
a sweep that slips (the long Keyscape sweep is 2.31 s late from its fourth note); see
`tools/fitting/README.md`. Re-measured: §20's partials 4-7 gap at C4-B4 is ~1.4 dB, not ~5; the
colour residuals stay within ±2 dB (bass 40-63 Hz −6 either way); the old decay rows for G1 were one
partial off from partial 20, and C4-C5's partials 13-32 had been read on hiss (the §18 floor covers
most of that). §3: Forte's decay rates are 6-15× smoother note to note than Keyscape's, but
Keyscape's own v64 and v96 differ as much (r ≈ 0.2-0.3 from C2 up), so it isn't a fixed per-note
pattern except in A0-B1; no region-boundary step beyond the baseline; the treble fundamental
drops 18-23 dB in 0.3-0.6 s in Keyscape, 6-8 in Forte. Recommendation: not the shared board yet.

**Autumn Leaves A/B and fold (voicing revision 1).** The user found Autumn Leaves (median velocity
82, pedal down 29%) honky and twangy where Chopin (36, 89%) sounded right. Of single lab changes,
a narrower unison spread cut the twang (C3-B4: the fundamental stopped sinking under partials 2-4,
beats 1.42 → 0.89 dB) and Velocity brightness 31 (contact exponent 0.5) cut the loud honk. The
user chose both with their Hardness 25 / Brightness 75, folded as the new 50s: felt ×0.5
everywhere, unison spread ×0.7 at C2 and ×0.5 at C4, contact exponent 0.5
(`voicing-overrides.json`), and Brightness 75's shelf (+6 dB at 10 kHz) added to
`board-colour.json`. The fold renders within ±0.2 dB per third-octave of the old build with those
settings.

**What's left, by the user's ear:** "thin", "uncanny", the ring "like an electric guitar", most in
the middle and treble; C5 up quieter. Measured:
- Held notes: Keyscape falls fast, then holds a long loud tail; Forte the reverse. C4, re the
  first 0.1 s: Keyscape −12.5 dB at 0.5-1 s, −27..−33 at 4-6 s; Forte −6.9 and −38. E2's tail
  at 4-9 s is ~10 dB under Keyscape's. The narrower spread made the tail 1-7 dB weaker still
  (in this model the aftersound comes only from the unison's detuning).
- The treble strike: C5-F6 strike 6-11 dB under the middle's at v64 (Keyscape's level with it);
  after the strike Forte falls ~5 dB, Keyscape ~12.
- Pitch is as steady in Keyscape as in Forte (drift < 1.4 cents, wobble 0.5-1 cent).

So the next step is the aftersound that doesn't depend on detuning (§4's second polarisation),
across the keyboard, not only the treble fundamentals.

**The second polarisation, prototype (2026-11-17, awaiting the user's ear).** Fitted per partial
to the corrected Keyscape measurement, power ∝ (1 − h²)·e^(−2·fast·t) + h²·e^(−2·slow·t): Keyscape
falls fast (C3 p1 1.4/s, C4 2.4, C5 5.8, C6 9.1) to a tail 19-30 dB down at 0.1-0.8/s.
`tools/fitting/fit_tail.py` → `data/model/tail.json` → `engine/src/TailFit.inc` (partials 1-12
under 5 kHz, A0-B6; smoothed ±3 notes; tail ≥ b3·f² and ≥ 0.12/s). The engine gives those
partials one more mode each, at the front of the note's modes: started at the tail's level × the
vertical strings' drive, not felt by the hammer, decaying at the tail rate (Second stage scales
it), 0.3·sin(2.3·key) cents off; the partial's strings then decay at the fast rate, uncoupled.
Realised: C4's fundamental 2.24 / 0.27 /s at −28.6 dB (Keyscape 2.41 / 0.29 / −27.9); held notes'
rms against Keyscape's 13.2 dB rms off over 0.3-9 s, from 20.1. Still short: the tails of
partials above the fit (C5 up has only 2-4), and C4-G4's 4-9 s by 4-8 dB. CPU +0.3 points
(AVX2), +0.7 (SSE2), bracketed. The user: "a bit better".

**Even voicing.** The user's screenshot of held notes: Keyscape's treble strikes as loud as its
middle, Forte's are blips. Keyscape's strikes sit within ±2.5 dB of C4's from C2 to C6; Forte's
treble was 12-15 dB short and neighbours jumped 4-10 dB. `tools/fitting/fit_level.py` fits a
level per note (mean of the strike peak and the 0.1-0.5 s rms, v64/v96, level-neutral) →
`data/model/level.json` → `engine/src/LevelFit.inc`, applied to the note's whole output. Note to
note against Keyscape: 4.9 dB rms (15.5 worst) → 0.2 (1.1). The user: "a bit better".

**The knock.** Then the knock stood out in the treble, "very bassy": its 150 Hz low-pass put
Keyscape's 0.25-2 kHz strike noise 8-24 dB too low and the thump too high (63-250 Hz, C6 up). Now a
band-pass (2 × one-pole high-pass at 300 Hz, 2 × low-pass at 2 kHz), and D7's level 0.6 → 0.17:
C5-C6's strike noise per octave within ~1-3 dB of Keyscape's, C7-B7 between its v64 and v96.

The user: the new knock is "wonderful".

**Detune** (SPEC §5, §6.3, §7.1): a sound control, 0-100, default 0; every note but A4 off its tuned
pitch by its own fixed amount (a hash of the note, even over ±1) × 3 cents at 100. In the Tuning
group, in presets (the 15th field; older files load 0), automatable, `forte-render --control
detune=`. Issue 0016 §11 had left random tuner imperfection for later "if Forte sounds too clean".

**The treble's ring** (after the commit). With the even voicing, the treble's envelope matched
Keyscape's to 0.5 s; after it, Keyscape's held on (−38 dB re the strike at 1-2.5 s, 92-99% of it
on partials 1-8, 20 dB over its noise) and Forte's fell 5-12 dB lower from F#5. Its partials 2-4
ring on in Keyscape; the tail fit had stopped at the first partial it couldn't fit (bumpy treble
envelopes), so C6 had tails on 2 partials. `fit_tail.py` now fits each partial on its own (robust
loss, rms ≤ 4 dB) and fills gaps from the notes around; then `correct` passes move each note's
tail levels by its whole late sound (0.5-2.5 s re the strike) against Keyscape's, smoothed ±2
notes, up to B6 and only where Keyscape is 12 dB clear of its noise. Three passes: C5-F6 within
~2 dB of Keyscape's out to 2.5 s (was up to 9 short). F#6 up left as fitted. `fit_level.py`
refitted after: note to note 0.1 dB rms. CPU unchanged, bracketed.

The user, on Autumn Leaves and Chopin: "off. Fake, synthesized, casio-tone, rather than a proper
bell-tone". Measured (`tools/fitting/analysis/wavering.py`, now run on every change): from C4 to
B6 the partials' level wavering was 0.00-0.03 dB and partial 1's pitch wobble 0.03-0.08 cents,
against Keyscape's 0.4-1.9 dB and 0.35-1.1 cents. The single tail mode per partial was a perfect
sine once the fast stage had gone, and the correction had made it louder. Not committed.

**A tail that beats.** The tail is now one mode per string (two on a single string), each at its
string's detuning plus its own place in `tailSpreadCents` (0.3 cents in the bass, 0.15-0.2 in the
tenor and middle, up to 0.4 at the top), at unequal shares (58/27/15 %, 68/32 % on two) and rates
×1, ×1.12, ×0.88. Movement is very sensitive to the shares: 48/32/20 % beat twice Keyscape's,
70/20/10 % almost not at all. Now (fast / slow waver dB, pitch wobble cents; Keyscape first): C3
0.04/0.38/0.44 against 0.04/1.28/0.37; C4 0.20/1.55/0.48 against 0.20/1.36/0.34; C5 0.96/1.45/0.66
against 0.50/1.15/0.29; C6 1.11/1.53 against 0.76/1.95. Like Keyscape's, a partial now dips where
its fast stage and tail cancel and recovers. Tails refitted and corrected (three passes), levels
refitted (0.1 dB rms). Held notes 7.4 dB rms from Keyscape's (committed 10.9). CPU +0.3 (AVX2),
+0.6 (SSE2), bracketed. The felt test measures the felt without tails.

The user preferred the committed build: "alive" had "a sort of rapidly changing digital artifact"
(heard by nulling it against committed). The difference was all tone (99.99 % on the partials, no
clicks, nothing above 8 kHz), its envelope moving at 0.6 Hz on every note from C3 to C6: one rate
everywhere, as a chorus moves, where a piano's beats differ note to note. Shelved, as
`build/ab/alive/alive-shelved.patch` (the multi-mode tail, the per-partial tail fit and the
correction passes); the engine is back at 3d56fb9. `analysis/wavering.py` stays.

**Resonance** (after): the user could barely hear it from 0 to 100. With the pedal down (My Funny
Valentine, 94 %) the sympathetic bank was −16.5 dB of the output at 50 and −10.8 at 100; the piece
0.4 dB louder. Now 0-50 linear as before, then doubling every 25: 75 = ×2 (the old top), 100 = ×4
(−5.8 dB of the output). The user's old favourite clip (15 Nov) had ×3, and longer sustain, which
they want everywhere below about A6. A/B in `build/ab/sustain/`.

The user: "resonance is totally the answer … fixes almost all the issues"; sustain ×2 below A6 with
the bank ×2 "totally the best sounding". Made the default: Sustain 100 on the bass and middle
registers folded (voicing revision 2, rates ×0.5 at C2 and C4, back to ×1 by C7), and
`sympatheticLevel` 0.1 → 0.2. Renders within ±0.4 dB per third-octave of that A/B clip. The
slowest rates now 0.06/s (115 s T60); the engine's reported tail 187 s.

Then wider extremes, asked for "whacky": Resonance above 50 doubles every 12.5 (×16 at 100,
the bank +10 dB rms over a pedalled piece); Detune is linear to ±3 cents at 50, then doubles
every 13.5 (±39 cents at 100). No clipping at either extreme (My Funny Valentine peaks −17 dBFS).

Rules that still hold: commit only when asked; `tools/lab-identity.ps1`-style identity checks and
Release `ctest` (pluginval included) after each step; Reaper locks the Release VST3, so ask the user
to close it before Release builds if a build fails to write it.
