// Fixture for the realtime-rule self-test: the checker must reject this file.
// It's never compiled.

#include <mutex>

namespace
{
std::mutex lock;

void touch()
{
    const std::lock_guard<std::mutex> hold (lock);
}
} // namespace
