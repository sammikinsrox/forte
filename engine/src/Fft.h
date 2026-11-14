#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

namespace forte::detail
{
/** An in-place complex FFT of a power-of-two size, on split real and imaginary arrays:
    iterative radix-2, decimation in time, with the twiddles and bit-reversal order precomputed.
    The forward transform is X[k] = Σ x[n]·e^{−2πikn/N}; the inverse uses e^{+2πikn/N} and is
    unscaled (it returns N·x).

    prepare() allocates. transform() is realtime-safe.
*/
template <typename T>
class ComplexFft
{
public:
    void prepare (std::size_t newSize)
    {
        assert (newSize >= 1 && (newSize & (newSize - 1)) == 0);
        size = newSize;

        // Twiddles e^{−2πik/N} for k < N/2. A stage with half-length h reads every (N/2h)-th.
        cosTable.resize (size / 2);
        sinTable.resize (size / 2);
        for (std::size_t k = 0; k < size / 2; ++k)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (size);
            cosTable[k] = static_cast<T> (std::cos (angle));
            sinTable[k] = static_cast<T> (std::sin (angle));
        }

        swaps.clear();
        for (std::size_t i = 0, j = 0; i < size; ++i)
        {
            if (i < j)
                swaps.emplace_back (i, j);

            auto bit = size >> 1;
            for (; bit > 0 && (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j |= bit;
        }
    }

    [[nodiscard]] std::size_t getSize() const noexcept { return size; }

    void forward (T* re, T* im) const noexcept { transform (re, im, T (1)); }
    void inverse (T* re, T* im) const noexcept { transform (re, im, T (-1)); }

private:
    void transform (T* re, T* im, T sign) const noexcept
    {
        for (const auto& [i, j] : swaps)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }

        for (std::size_t half = 1; half < size; half <<= 1)
        {
            const auto stride = size / (2 * half);

            for (std::size_t start = 0; start < size; start += 2 * half)
            {
                for (std::size_t k = 0; k < half; ++k)
                {
                    const T wr = cosTable[k * stride];
                    const T wi = sign * sinTable[k * stride];
                    const auto a = start + k;
                    const auto b = a + half;
                    const T tr = re[b] * wr - im[b] * wi;
                    const T ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

    std::size_t size = 0;
    std::vector<T> cosTable, sinTable;
    std::vector<std::pair<std::size_t, std::size_t>> swaps;
};

/** The FFT of N real samples (N a power of two, at least 4), through a complex FFT of N/2.

    Spectra are N/2 complex bins in split form: re[k], im[k] for k < N/2 hold bin k, except that
    bin 0 is real, so im[0] holds the real Nyquist bin (bin N/2). inverse() is unscaled: it
    returns N·x.

    prepare() allocates. forward() and inverse() are realtime-safe.
*/
template <typename T>
class RealFft
{
public:
    void prepare (std::size_t newSize)
    {
        assert (newSize >= 4 && (newSize & (newSize - 1)) == 0);
        size = newSize;
        half.prepare (size / 2);
        workRe.assign (size / 2, T (0));
        workIm.assign (size / 2, T (0));

        cosTable.resize (size / 2);
        sinTable.resize (size / 2);
        for (std::size_t k = 0; k < size / 2; ++k)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (size);
            cosTable[k] = static_cast<T> (std::cos (angle));
            sinTable[k] = static_cast<T> (std::sin (angle));
        }
    }

    [[nodiscard]] std::size_t getSize() const noexcept { return size; }

    /** in: N samples. re, im: N/2 each, in the packed layout above. */
    void forward (const T* in, T* re, T* im) noexcept
    {
        const auto m = size / 2;
        T* zr = workRe.data();
        T* zi = workIm.data();

        for (std::size_t n = 0; n < m; ++n)
        {
            zr[n] = in[2 * n];
            zi[n] = in[2 * n + 1];
        }

        half.forward (zr, zi);

        // X[k] = E[k] + W^k·O[k], with E = (Z[k] + Z*[m−k]) / 2 and O = (Z[k] − Z*[m−k]) / 2i.
        re[0] = zr[0] + zi[0];
        im[0] = zr[0] - zi[0];

        for (std::size_t k = 1; k < m; ++k)
        {
            const T ar = zr[k], ai = zi[k];
            const T br = zr[m - k], bi = -zi[m - k];      // Z*[m−k]
            const T er = T (0.5) * (ar + br), ei = T (0.5) * (ai + bi);
            const T or_ = T (0.5) * (ai - bi), oi = T (-0.5) * (ar - br);
            const T wr = cosTable[k], wi = sinTable[k];
            re[k] = er + (or_ * wr - oi * wi);
            im[k] = ei + (or_ * wi + oi * wr);
        }
    }

    /** re, im: N/2 each, in the packed layout. out: N samples, N times the inverse transform. */
    void inverse (const T* re, const T* im, T* out) noexcept
    {
        const auto m = size / 2;
        T* zr = workRe.data();
        T* zi = workIm.data();

        // E = X[k] + X*[m−k], O = (X[k] − X*[m−k])·W^{−k}, Z = E + i·O (each twice the true value,
        // which with the unscaled inverse of size m makes the N·x).
        zr[0] = re[0] + im[0];
        zi[0] = re[0] - im[0];

        for (std::size_t k = 1; k < m; ++k)
        {
            const T ar = re[k], ai = im[k];
            const T br = re[m - k], bi = -im[m - k];      // X*[m−k]
            const T er = ar + br, ei = ai + bi;
            const T dr = ar - br, di = ai - bi;
            const T wr = cosTable[k], wi = -sinTable[k];  // W^{−k}
            const T or_ = dr * wr - di * wi, oi = dr * wi + di * wr;
            zr[k] = er - oi;
            zi[k] = ei + or_;
        }

        half.inverse (zr, zi);

        for (std::size_t n = 0; n < m; ++n)
        {
            out[2 * n] = zr[n];
            out[2 * n + 1] = zi[n];
        }
    }

private:
    std::size_t size = 0;
    ComplexFft<T> half;
    std::vector<T> workRe, workIm;
    std::vector<T> cosTable, sinTable;
};
} // namespace forte::detail
