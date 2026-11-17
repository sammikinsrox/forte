// The hot loops' two instruction sets (SPEC §9.2, engine/src/Kernels.h): the engine picks the
// best the CPU has, each set renders deterministically and independently of block size, and
// the two agree to within float rounding. CI's runners have AVX2, so the SSE2 path is only
// exercised where these tests force it.

#include "support/AudioThreadGuard.h"
#include "support/EngineRender.h"

#include "Kernels.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <iterator>
#include <random>
#include <vector>

using namespace forte;
using namespace forte::test;
using detail::Kernels;

namespace
{
constexpr std::int64_t second = 48000;

/** Every instruction set this CPU can run. */
std::vector<InstructionSet> supportedSets()
{
    std::vector<InstructionSet> sets { InstructionSet::sse2 };
    if (Engine::cpuSupports (InstructionSet::avx2))
        sets.push_back (InstructionSet::avx2);
    return sets;
}

const Kernels& kernelsFor (InstructionSet set)
{
    return set == InstructionSet::avx2 ? detail::avx2Kernels : detail::sse2Kernels;
}

const char* nameOf (InstructionSet set)
{
    return set == InstructionSet::avx2 ? "AVX2" : "SSE2";
}

Rendered renderWith (InstructionSet set, const std::vector<TimedMidi>& events, std::int64_t length, int blockSize = 128)
{
    Engine engine;
    engine.setInstructionSet (set);
    engine.prepare (48000.0, blockSize);
    return render (engine, events, length, blockSize);
}

/** Decaying modes with random phases, as a ringing note's are. */
struct Modes
{
    explicit Modes (int count, unsigned seed)
    {
        std::mt19937 random (seed);
        std::uniform_real_distribution<float> unit (-1.0f, 1.0f);
        for (int i = 0; i < count; ++i)
        {
            const float radius = 0.9995f - 0.0004f * std::abs (unit (random));
            const float angle = 3.0f * std::abs (unit (random));
            re.push_back (unit (random));
            im.push_back (unit (random));
            mr.push_back (radius * std::cos (angle));
            mi.push_back (radius * std::sin (angle));
            w.push_back (0.01f * unit (random));
            dr.push_back (unit (random));
            di.push_back (unit (random));
        }
    }

    std::vector<float> re, im, mr, mi, w, dr, di;
};

const std::vector<TimedMidi> passage {
    controller (0, 64, 127),
    noteOn (0, 21, 110), noteOn (0, 33, 90), noteOn (40, 45, 70), noteOn (300, 60, 100),
    noteOn (700, 72, 64), noteOn (701, 84, 120), noteOn (2000, 96, 80), noteOn (2500, 108, 127),
    noteOff (6000, 60), controller (9000, 64, 70), noteOn (9100, 60, 50),  // half pedal, restrike
    controller (12000, 67, 127), noteOn (12001, 48, 90),                   // soft pedal
};
} // namespace

TEST_CASE ("The engine runs on AVX2 when the CPU has it, and SSE2 otherwise", "[isa]")
{
    const Engine engine;
    CHECK (Engine::cpuSupports (InstructionSet::sse2));
    CHECK (engine.getInstructionSet() == (Engine::cpuSupports (InstructionSet::avx2) ? InstructionSet::avx2 : InstructionSet::sse2));
    CHECK (&detail::bestKernels() == &kernelsFor (engine.getInstructionSet()));

    Engine forced;
    forced.setInstructionSet (InstructionSet::sse2);
    CHECK (forced.getInstructionSet() == InstructionSet::sse2);
    forced.setInstructionSet (InstructionSet::avx2);
    CHECK (forced.getInstructionSet() == engine.getInstructionSet());
}

TEST_CASE ("Each kernel set computes the plain-loop result", "[isa][kernels]")
{
    const auto set = GENERATE_COPY (from_range (supportedSets()));
    const Kernels& k = kernelsFor (set);
    INFO (nameOf (set));

    constexpr int count = 56;  // whole groups of 8, not of 32: both of freeModes's loops
    constexpr int samples = 100; // more than one 64-sample chunk

    SECTION ("freeModes")
    {
        Modes kernel (count, 1), plain (count, 1);
        std::vector<float> out (samples, 0.5f), expected (samples, 0.5f);

        k.freeModes (kernel.re.data(), kernel.im.data(), kernel.mr.data(), kernel.mi.data(), kernel.w.data(), count, out.data(), samples);

        for (int t = 0; t < samples; ++t)
        {
            double sum = 0.0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const float r = plain.re[i] * plain.mr[i] - plain.im[i] * plain.mi[i];
                const float m = plain.re[i] * plain.mi[i] + plain.im[i] * plain.mr[i];
                plain.re[i] = r;
                plain.im[i] = m;
                sum += plain.w[i] * m;
            }
            expected[static_cast<std::size_t> (t)] += static_cast<float> (sum);
        }

        for (std::size_t t = 0; t < samples; ++t)
            CHECK (std::abs (out[t] - expected[t]) < 1e-5f);
        for (std::size_t i = 0; i < count; ++i)
        {
            CHECK (std::abs (kernel.re[i] - plain.re[i]) < 1e-4f);
            CHECK (std::abs (kernel.im[i] - plain.im[i]) < 1e-4f);
        }
    }

    SECTION ("stepModes, driveModes, viewDot, dot")
    {
        Modes a (count, 2), b (count, 2);

        double expectedStep = 0.0, expectedView = 0.0, expectedDot = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            expectedView += b.dr[i] * b.im[i] + b.di[i] * b.re[i];
            expectedDot += b.dr[i] * b.di[i];
            const float r = b.re[i] * b.mr[i] - b.im[i] * b.mi[i];
            const float m = b.re[i] * b.mi[i] + b.im[i] * b.mr[i];
            b.re[i] = r;
            b.im[i] = m;
            expectedStep += b.w[i] * m;
        }

        CHECK (std::abs (k.viewDot (a.dr.data(), a.di.data(), a.re.data(), a.im.data(), count) - expectedView) < 1e-4);
        CHECK (std::abs (k.dot (a.dr.data(), a.di.data(), count) - expectedDot) < 1e-4);
        CHECK (std::abs (k.stepModes (a.re.data(), a.im.data(), a.mr.data(), a.mi.data(), a.w.data(), count) - expectedStep) < 1e-5);

        Modes c (count, 3), d (count, 3);
        k.driveModes (c.re.data(), c.im.data(), c.mr.data(), c.mi.data(), c.dr.data(), c.di.data(), 0.25f, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            CHECK (std::abs (c.re[i] - (d.re[i] * d.mr[i] - d.im[i] * d.mi[i] + d.dr[i] * 0.25f)) < 1e-5f);
            CHECK (std::abs (c.im[i] - (d.re[i] * d.mi[i] + d.im[i] * d.mr[i] + d.di[i] * 0.25f)) < 1e-5f);
        }
    }
}

TEST_CASE ("freeModes gives the same bits however the samples are split", "[isa][kernels][determinism]")
{
    const auto set = GENERATE_COPY (from_range (supportedSets()));
    const Kernels& k = kernelsFor (set);
    INFO (nameOf (set));

    constexpr int count = 72;
    Modes whole (count, 4), split (count, 4);
    std::vector<float> a (130, 0.0f), b (130, 0.0f);

    k.freeModes (whole.re.data(), whole.im.data(), whole.mr.data(), whole.mi.data(), whole.w.data(), count, a.data(), 130);
    const int bounds[] { 0, 1, 33, 64, 97, 130 };
    for (std::size_t i = 0; i + 1 < std::size (bounds); ++i)
        k.freeModes (split.re.data(), split.im.data(), split.mr.data(), split.mi.data(), split.w.data(), count,
                     b.data() + bounds[i], bounds[i + 1] - bounds[i]);

    CHECK (a == b);
    CHECK (whole.re == split.re);
    CHECK (whole.im == split.im);
}

TEST_CASE ("Each instruction set renders bit-identically whatever the block size", "[isa][tier1][determinism]")
{
    const auto set = GENERATE_COPY (from_range (supportedSets()));
    INFO (nameOf (set));

    const auto reference = renderWith (set, passage, second / 2, 128);
    REQUIRE_FALSE (allZero (reference.left));

    for (int blockSize : { 1, 37, 512 })
    {
        INFO ("block size " << blockSize);
        CHECK (renderWith (set, passage, second / 2, blockSize).left == reference.left);
    }
}

TEST_CASE ("SSE2 and AVX2 render the same piano, to within float rounding", "[isa]")
{
    if (! Engine::cpuSupports (InstructionSet::avx2))
        SKIP ("this CPU has no AVX2");

    const auto sse2 = renderWith (InstructionSet::sse2, passage, second);
    const auto avx2 = renderWith (InstructionSet::avx2, passage, second);
    REQUIRE (allFinite (sse2.left));
    REQUIRE (allFinite (avx2.left));

    std::vector<float> difference (sse2.left.size());
    for (std::size_t i = 0; i < difference.size(); ++i)
        difference[i] = sse2.left[i] - avx2.left[i];

    // −80 dB under the signal: rounding, not a different sound.
    const double signal = rms (sse2.left, 0, second);
    INFO ("difference " << 20.0 * std::log10 (rms (difference, 0, second) / signal) << " dB");
    CHECK (rms (difference, 0, second) < 1e-4 * signal);
}

TEST_CASE ("The SSE2 path never touches the heap on the audio thread", "[isa][audio-thread]")
{
    Engine engine;
    engine.setInstructionSet (InstructionSet::sse2);
    engine.prepare (48000.0, 512);

    std::vector<float> left (512), right (512);
    const auto use = heapUseOnAudioThread ([&]
    {
        for (const auto& e : passage)
        {
            MidiEvent event;
            const auto* raw = e.message.getRawData();
            event.size = static_cast<std::uint8_t> (e.message.getRawDataSize());
            for (int i = 0; i < event.size && i < 3; ++i)
                event.bytes[static_cast<std::size_t> (i)] = raw[i];
            engine.handleMidi (event);
            engine.render (left.data(), right.data(), 512);
        }
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}
