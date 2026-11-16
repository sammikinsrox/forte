// forte-render: renders a MIDI file to a stereo 32-bit float WAV through forte::Engine, the same
// way the plugin does (forte::bridge::renderBlock). Two runs with the same arguments write
// identical files.

#include <juce_audio_formats/juce_audio_formats.h>

#include "forte/Version.h"
#include "forte/bridge/RenderBlock.h"
#include "forte/bridge/VoicingFile.h"
#include "forte/engine/Engine.h"

#include <cmath>
#include <cstdio>
#include <optional>

namespace
{
struct Options
{
    juce::File input;
    juce::File output;
    double sampleRate = 48000.0;
    int blockSize = 128;
    double tailSeconds = 3.0;
    juce::File voicing;  // none: the committed voicing
    juce::File labValues;  // set: write what the voicing means and don't render
};

constexpr const char* usage =
    "Usage: forte-render <input.mid> <output.wav> [--rate <Hz>] [--block <samples>] [--tail <seconds>]\n"
    "                    [--voicing <voicing.json>]\n"
    "       forte-render --voicing <voicing.json> --lab-values <out.json>\n"
    "\n"
    "  --rate   sample rate, 44100 to 96000 (default 48000)\n"
    "  --block  block size in samples, 1 to 8192 (default 128)\n"
    "  --tail   seconds rendered after the last MIDI event (default 3)\n"
    "  --voicing a voicing lab file (SPEC 6.5), as Export voicing writes it (default: all 50)\n"
    "  --lab-values writes, instead of rendering, what the voicing means on each note and bridge\n"
    "           region in physical terms (for tools/fitting/fold_voicing.py)\n";

int fail (const juce::String& message)
{
    std::fprintf (stderr, "forte-render: %s\n", message.toRawUTF8());
    return 1;
}

std::optional<Options> parseArguments (const juce::StringArray& args, juce::String& error)
{
    Options options;
    juce::StringArray positional;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (! arg.startsWith ("--"))
        {
            positional.add (arg);
            continue;
        }

        if (i + 1 >= args.size())
        {
            error = arg + " needs a value";
            return std::nullopt;
        }

        const auto value = args[++i];

        if (arg == "--rate")
            options.sampleRate = value.getDoubleValue();
        else if (arg == "--block")
            options.blockSize = value.getIntValue();
        else if (arg == "--tail")
            options.tailSeconds = value.getDoubleValue();
        else if (arg == "--voicing")
            options.voicing = juce::File::getCurrentWorkingDirectory().getChildFile (value);
        else if (arg == "--lab-values")
            options.labValues = juce::File::getCurrentWorkingDirectory().getChildFile (value);
        else
        {
            error = "unknown option " + arg;
            return std::nullopt;
        }
    }

    if (options.labValues != juce::File())
    {
        if (options.voicing == juce::File() || ! positional.isEmpty())
        {
            error = "--lab-values takes --voicing and nothing else";
            return std::nullopt;
        }
        return options;
    }

    if (positional.size() != 2)
    {
        error = "expected an input MIDI file and an output WAV file";
        return std::nullopt;
    }

    if (options.sampleRate < 44100.0 || options.sampleRate > 96000.0)
    {
        error = "--rate must be between 44100 and 96000";
        return std::nullopt;
    }

    if (options.blockSize < 1 || options.blockSize > 8192)
    {
        error = "--block must be between 1 and 8192";
        return std::nullopt;
    }

    if (! (options.tailSeconds >= 0.0 && options.tailSeconds <= 600.0))
    {
        error = "--tail must be between 0 and 600";
        return std::nullopt;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    options.input = cwd.getChildFile (positional[0]);
    options.output = cwd.getChildFile (positional[1]);
    return options;
}

/** Merges every track into one sequence, timestamped in seconds. */
std::optional<juce::MidiMessageSequence> readMidiFile (const juce::File& file, juce::String& error)
{
    juce::FileInputStream stream (file);

    if (! stream.openedOk())
    {
        error = "can't open " + file.getFullPathName();
        return std::nullopt;
    }

    juce::MidiFile midiFile;

    if (! midiFile.readFrom (stream))
    {
        error = file.getFullPathName() + " is not a valid MIDI file";
        return std::nullopt;
    }

    midiFile.convertTimestampTicksToSeconds();

    juce::MidiMessageSequence sequence;

    for (int track = 0; track < midiFile.getNumTracks(); ++track)
        sequence.addSequence (*midiFile.getTrack (track), 0.0);

    sequence.sort();
    return sequence;
}

std::optional<forte::lab::Settings> readVoicing (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "can't open " + file.getFullPathName();
        return std::nullopt;
    }

    auto voicing = forte::bridge::parseVoicing (file.loadFileAsString(), error);
    if (! voicing)
        error = file.getFullPathName() + ": " + error;
    return voicing;
}

/** --lab-values: the voicing in physical terms, per note for the hammer and strings and per
    bridge region for the board, from the engine's own mapping. */
int writeLabValues (const Options& options)
{
    using namespace forte;
    juce::String error;
    const auto settings = readVoicing (options.voicing, error);
    if (! settings)
        return fail (error);

    const Engine engine;
    const auto toArray = [] (auto&& values) {
        juce::Array<juce::var> a;
        for (const double v : values)
            a.add (v);
        return juce::var (a);
    };

    auto* notes = new juce::DynamicObject();
    for (const auto& c : lab::controls)
    {
        if (c.group != lab::Group::hammer && c.group != lab::Group::strings)
            continue;

        std::vector<double> perNote;
        for (int midiNote = 21; midiNote <= 108; ++midiNote)
            perNote.push_back (engine.getLabPhysicalValue (*settings, c.control, midiNote));
        notes->setProperty (bridge::toJuceString (c.id), toArray (perNote));
    }

    const auto board = engine.getLabBoardValues (*settings);
    auto* boardObject = new juce::DynamicObject();
    boardObject->setProperty ("pickup", toArray (board.pickup));
    boardObject->setProperty ("tiltDbPerOctave", board.tiltDbPerOctave);
    juce::Array<juce::var> colour;
    for (const auto& region : board.colourDbAtCentres)
        colour.add (toArray (region));
    boardObject->setProperty ("colourDbAtCentres", colour);

    auto* root = new juce::DynamicObject();
    root->setProperty ("voicingRevision", Engine::getVoicingRevision());
    root->setProperty ("notes", juce::var (notes));
    root->setProperty ("tapeDrive", engine.getLabPhysicalValue (*settings, lab::Control::tapeDrive, 60));
    root->setProperty ("board", juce::var (boardObject));

    if (! options.labValues.replaceWithText (juce::JSON::toString (juce::var (root))))
        return fail ("can't write " + options.labValues.getFullPathName());

    std::printf ("forte-render %s: lab values -> %s\n", versionString, options.labValues.getFullPathName().toRawUTF8());
    return 0;
}

int run (const Options& options)
{
    if (options.labValues != juce::File())
        return writeLabValues (options);

    juce::String error;
    const auto sequence = readMidiFile (options.input, error);

    if (! sequence)
        return fail (error);

    // Every event lands on the sample nearest its time (SPEC §4.3).
    const auto toSample = [&] (double seconds) { return (juce::int64) std::llround (seconds * options.sampleRate); };

    const auto lastEventSample = sequence->getNumEvents() > 0 ? toSample (sequence->getEndTime()) : 0;
    const auto totalSamples = lastEventSample + toSample (options.tailSeconds);

    options.output.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (options.output);

    if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
        return fail ("can't write " + options.output.getFullPathName());

    juce::WavAudioFormat wav;
    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate (options.sampleRate)
                                   .withNumChannels (forte::Engine::numOutputChannels)
                                   .withBitsPerSample (32)
                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor (stream, writerOptions);

    if (writer == nullptr)
        return fail ("can't create a WAV writer for " + options.output.getFullPathName());

    forte::Engine engine;

    if (options.voicing != juce::File())
    {
        const auto voicing = readVoicing (options.voicing, error);
        if (! voicing)
            return fail (error);

        // Before prepare(), so every note is built with the lab's strings and nothing glides.
        engine.setVoicingLab (*voicing);
    }

    engine.prepare (options.sampleRate, options.blockSize);

    juce::AudioBuffer<float> buffer (forte::Engine::numOutputChannels, options.blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    int nextEvent = 0;

    for (juce::int64 blockStart = 0; blockStart < totalSamples; blockStart += options.blockSize)
    {
        const auto blockLength = (int) std::min<juce::int64> (options.blockSize, totalSamples - blockStart);
        const auto blockEnd = blockStart + blockLength;

        midi.clear();

        for (; nextEvent < sequence->getNumEvents(); ++nextEvent)
        {
            const auto& message = sequence->getEventPointer (nextEvent)->message;
            const auto at = toSample (message.getTimeStamp());

            if (at >= blockEnd)
                break;

            if (message.isMetaEvent() || message.isSysEx())
                continue;

            midi.addEvent (message, (int) (at - blockStart));
        }

        buffer.setSize (forte::Engine::numOutputChannels, blockLength, false, false, true);
        forte::bridge::renderBlock (engine, buffer, midi);

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, blockLength))
            return fail ("write failed: " + options.output.getFullPathName());
    }

    writer.reset();

    std::printf ("forte-render %s: %s -> %s (%.0f Hz, %lld samples)\n",
                 forte::versionString,
                 options.input.getFileName().toRawUTF8(),
                 options.output.getFullPathName().toRawUTF8(),
                 options.sampleRate,
                 (long long) totalSamples);
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.contains ("--help") || args.contains ("-h"))
    {
        std::printf ("%s", usage);
        return 0;
    }

    if (args.contains ("--version"))
    {
        std::printf ("forte-render %s\n", forte::versionString);
        return 0;
    }

    juce::String error;
    const auto options = parseArguments (args, error);

    if (! options)
    {
        std::fprintf (stderr, "forte-render: %s\n\n%s", error.toRawUTF8(), usage);
        return 2;
    }

    return run (*options);
}
