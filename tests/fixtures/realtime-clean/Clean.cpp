// Fixture for the realtime-rule self-test: the checker must accept this file.
// Banned names in comments are fine: std::mutex, #include <mutex>, Sleep(10).
/* Block comments too: std::lock_guard, #include <thread> */

#include <atomic>

namespace
{
std::atomic<int> counter { 0 };

int noSleep (int x) { return x + counter.load (std::memory_order_relaxed); }
} // namespace
