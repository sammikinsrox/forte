// Presets (SPEC §7.1): their contents, the factory list, the user files and the arrows' order.

#include "support/EngineRender.h"

#include "Presets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <set>

using namespace forte;
using namespace forte::presets;
using namespace forte::test;

namespace
{
/** An empty folder of its own, deleted afterwards. */
struct TemporaryFolder
{
    TemporaryFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("forte-preset-test", {}, false))
    {
        folder.createDirectory();
    }

    ~TemporaryFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

std::vector<juce::String> namesOf (const Library& library)
{
    std::vector<juce::String> names;
    for (const auto& id : library.all())
        names.push_back ((id.factory ? "F:" : "U:") + id.name);
    return names;
}

constexpr std::int64_t second = 48000;
} // namespace

TEST_CASE ("Concert Grand comes first and is every control at its default", "[presets]")
{
    const auto& factory = factoryPresets();
    REQUIRE (factory.size() >= 5);
    REQUIRE (factory.size() <= 8);
    CHECK (juce::String (factory.front().name) == "Concert Grand");
    CHECK (factory.front().values == Values {});

    // The defaults are the engine's own (SPEC §6.3).
    CHECK (applyTo (Values {}, Controls {}) == Controls {});
    CHECK (perspectiveOf (Values {}) == ListeningPerspective::microphones);
}

TEST_CASE ("Factory presets have unique names and values in range", "[presets]")
{
    std::set<juce::String> names;
    for (const auto& p : factoryPresets())
    {
        INFO (p.name);
        CHECK (names.insert (p.name).second);
        CHECK (clamped (p.values) == p.values);
    }
}

TEST_CASE ("A preset holds the 8 character controls, never Volume or A4", "[presets]")
{
    Controls start;
    start.volumeDb = -12.0;
    start.tuning.a4Hz = 432.0;

    const Values v { 10, 20, 30, 40, 0, 60, 70, 1 };
    const auto c = applyTo (v, start);
    CHECK (c.hardness == 10.0);
    CHECK (c.brightness == 20.0);
    CHECK (c.resonance == 30.0);
    CHECK (c.touch == 40.0);
    CHECK (c.tuning.stretch == 0.0);
    CHECK (c.tuning.spread == 60.0);
    CHECK (c.width == 70.0);
    CHECK (perspectiveOf (v) == ListeningPerspective::pianist);

    CHECK (c.volumeDb == -12.0);
    CHECK (c.tuning.a4Hz == 432.0);
}

TEST_CASE ("A preset file round-trips, with its format, model and name", "[presets]")
{
    const Values v { 1, 2, 3, 4, 5, 6, 7, 1 };
    const auto text = toJson ("Late Night", v);

    const auto parsed = juce::JSON::parse (text);
    CHECK (parsed["format"].toString() == fileFormat);
    CHECK (static_cast<int> (parsed["version"]) == fileFormatVersion);
    CHECK (parsed["model"].toString() == modelId);
    CHECK (parsed["name"].toString() == "Late Night");

    const auto read = fromJson (text);
    REQUIRE (read.has_value());
    CHECK (*read == v);
}

TEST_CASE ("A preset file's missing values are defaults, and out-of-range ones are clamped", "[presets]")
{
    const auto read = fromJson (R"({ "format": "forte-preset", "version": 9,
                                     "values": { "hardness": 130, "width": -4, "perspective": 3,
                                                 "brightness": 61.6, "touch": "loud", "volume": -20 } })");
    REQUIRE (read.has_value());
    CHECK (read->hardness == 100);
    CHECK (read->width == 0);
    CHECK (read->perspective == 1);
    CHECK (read->brightness == 62);
    CHECK (read->touch == 50);
    CHECK (read->resonance == 50);

    CHECK_FALSE (fromJson ("not json").has_value());
    CHECK_FALSE (fromJson (R"({ "format": "forte-voicing", "values": {} })").has_value());
}

TEST_CASE ("The arrows step through factory presets, then user ones alphabetically, and wrap", "[presets]")
{
    TemporaryFolder temp;
    Library library (temp.folder);

    REQUIRE (library.save ("zebra", {}).wasOk());
    REQUIRE (library.save ("Apple", { 60, 50, 50, 50, 50, 50, 100, 0 }).wasOk());
    REQUIRE (library.save ("banana", {}).wasOk());
    temp.folder.getChildFile ("notes.txt").replaceWithText ("not a preset");
    temp.folder.getChildFile ("broken.fortepreset").replaceWithText ("{");

    library.rescan();
    const auto names = namesOf (library);
    const auto numFactory = factoryPresets().size();
    REQUIRE (names.size() == numFactory + 3);
    CHECK (names[0] == "F:Concert Grand");
    CHECK (names[numFactory] == "U:Apple");
    CHECK (names[numFactory + 1] == "U:banana");
    CHECK (names[numFactory + 2] == "U:zebra");

    const Id first { true, "Concert Grand" };
    const Id last { false, "zebra" };
    CHECK (library.step (first, 1) == Id { true, factoryPresets()[1].name });
    CHECK (library.step (first, -1) == last);
    CHECK (library.step (last, 1) == first);
    CHECK (library.step (Id { true, factoryPresets().back().name }, 1) == Id { false, "Apple" });

    // From a preset that isn't listed, › goes to the first and ‹ to the last.
    CHECK (library.step (Id { false, "deleted" }, 1) == first);
    CHECK (library.step (Id { false, "deleted" }, -1) == last);

    REQUIRE (library.load ({ false, "Apple" }).has_value());
    CHECK (library.load ({ false, "Apple" })->hardness == 60);
    CHECK_FALSE (library.load ({ false, "deleted" }).has_value());
}

TEST_CASE ("User presets save, rename (even by case only) and delete", "[presets]")
{
    TemporaryFolder temp;
    Library library (temp.folder.getChildFile ("Presets"));  // created on the first save

    const Values v { 33, 50, 50, 50, 50, 50, 100, 1 };
    REQUIRE (library.save ("warm", v).wasOk());
    CHECK (library.userPresetExists ("warm"));

    REQUIRE (library.rename ("warm", "Warm").wasOk());
    CHECK (library.userNames() == std::vector<juce::String> { "Warm" });
    REQUIRE (library.load ({ false, "Warm" }).has_value());
    CHECK (*library.load ({ false, "Warm" }) == v);
    CHECK (juce::JSON::parse (library.fileFor ("Warm").loadFileAsString())["name"].toString() == "Warm");

    REQUIRE (library.rename ("Warm", "Evening").wasOk());
    CHECK (library.userNames() == std::vector<juce::String> { "Evening" });
    CHECK (*library.load ({ false, "Evening" }) == v);

    REQUIRE (library.remove ("Evening").wasOk());
    CHECK (library.userNames().empty());
    CHECK (temp.folder.getChildFile ("Presets").getNumberOfChildFiles (juce::File::findFiles) == 0);

    CHECK (library.save ("a/b", v).failed());
    CHECK (library.save ("", v).failed());
}

TEST_CASE ("Typed preset names are made safe as file names", "[presets]")
{
    CHECK (Library::cleanName ("  Late Night  ") == "Late Night");
    CHECK (Library::cleanName ("a/b:c?") == "abc");
    CHECK (Library::cleanName ("trailing. ") == "trailing");
    CHECK (Library::cleanName ("???").isEmpty());
}

TEST_CASE ("A preset change while notes ring causes no level drop and no click", "[tier1][presets]")
{
    // SPEC §2.9, §8.1: loading any factory preset mid-chord, pedal down, from Concert Grand.
    // (A perspective change crossfades the IR sets; the soundboard's own tests cover that.)
    const auto& preset = factoryPresets()[static_cast<std::size_t> (GENERATE (range (1, static_cast<int> (factoryPresets().size()))))];
    INFO (preset.name);

    const std::vector<TimedMidi> chord { controller (0, 64, 127), noteOn (0, 33, 100), noteOn (0, 52, 90),
                                         noteOn (0, 60, 80), noteOn (0, 76, 80), noteOn (0, 91, 70) };
    constexpr std::int64_t at = second / 4, length = second;

    forte::Engine engine;
    engine.prepare (48000.0, 128);
    auto changed = render (engine, chord, at);
    engine.setControls (applyTo (preset.values, engine.getControls()));
    engine.setListeningPerspective (perspectiveOf (preset.values));
    const auto rest = render (engine, {}, length - at);
    changed.left.insert (changed.left.end(), rest.left.begin(), rest.left.end());

    const auto steady = renderFresh (chord, length).left;

    float changedStep = 0.0f, steadyStep = 0.0f;
    for (auto i = at - 1000; i < at + 6000; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        changedStep = std::max (changedStep, std::abs (changed.left[n] - changed.left[n - 1]));
        steadyStep = std::max (steadyStep, std::abs (steady[n] - steady[n - 1]));
    }

    REQUIRE (allFinite (changed.left));
    CHECK (changedStep <= 1.5f * steadyStep);
    CHECK (rms (changed.left, at, length) > 0.7 * rms (steady, at, length));
}
