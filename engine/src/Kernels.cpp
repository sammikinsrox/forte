#include "Kernels.h"

#include <intrin.h>

namespace forte::detail
{
namespace
{
bool detectAvx2() noexcept
{
    int info[4] {};
    __cpuid (info, 0);
    if (info[0] < 7)
        return false;

    __cpuid (info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0;
    const bool osxsave = (info[2] & (1 << 27)) != 0;
    const bool avx = (info[2] & (1 << 28)) != 0;
    if (! (fma && osxsave && avx))
        return false;

    // The OS must save the YMM registers across context switches: XCR0 bits 1 (SSE) and 2 (AVX).
    if ((_xgetbv (0) & 0x6) != 0x6)
        return false;

    __cpuidex (info, 7, 0);
    return (info[1] & (1 << 5)) != 0;  // EBX bit 5: AVX2
}
} // namespace

bool cpuSupportsAvx2() noexcept
{
    static const bool supported = detectAvx2();
    return supported;
}

const Kernels& bestKernels() noexcept
{
    return cpuSupportsAvx2() ? avx2Kernels : sse2Kernels;
}
} // namespace forte::detail
