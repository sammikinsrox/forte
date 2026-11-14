---
id: 5
title: Reference recordings & what we can measure from them
labels: [wayfinder:research]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Which freely licensed recordings of real concert grands can serve as Forte's objective reference, and what can we reliably measure from them?

- **Sources:** Salamander Grand Piano and alternatives (e.g. University of Iowa MIS, other CC/public-domain sets). Cover licence, download location, velocity layers, which notes are sampled, and whether pedal-down/release samples exist.
- **Measurements:** per-note inharmonicity coefficient, partial frequencies and decay rates (including two-stage decay), spectral centroid vs velocity, attack transients, release behaviour, and sympathetic resonance. Cover the methods and tools for each (e.g. Python with numpy/scipy/librosa).
- **Published data:** any measured piano parameter tables (string dimensions, inharmonicity per note, hammer felt parameters) that could seed the physical model directly.

## Resolution

We use three reference sets, each for what it measures best:

- **Per-note measurements:** University of Iowa MIS Steinway B. It covers every note at pp, mf and ff, with unrestricted use. We measure inharmonicity, partial frequencies and decay rates (including two-stage decay) from it.
- **Velocity, attack and release, plus the listening A/B:** Salamander Grand V3 (Yamaha C5, CC BY 3.0, 16 velocity layers, release samples).
- **Sustain pedal and sympathetic resonance:** Piano in 162 (Steinway B), which has pedal-up and pedal-down samples. Its licence is unclear, so we only measure it locally and never redistribute it.

The model's starting values come from Chabassier & Duruflé's Steinway D parameter report and from Chaigne & Askenfelt (1994) for hammer and string values. We then fit them per note against the recordings.

We ship derived numbers (JSON/CSV) plus a credits file, and no audio. A download script fetches the recordings for the validation suite.

None of the free sets is a concert-size grand, so the recordings check trends and the published Steinway D tables set the absolute targets.

Findings: [research/reference-recordings.md](../research/reference-recordings.md)
