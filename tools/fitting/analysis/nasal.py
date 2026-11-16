"""The steady tone of single notes (0.1-1.0 s after the strike), forte against a reference:
(a) third-octave band levels re the window's total, forte minus reference, median per register;
(b) partial levels 1-16 re the strongest partial, both files, median per register.
python nasal.py velocity reference.wav forte.wav"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import read_wav, partial_frequency
import sweep_align as sa

A, B = 0.1, 1.0
CENTRES = [100 * 2 ** (k / 3) for k in range(0, 22)]   # 100 Hz .. 10 kHz
REGISTERS = [(36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"), (84, 95, "C6-B6")]
NP = 16


def per_note(path, vel):
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v != vel:
            continue
        t0 = found - 0.004
        seg = mono[int((t0 + A) * rate): int((t0 + B) * rate)]
        clipped = np.abs(mono[int(t0 * rate): int((t0 + 0.5) * rate)]).max() > 0.999
        size = 4 * len(seg)
        s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), size)) ** 2
        f = np.fft.rfftfreq(size, 1 / rate)
        tot = s[f > 50].sum()
        third = np.array([10 * np.log10(s[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum() / tot + 1e-30) for c in CENTRES])
        f0 = partial_frequency(m, 1)
        parts = []
        for n in range(1, NP + 1):
            fn = partial_frequency(m, n)
            sel = (f > fn - 0.2 * f0) & (f < fn + 0.2 * f0)
            parts.append(10 * np.log10(s[sel].max() + 1e-30) if sel.any() and fn < 0.45 * rate else np.nan)
        parts = np.array(parts)
        out[m] = (third, parts - np.nanmax(parts), clipped)
    return out


vel = int(sys.argv[1])
ref, forte = per_note(sys.argv[2], vel), per_note(sys.argv[3], vel)
print(f"v{vel}, 0.1-1.0 s. (a) third-octave bands, forte minus reference (dB), centres:")
print("        " + " ".join(f"{c/1000:5.2g}" for c in CENTRES))
for lo, hi, name in REGISTERS:
    rows = [forte[m][0] - ref[m][0] for m in range(lo, hi + 1) if m in ref and m in forte and not ref[m][2]]
    print(f"  {name} " + " ".join(f"{v:+5.1f}" for v in np.median(rows, axis=0)))
print("(b) partials 1-16, dB re the strongest: reference / forte")
for lo, hi, name in REGISTERS:
    for label, d in (("ref  ", ref), ("forte", forte)):
        rows = [d[m][1] for m in range(lo, hi + 1) if m in d and not ref.get(m, (0, 0, True))[2]]
        print(f"  {name} {label} " + " ".join(f"{v:5.0f}" for v in np.nanmedian(rows, axis=0)))
