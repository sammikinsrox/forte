// forte-string-probe: renders note models' raw bridge force (no soundboard, no sympathetic bank)
// for hammer and felt variants, as raw little-endian float32 files. Measuring the strings without
// the board is what the felt fit needs (issue 0021): through the board, its modes colour each
// partial and swamp the felt's effect.
//
//     forte-string-probe <requests.txt> <output folder>
//
// Each line of the requests file is one render: a name, then key=value settings over the seed
// note's parameters. Blank lines and lines starting with # are skipped.
//
//     c4-mf midi=60 speed=2.0 feltStiffness=3e9 feltExponent=2.8 feltRelaxation=0.95 feltRelaxationTime=5e-4
//
//   midi       the note (required)
//   speed      hammer speed in m/s (default: the note's mf speed); bypasses the velocity curve
//   velocity   a MIDI velocity through the note's velocity curve instead of speed
//   seconds    length rendered (default 1), from the strike
//   rate       sample rate (default 48000)
//   any NoteParameters hammer field: hammerMass, feltStiffness, feltExponent, feltRelaxation,
//   feltRelaxationTime, strikePosition, hammerSpeedPp/Mf/Ff, maxHammerSpeed
//
// Writes <output folder>/<name>.f32 at the seed's output gain, so levels are forte-render's
// before the soundboard. Read them with numpy.fromfile(path, "<f4"). Output is deterministic.

#include "NoteModel.h"
#include "PianoModel.h"
#include "VelocityCurve.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace forte::detail;

namespace
{
struct Request
{
    std::string name;
    std::map<std::string, double> values;
};

bool parseLine (const std::string& line, Request& request, std::string& error)
{
    std::istringstream words (line);
    if (! (words >> request.name))
        return false;

    for (std::string word; words >> word;)
    {
        const auto equals = word.find ('=');
        if (equals == std::string::npos)
        {
            error = "expected key=value, got " + word;
            return false;
        }

        try
        {
            request.values[word.substr (0, equals)] = std::stod (word.substr (equals + 1));
        }
        catch (...)
        {
            error = "bad number in " + word;
            return false;
        }
    }

    return true;
}

bool apply (const std::string& key, double value, NoteParameters& p)
{
    if (key == "hammerMass") p.hammerMass = value;
    else if (key == "feltStiffness") p.feltStiffness = value;
    else if (key == "feltExponent") p.feltExponent = value;
    else if (key == "feltRelaxation") p.feltRelaxation = value;
    else if (key == "feltRelaxationTime") p.feltRelaxationTime = value;
    else if (key == "strikePosition") p.strikePosition = value;
    else if (key == "hammerSpeedPp") p.hammerSpeeds.pp = value;
    else if (key == "hammerSpeedMf") p.hammerSpeeds.mf = value;
    else if (key == "hammerSpeedFf") p.hammerSpeeds.ff = value;
    else if (key == "maxHammerSpeed") p.maxHammerSpeed = value;
    else return false;
    return true;
}

int render (const Request& request, const PianoModel& model, const std::string& folder)
{
    const auto get = [&] (const char* key, double fallback) {
        const auto found = request.values.find (key);
        return found == request.values.end() ? fallback : found->second;
    };

    const int midi = static_cast<int> (get ("midi", 0.0));
    if (! isOnKeyboard (midi))
    {
        std::fprintf (stderr, "forte-string-probe: %s: midi must be 21-108\n", request.name.c_str());
        return 1;
    }

    auto p = model.note (midi);
    for (const auto& [key, value] : request.values)
        if (key != "midi" && key != "speed" && key != "velocity" && key != "seconds" && key != "rate" && ! apply (key, value, p))
        {
            std::fprintf (stderr, "forte-string-probe: %s: unknown setting %s\n", request.name.c_str(), key.c_str());
            return 1;
        }

    // A given speed: the velocity curve passes through its mf point at velocity 64.
    double velocity = get ("velocity", 64.0);
    if (request.values.count ("speed") != 0)
    {
        const double speed = get ("speed", 0.0);
        p.hammerSpeeds = { 0.5 * speed, speed, 2.0 * speed };
        p.maxHammerSpeed = 4.0 * speed;
        velocity = 64.0;
    }

    const double rate = get ("rate", 48000.0);
    const auto length = static_cast<int> (get ("seconds", 1.0) * rate) / NoteModel::controlInterval * NoteModel::controlInterval;

    NoteModel note;
    note.prepare (p, model.unisonSpreadCents, model.outputGain, rate);
    note.setKeyDown (true);
    note.strike (velocity, touchCalibrated, 0.0);

    std::vector<float> out (static_cast<std::size_t> (length), 0.0f), scratch (NoteModel::controlInterval);
    for (int at = 0; at < length; at += NoteModel::controlInterval)
    {
        note.controlTick (0.0f);
        note.render (out.data() + at, scratch.data(), NoteModel::controlInterval);
    }

    std::ofstream file (folder + "/" + request.name + ".f32", std::ios::binary);
    file.write (reinterpret_cast<const char*> (out.data()), static_cast<std::streamsize> (out.size() * sizeof (float)));
    if (! file)
    {
        std::fprintf (stderr, "forte-string-probe: can't write %s\n", request.name.c_str());
        return 1;
    }

    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "Usage: forte-string-probe <requests.txt> <output folder>\n");
        return 1;
    }

    std::ifstream requests (argv[1]);
    if (! requests)
    {
        std::fprintf (stderr, "forte-string-probe: can't read %s\n", argv[1]);
        return 1;
    }

    const auto model = makeSeedPianoModel();
    int lineNumber = 0;

    for (std::string line; std::getline (requests, line);)
    {
        ++lineNumber;
        if (line.empty() || line[0] == '#')
            continue;

        Request request;
        std::string error;
        if (! parseLine (line, request, error))
        {
            if (error.empty())
                continue;
            std::fprintf (stderr, "forte-string-probe: line %d: %s\n", lineNumber, error.c_str());
            return 1;
        }

        if (render (request, model, argv[2]) != 0)
            return 1;
    }

    return 0;
}
