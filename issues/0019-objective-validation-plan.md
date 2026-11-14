---
id: 19
title: Objective validation plan
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: [14]
---

## Question

Which measurements against the reference recordings count as "good enough" for v1, and how are they automated? Candidates include partial levels, decay times, brightness versus velocity, attack transient and band balance, building on the prototype's `compare.py`, `bands.py` and `knock.py`. Also decide:

- how to handle the fact that no free reference is a 9-ft concert grand;
- how to validate half-pedaling, which has no reference recordings;
- what role the user's blind A/B listening plays in signing off a release.

Starting point: the fit's own acceptance checks in [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md#acceptance-checks-the-fits-own-done-criteria-objective-validation-plan-sets-the-release-gates-and-may-tighten-them). This ticket decides which of them become release gates and whether to tighten them.

Also from [Touch and pedal mapping](0015-touch-and-pedal-mapping.md), decide whether these candidate checks become gates:

- At Touch 50, velocities 32/64/112 reproduce each note's pp/mf/ff loudness.
- The velocity curve rises monotonically at every Touch value.
- Overlapping note-ons don't cut notes short.
- MIDI events are timed to the exact sample.

Also from [Tuning system](0016-tuning-system.md):

- Is the stretch check a gate? That check: Railsback shape, within about ±5 cents of Iowa's measured stretch trend. Should it be tightened?
- Plan a whole-keyboard listening pass on the flat 0.7-cent unison spread (bass especially, where it barely beats). Any per-note fixes go into the voicing overrides.

Also from [Presets, saved state and automation](0018-presets-state-automation.md), decide whether these candidate checks become gates:

- The same project rendered twice is bit-identical.
- A preset change or automation while notes ring never cuts a note off.
- A state saved by the previous release loads in the new one, with every value intact.

## Resolution (2026-11-14)

Decided with the user in one grilling round (all recommendations accepted). Everything below is spec.

### Three tiers

1. **Engine checks:** fast and fully automated, run on every change. A failure blocks the change. **Never waivable.**
2. **Sound-match checks:** fully automated, run nightly and before every release. A failure blocks the release. They measure renders from the **real C++ engine** (not the Python fit's predictions) with the fitting pipeline's own measuring code, and compare them against the committed `data/reference-measurements/`. No reference audio is needed in automated runs. **Waivable by the user's ear**, with a written waiver kept with the release: which check, by how much, and why.
3. **Listening sign-off:** the user's blind verdict before each release. It is the final gate.

### Engine checks (Tier 1)

| Check | Pass condition |
|---|---|
| Velocity calibration | At Touch 50, velocities 32/64/112 reproduce each note's fitted pp/mf/ff loudness within ±1 dB |
| Velocity curve | Rises monotonically at every Touch value 0–100 |
| Overlapping note-ons | Never cut a note short |
| MIDI timing | A test note's onset lands on the exact sample of its event |
| Determinism | The same project rendered twice is bit-identical |
| Live changes | A preset change or automation while notes ring causes no sudden level drop and no click |
| State compatibility | A state saved by the previous release loads with every value intact. Each release commits its saved-state files as fixtures for the next one |

These sit alongside the existing pluginval, audio-thread allocation/lock and DSP unit tests from [Choose framework & language](0008-choose-framework-and-language.md).

### Sound-match checks (Tier 2)

| Area | Gate |
|---|---|
| Inharmonicity | Within ±10% of the Steinway D-scale values on every note. Bass must sit at or below Iowa's (one-sided, see below) |
| Decay | Within ±15% of measured decays for measurable partials; no note more than 25% off its neighbours' smoothed curve |
| Hammers | Spectral centroid ÷ f0 within ±10% of Iowa at pp, mf and ff |
| Microphones IR | Long-term spectrum within ±2 dB of Iowa per third-octave band, 100 Hz–10 kHz (one-sided below 250 Hz, see below) |
| Pianist IR | Within ±2 dB of Plath's dummy-head spectrum, 100 Hz–5 kHz |
| Irregularity | Partial-level scatter within ±2 dB of the reference |
| Stretch | Railsback shape, within ±5 cents of Iowa's measured stretch trend (one-sided at the extremes, see below) |
| Knock (`knock.py`) | Share of attack energy in 60–250 Hz in the first 30 ms at most 3 dB above the reference, on every measured note |
| Pops (`clicks.py`) | Zero detected clicks on the Prelude, a fast repeated-note test and a slow sustain-pedal sweep |
| Half-pedal endpoints | Pedal fully down matches the free-ringing strings; fully up matches the measured damper-down decay |
| Half-pedal monotonic | As the pedal rises 0→127, every note's decay time only lengthens |
| Half-pedal signature | At mid-pedal, high partials die noticeably faster than low ones |

All six fit acceptance checks from [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md) became gates with their tolerances **unchanged**: ±2 dB per third-octave band is already near the limit of audible broad colour change, and tighter numbers would chase recording noise.

**Not a 9-ft reference.** Iowa (Steinway B, 211 cm) and Salamander (Yamaha C5, 200 cm) are smaller than the modelled Steinway D (274 cm). Wherever physics predicts the direction of the difference, the gate is one-sided in exactly three places:

- **Bass inharmonicity:** at or below Iowa's (and within ±10% of the D tables).
- **Stretch in the lowest and highest octave:** up to 5 cents *less* stretched than Iowa's trend, never more.
- **Microphones band balance below 250 Hz:** up to 3 dB *above* Iowa, never below.

Everywhere else the recordings gate directly. The Pianist IR is checked against Plath, a real concert grand, so it needs no allowance.

**Half-pedaling** has no reference recording. It is covered by the three half-pedal gates above, the pedal-sweep pop check, and the user playing with a continuous pedal; the gap model's settings are finished by ear, as [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md) already says. If the user's keyboard only sends on/off pedal, rendered half-pedal sweeps stand in for live playing.

### Listening

- **Release sign-off:** a blind A/B page. A fixed set (the Prelude, plus C2, C4 and C6 at pp, mf and ff) is each paired with its reference, shuffled and unlabelled. The user rates how real each clip sounds, then the labels are revealed. The release passes when the user says it's ready. The bar is "a convincing piano next to the reference", not "indistinguishable".
- **Voicing pass:** replaces the narrower unison-spread pass. All 88 notes are rendered at mf (~4 s each) plus a pp pass, one clip per octave. The user flags uneven brightness, level, sustain or beating (bass especially, where the 0.7-cent spread barely beats). Fixes go into the voicing overrides file. It runs after the first full fit and after any refit that changes the sound, not on every release.

### Tooling

- **`forte-render`:** a command-line renderer built from the real engine. It takes a MIDI file and writes a WAV, at 48 kHz by default.
- **`tools/validate/`:** the checks, in Python, reusing the measuring code in `tools/fitting/`.
- **Report:** each run writes an HTML report with pass/fail per gate, the measured numbers, any waivers, and the blind sign-off and voicing-pass listening pages.
- **Handed to [Build, installer and release](0020-build-installer-release.md):** building `forte-render`, wiring Tier 1 into every-change CI and Tier 2 into nightly/release runs, and the CPU/performance gate.
