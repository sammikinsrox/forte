"""Across every Iowa mf note, Forte against Iowa:
- partial-level scatter (SPEC §3.4's "irregularity"): per note, the partial levels in the first
  1 s (below 4 kHz, within 50 dB of the loudest), less a quadratic in partial number through them;
  the scatter is the RMS of the rest. Median over notes.
- loudness: each note's first-2 s RMS relative to the C4-B4 mean, and the mean jump between
  neighbouring notes.

    python tools/fitting/keyboard_evenness.py [--render label=forte-render.exe ...] [--table]

Iowa's own figures include its player's mf varying from note to note, so treat its jump as a
ceiling, not a target."""

import argparse

import numpy as np

from forte_audio import RENDER, iowa_file, note_name, onset, partial_frequency, partial_levels, read_aiff, render_note


def note_figures(mono, rate, midi):
    start = onset(mono)
    levels = partial_levels(mono, rate, midi, start, 1.0, max_partial=24, max_hz=4000)
    ns = np.arange(1, len(levels) + 1)
    keep = levels > levels.max() - 50
    scatter = np.nan
    if keep.sum() >= 5:
        fit = np.polyval(np.polyfit(ns[keep], levels[keep], 2), ns[keep])
        scatter = float(np.sqrt(np.mean((levels[keep] - fit) ** 2)))
    x = mono[start: start + int(2 * rate)]
    return scatter, 10 * np.log10(np.mean(x ** 2))


def keyboard(render=None):
    rows = []
    for midi in range(21, 109):
        reference = iowa_file(midi)
        if reference is None:
            continue
        if render is None:
            x, rate = read_aiff(reference)
            mono = x.mean(axis=1)
        else:
            mono, rate = render_note(render, midi, hold=2.5, tail=0.3)
        rows.append((midi, *note_figures(mono, rate, midi)))
    return rows


def summary(rows):
    midi = np.array([r[0] for r in rows])
    levels = np.array([r[2] for r in rows])
    middle = (midi >= 60) & (midi <= 71)
    relative = levels - levels[middle].mean()
    return np.nanmedian([r[1] for r in rows]), np.mean(np.abs(np.diff(levels))), dict(zip(midi, relative))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--render", action="append", help="label=path to forte-render (repeatable)")
    parser.add_argument("--table", action="store_true", help="also print each note's loudness")
    args = parser.parse_args()
    renders = [r.split("=", 1) for r in args.render] if args.render else [("forte", str(RENDER))]

    results = [("iowa", summary(keyboard()))] + [(label, summary(keyboard(exe))) for label, exe in renders]
    for label, (scatter, jump, _) in results:
        print(f"{label[:30]:30s} partial scatter {scatter:5.1f} dB   neighbouring-note jump {jump:4.1f} dB")

    if args.table:
        print("\nloudness re the C4-B4 mean (dB)")
        print("note " + "".join(f"{label[:10]:>11s}" for label, _ in results))
        for midi in results[0][1][2]:
            print(f"{note_name(midi):5s}" + "".join(f"{r[2][midi]:11.1f}" for _, r in results))


if __name__ == "__main__":
    main()
