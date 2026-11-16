"""How the top end dies: level of 5-10 kHz and 10-16 kHz over time after each strike, dB re the
note's whole level in 0-0.1 s, medians per register at one velocity.
python top_decay.py velocity a.wav b.wav ..."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import read_wav
import sweep_align as sa

WINDOWS = [(0.0, 0.05), (0.05, 0.1), (0.1, 0.2), (0.2, 0.4), (0.4, 0.8), (0.8, 1.6), (1.6, 2.8)]
BANDS = [(5000, 10000), (10000, 16000)]
REGISTERS = [(36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"), (84, 95, "C6-B6")]


def band_db(seg, rate, lo, hi):
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    return 10 * np.log10(s[(f >= lo) & (f < hi)].sum() / len(seg) + 1e-30)


vel = int(sys.argv[1])
for path in sys.argv[2:]:
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    per = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v != vel:
            continue
        t0 = found - 0.004
        ref = band_db(mono[int(t0 * rate): int((t0 + 0.1) * rate)], rate, 20, 24000)
        rows = []
        for lo, hi in BANDS:
            rows.append([band_db(mono[int((t0 + a) * rate): int((t0 + b) * rate)], rate, lo, hi) - ref for a, b in WINDOWS])
        per[m] = np.array(rows)
    print(f"{os.path.basename(path)} v{vel}: windows " + " ".join(f"{a}-{b}" for a, b in WINDOWS))
    for lo, hi, name in REGISTERS:
        med = np.median([per[m] for m in range(lo, hi + 1) if m in per], axis=0)
        for (blo, bhi), row in zip(BANDS, med):
            print(f"  {name} {blo//1000}-{bhi//1000}k " + " ".join(f"{v:6.1f}" for v in row))
