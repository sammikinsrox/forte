"""Brightness per velocity against a reference: for each velocity, per region, the whole-note
(0-2.5 s) level in 1-8 kHz re 100 Hz-1 kHz, forte minus reference (dB, medians over notes), and
the loudness change from velocity 64 (forte minus reference). python vel_slope.py reference.wav a.wav ..."""
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import power_spectrum, read_wav
import sweep_align as sa

here = os.path.dirname(os.path.abspath(__file__))
REGIONS = {"tenor": (48, 59), "middle": (60, 75), "treble": (76, 95)}


def notes(path):
    cache = os.path.join(here, f"vs-{os.path.basename(path)}-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        seg = mono[int((found - 0.004) * rate): int((found + 2.5) * rate)]
        p = power_spectrum(seg)
        f = np.fft.rfftfreq(len(seg), 1 / rate)
        hi, lo = p[(f >= 1000) & (f < 8000)].sum(), p[(f >= 100) & (f < 1000)].sum()
        out[(m, v)] = (10 * np.log10(hi / lo), 10 * np.log10((hi + lo) / len(seg)), np.abs(seg).max() > 0.999)
    pickle.dump(out, open(cache, "wb"))
    return out


ref = notes(sys.argv[1])
for path in sys.argv[2:]:
    d = notes(path)
    print(os.path.basename(path))
    total = []
    for region, (lo, hi) in REGIONS.items():
        cells = []
        for v in (32, 64, 96, 127):
            keys = [(m, v) for m in range(lo, hi + 1) if (m, v) in d and (m, v) in ref and not ref[(m, v)][2]]
            b = np.median([d[k][0] - ref[k][0] for k in keys])
            l = np.median([(d[k][1] - d[(k[0], 64)][1]) - (ref[k][1] - ref[(k[0], 64)][1]) for k in keys])
            total.append(abs(b))
            cells.append(f"v{v}: {b:+5.1f} ({l:+4.1f})")
        print(f"  {region:7s} " + "   ".join(cells))
    print(f"  mean |brightness error| {np.mean(total):.2f} dB   (in brackets: loudness change from v64, forte minus reference)")
