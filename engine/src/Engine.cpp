#include "forte/engine/Engine.h"

#include "ScopedFlushDenormals.h"

#include <algorithm>

namespace forte
{
void Engine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    reset();
}

void Engine::reset() noexcept
{
}

void Engine::handleMidi (const MidiEvent& event) noexcept
{
    // Silent skeleton: MIDI is accepted and ignored until the note models exist.
    (void) event;
}

void Engine::render (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const detail::ScopedFlushDenormals noDenormals;

    std::fill_n (left, numSamples, 0.0f);
    std::fill_n (right, numSamples, 0.0f);
}
} // namespace forte
