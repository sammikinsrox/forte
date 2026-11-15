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

### 6. Step 1, first try: the hammer and felt fit against Iowa (2026-11-14, branch hammer-felt-fit)

**By ear this fit was a step back** (§7): more twang, and a cheap, plasticky strike. It's kept as
`fit_hammer.py fit` for reference, and the felt is now set by ear. The measurements below still
hold; the conclusions drawn from attack windows don't.

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

### 7. Listening: the fit was worse; the felt is now set by ear (2026-11-14)

**The fit sounded worse than the seed**: "the twang is so much worse", "cheap and plasticky". Best
so far by ear was the seed felt ×0.25 ("D", from the earlier listening). On Yesterdays (board #9,
level-matched), as shares of the whole piece's energy:

| Render | 1–3.5 kHz | 3.5–8 kHz | >8 kHz | Crest factor |
|---|---|---|---|---|
| D (felt ×0.25) | −18.0 dB | −66.6 | −104 | 22.1 dB |
| seed | −13.1 | −50.5 | −92 | 22.1 |
| §6's fit | −11.1 | −44.9 | −66 | 27.9 |

Why it went wrong:

- It matched attack-window spectra (0.15 s). The twang is the whole note's 1–3.5 kHz layer, which
  went up 2 dB over the seed and 7 over D.
- Its relaxation time (~1 ms) is as long as the hammer's contact, so the relaxation reshaped the
  whole pulse and put the mids back. Its large ε made a spike at every strike: crest factor +6 dB,
  the plastic click.
- Making the top octave speak raised C8 ~30 dB. Notes from C6 up have their fundamentals at
  1–4 kHz, the twang band.

**The fix: D plus a short relaxation time.** Keep D's softness for slow compression (relaxed
stiffness = 0.25 × the seed felt) and let only the fastest compression meet a stiff felt: Q0 =
relaxed / (1 − ε), τ 0.02–0.05 ms. On the bare strings this adds 25–80 dB at 4–12 kHz with the 1–3.5
kHz share moving ≤ 2 dB. Yesterdays, board #9, level-matched:

| Variant (ε, τ) | 1–3.5 kHz | 3.5–8 kHz | >8 kHz | Crest | By ear |
|---|---|---|---|---|---|
| D | −17.7 | −67.3 | −109 | 22.1 | |
| 0.5, 0.05 ms | −17.9 | −60.5 | −91 | 22.1 | hardly different from D |
| 0.8, 0.05 ms | −16.8 | −52.3 | −82 | 22.2 | some air back; "pretty decent", twang "almost non-existent" |
| 0.9, 0.02 ms | −17.8 | −53.4 | −83 | 22.2 | **chosen** (differences "very subtle") |
| 0.95, 0.02 ms | −16.1 | −47.6 | −76 | 22.3 | |
| 0.9, 0.05 ms | −14.9 | −45.8 | −77 | 22.4 | |
| 0.95, 0.05 ms | −12.9 | −40.7 | −70 | 22.5 | twang band back at the seed's |
| D, felt eased to ×0.5 above C5 | −16.3 | −66.0 | −106 | 22.1 | (more twang, no air) |

So from the hammer, air and twang trade against each other; a shorter τ buys a little air for
free. The chosen felt is committed (`fit_hammer.py voice`). Its air shrinks slightly with velocity
at C7 (a test notes it). Velocity 127 peaks ≤ −9 dBFS on board #9.

**"Boxy", "like a speaker in a box".** Summed over Iowa's mf notes (first 2 s, third-octave shares),
Forte follows Iowa within ±7 dB up to 5 kHz, then falls off a cliff: −80 dB at 6.3 kHz, −95 at 8
kHz, −103 at 10 kHz, against Iowa's −50 to −56. But Iowa's top is its hiss: 6–16 kHz sits at the
same level before the note as during it, and the strike lifts it only 1–4 dB. So a real piano at
mf has little string energy above ~6 kHz either. What a recording has is a −50 dB floor of hiss and
room; Forte has digital silence. Other candidates for "boxy": board #9's low end (+9–10 dB at 62
and 125 Hz, +2.5–3 dB at 315–400 Hz against Iowa: the 12 dB shelf at 250 Hz lifts the low mids),
and no room. **A reverb in Reaper "sounds a little better"**: the room is missing.

**Against Pianoteq and Keyscape ("much clearer and richer")**, in the likely order of effect:

1. Compare level-matched: Forte's renders sit around −31 dBFS RMS.
2. The decay (step 2): the fundamental dies 2× too fast and the upper partials linger. Richness is
   how the tone evolves; this is also where the remaining twang lives.
3. The board and the room (step 3, plus new work): denser, better-placed board modes radiating
   to ~10 kHz, less single-overtone pick-out, the low shelf at 80–120 Hz instead of 250 Hz, a room
   (early reflections and a short tail) in the Listening perspectives (the spec has no room yet),
   and stereo width checked against a real microphone pair.
4. Quick tries for shimmer: unison spread 1.0–1.5 cents (now 0.7), the sympathetic level (a
   placeholder 0.1), and the duplex scale (not modelled).
5. Mechanical noises: the hammer's knock, dampers, release, pedal.
6. Renders of the same MIDI from Pianoteq or Keyscape, kept locally, would let the tools measure
   against the sound wanted, not only Iowa.

### 8. Quick tries on board #9 (2026-11-15)

Yesterdays, level-matched to the favourite (`build/ab/more-air/Yesterdays1/D-flash-0.9-short.wav`),
whole-piece band shares (dB of the piece's energy) and crest factor, `piece_levels.py`. The base
renderer rebuilt from HEAD reproduces the favourite to −58 dB residual. Renders in
`build/ab/shimmer-board/Yesterdays1/`.

| Variant | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k | Crest |
|---|---|---|---|---|---|---|---|
| favourite (board #9) | −12.2 | −2.0 | −5.5 | −16.3 | −53.1 | −83.3 | 22.2 |
| unison spread 1.0 cent | −12.0 | −2.0 | −5.5 | −16.3 | −53.2 | −83.7 | 22.7 |
| unison spread 1.5 cent | −11.8 | −2.0 | −5.5 | −16.3 | −53.2 | −83.8 | 23.6 |
| sympathetic ×0.5 (0.05) | −12.2 | −2.0 | −5.4 | −16.2 | −53.1 | −83.3 | 22.2 |
| sympathetic ×2 (0.2) | −12.1 | −2.0 | −5.5 | −16.3 | −53.1 | −83.4 | 22.0 |
| direct-path shelf at 100 Hz | −13.0 | −2.1 | −5.2 | −16.0 | −52.9 | −83.1 | 22.1 |
| no direct-path shelf | −12.9 | −2.1 | −5.1 | −16.0 | −52.9 | −83.1 | 22.2 |
| MODE_ROLLOFF_HZ 12000 | −12.1 | −2.0 | −5.5 | −16.1 | −51.8 | −82.8 | 22.2 |

None of them moves the balance by more than ~1 dB. Why, from the board IRs themselves:

- **The shelf hardly reaches the output at direct 0.5.** The board's low end is its fixed low modes
  (50-127 Hz), not the direct path: removing the 12 dB shelf takes only 1.5 dB off at 60-100 Hz and
  5 dB at 150 Hz. Board #9's "bass" is mostly the modes.
- **The 12 kHz roll-off adds ~1 dB above 3 kHz**: up there the board is its direct path, and at
  direct 0.5 board #9 sits 10 dB (3 kHz) to 17 dB (6-10 kHz) under its 1 kHz level. That tilt, not
  the modes' roll-off, is the board's share of the dull top.
- **The spread changes time, not spectrum**: more beating, so peaks rise (crest +1.4 dB at 1.5
  cents). Listen for a slow shimmer or chorus in held notes, against sourness in the melody.
- **The sympathetic level** is a halo under held chords and pedalled passages: listen to the
  room-like ring after a chord, not the strike.

### 9. Keyscape's Yesterdays: what "fuller and richer" is (2026-11-15)

The user's Keyscape render (`C:\Users\sam\Downloads\yesterday-keyscape.wav`, kept locally; a
level-matched copy is `build/ab/decay-fit/Yesterdays1/keyscape.wav`), against the favourite:

| | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k | Crest |
|---|---|---|---|---|---|---|---|
| favourite | −12.2 | −2.0 | −5.5 | −16.3 | −53.1 | −83.3 | 22.2 |
| Keyscape | −6.8 | −5.1 | −3.7 | −12.6 | −37.5 | −57.1 | 24.1 |

By third octave, Keyscape has 10-18 dB more at 60-160 Hz, 3-5 dB less at 315-400 Hz, 5-14 dB more
at 0.8-3 kHz and 15-29 dB more at 4-12 kHz. Forte's energy is piled into 300-600 Hz: that's the
box.

**Correction (§12):** Keyscape's render runs ~0.4% faster than the MIDI (its offset slides from
−0.04 s to ~−1.3 s over the piece), and the table below aligned it with one global offset. Its
Keyscape row is therefore close to random-time averages and doesn't support the conclusion
drawn from it. §12 measures with a time map. The band shares are unaffected.

**The twang isn't the 1-3.5 kHz share.** Keyscape has 3.7 dB more there and isn't twangy. What
differs is when it sounds. Energy in the first 0.12 s after each onset over the rest (dB):

| | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k |
|---|---|---|---|---|---|---|
| favourite | −8.2 | −5.9 | −5.2 | **−1.8** | **+1.2** | +2.0 |
| Keyscape | −7.9 | −6.7 | −6.5 | **−5.7** | **−6.5** | −6.8 |

Forte's top is nearly all in the strikes, over a dull, mid-heavy sustain: a bright flash that
dies, which is what a plucked string does. Keyscape's highs carry on through the sustain as evenly
as its lows. That's both "richer" (the tone keeps its colour as it rings) and the likely real
twang. Making the 1-3.5 kHz layer quieter, as the felt did, treated the symptom and dulled the
sound.

Iowa agrees (§10): its fundamentals fall fast at first and its upper partials keep pace, so its
sustain is no darker than its strike. Part of Keyscape's even sustain is surely its room (samples
carry one, and a room tail sustains every band alike); "a reverb in Reaper helped a little" (§7)
fits that.

### 10. Step 2: the decay fit (2026-11-15)

`tools/fitting/fit_decay.py` (measure → fit → build, byte-identical; its docstring has the method),
measuring with `decay_measure.py`.

**Measuring.** A line fitted to a partial's envelope follows its beats, so per-partial "decay
rates" scatter wildly (§2's numbers among them). Mean levels over log-spaced windows (0.05-16 s)
don't, and they show the shape. Iowa's C4 fundamental falls 32 dB in its first second, then
~1.7 dB/s; its partials 2-7 fall only 12-18 dB in that second. Forte's (seed) fell 10 dB in its
first second and then 15 dB/s: no knee. So "the fundamental dies 2× too fast" (§2, a line over
0.3-2.3 s) was a line across a knee. The real fault is the shape: Iowa's prompt sound is fast and
its aftersound slow, and Forte's was neither. Iowa mf rings to its noise floor (17-51 s), so
both stages are measurable: 32 partials in the bass, 11 at C4, 4 at C6.

**The engine change.** NoteParameters gains measured partials: a prompt-sound and an aftersound
rate for each of partials 1 to `numMeasuredPartials` (up to 32, the coupled partials), with the
laws σ = c1 + c3·f² above them (SPEC §3.3, §3.5's "measured partials"). Each partial's unison is
coupled from its own two rates, so the fixed spread then gives the knee. The sympathetic bank's
modes take each partial's prompt rate. The data comes from the generated
`engine/src/DecayFit.inc`, like the hammer's.

**Fitting.** Per partial, the two rates whose envelope, solved exactly as UnisonModes solves it
(numpy, matching the probe to 0.2 dB) and measured through the same windows, best matches Iowa.
Two lessons:

- Single Iowa notes' fitted rates scatter 2-5× between neighbours: each real unison has its own
  tuning, which sets its knee, and Forte has one spread. Fitting each note to the median of its
  ±3 semitones' window levels cut the outliers from 293 to 55 of 754 partials.
- Late tails near Iowa's floor gave aftersound rates near zero (minutes-long T60s) and an
  engine tail of 909 s. Both rates are held at ≥ 0.12/s (T60 57 s).

**What came out.** C4: partials 1-3 two-stage, prompt ≈2.1-2.5/s (−18 to −22 dB/s) into an
aftersound ≈0.12-0.15/s; partials 4 and up single-stage at ≈0.8-1.2/s. C2: prompt 0.4-1.0/s. The
law's f² term is small (Iowa levels off above ~2 kHz, §2), so the highs decay at ~1.2-2/s
(−10 to −17 dB/s) instead of the seed's 4.7/s at 5 kHz. Outside C2-C6, notes borrow the rates at
the same frequency from the nearest measured note. Through the engine (probe, `fit_decay.py
check`), partials 1-4 match Iowa's medians within 1.6-4.4 dB rms on every note.

**Pieces** (board #9, level-matched to the favourite; renders in `build/ab/decay-fit/<piece>/`).
Yesterdays:

| Render | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k | Crest |
|---|---|---|---|---|---|---|---|
| favourite | −12.2 | −2.0 | −5.5 | −16.3 | −53.1 | −83.3 | 22.2 |
| decay fit | −10.9 | −2.3 | −5.2 | −14.4 | −50.6 | −78.6 | 23.3 |
| decay fit, partials 1-3 only (seed law above) | −10.9 | −2.3 | −5.2 | −14.4 | −51.1 | −81.5 | 23.4 |
| Keyscape | −6.8 | −5.1 | −3.7 | −12.6 | −37.5 | −57.1 | 24.1 |

The other three pieces move the same way: bass +1.2-1.3 dB, 1-3.5 kHz +1.7-2.0, 3.5-8 kHz
+2.4-2.7, >8 kHz +4.3-6.5, crest +0.6-1.2. The fundamentals' fast prompt sound makes the low mids
flash and fade (attack/sustain at 250-500 Hz −5.9 → −4.2 dB on Yesterdays). The partials-1-3
variant shows that's where the change comes from; the upper partials' fit adds the air above
8 kHz. Every move is towards Keyscape, but a fraction of the way: the strings' decay isn't most
of the gap. The rest is the board's tilt and low end (§8) and the room (§9).

**By ear:** "the twangy-ness is a lot better", and "the characteristics of the sound seem
fine", but the colour is wrong: no brightness, crispness, clarity, top end or shimmer, "emitted
from inside a box", muffled. Through the same reverb, Keyscape "sounds a million times better":
the room isn't the gap.

### 11. Colour: the top end (2026-11-15)

With the twang now coming from the decay rather than the felt, the levers that cut the top can
come back. Against Keyscape (Yesterdays, decay fit on board #9), Forte was 5-12 dB short at
1.2-3 kHz and 13-21 dB at 4-10 kHz. Three things cut it:

1. **The felt** at ×0.25, chosen when the twang was really the decay.
2. **Board #9's direct path** at 0.5: 10-17 dB under its 1 kHz level above 3 kHz (§8).
3. **The board's modes:** each mode's gain goes as 1/√damping and damping rises with frequency,
   so the generated modes' energy falls steeply through the treble. A new generator setting,
   `MODE_TILT_DB_PER_OCTAVE` (off by default, so the committed IRs are unchanged), tilts the
   modes' gains above 1 kHz. At +6 dB/octave with `MODE_ROLLOFF_HZ` 12000, board #9 is nearly
   flat to 4 kHz: +8 to +11 dB at 2-10 kHz ("board-bright").

All with the decay fit, level-matched (renders in `build/ab/colour/<piece>/`). Yesterdays:

| Render | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k | Crest |
|---|---|---|---|---|---|---|---|
| decay fit (board #9, felt ×0.25) | −10.9 | −2.3 | −5.2 | −14.4 | −50.6 | −78.6 | 23.3 |
| felt ×0.5 | −11.7 | −2.7 | −4.7 | −12.5 | −45.1 | −71.9 | 23.3 |
| felt ×1 | −12.3 | −3.0 | −4.4 | −10.9 | −40.1 | −64.9 | 23.5 |
| board-bright | −8.4 | −2.5 | −6.1 | −13.4 | −40.1 | −69.5 | 22.9 |
| felt ×0.5 + board-bright | −9.1 | −2.8 | −5.5 | −11.3 | −34.4 | −62.5 | 23.3 |
| Keyscape | −6.8 | −5.1 | −3.7 | −12.6 | −37.5 | −57.1 | 24.1 |

The other pieces move the same way. Felt ×0.5 + board-bright matches Keyscape within ±4 dB by
third octave from 1.2 to 10 kHz, running ~4 dB hot at 3-4 kHz. Still missing: 6-8 dB at 80-125 Hz,
4.6 dB too much at 400 Hz, and the top's timing (Forte's highs are still mostly in the strikes;
Keyscape's carry through the sustain).

**By ear:** felt ×0.5 + board-bright has the best colour, "but being extra bright like that, it
begins to sound plasticy on the attack of the note, losing some bell quality."

**The plastic is the impact flash.** The felt's stiffness at impact is Q0 = relaxed / (1 − ε),
so ×0.5 at ε 0.9 hits twice as hard at the instant of contact as the old ×0.25. On the bare
strings (probe, mf) that adds no level spike (peak over body moves ≤ 0.5 dB), but C4's 4-8 kHz
partials come out 28 dB hotter at the strike (−52 against −80 dB re the reference) and die at
once; board-bright lifts that region another ~10 dB. Lowering ε keeps the relaxed felt's body
and drops the flash: at ε 0.8 (Q0 the same as the old felt's) the 2-4 kHz band stays within
4 dB of ×0.5/ε 0.9 and the 4-8 kHz flash is gone. So the idea is: brightness from the felt's body
and the board, which carry through the note, not from the strike. Renders in
`build/ab/attack/<piece>/`, level-matched to felt ×0.5 + board-bright. Yesterdays:

| Render | <250 | 250-500 | 0.5-1k | 1-3.5k | 3.5-8k | >8k | Crest |
|---|---|---|---|---|---|---|---|
| felt ×0.5, ε 0.9, board-bright (liked colour) | −9.1 | −2.8 | −5.5 | −11.3 | −34.4 | −62.5 | 23.3 |
| felt ×0.5, ε 0.85, board-bright | −9.2 | −2.8 | −5.5 | −11.7 | −37.5 | −67.0 | 23.2 |
| felt ×0.5, ε 0.8, board-bright | −9.3 | −2.8 | −5.4 | −11.8 | −39.4 | −71.0 | 23.1 |
| felt ×0.7, ε 0.8, board-bright | −9.6 | −3.0 | −5.2 | −10.8 | −36.1 | −67.0 | 23.5 |
| felt ×0.5, ε 0.8, tilt +8 dB/oct ("board-brighter") | −7.3 | −2.9 | −6.2 | −12.0 | −36.7 | −66.6 | 22.5 |

The other three pieces move the same way. Board-brighter also gains ~2 dB of bass (the IRs are
normalised over 100 Hz-1 kHz) and has the softest strikes (crest −0.6 to −0.8 dB).

**By ear:** "the plasticy sound mostly went away. It's a lot more crisp. It sounds almost
perfect." Taken to be felt ×0.5, ε 0.8, board-brighter (the suggested one). Left: "the high
notes sound weak, not velocity wise, just thin and lacking fullness" next to Keyscape.

### 12. The high notes (2026-11-15)

**Measuring against Keyscape note by note.** Keyscape's render drifts ~0.4% against the MIDI, so
onsets are mapped through an offset measured every 20 s (envelope cross-correlation near the
running drift), then refined per note to the fundamental's steepest rise. Yesterdays has its
pedal down 73% of the time, and its high notes' keys are released after a median 0.12 s, so
notes are split by whether the pedal stays down through their first second.

**What "thin" is.** At the strike the high notes match Keyscape's (within ~1 dB over the first
0.35 s). After that Keyscape's hold and Forte's keep falling. Fundamental of G4-F#5 with the pedal
down, dB re the strike: Keyscape −5.5 at 0.6-1 s, Forte −19.8; second partial −8.8 against
−14.3. G5-D#6 second partial −6.7 against −12.7. Forte follows Iowa's anechoic Steinway (§10),
whose treble prompt sound is fast (C5 −24 dB/s); Keyscape's (a close-miked Yamaha C7, pedal-down
samples) sings on.

**What doesn't do it.** A more uneven strike or a wider unison spread (numpy unison model on
E5/C6's fitted rates) moves the knee only to ~−15 dB: the energy goes into the out-of-phase
modes, which barely move the bridge. Moving the prompt rate towards the aftersound's (by 0.4 or
0.2 from G5 up) closes about 6 of the 14 dB on fundamentals and nothing on the upper partials,
which are mostly single-stage.

**What does** (all on felt ×0.5, ε 0.8, board-brighter; voicing overrides on top of the fit, as
SPEC §3.2's per-note decay offsets; renders in `build/ab/treble/<piece>/`, level-matched to
`current.wav`). Level at 0.6-1 s re the strike, pedal down unless stated:

| Render | G4-F#5 p1 | G4-F#5 p2 | G5-D#6 p1 | G5-D#6 p2 | E6+ p1, pedal change |
|---|---|---|---|---|---|
| Keyscape | −5.5 | −8.8 | −16.0 | −6.7 | −15.8 |
| current | −19.8 | −14.3 | −20.3 | −12.7 | −23.9 |
| sympathetic ×3 | −15.5 | −11.5 | −20.2 | | |
| treble decay ×0.5 (every rate, blended in from C4 to G5) | −16.0 | −11.8 | −19.1 | −8.8 | −23.0 |
| treble decay ×0.5 + sympathetic ×3 | −12.5 | −10.7 | −18.8 | −8.9 | −23.1 |
| … + treble dampers ×0.3 | −12.5 | −10.7 | −18.9 | −8.9 | −14.2 |

The pedal-down halo (the sympathetic bank, a placeholder level) is part of the fullness: ×3
adds 3-4 dB to the high notes' tails. Forte's dampers stop high notes as hard as middle ones
(160-250 dB/s); real treble dampers are small and weak, so a pedal change only partly stops
them. Weaker treble dampers recover 9-13 dB from E6 up and 1-3 dB at G5-D#6. Whole-piece band
shares move by under 1 dB in all of these.

**By ear:** "a little better but it's subtle"; the Forte versions are hard to tell apart. The
high notes now "sound a little too much like a bell and less like a piano … like a metal bell",
"not a smooth sound like what Keyscape offers", "wirey". Sustain is long enough.

### 13. Single-note references (2026-11-15)

Pieces can't separate the candidates any more (pedalled overlap, the reference's drift), so the
next comparison is note by note: `tools/fitting/make_note_sweep.py` writes every note at
velocities 32/64/96/127, 3 s held, 2 s apart, no pedal (29 min;
`build/ab/single-notes/note-sweep.mid`, with Forte's render `forte-current.wav` beside it). The
user renders it through Keyscape. Candidates for "metal bell" and "wirey" in the high notes,
each measurable per note against it:

1. Inharmonicity: the seed's B curve (1.6e-3 at C6, 4e-3 at C7) is unmeasured; too much stretch
   is what makes a bell sound metallic.
2. Too clean: Forte's high notes are pure partials with smooth exponential envelopes, without the
   knock, the board's thump and the slight wavering that a real treble note carries (the plan's
   mechanical-noise step).
3. Upper partials ringing on (board-brighter, the treble decay override) where the reference's
   high notes settle to their first partials.
4. The unison's ~1 Hz beating in the treble at the fixed spread.

**Keyscape's render:** _pending._

## Plan

How to get bright + full + shimmer + rumble without twang. Twang and brightness live in different
places: twang is a loud, lingering 0.5–3.5 kHz layer over a weak fundamental; brightness and shimmer
are high partials (4–12 kHz) that flash at the strike and fade, over a strong fundamental. So:

1. **Felt set by ear (§7); the fit against Iowa didn't work by ear (§6).** **Hammer and felt fit (SPEC §3.3), with the felt's hysteresis.** Fit stiffness, exponent and
   hysteresis per note, plus the velocity calibration, against Iowa pp/mf/ff (C2/C4/C6) and mf
   (all notes), measuring the strings without the board. Felt hysteresis is the key extra freedom:
   a felt that is stiffer the faster it's compressed (Stulov) has a steep spectrum through the mids,
   with the high tail of a short, hard contact: the brightness without the twang. The seed's
   hysteresis (1e-5 s) is effectively off. Smooth across keys; check C8 still speaks. Brings the
   fundamental forward (bell) at every dynamic.
2. **Decay fit, done pending the ear (§10).** **Decay fit (SPEC §3.3).** Per-partial decays from Iowa mf, the law only above the measured
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

After §9, step 3 grows: against Keyscape the biggest gaps are the board's (its low modes, its
10-17 dB tilt above 3 kHz at direct 0.5) and the missing room, which sustains every band alike.
A first room (early reflections, a ~1 s tail) in the Listening perspectives is worth trying
before more string work; the spec has no room yet.

## Where things stand

The felt is set by ear (§7): `data/model/hammer.json` (written by `fit_hammer.py voice`) through the
generated `engine/src/HammerFit.inc` (`python tools/fitting/fit_hammer.py all` rebuilds both,
byte-identical). Velocity calibration: the seed's speeds. The strings' decay is fitted to Iowa
(§10): `data/model/decay.json` through `engine/src/DecayFit.inc` (`python tools/fitting/fit_decay.py
all`). Everything else is the seed sound of commit 8d80ff0 plus the sympathetic bank.
The values to reproduce what was heard:

- **Felt:** before step 1, a uniform multiplier on `feltStiffness` in `seedNote()`. ×2 was "harder
  felt"; ×0.25 removed the twang but sounded like a closed lid. Step 1's A/B variants were Q0 ×2
  (`fit-harder`) and ×0.5 (`fit-softer`) on every row of `HammerFit.inc`. §7's variants changed
  `FELT_SCALE`, `RELAXATION` and `RELAXATION_TIME` in `fit_hammer.py`.
- **Decay:** before step 2, the seed laws in `seedNote()` (prompt T60 20 s at A0 to 2 s at C8,
  c3 1.5e-7, aftersound 4× slower, no measured partials). §10's partials-1-3 variant capped
  `numMeasuredPartials` at 3 with those laws above.
- **Output gain:** `model.outputGain` 1.35e-2 instead of 5.6e-3 (+7.6 dB). Check that single
  velocity-127 notes stay below 0 dBFS (the hammer stability test checks it).
- **Board** (`tools/seed-irs/make_seed_irs.py`, all at prototype values now):
  - "fuller board": `RADIATION_CORNER_HZ` 250, `STATISTICAL_MODES_FROM_HZ` 80;
  - "board #9": `DIRECT_LOW_SHELF_DB` 12, with `DIRECT_GAIN` 0.5 the user's favourite for clarity;
  - "board-bright" (§11): board #9 plus `MODE_TILT_DB_PER_OCTAVE` 6, `MODE_ROLLOFF_HZ` 12000.

  Regenerate with `python tools/seed-irs/make_seed_irs.py`, then rebuild (the IRs are embedded).

The measuring tools are in `tools/fitting/` (see its README), including a string probe that renders
note models without the soundboard.
