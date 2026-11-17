#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "forte/engine/Engine.h"

#include <atomic>
#include <memory>

namespace forte
{
/** The 10 sound controls' plugin parameters (SPEC §6.3, §7.2): every one is automatable, and
    their ids are permanent.

    Hardness, Brightness, Resonance, Touch, Stretch, Spread and Width run 0–100 in whole numbers
    (50 = the real piano; Width's default is 100). A4 is whole Hz, 415–466. Volume is −60 to
    +6 dB in 0.1 dB steps, −60 showing as "Off". The Listening perspective is a two-state choice.

    The Room group's 6 controls (SPEC §2.11) are whole numbers too, their defaults the Studio
    room: Room, Size, Damping and Early 0–100, Decay 2–100 tenths of a second (shown as 0.2–10 s),
    Pre-delay 0–100 ms. */
class SoundParameters
{
public:
    struct Ids
    {
        static constexpr const char* hardness = "hardness";
        static constexpr const char* brightness = "brightness";
        static constexpr const char* resonance = "resonance";
        static constexpr const char* volume = "volume";
        static constexpr const char* perspective = "perspective";
        static constexpr const char* touch = "touch";
        static constexpr const char* a4 = "a4";
        static constexpr const char* stretch = "stretch";
        static constexpr const char* spread = "spread";
        static constexpr const char* width = "width";

        // The room (SPEC §2.11). Whole numbers; Decay in tenths of a second, shown as seconds.
        static constexpr const char* roomAmount = "roomAmount";
        static constexpr const char* roomSize = "roomSize";
        static constexpr const char* roomDecay = "roomDecay";
        static constexpr const char* roomDamping = "roomDamping";
        static constexpr const char* roomPredelay = "roomPredelay";
        static constexpr const char* roomEarly = "roomEarly";
    };

    /** The Piano group, ready for the processor's layout. */
    [[nodiscard]] static std::unique_ptr<juce::AudioProcessorParameterGroup> createGroup();

    /** The Room group. */
    [[nodiscard]] static std::unique_ptr<juce::AudioProcessorParameterGroup> createRoomGroup();

    /** "Off" at −60 dB, otherwise e.g. "-12.5 dB". */
    [[nodiscard]] static juce::String volumeText (float decibels);

    /** Finds every parameter in apvts. Call once, after apvts exists. */
    void attach (juce::AudioProcessorValueTreeState& apvts);

    /** The current values. Realtime-safe: atomic loads only. */
    [[nodiscard]] Controls read() const noexcept;
    [[nodiscard]] ListeningPerspective readPerspective() const noexcept;

private:
    std::atomic<float>* hardness = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* resonance = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* perspective = nullptr;
    std::atomic<float>* touch = nullptr;
    std::atomic<float>* a4 = nullptr;
    std::atomic<float>* stretch = nullptr;
    std::atomic<float>* spread = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* roomAmount = nullptr;
    std::atomic<float>* roomSize = nullptr;
    std::atomic<float>* roomDecay = nullptr;
    std::atomic<float>* roomDamping = nullptr;
    std::atomic<float>* roomPredelay = nullptr;
    std::atomic<float>* roomEarly = nullptr;
};
} // namespace forte
