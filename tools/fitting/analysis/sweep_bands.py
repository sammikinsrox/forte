"""The hammer fit's attack measurement (hammer_measure.measure_mono, with Iowa's noise-floor rule)
on every note of a sweep render. Returns {(midi, velocity): (reference dB, bands[5], clipped)}.
python sweep_bands.py sweep.wav [notes...] prints bands re velocity 64 per note."""
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import hammer_measure as hm
from forte_audio import read_wav
import sweep_align as sa

here = os.path.dirname(os.path.abspath(__file__))


def sweep_bands(path):
    cache = os.path.join(here, "sb-" + os.path.basename(path) + f"-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        a = max(0, int((found - 1.5) * rate))
        seg = mono[a: int((found + 3.2) * rate)]
        clipped = np.abs(mono[int(found * rate): int((found + 0.5) * rate)]).max() > 0.999
        floor = hm.noise_floor(seg, rate, m, hm.window_seconds(m))
        meas = hm.measure_mono(seg, rate, m, floor)
        out[(m, v)] = (meas.reference, meas.bands, clipped)
    pickle.dump(out, open(cache, "wb"))
    return out


if __name__ == "__main__":
    d = sweep_bands(sys.argv[1])
    notes = [int(n) for n in sys.argv[2:]] or list(range(24, 109, 6))
    for m in notes:
        base = d.get((m, 64))
        if not base:
            continue
        print(f"{m:3d} v64 bands " + " ".join(f"{b:6.1f}" for b in base[1]))
        for v in (32, 96, 127):
            r = d.get((m, v))
            if r:
                print(f"     v{v:<3d} loud {r[0]-base[0]:+5.1f}  bands Δ " + " ".join(f"{b - c:+6.1f}" for b, c in zip(r[1], base[1])) + ("  CLIPPED" if r[2] else ""))
