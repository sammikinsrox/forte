"""Find each scheduled note of the sweep in a render: per note, the strike (forte_audio.find_strikes:
the biggest level rise near where the schedule, corrected by the offset so far, expects it; it
follows the long Keyscape sweep's 2.3 s jump after its first note)."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import numpy as np
from forte_audio import find_strikes, read_wav

# The sweep's schedule (make_note_sweep.py writes it beside the MIDI). FORTE_SWEEP_CSV picks
# another, e.g. the long sweep's.
SCHEDULE = os.environ.get("FORTE_SWEEP_CSV", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..",
                                                          "build", "ab", "single-notes", "note-sweep.csv"))


def schedule():
    rows = [line.split(",") for line in open(SCHEDULE).read().split("\n")[1:] if line]
    return [(float(t), int(m), int(v)) for t, m, v in rows]


def onsets(x, rate):
    """[(scheduled s, midi, velocity, strike s, nan)] for the notes found in x (mono or stereo)."""
    rows = schedule()
    found = find_strikes(x, rate, [t for t, _, _ in rows])
    return [(t, m, v, f, float("nan")) for (t, m, v), f in zip(rows, found) if f is not None]


if __name__ == "__main__":
    for path in sys.argv[1:]:
        x, rate = read_wav(path)
        mono = x
        print(f"{path}: {rate} Hz, {x.shape[1]} ch, {len(x) / rate / 60:.2f} min, peak {20 * np.log10(np.abs(x).max()):.1f} dBFS")
        found = onsets(mono, rate)
        d = np.array([f - t for t, _, _, f, _ in found])
        weak = [(m, v) for t, m, v, f, r in found if r < 6]
        print(f"  offset at first {d[0]:+.3f} s, middle {d[len(d) // 2]:+.3f}, last {d[-1]:+.3f}; slope {np.polyfit([r[0] for r in found], d, 1)[0] * 100:+.3f} %;"
              f" residual from the line {np.std(d - np.polyval(np.polyfit([r[0] for r in found], d, 1), [r[0] for r in found])) * 1000:.1f} ms; weak onsets {len(weak)} {weak[:8]}")
