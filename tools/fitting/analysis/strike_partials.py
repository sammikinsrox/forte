"""Partials 2-8 at the strike (5-100 ms), dB re partial 1, median per register, for each file at
one velocity, and each file's mean |difference| from the first over p2-p8 per register.
python strike_partials.py velocity reference.wav a.wav b.wav ..."""
import os
import pickle
import sys
here = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv
sys.argv = [argv[0], "x", "y"]
exec(open(os.path.join(here, "sweep_compare.py")).read().split("ref_path, forte_path =")[0])
sys.argv = argv

REGISTERS = [(24, 35, "C1-B1"), (36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"), (84, 95, "C6-B6")]


def per_note(path, vel):
    cache = os.path.join(here, f"sp-{os.path.basename(path)}-{vel}-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v == vel:
            _, _, rows = partial_track(mono, rate, found - 0.004, m)
            r = rows["strike"]
            out[m] = np.array([r[i] if i < len(r) else np.nan for i in range(1, 8)])
    pickle.dump(out, open(cache, "wb"))
    return out


vel = int(sys.argv[1])
data = [(os.path.basename(p)[:30], per_note(p, vel)) for p in sys.argv[2:]]
ref = data[0][1]
print(f"v{vel}: partials 2-8 at the strike, dB re p1 (register medians) | mean |diff| from the reference")
for lo, hi, name in REGISTERS:
    print(name)
    for label, d in data:
        med = np.nanmedian([d[m] for m in range(lo, hi + 1) if m in d], axis=0)
        diffs = np.nanmedian([d[m] - ref[m] for m in range(lo, hi + 1) if m in d and m in ref], axis=0)
        print(f"   {label:30s} " + " ".join(f"{v:5.0f}" for v in med) + f"  | {np.nanmean(np.abs(diffs)):4.1f}  signed {np.nanmean(diffs):+5.1f}")
