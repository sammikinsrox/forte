"""Where Forte's attack differs from a reference's: octave-band levels of the attack (5-60 ms after
the strike) and of the early body (0.1-0.4 s), each dB re that window's total, per register, at one
velocity; printed as forte minus reference. Clipped reference notes are skipped.
python attack_map.py velocity reference.wav forte.wav"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import read_wav
import sweep_align as sa

EDGES = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
REGISTERS = [(21, 35, "A0-B1"), (36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"),
             (84, 95, "C6-B6"), (96, 108, "C7-C8")]
WINDOWS = {"attack 5-60ms": (0.005, 0.06), "body 0.1-0.4s": (0.1, 0.4)}


def bands(seg, rate):
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    e = np.array([s[(f >= a) & (f < b)].sum() for a, b in zip(EDGES[:-1], EDGES[1:])]) + 1e-30
    return 10 * np.log10(e / e.sum())


def per_note(path, vel):
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v != vel:
            continue
        t0 = found - 0.004
        clipped = np.abs(mono[int(t0 * rate): int((t0 + 0.5) * rate)]).max() > 0.999
        out[m] = ({k: bands(mono[int((t0 + a) * rate): int((t0 + b) * rate)], rate) for k, (a, b) in WINDOWS.items()}, clipped)
    return out


vel = int(sys.argv[1])
ref, forte = per_note(sys.argv[2], vel), per_note(sys.argv[3], vel)
print(f"velocity {vel}: forte minus reference, dB, octave bands from " + " ".join(f"{a}" for a in EDGES[:-1]) + " Hz")
for w in WINDOWS:
    print(w)
    for lo, hi, name in REGISTERS:
        rows = [forte[m][0][w] - ref[m][0][w] for m in range(lo, hi + 1) if m in ref and m in forte and not ref[m][1]]
        if rows:
            print(f"  {name} " + " ".join(f"{v:+6.1f}" for v in np.median(rows, axis=0)))
