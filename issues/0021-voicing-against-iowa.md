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

## Plan

How to get bright + full + shimmer + rumble without twang. Twang and brightness live in different
places: twang is a loud, lingering 0.5–3.5 kHz layer over a weak fundamental; brightness and shimmer
are high partials (4–12 kHz) that flash at the strike and fade, over a strong fundamental. So:

1. **Hammer and felt fit (SPEC §3.3), with the felt's hysteresis.** Fit stiffness, exponent and
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

The seed sound is unchanged from commit 8d80ff0 plus the sympathetic bank: none of the values tried
here were kept, since the fits will set them. The values to reproduce what was heard:

- **Felt:** a uniform multiplier on `feltStiffness` in `engine/src/PianoModel.cpp`'s `seedNote()`.
  ×2 was "harder felt"; ×0.25 removed the twang but sounded like a closed lid.
- **Output gain:** `model.outputGain` 1.35e-2 instead of 5.6e-3 (+7.6 dB). Check that single
  velocity-127 notes stay below 0 dBFS (the hammer stability test checks it).
- **Board** (`tools/seed-irs/make_seed_irs.py`, all at prototype values now):
  - "fuller board": `RADIATION_CORNER_HZ` 250, `STATISTICAL_MODES_FROM_HZ` 80;
  - "board #9": `DIRECT_LOW_SHELF_DB` 12, with `DIRECT_GAIN` 0.5 the user's favourite for clarity.

  Regenerate with `python tools/seed-irs/make_seed_irs.py`, then rebuild (the IRs are embedded).

The measuring tools are in `tools/fitting/` (see its README), including a string probe that renders
note models without the soundboard.
