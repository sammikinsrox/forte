#pragma once

#include <juce_core/juce_core.h>

#include "LabParameters.h"

#include "forte/engine/Engine.h"

namespace forte
{
/** The voicing lab's work that isn't realtime (SPEC §6.5): a background thread that watches
    the lab's values and, when they change, has the engine rebuild the notes whose strings
    changed (Engine::rebuildStrings()), then frees the models notes have let go.

    It polls every 30 ms rather than being woken from the audio thread, so the audio thread
    never signals, locks or waits. A drag rebuilds at most once per poll, with its latest value.
    The engine lock keeps it out of the engine while prepareToPlay() rebuilds the engine. */
class VoicingWorker final : private juce::Thread
{
public:
    VoicingWorker (Engine& engine, const LabParameters& lab, juce::CriticalSection& engineLock);
    ~VoicingWorker() override;

    void start();
    void stop();

private:
    void run() override;

    Engine& engine;
    const LabParameters& lab;
    juce::CriticalSection& engineLock;
    lab::Settings built;
};
} // namespace forte
