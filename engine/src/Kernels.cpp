#include "Kernels.h"

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <cpuid.h>
#endif

#include <cstdint>

namespace forte::detail
{
namespace
{
/** CPUID leaf, subleaf 0: { EAX, EBX, ECX, EDX }. */
void cpuid (unsigned leaf, unsigned (&info)[4]) noexcept
{
#if defined(_MSC_VER)
    int regs[4] {};
    __cpuidex (regs, static_cast<int> (leaf), 0);
    for (int i = 0; i < 4; ++i)
        info[i] = static_cast<unsigned> (regs[i]);
#else
    __cpuid_count (leaf, 0, info[0], info[1], info[2], info[3]);
#endif
}

/** XCR0, the register-state mask the OS saves. Only when CPUID says OSXSAVE. */
std::uint64_t xcr0() noexcept
{
#if defined(_MSC_VER)
    return _xgetbv (0);
#else
    // xgetbv by its encoding: no -mxsave needed for one instruction.
    unsigned low = 0, high = 0;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(low), "=d"(high) : "c"(0));
    return (static_cast<std::uint64_t> (high) << 32) | low;
#endif
}

bool detectAvx2() noexcept
{
    unsigned info[4] {};
    cpuid (0, info);
    if (info[0] < 7)
        return false;

    cpuid (1, info);
    const bool fma = (info[2] & (1u << 12)) != 0;
    const bool osxsave = (info[2] & (1u << 27)) != 0;
    const bool avx = (info[2] & (1u << 28)) != 0;
    if (! (fma && osxsave && avx))
        return false;

    // The OS must save the YMM registers across context switches: XCR0 bits 1 (SSE) and 2 (AVX).
    if ((xcr0() & 0x6) != 0x6)
        return false;

    cpuid (7, info);
    return (info[1] & (1u << 5)) != 0;  // EBX bit 5: AVX2
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
