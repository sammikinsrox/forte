# Forte v1 build checklist

How far the code is from [SPEC.md](../SPEC.md), section by section. Checked on 2026-11-16 against
commit `0bbbe50`, by reading the code, the tests and CI. Nothing was run for this check.

- [x] **done:** built, and usually tested.
- [~] **partial:** built, but it uses a placeholder, seed or by-ear value where the spec asks for a fit
  or calibration, or part of the section is missing.
- [ ] **missing:** not started.

## Summary

| Area | State |
|---|---|
| Synthesis engine (§2) | Nearly done. AVX2/SSE2 dispatch is missing, and a few values are placeholders |
| Model data and fitting (§3) | Partial. Voiced against Keyscape, but several calibrations are still seeds |
| MIDI, touch, pedals (§4) | Done |
| Tuning (§5) | Done |
| Editor (§6) | Mostly done. The overtone display, lit keys and presets UI are missing |
| Presets, state, automation (§7) | Automation and state done; **presets missing** |
| Validation (§8) | Tier 1 mostly done; **Tier 2 and the Tier 3 tooling missing** |
| Build, CI, release (§9) | Per-push CI done; **installer, release flow and notices missing** |
| Voicing ([issue 0021](../issues/0021-voicing-against-iowa.md)) | Ongoing: the "tinny / CP-70" treble is still open |

**Biggest gaps before a v1.0 release:**
1. Presets (§7.1).
2. AVX2 + SSE2 dispatch (§9.2).
3. Tier 2 checks, `tools/validate/` and the HTML report (§8.2, §8.4).
4. Installer, zip and tag-to-draft-release workflow (§9.4–9.5).
5. LICENSE, CREDITS.md and THIRD-PARTY.md, plus VST branding (§9.6).
6. Finishing the voicing.

---

## 1. Product

- [x] VST3 + Standalone, Windows (`plugin/CMakeLists.txt`).
- [x] Physics list: felt hammer, inharmonicity, coupled 1–3 strings, phantom partials, soundboard,
  dampers with half-pedaling, sympathetic resonance, soft pedal.
- [~] Performance target. The real engine was measured with the **SSE2** build only: 8.4–9.4% at
  48 kHz and 20–22% at 96 kHz for the 88-key glissando (issue 0021 §16). That's well inside the
  target, but there is no AVX2 build to gate (see §9.2).
- [ ] Licence files at the repo root (see §9.6).

## 2. Synthesis engine

- [x] §2.1 Signal path, in the stated order (`Engine.cpp`).
- [x] §2.2 88 permanent notes, restrikes, idle at −100 dB, runs at the host rate.
- [x] §2.3 Modal strings, coupled unisons, per-note spread curve, phantom partials through contact,
  bridge-end output, f0/2 high-pass.
  - [~] String counts use a simple split (≤ F#1 single, ≤ C3 double), not the Steinway D data
    (`PianoModel.cpp:98`).
- [x] §2.4 Stulov felt with 4× contact oversampling, contact-speed exponent, knock, per-note
  values. There is a stability test at every sample rate.
- [x] §2.5 Damper position, a linear half-pedal gap model, ~20 ms key-up, no dampers from G#6.
  - [~] The damper law is the prototype's, not fitted to Salamander's releases. The undamped
    boundary is approximate (`PianoModel.cpp:169-172`).
- [x] §2.6 Sympathetic bank: 12 modes per key, damper-gated, in its own region.
- [x] §2.7 Soft pedal: a shift read at each strike.
  - [~] Its felt is a placeholder ×0.7 (`PianoModel.cpp:167`). It still needs setting by ear.
- [x] §2.8 Four regions × 20k-tap stereo IRs, perspective crossfade, Brightness, Tape, Width,
  Volume.
- [x] §2.9 Live changes for every kind, including the voicing lab. Tested in `ControlsTests`, Tier 1
  automation and `VoicingLabTests`.

## 3. Piano model data and fitting

- [~] §3.1 References. Iowa and the Keyscape sweeps are used. **Salamander, Piano in 162 and Plath
  aren't used yet** (`pedal_difference.py` exists for Piano in 162). There is no
  `tools/fetch-references`.
- [~] §3.2 Pipeline. There are separate scripts (`fit_decay.py`, `fit_hammer.py`,
  `fold_voicing.py`, …), but no single measure / fit / build / `all` command.
- [~] §3.3 What is fitted:
  - [x] Inharmonicity B (Keyscape sweep).
  - [x] Decay per partial (Keyscape sweep, `data/model/decay.json`).
  - [x] Felt voiced per register; knock; contact-speed exponent.
  - [ ] Phantom levels fitted against Iowa ff. It's still the prototype's 0.3 (`PianoModel.cpp:142`).
  - [ ] Velocity calibration points from a fit that works by ear. The speeds are the seed's
    (`PianoModel.cpp:155`).
  - [ ] Damper-down law from Salamander; half-pedal gap set by ear.
  - [ ] Sympathetic level against Piano in 162. It's a placeholder (`PianoModel.cpp:230`).
  - [ ] Soft-pedal felt by ear.
- [~] §3.4 Soundboard IRs. These are the **seed** IRs, set by ear plus a Keyscape colour curve.
  The Microphones fit against Iowa, the Pianist colour from Plath and the fitted irregularity
  don't exist yet.
- [~] §3.5 What ships. The IRs are embedded in the binary (`engine/CMakeLists.txt`). The model lives
  as several JSON files in `data/model/` that are generated into C++, not one
  `concert-grand.json` with a schema version. Decide whether that's fine and update the spec, or
  build the single file.

## 4. MIDI, touch and pedals

- [x] §4.1 Per-note velocity curve through 32/64/112, its extensions, Touch. Tested in
  `VelocityCurveTests`.
- [x] §4.2 Continuous sustain and soft pedals with dead zones, ~20 ms smoothing, inverts, on-screen
  pedals sharing one value with MIDI.
- [x] §4.3 Counted note-ons, A0–C8, omni, sample-accurate timing, CC120/121/123, ignored messages.
  Tested in `MidiTests` and Tier 1.

## 5. Tuning

- [x] The tuner (`Tuner.cpp`): 4:2 / 6:3 / 2:1 with gradual blends, runs on the audio thread,
  ~50 ms glide, Stretch, A4, Spread. Tested in `TunerTests` and `ControlsTests`.

## 6. Editor

- [x] §6.1 Native JUCE with a custom LookAndFeel, fixed-aspect resizing, typed integer values
  (`Widgets.cpp`), embedded Jost and Italiana.
- [~] §6.2 Look:
  - [x] Fallboard design, felt strip, keyboard, light and dark themes, tuning curve.
  - [ ] **Live overtone display** of the last note.
  - [ ] **Keys lit by how strongly they ring.**
- [~] §6.3 Controls. All 12 are present with the 0–100 rules, double-click reset and Volume "Off".
  The Hardness span (÷4 to ×4) and Brightness range (±12 dB) are first-build values still
  waiting for an ear check (§10).
- [~] §6.4 Layout. Simple, Advanced and Lab tabs (saved with the project); the five advanced groups;
  Settings gear with both inverts and About; Notes and CPU meters.
  - [ ] Preset name with ‹ › arrows (needs §7.1).
- [x] §6.5 Voicing lab: all controls, registers, Lab | Committed A/B, Export voicing, fold, revision.
  Bit-identical at its 50s, and its costs were measured.

## 7. Presets, saved state and automation

- [ ] §7.1 **Presets: nothing built.** That covers the factory list led by "Concert Grand",
  `.fortepreset` user files in `Documents\Forte\Presets`, wrapping arrows, the `*` changed flag
  and the name menu.
- [x] §7.2 Automation: the 10 sound controls with permanent parameter IDs, perspective as two-state.
- [~] §7.3 Saved state: the 10 values, the view and state version 2 are saved. The preset name and
  its `*` flag are missing (needs §7.1).
- [x] Per-computer settings: inverts and theme (`MachineSettings.cpp`).
- [x] §7.4 Bit-identical offline bounce (Tier 1).
  - [ ] Sample-rate rebuild time measured against the "well under 1 s" target (§10).

## 8. Validation

- [~] §8.1 Tier 1, per push:
  - [x] Velocity curve monotonic; overlapping note-ons; MIDI timing; determinism (including
    through `forte-render`); live changes under automation.
  - [ ] Velocity calibration ±1 dB. It waits on the fitted pp/mf/ff loudness.
  - [ ] Live changes on a preset change (needs §7.1).
  - [ ] State compatibility against the previous release's fixtures (first fixtures come with the
    v1.0 release).
- [ ] §8.2 **Tier 2 sound-match checks: none built.** No `tools/validate/` and no nightly workflow.
  The gates also still name Iowa where the spec now makes Keyscape the target, so decide which
  reference each gate uses before building it.
- [ ] §8.3 Tier 3: the blind A/B page and the octave-by-octave voicing clips.
- [~] §8.4 Tooling: `forte-render` is done; `tools/validate/` and the HTML report are missing.

## 9. Build, CI and release

- [x] §9.1 MSVC, /O2 /fp:fast, FTZ/DAZ in the engine, the JUCE submodule, one shared engine
  library, version 1.0.0 in CMake.
- [ ] §9.2 **AVX2 + SSE2 paths with a startup CPU check: not built.** There's one build with
  default (SSE2) codegen. The manual gate then needs running on both paths.
- [~] §9.3 Safety net. Per push: Debug and Release builds, Catch2, the allocation guard, the
  realtime build rule, pluginval at 10 with the VST3 validator, Tier 1.
  - [ ] The nightly / release-tag Tier 2 workflow.
  - [ ] The CI render-time regression flag (> 1.5×).
- [ ] §9.4 **Inno Setup installer and portable zip: not built.**
- [ ] §9.5 **Tag → draft GitHub Release workflow: not built.**
- [~] §9.6 Release contents:
  - [x] About panel with the licence and font notices.
  - [ ] `LICENSE` (GPLv3) and the AGPLv3 text.
  - [ ] `CREDITS.md` and `THIRD-PARTY.md`. Add Keyscape to the credits decision, since its derived
    numbers are committed.
  - [ ] VST branding: logo, ®, the trademark line.
  - [ ] `docs/validation/` waivers and state fixtures, at the first release.

## 10. Settled in the first build

- [x] Hardness span: first-build value ÷4 to ×4.
- [ ] Brightness range, soft-pedal felt and half-pedal gap settings by ear.
- [ ] Final factory preset list and values (needs §7.1).
- [~] CPU: SSE2 measured and inside the gate; AVX2 not measured; 96 kHz needs no resampling at
  ~22% (SSE2).
- [ ] Sample-rate rebuild time.
- [ ] Editor checks in **FL Studio**: whether the advanced groups fit, typed values, resizing.
- [~] Undamped boundary and Steinway D string tables. Approximations are in place, so the data is
  still to find.
