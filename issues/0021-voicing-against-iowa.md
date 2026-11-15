---
id: 21
title: Voicing the seed piano against Iowa (listening findings, 2026-11-14)
labels: [voicing]
status: open
assignee:
parent: 1
blocked_by: []
---

## Question

After the sympathetic bank landed, the user listened on headphones (Cry Me a River, Danny Boy,
Funny Valentine, Yesterdays) and heard Forte as "fake", "twangy", "like an older electric grand or
a very good upright", and "like a closed lid" once the twang was removed. What causes each, what
fixes each, and in what order? The aim: **bright, full, with shimmer and rumble, and no twang.**

## Findings

All measured against the Iowa Steinway mf (and pp/ff where Iowa has them: C2, C4, C6), with
forte-render, single notes at velocity 64 unless stated.

### 1. The twang is the hammer: the middle of the keyboard is far too bright for its dynamic

- C4's partials 2–14, relative to the fundamental, are 10–30 dB hotter than Iowa's in the attack.
  Mean of partials 2–12 re partial 1: Iowa mf −33 dB, ff −21 dB; Forte at velocity 64 −17 dB
  (seed felt), −13 dB (felt ×2), and −9 dB at velocity 127. Forte's mf is brighter than Iowa's ff.
- A real piano at mf is fundamental-dominated: the overtones flash and fade, the "bell". Forte's
  thick, lingering 0.5–3.5 kHz layer reads as nasal and twangy.
- Felt ×0.25 removes most of the twang by ear (confirmed on Funny Valentine), but also dulls the
  sound ("closed lid"): above 4 kHz drops from −53 to −80 dB of the piece's energy. Felt stiffness
  alone moves the whole spectrum; it can't make the 1–3 kHz layer quieter and keep the top.
- C6 is roughly right at felt ×1–2 (mf −56 to −58 vs Iowa −50; ff −37 to −40 vs −42). The felt must
  vary along the keyboard (SPEC §3.3 fits it per note), not scale uniformly.
- There is **no cliff above 4.5 kHz** at the current felt and gain: C4's raw string output reaches
  −50 to −75 dB re partial 1 up to ~7 kHz, as strong as Iowa's (whose partials above that sit at its
  noise floor). An earlier "cliff" measurement came from the soft seed felt, the old low gain and a
  2 s window; it was wrong.

### 2. Decay runs the wrong way round

Over 0.3–2.3 s (key held): C4's fundamental decays at −16.5 dB/s (Iowa −8.6), A4's at −10.9
(Iowa −3.9). C4's partials 4–7 decay at −1 to −3.5 dB/s (Iowa −7 to −13); E5's partials 2–3 at
−3 (Iowa −11 to −14). The upper partials sit on the slow aftersound from the start: the unison's
detuning is the same in cents, so more Hz at higher partials, and the seed aftersound law is a
quarter of the prompt law, barely rising with frequency. A steeper aftersound law (aftersound c3 =
prompt c3, both 3e-7) fixes 1–2 kHz but kills 2.5–4 kHz far too fast: Iowa's decay levels off
above ~2 kHz, which an f² law can't follow. By ear the difference was subtle.

### 3. "Electric grand" is partly the soundboard's direct path

The seed IR is a flat direct path (weight 2.0) plus resonant modes normalised to unit energy, so
the output is mostly the bridge force itself, like a pickup. Direct weight 0.5 audibly reduces the
twang and was the user's favourite for clarity (board #9: original board + 12 dB direct-path low
shelf, direct 0.5). Partial-level scatter barely changes with the weight (Iowa 5.1 dB; Forte 4.3–5.6
dB), so the difference is in how the sound develops, not its spectrum. Costs: duller top (the board's
modes roll off above 6 kHz), and the direct-path shelf's bass goes with the direct path.

### 4. Body and bass

- The seed generator's radiation curve cut its modes −21 dB at 100 Hz, with only nine modes below
  300 Hz and none between 128 and 186 Hz. Radiation corner 250 Hz and statistical modes from 80 Hz
  ("fuller board") give body, but strong low modes colour individual partials and sounded
  synthetic; a few bass notes boom (D3, C2, G2, Gb2, Ab2 6–12 dB over their neighbours).
- A direct-path low shelf (min-phase, at 250 Hz) adds even bass weight but also low-mid body; a
  lower corner (80–120 Hz) would target rumble.
- In Iowa, C2's overtones sit 17 dB above its fundamental (a real board radiates the lowest
  fundamentals weakly): the bass's "growl" is in partials 2–8. Forte's C2 fundamental is as strong as
  its overtones, and the shelf makes it stronger. Real rumble is the overtones plus the lowest board
  modes, not a louder fundamental.
- Board modes pick out single overtones: through board #9 at direct 0.5, C4's second partial is
  17.6 dB above its fundamental (Iowa: 11 dB below). That's a likely source of "twang on certain
  notes".

### 5. Level

Output gain is up 7.6 dB (5.6e-3 → 1.35e-2, uncommitted): a played piece averaged −33 dBFS, too
quiet to judge the bass by. Above that, single velocity-127 notes clip (D3 reached +2.4 dBFS).

### What didn't work

- Fitting a per-region third-octave colour correction to Iowa: wouldn't converge (2–3 Iowa notes per
  bass band, the felt's brightness skewing the shares); dropped.
- Damping the fuller board's low modes 4×: less even and thinner.
- A per-note felt fit through board #9: the board's modal colouring dominated, and Iowa's own
  note-to-note brightness scatter is 10–20 dB. The fit must measure the strings without the board.
- Raising the felt exponent without compensating stiffness: makes the felt ~50× softer.

### 6. Step 1 done: the hammer and felt fit (2026-11-14, branch hammer-felt-fit)

`tools/fitting/fit_hammer.py` (measure → fit → build; its docstring has the method), measuring the
strings without the board through `forte-string-probe`.

**Where the twang came from.** On the bare strings the seed's C4 contact lasted ~1 ms (real: 2–4
ms), so the force spectrum was flat to ~1 kHz and partials 2–7 came out level with the fundamental
at every speed and felt. A softer felt lengthens the pulse but filters out the strings' reflections,
which takes the top away: the closed lid.

**The felt model is now Stulov's hereditary felt** (SPEC §2.4), F = Q0·[cᵖ − ε·(cᵖ low-passed with
τ)], replacing the α·d(cᵖ)/dt term (its τ → 0 limit). Fitted to C4's band changes between pp, mf
and ff: no hysteresis 12.6 dB rms, α-law 4.1, Stulov 3.7 (C2: 6.1 for both hysteretic laws). An
elastic felt can't brighten from pp to ff as steeply as Iowa does (15–36 dB per band). Stulov was
chosen over the α-law for its bounded force and its time scale. The fitted hysteresis is large: ε
0.75–0.97, τ 0.64 ms (C2) to 1.06 ms (C4 up).

**Measurement lessons** (in `hammer_measure.py`):

- `forte_audio.onset`'s 5 % threshold fires on Iowa pp's lead-in noise, 0.25 s early; that, not
  the noise floor, made "pp brighter than mf". With a 30 % onset, a per-partial noise floor and a
  peak-prominence test, C2 and C4 pp are usable (half weight); C6 pp is still brighter than its mf.
- Iowa's C4 is not an outlier on the old metric (mean of partials 2–12 re 1): Iowa darkens steadily
  up the keyboard, C3 +2.5, G3 −15, B3 −20, C4 −34, A4 −35, C5 −48 dB. But a single note's octave
  bands scatter ±15 dB, so the targets are running medians over ±4 semitones.
- Band brightness must be a median of partial levels, not a power mean: a power mean follows the
  loudest partial, often one picked out by a board mode. The first fit, on power means, kept the
  twang.
- Iowa mf is at its noise floor above ~4 kHz from C4 up, and C6 has one usable band. So Iowa can't
  say how bright the top should be: the felt's shape (p, ε, τ) comes from C2 and C4, and the top is
  what the hysteresis gives.
- Band changes between dynamics belong to the felt alone (the board, microphones and room are
  linear and cancel). Fitting those is what made the fit work.

**What changed, at velocity 64, in the attack (bare strings, octave bands re the fundamental):**
1–2 kHz and 2–4 kHz down 3–16 dB from C3 to G4; partials 2–3 down 5–17 dB from E4 up. C5 and E5,
which had nothing above 2 kHz (−120 dB), now have a top at −35 to −60 dB. Through board #9, the
mean of C4's partials 2–12 is −25.8 dB (seed −16.3; Iowa mf −35, ff −27): no longer brighter than
Iowa's ff, though still 9 dB brighter than Iowa's single (dark) C4. C6 −48.8 (seed −58.4; Iowa mf
−49.5).

**Pieces** (board #9, level-matched): energy above 4 kHz up 4–10 dB, above 8 kHz up 23–27 dB, peaks
up to 6 dB higher at the same RMS (sharper strikes). But the share at 1–3.5 kHz is unchanged
within ±2 dB. Over a piece that band is mostly the sustain, so the lingering layer is step 2's (the
decay), as §2 suggested.

**Where the fit stops matching Iowa:**

- C5–Gb5: the best per-note felt there is nearly elastic (ε ≈ 0.02). Iowa's mf treble has its 2–5
  kHz band ~60 dB under the fundamental, and the hysteresis's flash can't go that low. The smoothed
  ε (0.73–0.86) keeps the flash, ~25 dB brighter than Iowa there. That's the brightness asked for,
  at Iowa's expense; `fit-softer` leans the other way.
- Treble brightness is U-shaped in velocity: from E5 up, pp is a little brighter than mf, then ff
  brightest. Iowa's C6 pp is also brighter than its mf, but likely the treble's τ is wrong, held at
  C4's because Iowa has no treble dynamics to fit.
- C4's ff speed is 9.2 m/s (real ff ~5–6) to reach Iowa's +13 dB from mf. Forte's loudness rises
  more slowly with speed than Iowa's gap asks for. The mf speed (2 m/s) is a seed; Iowa can't
  identify it.

**The top octave and velocity 127.** Above C6 the stiffest, fastest blows outran the 4×-oversampled
contact: at 44.1 kHz a velocity-127 note peaked up to 9 dB hotter than at 96 kHz, and clipped (+6.7
dBFS at A7). The hammer stability test missed it, because it checked the left channel only and the
treble sits right. Now: Q0 rises 1.155× per semitone above C6 (capped so every note stays within
1 dB of 96 kHz at ff), velocity 127 falls from 12.4 m/s at A6 to 7.8 at C8 (the fastest accurate
speed), and the test checks both channels. Every note at 127 peaks ≤ −1.25 dBFS at 44.1 and 96 kHz
with the committed board. C8 speaks: its fundamental at velocity 64 is 4 dB under C6's (seed: 31 dB
under).

## Plan

How to get bright + full + shimmer + rumble without twang. Twang and brightness live in different
places: twang is a loud, lingering 0.5–3.5 kHz layer over a weak fundamental; brightness and shimmer
are high partials (4–12 kHz) that flash at the strike and fade, over a strong fundamental. So:

1. **Done (§6); listening pending.** **Hammer and felt fit (SPEC §3.3), with the felt's hysteresis.** Fit stiffness, exponent and
   hysteresis per note, plus the velocity calibration, against Iowa pp/mf/ff (C2/C4/C6) and mf
   (all notes), measuring the strings without the board. Felt hysteresis is the key extra freedom:
   a felt that is stiffer the faster it's compressed (Stulov) has a steep spectrum through the mids,
   with the high tail of a short, hard contact: the brightness without the twang. The seed's
   hysteresis (1e-5 s) is effectively off. Smooth across keys; check C8 still speaks. Brings the
   fundamental forward (bell) at every dynamic.
2. **Decay fit (SPEC §3.3).** Per-partial decays from Iowa mf, the law only above the measured
   partials. Needs the engine to take measured per-partial decays (the model file plan already lists
   "measured partials"). Fixes the fundamental decaying 2× too fast and the upper partials
   lingering, and makes the high partials flash rather than ring: the shimmer.
3. **Board calibration (SPEC §3.4).** The direct/modal mix (around 0.5, the user's choice for
   clarity), weaker radiation of the lowest fundamentals (growl from the overtones), board modes up
   to ~10 kHz so the board can carry the new high partials, and less single-overtone pick-out
   (denser modes or smaller modal swings). Rumble from a low-corner (80–120 Hz) lift and the
   lowest modes, not the low mids.
4. **Then taste.** Re-listen with all three in place before any further voicing. Candidates:
   Brightness's default, rumble amount, the sympathetic bank (a detuned version, 12 partials × the
   unison's strings, was tried once and is an option for shimmer), output gain.

Order matters: the board and taste steps are only judgeable once the strings are right.

## Where things stand

Step 1 is in (§6): the felt and velocity calibration come from `data/model/hammer.json` through the
generated `engine/src/HammerFit.inc` (`python tools/fitting/fit_hammer.py all` rebuilds both,
byte-identical). Everything else is the seed sound of commit 8d80ff0 plus the sympathetic bank.
The values to reproduce what was heard:

- **Felt:** before step 1, a uniform multiplier on `feltStiffness` in `seedNote()`. ×2 was "harder
  felt"; ×0.25 removed the twang but sounded like a closed lid. Step 1's A/B variants were Q0 ×2
  (`fit-harder`) and ×0.5 (`fit-softer`) on every row of `HammerFit.inc`.
- **Output gain:** `model.outputGain` 1.35e-2 instead of 5.6e-3 (+7.6 dB). Check that single
  velocity-127 notes stay below 0 dBFS (the hammer stability test checks it).
- **Board** (`tools/seed-irs/make_seed_irs.py`, all at prototype values now):
  - "fuller board": `RADIATION_CORNER_HZ` 250, `STATISTICAL_MODES_FROM_HZ` 80;
  - "board #9": `DIRECT_LOW_SHELF_DB` 12, with `DIRECT_GAIN` 0.5 the user's favourite for clarity.

  Regenerate with `python tools/seed-irs/make_seed_irs.py`, then rebuild (the IRs are embedded).

The measuring tools are in `tools/fitting/` (see its README), including a string probe that renders
note models without the soundboard.
