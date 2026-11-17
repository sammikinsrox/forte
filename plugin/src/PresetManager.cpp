#include "PresetManager.h"

namespace forte
{
using namespace presets;

const juce::Identifier PresetManager::stateType { "PRESET" };

namespace
{
const juce::Identifier nameAttribute { "name" };
const juce::Identifier factoryAttribute { "factory" };

juce::RangedAudioParameter& parameterFor (juce::AudioProcessorValueTreeState& parameters, const Field& f)
{
    auto* p = parameters.getParameter (f.id);
    jassert (p != nullptr);
    return *p;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& p, juce::File folder)
    : parameters (p), lib (std::move (folder))
{
}

Id PresetManager::current() const
{
    const juce::ScopedLock locked (lock);
    return id;
}

Values PresetManager::liveValues() const
{
    Values v;
    for (const auto& f : fields)
        v.*f.member = juce::roundToInt (parameters.getRawParameterValue (f.id)->load (std::memory_order_relaxed));
    return v;
}

void PresetManager::applyRoom (const presets::Room& room)
{
    apply (presets::withRoom (liveValues(), room));
}

bool PresetManager::isModified() const
{
    const auto live = liveValues();
    const juce::ScopedLock locked (lock);
    return live != loaded;
}

juce::String PresetManager::displayName() const
{
    return current().name + (isModified() ? "*" : "");
}

void PresetManager::apply (const Values& values)
{
    for (const auto& f : fields)
    {
        auto& p = parameterFor (parameters, f);
        const float target = p.convertTo0to1 (static_cast<float> (values.*f.member));
        if (p.getValue() == target)
            continue;
        p.beginChangeGesture();
        p.setValueNotifyingHost (target);
        p.endChangeGesture();
    }
}

bool PresetManager::select (const Id& wanted)
{
    const auto values = lib.load (wanted);
    if (! values.has_value())
        return false;

    {
        const juce::ScopedLock locked (lock);
        id = wanted;
        loaded = *values;
    }
    apply (*values);
    return true;
}

bool PresetManager::step (int delta)
{
    lib.rescan();  // the folder may have changed since (Explorer, another instance)
    const auto from = current();

    // Skip a stop that's gone (a file deleted outside Forte) rather than sticking on it.
    auto next = lib.step (from, delta);
    for (std::size_t tries = 0; tries < lib.all().size(); ++tries)
    {
        if (select (next))
            return true;
        next = lib.step (next, delta);
    }
    return false;
}

juce::Result PresetManager::save()
{
    const auto now = current();
    if (now.factory)
        return juce::Result::fail ("Factory presets are read-only: use Save as");

    const auto live = liveValues();
    const auto result = lib.save (now.name, live);
    if (result.wasOk())
    {
        const juce::ScopedLock locked (lock);
        loaded = live;
    }
    return result;
}

juce::Result PresetManager::saveAs (const juce::String& name)
{
    const auto live = liveValues();
    const auto result = lib.save (name, live);
    if (result.wasOk())
    {
        const juce::ScopedLock locked (lock);
        id = { false, name };
        loaded = live;
    }
    return result;
}

juce::Result PresetManager::renameCurrent (const juce::String& to)
{
    const auto now = current();
    if (now.factory)
        return juce::Result::fail ("Factory presets can't be renamed");

    const auto result = lib.rename (now.name, to);
    if (result.wasOk())
    {
        // The file holds the values as last saved, which is what `*` compares against.
        const juce::ScopedLock locked (lock);
        id.name = to;
    }
    return result;
}

juce::Result PresetManager::deleteCurrent()
{
    const auto now = current();
    if (now.factory)
        return juce::Result::fail ("Factory presets can't be deleted");
    return lib.remove (now.name);
}

juce::ValueTree PresetManager::toState() const
{
    const juce::ScopedLock locked (lock);
    juce::ValueTree tree (stateType);
    tree.setProperty (nameAttribute, id.name, nullptr);
    tree.setProperty (factoryAttribute, id.factory, nullptr);
    for (const auto& f : fields)
        tree.setProperty (f.id, loaded.*f.member, nullptr);
    return tree;
}

void PresetManager::fromState (const juce::ValueTree& tree)
{
    Id restored;
    Values values = factoryPresets().front().values;

    if (tree.isValid() && tree.hasType (stateType) && tree.getProperty (nameAttribute).toString().isNotEmpty())
    {
        restored = { static_cast<bool> (tree.getProperty (factoryAttribute, true)), tree.getProperty (nameAttribute).toString() };

        // A factory preset's values may have changed since the project was saved: then `*`
        // compares against the ones it has now, which the arrows would load.
        if (restored.factory)
        {
            if (const auto now = lib.load (restored); now.has_value())
                values = *now;
            else
                restored = {};
        }
        else
        {
            for (const auto& f : fields)
                values.*f.member = static_cast<int> (tree.getProperty (f.id, values.*f.member));
            values = clamped (values);
        }
    }

    const juce::ScopedLock locked (lock);
    id = restored;
    loaded = values;
}
} // namespace forte
