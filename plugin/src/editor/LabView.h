#pragma once

#include "Widgets.h"

#include "forte/engine/VoicingLab.h"

#include <array>
#include <memory>

namespace forte
{
class PluginProcessor;
}

namespace forte::ui
{
/** The voicing lab's screen (SPEC §6.5), the editor's third view.

    Every control is a knob showing one register at a time, Bass, Middle or Treble (or All, which
    moves the three together), with its value in physical terms and the three registers as
    small bars. The control under the mouse is drawn across the whole keyboard below, the
    committed voicing beside it, with a handle at each register's anchor to drag. Lab | Committed
    compares by ear; Export voicing writes the file the fold reads. */
class LabView final : public juce::Component,
                      private juce::Timer
{
public:
    explicit LabView (PluginProcessor& processor);
    ~LabView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /** Which register the knobs show: 0–2, or all = 3. */
    static constexpr int allRegisters = 3;

    // --- For the view's parts ---------------------------------------------------------------
    [[nodiscard]] const lab::Settings& values() const noexcept { return settings; }
    [[nodiscard]] int registerMode() const noexcept { return mode; }
    void setRegisterMode (int newMode);
    [[nodiscard]] lab::Control focus() const noexcept { return focused; }
    void setFocus (lab::Control control);

    /** The register a control shows: its own one, Middle under All, Bass for a global control. */
    [[nodiscard]] lab::Register shownRegister (lab::Control control) const noexcept;

    /** What a control means at a register's anchor key under settings. */
    [[nodiscard]] double physical (const lab::Settings& at, lab::Control control, lab::Register reg) const noexcept;
    [[nodiscard]] double physicalAtKey (const lab::Settings& at, lab::Control control, int midiNote) const noexcept;

    /** An edit: begin, any number of sets, end. Registers outside the control's are ignored. */
    void beginEdit (lab::Control control);
    void setValue (lab::Control control, lab::Register reg, int value);
    void endEdit (lab::Control control);

    /** One complete change, as a typed value or a reset. */
    void setValueNow (lab::Control control, lab::Register reg, int value);
    void resetControl (lab::Control control);
    void resetGroup (lab::Group group);
    void resetAll();

private:
    class Cell;
    class Curve;
    class Info;

    void timerCallback() override;
    [[nodiscard]] juce::RangedAudioParameter* parameter (lab::Control control, lab::Register reg) const noexcept;
    [[nodiscard]] lab::Settings readSettings() const;
    void refresh();
    [[nodiscard]] juce::Rectangle<int> groupArea (lab::Group group) const;

    PluginProcessor& processor;
    std::array<std::array<juce::RangedAudioParameter*, lab::numRegisters>, lab::numControls> parameters {};
    lab::Settings settings;
    int mode = 1;  // Middle
    lab::Control focused = lab::Control::feltHardness;

    Segmented registers { { "Bass  C2", "Middle  C4", "Treble  C7", "All" } };
    Segmented compare { { "Lab", "Committed" } };
    juce::TextButton exportButton { "Export voicing" };
    std::array<std::unique_ptr<juce::TextButton>, 4> groupResets;
    std::array<std::unique_ptr<Cell>, lab::numControls> cells;
    std::unique_ptr<Curve> curve;
    std::unique_ptr<Info> info;
    PianoKeyboard keyboard;

    juce::String status;
    bool statusBusy = false;
};
} // namespace forte::ui
