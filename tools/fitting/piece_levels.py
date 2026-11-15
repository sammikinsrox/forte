"""Levels of rendered pieces, for A/B listening: peak, RMS, and the share of energy below 250 Hz and
above 4 kHz. With --match, first scales every WAV in the folder (in place) to the RMS of the named
one, so loudness doesn't sway the comparison.

    python tools/fitting/piece_levels.py <folder> [--match <stem>]"""

import argparse
from pathlib import Path

import numpy as np

from forte_audio import read_wav, scale_float_wav


def report(wav):
    x, rate = read_wav(wav)
    mono = x.mean(axis=1)
    n = 8192
    frames = mono[: len(mono) // n * n].reshape(-1, n)[::8] * np.hanning(n)
    power = (np.abs(np.fft.rfft(frames, axis=1)) ** 2).mean(axis=0)
    freqs = np.fft.rfftfreq(n, 1 / rate)
    share = lambda sel: 10 * np.log10(power[sel].sum() / power.sum())
    return (20 * np.log10(np.abs(x).max()), 10 * np.log10(np.mean(x ** 2)),
            share(freqs < 250), share(freqs > 4000))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--match", help="stem of the WAV whose RMS the others are scaled to")
    args = parser.parse_args()
    wavs = sorted(args.folder.glob("*.wav"))

    if args.match:
        target = np.mean(read_wav(args.folder / f"{args.match}.wav")[0] ** 2)
        for wav in wavs:
            gain = np.sqrt(target / np.mean(read_wav(wav)[0] ** 2))
            scale_float_wav(wav, gain)
            print(f"{wav.stem}: scaled {20 * np.log10(gain):+.1f} dB")

    width = max(len(w.stem) for w in wavs)
    for wav in wavs:
        peak, rms, low, high = report(wav)
        print(f"{wav.stem:{width}s}  peak {peak:6.1f} dBFS  rms {rms:6.1f} dBFS  "
              f"below 250 Hz {low:6.1f} dB  above 4 kHz {high:6.1f} dB")


if __name__ == "__main__":
    main()
