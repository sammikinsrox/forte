---
id: 20
title: Build, installer and release
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

How is Forte built, tested and shipped on Windows? Decide each of these:

- **Toolchain:** MSVC or clang-cl, and how JUCE 9 is pulled in.
- **CPU paths:** AVX2 with a runtime SSE2 fallback, plus a CPU check of the real engine at 48 and 96 kHz.
- **Safety net:** wiring pluginval, the audio-thread allocation/lock tests and the DSP unit tests into CI.
- **Hosting and installer:** where the source and releases live, and the installer type and VST3 install path.
- **Signing and branding:** code signing, and use of the VST logo.
- **Release contents:** licence and credits files (GPLv3, JUCE AGPLv3, CREDITS for the references).

Also from [Objective validation plan](0019-objective-validation-plan.md#tooling):

- **Validation wiring:** building the `forte-render` command-line renderer, running the engine checks on every change and the sound-match checks nightly and before release, and where the HTML validation report and any waivers are kept with a release.

## Resolution

Resolved by grilling on 2026-11-14. The user accepted Q1–Q9 as recommended. For Q10–Q12 they said to "use your best judgement … or go with your recommendations", so those are the recommendations below. Research for signing and branding: [research/code-signing-and-vst-branding.md](../research/code-signing-and-vst-branding.md).

### Toolchain

- **Compiler:** MSVC (Visual Studio 2026), with `/O2 /fp:fast` and flush-to-zero/denormals-are-zero set inside the engine. clang-cl stays a later option, used only if profiling shows a hot spot it would fix. Its main draw (RealtimeSanitizer) doesn't run on Windows.
- **JUCE:** a git submodule pinned to the **JUCE 9.0.5** tag. Upgrading JUCE is a deliberate commit that has to pass the full safety net. A recursive checkout is the complete corresponding source the GPL requires.
- **Build:** CMake. The targets are the VST3, the Standalone, `forte-render` and the test executables. They all link one shared engine library, so `forte-render`'s output is the plugin's output.
- **Version:** one semantic version number in CMake, starting at `1.0.0`.

### CPU paths and performance gate

- **One binary, two engine paths.** The engine's hot loops are compiled twice, once for AVX2 and once for SSE2. A CPU check at startup picks the path. Users never choose.
- **Performance gate** (manual, on the user's Ryzen 5 5500, before each release). Each check is a full 88-key sustain glissando at a 128-sample buffer:
  - AVX2 at 48 kHz: **≤ 25%** of one core (the product target).
  - AVX2 at 96 kHz: **≤ 50%** of one core.
  - SSE2 at 48 kHz: **no dropouts**. Its CPU % is recorded, but it is not a gate.
- **In CI:** shared runners' timings are too noisy for a real gate. CI only fails when a render takes more than **1.5×** its last recorded time.

### Safety net (GitHub Actions, Windows runners)

- **On every push and pull request** (Tier 1, never waivable):
  - Debug and Release builds.
  - **Catch2** DSP unit tests.
  - Audio-thread tests: a guard fails any allocation on the audio thread while tests drive the engine. Locks are kept out by a build rule that rejects `<mutex>` and the like in DSP sources, plus code review.
  - **pluginval** at strictness 10 on the VST3.
  - The Tier 1 engine checks, run through `forte-render`.
- **Nightly and on every release tag:** the Tier 2 sound-match checks. They produce the HTML validation report.

### Hosting, installer and model data

- **Hosting:** a public GitHub repo, with GitHub Releases for downloads and GitHub Actions for CI. Run `git init` when implementation starts. `issues/` stays as the design record.
- **Model data:** the versioned model file and the 48 kHz IRs are **embedded in the binary** (both the `.vst3` and the `.exe`). Each plugin version is exactly one model version, consistent with [ADR 0001](../docs/adr/0001-projects-follow-latest-model.md), and nothing can go missing.
- **Installer (Inno Setup):**
  - Per-machine install, run as admin.
  - `Forte.vst3` → `C:\Program Files\Common Files\VST3\`.
  - `Forte.exe` → `C:\Program Files\Forte\`, with a Start-menu shortcut.
  - Checkboxes for VST3 and Standalone, plus an uninstaller.
  - Never touches `Documents\Forte\Presets`.
- **Portable zip:** the same two files, published alongside the installer.

### Release flow

1. Bump the version and push a tag such as `v1.0.0`.
2. CI builds, runs Tier 1 and Tier 2, packages the installer and zip, and creates a **draft** GitHub Release with the validation report attached.
3. The user runs the blind A/B sign-off and the performance gate.
4. The user publishes. Nothing goes public without that sign-off.

### Release contents and records

- **Shipped with every release:**
  - `LICENSE` (GPLv3) and the AGPLv3 text.
  - A link to the matching source tag.
  - `THIRD-PARTY.md`.
  - `CREDITS.md` (Iowa MIS, Salamander Grand CC-BY, Piano in 162, Plath dataset CC BY 4.0).
- **About panel** (opened from the Settings gear): credits, legal notices, the VST3 SDK MIT text, the OFL text and the font copyright lines.
- **`THIRD-PARTY.md` covers:**
  - JUCE (AGPLv3) and the VST3 SDK (MIT).
  - Jost and Italiana (OFL). Both are embedded **unmodified**, with no subsetting, because Italiana has a Reserved Font Name.
  - Every JUCE-bundled library that actually ends up in the binary (for example SheenBidi, Apache-2.0). This is checked against the real build at release.
- **Validation report:** attached to the GitHub Release.
- **Waivers:** committed as `docs/validation/<version>-waivers.md`.

### Code signing

- **v1.0 ships unsigned.** The download page gets a short note explaining SmartScreen's one-time "Run anyway" prompt.
- **Known gap:** Smart App Control, where enforcing, blocks unsigned code with no override.
- **Next step** (the user postponed this on 2026-11-14, so revisit once Forte has users): email SignPath Foundation and ask three things:
  - whether a solo maintainer can hold all the roles;
  - whether JUCE's commercial dual licence conflicts with their terms;
  - what reputation they require.
- **If accepted:** sign via SignPath in GitHub Actions.
- **If refused:** use Microsoft Artifact Signing ($9.99/month) if the user lives in the US or Canada. Otherwise use Certum's open-source certificate (~€69/year, smart card, signed locally on the user's PC). Skip EV, since it no longer gives instant SmartScreen trust.

### VST branding

Use "VST3" and follow Steinberg's usage guidelines (no agreement needed):
- The VST Compatible logo from the SDK goes next to the claim, on the README, on the Releases page and in the About panel.
- **®** on the first "VST" in each document.
- The exact line: "VST is a registered trademark of Steinberg Media Technologies GmbH."
- No "VSTi" or other variants, and never "VST" in the product name.

This is an item on the release checklist.
