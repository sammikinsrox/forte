"""How much louder and brighter each note gets from velocity 64 to 96 (and 127), per register:
level of the strike (0-0.15 s), and the strike's and the body's (0.3-1 s) share above 2 kHz and
above 5 kHz. python velocity_colour.py a.wav b.wav ..."""
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import power_spectrum, read_wav
import sweep_align as sa

here = os.path.dirname(os.path.abspath(__file__))
REGISTERS = [(21, 35, "A0-B1"), (36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"),
             (84, 95, "C6-B6"), (96, 108, "C7-C8")]


def measures(seg, rate):
    s = power_spectrum(seg)
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    tot = s.sum() + 1e-30
    return (10 * np.log10(tot / len(seg)), 10 * np.log10(s[f > 2000].sum() / tot + 1e-30),
            10 * np.log10(s[f > 5000].sum() / tot + 1e-30))


def measure_file(path):
    cache = os.path.join(here, "vc-" + os.path.basename(path) + f"-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        t0 = found - 0.004
        strike = mono[int(t0 * rate): int((t0 + 0.15) * rate)]
        body = mono[int((t0 + 0.3) * rate): int((t0 + 1.0) * rate)]
        clipped = np.abs(mono[int(t0 * rate): int((t0 + 0.5) * rate)]).max() > 0.999
        out[(m, v)] = measures(strike, rate) + measures(body, rate)[1:] + (clipped,)
    pickle.dump(out, open(cache, "wb"))
    return out


files = sys.argv[1:]
data = [(os.path.basename(p)[:22], measure_file(p)) for p in files]
for hi_v in (96, 127):
    print(f"\nvelocity 64 -> {hi_v}: change in strike level | strike >2k share | >5k share | body >2k | body >5k (dB); clipped notes skipped")
    for lo, hi, name in REGISTERS:
        print(name)
        for label, d in data:
            rows = []
            for m in range(lo, hi + 1):
                a, b = d.get((m, 64)), d.get((m, hi_v))
                if a and b and not a[5] and not b[5]:
                    rows.append(np.array(b[:5]) - np.array(a[:5]))
            if rows:
                med = np.median(rows, axis=0)
                print(f"   {label:22s} " + " | ".join(f"{v:+6.1f}" for v in med) + f"   ({len(rows)} notes)")
