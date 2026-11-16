"""Per partial decay rate σ (1/s) over the held note's first 0.8 s (decay_measure's windows, a line
through the counting windows 0.05-0.8 s), from a sweep render. Cached.
python partial_sigmas.py velocity a.wav b.wav ... prints medians per register and frequency band."""
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import decay_measure as dm
from forte_audio import read_wav, partial_frequency
import sweep_align as sa

here = os.path.dirname(os.path.abspath(__file__))
FIT_WINDOWS = 4   # 0.05-0.8 s
BANDS = [(1000, 2000), (2000, 3000), (3000, 4500), (4500, 6500), (6500, 10000)]
REGISTERS = [(36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"), (84, 95, "C6-B6")]


def sigmas(path):
    cache = os.path.join(here, "ps-" + os.path.basename(path) + f"-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    t_mid = dm.window_times()[:FIT_WINDOWS]
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v not in (96, 127):
            continue
        a = max(0, int((found - 1.5) * rate))
        seg = mono[a: int((found + 2.9) * rate)]
        clipped = np.abs(mono[int(found * rate): int((found + 0.5) * rate)]).max() > 0.999
        times, envs, floors = dm.envelopes(seg, rate, m, partials=200)
        rows = []
        for n, (env, floor) in enumerate(zip(envs, floors), start=1):
            levels, valid = dm.window_levels(times, env, floor)
            lv, ok = levels[:FIT_WINDOWS], valid[:FIT_WINDOWS]
            if ok.sum() >= 3:
                slope = np.polyfit(t_mid[ok], lv[ok], 1)[0]
                rows.append((n, partial_frequency(m, n), -slope / (20 * np.log10(np.e))))
        out[(m, v)] = (rows, clipped)
    pickle.dump(out, open(cache, "wb"))
    return out


if __name__ == "__main__":
    vel = int(sys.argv[1])
    for path in sys.argv[2:]:
        d = sigmas(path)
        print(f"{os.path.basename(path)} v{vel}: median σ (1/s, amplitude) per band " + " ".join(f"{a//1000}-{b/1000:g}k" for a, b in BANDS))
        for lo, hi, name in REGISTERS:
            cells = []
            for blo, bhi in BANDS:
                vals = [s for m in range(lo, hi + 1) if (m, vel) in d and not d[(m, vel)][1]
                        for n, f, s in d[(m, vel)][0] if blo <= f < bhi]
                cells.append(f"{np.median(vals):5.1f}({len(vals):3d})" if vals else "    -     ")
            print(f"  {name} " + " ".join(cells))
