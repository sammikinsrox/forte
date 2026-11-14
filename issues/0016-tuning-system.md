---
id: 16
title: Tuning system
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

How is Forte tuned?

- **Stretch:** where the stretch curve comes from (Railsback from the measured B, or the reference recordings) and what the Stretch knob scales.
- **Unisons:** the default spread and whether it varies across the keyboard.
- **Pitch:** the A4 range.
- **Temperaments:** whether v1 offers alternatives, or MIDI Tuning Standard / MTS-ESP.

Inputs from [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md):
- Per-note inharmonicity B, from the Steinway D scale. This ticket turns A4, stretch and B into each note's f0. The pipeline does not fit f0.
- This ticket solely owns the unison spread curve. The default is 0.7 cent; the pipeline does not fit spread.

## Resolution (2026-11-14)

Decided with the user in one grilling round plus a confirmation. Everything below is spec.

### Pitch calculation

1. **A simulated tuner sets every note's pitch** from the model's inharmonicity B, A4 and Stretch. The pitches are not measured from recordings.
   - It starts at A4, sets A3–A4 to equal temperament, then tunes outward one note at a time.
   - **About C3–C6:** 4:2 octaves. The lower note's 4th partial is matched to the upper note's 2nd.
   - **Below about C3:** 6:3 matching, which pulls the low bass flatter.
   - **Above about C6:** 2:1 matching, because the higher partials there are too faint to tune by.
   - The tuner sets each note's average string pitch, and the unison spread is applied around it.
2. **When it runs:** whenever A4 or Stretch changes. It runs off the audio thread and takes a few milliseconds for 88 notes. Ringing modes glide to the new pitches over ~50 ms, as decided in [Choose the synthesis architecture](0007-choose-synthesis-architecture.md).
3. **Check:** the curve should have the Railsback shape and stay within about ±5 cents of Iowa's measured stretch trend. Iowa is a sanity check on the shape, not a target, and [Objective validation plan](0019-objective-validation-plan.md) may tighten the check.

### Controls

4. **Stretch:** 0–100, default 50. It scales each note's distance from plain equal temperament: 0 = none (exact 2:1 octaves), 50 = the calculated curve, 100 = double. It's a smooth multiplier and does not switch which partials the tuner matches.
5. **A4:** 415–466 Hz in whole-Hz steps, default 440.
6. **No transpose or extra fine-tune.** A4 and Stretch are the only tuning controls. Hosts already transpose MIDI.

### Unisons

7. **Default spread:** a flat 0.7 cent on every note. The string offsets from [Single-note listening test](0009-single-note-listening-test.md) stay: −1 / +0.2 / +0.85 × spread for three strings, ±0.5 × spread for two.
8. **How it's stored:** a per-note spread curve times one overall amount. Individual notes can be adjusted by ear in the voicing overrides file ([Parameter fitting pipeline](0014-parameter-fitting-pipeline.md)). Whether the overall amount gets a knob is for [Editor control set](0017-editor-control-set.md).
9. **Listening:** checking the spread across the whole keyboard belongs to [Objective validation plan](0019-objective-validation-plan.md).

### Scope

10. **Stretched equal temperament only.** Historical temperaments, MTS-ESP and MIDI Tuning Standard are out of scope for v1. Tuning is calculated, not stored, so they can be added later without rework.
11. **No random tuner imperfection.** Tuning is exactly as calculated. If listening finds Forte too clean, it can be added later as a voicing tweak.
