// String probe: renders one note model's raw bridge force (no soundboard, no sympathetic bank) for
// hammer-parameter variants, as raw little-endian float32 files at 48 kHz. Measuring the strings
// without the board is what the felt fit needs (issue 0021): through the board, its modes colour
// each partial and swamp the felt's effect.
//
// Not built by default. To use it:
//   1. add `../tools/fitting/probes/StringProbe.cpp` to forte_tests' sources in tests/CMakeLists.txt;
//   2. build forte_tests (Release);
//   3. set FORTE_PROBE_DIR to an output folder and run `forte-tests "[.probe]"`;
//   4. read the files with numpy.fromfile(path, "<f4"), e.g. with forte_audio.partial_levels;
//   5. take it out of tests/CMakeLists.txt again.
// Edit `notes` and `variants` below for the question at hand.

#include "NoteModel.h"
#include "PianoModel.h"
#include "VelocityCurve.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace forte::detail;

TEST_CASE ("String probe", "[.probe]")
{
#pragma warning(suppress : 4996)
    const char* dir = std::getenv ("FORTE_PROBE_DIR");
    REQUIRE (dir != nullptr);

    const auto model = makeSeedPianoModel();
    constexpr double rate = 48000.0;
    constexpr int length = 48000;
    constexpr double velocity = 64.0;

    const std::vector<int> notes { 48, 60, 76 };
    const std::vector<std::pair<std::string, std::function<void (NoteParameters&)>>> variants {
        { "seed", [] (NoteParameters&) {} },
        { "hysteresis10x", [] (NoteParameters& p) { p.feltHysteresis *= 10.0; } },
        { "stiffness0.25x", [] (NoteParameters& p) { p.feltStiffness *= 0.25; } },
    };

    for (int midi : notes)
    {
        for (const auto& [name, change] : variants)
        {
            auto p = model.note (midi);
            change (p);

            NoteModel note;
            note.prepare (p, model.unisonSpreadCents, model.outputGain, rate);
            note.setKeyDown (true);
            note.strike (velocity, touchCalibrated, 0.0);

            std::vector<float> out (length, 0.0f), scratch (NoteModel::controlInterval);
            for (int at = 0; at < length; at += NoteModel::controlInterval)
            {
                note.controlTick (0.0f);
                note.render (out.data() + at, scratch.data(), NoteModel::controlInterval);
            }

            std::ofstream file (std::string (dir) + "/" + std::to_string (midi) + "-" + name + ".f32", std::ios::binary);
            file.write (reinterpret_cast<const char*> (out.data()), static_cast<std::streamsize> (out.size() * sizeof (float)));
        }
    }
}
