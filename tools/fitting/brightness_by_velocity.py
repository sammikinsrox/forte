"""A note's attack brightness, Forte at several velocities against Iowa's pp/mf/ff: the mean level
of partials 2-12 below 5 kHz (dB re partial 1), and the spectral centroid (100 Hz-10 kHz).

    python tools/fitting/brightness_by_velocity.py C4 [--render label=forte-render.exe ...]

Iowa has all three dynamics for C2, C4 and C6 only. Its pp recordings are quiet enough that the
noise floor lifts both numbers (C4 pp reads brighter than mf), so compare against mf and ff."""

import argparse

import numpy as np

from forte_audio import (RENDER, iowa_file, midi_of, onset, partial_frequency, partial_levels, power_spectrum,
                         read_aiff, render_note)


def brightness(mono, rate, midi):
    start = onset(mono)
    seconds = max(0.25, 8 / partial_frequency(midi, 1))
    levels = partial_levels(mono, rate, midi, start, seconds, max_partial=12, max_hz=5000)
    x = mono[start: start + int(seconds * rate)]
    spectrum = power_spectrum(x)
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    band = (freqs > 100) & (freqs < 10000)
    centroid = (freqs[band] * spectrum[band]).sum() / spectrum[band].sum()
    mean = (levels[1:] - levels[0]).mean() if len(levels) > 1 else np.nan
    return mean, centroid


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("note", help="Iowa note name, e.g. C4")
    parser.add_argument("--render", action="append", help="label=path to forte-render (repeatable)")
    parser.add_argument("--velocities", default="20,35,50,64,90,127")
    args = parser.parse_args()
    renders = [r.split("=", 1) for r in args.render] if args.render else [("forte", str(RENDER))]
    midi = midi_of(args.note)

    print(f"{args.note} attack: mean of partials 2-12 (dB re partial 1), spectral centroid")
    for dynamic in ("pp", "mf", "ff"):
        reference = iowa_file(midi, dynamic)
        if reference is not None:
            x, rate = read_aiff(reference)
            mean, centroid = brightness(x, rate, midi)
            print(f"  Iowa {dynamic:3s}{'':22s}{mean:7.1f} dB  {centroid:6.0f} Hz")
    for label, exe in renders:
        for velocity in (int(v) for v in args.velocities.split(",")):
            mono, rate = render_note(exe, midi, velocity, hold=0.5)
            mean, centroid = brightness(mono, rate, midi)
            print(f"  {label[:16]:16s} velocity {velocity:3d}{mean:7.1f} dB  {centroid:6.0f} Hz")


if __name__ == "__main__":
    main()
