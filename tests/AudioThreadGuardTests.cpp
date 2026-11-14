// The guard has to actually catch allocations, or every "doesn't allocate" test passes vacuously.

#include "support/AudioThreadGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using forte::test::AudioThreadScope;
using forte::test::heapUseOnAudioThread;

namespace
{
// Volatile sinks stop the optimiser removing an allocation it can prove is unused.
volatile int sink = 0;

void allocateWithNew()
{
    std::vector<int> v (64, 1);
    sink = v[static_cast<std::size_t> (sink & 1)];
}
} // namespace

TEST_CASE ("Audio-thread guard counts operator new and delete inside the scope", "[audio-thread]")
{
    const auto use = heapUseOnAudioThread (allocateWithNew);

    CHECK (use.allocations >= 1);
    CHECK (use.deallocations >= 1);
    CHECK (use.any());
}

TEST_CASE ("Audio-thread guard counts aligned allocations", "[audio-thread]")
{
    struct alignas (64) Aligned
    {
        float values[16];
    };

    const auto use = heapUseOnAudioThread ([]
    {
        auto p = std::make_unique<Aligned>();
        sink = static_cast<int> (p->values[sink & 1]);
    });

    CHECK (use.allocations >= 1);
    CHECK (use.deallocations >= 1);
}

#ifdef _DEBUG
TEST_CASE ("Audio-thread guard counts malloc and free in Debug", "[audio-thread]")
{
    // Called through a volatile pointer so the calls can't be optimised out.
    void* (*volatile doMalloc) (std::size_t) = std::malloc;
    void (*volatile doFree) (void*) = std::free;

    const auto use = heapUseOnAudioThread ([&]
    {
        void* p = doMalloc (128);
        doFree (p);
    });

    CHECK (use.allocations == 1);
    CHECK (use.deallocations == 1);
}
#endif

TEST_CASE ("Audio-thread guard ignores allocations outside the scope", "[audio-thread]")
{
    allocateWithNew();
    const auto use = heapUseOnAudioThread ([] {});
    allocateWithNew();

    CHECK_FALSE (use.any());
}

TEST_CASE ("Audio-thread guard watches only the thread that opened the scope", "[audio-thread]")
{
    // Starting a thread allocates on this thread, so start it before the scope opens and only
    // let it allocate once the scope is open.
    std::atomic<bool> go { false };
    std::atomic<bool> done { false };
    std::thread other ([&]
    {
        while (! go.load())
            std::this_thread::yield();

        allocateWithNew();
        done.store (true);
    });

    forte::test::HeapUse use;
    {
        const AudioThreadScope scope;
        go.store (true);

        while (! done.load())
            std::this_thread::yield();

        use = scope.heapUse();
    }

    other.join();
    CHECK_FALSE (use.any());
}

TEST_CASE ("Audio-thread scopes nest", "[audio-thread]")
{
    AudioThreadScope outer;
    {
        const auto inner = heapUseOnAudioThread (allocateWithNew);
        CHECK (inner.any());  // Catch2 allocates here, so check before the outer total
    }
    CHECK (outer.heapUse().any());
}
