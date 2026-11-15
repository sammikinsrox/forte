"""The sympathetic bank's share of held notes: pedal-down minus pedal-up. A held key's own strings
don't depend on the pedal and everything after the strike is linear, so the difference is exactly
the other keys' banks. Prints the note and the difference per time span, and their ratio.

    python tools/fitting/pedal_difference.py 28 36 48 60 72 84 [--render forte-render.exe]

SPEC §3.3 calibrates the bank's 100% level against the same difference in Piano in 162."""

import argparse

import numpy as np

from forte_audio import RENDER, note_name, render_note

SPANS = [(0.0, 1.0), (1.0, 3.0), (3.0, 6.0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("notes", nargs="+", type=int, help="MIDI note numbers")
    parser.add_argument("--render", default=str(RENDER))
    args = parser.parse_args()

    db = lambda x: 20 * np.log10(max(np.sqrt(np.mean(x * x)), 1e-12))
    for midi in args.notes:
        up, rate = render_note(args.render, midi, hold=6.0, tail=0.1)
        down, _ = render_note(args.render, midi, hold=6.0, tail=0.1, pedal=True)
        difference = down - up
        cells = []
        for a, b in SPANS:
            i, j = int(a * rate), int(b * rate)
            cells.append(f"{a:.0f}-{b:.0f} s: note {db(up[i:j]):6.1f}, banks {db(difference[i:j]):6.1f} "
                         f"({db(difference[i:j]) - db(up[i:j]):+5.1f}) dB")
        print(f"{note_name(midi):4s} " + " | ".join(cells))


if __name__ == "__main__":
    main()
