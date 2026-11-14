#pragma once

#include "PianoModel.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace forte::detail
{
/** The Listening perspectives (SPEC §6.3), as indices into an IR set. */
inline constexpr int numPerspectives = 2;
inline constexpr int microphonesPerspective = 0;
inline constexpr int pianistPerspective = 1;

/** The rate the IRs ship at (SPEC §3.5). */
inline constexpr double irFileSampleRate = 48000.0;

struct StereoIr
{
    std::vector<float> left, right;
};

/** One stereo IR per bridge region for each Listening perspective (SPEC §2.8), at one rate. */
struct SoundboardIrs
{
    double sampleRate = irFileSampleRate;
    std::array<std::array<StereoIr, numBridgeRegions>, numPerspectives> sets;

    /** Taps in the longest IR. */
    [[nodiscard]] std::size_t length() const noexcept;
};

/** Reads a stereo 32-bit float WAV at irFileSampleRate (the shipped IR format, SPEC §3.5).
    Returns nothing if it's anything else. */
[[nodiscard]] std::optional<StereoIr> parseIrWav (std::span<const unsigned char> bytes);

/** The embedded IRs (data/ir/<perspective>-<region>.wav), at irFileSampleRate. Until the
    calibrated IRs of SPEC §3.4 exist these are the seed IRs from tools/seed-irs. A missing or
    unreadable file, which the tests rule out, becomes a unit impulse, so the engine still plays. */
[[nodiscard]] SoundboardIrs loadEmbeddedIrs();

/** Resamples every IR to newRate (SPEC §3.5: once, at load), keeping its length in seconds.
    The interpolation kernel is minimum phase, so an IR that starts with an impulse still starts
    at tap 0. A linear-phase kernel would ring before it, which would either add latency or, cut
    off, colour the direct path (by −2.5 dB at DC going to 96 kHz). The cost is the least delay
    any kernel with this response can have, about 55 µs. The magnitude is flat within 0.05 dB
    to 20 kHz (or 0.4 × the lower rate). Not realtime: it allocates and designs a filter. */
[[nodiscard]] SoundboardIrs resampleIrs (const SoundboardIrs& irs, double newRate);

/** The resampler on its own: each input, at fromRate, becomes outLength samples at toRate,
    scaled by fromRate / toRate so that convolving with it gives the same level. */
[[nodiscard]] std::vector<std::vector<float>> resampleSignals (std::span<const std::vector<float>* const> inputs,
                                                               double fromRate, double toRate, std::size_t outLength);
} // namespace forte::detail
