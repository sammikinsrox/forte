#pragma once

// The engine's hot loops, built twice (SPEC §9.2): once for SSE2, which every x64 CPU has, and
// once for AVX2 with FMA. One binary carries both, and the engine picks a set when it's made:
// the best the CPU supports, unless asked otherwise.
//
// The two builds come from one source, Kernels.inc, compiled by KernelsSse2.cpp and
// KernelsAvx2.cpp (the latter with /arch:AVX2). That source includes nothing but <immintrin.h>
// and this header, which declares only types and data, and every function in it has internal
// linkage. That matters: an inline function or template from a shared header, compiled with
// /arch:AVX2, could be picked by the linker for the SSE2 path too and crash a CPU without AVX.
//
// The two sets sum in different orders and AVX2 fuses multiply-adds, so their renders differ in
// the last bits. Each one on its own is deterministic and independent of how blocks are split.

namespace forte::detail
{
struct Kernels
{
    const char* name;  // "SSE2", "AVX2"

    /** Free resonators, mode by mode (each held in registers across the samples): advances the
        modes [0, count) numSamples samples, each by its complex multiplier m, and adds
        Σ w·Im(state) for each sample to out. count is a multiple of kernelModeAlignment. */
    void (*freeModes) (float* re, float* im, const float* mr, const float* mi, const float* w,
                       int count, float* out, int numSamples) noexcept;

    /** One sample of the same, sample-major (for modes something reads every sample): advances
        modes [0, count) once and returns Σ w·Im(state). */
    float (*stepModes) (float* re, float* im, const float* mr, const float* mi, const float* w, int count) noexcept;

    /** One hammer substep: state = state·m + drive·impulse, for modes [0, count). */
    void (*driveModes) (float* re, float* im, const float* mr, const float* mi, const float* dr, const float* di,
                        float impulse, int count) noexcept;

    /** Σ (wr·im + wi·re) over [0, count): the strings' displacement at the strike point. */
    float (*viewDot) (const float* wr, const float* wi, const float* re, const float* im, int count) noexcept;

    /** Σ a·b over [0, count). */
    float (*dot) (const float* a, const float* b, int count) noexcept;

    /** Complex multiply-accumulate over [0, count): acc += x·h, for one input against a left and
        a right filter (the soundboard's partitioned convolution). */
    void (*complexMac2) (float* lRe, float* lIm, float* rRe, float* rIm,
                         const float* xr, const float* xi,
                         const float* hlr, const float* hli, const float* hrr, const float* hri, int count) noexcept;
};

/** freeModes() takes whole groups of this many modes; per-mode arrays are padded to it. */
inline constexpr int kernelModeAlignment = 8;

extern const Kernels sse2Kernels;
extern const Kernels avx2Kernels;

/** True when the CPU and the OS support AVX2 and FMA. */
[[nodiscard]] bool cpuSupportsAvx2() noexcept;

/** AVX2 when cpuSupportsAvx2(), otherwise SSE2. */
[[nodiscard]] const Kernels& bestKernels() noexcept;
} // namespace forte::detail
