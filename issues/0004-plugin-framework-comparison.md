---
id: 4
title: "Plugin framework: JUCE/C++ vs nih-plug/Rust"
labels: [wayfinder:research]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Which plugin framework and language best fit Forte? Compare JUCE (C++), nih-plug (Rust), and any other serious current option, against these criteria:

- **License fit** with a GPLv3 open-source release, including the VST3 SDK licensing path.
- **Windows VST3 plus standalone** support, and known issues in FL Studio and Reaper.
- **Custom UI ceiling:** how beautiful, bespoke and animated the editor can be. Covers GPU/vector rendering, HiDPI, resizing, and the GUI options available (e.g. JUCE components/WebView UI; nih-plug's egui/vizia/iced; web-view approaches).
- **Real-time safety** and DSP performance (SIMD, allocation-free audio thread).
- **Maturity, docs and community** as of late 2026.
- **Fit for an AI-written codebase** whose owner can't read C++ or Rust well: build simplicity, compiler-enforced safety, testability.

## Resolution

Recommend **JUCE 9 (C++), used under its free AGPLv3 licence, with a CMake build targeting VST3 + Standalone**. GPLv3 §13 permits combining Forte's code with AGPLv3 code. The VST3 SDK has been MIT-licensed since 3.8 (Oct 2025), so no Steinberg agreement is needed.
JUCE is the most mature and host-tested option, is actively released (9.0.5, Nov 2026) and has the highest custom-UI ceiling (WebView2-based web UI, or native Direct2D with an animation module).
nih-plug is in maintenance mode, and its fork nice-plug calls itself "experimental" and has weak GUI documentation, so Rust is not recommended for v1 despite its stronger compiler safety.
The C++ safety gap will be covered by guardrails: pluginval in CI, audio-thread allocation checks and DSP unit tests.
Editor tech (JUCE WebView vs native JUCE graphics) should be settled by a prototype.
Full findings: [research/plugin-framework.md](../research/plugin-framework.md).
