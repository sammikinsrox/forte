---
id: 8
title: Choose framework & language
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: [4]
---

## Question

Given the findings of [Plugin framework: JUCE/C++ vs nih-plug/Rust](0004-plugin-framework-comparison.md), which framework, language and GUI technology does Forte use?

## Resolution

Decided with the user:

1. **Framework and language:** JUCE 9 (C++), pinned to a 9.0.x release, CMake build, VST3 + Standalone targets. Rust/nice-plug is rejected for v1 and should be revisited only if nice-plug reaches a stable 1.0 with a documented GUI story.
2. **Licensing:** Forte's own code is GPLv3-or-later. JUCE is used under AGPLv3, which may be combined with GPLv3 per GPLv3 §13. The VST3 SDK is MIT (3.8+), so no Steinberg agreement is needed.
3. **Editor technology:** deferred to [Editor rendering approach: WebView vs native JUCE](0010-editor-rendering-approach.md). The WebView editor is the front-runner because the chosen design (variant D) is already HTML/CSS/JS.
4. **Safety net, required from the first commit:** pluginval on every build, tests that fail on memory allocation or lock-waiting on the audio thread, and DSP unit tests against reference measurements. How these are set up on Windows belongs to the map's "Build, installer and release" item.

See [research/plugin-framework.md](../research/plugin-framework.md) for the evidence.
