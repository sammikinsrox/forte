// =============================================================================
// PROTOTYPE -- THROWAWAY CODE. Not part of Forte. Do not ship, do not build on.
//
// Forte CPU micro-benchmark (ticket 0011: Modal resonator CPU benchmark).
//
// Measures, single-threaded, at 48 kHz in 128-sample blocks:
//   a. Modal string bank for an 88-note sustain glissando (~10,000 two-pole
//      resonators, struct-of-arrays, SIMD), with a power-law felt hammer that
//      drives each struck note for a fixed 4 ms contact window.
//   b. ~1,000 sympathetic resonators fed by the summed bridge signal.
//   c. A mono-in / stereo-out uniformly partitioned FFT convolution with a
//      20,000-tap IR per channel (128-sample partitions, 256-point FFT).
//
// Build: build.bat (makes an AVX2+FMA build and an SSE2 build with MSVC).
// The SIMD width is chosen at compile time from __AVX2__.
// =============================================================================

#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <random>
#include <vector>

// ---------------------------------------------------------------- constants
static constexpr double kFs = 48000.0;
static constexpr int kBlock = 128;
static constexpr int kNotes = 88;
static constexpr double kBudgetMs = 1000.0 * kBlock / kFs; // 2.6667 ms
static constexpr int kContactSamples = 192;                // 4 ms hammer contact
static constexpr int kIrTaps = 20000;
static constexpr int kFftN = 2 * kBlock; // 256
static constexpr int kBins = kFftN / 2 + 1; // 129
static constexpr int kBinsPad = 136;        // padded to a multiple of 8
static constexpr int kParts = (kIrTaps + kBlock - 1) / kBlock; // 157
static constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- SIMD shim
#if defined(__AVX2__)
typedef __m256 V;
static constexpr int W = 8;
static const char* kIsa = "AVX2+FMA (8 floats/op)";
static inline V vload(const float* p) { return _mm256_load_ps(p); }
static inline void vstore(float* p, V v) { _mm256_store_ps(p, v); }
static inline V vset1(float x) { return _mm256_set1_ps(x); }
static inline V vzero() { return _mm256_setzero_ps(); }
static inline V vadd(V a, V b) { return _mm256_add_ps(a, b); }
static inline V vsub(V a, V b) { return _mm256_sub_ps(a, b); }
static inline V vmul(V a, V b) { return _mm256_mul_ps(a, b); }
static inline V vfma(V a, V b, V c) { return _mm256_fmadd_ps(a, b, c); } // a*b+c
static inline V vfms(V a, V b, V c) { return _mm256_fnmadd_ps(a, b, c); } // c-a*b
static inline float vhsum(V v) {
    __m128 lo = _mm256_castps256_ps128(v), hi = _mm256_extractf128_ps(v, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_add_ps(lo, _mm_movehl_ps(lo, lo));
    lo = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 1));
    return _mm_cvtss_f32(lo);
}
#else
typedef __m128 V;
static constexpr int W = 4;
static const char* kIsa = "SSE2 (4 floats/op, no FMA)";
static inline V vload(const float* p) { return _mm_load_ps(p); }
static inline void vstore(float* p, V v) { _mm_store_ps(p, v); }
static inline V vset1(float x) { return _mm_set1_ps(x); }
static inline V vzero() { return _mm_setzero_ps(); }
static inline V vadd(V a, V b) { return _mm_add_ps(a, b); }
static inline V vsub(V a, V b) { return _mm_sub_ps(a, b); }
static inline V vmul(V a, V b) { return _mm_mul_ps(a, b); }
static inline V vfma(V a, V b, V c) { return _mm_add_ps(_mm_mul_ps(a, b), c); }
static inline V vfms(V a, V b, V c) { return _mm_sub_ps(c, _mm_mul_ps(a, b)); }
static inline float vhsum(V v) {
    v = _mm_add_ps(v, _mm_movehl_ps(v, v));
    v = _mm_add_ss(v, _mm_shuffle_ps(v, v, 1));
    return _mm_cvtss_f32(v);
}
#endif

// 32-byte aligned float vector.
struct AVec {
    float* p = nullptr;
    size_t n = 0;
    void alloc(size_t count) {
        n = count;
        p = (float*)_aligned_malloc(std::max<size_t>(count, 8) * sizeof(float), 32);
        std::memset(p, 0, std::max<size_t>(count, 8) * sizeof(float));
    }
    float& operator[](size_t i) { return p[i]; }
};

// ------------------------------------------------------- resonator bank (SoA)
// Each mode: y[n] = a1*y[n-1] + a2*y[n-2] + b*F[n]   (F = input force)
// Output contribution: c*y[n]. Hammer sees displacement sum(phi*y).
struct Bank {
    AVec y1, y2, a1, a2, b, c, phi;
    size_t count = 0; // padded
    void alloc(size_t n) {
        count = n;
        for (AVec* v : {&y1, &y2, &a1, &a2, &b, &c, &phi}) v->alloc(n);
    }
    // Impulse-invariant resonator for a mode of frequency f (Hz), decay rate
    // sigma (1/s), modal mass m (kg), input weight in, output weight out.
    void setMode(size_t i, double f, double sigma, double m, double in, double out) {
        const double T = 1.0 / kFs, w = 2 * kPi * f, r = std::exp(-sigma * T);
        a1[i] = (float)(2 * r * std::cos(w * T));
        a2[i] = (float)(-r * r);
        b[i] = (float)(in * T * r * std::sin(w * T) / (m * w));
        c[i] = (float)out;
        phi[i] = (float)in;
    }
};

// Free decay (zero input), accumulate c*y into acc[t]. Range [s,e), multiple of W.
// Processes 4 vectors at a time to hide the recursion latency.
static void bankFree(Bank& B, size_t s, size_t e, V* acc) {
    size_t i = s;
    for (; i + 4 * W <= e; i += 4 * W) {
        V a1[4], a2[4], c[4], y1[4], y2[4];
        for (int k = 0; k < 4; ++k) {
            a1[k] = vload(&B.a1[i + k * W]); a2[k] = vload(&B.a2[i + k * W]);
            c[k] = vload(&B.c[i + k * W]);
            y1[k] = vload(&B.y1[i + k * W]); y2[k] = vload(&B.y2[i + k * W]);
        }
        for (int t = 0; t < kBlock; ++t) {
            V sum = acc[t];
            for (int k = 0; k < 4; ++k) {
                V y = vfma(a1[k], y1[k], vmul(a2[k], y2[k]));
                y2[k] = y1[k]; y1[k] = y;
                sum = vfma(c[k], y, sum);
            }
            acc[t] = sum;
        }
        for (int k = 0; k < 4; ++k) { vstore(&B.y1[i + k * W], y1[k]); vstore(&B.y2[i + k * W], y2[k]); }
    }
    for (; i < e; i += W) {
        V a1 = vload(&B.a1[i]), a2 = vload(&B.a2[i]), c = vload(&B.c[i]);
        V y1 = vload(&B.y1[i]), y2 = vload(&B.y2[i]);
        for (int t = 0; t < kBlock; ++t) {
            V y = vfma(a1, y1, vmul(a2, y2));
            y2 = y1; y1 = y;
            acc[t] = vfma(c, y, acc[t]);
        }
        vstore(&B.y1[i], y1); vstore(&B.y2[i], y2);
    }
}

// Driven by a known per-sample input x[t] (broadcast to all modes).
static void bankDriven(Bank& B, size_t s, size_t e, const float* x, V* acc) {
    size_t i = s;
    for (; i + 4 * W <= e; i += 4 * W) {
        V a1[4], a2[4], b[4], c[4], y1[4], y2[4];
        for (int k = 0; k < 4; ++k) {
            a1[k] = vload(&B.a1[i + k * W]); a2[k] = vload(&B.a2[i + k * W]);
            b[k] = vload(&B.b[i + k * W]); c[k] = vload(&B.c[i + k * W]);
            y1[k] = vload(&B.y1[i + k * W]); y2[k] = vload(&B.y2[i + k * W]);
        }
        for (int t = 0; t < kBlock; ++t) {
            V xt = vset1(x[t]), sum = acc[t];
            for (int k = 0; k < 4; ++k) {
                V y = vfma(a1[k], y1[k], vfma(a2[k], y2[k], vmul(b[k], xt)));
                y2[k] = y1[k]; y1[k] = y;
                sum = vfma(c[k], y, sum);
            }
            acc[t] = sum;
        }
        for (int k = 0; k < 4; ++k) { vstore(&B.y1[i + k * W], y1[k]); vstore(&B.y2[i + k * W], y2[k]); }
    }
    for (; i < e; i += W) {
        V a1 = vload(&B.a1[i]), a2 = vload(&B.a2[i]), b = vload(&B.b[i]), c = vload(&B.c[i]);
        V y1 = vload(&B.y1[i]), y2 = vload(&B.y2[i]);
        for (int t = 0; t < kBlock; ++t) {
            V y = vfma(a1, y1, vfma(a2, y2, vmul(b, vset1(x[t]))));
            y2 = y1; y1 = y;
            acc[t] = vfma(c, y, acc[t]);
        }
        vstore(&B.y1[i], y1); vstore(&B.y2[i], y2);
    }
}

// ------------------------------------------------------------------ hammer
struct Note {
    size_t start = 0, end = 0;  // mode range in the string bank (padded)
    int realModes = 0;
    double mass = 0.01, K = 4e8, p = 2.5; // felt: F = K * compression^p
    double xh = 0, vh = 0;      // hammer position / velocity
    int contactLeft = 0;        // samples of contact window remaining
};

static double gMaxForce = 0; // diagnostics

// Sample-major processing of one note while the hammer is in contact.
// Per sample: string displacement at the strike point (dot product over modes,
// one-sample delay), power-law felt force, hammer update, then all modes.
static void noteContact(Bank& B, Note& n, V* acc, int t0, int t1) {
    const double T = 1.0 / kFs;
    for (int t = t0; t < t1; ++t) {
        V dot = vzero();
        for (size_t i = n.start; i < n.end; i += W) dot = vfma(vload(&B.phi[i]), vload(&B.y1[i]), dot);
        const double xs = vhsum(dot);
        const double comp = n.xh - xs;
        const double F = comp > 0 ? std::min(n.K * std::pow(comp, n.p), 2000.0) : 0.0; // clamp: safety net
        gMaxForce = std::max(gMaxForce, F);
        n.vh -= F / n.mass * T;
        n.xh += n.vh * T;
        const V f = vset1((float)F);
        V sum = acc[t];
        for (size_t i = n.start; i < n.end; i += W) {
            V y1 = vload(&B.y1[i]), y2 = vload(&B.y2[i]);
            V y = vfma(vload(&B.a1[i]), y1, vfma(vload(&B.a2[i]), y2, vmul(vload(&B.b[i]), f)));
            vstore(&B.y2[i], y1); vstore(&B.y1[i], y);
            sum = vfma(vload(&B.c[i]), y, sum);
        }
        acc[t] = sum;
    }
}

// ------------------------------------------------------- radix-2 FFT (N=256)
struct Fft {
    int rev[kFftN];
    float twr[kFftN / 2], twi[kFftN / 2];
    Fft() {
        int bits = 0; while ((1 << bits) < kFftN) ++bits;
        for (int i = 0; i < kFftN; ++i) {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        for (int k = 0; k < kFftN / 2; ++k) {
            twr[k] = (float)std::cos(-2 * kPi * k / kFftN);
            twi[k] = (float)std::sin(-2 * kPi * k / kFftN);
        }
    }
    // In-place forward complex FFT on split arrays; sign=-1 inverse (unscaled).
    void run(float* re, float* im, bool inverse) const {
        for (int i = 0; i < kFftN; ++i) if (i < rev[i]) { std::swap(re[i], re[rev[i]]); std::swap(im[i], im[rev[i]]); }
        for (int len = 2; len <= kFftN; len <<= 1) {
            const int half = len >> 1, step = kFftN / len;
            for (int s = 0; s < kFftN; s += len)
                for (int k = 0; k < half; ++k) {
                    const float wr = twr[k * step], wi = inverse ? -twi[k * step] : twi[k * step];
                    const int a = s + k, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                }
        }
    }
};

// ------------------------------------- uniformly partitioned convolution
// Overlap-save, mono input, two output channels (stereo IR).
struct Conv {
    Fft fft;
    int parts = 0;
    std::vector<AVec> Hr[2], Hi[2]; // [ch][part] spectra, kBinsPad each
    std::vector<AVec> Xr, Xi;       // frequency-domain delay line [part]
    int head = 0;
    float inBuf[kFftN] = {};
    AVec accR, accI;

    void init(const std::vector<float> ir[2]) {
        const int taps = (int)ir[0].size();
        parts = (taps + kBlock - 1) / kBlock;
        for (int ch = 0; ch < 2; ++ch) {
            Hr[ch].resize(parts); Hi[ch].resize(parts);
            for (int p = 0; p < parts; ++p) {
                Hr[ch][p].alloc(kBinsPad); Hi[ch][p].alloc(kBinsPad);
                float re[kFftN] = {}, im[kFftN] = {};
                for (int j = 0; j < kBlock; ++j) { int k = p * kBlock + j; re[j] = k < taps ? ir[ch][k] : 0.f; }
                fft.run(re, im, false);
                for (int k = 0; k < kBins; ++k) { Hr[ch][p][k] = re[k]; Hi[ch][p][k] = im[k]; }
            }
        }
        Xr.resize(parts); Xi.resize(parts);
        for (int p = 0; p < parts; ++p) { Xr[p].alloc(kBinsPad); Xi[p].alloc(kBinsPad); }
        accR.alloc(kBinsPad); accI.alloc(kBinsPad);
        head = 0; std::memset(inBuf, 0, sizeof(inBuf));
    }

    void process(const float* in, float* outL, float* outR) {
        // slide input: [previous block | current block]
        std::memmove(inBuf, inBuf + kBlock, kBlock * sizeof(float));
        std::memcpy(inBuf + kBlock, in, kBlock * sizeof(float));
        float re[kFftN], im[kFftN];
        std::memcpy(re, inBuf, sizeof(re)); std::memset(im, 0, sizeof(im));
        fft.run(re, im, false);
        head = (head + parts - 1) % parts;
        for (int k = 0; k < kBins; ++k) { Xr[head][k] = re[k]; Xi[head][k] = im[k]; }
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < kBinsPad; k += W) { vstore(&accR[k], vzero()); vstore(&accI[k], vzero()); }
            for (int p = 0; p < parts; ++p) {
                const int slot = (head + p) % parts;
                const float *xr = Xr[slot].p, *xi = Xi[slot].p, *hr = Hr[ch][p].p, *hi = Hi[ch][p].p;
                for (int k = 0; k < kBinsPad; k += W) {
                    V a = vload(xr + k), b = vload(xi + k), c = vload(hr + k), d = vload(hi + k);
                    V r = vload(&accR[k]), i = vload(&accI[k]);
                    r = vfma(a, c, r); r = vfms(b, d, r);   // += ac - bd
                    i = vfma(a, d, i); i = vfma(b, c, i);   // += ad + bc
                    vstore(&accR[k], r); vstore(&accI[k], i);
                }
            }
            // rebuild Hermitian spectrum and inverse FFT
            re[0] = accR[0]; im[0] = 0;
            for (int k = 1; k < kFftN / 2; ++k) {
                re[k] = accR[k]; im[k] = accI[k];
                re[kFftN - k] = accR[k]; im[kFftN - k] = -accI[k];
            }
            re[kFftN / 2] = accR[kFftN / 2]; im[kFftN / 2] = 0;
            fft.run(re, im, true);
            float* out = ch == 0 ? outL : outR;
            const float scale = 1.0f / kFftN;
            for (int j = 0; j < kBlock; ++j) out[j] = re[kBlock + j] * scale;
        }
    }
};

// Correctness check against direct convolution (double precision).
static double verifyConv(int taps, int blocks) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<float> ir[2];
    for (int ch = 0; ch < 2; ++ch) { ir[ch].resize(taps); for (auto& v : ir[ch]) v = u(rng) * 0.01f; }
    std::vector<float> x(blocks * kBlock);
    for (auto& v : x) v = u(rng);
    Conv* c = new Conv(); c->init(ir);
    std::vector<float> yl(x.size()), yr(x.size());
    for (int b = 0; b < blocks; ++b) c->process(&x[b * kBlock], &yl[b * kBlock], &yr[b * kBlock]);
    double maxErr = 0, maxRef = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t n = 0; n < x.size(); ++n) {
            double ref = 0;
            for (int k = 0; k < taps && k <= (int)n; ++k) ref += (double)ir[ch][k] * x[n - k];
            const double got = ch == 0 ? yl[n] : yr[n];
            maxErr = std::max(maxErr, std::fabs(got - ref));
            maxRef = std::max(maxRef, std::fabs(ref));
        }
    delete c;
    return maxErr / maxRef;
}

// ------------------------------------------------------------ piano layout
static double inharmB(int n) { // rough Steinway-D-like curve: ~2e-4 at A0, ~1e-4 low-mid, ~1e-2 at C8
    const double l = n < 26 ? -3.7 - 0.3 * n / 26.0 : -4.0 + 2.0 * (n - 26) / 61.0;
    return std::pow(10.0, l);
}
static int stringsPerNote(int n) { return n < 8 ? 1 : n < 28 ? 2 : 3; }

// Bit-pattern finiteness test (std::isfinite is unreliable under /fp:fast).
static inline bool isFin(float v) { uint32_t u; std::memcpy(&u, &v, 4); return (u & 0x7f800000u) != 0x7f800000u; }

struct Stats { double avg, p99, max; };
static Stats stats(std::vector<double> v) {
    double s = 0; for (double x : v) s += x;
    std::sort(v.begin(), v.end());
    return { s / v.size(), v[(size_t)(0.99 * (v.size() - 1))], v.back() };
}

int main(int argc, char** argv) {
    double seconds = 30.0;
    bool chordStress = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        if (!std::strcmp(argv[i], "--chords")) chordStress = true;
    }
    // Flush-to-zero + denormals-are-zero.
    _mm_setcsr(_mm_getcsr() | 0x8040);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);

    std::printf("Forte CPU benchmark (PROTOTYPE)  ISA: %s\n", kIsa);

    // --- convolution correctness
    std::printf("Convolution check vs direct FIR: 1000 taps rel.err %.2e, 20000 taps rel.err %.2e\n",
                verifyConv(1000, 20), verifyConv(kIrTaps, 200));

    // --- build string bank: per note transverse + unison-secondary + longitudinal modes
    const int kCap = 300;              // per-note transverse cap
    const double kFmax = 20000.0;      // transverse modes below 20 kHz
    const int kUnisonPartials = 32;    // lowest partials get extra coupled modes
    const int kLongModes = 10, kLongNotes = 30;
    std::vector<Note> notes(kNotes);
    struct ModeDef { double f, sigma, m, in, out; };
    std::vector<std::vector<ModeDef>> defs(kNotes);
    size_t realTotal = 0, padTotal = 0, transTotal = 0, unisonTotal = 0, longTotal = 0;
    for (int n = 0; n < kNotes; ++n) {
        const double f0 = 27.5 * std::pow(2.0, n / 12.0), B = inharmB(n);
        const double t60_0 = 20.0 * std::pow(0.2 / 20.0 * 10, n / 87.0); // 20 s bass -> 2 s top
        const double sigma0 = 6.91 / t60_0;
        const double mStr = 0.03 * std::pow(0.1, n / 87.0); // string mass (kg)
        const double x0 = 1.0 / 8.0; // strike point
        std::vector<ModeDef>& d = defs[n];
        int trans = 0;
        for (int k = 1; k <= kCap; ++k) {
            const double f = k * f0 * std::sqrt(1 + B * k * k);
            if (f > kFmax) break;
            const double sigma = sigma0 * (1 + 3e-8 * f * f);
            d.push_back({ f, sigma, mStr / 2, std::sin(k * kPi * x0), 1.0 / k });
            ++trans;
        }
        if (trans == 0) { d.push_back({ std::min(f0, 20000.0), sigma0, mStr / 2, 0.7, 1.0 }); trans = 1; }
        int uni = 0;
        for (int s = 1; s < stringsPerNote(n); ++s)
            for (int k = 0; k < std::min(kUnisonPartials, trans); ++k) {
                ModeDef m = d[k]; m.f *= 1.0 + 0.0004 * s; m.sigma *= 0.3; m.out *= 0.5;
                d.push_back(m); ++uni;
            }
        int lng = 0;
        if (n < kLongNotes)
            for (int k = 1; k <= kLongModes; ++k) {
                d.push_back({ std::min(k * 15.0 * f0 + 600.0, 20000.0), sigma0 * 2, mStr / 2, 0.05, 0.1 });
                ++lng;
            }
        notes[n].realModes = (int)d.size();
        realTotal += d.size(); transTotal += trans; unisonTotal += uni; longTotal += lng;
        padTotal += (d.size() + W - 1) / W * W;
        notes[n].mass = 0.0115 - 0.0055 * n / 87.0; // hammer 11.5 g -> 6 g
        notes[n].K = 1e9 * std::pow(10.0, 0.7 * n / 87.0); // N/m^p
        notes[n].p = 2.5;
    }
    Bank strings; strings.alloc(padTotal);
    {
        size_t pos = 0;
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> u(-1e-4f, 1e-4f);
        for (int n = 0; n < kNotes; ++n) {
            notes[n].start = pos;
            for (const ModeDef& m : defs[n]) {
                strings.setMode(pos, m.f, m.sigma, m.m, m.in, m.out);
                strings.y1[pos] = u(rng); strings.y2[pos] = u(rng); // already sounding
                ++pos;
            }
            pos = (pos + W - 1) / W * W; // padding modes stay all-zero
            notes[n].end = pos;
        }
    }
    std::printf("String bank: %zu resonators (%zu transverse, %zu unison-coupled, %zu longitudinal), %zu incl. SIMD padding\n",
                realTotal, transTotal, unisonTotal, longTotal, padTotal);
    std::printf("  modes/note: A0 %d, A1 %d, A2 %d, A3 %d, A4 %d, A5 %d, A6 %d, A7 %d, C8 %d\n",
                notes[0].realModes, notes[12].realModes, notes[24].realModes, notes[36].realModes,
                notes[48].realModes, notes[60].realModes, notes[72].realModes, notes[84].realModes, notes[87].realModes);

    // --- sympathetic bank: 12 low partials per key
    const int kSympPer = 12;
    Bank symp; symp.alloc((size_t)kNotes * kSympPer); // 1056, multiple of 8
    for (int n = 0; n < kNotes; ++n) {
        const double f0 = 27.5 * std::pow(2.0, n / 12.0), B = inharmB(n);
        for (int k = 1; k <= kSympPer; ++k) {
            const double f = std::min(k * f0 * std::sqrt(1 + B * k * k), 20000.0);
            symp.setMode(n * kSympPer + k - 1, f, 0.5, 1.0, 1e-3, 1.0 / k);
        }
    }
    std::printf("Sympathetic bank: %zu resonators\n", symp.count);

    // --- soundboard IR: 20,000 taps of decaying noise per channel
    std::vector<float> ir[2];
    {
        std::mt19937 rng(42);
        std::normal_distribution<float> g(0, 1);
        for (int ch = 0; ch < 2; ++ch) {
            ir[ch].resize(kIrTaps);
            for (int k = 0; k < kIrTaps; ++k) ir[ch][k] = g(rng) * 0.02f * (float)std::exp(-k / 3000.0);
        }
    }
    Conv* conv = new Conv(); conv->init(ir);
    std::printf("Convolution: %d taps/ch, %d partitions of %d, FFT %d, mono in / stereo out\n",
                kIrTaps, conv->parts, kBlock, kFftN);

    // --- run
    alignas(32) V acc[kBlock];
    alignas(32) float bridge[kBlock], sympOut[kBlock], bus[kBlock], outL[kBlock], outR[kBlock];
    const int warmBlocks = 400;
    const int totalBlocks = warmBlocks + (int)(seconds * kFs / kBlock);
    const int glissStep = 960; // a new note every 20 ms: ~1.8 s per sweep
    std::vector<double> tS, tY, tC, tT;
    tS.reserve(totalBlocks); tY.reserve(totalBlocks); tC.reserve(totalBlocks); tT.reserve(totalBlocks);
    long long sampleClock = 0;
    int glissIdx = 0, glissDir = 1;
    double peak = 0;
    bool finite = true;
    int strikes = 0, maxConcurrentContacts = 0;
    using clk = std::chrono::steady_clock;

    for (int blk = 0; blk < totalBlocks; ++blk) {
        // schedule onsets (sample-accurate start inside block)
        int onsetAt[kNotes];
        for (int n = 0; n < kNotes; ++n) onsetAt[n] = -1;
        for (int t = 0; t < kBlock; ++t) {
            const long long s = sampleClock + t;
            if (s % glissStep == 0) {
                onsetAt[glissIdx] = t;
                glissIdx += glissDir;
                if (glissIdx == kNotes) { glissIdx = kNotes - 2; glissDir = -1; }
                if (glissIdx < 0) { glissIdx = 1; glissDir = 1; }
            }
            if (chordStress && s % 48000 == 0) // 10-note chord once a second
                for (int k = 0; k < 10; ++k) onsetAt[(k * 9 + (int)(s / 48000) * 5) % kNotes] = t;
        }

        auto t0 = clk::now();
        // (a) string bank + hammers
        for (int t = 0; t < kBlock; ++t) acc[t] = vzero();
        int contacts = 0;
        for (int n = 0; n < kNotes; ++n) {
            Note& nt = notes[n];
            if (onsetAt[n] >= 0) {
                // hammer arrives touching the string at 3 m/s
                V dot = vzero();
                for (size_t i = nt.start; i < nt.end; i += W) dot = vfma(vload(&strings.phi[i]), vload(&strings.y1[i]), dot);
                nt.xh = vhsum(dot); nt.vh = 3.0;
                ++strikes;
                // Simplification: the hammer starts at the block start; the onset
                // offset only shortens the remaining contact window.
                nt.contactLeft = kContactSamples;
                noteContact(strings, nt, acc, 0, kBlock);
                nt.contactLeft -= (kBlock - onsetAt[n]);
                ++contacts;
            } else if (nt.contactLeft > 0) {
                noteContact(strings, nt, acc, 0, kBlock);
                nt.contactLeft -= kBlock;
                ++contacts;
            } else {
                bankFree(strings, nt.start, nt.end, acc);
            }
        }
        maxConcurrentContacts = std::max(maxConcurrentContacts, contacts);
        for (int t = 0; t < kBlock; ++t) bridge[t] = vhsum(acc[t]);
        auto t1 = clk::now();
        // (b) sympathetic bank driven by the bridge signal
        for (int t = 0; t < kBlock; ++t) acc[t] = vzero();
        bankDriven(symp, 0, symp.count, bridge, acc);
        for (int t = 0; t < kBlock; ++t) { sympOut[t] = vhsum(acc[t]); bus[t] = bridge[t] + sympOut[t]; }
        auto t2 = clk::now();
        // (c) soundboard convolution
        conv->process(bus, outL, outR);
        auto t3 = clk::now();

        for (int t = 0; t < kBlock; ++t) {
            if (!isFin(outL[t]) || !isFin(outR[t]) || !isFin(bridge[t])) {
                if (finite) {
                    std::printf("non-finite at block %d sample %d: bridge %g symp %g L %g\n", blk, t, bridge[t], sympOut[t], outL[t]);
                    for (int u = 0; u < kBlock; ++u) if (!isFin(bus[u]) || !isFin(sympOut[u])) std::printf("  bus[%d]=%g symp=%g bridge=%g\n", u, bus[u], sympOut[u], bridge[u]);
                }
                finite = false;
            }
            peak = std::max(peak, (double)std::fabs(outL[t]));
        }
        if (blk >= warmBlocks) {
            auto ms = [](clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            tS.push_back(ms(t0, t1)); tY.push_back(ms(t1, t2)); tC.push_back(ms(t2, t3)); tT.push_back(ms(t0, t3));
        }
        sampleClock += kBlock;
    }

    // check for denormals left in state
    size_t denorm = 0;
    for (size_t i = 0; i < strings.count; ++i) {
        const float v = strings.y1[i];
        if (v != 0 && std::fabs(v) < 1.17549435e-38f) ++denorm;
        if (!isFin(v)) finite = false;
    }

    std::printf("\nSimulated %.1f s (%zu timed blocks, %d warm-up), %d hammer strikes, max %d notes in contact per block%s\n",
                seconds, tT.size(), warmBlocks, strikes, maxConcurrentContacts, chordStress ? " [10-note chord every 1 s]" : "");
    std::printf("Outputs finite: %s, peak |L| = %.3g, peak hammer force %.1f N (clamp 2000 N), denormal states: %zu\n\n",
                finite ? "yes" : "NO", peak, gMaxForce, denorm);
    std::printf("%-34s %10s %10s %10s %10s %10s\n", "Part", "avg ms", "p99 ms", "max ms", "avg %core", "p99 %core");
    auto row = [](const char* name, const std::vector<double>& v) {
        Stats s = stats(v);
        std::printf("%-34s %10.4f %10.4f %10.4f %9.2f%% %9.2f%%\n", name, s.avg, s.p99, s.max,
                    100 * s.avg / kBudgetMs, 100 * s.p99 / kBudgetMs);
    };
    row("a. strings+hammers", tS);
    row("b. sympathetic", tY);
    row("c. stereo convolution", tC);
    row("total", tT);
    const double nsPerRes = stats(tS).avg * 1e6 / ((double)padTotal * kBlock);
    std::printf("\nString bank: %.3f ns per resonator-sample (padded count)\n", nsPerRes);
    std::printf("Budget per block: %.4f ms (100%% of one core); Forte target <= 25%%\n", kBudgetMs);
    return finite ? 0 : 1;
}
