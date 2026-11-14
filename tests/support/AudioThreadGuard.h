#pragma once

#include <cstdint>

namespace forte::test
{
/** Heap operations counted on one thread while an AudioThreadScope was open. */
struct HeapUse
{
    std::int64_t allocations = 0;
    std::int64_t deallocations = 0;

    [[nodiscard]] bool any() const noexcept { return allocations != 0 || deallocations != 0; }
};

/** Marks the current thread as the audio thread for as long as it lives, and counts every heap
    operation the thread makes meanwhile (SPEC §9.3).

    What is counted:
    - every replaceable global operator new and delete (all configurations);
    - in Debug builds, also malloc, calloc, realloc and free, through the CRT allocation hook.
      This is what catches JUCE's HeapBlock, which calls std::malloc.

    Only this thread is watched. Other threads may allocate freely.

    Opening and closing a scope doesn't allocate, so the engine calls under test are the only
    thing measured. Keep Catch2 assertions outside the scope: they allocate.
*/
class AudioThreadScope
{
public:
    AudioThreadScope() noexcept;
    ~AudioThreadScope() noexcept;

    AudioThreadScope (const AudioThreadScope&) = delete;
    AudioThreadScope& operator= (const AudioThreadScope&) = delete;

    /** Heap operations on this thread since the scope opened. */
    [[nodiscard]] HeapUse heapUse() const noexcept;

private:
    HeapUse start;
};

/** Runs f as if on the audio thread and returns the heap operations it made. */
template <typename Function>
[[nodiscard]] HeapUse heapUseOnAudioThread (Function&& f)
{
    const AudioThreadScope scope;
    f();
    return scope.heapUse();
}
} // namespace forte::test
