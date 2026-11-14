---
id: 13
title: "Listening perspective: pianist's seat or mics over the lid?"
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

Where is the listener in Forte's sound? Options: at the pianist's seat (the binaural dummy-head data from the Plath dataset, used as-is), a stereo microphone pair over the open lid (the usual recorded-piano sound), or a choice between the two. This decides how the soundboard IR is calibrated (see [Soundboard impulse response source](0012-soundboard-impulse-response-source.md)), and possibly whether the editor gets a perspective control.

## Resolution

Decided with the user:

1. **Both perspectives ship in v1** as a user choice called the **Listening perspective**:
   - **Microphones** (default): a close stereo pair over the open lid, the familiar recorded-piano sound. Its soundboard IR is calibrated against the close-mic reference recordings (Iowa, Salamander).
   - **Pianist**: the player's-seat sound. Its soundboard IR is calibrated from the Plath dummy-head data.

   Only one runs at a time, so there's no extra CPU. The cost is a second IR calibration (about a week of tool work, estimated).
2. **Stereo layout:** bass on the left, treble on the right, in both perspectives.
3. **Editor:** a two-way toggle reading **Microphones | Pianist**, under the preset name in the simple view and in the advanced view header.
4. **Width knob** (0 = mono, 100 = full stereo) in the advanced view's "Pedals and output" group.
