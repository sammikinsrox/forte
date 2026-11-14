---
id: 11
title: Modal resonator CPU benchmark
labels: [wayfinder:task]
status: closed
assignee: claude
parent: 1
blocked_by: []
---

## Question

Do the CPU estimates from [String & hammer modeling approaches](0002-string-hammer-modeling-approaches.md) and [Soundboard, string coupling, dampers & sympathetic resonance models](0003-soundboard-coupling-damper-models.md) hold on real hardware? Write a throwaway C++ micro-benchmark on this machine. It should cover:

- a vectorized bank of modal resonators sized for a full 88-note sustain glissando (about 10,000 resonators);
- the about 1,000 sympathetic resonators;
- one stereo 20,000-tap FFT convolution at a 128-sample block, 48 kHz.

Report the share of one core used, and scale it to a 2020 mid-range CPU. Record the CPU model the benchmark ran on.

## Resolution

**Answer: yes, with lots of room to spare.** The full 88-key sustain glissando (strings, hammers, sympathetic bank and soundboard convolution) uses about **4% of one core on average and about 6% in the worst 1% of blocks** with AVX2. Scaled to a 2020 mid-range CPU, that is roughly **5% average / 8% p99**, against a target of 25%. Even a plain SSE2 build, with no AVX2, fits: about **10% / 15%** scaled. The research estimates (10–12% for the strings, 7–22% of the budget for the rest) were pessimistic by about 3×.

Prototype: [prototypes/cpu-benchmark/](../prototypes/cpu-benchmark/). It is throwaway code: `bench.cpp` plus `build.bat` (one command: builds both versions with MSVC and runs them).

### Setup

- **Machine:** AMD Ryzen 5 5500 (Zen 3, 6 cores / 12 threads, 3.6 GHz base / 4.2 GHz boost), Windows 11. MSVC 14.51 (Visual Studio 2026), `/O2 /fp:fast`, with `/arch:AVX2` (8 floats per instruction, FMA) or the default x64 SSE2 build (4 floats, no FMA). Single thread, pinned to one core, high priority. Flush-to-zero and denormals-are-zero are on.
- **Run:** 48 kHz, 128-sample blocks, 30 s of simulated audio (11,250 timed blocks after 400 warm-up blocks). Budget: one block = 2.667 ms = 100% of one core.
- **a. Strings + hammers.** 9,675 two-pole resonators, padded to 10,008 for SIMD. Struct-of-arrays layout, 4 vectors in flight at once. Per note:
  - transverse partials below 20 kHz with inharmonicity (6,299 in total);
  - 2 extra coupled unison modes per extra string for the lowest 32 partials (3,076);
  - 10 longitudinal modes for the lowest 30 notes (300).
  
  That is 231 modes at A0, 158 at A2, 99 at A4, 54 at A5 and 12 at C8. The power-law felt hammer (`F = K·compression^2.5`, hammer mass 11.5 → 6 g) runs sample by sample against the string displacement for a 4 ms contact window. Every mode of all 88 notes runs all the time, with no culling. The glissando moves up and down continuously, one note every 20 ms.
- **b. Sympathetic bank.** 1,056 resonators (12 per key), driven by the summed bridge signal.
- **c. Soundboard convolution.** One input (the bridge bus), two outputs, with a 20,000-tap IR per channel. The IR is uniformly partitioned (157 × 128) and run with overlap-save, using a simple 256-point radix-2 FFT. Checked against direct convolution: relative error 4×10⁻⁷.
- **Checks:** all outputs finite, no denormal states. A stress variant adds a 10-note chord every second (up to 11 notes in hammer contact at once).

### Results (share of one core per 128-sample block)

| Part | AVX2 avg | AVX2 p99 | SSE2 avg | SSE2 p99 |
|---|---|---|---|---|
| a. Strings + hammers (~10,000 modes) | 0.082 ms · 3.1% | 0.124 ms · 4.7% | 0.174 ms · 6.5% | 0.253 ms · 9.5% |
| b. Sympathetic (1,056 modes) | 0.010 ms · 0.4% | 0.016 ms · 0.6% | 0.023 ms · 0.9% | 0.036 ms · 1.4% |
| c. Stereo 20k-tap convolution | 0.017 ms · 0.6% | 0.027 ms · 1.0% | 0.022 ms · 0.8% | 0.034 ms · 1.3% |
| **Total, this machine** | **0.109 ms · 4.1%** | **0.165 ms · 6.2%** | **0.219 ms · 8.2%** | **0.316 ms · 11.9%** |
| **Total, scaled to a 2020 mid-range CPU (×1.25)** | **≈ 5%** | **≈ 8%** | **≈ 10%** | **≈ 15%** |

- **Worst single block:** 0.24 ms (9.1%) with AVX2 and 0.52 ms (19%) with SSE2.
- **Chord stress run:** within noise of the plain glissando (AVX2: 4.0% avg / 5.7% p99; SSE2: 8.3% / 12.7%).
- **String bank cost:** 0.064 ns per resonator per sample with AVX2, about 0.28 cycles at boost clock. SSE2 is 0.138 ns. The research assumed 0.25–1 cycle, so this is at the cheap end. AVX2 is about 2.1× faster than SSE2 for the resonators. For the convolution it is only 1.3× faster, because the scalar FFT is not vectorised.

**Scaling assumption:** "2020 mid-range" means a Ryzen 5 3600 (Zen 2) or Core i5-10400 (Comet Lake). Both have two 256-bit FMA units per core and boost to about 4.2 GHz. This test is limited by how fast the core can do vector arithmetic, so those chips should run it at about 80–90% of the speed of this Zen 3 core (mostly lower IPC). I multiplied by 1.25 to stay on the safe side. This is an estimate; no 2020 CPU was measured.

**Verdict:** ✅ **The architecture fits the budget.** The full glissando plus all the extras uses about a third of the 25% target with AVX2, and about 60% of it with SSE2.

### Caveats

- **This is the raw cost of the DSP only.** It leaves out:
  - per-block coefficient updates for dampers, half-pedal and parameter smoothing;
  - the nonlinear longitudinal drive (the squared sum of mode amplitudes; the research puts it at ~1%);
  - unison coupling *between* banks (here the unison modes are only extra modes driven by the hammer);
  - per-note direct output or panning, voice management, and plugin/host/GUI overhead.
  
  Expect the real engine to cost **2–3× these numbers**. That is still within the 25% target with AVX2. With SSE2 it would be borderline (~20–30%).
- **The hammer physics is only realistic enough for timing.** The parameters are rough guesses (peak force ~680 N in the treble, too high), and the hammer runs a fixed 4 ms contact window. The cost does not depend on the signal values, so the timing still holds. The real felt model and its stability (light treble strings with a stiff felt at 48 kHz went unstable until the parameters were softened) need proper design.
- **The convolution has one input** (the mono bridge bus) and two outputs. Four bridge-region convolutions, as floated in the soundboard research, would add about 4 × 0.6–1%, which is still cheap.
- **Single run on one machine.** Two AVX2 runs agreed within ±3%. The timer resolution is 100 ns. This was not run at 44.1/96 kHz. CPU cost scales with sample rate, so 96 kHz costs about 2×: ~10% / 16% scaled with AVX2.

### New questions (for "Voice management and CPU strategy")

- **Mode budget:** there is headroom for more modes than the research assumed (e.g. no 20 kHz cap tightening, more unison-coupled partials). Mode culling matters less for CPU than expected. It may still be worth doing to keep 96 kHz comfortable.
- **AVX2 requirement:** AVX2 halves the cost but is not needed to meet the budget at 48 kHz. Recommend shipping AVX2 with an SSE2 fallback chosen at runtime, rather than requiring AVX2.
- **96 kHz:** the budget is probably fine with AVX2. Decide whether to run the string bank at 48 kHz internally when the host runs at 96 kHz.
