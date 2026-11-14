---
id: 1
title: "Map: Forte v1 spec"
labels: [wayfinder:map]
status: closed
assignee:
parent:
blocked_by: []
---

## Destination

A build-ready v1 spec for **Forte**, a free physically modeled concert-grand piano plugin. The synthesis architecture, tech stack, physics scope, editor design and validation plan are all decided, so implementation can start with no open questions.

**Reached 2026-11-14.** The compiled spec is [SPEC.md](../SPEC.md).

## Notes

- **Domain:** real-time audio DSP and audio plugin development. Vocabulary lives in [GLOSSARY.md](../GLOSSARY.md).
- **Roles:** Claude writes all code and DSP. The user plays builds, judges the sound by ear, and owns product decisions. The user is new to DSP, C++ and Rust, so explain every technical decision in plain language with a clear recommendation.
- **Settled constraints (from charting):**
  - Free release, open source under GPLv3.
  - v1 targets Windows, VST3 plus standalone. Test hosts are FL Studio and Reaper.
  - One modern concert grand, built on a parameterized model so other pianos can come later as presets.
  - v1 physics: felt-hammer/string interaction, string stiffness (inharmonicity), 2–3 detuned coupled strings per note (two-stage decay), soundboard and body, dampers with sustain pedal including half-pedaling, sympathetic resonance, soft pedal (una corda).
  - No built-in reverb.
  - The editor is a beautiful, properly designed custom UI from the start, not a basic knob panel.
  - Performance: a full 88-key sustain-pedal glissando plays without dropouts at a 128-sample buffer, using no more than about 25% of one core on a mid-range PC from around 2020.
  - Freely licensed recordings of real grands (e.g. Salamander Grand, CC-BY) are the objective reference. The user makes the final call by ear.
- **Skills:** grilling tickets use `grilling` + `domain-modeling`. Prototype tickets use `prototype`. Research tickets use `research` and write findings to `research/<name>.md`.
- **Tracker:** local markdown, see [issues/README.md](README.md).

## Decisions so far

<!-- one line per closed ticket -->

- [String & hammer modeling approaches](0002-string-hammer-modeling-approaches.md): modal strings (one resonator per partial) driven by a nonlinear felt-mass hammer, after Bank/Zambon/Fontana 2010. Waveguides are the fallback; FDTD is offline only. Estimated at roughly 10–12% of a core.
- [Soundboard, string coupling, dampers & sympathetic resonance models](0003-soundboard-coupling-damper-models.md): one-way plumbing. Weinreich-style coupled unison modes, a convolution soundboard, damping weighted by damper position plus a gap model for half-pedaling, about 1,000 per-key sympathetic resonators, and a soft pedal that changes which strings the hammer strikes. Estimated at about 7–22% of the budget.
- [Plugin framework: JUCE/C++ vs nih-plug/Rust](0004-plugin-framework-comparison.md): JUCE 9 (C++, AGPLv3, GPLv3-compatible) with CMake. The VST3 SDK has been MIT-licensed since 3.8. nih-plug is rejected because upstream is in maintenance mode and its GUI story is weak. Leaning toward a WebView editor.
- [Reference recordings & what we can measure from them](0005-reference-recordings-and-measurements.md): Iowa MIS Steinway B for per-note measurements, Salamander C5 for velocity and release, Piano in 162 for pedal resonance. Seed values come from published Steinway D tables. Ship derived numbers, never the audio.
- [Soundboard impulse response source](0012-soundboard-impulse-response-source.md): no usable measured grand IR exists. Forte synthesizes its own IR offline from published soundboard data, then calibrates it against the Plath concert-grand dataset (CC BY 4.0, with bridge accelerometer and dummy-head recordings). A physical plate model is deferred past v1.
- [Editor look & feel direction](0006-editor-look-and-feel.md): variant D. A white/ebony fallboard simple view (three big dials, keyboard) expands to an advanced view with a live overtone display, tuning curve and paired knobs. Values are whole numbers you can type. Light and dark themes.
- [Choose framework & language](0008-choose-framework-and-language.md): JUCE 9 (C++), CMake, VST3 + Standalone. Forte is GPLv3-or-later and JUCE is AGPLv3. WebView vs native editor goes to its own ticket. pluginval, audio-thread allocation/lock tests and DSP unit tests are required from the first commit.
- [Listening perspective: pianist's seat or mics over the lid?](0013-listening-perspective.md): both. A Microphones | Pianist toggle (Microphones by default) in both editor views, each with its own calibrated soundboard IR. Bass on the left. A Width knob in the advanced view.
- [Modal resonator CPU benchmark](0011-modal-resonator-cpu-benchmark.md): measured on a Ryzen 5 5500. The full 88-key sustain glissando (~10,000 string modes, 1,056 sympathetic modes, 20k-tap stereo convolution) costs about 4% average / 6% worst-1% of one core with AVX2, and about 8% / 12% with SSE2. That fits the 25% target with about 3× headroom, before a 2–3× allowance for the rest of the real engine.
- [Editor rendering approach: WebView vs native JUCE](0010-editor-rendering-approach.md): native JUCE drawing (custom LookAndFeel, embedded Jost/Italiana fonts). It uses about 75 MB vs about 450 MB, opens instantly vs about 0.4 s, and needs no WebView2. Both builds were verified in Reaper; FL Studio is to be rechecked in the first real build.
- [Choose the synthesis architecture](0007-choose-synthesis-architecture.md): the engine design.
  - **Strings and hammer:** each note's unison is modal strings up to 20 kHz with real 1/2/3 string counts, coupled two-stage decay and bass phantom partials. A hardening felt hammer computes its contact at 4×.
  - **Sound path:** one-way bridge feed → damper-gated sympathetic bank → four bridge-region stereo convolutions → Brightness → Width → Volume.
  - **Running:** 88 permanent notes with true restrikes and no voice stealing, run at the host rate.
  - Estimated at ~15–25% of a core for a full glissando.
- [Single-note listening test](0009-single-note-listening-test.md): architecture confirmed by ear on single notes and a full Bach Prelude.
  - **Chosen settings:** unison spread ~0.7 cent (variant B).
  - **Bug fixes carried into the engine:** bridge output taken at the bridge end (that fixed the "wooden knock"), a continuous phantom-partial path, and an f0/2 high-pass.
  - **Remaining gap:** a "closed lid" sound, owed to the uncalibrated soundboard/mic stage. That goes to the fitting pipeline.
- [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md): offline Python in `tools/fitting/` (measure → fit → build). Its output is one versioned model file plus 48 kHz IRs, with ear-set voicing overrides layered on top.
  - **Strings:** inharmonicity from the Steinway D string scale; decay from Iowa mf.
  - **Hammers:** felt fitted to Iowa pp, mf and ff.
  - **Soundboard IRs:** Microphones colour fitted to Iowa; Pianist colour from Plath's dummy head; a fitted irregularity control.
  - **Hand-offs:** f0 and unison spread go to Tuning; hammer-speed points go to Touch mapping.
  - **Done when:** acceptance checks are met, plus a Prelude sign-off.
- [Touch and pedal mapping](0015-touch-and-pedal-mapping.md): velocity sets hammer speed only, through a per-note curve anchored at velocities 32/64/112 = fitted pp/mf/ff.
  - **Touch:** one 0–100 control (50 = calibrated) bends the curve lighter or heavier.
  - **Pedals:** sustain and soft pedal are both continuous, with 0–12 / 116–127 dead zones. Damper movement is smoothed over ~20 ms, and a pedal invert setting covers reversed pedals. The soft pedal is read at each strike. On-screen pedals share one value with MIDI, and the latest message wins.
  - **Notes:** every note-on is a restrike. Overlapping note-ons are counted. Notes outside A0–C8 are ignored.
  - **Other MIDI:** omni, sample-accurate timing, and CC120/121/123 honored.
- [Tuning system](0016-tuning-system.md): a simulated tuner turns B, A4 and Stretch into pitches.
  - **Tuner:** 4:2 octaves in the middle, 6:3 in the bass, 2:1 in the high treble. It reruns off the audio thread and modes glide.
  - **Controls:** Stretch 0–100 (50 = calculated, 0 = none, 100 = double). A4 is 415–466 Hz, default 440.
  - **Unisons:** flat 0.7-cent spread, stored per note × one overall amount.
  - **Not in v1:** other temperaments, tuner scatter, transpose.
- [Editor control set](0017-editor-control-set.md): every sound control is 0–100 with 50 = the real piano, and double-click resets to default.
  - **Both views:** Hardness, Brightness, Resonance, Volume (−60 to +6 dB), perspective toggle, Sustain slider, Soft toggle.
  - **Advanced only:** Touch, A4, Stretch, Spread (unison spread; new) and Width, in five groups: Hammer, Tuning, Strings, Output, Pedals.
  - **Header:** Settings gear with a separate invert for each pedal. The advanced view adds Notes and CPU meters.
- [Presets, saved state and automation](0018-presets-state-automation.md): presets hold the 8 character controls, not Volume, A4 or the pedals.
  - **Presets:** about 5–8 read-only factory presets led by "Concert Grand"; user presets are JSON files in `Documents\Forte\Presets`. The ‹ › arrows wrap and a `*` marks unsaved changes.
  - **Automation:** the 10 sound controls are automatable with permanent IDs. The pedals are MIDI only.
  - **Saved state:** projects follow the latest piano model (ADR 0001). Pedal invert and theme are saved per computer.
  - **Engine:** a sample-rate change rebuilds the engine, and bounces are bit-identical to real-time playback.
- [Objective validation plan](0019-objective-validation-plan.md): three tiers.
  - **Engine checks:** on every change, never waivable. Velocity calibration ±1 dB, sample-accurate MIDI, bit-identical renders, no cut-off notes, previous-release states load.
  - **Sound-match checks:** nightly and before release. Real-engine renders vs the committed reference measurements, with the fit's tolerances unchanged, plus knock, pops and half-pedal behaviour gates. One-sided allowances where a D differs from the smaller reference pianos. Waivable by ear with a written note.
  - **Listening:** a blind A/B release sign-off by the user, plus an octave-by-octave voicing pass after each refit.
  - **Tooling:** `forte-render` plus `tools/validate/`; CI wiring goes to Build, installer and release.
- [Build, installer and release](0020-build-installer-release.md): MSVC (VS 2026) and CMake, with JUCE 9.0.5 as a pinned git submodule. Public GitHub repo, Releases and Actions.
  - **CPU:** one binary that picks AVX2 or SSE2 at startup. Manual performance gate: AVX2 ≤ 25% at 48 kHz and ≤ 50% at 96 kHz; SSE2 with no dropouts.
  - **CI:** every push runs Tier 1 (Catch2, an audio-thread allocation guard, pluginval at strictness 10). Tier 2 runs nightly and on release tags.
  - **Shipping:** model and IRs embedded in the binary. Inno Setup installer plus a zip. A tag builds a draft release, and the user publishes after the blind A/B.
  - **Signing:** v1.0 is unsigned, with a SignPath application in parallel. Follow Steinberg's VST3 trademark rules; full licence and credit notices.

## Not yet specified

<!-- All earlier fog graduated on 2026-11-14 into: Parameter fitting pipeline, Touch and pedal mapping,
     Tuning system, Editor control set, Presets/saved state/automation, Objective validation plan,
     Build/installer/release. -->

_Nothing. Every ticket is closed and the way to the destination is clear (2026-11-14)._

## Out of scope

- macOS, AU, Linux (v1 is Windows only).
- Built-in reverb.
- Sostenuto pedal, duplex scaling, key/pedal mechanical noises, lid and microphone positions.
- Piano models other than the one concert grand.
- Historical temperaments, MTS-ESP and MIDI Tuning Standard. v1 is stretched equal temperament only, and tuning is calculated, so they can be added later. See [Tuning system](0016-tuning-system.md).
- Release velocity. Dampers land with a fixed ramp because few keyboards send release velocity and its effect is slight. See [Touch and pedal mapping](0015-touch-and-pedal-mapping.md).
