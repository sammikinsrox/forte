#pragma once

#include "PianoModel.h"
#include "SoundboardIrs.h"

#include <array>

namespace forte::detail
{
/** What the voicing lab's Board controls ask of the IRs (SPEC §6.5), per bridge region where
    they are per register. */
struct BoardVoicing
{
    std::array<double, numBridgeRegions> pickup {};  // the direct path's gain
    double tiltDbPerOctave = 0.0;                    // on the modes above board::modeTiltFromHz
    std::array<std::array<double, 3>, numBridgeRegions> colourDb {};  // low, mid, high shelves over the fitted colour

    friend bool operator== (const BoardVoicing&, const BoardVoicing&) = default;
};

/** The board the shipped IRs were built with. */
[[nodiscard]] BoardVoicing committedBoardVoicing() noexcept;

/** The lab's colour shelves (low, mid, high dB) at each of the fitted colour curve's centres
    (board::colourCentresHz): what they add to it. Each crossover, at 250 Hz and 2.5 kHz, blends
    over an octave. */
[[nodiscard]] std::vector<double> labColourAtCentres (const std::array<double, 3>& shelvesDb);

/** The key a region's per-register values are read at: the middle of the keys its bridge carries. */
[[nodiscard]] double regionCentreKey (int region) noexcept;

/** The IRs' kept resonant parts (data/ir/parts), at irFileSampleRate: each IR's modes, before the
    direct path, colour, placement, fade and gain, scaled so the louder channel has unit energy. */
struct BoardParts
{
    std::array<std::array<StereoIr, numBridgeRegions>, numPerspectives> modes;
};

/** The embedded parts. Missing or unreadable ones (which the tests rule out) are silence. */
[[nodiscard]] BoardParts loadEmbeddedBoardParts();

/** The four IRs of both perspectives, at irFileSampleRate, as tools/seed-irs/make_seed_irs.py
    builds them, with the board's tilt, pickup and colour as voicing has them. At
    committedBoardVoicing() it gives the shipped IRs again to within float rounding. Not
    realtime: it allocates and runs FFTs. */
[[nodiscard]] SoundboardIrs buildBoardIrs (const BoardParts& parts, const BoardVoicing& voicing);
} // namespace forte::detail
