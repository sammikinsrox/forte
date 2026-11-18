---
id: 23
title: Linux build (VST3, LV2, Standalone)
labels: [build, release, platform]
status: open
assignee:
parent: 1
blocked_by: []
---

## Why

The user wants to share Forte with a friend on Arch Linux (Reaper, maybe LMMS, a Ryzen 9 7900X)
and to support the open-source and freeware community. SPEC §1 had Linux out of scope for v1;
it's in from 2026-11-17. macOS/AU, CLAP and ARM stay out.

## Decisions

- **Formats:** VST3 and Standalone on both platforms; LV2 on Linux too (LMMS hosts LV2
  instruments, not VST3). Windows ships exactly what it did. The LV2 URI is
  `https://github.com/sammikinsrox/forte`: like the VST3 class ID, it never changes once released.
- **Compilers:** MSVC on Windows (unchanged); GCC or Clang on Linux, x86-64 only (the hot loops
  are SSE2 and AVX2). Linux flags: `-Wall -Wextra` (`-Werror` on Forte's own code), `-O2`,
  `-fno-math-errno`, `-ffp-contract=off` (no fused multiply-adds the source doesn't ask for), and
  no `-ffast-math` (the tests check for NaNs). Release also gets `-ffunction-sections
  -fdata-sections -Wl,--gc-sections`, which MSVC's `/OPT:REF` does by default.
- **Position-independent code** project-wide (`CMAKE_POSITION_INDEPENDENT_CODE`): the static
  engine goes into the plugins' shared objects. No effect on Windows.
- **Unused codecs off** everywhere: `JUCE_USE_OGGVORBIS=0`, `JUCE_USE_OPUS=0`,
  `JUCE_USE_MP3AUDIOFORMAT=0`. Forte writes WAV only. MSVC's linker had dropped them; GCC's
  linked them in, against THIRD-PARTY.md's list. On Linux the linker also drops FLAC.
- **AVX2 detection** (`Kernels.cpp`): `<cpuid.h>` and `xgetbv` by its encoding on GCC/Clang,
  `<intrin.h>` on MSVC.
- **Tests:** the audio-thread guard's aligned allocations use `posix_memalign` on Linux. Its
  Debug CRT heap hook (catching plain `malloc`) stays Windows-only; the code under test is the
  same on both, so the Windows run covers it. pluginval 1.0.4 on Linux passes Steinberg's
  validator the `.so` inside the bundle, which the validator rejects ("is not a module
  directory"), and then stops before its own tests. So on Linux the validator runs as its own
  CTest test on the bundle, and pluginval runs without it.
- **Tools:** `tools/get-pluginval.sh` and `tools/get-vst3-validator.sh` (Linux twins of the
  `.ps1`s), into `.tools/linux/` so one checkout serves Windows and WSL. The validator's SDK
  build is cut to the validator alone (its editor and audio host samples need GTK 3).
- **Presets:** `linux` (Ninja Multi-Config, `build-linux/`), `linux-debug`, `linux-release`.
- **Packaging:** `tools/release/package.sh` writes `dist/Forte-<version>-linux-x64.tar.gz` (VST3,
  LV2, Standalone, licences, README, `install.sh`). `install.sh` installs per user into
  `~/.vst3`, `~/.lv2` and `~/.local/bin`, or with `--system` into `/usr/local/lib/{vst3,lv2}` and
  `/usr/local/bin`, and `--uninstall`s.
- **Releases:** `release.yml` gets a Linux job (Ubuntu 24.04 runner, after the Windows job made
  the draft) that builds, tests under `xvfb-run` and attaches the tarball.
  `make-release.ps1` runs `tools/release/build-linux.sh` in WSL (`-NoLinux` skips it). The
  per-push CI stays Windows-only, to keep CI use sparing; local Linux runs in WSL are the gate.

## Results (2026-11-17, WSL Ubuntu 24.04, GCC 13.3)

- Forte's own code builds with no warnings under `-Werror`. The remaining warnings are all in
  JUCE's vendored HarfBuzz and one JUCE look-and-feel file.
- `ctest --preset linux-release` under `xvfb-run`: **162/162**: every unit and Tier 1 test,
  the realtime rule, pluginval at strictness 10 on the VST3 and on the LV2 (25 test groups each,
  editor included), and Steinberg's validator on the VST3 (47/47). Windows: 161/161 (Debug)
  with the shared changes. After the lab-preset fixes (voicing revision 6): Linux 164/164,
  Windows Debug 163/163, Release 162/162.
- **Sound, Linux against Windows** (`forte-render`, defaults): the difference is −82 to −83 dB
  re the signal, peak difference −81.5 to −84.6 dBFS, octave bands equal to 0.00 dB, the same
  length to the sample; Autumn Leaves and Chopin on AVX2, Chopin on SSE2. Not bit-identical
  (the maths libraries round differently), so the voicing lab's bit-identity references
  (`tools/lab-identity.ps1`) stay per platform; Linux has none yet. Run to run and across block
  sizes, Linux is bit-identical with itself (Tier 1).
- **The package:** 22 MB. `install.sh` installs and uninstalls cleanly in a fresh home. The
  plugin needs ALSA, FreeType and fontconfig (and their usual dependencies) beyond the C and C++
  runtimes, and glibc 2.38 or newer (the newest symbol version it uses). No LV2 hosting code
  (lilv, serd, sord) is in the binaries.

## Not yet checked

- A real Linux DAW: Reaper and LMMS on the friend's Arch machine, with audio, the editor under
  their desktop (X11, or Wayland through XWayland) and saved projects.
- Clang (installed in WSL, not yet built), and a Linux Debug build.
- The CPU gate (SPEC §9.2) on Linux.
- The release workflow's Linux job on GitHub (written, not run).
