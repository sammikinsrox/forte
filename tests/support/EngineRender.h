#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "forte/bridge/RenderBlock.h"
#include "forte/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace forte::test
{
/** A MIDI message at an absolute sample position. */
struct TimedMidi
{
    std::int64_t sample = 0;
    juce::MidiMessage message;
};

inline TimedMidi noteOn (std::int64_t at, int note, int velocity, int channel = 1)
{
    return { at, juce::MidiMessage (0x90 | (channel - 1), note, velocity) };
}

inline TimedMidi noteOff (std::int64_t at, int note, int channel = 1)
{
    return { at, juce::MidiMessage (0x80 | (channel - 1), note, 0) };
}

inline TimedMidi controller (std::int64_t at, int number, int value, int channel = 1)
{
    return { at, juce::MidiMessage::controllerEvent (channel, number, value) };
}

/** The engine's stereo output. */
struct Rendered
{
    std::vector<float> left, right;
};

/** Plays events through forte::bridge::renderBlock in blocks of blockSize, as a host would.
    Events must be in time order. */
inline Rendered render (Engine& engine, const std::vector<TimedMidi>& events, std::int64_t numSamples, int blockSize = 128)
{
    Rendered out;
    out.left.reserve (static_cast<std::size_t> (numSamples));
    out.right.reserve (static_cast<std::size_t> (numSamples));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::size_t next = 0;

    for (std::int64_t start = 0; start < numSamples; start += blockSize)
    {
        const auto length = static_cast<int> (std::min<std::int64_t> (blockSize, numSamples - start));
        buffer.setSize (2, length, false, false, true);
        midi.clear();

        for (; next < events.size() && events[next].sample < start + length; ++next)
            midi.addEvent (events[next].message, static_cast<int> (events[next].sample - start));

        bridge::renderBlock (engine, buffer, midi);

        out.left.insert (out.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
        out.right.insert (out.right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
    }

    return out;
}

/** A freshly prepared engine at sampleRate, playing events. */
inline Rendered renderFresh (const std::vector<TimedMidi>& events, std::int64_t numSamples, int blockSize = 128, double sampleRate = 48000.0)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize);
    return render (engine, events, numSamples, blockSize);
}

inline std::vector<float> slice (const std::vector<float>& v, std::int64_t from, std::int64_t to)
{
    return { v.begin() + from, v.begin() + to };
}

inline double rms (const std::vector<float>& v, std::int64_t from, std::int64_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += static_cast<double> (v[static_cast<std::size_t> (i)]) * v[static_cast<std::size_t> (i)];

    return std::sqrt (sum / static_cast<double> (to - from));
}

inline float peak (const std::vector<float>& v)
{
    float p = 0.0f;
    for (float s : v)
        p = std::max (p, std::abs (s));
    return p;
}

/** The hammer stability tests' bound on a single velocity-127 strike's peak (SPEC §2.4): a
    blow-up guard, not a level rule. It was 1.0 (0 dBFS) at the first output gain. At Keyscape's
    level (12 dB up, the tape off) single velocity-127 strikes reach about +6 dBFS, as Keyscape's
    own clip; the output is float, so the host's gain staging handles it. */
inline constexpr float stableStrikePeak = 4.0f;  // +12 dBFS

/** Index of the first non-zero sample, or -1. */
inline std::int64_t firstNonZero (const std::vector<float>& v)
{
    const auto it = std::find_if (v.begin(), v.end(), [] (float s) { return s != 0.0f; });
    return it == v.end() ? -1 : static_cast<std::int64_t> (it - v.begin());
}

inline bool allZero (const std::vector<float>& v)
{
    return firstNonZero (v) < 0;
}

/** Finiteness from the bit pattern: std::isfinite can be optimised away under /fp:fast. */
inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float s)
    {
        std::uint32_t bits;
        std::memcpy (&bits, &s, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    });
}
} // namespace forte::test
