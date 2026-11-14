#pragma once

#include <xmmintrin.h>

namespace forte::detail
{
/** Sets flush-to-zero and denormals-are-zero for the current thread, and restores the caller's
    MXCSR on exit. The engine sets these itself rather than trusting the host (SPEC §9.1).
*/
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
        : saved (_mm_getcsr())
    {
        constexpr unsigned int flushToZero = 0x8000;       // MXCSR.FTZ
        constexpr unsigned int denormalsAreZero = 0x0040;  // MXCSR.DAZ
        _mm_setcsr (saved | flushToZero | denormalsAreZero);
    }

    ~ScopedFlushDenormals() noexcept { _mm_setcsr (saved); }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
    unsigned int saved;
};
} // namespace forte::detail
