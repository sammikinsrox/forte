"""Writes the seed soundboard IRs in data/ir/ (SPEC §2.8, §3.5). They stand in for the calibrated
IRs of SPEC §3.4 until the fitting pipeline makes them, in the same shipped form: 32-bit float WAV
at 48 kHz, stereo, one per bridge region and Listening perspective, 20000 taps (~0.42 s).

    python tools/seed-irs/make_seed_irs.py            (numpy; output is byte-identical every run)

Each IR is the listening-test prototype's modal-noise generator (soundboard_ir() in
prototypes/listening-test/forte_proto.py), copied below with only the rate and length made
arguments. The prototype is throwaway, so the generator lives here; --check confirms the copy
still matches it. The prototype seeded each region with its number, 1-4; Microphones keeps those
seeds, so it is the soundboard the listening test heard (without its fitted colour curve), and
Pianist uses 5-8. That makes the two perspectives audibly different soundboards, which is all a
seed needs: §3.4 replaces both with one generator and two calibrated colours.

On top of the generator, for each IR:
- Stereo placement (SPEC §2.8: placement comes from the regions, bass on the left, in both
  perspectives): constant-power channel gains from where the region sits along the keyboard,
  narrower for Microphones than for Pianist. The calibrated IRs will carry their own placement.
- A 100 ms raised-cosine fade to zero at the end, so truncating the slow low modes at 20000 taps
  doesn't click.
- One gain per perspective, so the four regions average unity power gain per octave over
  100 Hz-1 kHz, where most of a piano's energy is. The soundboard then leaves a piano's loudness
  roughly as the bridge force had it (unit broadband energy instead made a pedalled glissando
  6 dB louder, the modes being strongest in that band), and both perspectives play equally loud.
"""

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "data" / "ir"
SR = 48000
TAPS = 20000
FADE_SECONDS = 0.1

REGIONS = ["bass", "tenor", "middle", "treble"]
# The keys each region's bridge carries (SPEC §2.8; the boundaries are the prototype's region_of()).
REGION_KEYS = {"bass": (21, 47), "tenor": (48, 59), "middle": (60, 75), "treble": (76, 108)}
PERSPECTIVES = {
    # name: (first region seed, stereo spread: 1 = the keyboard's ends at the speakers)
    "microphones": (1, 0.7),
    "pianist": (5, 0.9),
}


def soundboard_ir(region_seed, sr, taps):
    """The prototype's soundboard_ir(region_seed, length) with COLOUR = None, at rate sr."""
    r = np.random.default_rng(region_seed)
    t = np.arange(taps) / sr
    low = [50, 62, 90, 105, 127, 187, 222, 245, 325]
    f_hi = []
    f = 300.0
    while f < 12000:                              # concert grand: ~2x upright density below 1.1 kHz
        dens = 0.12 if f < 1100 else 0.12 * (1100 / f) ** 0.5
        f += r.exponential(1 / dens); f_hi.append(f)
    freqs = np.array(low + f_hi)
    eta = r.uniform(0.01, 0.03, freqs.size)
    alpha = math.pi * freqs * eta
    # colour: radiation rises to ~500 Hz, mobility flat 500 Hz-5 kHz, gentle roll-off above
    col = (freqs / 500) ** 1.5 / (1 + (freqs / 500) ** 1.5) / (1 + (freqs / 6000) ** 2)
    ir = np.zeros((t.size, 2))
    for c in range(2):
        for _ in range(1):
            g = col * r.normal(0, 1, freqs.size) / np.sqrt(alpha)
            ph = r.uniform(0, 2 * np.pi, freqs.size)
            for k0 in range(0, freqs.size, 64):
                sl = slice(k0, k0 + 64)
                ir[:, c] += (g[sl, None] * np.exp(-alpha[sl, None] * t) *
                             np.sin(2 * np.pi * freqs[sl, None] * t + ph[sl, None])).sum(0)
    ir[:, :] /= np.sqrt((ir ** 2).sum(0).max())
    # Smooth (non-resonant) part. At 0.35 the random modes alone swung partial levels by up to
    # +/-20 dB (C4 came out hollow); 2.0 keeps the swings around +/-3-6 dB. Plath calibration must set this.
    direct = np.zeros_like(ir); direct[0] = [2.0, 2.0]
    ir = ir + direct
    return ir


def placement_gains(region, spread):
    """Constant-power (left, right) gains, unity per channel at the centre."""
    lo, hi = REGION_KEYS[region]
    position = spread * ((lo + hi) / 2 - 64.5) / 43.5          # -1 = A0, +1 = C8
    theta = (position + 1) * math.pi / 4
    return math.sqrt(2) * math.cos(theta), math.sqrt(2) * math.sin(theta)


def fade_out(taps, sr):
    n = int(round(FADE_SECONDS * sr))
    w = np.ones(taps)
    w[taps - n:] = 0.5 * (1 + np.cos(np.pi * (np.arange(n) + 1) / n))   # reaches exactly 0
    return w


def perspective_irs(name):
    first_seed, spread = PERSPECTIVES[name]
    w = fade_out(TAPS, SR)
    irs = {}
    for i, region in enumerate(REGIONS):
        ir = soundboard_ir(first_seed + i, SR, TAPS)
        gl, gr = placement_gains(region, spread)
        irs[region] = ir * np.array([gl, gr])[None, :] * w[:, None]
    gain = np.mean([band_power_gain(ir) for ir in irs.values()])
    return {region: ir / math.sqrt(gain) for region, ir in irs.items()}


def band_power_gain(ir, lo=100.0, hi=1000.0):
    """Mean power gain of both channels over lo-hi, each octave weighted equally."""
    spectrum = np.fft.rfft(ir, 1 << 16, axis=0)
    f = np.fft.rfftfreq(1 << 16, 1 / SR)
    band = (f >= lo) & (f <= hi)
    weight = 1 / f[band]
    return float(((np.abs(spectrum[band]) ** 2) * weight[:, None]).sum(0).mean() / weight.sum())


def float_wav(x, sr):
    """32-bit IEEE float WAV: fmt (18 bytes, as non-PCM formats require), fact, data."""
    frames, channels = x.shape
    data = np.ascontiguousarray(x, dtype="<f4").tobytes()
    fmt = struct.pack("<HHIIHHH", 3, channels, sr, sr * channels * 4, channels * 4, 32, 0)
    fact = struct.pack("<I", frames)
    body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"fact" + struct.pack("<I", len(fact)) + fact
            + b"data" + struct.pack("<I", len(data)) + data)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def check_against_prototype():
    """The copied generator must give exactly the prototype's numbers."""
    sys.path.insert(0, str(ROOT / "prototypes" / "listening-test"))
    import forte_proto
    forte_proto.SR = SR
    for seed in range(1, 9):
        theirs = forte_proto.soundboard_ir(seed, length=TAPS / SR)
        ours = soundboard_ir(seed, SR, TAPS)
        if theirs.shape != ours.shape or not np.array_equal(theirs, ours):
            sys.exit(f"seed {seed}: the copy no longer matches forte_proto.soundboard_ir")
    print("generator matches the prototype for seeds 1-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="compare the generator with the prototype's")
    args = parser.parse_args()
    if args.check:
        check_against_prototype()
        return

    OUT.mkdir(parents=True, exist_ok=True)
    for name in PERSPECTIVES:
        for region, ir in perspective_irs(name).items():
            path = OUT / f"{name}-{region}.wav"
            path.write_bytes(float_wav(ir, SR))
            peak = np.abs(ir).max(0)
            energy = (ir ** 2).sum(0)
            print(f"{path.relative_to(ROOT)}: peak L {peak[0]:.3f} R {peak[1]:.3f}, "
                  f"energy L {energy[0]:.3f} R {energy[1]:.3f}")


if __name__ == "__main__":
    main()
