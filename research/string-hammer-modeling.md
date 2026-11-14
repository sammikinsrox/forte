# String & hammer modeling approaches

Research for ticket [0002 String & hammer modeling approaches](../issues/0002-string-hammer-modeling-approaches.md).

**Question:** Which real-time methods exist for modeling piano strings and the felt-hammer/string interaction? How do they compare for Forte's quality bar and CPU budget? The budget is an 88-key sustain glissando at a 128-sample buffer and 48 kHz, using no more than about 25% of one mid-range 2020 core.

## Short answer

Use **modal synthesis for the strings**, driven by a **lumped nonlinear felt hammer** (a mass on a power-law spring). This is the architecture of Bank, Zambon & Fontana's real-time modal piano [B10]. It is the only published complete piano model that does all of the following:

- reproduces inharmonicity exactly,
- produces velocity-dependent brightness from real hammer physics,
- adds longitudinal "phantom partials" (the metallic bass character),
- has a measured full-polyphony CPU figure that fits Forte's budget once scaled to 2020 hardware.

Digital waveguides with dispersion allpass filters are the cheaper, well-proven runner-up. Finite-difference (FDTD) schemes are the most physically complete, but they are 10–300× over budget for a whole piano. Use them as an offline reference only. Commuted synthesis is cheap, but it gives up the physical interactions that Forte's v1 scope needs (sympathetic resonance, restrike, soundboard as a separate part).

## Plain-language primer

A piano string vibrates at many frequencies at once: the fundamental plus overtones (**partials**). Because the steel is stiff, the partials sit progressively sharper than exact multiples of the fundamental (**inharmonicity**, `f_k = k·f0·√(1 + B·k²)` [B03 eq. 12]). The ways to simulate this differ mainly in *what they compute every sample*:

| Method | What it computes each sample | Analogy |
|---|---|---|
| **Digital waveguide** | A wave travelling round a delay line, with filters in the loop for losses and stiffness | An echo bouncing between two walls |
| **Modal synthesis** | Each partial as its own decaying sine oscillator (a 2nd-order resonator), summed | An organ with one pipe per partial |
| **Finite differences (FDTD)** | The displacement of the string at dozens to hundreds of points along its length, stepped forward in time | A flip-book animation of the whole string |
| **Commuted synthesis** | A waveguide string excited by a stored recording of the soundboard, shaped by hammer filters | Playing a recorded "knock" into a resonant tube |
| **Hybrids** | Mixes of the above, e.g. modal string plus a filter-based soundboard, or FD transverse plus modal longitudinal | — |

The **hammer** in every physical method is the same idea: a small mass hitting the string through felt. Felt gets stiffer the harder it is squeezed (`F = k·y^p`, with p ≈ 2.3–3 [B03 eq. 2, Table 1; CA94]). A harder strike therefore means a shorter, sharper contact and a brighter tone. That is the velocity-dependent brightness Forte needs, and it comes out of the physics for free.

## Method-by-method

### 1. Digital waveguides (with dispersion allpass filters)

- **How it works.** The travelling-wave solution of the wave equation is sampled into delay lines. Losses, stiffness and fine tuning are lumped into one reflection filter `H_r = −H_l·H_d·H_fd` [B03 §4; Smith PASP]. Most piano synthesizers before 2010 used this "due to its efficiency" [B10 §I–III].
- **Inharmonicity.** This needs a dispersion allpass filter `H_d`. Bass notes need high orders: Rocchesso & Scalcon placed "several tens of partials" correctly "with the allpass filter order not exceeding 20", and their C2 example is 18th order [B03 §4.2]. Robust high-order designs exist [AVS10; Smith PASP "Dispersion Filter Design"]. B03 calls the cost of `H_d` "heavy". A perceptual shortcut applies: partials above a pitch-dependent bandwidth can be made harmonic without audible effect [B03 §4.2, citing Rocchesso & Scalcon 1999 and Järveläinen et al. 2001]. Low notes can also run at half sample rate to save cost [B03 §4.2].
- **Nonlinear hammer.** Connecting a mass/spring hammer to the string creates a delay-free loop: force depends on string position and vice versa. The naive fix (use last sample's force) "drives the system towards instability" at high velocities. The published fixes are the *K-method* (a precomputed lookup table), a *multirate hammer* run at 2× sample rate (negligible extra cost, stable up to 10× higher impact velocity), or a wave-digital hammer [B03 §3.2, Fig. 2].
- **Longitudinal / phantom partials.** Precise modeling "is not possible with digital waveguides in their efficient form" because the lumped dispersion filter makes the string modes non-orthogonal. You either distribute the dispersion filter ("increasing the model complexity significantly") or use "less physical tricks" [B10 §I].
- **Unison coupling.** Use two or three waveguides per note with a coupling filter, or one waveguide plus 5–10 parallel resonators at 1/8–1/16 sample rate (under 5 multiplications per sample per note) [B03 §4.3].
- **Difficulty.** Medium. Loss-filter design must preserve stability (|H| < 1). Tuning needs fractional-delay filters, and dispersion filter design is non-trivial. The code is serial per string, so it is harder to vectorise (SIMD).

### 2. Modal synthesis

- **How it works.** The string's PDE is decoupled into independent modes. Each mode is a 2nd-order resonator, and the output is their weighted sum [B10 §III]. The structure is "fully parallel" with "simple code that is easier to optimize", and the authors' SSE implementation gave "a speed-up factor of almost four" [B10 §I, §VIII].
- **Inharmonicity.** This is exact and free: each mode's frequency is set directly (no filter design). Each mode also gets its own decay time, so frequency-dependent loss is trivial [B10 §III].
- **Nonlinear hammer.** Same mass + power-law spring as above. With impulse-invariant discretization, the string model itself contains a unit delay, so "the delay-free loop does not arise" [B10 §IV]. The hammer only runs during contact ("first few ms"). B10 neglects felt hysteresis. An earlier review found informally that hysteresis "does not improve the sound quality significantly" [B03 §3.1].
- **Longitudinal / phantom partials.** This is "very straightforward with modal synthesis" [B10 §I]. Tension variation is the squared sum of transverse mode amplitudes, and it drives a few (~10) longitudinal resonators. These produce both the longitudinal peaks and the phantom partials, which matter most "in the low note range where it greatly contributes to the metallic character" [B10 §V; Bank & Sujbert 2005]. The cost is dominated by pairwise products of transverse amplitudes (~1000 multiply-adds in their example). An odd/even shared-force trick reduces it [B10 §V-B].
- **Unison coupling / sympathetic resonance.** Secondary resonator banks are driven by the hammer force (for beating and two-stage decay). They also receive the summed outputs of all strings, giving one-way, "structurally stable" sympathetic coupling [B10 §VI]. Dampers and half-pedal fit naturally as per-mode decay-time changes. That design is for ticket 0003.
- **Difficulty.** Low to medium. The maths is simple, SIMD is easy, and parameters map one-to-one to partials measured from recordings. "Simpler parameter estimation and the possibility of modifying the parameters in real-time" [B10 §I]. The main risk is mode count, because bass notes have hundreds of partials.

### 3. Finite-difference time-domain (FDTD)

- **How it works.** The stiff, lossy string PDE is discretized on a spatial grid and stepped in time [CA94; Bensa et al. 2003; Bilbao 2009]. Chaigne & Askenfelt's 1994 FD string + nonlinear hammer model is the reference physical piano string model. It was validated against measurements in their Part II [CA94].
- **Inharmonicity.** Comes directly from the stiffness term. Numerical dispersion must be controlled, and Bensa et al. relate well-posed FD schemes directly to waveguides [Bensa et al. 2003].
- **Nonlinear hammer and longitudinal effects.** These are the most complete. Geometrically exact models give longitudinal motion and phantom partials with velocity-dependent brightness emerging naturally [DBW22 Fig. 4]. But the stability condition of the longitudinal wave "has to be chosen much larger than typical audio rates", needing "an oversampling factor of at least 12" [DBW22].
- **Measured cost.**
  - [DBW22], geometrically exact string, fastest C++ scheme: one D3 string at 12× oversampling ran at **0.756× real time on a 2016 2.9 GHz i7**, so **one string ≈ 75% of a core**.
  - [RHB25], simpler cubic-nonlinear stiff string: **1.0–3.6% of an Apple M3 core per string** (32-bit, 44.1 kHz, f0 200→20 Hz, 84→360 grid points), with "60 note polyphony" at 96 kHz using multi-threading.
  - Full piano FD/FE simulations (strings + soundboard + air) are research-grade and far from real time [Chabassier, Chaigne & Joly 2014; Chabassier, Duruflé & Joly 2016].
- **Difficulty.** High. You need stability analysis, implicit or SAV schemes, and oversampling, and parameters can't be freely modulated in real time because "the stability condition depends on their values" [RHB25 §7.3].

### 4. Commuted synthesis (and hybrids)

- **Commuted.** Because string and soundboard are linear, their order can be swapped. The soundboard response becomes a stored excitation table feeding a waveguide string, and the hammer becomes velocity-dependent filters [Smith PASP "Commuted Piano Synthesis"; Smith & Van Duyne 1995]. It has "high fidelity and low computational cost", but the hammer filters "have to be precalculated and stored for all notes and hammer velocities", and "restrike ... cannot be precisely modeled" [B03 §1]. B03 adds that cheap soundboard models have since reduced commuted synthesis's advantage. It also bakes the soundboard into each note, which works against Forte's separate soundboard, sympathetic resonance and half-pedal goals.
- **Hybrids in the literature.**
  - Waveguide string + parallel modal resonators for beating [B03 §4.3].
  - Modal string + filter/convolution soundboard [B10].
  - FD transverse + modal longitudinal ("LU_NIT" in [DBW22]).
  - Bank's 2003 conclusion recommends "building blocks with physical input and output parameters, but whose inner structure does not necessarily follow physical model" [B03 §6].

### Commercial approaches (published)

- **Modartt Pianoteq** (2006) is the first commercial software physical-model piano [B10 §I]. Modartt has not published its algorithm.
- **Roland V-Piano** (2009) is the first physical-modeling digital piano [B10 §I]. Its algorithm is also unpublished.
- **Viscount** funded the Verona modal piano [B10, acknowledgements]. That is the most detailed public description of a commercially backed real-time piano model.
- **Generalmusic** funded Borin et al.'s 1997 waveguide piano with a shared-termination sustain-pedal model, which was patented as US 5,744,743 [B03 §1, ref 45].

## Comparison table

| | Digital waveguide + dispersion allpass | Modal synthesis | FDTD | Commuted |
|---|---|---|---|---|
| **Inharmonicity** | Good. Needs high-order allpass in bass (≤ ~20th order) [B03] | **Exact, free** (set each mode's frequency) | Natural from physics, but watch numerical dispersion | Same as waveguide |
| **Velocity-dependent brightness** | Physical (nonlinear hammer). Needs delay-free-loop fix | **Physical. No delay-free loop with impulse-invariant design** [B10] | Physical, most accurate | Approximated by precomputed filters |
| **Longitudinal / phantom partials** | Hard. Modes non-orthogonal [B10] | **Straightforward** (squared-sum tension → longitudinal resonators) [B10] | Natural, but needs ~12× oversampling [DBW22] | No |
| **Unison beating / two-stage decay** | 2–3 coupled waveguides, or + 5–10 resonators | Secondary resonator banks | Coupled grids (expensive) | Limited |
| **Sympathetic, dampers, half-pedal** | Shared termination (classic) | One-way coupling into secondary banks. Per-mode damping | Physical, expensive | Poor fit |
| **Restrike of a ringing string** | Yes | Yes | Yes | No [B03] |
| **SIMD / parallel friendliness** | Poor (serial loop per string) | **Excellent** [B10] | Good within a grid | Poor |
| **Parameter fitting from recordings** | Indirect (filter design) | **Direct** (freq/decay/amp per partial) | Physical constants. Indirect | Needs per-note/velocity tables |
| **Implementation difficulty** | Medium | Low–medium | High | Medium |
| **Full-piano CPU vs Forte budget** | ~5–10% of core (est.) ✅ | ~4–16% of core (scaled from measured; bottom-up est. ~10–12%) ✅ | 7× to >300× over ❌ | ~2–5% (est.) ✅ but fails scope |

## CPU cost against Forte's budget

### The budget in numbers

- **Reference core.** A mid-range 2020 desktop core (e.g. Ryzen 5 3600 / Core i5-10400 class) runs at ~4 GHz with AVX2 (8 floats per instruction) and FMA. *These are typical spec figures, not measured here.*
- **25% of that core** ≈ **1.0 × 10⁹ cycles/s**. At 48 kHz that is **≈ 21,000 cycles per output sample for the whole instrument**. At 128 samples per buffer, one block (2.67 ms of audio) must on average finish in ≈ 0.67 ms.
- **Strings + hammers share.** I assume they get ~60% of that, leaving the rest for soundboard, sympathetic routing, voice management and host overhead. That gives **~12,500 cycles/sample ≈ 140 cycles per note per sample** with all 88 notes sounding.

### Per-method estimates

Figures marked **measured** come from the cited papers. Figures marked **est.** are my arithmetic from operation counts and should be confirmed by a prototype benchmark.

**Modal (recommended)**

- **Measured [B10].** Full polyphony: "10 000 second-order resonators and four 20 000 tap convolutions, at an approximate load of 30% on a Intel's Core 2 Duo@2.4-GHz laptop", with SSE. The soundboard convolutions are "around one fourth" of the string+hammer cost. Strings + hammers are therefore ≈ 24% of that machine.
- **Scaling (est.).** A 2020 core is roughly 3–6× faster per core for this kind of SIMD loop (~1.7× clock, 2× vector width, FMA, better IPC). The paper does not say whether 30% means one core or the whole dual-core chip. Taking both readings, strings + hammers come out at **≈ 4–16% of a 2020 core**.
- **Bottom-up check (est.).**
  - Bank's ~10,000 resonators average ≈ 114 per note.
  - At ~0.75 cycles per resonator update with AVX2 FMA (3 multiplies per resonator [B10 §VIII] across 8 lanes, plus memory traffic): 88 × 115 × 0.75 ≈ 7,600 cycles/sample → **≈ 9% of a core**.
  - Longitudinal forces for the lowest ~30 notes, using the odd/even trick: ~3 multiplies per involved transverse mode [B10 §VIII], under ~1,000 extra cycles/sample → **≈ 1%**.
  - Hammers run only for ~2–4 ms after each note-on. Even 10 overlapping strikes in a fast glissando cost ~1,500 operations/sample briefly, so the effect on the average is negligible.
- **Total ≈ 10–12% of a core.** That is inside the ~15% string share, with headroom for culling (B10 already caps modes by frequency and count, and suggests "selectively deactivating the partials of a single note when they are not audible anymore").

**Digital waveguide (est.)**

- Per string: a delay line, a one-pole loss filter (~3 ops), a tuning allpass (~3 ops), and a dispersion cascade (bass ~8–20 sections, mid 2–4, treble 0–1). That averages ~30 operations, mostly scalar → ~30–40 cycles per string-sample.
- One waveguide + resonators per note: 88 × ~45 ≈ 4,000 cycles → **≈ 5%**.
- Full 2–3 waveguides per note (~230 strings): ≈ 8,000 cycles → **≈ 10%**.
- Longitudinal effects would be extra and only approximate.

**FDTD**

- **Linear stiff string, one per note (est.).** 50–200 grid points × ~10 ops, vectorised → ~100–300 cycles per string-sample. 88 strings gives ~9,000–26,000 cycles → **≈ 11–32%**, i.e. the whole budget, with no unisons, no longitudinal motion and no soundboard.
- **Cubic-nonlinear string (measured [RHB25], scaled).** 1–3.6% of an M3 core per string, which is ~2–7% of a 2020 x86 core (M3 core assumed ~1.5–2× faster). ×88 strings ≈ **175–600%**, about **7–25× over**. ×230 strings ≈ 18–60× over.
- **Geometrically exact with longitudinal motion (measured [DBW22]).** ~75% of a 2016 i7 core per string. A full piano would be **>300× over**.

**Commuted (est.)**

- A low-order waveguide plus a table read per string ≈ 20 cycles × 88–230 strings → **≈ 2–5%**. It is cheapest, but it fails the v1 physics scope (see §4).

### Real-time safety notes (all methods)

The 128-sample deadline is about the worst block, not the average.

- Do no allocation or locking in the audio thread.
- Precompute resonator coefficients off the audio thread. B10 does this at a 20–50 Hz "calibration rate".
- Flush denormals.
- Bound the per-block cost. B10 suggests a polyphony queue limited by *total computational cost*, not note count.

## Recommendation

**Choose modal synthesis for strings, with a lumped nonlinear felt hammer, following [B10].**

1. **String model.** For each note, use a bank of 2nd-order resonators (one per transverse partial), with frequencies from `f_k = k·f0·√(1 + B·k²)` and an individual decay time per mode. Cap the mode count by frequency (below ~20 kHz and Nyquist) and by a per-note limit. Process 8 modes per AVX2 instruction.
2. **Hammer.** Use a mass plus power-law felt spring (`F = k·y^p`), with per-note mass, stiffness and exponent from published measurements [B03 Table 1; CA94]. Discretize it impulse-invariantly so that no delay-free loop arises. Run it only during contact. Skip felt hysteresis in v1, and revisit only if listening tests ask for it.
3. **Longitudinal / phantom partials.** Add them to bass notes using the squared-sum tension method, with the odd/even shared-force shortcut. Gate the feature by register so its cost stays bounded.
4. **Unisons, sympathetic resonance and dampers** reuse the same resonator machinery (secondary banks, per-mode damping). Ticket 0003 owns those details.
5. **Do not use FDTD in the plugin.** Keep it as an optional *offline* reference model to sanity-check the modal model's hammer/string behaviour.
6. **Fallback.** If modal mode counts prove too costly in the bass, run the lowest octaves at half sample rate [B03 §4.2] or switch only those notes to waveguide strings. Both are published, well-understood techniques.

**Why, in plain language:**

- **Tuning.** Modal synthesis lets us set every overtone's pitch, loudness and decay directly. Those are exactly the numbers we can measure from the Salamander recordings, so matching the reference becomes a fitting problem rather than a filter-design puzzle.
- **Physics.** It is the only cheap method in which the bass's metallic "phantom partials" can be added faithfully.
- **Proof.** It has a published, measured full-piano real-time result that fits the budget on 2020 hardware.
- **Code.** The code is simple and parallel, which suits modern CPUs and makes the DSP easier to read and debug.
- **Costs.** More operations than a waveguide, and more parameters to store, both of which the measurements show are affordable.

## Sources

- **[B03]** B. Bank, F. Avanzini, G. Borin, G. De Poli, F. Fontana, D. Rocchesso, "Physically Informed Signal Processing Methods for Piano Sound Synthesis: A Research Overview," *EURASIP J. Applied Signal Processing* 2003:10, 941–952. https://air.unimi.it/retrieve/dfa8b99e-d2be-748b-e053-3a05fe0a3a96/bank_jasp03.pdf
- **[B10]** B. Bank, S. Zambon, F. Fontana, "A Modal-Based Real-Time Piano Synthesizer," *IEEE Trans. Audio, Speech, and Language Processing* 18(4):809–821, 2010. DOI 10.1109/TASL.2010.2040524. Post-print: https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf. Sound examples: https://home.mit.bme.hu/~bank/publist/taslp-piano/index.html
- **[CA94]** A. Chaigne, A. Askenfelt, "Numerical simulations of piano strings. I. A physical model for a struck string using finite difference methods," *JASA* 95(2):1112–1118, 1994, and "... II. Comparisons with measurements and systematic exploration of some hammer-string parameters," *JASA* 95(3):1631–1640, 1994. https://doi.org/10.1121/1.408459
- **Bensa et al. 2003:** J. Bensa, S. Bilbao, R. Kronland-Martinet, J. O. Smith III, "The simulation of piano string vibration: From physical models to finite difference schemes and digital waveguides," *JASA* 114(2):1095–1107, 2003. https://doi.org/10.1121/1.1587146
- **[DBW22]** M. Ducceschi, S. Bilbao, C. Webb, "Real-time simulation of the struck piano string with geometrically exact nonlinearity," ENOC 2022, Lyon. https://cris.unibo.it/retrieve/775112f7-ce58-46dc-acde-59ed7263cf16/ENOC2022_PianoString1-6-2-9.pdf
- **[RHB25]** T. Risse, T. Hélie, S. Bilbao, "Power-balanced drift regulation for scalar auxiliary variable methods: application to real-time simulation of nonlinear string vibrations," DAFx25, Ancona. https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_24.pdf
- **[AVS10]** J. S. Abel, V. Välimäki, J. O. Smith III, "Robust, efficient design of allpass filters for dispersive string sound synthesis," *IEEE Signal Processing Letters* 17(4):406–409, 2010. DOI 10.1109/LSP.2010.2040924. Code and notes: https://ccrma.stanford.edu/wiki/Dispersion_Filter_Design
- **Smith PASP:** J. O. Smith III, *Physical Audio Signal Processing* (online book), CCRMA:
  - "Commuted Piano Synthesis": https://ccrma.stanford.edu/~jos/pasp/Commuted_Piano_Synthesis.html
  - "Piano Hammer Modeling": https://ccrma.stanford.edu/~jos/pasp/Piano_Hammer_Modeling.html
  - "Dispersion Filter Design": https://ccrma.stanford.edu/~jos/pasp/Dispersion_Filter_Design.html
- **Bilbao 2009:** S. Bilbao, *Numerical Sound Synthesis: Finite Difference Schemes and Simulation in Musical Acoustics*, Wiley, 2009 (stiff strings and hammer chapters). Textbook background for FDTD; not quoted above.
- **Chabassier et al.:** J. Chabassier, A. Chaigne, P. Joly, "Time domain simulation of a piano. Part 1: model description," *ESAIM: M2AN* 48(5):1241–1278, 2014. https://www.numdam.org/item/M2AN_2014__48_5_1241_0. Part 2 (Chabassier, Duruflé, Joly), *M2AN* 50(1):93–133, 2016. https://www.numdam.org/articles/10.1051/m2an/2015007/
- **Bank & Sujbert 2005:** B. Bank, L. Sujbert, "Generation of longitudinal vibrations in piano strings: From physics to sound synthesis," *JASA* 117(4), 2005. Cited via [B10] ref [12]; not read directly.
- **Smith & Van Duyne 1995:** J. O. Smith III, S. A. Van Duyne, "Commuted piano synthesis," Proc. ICMC 1995, 335–342. Cited via [B03].

*Caveats:* The 2020-core figures and all "est." CPU numbers are engineering estimates from operation counts and generational speed-ups, not benchmarks. A prototype benchmark of an 88-note modal bank on the target class of CPU should confirm them before the architecture is locked (see the questions in the ticket resolution).
