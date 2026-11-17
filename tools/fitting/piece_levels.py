"""Levels of rendered pieces, for A/B listening: peak, RMS, crest factor (peak over RMS) and each
band's share of the whole piece's energy. With --match, first scales every WAV in the folder (in
place) to the RMS of the named one, so loudness doesn't sway the comparison.

    python tools/fitting/piece_levels.py <folder> [--match <stem>] [--extra <wav> ...]

The bands (issue 0021): below 250 Hz the bass and rumble, 250-500 Hz the low mids ("boxy"),
0.5-1 kHz, 1-3.5 kHz the twang layer, 3.5-8 kHz the brightness, above 8 kHz the air. --extra adds
WAVs from elsewhere (a reference render) to the table without scaling them."""

import argparse
from pathlib import Path

import numpy as np

from forte_audio import read_wav, scale_float_wav

BANDS = [(0, 250, "<250"), (250, 500, "250-500"), (500, 1000, "0.5-1k"), (1000, 3500, "1-3.5k"),
         (3500, 8000, "3.5-8k"), (8000, None, ">8k")]


def report(wav):
    """(peak dBFS, RMS dBFS, crest dB, [band shares, dB])."""
    x, rate = read_wav(wav)
    n = 8192
    power = 0                  # each channel's power, summed: never the channels' sum (issue 0022 §1)
    for c in range(x.shape[1]):
        frames = x[: len(x) // n * n, c].astype(np.float64).reshape(-1, n) * np.hanning(n)
        power = power + (np.abs(np.fft.rfft(frames, axis=1)) ** 2).sum(axis=0)
    freqs = np.fft.rfftfreq(n, 1 / rate)
    shares = [10 * np.log10(power[(freqs >= lo) & (freqs < (hi or np.inf))].sum() / power.sum() + 1e-30)
              for lo, hi, _ in BANDS]
    peak = 20 * np.log10(np.abs(x).max())
    rms = 10 * np.log10(np.mean(x ** 2))
    return peak, rms, peak - rms, shares


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--match", help="stem of the WAV whose RMS the others are scaled to")
    parser.add_argument("--extra", type=Path, nargs="*", default=[], help="more WAVs to report, unscaled")
    args = parser.parse_args()
    wavs = sorted(args.folder.glob("*.wav"))

    if args.match:
        target = np.mean(read_wav(args.folder / f"{args.match}.wav")[0] ** 2)
        for wav in wavs:
            gain = np.sqrt(target / np.mean(read_wav(wav)[0] ** 2))
            scale_float_wav(wav, gain)
            print(f"{wav.stem}: scaled {20 * np.log10(gain):+.1f} dB")

    wavs += args.extra
    width = max(len(w.stem) for w in wavs)
    print(f"{'':{width}s}  {'peak':>6s} {'rms':>6s} {'crest':>6s}  " + " ".join(f"{label:>8s}" for *_, label in BANDS))
    for wav in wavs:
        peak, rms, crest, shares = report(wav)
        print(f"{wav.stem:{width}s}  {peak:6.1f} {rms:6.1f} {crest:6.1f}  " + " ".join(f"{s:8.1f}" for s in shares))


if __name__ == "__main__":
    main()
