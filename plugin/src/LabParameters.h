#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "forte/engine/VoicingLab.h"

#include <array>
#include <atomic>
#include <memory>

namespace forte
{
/** The voicing lab's plugin parameters (SPEC §6.5), built from forte::lab::controls: one per
    control, or one per register for a per-register control. Whole numbers 0–100, default 50.

    Parameter ids are permanent (SPEC §7.2): "lab.<control id>" or "lab.<control id>.<register>",
    e.g. "lab.feltHardness.bass". */
class LabParameters
{
public:
    /** The Lab group, with a subgroup per control group, ready for the processor's layout. */
    [[nodiscard]] static std::unique_ptr<juce::AudioProcessorParameterGroup> createGroup();

    [[nodiscard]] static juce::String parameterId (lab::Control control, lab::Register reg);

    /** Export voicing: a momentary on/off. Turning it on asks for an export; the processor
        turns it off again once the file is written. */
    static constexpr const char* exportParameterId = "lab.exportVoicing";

    /** Finds every lab parameter in apvts. Call once, after apvts exists. */
    void attach (juce::AudioProcessorValueTreeState& apvts);

    /** The current values. Realtime-safe: atomic loads only. */
    [[nodiscard]] lab::Settings read() const noexcept;

    [[nodiscard]] juce::RangedAudioParameter& exportParameter() const noexcept { return *exportVoicing; }

private:
    std::array<std::array<std::atomic<float>*, lab::numRegisters>, lab::numControls> values {};
    juce::RangedAudioParameter* exportVoicing = nullptr;
};
} // namespace forte
