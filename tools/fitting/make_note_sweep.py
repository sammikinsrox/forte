"""A MIDI file of every note, one at a time, at several velocities: for rendering a reference
instrument (Keyscape, Pianoteq) and Forte note by note (issue 0021 §13). No pedal.

    python tools/fitting/make_note_sweep.py <out.mid>

Each note is held HOLD s, then released, with GAP s before the next, from A0 to C8, each note at
every velocity in turn. Also writes <out>.csv: onset time (s), MIDI note, velocity, so the
analysis knows what to expect where."""

import argparse
from pathlib import Path

from forte_audio import write_midi

VELOCITIES = (32, 64, 96, 127)
HOLD = 3.0
GAP = 2.0
LEAD_IN = 2.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("--hold", type=float, default=HOLD, help=f"seconds each note is held (default {HOLD})")
    parser.add_argument("--gap", type=float, default=GAP, help=f"seconds between notes (default {GAP})")
    args = parser.parse_args()
    events, rows = [(0.0, [0xB0, 64, 0])], []
    t = LEAD_IN
    for midi in range(21, 109):
        for velocity in VELOCITIES:
            events += [(t, [0x90, midi, velocity]), (t + args.hold, [0x80, midi, 0])]
            rows.append(f"{t:.3f},{midi},{velocity}")
            t += args.hold + args.gap
    events.append((t, [0xB0, 64, 0]))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_midi(args.out, events)
    args.out.with_suffix(".csv").write_text("seconds,midi,velocity\n" + "\n".join(rows) + "\n")
    print(f"wrote {args.out}: {len(rows)} notes, {t / 60:.1f} minutes")


if __name__ == "__main__":
    main()
