---
id: 14
title: Parameter fitting pipeline
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

How does Forte turn the reference recordings and published data into the model's per-note parameters, and what exactly does it ship? Cover the following, deciding the method, sources and acceptance check for each:

- **Strings:** f0, inharmonicity B and per-partial decay (fast/slow) for all 88 notes, including notes where few partials can be measured.
- **Unisons:** whether the ~0.7-cent default spread from [Single-note listening test](0009-single-note-listening-test.md) is fitted per note or set as a curve.
- **Hammers:** felt stiffness, exponent and mass per note, fitted across velocity layers so brightness follows loudness.
- **Soundboard IRs:** both soundboard IR calibrations (Microphones and Pianist) against Plath. Include an explicit irregularity control, plus the open-lid air above 4 kHz and the body around 125 Hz that the listening test found missing.
- **Tooling:** the offline tool chain (language, where it lives, how it runs) and the shipped data format.

## Resolution (2026-11-14)

Decided with the user over two grilling rounds. Everything below is spec.

### Pipeline

1. **Three offline steps, each with committed output.**
   - **measure:** recordings → `data/reference-measurements/`.
   - **fit:** published seeds + measurements → model parameters.
   - **build:** parameters + voicing overrides → the shipped model file and IRs.
   - The plugin never fits. Committed outputs let later steps re-run without the recordings.
2. **Tooling:** Python (numpy, scipy, soundfile) in `tools/fitting/`.
   - One command per step plus `all`.
   - Fixed random seeds, so output is byte-identical every run.
   - `tools/fetch-references` downloads Iowa, Salamander and Plath into the git-ignored `refs/` folder.
   - No C++ build is needed to run it.
3. **Voicing overrides:** a small, hand-edited file of per-note brightness, level and decay offsets, applied on top of the fit in the build step. Refits never overwrite decisions made by ear.

### Strings

4. **Inharmonicity B** is computed from the published Steinway D string scale (Chabassier & Duruflé) and smoothed across keys. Iowa (Steinway B) only checks the trend. If the D tables can't be obtained, the fallback is Iowa's B scaled by the B-to-D string-length ratio (B ∝ 1/L²).
5. **f0 is not fitted.** This pipeline ships B, and [Tuning system](0016-tuning-system.md) turns A4, stretch and B into pitches.
6. **Per-partial decay** is measured from Iowa **mf**:
   - Partials at least 10 dB above the noise floor keep their own measured fast and slow rates and slow-stage level.
   - Higher partials follow a per-note law, σ = b1 + b3·f².
   - The law coefficients are smoothed across neighbouring notes, with outliers rejected.
   - The top octave borrows its law from its neighbours.
   - A truncated or noisy tail (C6) is never trusted over the law.
7. **Unison spread is not fitted.** The spread curve belongs entirely to [Tuning system](0016-tuning-system.md); the default is 0.7 cent from [Single-note listening test](0009-single-note-listening-test.md).
8. **Unison coupling strength** per partial is derived from that partial's fast and slow decay, its slow-stage level and the fixed spread. This replaces the prototype's detuning search.
9. **Phantom partials:** level fitted per note (lowest ~30 notes) against Iowa **ff**.

### Hammers

10. **Mass:** a smooth curve from Chaigne & Askenfelt seeds, not fitted.
11. **Felt stiffness and exponent:** fitted per note against Iowa pp, mf and ff together.
    - For each dynamic, the hammer speed that matches the recording's loudness is solved first.
    - The felt values are then fitted so brightness matches at all three dynamics.
    - The result is smoothed across keys.
    - Salamander's 16 layers check the shape of the brightness-versus-loudness curve between those points.
12. **Velocity calibration hand-off:** the three matched hammer speeds per note go to [Touch and pedal mapping](0015-touch-and-pedal-mapping.md) as calibration points for the velocity curve.
13. **Soft-pedal felt:** seeded from the literature as a fixed offset from the normal felt, then set by ear. There is no reference recording for it.

### Dampers and sympathetic resonance

14. **Damper-down damping:** a smooth law over frequency and key, measured from Salamander's release samples.
15. **Half-pedal gap model:** seeded from Lehtonen et al. and Stulov, Välimäki & Lehtonen, then set by ear.
16. **Undamped region:** where it starts comes from the Steinway D data.
17. **Sympathetic bank:** mode frequencies and decays come straight from the fitted strings. Only the 100% level is calibrated: Forte's pedal-down minus pedal-up spectrum difference should match the same difference in Piano in 162. That set is measured locally and never shipped.

### Soundboard IRs

18. **Shared board:** one generator; both perspectives use the same low modes and per-band decay times, taken from Plath's bridge accelerometer.
19. **Microphones colour:** fitted by render-and-compare.
    - Forte's long-term spectrum per bridge region is matched in third-octave bands against Iowa, with Salamander as the cross-check.
    - This targets the open-lid air above 4 kHz and the body around 125 Hz that the listening test found missing.
20. **Pianist colour:** the bridge-to-ear transfer from Plath (accelerometer vs dummy head, H1 estimate), pooled across keys per bridge region, with head-related detail above ~2 kHz smoothed out.
21. **Irregularity:** an explicit generator parameter, the mix of resonant modes against a smooth direct path. It's fitted so the scatter of partial levels around a smooth envelope matches the reference. It is not a user knob in v1.
22. **Length:** 20k taps (~0.42 s at 48 kHz), as benchmarked, with a gentle fade on the few lowest modes.

### What ships

23. **One model file**, `data/model/concert-grand.json`, with a schema version. It holds per-note B, decay laws and measured partials, coupling, phantom levels, hammer and damper values, the sympathetic level and the IR generator parameters. Another piano later is another model file.
24. **The IRs:** 32-bit float WAV at 48 kHz, 4 regions × 2 perspectives, stereo, ~3 MB. They're resampled once at load for other host rates.
25. **Packaging:** both are embedded in the plugin binary. `CREDITS.md` credits Iowa, Salamander (CC BY 3.0), Plath (CC BY 4.0, modified) and the papers. No reference audio ships.

### Acceptance checks (the fit's own done-criteria; [Objective validation plan](0019-objective-validation-plan.md) sets the release gates and may tighten them)

| Area | Check |
|---|---|
| Inharmonicity | Within ±10% of the D-scale values on every note, and the Iowa trend agrees in shape |
| Decay | The law reproduces measured decays within ±15% for measurable partials, with no note more than 25% off its neighbours' smoothed curve |
| Hammers | Spectral centroid ÷ f0 within ±10% of Iowa at pp, mf and ff |
| Microphones IR | Long-term spectrum within ±2 dB of Iowa per third-octave band, 100 Hz–10 kHz |
| Pianist IR | Within ±2 dB of Plath's dummy-head spectrum, 100 Hz–5 kHz |
| Irregularity | Partial-level scatter within ±2 dB of the reference. The C4 2nd partial is no longer hot and the C2 partial-6 dip is gone |
| Overall | The user signs off in a Prelude A/B against Iowa |
