---
id: 7
title: Choose the synthesis architecture
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: [2, 3, 11, 12]
---

## Question

Given the findings of [String & hammer modeling approaches](0002-string-hammer-modeling-approaches.md) and [Soundboard, string coupling, dampers & sympathetic resonance models](0003-soundboard-coupling-damper-models.md), which modeling method does Forte use for each part? The parts are strings, hammer, unison coupling, soundboard/body, dampers, sympathetic resonance and una corda. How do the parts connect into one signal path that fits the CPU budget?

## Progress (2026-11-14)

Round 1, all accepted:
1. **Strings:** modal, one resonator per partial, partials up to 20 kHz (or just under the sample rate's limit, if lower).
2. **Hammer:** lumped mass with a felt spring that stiffens under compression and has slight hysteresis. It interacts with the string sample by sample, and the strike position (~1/8 of the string length) shapes which partials are excited. Mass, stiffness and hardness are set per note.
3. **Unisons:** real Steinway D string counts (about 10 single, 18 double, the rest triple). Weinreich coupled modes are pre-computed for the lowest 32 partials.
4. **Phantom partials:** added to v1 for the lowest ~30 notes (~1% CPU). This extends the v1 physics list.
5. **Soundboard:** four bridge regions (bass, tenor, middle, treble). Each region is convolved with its own stereo IR, using only the active Listening perspective's set. Width is applied at the output. Stereo placement comes from the regions, not from panning.
6. **Connections are one-way:** strings → bridge regions and the sympathetic bank, with nothing flowing back. The soundboard's energy drain is already included in the measured decay times.
7. **Dampers:** per-partial damping driven by a 0–1 damper position, with a gap model for half-pedaling. Roughly the top 18 notes are undamped (exact boundary from Steinway D data).
8. **Sympathetic:** a separate bank of 12 modes per key, fed from the bridge and gated by that key's damper.
9. **Soft pedal:** a continuous 0–1 hammer shift that changes which strings are struck and how hard the felt is. The pedal response itself belongs to touch mapping.
10. **No voices:** 88 permanent per-note string models. Repeated notes are restrikes of the still-ringing strings, and there is no voice stealing. (The glossary already rules out "string set" as a synonym for Unison, so code uses a per-note model name instead, and the glossary uses Note/Unison/Restrike.)

Round 2, all accepted:
11. **Sympathetic routing:** played strings → sympathetic bank → soundboard (in the bank's own bridge region). The sympathetic strings never feed each other or the played strings.
12. **Sample rate:** run at the host rate (44.1–96 kHz) with no internal resampling. Revisit only if 96 kHz misses the CPU target in the real build.
13. **Hammer stability:** compute hammer–string contact at 4× oversampling for the contact window only. An automated test hammers every note at maximum velocity with the hardest felt at every supported rate.
14. **Live parameter changes:**
    - strike-time parameters (hardness, velocity curve, soft-pedal felt) affect the next strike only;
    - tuning (A4, stretch) glides ringing modes over ~50 ms;
    - output parameters (volume, width, perspective, brightness, sympathetic) change immediately, smoothed over ~20 ms. A perspective change crossfades the IR sets.
15. **Resting:** modes below −100 dB stop computing. A note goes idle once it has decayed and its damper is down.
16. **Brightness** is a treble-tilt tone control on each bridge region's output (distinct from Hardness). **Sympathetic** is the sympathetic bank's level, 0–200%, where 100% is the physical level.

## Resolution (2026-11-14)

The user confirmed the full signal path:

```
MIDI → Hammer (felt spring, 4× contact, soft-pedal shift)
     → Note's Unison as modal resonators (stretch, coupled two-stage decay,
       phantom partials, damper position + gap)
     → one-way bridge force → [sympathetic bank (12/key, damper-gated)] → bridge region bus ×4
     → per-region stereo convolution (active perspective; crossfade on switch)
     → Brightness tilt per region → sum → Width → Volume → stereo out
```

Decisions 1–16 above are the spec. Expected cost: about 5–8% measured for the core parts, plus ~3% for bridge regions and ~1% for phantom partials. With the 2–3× allowance for the real engine, that is roughly 15–25% of one core for a full sustain glissando at 48 kHz. Confirm in the first real build.
