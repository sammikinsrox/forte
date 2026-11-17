"""Forte against Iowa per bridge region, in third-octave bands: the start of SPEC §3.4's
Microphones-colour comparison.

Per Iowa mf note: band energies in its first 2 s, as shares of its energy up to 1.25 kHz (the felt's
brightness above that would skew every band). Only bands holding one of the note's partials count:
below the fundamental and between the bass's sparse partials, a recording has room noise and thumps
that no IR should try to match. Prints the median Forte-minus-Iowa per band over each region's notes,
and each region's median loudness against Iowa relative to the middle region's.

    python tools/fitting/band_balance.py [--render forte-render.exe]

A first attempt to correct the IRs from these numbers didn't converge (issue 0021): 2-3 Iowa notes
per bass band are too few, and a fit needs the strings right first."""

import argparse

import numpy as np

from forte_audio import RENDER, iowa_file, onset, partial_frequency, read_aiff, render_note

BANDS = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
         1600, 2000, 2500, 3150, 4000]
SHARE_UP_TO = 1250
WINDOW = 2.0
REGIONS = {"bass": (21, 47), "tenor": (48, 59), "middle": (60, 75), "treble": (76, 108)}  # PianoModel.cpp


def shares_and_loudness(mono, rate, midi):
    start = onset(mono)
    x = mono[start: start + int(WINDOW * rate)]
    power = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    limit = SHARE_UP_TO * 2 ** (1 / 6)
    total = power[freqs < limit].sum()
    own = []
    n = 1
    while (f := partial_frequency(midi, n)) < BANDS[-1] * 2 ** (1 / 6):
        own.append(f)
        n += 1
    shares = np.full(len(BANDS), np.nan)
    for i, centre in enumerate(BANDS):
        lo, hi = centre / 2 ** (1 / 6), centre * 2 ** (1 / 6)
        if any(lo <= f < hi for f in own):
            shares[i] = 10 * np.log10(power[(freqs >= lo) & (freqs < hi)].sum() / total + 1e-30)
    return shares, 10 * np.log10(total / len(x))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--render", default=str(RENDER), help="path to forte-render")
    args = parser.parse_args()

    results = {}
    for region, (lo, hi) in REGIONS.items():
        rows, levels = [], []
        for midi in range(lo, hi + 1):
            reference = iowa_file(midi)
            if reference is None:
                continue
            x, rate = read_aiff(reference)
            iowa_shares, iowa_level = shares_and_loudness(x.mean(axis=1), rate, midi)
            mono, yrate = render_note(args.render, midi)
            forte_shares, forte_level = shares_and_loudness(mono, yrate, midi)
            rows.append(forte_shares - iowa_shares)
            levels.append(forte_level - iowa_level)
        rows = np.array(rows)
        median = np.array([np.nanmedian(rows[:, i]) if np.sum(~np.isnan(rows[:, i])) >= 2 else np.nan
                           for i in range(len(BANDS))])
        results[region] = (median, float(np.median(levels)), len(rows))

    reference_level = results["middle"][1]
    print("Forte minus Iowa per band (dB), median over each region's Iowa mf notes ('.' = not measured)")
    print("region (notes) level" + "".join(f"{b:>7g}" for b in BANDS))
    for region, (median, level, count) in results.items():
        cells = "".join("      ." if np.isnan(v) else f"{v:7.1f}" for v in median)
        print(f"{region:8s} ({count:2d}) {level - reference_level:5.1f}{cells}")


if __name__ == "__main__":
    main()
