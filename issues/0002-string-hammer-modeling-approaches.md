---
id: 2
title: String & hammer modeling approaches
labels: [wayfinder:research]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Which real-time methods exist for modeling piano strings and the felt-hammer/string interaction, and how do they compare for Forte's quality bar and CPU budget (88-key sustain glissando, ≤ ~25% of one core, 128-sample buffer)?

Compare at least: digital waveguides (with dispersion allpass filters for stiffness), modal synthesis, and finite-difference time-domain schemes. Also cover hybrid approaches. For each method, cover how it handles inharmonicity, nonlinear hammer felt (velocity-dependent brightness), and string-mode/longitudinal effects, plus its rough per-voice CPU cost and implementation difficulty. Cite primary sources (e.g. Smith, Bank, Bensa, Chaigne & Askenfelt, Bilbao) and note any published commercial approaches.

## Resolution

Recommend **modal synthesis for the strings** (one 2nd-order resonator per partial), driven by a **lumped nonlinear felt hammer** (mass + power-law spring, `F = k·y^p`), following Bank, Zambon & Fontana's real-time modal piano (IEEE TASLP 2010).

- **Why modal:** it gives exact inharmonicity and velocity-dependent brightness from the hammer physics, it avoids the hammer's delay-free-loop problem, and it supports longitudinal "phantom partials" for the bass, which waveguides can't do precisely. It also vectorises well, and its parameters map directly to partials measured from recordings.
- **Cost:** the published measured full-polyphony cost, scaled to a 2020 core, puts strings plus hammers at roughly 4–16% of one core (bottom-up estimate ~10–12%). That fits inside the 25% budget.
- **Runner-up:** digital waveguides with dispersion allpass filters (cheaper, but no faithful longitudinal effects).
- **Rejected:** FDTD for real-time use (measured 7× to over 300× over budget), and commuted synthesis (conflicts with the separate soundboard, sympathetic resonance and restrike requirements).

Findings, comparison table and CPU estimates: [research/string-hammer-modeling.md](../research/string-hammer-modeling.md)
