#include "support/AudioThreadGuard.h"
#include "support/EngineRender.h"

#include "forte/bridge/VoicingFile.h"
#include "forte/engine/VoicingLab.h"

#include "LabMapping.h"
#include "PianoModel.h"
#include "VoicingExport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cmath>
#include <set>
#include <string_view>
#include <thread>

using namespace forte;
using namespace forte::test;
using Catch::Matchers::WithinRel;
using lab::Control;
using lab::Register;

namespace
{
constexpr std::int64_t second = 48000;

lab::Settings with (Control control, float value)
{
    lab::Settings s;
    for (int r = 0; r < lab::numRegisters; ++r)
        s.set (control, static_cast<Register> (r), value);
    return s;
}

/** Plays `before` from a fresh engine for `split` samples, applies the lab, then plays `after`
    (times relative to the split) to `length`. */
Rendered renderWithChange (const std::vector<TimedMidi>& before, std::int64_t split, const lab::Settings& settings,
                           const std::vector<TimedMidi>& after, std::int64_t length)
{
    Engine engine;
    engine.prepare (48000.0, 128);
    auto out = render (engine, before, split);
    engine.setVoicingLab (settings);
    const auto rest = render (engine, after, length - split);
    out.left.insert (out.left.end(), rest.left.begin(), rest.left.end());
    out.right.insert (out.right.end(), rest.right.begin(), rest.right.end());
    return out;
}
} // namespace

TEST_CASE ("Voicing lab: every control has a unique, non-empty id and name", "[lab]")
{
    std::set<std::string_view> ids;

    for (const auto& c : lab::controls)
    {
        CHECK_FALSE (c.id.empty());
        CHECK_FALSE (c.name.empty());
        CHECK_FALSE (c.range.empty());
        CHECK (ids.insert (c.id).second);
    }

    CHECK (ids.size() == static_cast<std::size_t> (lab::numControls));
}

TEST_CASE ("Voicing lab: settings start at the committed voicing", "[lab]")
{
    const lab::Settings settings;

    for (const auto& c : lab::controls)
    {
        CHECK (settings.isNeutral (c.control));
        for (int r = 0; r < lab::numRegisters; ++r)
            CHECK (settings.get (c.control, static_cast<Register> (r)) == lab::neutralValue);
    }

    for (auto group : { lab::Group::hammer, lab::Group::strings, lab::Group::board, lab::Group::output })
        CHECK (settings.isNeutral (group));
}

TEST_CASE ("Voicing lab: one register off 50 makes its control and group non-neutral", "[lab]")
{
    lab::Settings settings;
    settings.set (Control::sustain, Register::treble, 51.0f);

    CHECK_FALSE (settings.isNeutral (Control::sustain));
    CHECK_FALSE (settings.isNeutral (lab::Group::strings));
    CHECK (settings.isNeutral (lab::Group::hammer));
    CHECK (settings != lab::Settings {});
}

TEST_CASE ("Voicing lab: a control without registers ignores its unused slots", "[lab]")
{
    lab::Settings settings;
    settings.set (Control::tapeDrive, Register::treble, 0.0f);

    CHECK (settings.isNeutral (Control::tapeDrive));
}

TEST_CASE ("Voicing lab: 50 maps to the committed value exactly, 0 and 100 to the ends", "[lab]")
{
    using detail::labMap;

    for (const auto& c : lab::controls)
        for (double fitted : { 0.0, 0.37, 1.0, 8.0 })
            CHECK (labMap (c.control, 50.0, fitted) == fitted);

    CHECK_THAT (labMap (Control::feltHardness, 0.0, 1.0), WithinRel (0.25, 1e-12));
    CHECK_THAT (labMap (Control::feltHardness, 100.0, 1.0), WithinRel (4.0, 1e-12));
    CHECK_THAT (labMap (Control::feltHardness, 75.0, 1.0), WithinRel (2.0, 1e-12));
    CHECK (labMap (Control::feltSpike, 0.0, 0.85) == 0.0);
    CHECK_THAT (labMap (Control::feltSpike, 100.0, 0.85), WithinRel (0.95, 1e-12));
    CHECK (labMap (Control::velocityBrightness, 0.0, 0.5) == 0.0);
    CHECK_THAT (labMap (Control::velocityBrightness, 100.0, 0.5), WithinRel (1.0, 1e-12));
    CHECK_THAT (labMap (Control::strikePosition, 0.0, 1.0), WithinRel (0.6, 1e-12));
    CHECK_THAT (labMap (Control::strikePosition, 100.0, 1.0), WithinRel (1.4, 1e-12));
    CHECK_THAT (labMap (Control::hammerMass, 0.0, 1.0), WithinRel (0.5, 1e-12));
    CHECK_THAT (labMap (Control::hammerMass, 100.0, 1.0), WithinRel (2.0, 1e-12));
    CHECK (labMap (Control::knockLevel, 0.0, 1.0) == 0.0);
    CHECK_THAT (labMap (Control::knockLevel, 100.0, 1.0), WithinRel (3.0, 1e-12));
    CHECK_THAT (labMap (Control::sustain, 0.0, 1.0), WithinRel (2.0, 1e-12));
    CHECK_THAT (labMap (Control::sustain, 100.0, 1.0), WithinRel (0.5, 1e-12));
    CHECK_THAT (labMap (Control::overtoneDecay, 0.0, 0.0), WithinRel (-0.5, 1e-12));
    CHECK_THAT (labMap (Control::overtoneDecay, 100.0, 0.0), WithinRel (0.5, 1e-12));
    CHECK_THAT (labMap (Control::pickup, 100.0, 0.5), WithinRel (2.0, 1e-12));
    CHECK_THAT (labMap (Control::boardTilt, 0.0, 8.0), WithinRel (-4.0, 1e-12));
    CHECK_THAT (labMap (Control::boardTilt, 100.0, 8.0), WithinRel (16.0, 1e-12));
    CHECK_THAT (labMap (Control::colourHigh, 100.0, 0.0), WithinRel (12.0, 1e-12));
    CHECK (labMap (Control::tapeDrive, 0.0, 8.0) == 0.0);
    CHECK_THAT (labMap (Control::tapeDrive, 100.0, 8.0), WithinRel (24.0, 1e-12));
}

TEST_CASE ("Voicing lab: registers sit at C2, C4 and C7, flat outside, log between for multipliers", "[lab]")
{
    using detail::labValueAtKey;

    lab::Settings s;
    s.set (Control::feltHardness, Register::bass, 100.0f);    // ×4
    s.set (Control::feltHardness, Register::middle, 50.0f);   // ×1
    s.set (Control::feltHardness, Register::treble, 0.0f);    // ×0.25

    CHECK_THAT (labValueAtKey (s, Control::feltHardness, 21, 1.0), WithinRel (4.0, 1e-12));
    CHECK_THAT (labValueAtKey (s, Control::feltHardness, 36, 1.0), WithinRel (4.0, 1e-12));
    CHECK_THAT (labValueAtKey (s, Control::feltHardness, 48, 1.0), WithinRel (2.0, 1e-12));   // halfway in log
    CHECK (labValueAtKey (s, Control::feltHardness, 60, 1.0) == 1.0);
    CHECK_THAT (labValueAtKey (s, Control::feltHardness, 78, 1.0), WithinRel (0.5, 1e-12));
    CHECK_THAT (labValueAtKey (s, Control::feltHardness, 108, 1.0), WithinRel (0.25, 1e-12));

    lab::Settings k;
    k.set (Control::knockLevel, Register::middle, 100.0f);   // ×3 at C4, ×1 at C2: linear between
    CHECK_THAT (labValueAtKey (k, Control::knockLevel, 48, 1.0), WithinRel (2.0, 1e-12));
}

TEST_CASE ("Voicing lab: the committed hammer is each note's own", "[lab]")
{
    const auto model = detail::makeSeedPianoModel();
    const lab::Settings neutral;

    for (const auto& p : model.notes)
    {
        const auto v = detail::labStrikeVoicing (neutral, p);
        CHECK (v.feltStiffnessScale == 1.0);
        CHECK (v.feltRelaxation == p.feltRelaxation);
        CHECK (v.contactSpeedExponent == p.contactSpeedExponent);
        CHECK (v.strikePositionScale == 1.0);
        CHECK (v.hammerMassScale == 1.0);
        CHECK (v.knockScale == 1.0);
    }
}

TEST_CASE ("Voicing lab: back at 50 after a change, the engine is the committed one bit for bit", "[lab][engine]")
{
    const std::vector<TimedMidi> notes { noteOn (0, 48, 90), noteOn (100, 72, 110), noteOn (5000, 96, 127) };
    const auto committed = renderFresh (notes, second);

    Engine engine;
    engine.prepare (48000.0, 128);
    lab::Settings moved = with (Control::feltHardness, 90.0f);
    moved.set (Control::tapeDrive, Register::bass, 10.0f);
    moved.set (Control::strikePosition, Register::treble, 0.0f);
    engine.setVoicingLab (moved);
    engine.setVoicingLab (lab::Settings {});
    engine.reset();  // lands the tape at once

    const auto out = render (engine, notes, second);
    CHECK (out.left == committed.left);
    CHECK (out.right == committed.right);
}

TEST_CASE ("Voicing lab: hammer controls act on the next strike, never on a ringing note", "[lab][engine]")
{
    const auto control = GENERATE (Control::feltHardness, Control::feltSpike, Control::velocityBrightness,
                                   Control::strikePosition, Control::hammerMass, Control::knockLevel);
    const auto value = GENERATE (0.0f, 100.0f);
    INFO ("control " << lab::info (control).id << " at " << value);

    // C6 at velocity 100: every hammer control has something to change there (C6 has a knock
    // and a relaxing felt). The change lands 16 samples into the contact and again 0.25 s in.
    constexpr int note = 84;
    const auto split = GENERATE (std::int64_t { 16 }, second / 4);
    const std::int64_t restrike = second / 2;
    const std::int64_t length = second;

    const auto committed = renderFresh ({ noteOn (0, note, 100), noteOn (restrike, note, 100) }, length);
    const auto changed = renderWithChange ({ noteOn (0, note, 100) }, split, with (control, value),
                                           { noteOn (restrike - split, note, 100) }, length);

    CHECK (slice (changed.left, 0, restrike) == slice (committed.left, 0, restrike));
    CHECK (slice (changed.right, 0, restrike) == slice (committed.right, 0, restrike));
    CHECK (slice (changed.left, restrike, length) != slice (committed.left, restrike, length));
}

TEST_CASE ("Voicing lab: the hammer is stable on every note at the controls' extremes", "[lab][engine][hammer]")
{
    // SPEC §2.4's stability test, with the lab's hardest felt (SPEC §6.5: and every other
    // hammer control at an extreme), at every sample rate.
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    const auto [massValue, strikeValue] = GENERATE (table<float, float> ({ { 0.0f, 0.0f }, { 100.0f, 100.0f }, { 0.0f, 100.0f } }));

    lab::Settings s;
    for (int r = 0; r < lab::numRegisters; ++r)
    {
        const auto reg = static_cast<Register> (r);
        for (auto c : { Control::feltHardness, Control::feltSpike, Control::velocityBrightness, Control::knockLevel })
            s.set (c, reg, 100.0f);
        s.set (Control::hammerMass, reg, massValue);
        s.set (Control::strikePosition, reg, strikeValue);
    }

    Engine engine;
    engine.prepare (sampleRate, 512);
    engine.setVoicingLab (s);

    for (int note = 21; note <= 108; ++note)
    {
        engine.reset();
        const auto out = render (engine, { noteOn (0, note, 127) }, static_cast<std::int64_t> (sampleRate * 0.05), 512);

        INFO ("note " << note << " at " << sampleRate << " Hz, mass " << massValue << ", strike " << strikeValue);
        REQUIRE (allFinite (out.left));
        REQUIRE (allFinite (out.right));
        REQUIRE (std::max (peak (out.left), peak (out.right)) < 1.0f);
        REQUIRE (std::max (peak (out.left), peak (out.right)) > 1e-3f);
    }
}

TEST_CASE ("Voicing lab: tape drive glides to its new value in about 20 ms", "[lab][engine]")
{
    // The tape has no memory, so once the glide ends the output is exactly the new drive's.
    const std::vector<TimedMidi> chord { noteOn (0, 48, 120), noteOn (0, 60, 120), noteOn (0, 67, 120) };
    const std::int64_t split = second / 10;
    const std::int64_t glideEnd = split + second / 50 + 128;
    const std::int64_t length = second / 2;

    const auto committed = renderFresh (chord, length);

    Engine off;
    off.prepare (48000.0, 128);
    off.setVoicingLab (with (Control::tapeDrive, 0.0f));
    off.reset();
    const auto untaped = render (off, chord, length);
    REQUIRE (untaped.left != committed.left);

    const auto changed = renderWithChange (chord, split, with (Control::tapeDrive, 0.0f), {}, length);

    CHECK (slice (changed.left, 0, split) == slice (committed.left, 0, split));
    CHECK (slice (changed.left, glideEnd, length) == slice (untaped.left, glideEnd, length));

    // No jump: no step in the glide is bigger than the largest around it.
    float largestStep = 0.0f, largestGlideStep = 0.0f;
    for (auto i = split - second / 50; i < glideEnd + second / 50; ++i)
    {
        const auto at = static_cast<std::size_t> (i);
        const float step = std::abs (changed.left[at] - changed.left[at - 1]);
        if (i >= split && i < glideEnd)
            largestGlideStep = std::max (largestGlideStep, step);
        else
            largestStep = std::max (largestStep, step);
    }
    CHECK (largestGlideStep <= 1.5f * largestStep);
}

TEST_CASE ("Voicing lab: applying lab values and striking with them doesn't allocate", "[lab][engine][audio-thread]")
{
    Engine engine;
    engine.prepare (48000.0, 128);
    std::vector<float> left (128), right (128);

    lab::Settings s = with (Control::feltHardness, 80.0f);
    s.set (Control::strikePosition, Register::middle, 20.0f);
    s.set (Control::tapeDrive, Register::bass, 70.0f);

    const auto use = heapUseOnAudioThread ([&]
    {
        engine.setVoicingLab (s);
        engine.handleMidi ({ { 0x90, 60, 100 }, 3 });
        engine.handleMidi ({ { 0x90, 96, 100 }, 3 });
        for (int block = 0; block < 40; ++block)
            engine.render (left.data(), right.data(), 128);
        engine.setVoicingLab (lab::Settings {});
    });

    CHECK_FALSE (use.any());
}

TEST_CASE ("Voicing lab: the voicing file round-trips, and missing entries are 50", "[lab]")
{
    lab::Settings s;
    s.set (Control::feltHardness, Register::treble, 31.0f);
    s.set (Control::tapeDrive, Register::bass, 77.0f);

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", bridge::voicingFormat);
    root->setProperty ("version", bridge::voicingFormatVersion);
    root->setProperty ("controls", bridge::voicingControlsToVar (s));
    const auto text = juce::JSON::toString (juce::var (root));

    juce::String error;
    const auto read = bridge::parseVoicing (text, error);
    REQUIRE (read.has_value());
    CHECK (*read == s);

    const auto sparse = bridge::parseVoicing (R"({ "format": "forte-voicing", "controls": { "sustain": { "middle": 70 }, "unknown": 3 } })", error);
    REQUIRE (sparse.has_value());
    CHECK (sparse->get (Control::sustain, Register::middle) == 70.0f);
    CHECK (sparse->get (Control::sustain, Register::bass) == 50.0f);
    CHECK (sparse->isNeutral (Control::feltHardness));

    CHECK_FALSE (bridge::parseVoicing (R"({ "format": "something-else", "controls": {} })", error).has_value());
    CHECK_FALSE (bridge::parseVoicing (R"({ "format": "forte-voicing", "controls": { "tapeDrive": 140 } })", error).has_value());
}

TEST_CASE ("Voicing lab: the export reads back, says what the values mean, and keeps the last file", "[lab]")
{
    const Engine engine;
    lab::Settings s;
    s.set (Control::feltHardness, Register::bass, 100.0f);
    s.set (Control::tapeDrive, Register::bass, 0.0f);

    const auto text = makeVoicingFile (engine, s);
    juce::String error;
    const auto read = bridge::parseVoicing (text, error);
    REQUIRE (read.has_value());
    CHECK (*read == s);

    const auto root = juce::JSON::parse (text);
    CHECK_THAT (static_cast<double> (root["physical"]["feltHardness"]["bass"]), WithinRel (4.0, 1e-12));
    CHECK (static_cast<double> (root["physical"]["feltHardness"]["middle"]) == 1.0);
    CHECK (static_cast<double> (root["physical"]["tapeDrive"]) == 0.0);
    CHECK (static_cast<double> (root["physical"]["pickup"]["treble"]) == 0.5);
    CHECK (root["builtFrom"].toString().isNotEmpty());

    const juce::TemporaryFile folder;
    const auto target = folder.getFile().getChildFile ("Forte").getChildFile ("voicing.json");
    REQUIRE (exportVoicing (engine, lab::Settings {}, target).wasOk());
    REQUIRE (exportVoicing (engine, s, target).wasOk());

    CHECK (*bridge::parseVoicing (target.loadFileAsString(), error) == s);
    const auto previous = target.getSiblingFile ("voicing.previous.json");
    REQUIRE (previous.existsAsFile());
    CHECK (*bridge::parseVoicing (previous.loadFileAsString(), error) == lab::Settings {});
    folder.getFile().deleteRecursively();
}

namespace
{
/** Renders events from a fresh engine in phases: before each phase's start, `change` runs
    (with the engine and the phase's index), so a test can rebuild strings between them. */
template <typename Change>
Rendered renderPhases (const std::vector<TimedMidi>& events, const std::vector<std::int64_t>& phaseStarts, std::int64_t length, Change&& change)
{
    Engine engine;
    engine.prepare (48000.0, 128);

    Rendered out;
    for (std::size_t phase = 0; phase < phaseStarts.size(); ++phase)
    {
        const auto start = phaseStarts[phase];
        const auto end = phase + 1 < phaseStarts.size() ? phaseStarts[phase + 1] : length;
        change (engine, phase);

        std::vector<TimedMidi> inPhase;
        for (const auto& e : events)
            if (e.sample >= start && e.sample < end)
                inPhase.push_back ({ e.sample - start, e.message });

        const auto part = render (engine, inPhase, end - start);
        out.left.insert (out.left.end(), part.left.begin(), part.left.end());
        out.right.insert (out.right.end(), part.right.begin(), part.right.end());
    }
    return out;
}
} // namespace

TEST_CASE ("Voicing lab: string controls reach a note only when it next starts from rest", "[lab][engine][strings]")
{
    const auto control = GENERATE (Control::inharmonicity, Control::unisonSpread, Control::sustain,
                                   Control::overtoneDecay, Control::secondStage, Control::phantomLevel);
    const auto value = GENERATE (0.0f, 100.0f);
    INFO ("control " << lab::info (control).id << " at " << value);

    // Phantom partials are on the bass notes only. The change lands while the note rings; a
    // restrike while it still sounds keeps the old strings; once released and silent, the next
    // strike takes the new ones.
    const int note = control == Control::phantomLevel ? 40 : 84;
    const std::int64_t change = second / 4, ringingRestrike = second / 2, release = 6 * second / 10;
    const std::int64_t fromRest = 3 * second, length = 3 * second + second / 2;
    const std::vector<TimedMidi> events { noteOn (0, note, 100), noteOn (ringingRestrike, note, 100),
                                          noteOff (release, note), noteOff (release, note), noteOn (fromRest, note, 100) };

    const auto committed = renderFresh (events, length);
    const auto changed = renderPhases (events, { 0, change }, length, [&] (Engine& e, std::size_t phase)
    {
        if (phase == 1)
            REQUIRE (e.rebuildStrings (with (control, value)) > 0);
    });

    CHECK (slice (changed.left, 0, fromRest) == slice (committed.left, 0, fromRest));
    CHECK (slice (changed.right, 0, fromRest) == slice (committed.right, 0, fromRest));
    CHECK (slice (changed.left, fromRest, length) != slice (committed.left, fromRest, length));
}

TEST_CASE ("Voicing lab: strings back at 50 are the committed ones bit for bit", "[lab][engine][strings]")
{
    const std::vector<TimedMidi> notes { noteOn (0, 30, 90), noteOn (100, 60, 110), noteOn (5000, 96, 127) };
    const auto committed = renderFresh (notes, second);

    const auto out = renderPhases (notes, { 0 }, second, [] (Engine& e, std::size_t)
    {
        lab::Settings moved = with (Control::sustain, 10.0f);
        moved.set (Control::inharmonicity, Register::treble, 90.0f);
        moved.set (Control::phantomLevel, Register::bass, 100.0f);
        REQUIRE (e.rebuildStrings (moved) == 88);
        REQUIRE (e.rebuildStrings (lab::Settings {}) == 88);
    });

    CHECK (out.left == committed.left);
    CHECK (out.right == committed.right);
}

TEST_CASE ("Voicing lab: prepare() builds the lab's strings as a rebuild does", "[lab][engine][strings]")
{
    lab::Settings s = with (Control::overtoneDecay, 80.0f);
    s.set (Control::unisonSpread, Register::middle, 100.0f);
    const std::vector<TimedMidi> notes { noteOn (0, 45, 100), noteOn (0, 64, 100), noteOn (200, 88, 100) };

    Engine prepared;
    prepared.setVoicingLab (s);
    prepared.prepare (48000.0, 128);
    const auto a = render (prepared, notes, second / 2);

    const auto b = renderPhases (notes, { 0 }, second / 2, [&] (Engine& e, std::size_t) { e.rebuildStrings (s); });

    CHECK (a.left == b.left);
    CHECK (a.left != renderFresh (notes, second / 2).left);
}

TEST_CASE ("Voicing lab: inharmonicity keeps the first partial on pitch", "[lab][strings]")
{
    const auto model = detail::makeSeedPianoModel();

    for (double scale : { 0.5, 2.0 })
        for (const auto& p : model.notes)
        {
            detail::StringVoicing v;
            v.inharmonicityScale = scale;
            const auto q = detail::applyStringVoicing (p, v);
            CHECK_THAT (q.inharmonicity, WithinRel (p.inharmonicity * scale, 1e-12));
            CHECK_THAT (detail::partialFrequency (q, 1), WithinRel (detail::partialFrequency (p, 1), 1e-12));
        }
}

TEST_CASE ("Voicing lab: a note taking its new strings doesn't allocate", "[lab][engine][strings][audio-thread]")
{
    Engine engine;
    engine.prepare (48000.0, 128);
    REQUIRE (engine.rebuildStrings (with (Control::sustain, 80.0f)) > 0);
    REQUIRE (engine.hasIncomingStrings());
    std::vector<float> left (128), right (128);

    const auto use = heapUseOnAudioThread ([&]
    {
        engine.handleMidi ({ { 0x90, 60, 100 }, 3 });
        engine.handleMidi ({ { 0x90, 84, 100 }, 3 });
        for (int block = 0; block < 20; ++block)
            engine.render (left.data(), right.data(), 128);
    });

    CHECK_FALSE (use.any());
    engine.collectRetiredStrings();
}

TEST_CASE ("Voicing lab: longer sustain lengthens the reported tail", "[lab][engine][strings]")
{
    Engine engine;
    engine.prepare (48000.0, 128);
    const double committed = engine.getTailSeconds();

    engine.rebuildStrings (with (Control::sustain, 100.0f));
    CHECK (engine.getTailSeconds() > 1.5 * committed);

    engine.rebuildStrings (lab::Settings {});
    CHECK (engine.getTailSeconds() == committed);
}

TEST_CASE ("Voicing lab: strings rebuilt on another thread while notes play", "[lab][engine][strings]")
{
    // The handover under load: a worker rebuilding the treble's strings over and over while the
    // audio thread strikes, releases and renders. Nothing may crash, leak or go non-finite.
    Engine engine;
    engine.prepare (48000.0, 128);
    std::atomic<bool> done { false };

    std::thread worker ([&]
    {
        for (int i = 0; i < 12; ++i)
        {
            lab::Settings s;
            s.set (Control::sustain, Register::treble, static_cast<float> (10 * (i % 10)));
            s.set (Control::inharmonicity, Register::treble, static_cast<float> (100 - 8 * (i % 10)));
            engine.rebuildStrings (s);
        }
        engine.collectRetiredStrings();
        done = true;
    });

    std::vector<float> left (128), right (128);
    bool finite = true;
    for (int block = 0; ! done || block < 200; ++block)
    {
        const auto note = static_cast<std::uint8_t> (72 + (block % 30));
        if (block % 3 == 0)
            engine.handleMidi ({ { 0x90, note, 90 }, 3 });
        if (block % 3 == 2)
            engine.handleMidi ({ { 0x80, note, 0 }, 3 });

        engine.render (left.data(), right.data(), 128);
        finite = finite && allFinite (left) && allFinite (right);
    }

    worker.join();
    CHECK (finite);
}
