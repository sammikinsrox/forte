---
id: 3
title: Soundboard, string coupling, dampers & sympathetic resonance models
labels: [wayfinder:research]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Which real-time methods exist for the parts of a piano beyond a single string, and what does each cost in CPU and complexity? The parts:

1. **Unison coupling:** 2–3 detuned strings per note coupled through the bridge, producing the two-stage decay and beating.
2. **Soundboard and body:** radiation and coloration, e.g. modal banks, commuted synthesis, convolution, or feedback delay networks.
3. **Dampers:** felt damping on note release, including half-pedaling (partial damper contact).
4. **Sympathetic resonance:** undamped strings ringing when the sustain pedal is down. Cover how to do this without simulating all strings at full cost.
5. **Una corda:** how the soft pedal's hammer shift (striking fewer strings, different felt) is modeled.

Note how each part connects to the string models surveyed in [String & hammer modeling approaches](0002-string-hammer-modeling-approaches.md). Cite primary sources.

## Resolution

All five parts fit inside about 7–22% of the CPU budget (estimate), leaving about 80% or more for strings and hammers. They all use one-way plumbing: string bridge force goes to the soundboard filter and to the sympathetic resonators.
- **Unison:** pre-computed Weinreich coupled modes, 2–3 per low partial (waveguide variant: 2–3 strings on a shared bridge filter). **Soundboard:** a shared stereo partitioned convolution plus bridge-region tone filters, with parallel second-order filters as the upgrade path (no commuted synthesis, since it breaks the nonlinear hammer).
- **Dampers:** ramped per-mode damping weighted by damper position, plus a "gap" model for half-pedaling. **Sympathetic:** about 1,000 per-key resonators fed one-way from the bridge bus and gated by the same damper state. **Soft pedal:** change which strings the hammer drives, plus softer hammer settings (no extra cost).
- Open: soundboard impulse-response source, CPU micro-benchmark, half-pedal reference data.

Findings: [research/soundboard-coupling-dampers.md](../research/soundboard-coupling-dampers.md)
