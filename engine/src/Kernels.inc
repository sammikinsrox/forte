// The engine's hot loops (Kernels.h), for one instruction set. KernelsSse2.cpp and
// KernelsAvx2.cpp include this with FORTE_KERNELS_AVX2 set to 0 or 1, and the latter is
// compiled with /arch:AVX2.
//
// Includes nothing but <immintrin.h> and Kernels.h, and everything here has internal linkage:
// see Kernels.h for why. No <algorithm>, <cmath> or the like.

#include "Kernels.h"

#include <immintrin.h>

#ifndef FORTE_KERNELS_AVX2
#error "Define FORTE_KERNELS_AVX2 to 0 or 1 before including Kernels.inc"
#endif

namespace forte::detail
{
namespace
{
#if FORTE_KERNELS_AVX2
using V = __m256;
constexpr int W = 8;

V vzero() noexcept { return _mm256_setzero_ps(); }
V vload (const float* p) noexcept { return _mm256_loadu_ps (p); }
void vstore (float* p, V v) noexcept { _mm256_storeu_ps (p, v); }
V vmul (V a, V b) noexcept { return _mm256_mul_ps (a, b); }
V vmuladd (V a, V b, V c) noexcept { return _mm256_fmadd_ps (a, b, c); }  // a·b + c
V vmulsub (V a, V b, V c) noexcept { return _mm256_fmsub_ps (a, b, c); }  // a·b − c

float hsum (V v) noexcept
{
    __m128 s = _mm_add_ps (_mm256_castps256_ps128 (v), _mm256_extractf128_ps (v, 1));
    s = _mm_add_ps (s, _mm_movehl_ps (s, s));
    s = _mm_add_ss (s, _mm_shuffle_ps (s, s, 1));
    return _mm_cvtss_f32 (s);
}
#else
using V = __m128;
constexpr int W = 4;

V vzero() noexcept { return _mm_setzero_ps(); }
V vload (const float* p) noexcept { return _mm_loadu_ps (p); }
void vstore (float* p, V v) noexcept { _mm_storeu_ps (p, v); }
V vmul (V a, V b) noexcept { return _mm_mul_ps (a, b); }
V vmuladd (V a, V b, V c) noexcept { return _mm_add_ps (_mm_mul_ps (a, b), c); }
V vmulsub (V a, V b, V c) noexcept { return _mm_sub_ps (_mm_mul_ps (a, b), c); }

float hsum (V s) noexcept
{
    s = _mm_add_ps (s, _mm_movehl_ps (s, s));
    s = _mm_add_ss (s, _mm_shuffle_ps (s, s, 1));
    return _mm_cvtss_f32 (s);
}
#endif

static_assert (kernelModeAlignment % W == 0, "a mode group is whole vectors");

/** Groups of G vectors share each sample's pass, so G independent recursions hide each other's
    latency. Four groups is 20 vectors of state and coefficients, a few more than the 16
    registers: the coefficients spill to L1, which is cheap next to the recursion. */
constexpr int G = 4;
constexpr int chunk = 64;  // samples per pass over the modes

void freeModes (float* re, float* im, const float* mr, const float* mi, const float* w,
                int count, float* out, int numSamples) noexcept
{
    for (int t0 = 0; t0 < numSamples; t0 += chunk)
    {
        const int n = numSamples - t0 < chunk ? numSamples - t0 : chunk;

        // Each sample's lanes sum here, mode group after mode group in a fixed order, so the
        // result doesn't depend on how the caller splits the samples.
        V acc[chunk];
        for (int t = 0; t < n; ++t)
            acc[t] = vzero();

        int i = 0;
        for (; i + G * W <= count; i += G * W)
        {
            V r[G], m[G], a[G], b[G], c[G];
            for (int k = 0; k < G; ++k)
            {
                r[k] = vload (re + i + k * W);
                m[k] = vload (im + i + k * W);
                a[k] = vload (mr + i + k * W);
                b[k] = vload (mi + i + k * W);
                c[k] = vload (w + i + k * W);
            }

            for (int t = 0; t < n; ++t)
            {
                V sum = acc[t];
                for (int k = 0; k < G; ++k)
                {
                    const V nr = vmulsub (r[k], a[k], vmul (m[k], b[k]));
                    const V nm = vmuladd (r[k], b[k], vmul (m[k], a[k]));
                    r[k] = nr;
                    m[k] = nm;
                    sum = vmuladd (c[k], nm, sum);
                }
                acc[t] = sum;
            }

            for (int k = 0; k < G; ++k)
            {
                vstore (re + i + k * W, r[k]);
                vstore (im + i + k * W, m[k]);
            }
        }

        for (; i < count; i += W)
        {
            V r = vload (re + i), m = vload (im + i);
            const V a = vload (mr + i), b = vload (mi + i), c = vload (w + i);

            for (int t = 0; t < n; ++t)
            {
                const V nr = vmulsub (r, a, vmul (m, b));
                const V nm = vmuladd (r, b, vmul (m, a));
                r = nr;
                m = nm;
                acc[t] = vmuladd (c, nm, acc[t]);
            }

            vstore (re + i, r);
            vstore (im + i, m);
        }

        for (int t = 0; t < n; ++t)
            out[t0 + t] += hsum (acc[t]);
    }
}

// The rest are plain loops: the compiler vectorises them for this file's instruction set.

float stepModes (float* __restrict re, float* __restrict im, const float* __restrict mr, const float* __restrict mi,
                 const float* __restrict w, int count) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float r = re[i] * mr[i] - im[i] * mi[i];
        const float m = re[i] * mi[i] + im[i] * mr[i];
        re[i] = r;
        im[i] = m;
        sum += w[i] * m;
    }
    return sum;
}

void driveModes (float* __restrict re, float* __restrict im, const float* __restrict mr, const float* __restrict mi,
                 const float* __restrict dr, const float* __restrict di, float impulse, int count) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const float r = re[i] * mr[i] - im[i] * mi[i] + dr[i] * impulse;
        const float m = re[i] * mi[i] + im[i] * mr[i] + di[i] * impulse;
        re[i] = r;
        im[i] = m;
    }
}

float viewDot (const float* __restrict wr, const float* __restrict wi, const float* __restrict re,
               const float* __restrict im, int count) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
        sum += wr[i] * im[i] + wi[i] * re[i];
    return sum;
}

float dot (const float* __restrict a, const float* __restrict b, int count) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
        sum += a[i] * b[i];
    return sum;
}

void complexMac2 (float* __restrict lRe, float* __restrict lIm, float* __restrict rRe, float* __restrict rIm,
                  const float* __restrict xr, const float* __restrict xi,
                  const float* __restrict hlr, const float* __restrict hli,
                  const float* __restrict hrr, const float* __restrict hri, int count) noexcept
{
    for (int k = 0; k < count; ++k)
    {
        lRe[k] += xr[k] * hlr[k] - xi[k] * hli[k];
        lIm[k] += xr[k] * hli[k] + xi[k] * hlr[k];
        rRe[k] += xr[k] * hrr[k] - xi[k] * hri[k];
        rIm[k] += xr[k] * hri[k] + xi[k] * hrr[k];
    }
}
} // namespace

#if FORTE_KERNELS_AVX2
extern const Kernels avx2Kernels { "AVX2", freeModes, stepModes, driveModes, viewDot, dot, complexMac2 };
#else
extern const Kernels sse2Kernels { "SSE2", freeModes, stepModes, driveModes, viewDot, dot, complexMac2 };
#endif
} // namespace forte::detail
