// Replaces the global allocation functions for the test executable, so the audio-thread guard
// sees every allocation. Link this file into test executables only, never into the plugin.
//
// On Windows, Debug builds also hook the CRT heap, so a plain malloc() is caught too. Linux has no
// such hook; the engine's sources are the same on both, so the Windows run covers that.

#include "AudioThreadGuard.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
    #include <malloc.h>
#endif

#if defined(_MSC_VER) && defined(_DEBUG)
    #include <crtdbg.h>
#endif

namespace
{
thread_local int audioThreadDepth = 0;
thread_local std::int64_t allocationCount = 0;
thread_local std::int64_t deallocationCount = 0;

// Non-zero while one of our operator new/delete replacements is calling into the CRT, so the
// Debug CRT hook doesn't count the same operation twice.
thread_local int insideReplacement = 0;

void noteAllocation() noexcept
{
    if (audioThreadDepth > 0)
        ++allocationCount;
}

void noteDeallocation() noexcept
{
    if (audioThreadDepth > 0)
        ++deallocationCount;
}

struct ReplacementCall
{
    ReplacementCall() noexcept { ++insideReplacement; }
    ~ReplacementCall() noexcept { --insideReplacement; }
};

void* allocate (std::size_t size) noexcept
{
    noteAllocation();
    const ReplacementCall call;
    return std::malloc (size != 0 ? size : 1);
}

void* allocateAligned (std::size_t size, std::align_val_t alignment) noexcept
{
    noteAllocation();
    const ReplacementCall call;
#if defined(_MSC_VER)
    return _aligned_malloc (size != 0 ? size : 1, static_cast<std::size_t> (alignment));
#else
    // posix_memalign wants at least a pointer's alignment; free() releases it.
    void* p = nullptr;
    const auto align = std::max (static_cast<std::size_t> (alignment), sizeof (void*));
    return posix_memalign (&p, align, size != 0 ? size : 1) == 0 ? p : nullptr;
#endif
}

void release (void* p) noexcept
{
    if (p == nullptr)
        return;

    noteDeallocation();
    const ReplacementCall call;
    std::free (p);
}

void releaseAligned (void* p) noexcept
{
    if (p == nullptr)
        return;

    noteDeallocation();
    const ReplacementCall call;
#if defined(_MSC_VER)
    _aligned_free (p);
#else
    std::free (p);
#endif
}

void* allocateOrThrow (std::size_t size)
{
    if (auto* p = allocate (size))
        return p;

    throw std::bad_alloc();
}

void* allocateAlignedOrThrow (std::size_t size, std::align_val_t alignment)
{
    if (auto* p = allocateAligned (size, alignment))
        return p;

    throw std::bad_alloc();
}

#if defined(_MSC_VER) && defined(_DEBUG)
int __cdecl crtAllocationHook (int allocationType, void*, std::size_t, int, long, const unsigned char*, int)
{
    if (audioThreadDepth > 0 && insideReplacement == 0)
    {
        if (allocationType == _HOOK_FREE)
            ++deallocationCount;
        else
            ++allocationCount;  // _HOOK_ALLOC and _HOOK_REALLOC
    }

    return 1;  // allow the operation
}

const bool crtHookInstalled = []
{
    _CrtSetAllocHook (crtAllocationHook);
    return true;
}();
#endif
} // namespace

namespace forte::test
{
AudioThreadScope::AudioThreadScope() noexcept
    : start { allocationCount, deallocationCount }
{
    ++audioThreadDepth;
}

AudioThreadScope::~AudioThreadScope() noexcept
{
    --audioThreadDepth;
}

HeapUse AudioThreadScope::heapUse() const noexcept
{
    return { allocationCount - start.allocations, deallocationCount - start.deallocations };
}
} // namespace forte::test

// clang-format off
void* operator new   (std::size_t size)                                        { return allocateOrThrow (size); }
void* operator new[] (std::size_t size)                                        { return allocateOrThrow (size); }
void* operator new   (std::size_t size, const std::nothrow_t&) noexcept        { return allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept        { return allocate (size); }
void* operator new   (std::size_t size, std::align_val_t a)                    { return allocateAlignedOrThrow (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a)                    { return allocateAlignedOrThrow (size, a); }
void* operator new   (std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned (size, a); }

void operator delete   (void* p) noexcept                                      { release (p); }
void operator delete[] (void* p) noexcept                                      { release (p); }
void operator delete   (void* p, std::size_t) noexcept                         { release (p); }
void operator delete[] (void* p, std::size_t) noexcept                         { release (p); }
void operator delete   (void* p, const std::nothrow_t&) noexcept               { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept               { release (p); }
void operator delete   (void* p, std::align_val_t) noexcept                    { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                    { releaseAligned (p); }
void operator delete   (void* p, std::size_t, std::align_val_t) noexcept       { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept       { releaseAligned (p); }
void operator delete   (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
// clang-format on
