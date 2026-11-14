---
id: 6
title: Editor look & feel direction
labels: [wayfinder:prototype]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

What should Forte's editor look and feel like? Produce a few rough, contrasting visual mockups (e.g. skeuomorphic wood-and-brass, minimal modern, scientific/visualizing-the-physics), react to them together, and settle on a visual direction. That direction covers mood, palette, typography, the role of animation or live visualization, and the window size or aspect ratio.

The exact control set is still in the fog, so mockups may use placeholder controls.

## Resolution

The user chose **variant D**, a hybrid of the "Fallboard" (C) and "Scale drawing" (B) mockups, with no further changes. Prototype: [prototypes/editor-look-and-feel/index.html?variant=D](../prototypes/editor-look-and-feel/index.html) (throwaway; kept as the primary source).

- **Simple view (default):** fallboard layout with a large FORTE wordmark (Italiana) and the preset name beneath. Three large dials (Hammer, Tone, Resonance), a volume fader, and a red felt strip above a full-width playable 88-key keyboard. Soft-pedal toggle and a sustain slider marked Up / Half / Down.
- **Advanced view:** opened with an "Advanced" button and closed with "Simple". It shows a live overtone display of the last note played (stiff-string partials against perfect harmonics, decaying in real time), a stretch-tuning curve across all 88 notes, and the keyboard with keys lit by how strongly they ring. Controls are **knobs, not sliders**, grouped in pairs: Hammer, Tuning, Resonance, Pedals and output.
- **Values:** every control shows a whole number, and the user can type a value directly (Enter commits, ↑/↓ steps by 1, Esc cancels). Ranges: 0–100 for most controls, A4 in Hz, volume in dB.
- **Light and Dark themes**, toggled in both views. Light is white lacquer with ebony knobs. Dark is ebony lacquer with ivory knobs. The red felt strip (#9E1B32) and the keyboard stay the same in both. Typography: Italiana for the wordmark, Jost for everything else.
- The advanced view uses the same visual language as the simple view (not B's graph-paper look), so it reads as the same instrument opening up.
