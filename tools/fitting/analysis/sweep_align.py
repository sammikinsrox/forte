"""Find each scheduled note of the sweep in a render: per note, the strike's onset (the steepest
rise above 250 Hz near where the schedule, corrected for the drift so far, expects it)."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import numpy as np
from forte_audio import read_wav

# The sweep's schedule (make_note_sweep.py writes it beside the MIDI). FORTE_SWEEP_CSV picks
# another, e.g. the long sweep's.
SCHEDULE = os.environ.get("FORTE_SWEEP_CSV", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..",
                                                          "build", "ab", "single-notes", "note-sweep.csv"))


def schedule():
    rows = [line.split(",") for line in open(SCHEDULE).read().split("\n")[1:] if line]
    return [(float(t), int(m), int(v)) for t, m, v in rows]


def onsets(mono, rate, search=0.25):
    hop = int(0.002 * rate); n = 4 * hop
    count = (len(mono) - n) // hop
    idx = np.arange(n)[None, :] + hop * np.arange(count)[:, None]
    e = 10 * np.log10((mono[idx] ** 2).sum(1) + 1e-20)
    rise = np.zeros_like(e); rise[3:] = e[3:] - e[:-3]
    out, drift = [], 0.0
    for t, m, v in schedule():
        guess = t + drift
        lo, hi = int((guess - search) * rate / hop), int((guess + search) * rate / hop)
        lo, hi = max(lo, 3), min(hi, len(rise) - 1)
        i = lo + int(np.argmax(rise[lo:hi]))
        found = i * hop / rate
        out.append((t, m, v, found, float(rise[i])))
        drift = found - t
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        x, rate = read_wav(path)
        mono = x.mean(axis=1)
        print(f"{path}: {rate} Hz, {x.shape[1]} ch, {len(x) / rate / 60:.2f} min, peak {20 * np.log10(np.abs(x).max()):.1f} dBFS")
        found = onsets(mono, rate)
        d = np.array([f - t for t, _, _, f, _ in found])
        weak = [(m, v) for t, m, v, f, r in found if r < 6]
        print(f"  offset at first {d[0]:+.3f} s, middle {d[len(d) // 2]:+.3f}, last {d[-1]:+.3f}; slope {np.polyfit([r[0] for r in found], d, 1)[0] * 100:+.3f} %;"
              f" residual from the line {np.std(d - np.polyval(np.polyfit([r[0] for r in found], d, 1), [r[0] for r in found])) * 1000:.1f} ms; weak onsets {len(weak)} {weak[:8]}")
