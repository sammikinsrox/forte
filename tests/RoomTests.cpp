// The room (SPEC §2.11): early reflections and the late tail after the tape.

#include "support/AudioThreadGuard.h"

#include "Room.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace forte::detail;
using forte::RoomControls;
using forte::test::heapUseOnAudioThread;

namespace
{
struct Stereo
{
    std::vector<float> left, right;
};

Stereo noiseBurst (std::size_t length, std::size_t burst, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> normal (0.0f, 0.1f);
    Stereo s { std::vector<float> (length), std::vector<float> (length) };
    for (std::size_t i = 0; i < burst; ++i)
    {
        s.left[i] = normal (rng);
        s.right[i] = normal (rng);
    }
    return s;
}

/** The room's response (output minus input) to a one-sample click in both channels. */
Stereo impulseResponse (const RoomControls& controls, double rate, std::size_t length)
{
    Room room;
    room.setControls (controls, true);
    room.prepare (rate);
    Stereo s { std::vector<float> (length), std::vector<float> (length) };
    s.left[0] = s.right[0] = 1.0f;
    room.process (s.left.data(), s.right.data(), static_cast<int> (length));
    s.left[0] -= 1.0f;
    s.right[0] -= 1.0f;
    return s;
}

/** RT60 from the Schroeder backward integral's slope between −5 and −35 dB. */
double rt60 (const std::vector<float>& x, double rate)
{
    std::vector<double> energy (x.size());
    double sum = 0.0;
    for (std::size_t i = x.size(); i-- > 0;)
        energy[i] = (sum += static_cast<double> (x[i]) * x[i]);

    const auto when = [&] (double db)
    {
        const double level = energy[0] * std::pow (10.0, db / 10.0);
        for (std::size_t i = 0; i < energy.size(); ++i)
            if (energy[i] < level)
                return static_cast<double> (i) / rate;
        return static_cast<double> (energy.size()) / rate;
    };
    return 2.0 * (when (-35.0) - when (-5.0));
}

double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = from; i < a.size(); ++i)
    {
        ab += static_cast<double> (a[i]) * b[i];
        aa += static_cast<double> (a[i]) * a[i];
        bb += static_cast<double> (b[i]) * b[i];
    }
    return ab / std::sqrt (aa * bb);
}
} // namespace

TEST_CASE ("Room 0 leaves the piano untouched", "[room]")
{
    RoomControls c;
    c.amount = 0.0;
    Room room;
    room.setControls (c, true);
    room.prepare (48000.0);

    auto s = noiseBurst (20000, 20000);
    const auto dry = s;
    room.process (s.left.data(), s.right.data(), 20000);
    CHECK (s.left == dry.left);
    CHECK (s.right == dry.right);
}

TEST_CASE ("The tail decays at the Decay setting", "[room]")
{
    const double decay = GENERATE (0.6, 1.5, 3.0);
    const double rate = GENERATE (44100.0, 96000.0);
    RoomControls c;
    c.amount = 100.0;
    c.decaySeconds = decay;
    c.damping = 0.0;  // the highs as long as the lows, so the broadband slope is the setting's
    c.early = 0.0;

    const auto ir = impulseResponse (c, rate, static_cast<std::size_t> (rate * decay * 2.0));
    const double measured = rt60 (ir.left, rate);
    INFO ("decay " << decay << " s at " << rate << " Hz: measured " << measured << " s");
    CHECK (measured > 0.8 * decay);
    CHECK (measured < 1.2 * decay);
}

TEST_CASE ("Damping shortens the highs", "[room]")
{
    RoomControls c;
    c.amount = 100.0;
    c.decaySeconds = 2.0;
    c.early = 0.0;

    const auto highShare = [&] (double damping)
    {
        c.damping = damping;
        const auto ir = impulseResponse (c, 48000.0, 96000);
        // The share of the second half's energy above ~6 kHz: a first difference weights it.
        double all = 0.0, high = 0.0;
        for (std::size_t i = 48001; i < ir.left.size(); ++i)
        {
            const double d = ir.left[i] - ir.left[i - 1];
            all += static_cast<double> (ir.left[i]) * ir.left[i];
            high += d * d;
        }
        return high / all;
    };

    CHECK (highShare (90.0) < 0.5 * highShare (0.0));
}

TEST_CASE ("The room's left and right are decorrelated", "[room]")
{
    RoomControls c;
    c.amount = 100.0;
    const auto ir = impulseResponse (c, 48000.0, 48000);
    // From 50 ms, after the early reflections.
    CHECK (std::abs (correlation (ir.left, ir.right, 2400)) < 0.3);
}

TEST_CASE ("The room doesn't depend on how blocks are split, gliding or not", "[room]")
{
    const auto run = [] (int block)
    {
        Room room;
        RoomControls c;
        c.amount = 60.0;
        room.setControls (c, true);
        room.prepare (48000.0);
        auto s = noiseBurst (48000, 4000);
        for (int at = 0; at < 48000;)
        {
            if (at >= 10000 && at < 10000 + block)
            {
                c.size = 80.0;
                c.decaySeconds = 2.5;
                c.amount = 30.0;
                c.predelayMs = 30.0;
                room.setControls (c, false);
            }
            const int n = std::min (block, 48000 - at);
            room.process (s.left.data() + at, s.right.data() + at, n);
            at += n;
        }
        return s;
    };

    // The controls change at the first block boundary at or after sample 10000: 10000 is a
    // multiple of every block size here, so all runs change them at the same sample.
    const auto reference = run (10000);
    for (int block : { 1, 16, 125, 400 })
    {
        const auto s = run (block);
        INFO ("block " << block);
        CHECK (s.left == reference.left);
        CHECK (s.right == reference.right);
    }
}

TEST_CASE ("The room stays finite and bounded at its extremes", "[room]")
{
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);
    for (double size : { 0.0, 100.0 })
        for (double damping : { 0.0, 100.0 })
            for (double predelay : { 0.0, 100.0 })
            {
                RoomControls c { 100.0, size, 10.0, damping, predelay, 100.0 };
                Room room;
                room.setControls (c, true);
                room.prepare (rate);
                auto s = noiseBurst (static_cast<std::size_t> (rate * 3), static_cast<std::size_t> (rate));
                room.process (s.left.data(), s.right.data(), static_cast<int> (s.left.size()));
                float peak = 0.0f;
                bool finite = true;
                for (float v : s.left)
                {
                    finite = finite && std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                }
                INFO ("size " << size << ", damping " << damping << ", pre-delay " << predelay << " at " << rate);
                CHECK (finite);
                CHECK (peak < 10.0f);
            }
}

TEST_CASE ("The room sleeps after its tail, and wakes cleared", "[room]")
{
    RoomControls c;
    c.amount = 50.0;
    c.decaySeconds = 0.5;
    Room room;
    room.setControls (c, true);
    room.prepare (48000.0);

    const auto tail = static_cast<std::size_t> (Room::tailSeconds (c) * 48000.0);
    auto s = noiseBurst (tail + 48000, 2000);
    room.process (s.left.data(), s.right.data(), static_cast<int> (s.left.size()));

    // Ringing within the tail, silent (exactly the zero input) after it.
    CHECK (std::any_of (s.left.begin() + 4000, s.left.begin() + 8000, [] (float v) { return v != 0.0f; }));
    CHECK (std::all_of (s.left.end() - 24000, s.left.end(), [] (float v) { return v == 0.0f; }));

    // The same burst again gives the same response as the first time.
    auto again = noiseBurst (48000, 2000);
    auto first = noiseBurst (48000, 2000);
    Room fresh;
    fresh.setControls (c, true);
    fresh.prepare (48000.0);
    room.process (again.left.data(), again.right.data(), 48000);
    fresh.process (first.left.data(), first.right.data(), 48000);
    CHECK (again.left == first.left);
}

TEST_CASE ("The room doesn't allocate on the audio thread", "[room][realtime]")
{
    Room room;
    RoomControls c;
    c.amount = 40.0;
    room.setControls (c, true);
    room.prepare (48000.0);
    auto s = noiseBurst (20000, 5000);

    const auto use = heapUseOnAudioThread ([&]
    {
        for (int at = 0; at < 20000; at += 100)
        {
            if (at == 5000)
            {
                c.size = 90.0;
                room.setControls (c, false);
            }
            room.process (s.left.data() + at, s.right.data() + at, 100);
        }
        room.reset();
    });

    CHECK (use.allocations == 0);
    CHECK (use.deallocations == 0);
}
