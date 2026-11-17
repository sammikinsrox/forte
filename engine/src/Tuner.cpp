#include "Tuner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace forte::detail
{
namespace
{
constexpr int a4Note = 69;
constexpr int a3Note = 57;

/** Where the matched partials change, and over how many notes the tuner moves from one pair
    to the next (SPEC §5: "about" C6 and C3). A switch at one note leaves a step of 2–3 cents. */
constexpr int upperBreak = 84;  // C6: 4:2 below, 2:1 above
constexpr int lowerBreak = 48;  // C3: 4:2 above, 6:3 below
constexpr double breakWidth = 12.0;

/** Cents by which partial n of a string with inharmonicity b sits above n × its first partial. */
double partialSharpness (double b, int partial) noexcept
{
    return 1200.0 * std::log2 (std::sqrt (1.0 + b * partial * partial) / std::sqrt (1.0 + b));
}

/** How far above a pure 2:1 an octave from `lower` to `upper` is, when the lower note's partial
    2n is tuned beatless to the upper note's partial n. */
double octaveStretch (double lowerB, double upperB, int n) noexcept
{
    return partialSharpness (lowerB, 2 * n) - partialSharpness (upperB, n);
}

/** 0 at the near side of a break's width, 1 at the far side. */
double across (double note, double breakNote, double direction) noexcept
{
    return std::clamp (0.5 + direction * (note - breakNote) / breakWidth, 0.0, 1.0);
}
} // namespace

StretchCurve calculateStretchCurve (const PianoModel& model) noexcept
{
    StretchCurve curve;
    const auto b = [&] (int midiNote) { return model.note (midiNote).inharmonicity; };
    const auto at = [&] (int midiNote) -> double& { return curve.cents[static_cast<std::size_t> (midiNote - lowestNote)]; };

    // The temperament octave: A3–A4 as a 4:2 octave, divided equally.
    at (a4Note) = 0.0;
    at (a3Note) = -octaveStretch (b (a3Note), b (a4Note), 2);
    for (int note = a3Note + 1; note < a4Note; ++note)
        at (note) = at (a3Note) * (a4Note - note) / 12.0;

    // Upwards, each note an octave over the one tuned before it: 4:2, then 2:1 past C6.
    for (int note = a4Note + 1; note <= highestNote; ++note)
    {
        const int below = note - 12;
        const double t = across (note, upperBreak, 1.0);
        const double stretch = (1.0 - t) * octaveStretch (b (below), b (note), 2) + t * octaveStretch (b (below), b (note), 1);
        at (note) = at (below) + stretch;
    }

    // Downwards: 4:2, then 6:3 past C3.
    for (int note = a3Note - 1; note >= lowestNote; --note)
    {
        const int above = note + 12;
        const double t = across (note, lowerBreak, -1.0);
        const double stretch = (1.0 - t) * octaveStretch (b (note), b (above), 2) + t * octaveStretch (b (note), b (above), 3);
        at (note) = at (above) - stretch;
    }

    return curve;
}

double tuningRatio (const StretchCurve& curve, int noteIndex, double a4Hz, double stretch, double detune) noexcept
{
    const double cents = curve.cents[static_cast<std::size_t> (noteIndex)];
    const double scale = stretch / calculatedStretch;
    return a4Hz / referenceA4 * std::pow (2.0, (scale * cents + detuneCents (noteIndex, detune)) / 1200.0);
}

double detuneShape (int noteIndex) noexcept
{
    if (noteIndex == 69 - lowestNote)
        return 0.0;  // A4: the pitch the tuner set first, and the one everyone tunes to

    // SplitMix64's finaliser on the note: fixed, and uncorrelated from note to note.
    auto z = static_cast<std::uint64_t> (noteIndex + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double> (z >> 11) / static_cast<double> (1ull << 53) * 2.0 - 1.0;
}
} // namespace forte::detail
