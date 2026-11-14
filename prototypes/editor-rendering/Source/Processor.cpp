// PROTOTYPE (throwaway)
#include "Processor.h"

#if FORTE_EDITOR_WEB
 #include "WebEditor.h"
#else
 #include "NativeEditor.h"
#endif

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& k : ids::knobs)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { k.id, 1 }, k.name,
                   juce::NormalisableRange<float> (k.lo, k.hi, 1.0f), k.def,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "soft", 1 }, "Soft pedal", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "perspective", 1 }, "Listening perspective",
                                                         juce::StringArray { "Microphones", "Pianist" }, 0));
    return l;
}

ProtoProcessor::ProtoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "STATE", makeLayout())
{
    // FORTE_PROTO_AUTOPLAY=1 plays a slow arpeggio so editor CPU can be measured without a human.
    autoplay = juce::SystemStats::getEnvironmentVariable ("FORTE_PROTO_AUTOPLAY", "0") == "1";
    uiAdvanced = juce::SystemStats::getEnvironmentVariable ("FORTE_PROTO_ADVANCED", "0") == "1";
    uiDark     = juce::SystemStats::getEnvironmentVariable ("FORTE_PROTO_DARK", "0") == "1";
}

void ProtoProcessor::prepareToPlay (double newSr, int) { sr = newSr; for (auto& v : voices) v.midi = -1; }

void ProtoProcessor::noteOn (int m, float vel)
{
    vel = std::pow (vel, 0.4f + value ("curve") / 100.0f * 1.4f);
    Voice* v = nullptr;
    for (auto& cand : voices) if (cand.midi == m) v = &cand;
    if (v == nullptr) for (auto& cand : voices) if (cand.midi < 0) { v = &cand; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& cand : voices) if (cand.age > v->age) v = &cand; }

    const float a4 = value ("a4"), stretch = value ("stretch") / 50.0f;
    const float f0 = a4 * std::pow (2.0f, (m - 69) / 12.0f) * std::pow (2.0f, railsback (m) * stretch / 1200.0f);
    const float B = inharm (m), tau = 10.0f * std::pow (0.955f, (float) (m - 21)) + 0.4f;
    const float tilt = juce::jmax (0.6f, 2.7f - 1.3f * value ("hardness") / 100.0f - 0.9f * vel - 0.6f * value ("brightness") / 100.0f);
    const bool soft = value ("soft") > 0.5f;

    *v = Voice {};
    v->midi = m; v->held = true; v->vel = vel;
    int n = 1, i = 0;
    for (; n <= 20; ++n)
    {
        const float fn = n * f0 * std::sqrt (1.0f + B * n * n);
        if (fn > 15000.0f || fn > sr * 0.45) break;
        float amp = std::pow (vel, 1.4f) / std::pow ((float) n, tilt) * 0.12f;
        if (soft) amp *= n > 2 ? 0.6f : 0.85f;
        const float t1 = 0.12f * tau / std::sqrt ((float) n), t2 = tau / std::pow ((float) n, 0.6f);
        for (int s : { -1, 1 })
        {
            auto& p = v->p[(size_t) i++];
            const double w = juce::MathConstants<double>::twoPi * fn * (1.0 + s * 0.00035) / sr;
            p.cr = (float) std::cos (w); p.sr = (float) std::sin (w);
            p.amp = p.initAmp = amp;
            p.t1Samples = (int) (t1 * sr);
            p.d1 = (float) std::pow (0.3, 1.0 / (t1 * sr));             // falls to 30% over t1
            p.d2 = (float) std::pow (1e-4 / 0.3, 1.0 / (t2 * sr));      // then to -80 dB over t2
        }
    }
    v->numParts = i;

    viz.lastMidi = m; viz.lastF0 = f0; viz.numParts = n - 1;
    for (int k = 0; k < n - 1; ++k) viz.partFreq[(size_t) k] = (k + 1) * f0 * std::sqrt (1.0f + B * (k + 1) * (k + 1));
}

void ProtoProcessor::damp (Voice& v)
{
    const float tc = 0.05f + value ("sustain") / 100.0f * 1.6f;
    v.damped = true; v.dampMul = (float) std::exp (-1.0 / (tc * sr));
}

void ProtoProcessor::noteOff (int m)
{
    for (auto& v : voices)
        if (v.midi == m && v.held)
        {
            v.held = false;
            if (m < 89 && value ("sustain") <= 85.0f) damp (v);   // top notes have no dampers; pedal down holds
        }
}

void ProtoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    if (autoplay)
    {
        autoplayCounter += n;
        if (autoplayCounter > (int) (sr * 0.3))
        {
            autoplayCounter = 0;
            static constexpr int pattern[] = { 36, 48, 55, 60, 64, 67, 72, 76, 79, 84, 79, 76, 72, 67, 64, 60, 55, 48 };
            const int m = pattern[autoplayStep++ % (int) std::size (pattern)];
            midi.addEvent (juce::MidiMessage::noteOn (1, m, (juce::uint8) 90), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, pattern[(autoplayStep + 15) % (int) std::size (pattern)]), 0);
        }
    }
    keyboard.processNextMidiBuffer (midi, 0, n, true);

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
        else if (msg.isSustainPedalOn())  state.getParameter ("sustain")->setValueNotifyingHost (1.0f);
        else if (msg.isSustainPedalOff()) state.getParameter ("sustain")->setValueNotifyingHost (0.0f);
    }

    const float sustain = value ("sustain");
    if (lastSustain > 85.0f && sustain <= 85.0f)
        for (auto& v : voices) if (v.midi >= 0 && ! v.held && ! v.damped && v.midi < 89) damp (v);
    lastSustain = sustain;

    const float gain = value ("volume") <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (value ("volume"));
    const float width = value ("width") / 100.0f;
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    std::array<float, 88> energy {};
    int active = 0;

    for (auto& v : voices)
    {
        if (v.midi < 0) continue;
        ++active;
        const float pan = juce::jlimit (-1.0f, 1.0f, (v.midi - 64.5f) / 43.5f) * width;   // bass left
        const float gl = gain * std::sqrt (0.5f * (1.0f - pan)), gr = gain * std::sqrt (0.5f * (1.0f + pan));
        float peak = 0;
        for (int s = 0; s < n; ++s)
        {
            float x = 0;
            for (int k = 0; k < v.numParts; ++k)
            {
                auto& p = v.p[(size_t) k];
                const float c = p.c * p.cr - p.s * p.sr, si = p.c * p.sr + p.s * p.cr;
                p.c = c; p.s = si;
                p.amp *= (p.t1Samples-- > 0 ? p.d1 : p.d2);
                x += si * p.amp;
            }
            if (v.damped) { v.damp *= v.dampMul; x *= v.damp; }
            L[s] += x * gl; R[s] += x * gr;
        }
        for (int k = 0; k < v.numParts; ++k)   // renormalise rotators against drift
        {
            auto& p = v.p[(size_t) k];
            const float mag = std::sqrt (p.c * p.c + p.s * p.s);
            p.c /= mag; p.s /= mag;
            peak = juce::jmax (peak, p.amp / juce::jmax (1e-6f, p.initAmp) * (v.damped ? v.damp : 1.0f));
        }
        energy[(size_t) (v.midi - 21)] = juce::jmax (energy[(size_t) (v.midi - 21)], v.vel * peak);
        v.age += n;
        if (peak < 1e-4f || (v.damped && v.damp < 1e-4f)) v.midi = -1;

        if (v.midi == viz.lastMidi.load())
            for (int k = 0; k < v.numParts / 2 && k < 20; ++k)
                viz.partAmp[(size_t) k] = (v.p[(size_t) (2 * k)].amp + v.p[(size_t) (2 * k + 1)].amp) * (v.damped ? v.damp : 1.0f);
    }
    for (size_t k = 0; k < 88; ++k) viz.energy[k] = energy[k];
    viz.activeVoices = active;
}

void ProtoProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void ProtoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ProtoProcessor::createEditor()
{
    editorOpenedMs = juce::Time::currentTimeMillis();
#if FORTE_EDITOR_WEB
    return new WebEditor (*this);
#else
    return new NativeEditor (*this);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ProtoProcessor(); }
