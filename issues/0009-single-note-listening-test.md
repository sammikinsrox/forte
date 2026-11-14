---
id: 9
title: Single-note listening test
labels: [wayfinder:prototype]
status: closed
assignee: sam
parent: 1
blocked_by: [7]
---

## Question

Does the architecture chosen in [Choose the synthesis architecture](0007-choose-synthesis-architecture.md) sound piano-like enough to commit to? Render a handful of single notes offline (low, middle and high register, soft and loud), compare them against the reference recordings, and listen together. Either confirm the architecture or identify what must change.

## Progress (2026-11-14)

Prototype: [prototypes/listening-test/](../prototypes/listening-test/) (throwaway, Python/numpy, offline). It runs with `python forte_proto.py`, then `python compare.py`. It writes `out/*.wav` and the A/B page `out/index.html`. The references are 9 Iowa MIS Steinway B notes (C2, C4, C6 × pp, mf, ff), stored in the git-ignored `refs/iowa/`.

What the renderer implements from the architecture:
- modal strings up to 20 kHz, with f0, B and per-partial two-stage decay measured from the Iowa mf note;
- a power-law felt hammer with slight hysteresis, solved at 4× during contact, with strike position and seeds from Chaigne & Askenfelt;
- Weinreich coupled unisons (2 strings at C2, 3 strings above) for the lowest 32 partials;
- phantom partials at C2 (longitudinal modes driven by the squared transverse motion);
- a synthesized stereo soundboard IR per bridge region.

Fitting that stands in for the future pipeline:
- one hammer-stiffness scale per note, fitted to the ff spectrum;
- one level per note, from ff;
- pp/mf hammer speeds fitted to the reference loudness, so brightness versus loudness is left to the physics;
- **one global colour curve** for the soundboard/mic path, fitted across all notes, standing in for the Plath calibration.

Not exercised: dampers/release, sympathetic resonance, soft pedal, restrike (single held notes only).

Findings so far (numbers from `compare.py`):
- Loudness, decay shape over 0–8 s and the growth of brightness with velocity are within a few dB of the reference for C2 and C4. The C6 early beating dip isn't reproduced.
- **The raw random-mode IR is too irregular.** On its own it swung individual partials ±20 dB, which made C4 hollow, with the 2nd partial 18 dB over the fundamental. A non-resonant direct component at 2.0 brings the swings down to about ±3–6 dB. The IR generator needs an explicit irregularity control, set by the Plath calibration.
- **Musical example (the user asked for more than single notes):** Bach, Prelude in C major BWV 846 (public domain; my own transcription), ~2:13 and 549 notes across C2–A5, rendered by `song.py`.
  - **Forte:** event-driven per-note model. Strikes on ringing strings (restrike) start from the current string state. Key-up ramps the dampers down over 20 ms (extra decay 8 + 15·√(f/1 kHz) per second). Notes rest below −100 dB. Parameters are measured per note from the Iowa mf recordings, with per-note levels taken from mf.
  - **Reference:** a simple sampler of the Iowa mf notes, with a 0.08 s fade standing in for the dampers.
  - Both are matched on average loudness. Their 10 s loudness curves agree within ~1–2 dB.
- **User's first Prelude feedback:** "not as bright", "needs a tiny bit of twangy-ness", and "a strange popping artifact" between rapid notes. What I found and fixed:
  - **Pops:** they came from the phantom-partial path pausing during hammer contact. That path now runs continuously through contact, with a 40 Hz high-pass. There were 57 detected pops before the fix and 0 after.
  - **Dull:** the hammers had been fitted to ff while the piece is played around mf, so they were refitted per note to the Iowa mf recordings. Forte is now within ~2 dB of the reference up to 8 kHz, where it had been −4 dB at 2 kHz and −9 dB at 4 kHz.
  - **Twang:** the per-partial fit had left the unisons almost perfectly in tune (often under 0.1 cent apart), and partials above 32 had no beating at all. The fix is fixed per-string offsets: the same cents on every partial, with all partials detuned (uncoupled above 32).
  - **Variants for the user:** A (brighter), B (+0.7-cent unison spread), C (+1.4 cents, phantom partials ×2).
- **User's second Prelude feedback:** pops still audible, "like a cup being placed face down on a table… like the hammers are hitting a wooden block".
  - **Cause: a modelling error.** The bridge force was taken as the string slope at x = 0, the agraffe end right next to the hammer (strike at a·L), so ~88% of the hammer blow went straight to the soundboard as a low thump. The bridge is at x = L, where the slope of mode n carries (−1)ⁿ and only ~12% of the quasi-static push arrives. Partial magnitudes are unchanged; only their phase alignment at the onset changes.
  - **Also added:** a per-note 2nd-order high-pass at f0/2 (notes above 100 Hz), which removes only the sub-fundamental strike transient.
  - **Measured:** before the fix, Forte had 10–13 dB more 60–250 Hz burst in the first 30 ms than the Steinway. After it, the share of attack energy in 60–250 Hz is equal to or below the Steinway's for C2–G4; C5 is ~6 dB above. **Real-engine spec point: bridge output uses the bridge-end slope (alternating sign per mode).**
- Remaining mismatches: the C4 2nd partial is still ~7 dB hot, C2 has a dip at partial 6, and the C6 upper partials (5+) fade faster than the reference (partly the reference's noise floor).

## Resolution (2026-11-14)

**Architecture confirmed.** On the final Prelude render the user said variant **B** "sounds closest" and "the knock is gone". Their remaining comment was that it's "not as… nice as the Steinway recording. Almost like the lid is closed… not quite as bright". The measurements agree: −1 to −3 dB at 4–8 kHz, much less above 10 kHz, and about −9 dB around 125 Hz.

Diagnosis: the gap is in the soundboard/mic stage. That stage is still a placeholder (one fitted colour curve standing in for the Plath calibration), against a close-miked, open-lid reference. Strings, hammer, unisons, dampers and restrikes held up in real music.

Carried into the real engine (spec points):
- **Bridge output** is the slope at the bridge end, with alternating sign per mode.
- **Phantom-partial path** runs continuously, including during hammer contact, with a ~40 Hz high-pass on the longitudinal force.
- **Unisons** use fixed per-string tuning offsets that are the same in cents for every partial. The default spread is about **0.7 cent** (variant B): 3 strings at −1, +0.2, +0.85 × spread; 2 strings at ±0.5 × spread. Coupled for partials 1–32, detuned and uncoupled above.
- **Per-note high-pass at f0/2** removes the sub-fundamental strike transient.
- **Hammer felt** is fitted per note at the dynamic actually played. Fitting at ff alone left mf dull.

Handed to the parameter fitting pipeline:
- open-lid brightness and air above 4 kHz;
- body around 125 Hz;
- an explicit irregularity control in the synthesized IR (the raw random-mode IR swung partials ±20 dB);
- the hot C4 2nd partial;
- C6 upper-partial decay.
