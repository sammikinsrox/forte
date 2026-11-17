"""Partials 2-8 re partial 1 at the strike, 0.3-0.6 s and 1-2 s (sweep_compare.partial_track),
median per register at one velocity, reference then each file.
python evolution.py velocity reference.wav a.wav ..."""
import os
import pickle
import sys
here = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv
sys.argv = [argv[0], "x", "y"]
exec(open(os.path.join(here, "sweep_compare.py")).read().split("ref_path, forte_path =")[0])
sys.argv = argv
REGISTERS = [(36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5")]


def per_note(path, vel):
    cache = os.path.join(here, f"ev-{os.path.basename(path)}-{vel}-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v == vel:
            _, _, rows = partial_track(mono, rate, found - 0.004, m)
            out[m] = {w: np.array([r[i] if i < len(r) else np.nan for i in range(0, 8)]) for w, r in rows.items()}
    pickle.dump(out, open(cache, "wb"))
    return out


vel = int(sys.argv[1])
data = [(os.path.basename(p)[:26], per_note(p, vel)) for p in sys.argv[2:]]
print(f"v{vel}: p1 (re p1 at the strike) then p2..p8 re p1 in the same window, register medians")
for lo, hi, name in REGISTERS:
    print(name)
    for w in ("strike", "0.3-0.6", "1-2"):
        for label, d in data:
            rows = [d[m][w] for m in range(lo, hi + 1) if m in d]
            med = np.nanmedian([r - np.r_[0, np.full(7, r[0])] for r in rows], axis=0)
            print(f"   {w:8s} {label:26s} p1 {med[0]:5.0f} | " + " ".join(f"{v:5.0f}" for v in med[1:]))
