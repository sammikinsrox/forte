#include "VoicingWorker.h"

namespace forte
{
namespace
{
constexpr int pollMilliseconds = 30;
}

VoicingWorker::VoicingWorker (Engine& e, std::function<lab::Settings()> read, juce::CriticalSection& lock)
    : juce::Thread ("Forte voicing"), engine (e), readLab (std::move (read)), engineLock (lock)
{
}

VoicingWorker::~VoicingWorker()
{
    stop();
}

void VoicingWorker::start()
{
    startThread (juce::Thread::Priority::low);
}

void VoicingWorker::stop()
{
    stopThread (5000);
}

void VoicingWorker::run()
{
    while (! threadShouldExit())
    {
        wait (pollMilliseconds);

        const auto settings = readLab();
        const juce::ScopedLock locked (engineLock);
        const auto tuning = engine.getLatestTuning();

        if (settings != built || tuning != builtTuning)
        {
            engine.rebuildStrings (settings, tuning);  // a no-op for notes whose strings didn't change
            engine.rebuildBoard (settings);            // and for an unchanged board
            built = settings;
            builtTuning = tuning;
        }
        else
        {
            engine.collectRetired();
        }
    }
}
} // namespace forte
