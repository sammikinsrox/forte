"""Per partial, Forte against Iowa mf: the level in the attack (dB re partial 1) and the decay
rate while the key is held (dB/s, a line fitted to the partial's envelope).

    python tools/fitting/partial_attack_decay.py C4 E5 [--render label=forte-render.exe ...]

Without --render it uses the Release forte-render in build/. Notes are Iowa names (C4 = MIDI 60).
The note is held for 2.5 s, so the decay fit (0.3-2.3 s) never sees the damper."""

import argparse

import numpy as np

from forte_audio import RENDER, iowa_file, midi_of, onset, partial_frequency, read_aiff, render_note, stft_power

ATTACK_SECONDS = 0.25
FRAME, HOP = 0.2, 0.05
FIT_FROM, FIT_TO = 0.3, 2.3


def analyse(mono, rate, midi, partials):
    start = onset(mono)
    x = mono[start: start + int((FIT_TO + FRAME) * rate)]
    size, hop = int(FRAME * rate), int(HOP * rate)
    freqs = np.fft.rfftfreq(size, 1 / rate)
    frames = np.sqrt(stft_power(x, size, range(0, len(x) - size, hop)))
    times = np.arange(len(frames)) * HOP + FRAME / 2
    fit = (times > FIT_FROM) & (times < FIT_TO)
    out = []
    for n in range(1, partials + 1):
        f = partial_frequency(midi, n)
        if f > 0.45 * rate:
            break
        band = (freqs > f * 0.97) & (freqs < f * 1.03)
        envelope = 20 * np.log10(np.array([frame[band].max() for frame in frames]) + 1e-12)
        out.append((n, f, envelope[times < ATTACK_SECONDS].max(), np.polyfit(times[fit], envelope[fit], 1)[0]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("notes", nargs="+", help="Iowa note names, e.g. C4 A4 E5")
    parser.add_argument("--render", action="append", help="label=path to forte-render (repeatable)")
    parser.add_argument("--partials", type=int, default=16)
    parser.add_argument("--velocity", type=int, default=64)
    args = parser.parse_args()
    renders = [r.split("=", 1) for r in args.render] if args.render else [("forte", str(RENDER))]

    for name in args.notes:
        midi = midi_of(name)
        reference = iowa_file(midi)
        if reference is None:
            print(f"{name}: no Iowa mf recording")
            continue
        x, rate = read_aiff(reference)
        columns = [("iowa", analyse(x, rate, midi, args.partials))]
        for label, exe in renders:
            mono, yrate = render_note(exe, midi, args.velocity)
            columns.append((label, analyse(mono, yrate, midi, args.partials)))

        count = min(len(c) for _, c in columns)
        print(f"\n{name}: attack level (dB re partial 1) | decay (dB/s, {FIT_FROM}-{FIT_TO} s)")
        print("  n      Hz" + "".join(f"{label[:12]:>13s}" for label, _ in columns) + " |"
              + "".join(f"{label[:12]:>13s}" for label, _ in columns))
        for i in range(count):
            n, f = columns[0][1][i][:2]
            attack = "".join(f"{c[i][2] - c[0][2]:13.1f}" for _, c in columns)
            decay = "".join(f"{c[i][3]:13.1f}" for _, c in columns)
            print(f"{n:3d} {f:7.0f}{attack} |{decay}")


if __name__ == "__main__":
    main()
