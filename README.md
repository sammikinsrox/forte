# Forte

A free, open-source, physically modeled concert-grand piano for Windows, as a VST®3 plugin and a Standalone app. It simulates the hammer, strings, dampers and soundboard instead of playing back recordings.

**Status:** early engine. The 88 note models (modal strings, felt hammer, dampers) play from MIDI with seed values, through the soundboard: four bridge regions, each convolved with a stereo IR for the Microphones or Pianist perspective. The IRs are uncalibrated seeds from the listening-test prototype's generator. No sympathetic resonance or controls yet, and no fitted piano model. The design lives in [SPEC.md](SPEC.md), with vocabulary in [GLOSSARY.md](GLOSSARY.md) and the reasoning in [issues/](issues/).

## Building

Needs Visual Studio 2026 (Desktop development with C++), CMake 3.25+ and Git.

```powershell
git clone --recursive <repo-url> forte      # or: git submodule update --init --recursive
cd forte
./tools/get-pluginval.ps1                    # optional: enables the pluginval test
./tools/get-vst3-validator.ps1               # optional: adds Steinberg's validator to it (a few minutes)
cmake --preset vs2026
cmake --build --preset release
ctest --preset release
```

Output, under `build/`:

| Target | File |
|---|---|
| VST3 | `plugin/Forte_artefacts/<Config>/VST3/Forte.vst3` |
| Standalone | `plugin/Forte_artefacts/<Config>/Standalone/Forte.exe` |
| `forte-render` (MIDI → WAV) | `tools/forte-render/forte_render_artefacts/<Config>/forte-render.exe` |
| Unit tests | `tests/forte_tests_artefacts/<Config>/forte-tests.exe` |

## Layout

| Path | What |
|---|---|
| `engine/` | `forte_engine`, the one shared engine library. Plain C++, no JUCE. Everything here is a DSP source and must pass the realtime rule. |
| `bridge/` | Header-only glue from JUCE buffers to the engine, shared by the plugin and `forte-render`. |
| `plugin/` | The VST3/Standalone wrapper. |
| `tools/forte-render/` | Command-line renderer through the real engine. |
| `tools/embed/` | Build-time tool that embeds data files (the IRs) in the engine. |
| `tools/seed-irs/` | Writes the seed soundboard IRs, their kept parts and `engine/src/BoardSettings.inc` (`python tools/seed-irs/make_seed_irs.py`, needs numpy). |
| `data/ir/` | The soundboard IRs the engine embeds: 32-bit float WAV, 48 kHz, one per perspective and bridge region. `parts/` holds each one's resonant modes, which the voicing lab rebuilds the IRs from. |
| `tests/` | Catch2 tests, the audio-thread allocation guard, and the CTest checks. |
| `external/JUCE` | JUCE 9.0.5, a pinned submodule. |

## Safety net

`ctest` runs what CI runs on every push (SPEC §9.3):

- **Unit tests** (Catch2).
- **Audio-thread allocation guard:** `tests/support/AudioThreadGuard.*` replaces the global `operator new`/`delete` in the test executable, and in Debug also hooks the CRT heap (`malloc`/`free`). Code run inside an `AudioThreadScope` must make zero heap calls.
- **Realtime rule:** `cmake/CheckRealtimeSources.cmake` fails the build if anything under `engine/` includes `<mutex>`, `<thread>`, `<condition_variable>`, `<windows.h>`, JUCE headers and the like, or uses lock types.
- **Tier 1** (SPEC §8.1, label `tier1`): the velocity curve rises at every Touch value, overlapping note-ons never cut a note short, onsets land on their event's exact sample, and renders are bit-identical, both run to run and across block sizes (also end to end through `forte-render`). Velocity calibration, live changes and state compatibility arrive with the fitted model, the parameters and the saved state.
- **pluginval** at strictness 10 on the VST3, including Steinberg's VST3 validator. JUCE's bundled SDK leaves the validator out, so `tools/get-vst3-validator.ps1` builds it from a pinned `vst3sdk` tag. The test fails if pluginval reports the validator as skipped.

## Licence

Forte is GPLv3-or-later. JUCE is used under AGPLv3, and the VST3 SDK under MIT. See SPEC §1 and §9.6.

VST is a registered trademark of Steinberg Media Technologies GmbH.
