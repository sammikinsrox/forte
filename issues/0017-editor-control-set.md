---
id: 17
title: Editor control set
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: [15, 16]
---

## Question

What is the final list of parameters in Forte's simple and advanced views? For each, cover its name, range, default, unit and integer display. The layout is set by [Editor look & feel direction](0006-editor-look-and-feel.md). Brightness, Sympathetic, Width and Listening perspective are already defined by [Choose the synthesis architecture](0007-choose-synthesis-architecture.md) and [Listening perspective: pianist's seat or mics over the lid?](0013-listening-perspective.md). Also decide whether the advanced view gets anything new, such as unison spread, soft-pedal amount or a half-pedal display.

Input from [Touch and pedal mapping](0015-touch-and-pedal-mapping.md): place the **Touch** control (0–100, default 50 = calibrated, lighter ↔ heavier) and a **pedal invert** setting for reversed sustain/soft pedals. The on-screen sustain slider and soft-pedal toggle mirror incoming MIDI pedal values, and the latest message wins.

Input from [Tuning system](0016-tuning-system.md): place **A4** (415–466 Hz, whole Hz, default 440) and **Stretch** (0–100, default 50 = calculated curve, 0 = none, 100 = double). Decide whether the overall **unison spread** amount (default 0.7 cent) gets a knob. Transpose, temperament choices and fine-tune are not controls.

## Resolution (2026-11-14)

Decided with the user over two grilling rounds, all recommendations accepted. Everything below is spec. Layout and look stay as in [Editor look & feel direction](0006-editor-look-and-feel.md) (variant D), except where changed here.

### Value rules

1. **"50 = the real piano."** Every sound control runs 0–100, and 50 is the piano as measured or calibrated.
2. **Whole numbers only.** Every value can be typed directly: Enter commits, ↑/↓ steps by 1, Esc cancels.
3. **Double-click resets** a knob, dial or fader to its default. Pedal controls are the exception because they always show the live pedal.
4. **One name per control, in both views.** The prototype's Hammer / Tone / Felt hardness / Touch curve / Sympathetic labels are replaced. The new terms are in [GLOSSARY.md](../GLOSSARY.md).

### Controls

| Control | Range | Default | Meaning | Views |
|---|---|---|---|---|
| **Hardness** | 0–100 | 50 = fitted felt | Log-scale multiplier on the fitted felt stiffness. The span is set by ear in the first build. Strike-time: it affects the next strike only. | simple dial, advanced knob |
| **Brightness** | 0–100 | 50 = no tilt | Treble tilt on each bridge region's output, about −12 to +12 dB at 10 kHz (exact by ear). | both |
| **Resonance** | 0–100 | 50 = physical level | Sympathetic bank level. 0 = none, 100 = double. This replaces "0–200%" from [Choose the synthesis architecture](0007-choose-synthesis-architecture.md), with the same gain range. | both |
| **Volume** | −60 to +6 dB | 0 dB | −60 shows as "Off" (silence). | simple fader, advanced knob |
| **Listening perspective** | Microphones \| Pianist | Microphones | As in [Listening perspective](0013-listening-perspective.md). | both headers |
| **Sustain** | 0–100 | live | Raw pedal position scaled from MIDI 0–127 (0 = Up, 100 = Down). The dead zones are applied inside the engine, not shown. Same horizontal Up / Half / Down slider in both views. Mirrors MIDI, and the latest message wins. | both |
| **Soft** | toggle | off | Shows as on once the pedal passes halfway. Clicking sends fully down or fully up. | both |
| **Touch** | 0–100 | 50 = calibrated | As in [Touch and pedal mapping](0015-touch-and-pedal-mapping.md). | advanced only |
| **A4** | 415–466 Hz | 440 | Whole Hz. | advanced |
| **Stretch** | 0–100 | 50 = calculated | As in [Tuning system](0016-tuning-system.md). | advanced |
| **Spread** | 0–100 | 50 = 0.7 cent | The overall unison spread amount. 0 = strings in perfect unison, 100 = double (1.4 cent). Labelled "Spread" because "Unison" already means the string group. | advanced |
| **Width** | 0–100 | 100 | 0 = mono, 100 = full stereo. | advanced |

### Layout

5. **Simple view:**
   - three dials: Hardness, Brightness, Resonance;
   - a Volume fader;
   - the Microphones | Pianist toggle;
   - Soft toggle and Sustain slider;
   - the Advanced button, theme toggle and Settings gear;
   - preset name and arrows.
6. **Advanced view groups:**
   - **Hammer:** Hardness · Touch
   - **Tuning:** A4 · Stretch
   - **Strings:** Spread · Resonance
   - **Output:** Brightness · Width · Volume
   - **Pedals:** Sustain slider · Soft toggle

   Fitting five groups in the side column is a layout check for the first build.
7. **Header:**
   - A **Settings** gear menu in both views, with separate *Invert sustain pedal* and *Invert soft pedal* checkboxes.
   - The advanced view shows a **Notes** meter (notes currently sounding, not idle) and a **CPU** meter (% of one core). This replaces the prototype's "Voices", because Forte has no voices.

### Not in v1

8. No soft-pedal amount control and no separate half-pedal display. The Sustain slider and the keyboard glow already show both.

### Handed to [Presets, saved state and automation](0018-presets-state-automation.md)

- The preset name and arrows: what they do.
- Which of the controls above are host-automatable and saved in presets.
- How the pedal invert settings and theme are saved (per machine or per project).
