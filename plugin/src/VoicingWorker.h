#pragma once

#include <juce_core/juce_core.h>

#include "forte/engine/Engine.h"

#include <functional>

namespace forte
{
/** The engine's work that isn't realtime (SPEC §5, §6.5): a background thread that watches the
    voicing lab's values and the tuning the audio thread last set and, when either changes, has
    the engine rebuild the notes whose strings changed (Engine::rebuildStrings()) and the board
    (Engine::rebuildBoard()), then frees what the engine has let go.

    It polls every 30 ms rather than being woken from the audio thread, so the audio thread
    never signals, locks or waits. A drag rebuilds at most once per poll, with its latest value.
    The engine lock keeps it out of the engine while prepareToPlay() rebuilds the engine. */
class VoicingWorker final : private juce::Thread
{
public:
    /** readLab gives the lab values the engine should play (realtime-safe, any thread). */
    VoicingWorker (Engine& engine, std::function<lab::Settings()> readLab, juce::CriticalSection& engineLock);
    ~VoicingWorker() override;

    void start();
    void stop();

private:
    void run() override;

    Engine& engine;
    std::function<lab::Settings()> readLab;
    juce::CriticalSection& engineLock;
    lab::Settings built;
    Tuning builtTuning;
};
} // namespace forte
