# PROTOTYPE (throwaway): find clicks/pops in a render and line them up with score events.
import sys, wave
import numpy as np
from scipy.signal import butter, sosfilt
import song

def load(p):
    w = wave.open(str(p)); b = np.frombuffer(w.readframes(w.getnframes()), np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16); v = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608
    return v.reshape(-1, 2).mean(axis=1)

SR = 44100
ev, _ = song.score()
for name in sys.argv[1:] or ["prelude_forte", "prelude_ref"]:
    x = load(song.OUT / f"{name}.wav")
    hp = sosfilt(butter(4, 6000, "hp", fs=SR, output="sos"), x)
    e = np.sqrt(np.convolve(hp ** 2, np.ones(64) / 64, "same"))
    base = np.sqrt(np.convolve(hp ** 2, np.ones(4410) / 4410, "same")) + 1e-9
    r = e / base
    idx = np.flatnonzero(r > 6)
    # group spikes
    groups = []
    for i in idx:
        if not groups or i - groups[-1][-1] > 400: groups.append([i])
        else: groups[-1].append(i)
    print(f"{name}: {len(groups)} spikes (>6x local HF level)")
    for g in groups[:25]:
        t = g[0] / SR
        near = [(round(a - t, 3), "on", song.nm(m)) for a, b, m, v in ev if abs(a - t) < 0.03] + \
               [(round(b - t, 3), "off", song.nm(m)) for a, b, m, v in ev if abs(b - t) < 0.06]
        print(f"  {t:7.3f}s  x{r[g].max():5.1f}  {near}")
