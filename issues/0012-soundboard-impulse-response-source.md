---
id: 12
title: Soundboard impulse response source
labels: [wayfinder:research]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Where can Forte get a grand-piano soundboard/body impulse response (or the data to make one) under a GPLv3-compatible licence? Options include published free measurements, synthesizing one from a modal plate model or published soundboard mode data, and estimating one from the reference recordings chosen in [Reference recordings & what we can measure from them](0005-reference-recordings-and-measurements.md). For each option, cover licence, quality and effort, and give a recommendation.

## Resolution

No measured grand-piano soundboard IR is available under a GPL-compatible licence. The free ones are uprights: one CC0 "knock", fine only as a test placeholder. The C3 IRs and Bank's data aren't published, and Bank's code is non-commercial.
Recommendation: write our own offline generator, `tools/make-soundboard-ir`, that builds the IR as "modal noise". Low modes are placed by hand from published concert-grand values, and the rest are spread statistically with about 2% loss factor, so the generator and its output are all GPLv3.
Calibrate its colour and decay against the Plath concert-grand dataset (Zenodo 10.5281/zenodo.3274772, CC BY 4.0, GPL-compatible per the FSF). It has bridge accelerometer and anechoic dummy-head recordings of the same notes. Cross-check against Iowa and Salamander. A physical plate model is deferred past v1.
Ship the generated IR and its parameters, with a CC BY credit line. The 4.6 GB dataset stays out of the repo.
Findings: [research/soundboard-impulse-response.md](../research/soundboard-impulse-response.md)
