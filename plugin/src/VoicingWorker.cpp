#include "VoicingWorker.h"

namespace forte
{
namespace
{
constexpr int pollMilliseconds = 30;
}

VoicingWorker::VoicingWorker (Engine& e, const LabParameters& l, juce::CriticalSection& lock)
    : juce::Thread ("Forte voicing lab"), engine (e), lab (l), engineLock (lock)
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

        const auto settings = lab.read();
        const juce::ScopedLock locked (engineLock);

        if (settings != built)
        {
            engine.rebuildStrings (settings);  // a no-op for notes whose strings didn't change
            engine.rebuildBoard (settings);    // and for an unchanged board
            built = settings;
        }
        else
        {
            engine.collectRetired();
        }
    }
}
} // namespace forte
