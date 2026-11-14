---
id: 10
title: "Editor rendering approach: WebView vs native JUCE"
labels: [wayfinder:prototype]
status: closed
assignee: sam
parent: 1
blocked_by: [6, 8]
---

## Question

Should Forte's editor be a web page inside the plugin window (JUCE WebView, drawn by Windows' WebView2) or JUCE's native GPU-drawn components? Build the chosen look from [Editor look & feel direction](0006-editor-look-and-feel.md) both ways at minimal fidelity, then compare in FL Studio and Reaper. Criteria: visual fidelity, animation smoothness, CPU/GPU cost, resizing and HiDPI, keyboard focus, where WebView2 stores its user data, and startup time.

## Progress (2026-11-14)

Prototype: [prototypes/editor-rendering/](../prototypes/editor-rendering/) (throwaway). One JUCE 9.0.5 plugin with shared parameters and a toy sound, built twice: **Forte Web Proto** (variant D page in WebView2, bound to the parameters via JUCE relays) and **Forte Native Proto** (variant D redrawn with JUCE graphics). VST3 + Standalone, Release, MSVC (VS 2026), Ryzen 5 5500.

Standalone measurements, 20 s each, test arpeggio playing. Includes the toy audio engine. Single runs, noisy (about ±3 points).

| Scenario | Web: CPU (% of one core) | Native: CPU | Web: memory (working set) | Native: memory |
|---|---|---|---|---|
| Simple view, playing | ~13–17% | ~3–9% | ~420–480 MB over 7 processes | ~75 MB, 1 process |
| Advanced view (dark), playing | ~20% (48% before redrawing only on new data) | ~14–21% | ~420–500 MB | ~80 MB |
| Simple view, idle | ~3% | ~5% | ~400 MB | ~72 MB |
| Editor open → ready | ~430 ms | ~26 ms | | |

Observations:
- **Fidelity:** both reproduce variant D closely. The web version is the design file itself. The native version needed hand-porting, plus two JUCE-specific fixes: Jost is a variable font whose default instance is Thin, so 400/500 weights have to be cloned explicitly; and JUCE font height ≠ CSS font-size, so use `withPointHeight`.
- **CPU:** comparable once the web page is written carefully (redraw only on new data, no forced 2× canvas). A naive page used ~2.5× more.
- **Memory and startup:** WebView2 costs ~5–6× the memory (a separate browser process tree) and ~0.4 s to open. Native opens instantly.
- **Development:** the web editor can be designed and previewed in a normal browser, and UI changes don't need a C++ rebuild if the page is loaded from disk during development.
- **Still to check with the user, in FL Studio and Reaper:** the look side by side, knob feel and typing values (does the host steal keystrokes?), resizing, HiDPI scaling, opening/closing speed, several instances at once.

## Resolution (2026-11-14)

**Native JUCE editor.** Both builds worked in Reaper; the user chose native. It uses about 75 MB instead of about 450 MB, opens in about 0.03 s instead of 0.4 s, runs in a single process, and doesn't depend on the WebView2 runtime or a user-data folder. The cost is that design changes are hand-ported C++ rather than edited HTML.

Carry forward from the prototype:
- Port variant D with a custom `LookAndFeel` (rotary dials, faders, pill and segment buttons), a scaled content component for fixed-aspect resizing, and editable integer labels for typed values.
- Embed the Jost and Italiana fonts as binary data. Jost is variable, so clone the 400/500 weights explicitly, and use `withPointHeight` to match CSS sizes.
- Drive visualisations from lock-free atomics that the audio thread publishes, repainted at about 30 Hz, and repaint only when the data changes.
- The HTML mockup in `prototypes/editor-look-and-feel/` stays the visual reference for the port.
- FL Studio wasn't tested. Recheck typed values (host keystroke capture) and resizing there in the first real build.
