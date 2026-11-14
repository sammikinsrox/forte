# PROTOTYPE (throwaway): numeric A/B of rendered notes vs references.
import json, math, wave
from pathlib import Path
import numpy as np

OUT = Path(__file__).resolve().parent / "out"
SR = 44100
rep = json.loads((OUT / "report.json").read_text())

def load(p):
    with wave.open(str(p)) as w:
        b = np.frombuffer(w.readframes(w.getnframes()), np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608
        return v.reshape(-1, w.getnchannels()).mean(axis=1)

def prof(x, f0, B, t0, t1, nmax=12):
    seg = x[int(t0 * SR):int(t1 * SR)]
    seg = seg * np.hanning(seg.size)
    S = np.abs(np.fft.rfft(seg, 1 << 17)); fr = np.fft.rfftfreq(1 << 17, 1 / SR)
    r = []
    for n in range(1, nmax + 1):
        f = n * f0 * math.sqrt(1 + B * n * n)
        k = (fr > f - 0.2 * f0) & (fr < f + 0.2 * f0)
        r.append(20 * math.log10(S[k].max() + 1e-12) if k.any() else -200)
    return np.array(r)

def env(x, ts):
    return [20 * math.log10(np.sqrt(np.mean(x[int(t * SR):int(t * SR) + 2205] ** 2)) + 1e-9) for t in ts]

ts = [0.05, 0.25, 0.5, 1, 2, 4, 8]
for name, r in rep.items():
    for dy in ("pp", "mf", "ff"):
        a, b = load(OUT / f"{name}_{dy}_ref.wav"), load(OUT / f"{name}_{dy}_forte.wav")
        pa, pb = prof(a, r["f0"], r["B"], 0.02, 0.32), prof(b, r["f0"], r["B"], 0.02, 0.32)
        top = pa.max()
        print(f"{name} {dy}  partials dB (ref/forte, rel. ref max), 20-320 ms")
        print("   ref  ", " ".join(f"{v - top:5.0f}" for v in pa))
        print("   forte", " ".join(f"{v - top:5.0f}" for v in pb))
        ea, eb = env(a, [t for t in ts if t * SR < a.size - 2300]), env(b, [t for t in ts if t * SR < b.size - 2300])
        print("   level dB at", ts[:len(ea)])
        print("   ref  ", " ".join(f"{v:6.1f}" for v in ea))
        print("   forte", " ".join(f"{v:6.1f}" for v in eb))
