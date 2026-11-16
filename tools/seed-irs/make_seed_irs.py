"""Writes the seed soundboard IRs in data/ir/ (SPEC §2.8, §3.5). They stand in for the calibrated
IRs of SPEC §3.4 until the fitting pipeline makes them, in the same shipped form: 32-bit float WAV
at 48 kHz, stereo, one per bridge region and Listening perspective, 20000 taps (~0.42 s).

    python tools/seed-irs/make_seed_irs.py            (numpy; output is byte-identical every run)

Each IR is the listening-test prototype's modal-noise generator (soundboard_ir() in
prototypes/listening-test/forte_proto.py), copied below with the rate and length made arguments.
Its fixed values are settings too (the board settings below), all at the prototype's own values,
so the seed is the prototype's board; issue 0021 records what moving each one did by ear. The
prototype is throwaway, so the generator lives here; --check confirms that, at the prototype's
values, the copy still matches it. The prototype seeded each region with its number, 1-4; Microphones keeps those
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

It also writes what the engine needs to build the IRs again under the voicing lab's Board
controls (SPEC §6.5): each IR's resonant part, before the direct path, colour, placement, fade and
gain, as data/ir/parts/<perspective>-<region>-modes.wav, and the settings above as
engine/src/BoardSettings.inc. At the lab's 50s the engine's rebuild is these IRs again (the tests
check it), though it plays the shipped files.
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

# Board settings, all at the prototype's values (issue 0021 has what each did by ear). SPEC §3.4's
# calibration sets them.

# Where the board's radiation stops rising. At 500 Hz the modes are 21 dB down at 100 Hz, so the
# bass and low mids come through the smooth direct path alone.
RADIATION_CORNER_HZ = 500.0

# Where the statistical modes start. Below it are only nine fixed modes, none between 127 and
# 187 Hz.
STATISTICAL_MODES_FROM_HZ = 300.0

# How many times more the modes below LOW_MODE_LIMIT_HZ are damped (1 = as generated), and a
# minimum-phase low shelf on the direct path (0 dB = none).
LOW_MODE_LIMIT_HZ = 300.0
LOW_MODE_DAMPING = 1.0
DIRECT_LOW_SHELF_DB = 12.0
DIRECT_LOW_SHELF_HZ = 250.0

# The smooth direct path's weight against the resonant modes (normalised to unit energy). At 2.0
# the output is mostly the bridge force itself, like a pickup under the bridge; SPEC §3.4 fits
# this "irregularity" to the reference's partial-level scatter.
DIRECT_GAIN = 0.5

# Where the modes' radiation rolls off at the top.
MODE_ROLLOFF_HZ = 12000.0

# A tilt on the modes' gains above 1 kHz, dB per octave (0 = as generated). Each mode's gain goes
# as 1/sqrt(damping), and damping rises with frequency, so the generated modes' energy falls
# steeply through the treble (issue 0021 §11).
MODE_TILT_DB_PER_OCTAVE = 8.0
MODE_TILT_FROM_HZ = 1000.0

# A colour curve per region (SPEC §3.4): third-octave gains in dB, interpolated in log frequency,
# from a JSON file {"centresHz": [...], "regions": {"bass": [...], ...}} (None = no colour).
# data/model/board-colour.json is fitted to the user's Keyscape sweep (issue 0021 §14-15).
COLOUR_FILE = ROOT / "data" / "model" / "board-colour.json"

REGIONS = ["bass", "tenor", "middle", "treble"]
# The keys each region's bridge carries (SPEC §2.8; the boundaries are the prototype's region_of()).
REGION_KEYS = {"bass": (21, 47), "tenor": (48, 59), "middle": (60, 75), "treble": (76, 108)}
PERSPECTIVES = {
    # name: (first region seed, stereo spread: 1 = the keyboard's ends at the speakers)
    "microphones": (1, 0.7),
    "pianist": (5, 0.9),
}


def low_shelf(gain_db, corner_hz, sr, taps):
    """Impulse response of an RBJ-cookbook low shelf (slope 1): minimum phase, so it adds no
    pre-ringing, and flat above the corner."""
    a = 10 ** (gain_db / 40)
    w0 = 2 * math.pi * corner_hz / sr
    alpha = math.sin(w0) / 2 * math.sqrt(2)
    c = math.cos(w0)
    b = [a * ((a + 1) - (a - 1) * c + 2 * math.sqrt(a) * alpha),
         2 * a * ((a - 1) - (a + 1) * c),
         a * ((a + 1) - (a - 1) * c - 2 * math.sqrt(a) * alpha)]
    den = [(a + 1) + (a - 1) * c + 2 * math.sqrt(a) * alpha,
           -2 * ((a - 1) + (a + 1) * c),
           (a + 1) + (a - 1) * c - 2 * math.sqrt(a) * alpha]
    h = np.zeros(taps)
    x1 = x2 = y1 = y2 = 0.0
    for n in range(taps):
        x0 = 1.0 if n == 0 else 0.0
        y0 = (b[0] * x0 + b[1] * x1 + b[2] * x2 - den[1] * y1 - den[2] * y2) / den[0]
        h[n] = y0
        x2, x1, y2, y1 = x1, x0, y1, y0
    return h


def soundboard_ir(region_seed, sr, taps, radiation_corner=500.0, statistical_from=300.0,
                  low_mode_damping=1.0, direct_shelf_db=0.0, direct_shelf_hz=250.0, direct_gain=2.0,
                  mode_rolloff=6000.0, mode_tilt=0.0):
    """The prototype's soundboard_ir(region_seed, length) with COLOUR = None, at rate sr. The
    prototype's radiation curve rises to radiation_corner, and its statistical modes start at
    statistical_from. Modes below LOW_MODE_LIMIT_HZ are damped low_mode_damping times more, and
    the direct path gets a low shelf of direct_shelf_db at direct_shelf_hz. The defaults are the
    prototype's own."""
    ir = soundboard_modes(region_seed, sr, taps, radiation_corner, statistical_from, low_mode_damping,
                          mode_rolloff, mode_tilt)
    return ir + direct_path(sr, taps, direct_shelf_db, direct_shelf_hz, direct_gain)


def direct_path(sr, taps, direct_shelf_db, direct_shelf_hz, direct_gain):
    """The smooth (non-resonant) part. At 0.35 the random modes alone swung partial levels by up to
    +/-20 dB (C4 came out hollow); 2.0 keeps the swings around +/-3-6 dB. Plath calibration must
    set this."""
    direct = np.zeros((taps, 2)); direct[0] = [direct_gain, direct_gain]
    if direct_shelf_db != 0.0:
        direct = direct_gain * np.repeat(low_shelf(direct_shelf_db, direct_shelf_hz, sr, taps)[:, None], 2, axis=1)
    return direct


def soundboard_modes(region_seed, sr, taps, radiation_corner, statistical_from, low_mode_damping,
                     mode_rolloff, mode_tilt):
    """The resonant part of soundboard_ir(), scaled so its louder channel has unit energy. The
    engine keeps it (data/ir/parts) to rebuild the IRs under the voicing lab (SPEC §6.5)."""
    r = np.random.default_rng(region_seed)
    t = np.arange(taps) / sr
    low = [50, 62, 90, 105, 127, 187, 222, 245, 325]
    f_hi = []
    f = statistical_from
    while f < 12000:                              # concert grand: ~2x upright density below 1.1 kHz
        dens = 0.12 if f < 1100 else 0.12 * (1100 / f) ** 0.5
        f += r.exponential(1 / dens); f_hi.append(f)
    freqs = np.array(low + f_hi)
    eta = r.uniform(0.01, 0.03, freqs.size)
    if low_mode_damping != 1.0:
        eta = np.where(freqs < LOW_MODE_LIMIT_HZ, eta * low_mode_damping, eta)
    alpha = math.pi * freqs * eta
    # colour: radiation rises to ~500 Hz, mobility flat 500 Hz-5 kHz, gentle roll-off above
    rise = (freqs / radiation_corner) ** 1.5
    col = rise / (1 + rise) / (1 + (freqs / mode_rolloff) ** 2)
    if mode_tilt != 0.0:
        col = col * np.maximum(freqs / MODE_TILT_FROM_HZ, 1.0) ** (mode_tilt / (20 * math.log10(2)))
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


def apply_colour(ir, centres, gains_db):
    """The IR through a zero-phase EQ: gains_db at centres (Hz), linear in log frequency between
    them and held flat beyond. The curve is smooth, so its short acausal part lands in the padding
    and is cut with it."""
    size = 1 << 17
    spectrum = np.fft.rfft(ir, size, axis=0)
    f = np.maximum(np.fft.rfftfreq(size, 1 / SR), 1.0)
    gain = 10 ** (np.interp(np.log(f), np.log(centres), gains_db) / 20)
    return np.fft.irfft(spectrum * gain[:, None], size, axis=0)[: ir.shape[0]]


def load_colour():
    if not COLOUR_FILE:
        return None
    import json
    return json.loads(Path(COLOUR_FILE).read_text())


def perspective_irs(name):
    """The perspective's IRs, and each one's kept mode part (soundboard_modes())."""
    first_seed, spread = PERSPECTIVES[name]
    w = fade_out(TAPS, SR)
    colour = load_colour()
    irs, modes = {}, {}
    for i, region in enumerate(REGIONS):
        modes[region] = soundboard_modes(first_seed + i, SR, TAPS, RADIATION_CORNER_HZ, STATISTICAL_MODES_FROM_HZ,
                                         LOW_MODE_DAMPING, MODE_ROLLOFF_HZ, MODE_TILT_DB_PER_OCTAVE)
        ir = modes[region] + direct_path(SR, TAPS, DIRECT_LOW_SHELF_DB, DIRECT_LOW_SHELF_HZ, DIRECT_GAIN)
        if colour:
            ir = apply_colour(ir, colour["centresHz"], colour["regions"][region])
        gl, gr = placement_gains(region, spread)
        irs[region] = ir * np.array([gl, gr])[None, :] * w[:, None]
    gain = np.mean([band_power_gain(ir) for ir in irs.values()])
    return {region: ir / math.sqrt(gain) for region, ir in irs.items()}, modes


def settings_inc():
    """engine/src/BoardSettings.inc: everything the engine's rebuild needs besides the mode parts,
    so that it builds the shipped IRs again from them (SPEC §6.5)."""
    colour = load_colour()
    def numbers(xs):
        return ", ".join(repr(float(x)) for x in xs)
    lines = [
        "// Generated by tools/seed-irs/make_seed_irs.py from its board settings and",
        "// data/model/board-colour.json. Don't edit.",
        f"inline constexpr int irTaps = {TAPS};",
        f"inline constexpr double fadeSeconds = {FADE_SECONDS!r};",
        f"inline constexpr double directGain = {DIRECT_GAIN!r};",
        f"inline constexpr double directLowShelfDb = {DIRECT_LOW_SHELF_DB!r};",
        f"inline constexpr double directLowShelfHz = {DIRECT_LOW_SHELF_HZ!r};",
        f"inline constexpr double modeTiltDbPerOctave = {MODE_TILT_DB_PER_OCTAVE!r};",
        f"inline constexpr double modeTiltFromHz = {MODE_TILT_FROM_HZ!r};",
        "inline constexpr int colourFftSize = 1 << 17;",
        "inline constexpr int bandFftSize = 1 << 16;",
        "inline constexpr double bandLowHz = 100.0, bandHighHz = 1000.0;",
        "// Each region's first and last key (SPEC §2.8), and each perspective's stereo spread.",
        "inline constexpr std::array<std::array<int, 2>, 4> regionKeys { { "
        + ", ".join(f"{{ {lo}, {hi} }}" for lo, hi in (REGION_KEYS[r] for r in REGIONS)) + " } };",
        "inline constexpr std::array<double, 2> perspectiveSpread { "
        + numbers(PERSPECTIVES[p][1] for p in PERSPECTIVES) + " };",
        f"inline constexpr bool hasColour = {'true' if colour else 'false'};",
    ]
    centres = colour["centresHz"] if colour else [1000.0]
    lines.append(f"inline constexpr std::array<double, {len(centres)}> colourCentresHz {{ {numbers(centres)} }};")
    lines.append(f"inline constexpr std::array<std::array<double, {len(centres)}>, 4> colourDb {{ {{")
    for r in REGIONS:
        gains = colour["regions"][r] if colour else [0.0]
        lines.append(f"    {{ {numbers(gains)} }},  // {r}")
    lines.append("} };")
    return "\n".join(lines) + "\n"


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

    (OUT / "parts").mkdir(parents=True, exist_ok=True)
    for name in PERSPECTIVES:
        irs, modes = perspective_irs(name)
        for region, ir in irs.items():
            path = OUT / f"{name}-{region}.wav"
            path.write_bytes(float_wav(ir, SR))
            (OUT / "parts" / f"{name}-{region}-modes.wav").write_bytes(float_wav(modes[region], SR))
            peak = np.abs(ir).max(0)
            energy = (ir ** 2).sum(0)
            print(f"{path.relative_to(ROOT)}: peak L {peak[0]:.3f} R {peak[1]:.3f}, "
                  f"energy L {energy[0]:.3f} R {energy[1]:.3f}")
    inc = ROOT / "engine" / "src" / "BoardSettings.inc"
    inc.write_text(settings_inc(), encoding="utf-8", newline="\n")
    print(f"{inc.relative_to(ROOT)} and data/ir/parts written")


if __name__ == "__main__":
    main()
