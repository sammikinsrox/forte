"""Note by note, a reference render of the sweep against Forte's: inharmonicity, partial levels
over time, the strike's noise share and the partials' wavering.
python sweep_compare.py reference.wav forte.wav [velocity]"""
import math
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import read_wav, note_name, partial_frequency, inharmonicity
import sweep_align as sa

MAX_PARTIALS = 16
WINDOWS = {"strike": (0.005, 0.1), "0.3-0.6": (0.3, 0.6), "1-2": (1.0, 2.0)}


def spectrum(x, rate, a, b, pad=8):
    seg = x[int(a * rate): int(b * rate)]
    size = pad * len(seg)
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), size)) / (len(seg) / 4)
    return s, np.fft.rfftfreq(size, 1 / rate)


def peak(s, f, centre, width):
    sel = np.where((f > centre - width) & (f < centre + width))[0]
    if len(sel) < 3:
        return np.nan, -300.0
    i = sel[np.argmax(s[sel])]
    if 0 < i < len(s) - 1:   # parabolic interpolation in dB
        y0, y1, y2 = 20 * np.log10(s[i - 1:i + 2] + 1e-15)
        d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
        return f[i] + d * (f[1] - f[0]), y1 - 0.25 * (y0 - y2) * d
    return f[i], 20 * np.log10(s[i] + 1e-15)


def partial_track(x, rate, t0, midi):
    """Partial frequencies (from 0.05-1.05 s) found one by one, each predicted from the B fitted so
    far; then their levels in each window (dB re partial 1's strike level)."""
    s, f = spectrum(x, rate, t0 + 0.05, t0 + 1.05)
    f0_guess = partial_frequency(midi, 1)
    freqs, levels = [], []
    b, f0 = inharmonicity(midi), None
    for n in range(1, MAX_PARTIALS + 1):
        guess = n * (f0 or f0_guess) * math.sqrt(1 + b * n * n)
        if guess > min(12000, 0.45 * rate):
            break
        fr, lv = peak(s, f, guess, 0.25 * f0_guess if n > 1 else 0.06 * f0_guess)
        freqs.append(fr); levels.append(lv)
        if n == 1:
            f0 = fr
        ok = [(k + 1, q) for k, q in enumerate(freqs) if np.isfinite(q) and levels[k] > levels[0] - 50]
        if len(ok) >= 3:
            ns = np.array([k for k, _ in ok], float); fs = np.array([q for _, q in ok])
            slope, intercept = np.polyfit(ns ** 2, (fs / ns) ** 2, 1)
            if intercept > 0:
                b, f0 = max(slope / intercept, 0.0), math.sqrt(intercept) / math.sqrt(1 + 0)
    ok = [(k + 1, q) for k, q in enumerate(freqs) if np.isfinite(q) and levels[k] > levels[0] - 45]
    fitted_b = np.nan
    if len(ok) >= 4:
        ns = np.array([k for k, _ in ok], float); fs = np.array([q for _, q in ok])
        slope, intercept = np.polyfit(ns ** 2, (fs / ns) ** 2, 1)
        fitted_b = slope / intercept if intercept > 0 else np.nan
    rows = {}
    for name, (a, bb) in WINDOWS.items():
        sw, fw = spectrum(x, rate, t0 + a, t0 + bb)
        rows[name] = np.array([peak(sw, fw, q, max(6.0, 0.02 * q))[1] if np.isfinite(q) else np.nan for q in freqs])
    ref = rows["strike"][0]
    return np.array(freqs), fitted_b, {k: v - ref for k, v in rows.items()}


def noise_share(x, rate, t0, freqs, a=0.005, b=0.055):
    """dB of the window's energy that lies away from the partials (beyond ±max(30 Hz, 1%))."""
    s, f = spectrum(x, rate, t0 + a, t0 + b, pad=1)
    p = s ** 2
    mask = np.ones(len(f), bool)
    for q in freqs:
        if np.isfinite(q):
            mask &= np.abs(f - q) > max(30.0, 0.01 * q) + 2 * (f[1] - f[0])
    sel = f > 100
    return 10 * np.log10(p[mask & sel].sum() / p[sel].sum() + 1e-12)


def wavering(x, rate, t0, freq, a=0.3, b=2.5):
    """std (dB) of a partial's envelope over a-b s around its own straight-line decay."""
    if not np.isfinite(freq):
        return np.nan
    hop = int(0.02 * rate); n = int(max(0.08, 6 / freq) * rate)
    t = np.arange(int((t0 + a) * rate), int((t0 + b) * rate) - n, hop)
    w = np.hanning(n); k = np.arange(n)
    phasor = np.exp(-2j * np.pi * freq * k / rate) * w
    env = 20 * np.log10(np.array([np.abs(np.dot(x[i:i + n], phasor)) for i in t]) + 1e-12)
    tt = t / rate
    return float(np.std(env - np.polyval(np.polyfit(tt, env, 1), tt)))


ref_path, forte_path = sys.argv[1], sys.argv[2]
velocity = int(sys.argv[3]) if len(sys.argv) > 3 else 64
data = {}
for label, path in (("keyscape", ref_path), ("forte", forte_path)):
    x, rate = read_wav(path)
    mono = x.mean(axis=1)
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v != velocity:
            continue
        t0 = found - 0.004
        freqs, b, rows = partial_track(mono, rate, t0, m)
        data[(label, m)] = dict(b=b, rows=rows, noise=noise_share(mono, rate, t0, freqs),
                                waver=[wavering(mono, rate, t0, freqs[i]) if i < len(freqs) else np.nan for i in range(3)],
                                f1=freqs[0])

print(f"velocity {velocity}")
print("note   B keyscape   B forte | p2-p6 re p1 at the strike: keyscape / forte | at 1-2 s: keyscape / forte | p1 fall to 1-2 s k/f | noise share k/f | wavering p1,p2 k/f")
for m in range(21, 109):
    k, f = data.get(("keyscape", m)), data.get(("forte", m))
    if not k or not f:
        continue
    def seg(d, w):
        r = d["rows"][w]
        return " ".join("    ." if i >= len(r) or not np.isfinite(r[i]) else f"{r[i]:5.0f}" for i in range(1, 6))
    print(f"{note_name(m):4s} {k['b']:10.2e} {f['b']:9.2e} | {seg(k, 'strike')} / {seg(f, 'strike')} | {seg(k, '1-2')} / {seg(f, '1-2')} |"
          f" {k['rows']['1-2'][0]:5.0f} {f['rows']['1-2'][0]:5.0f} | {k['noise']:5.1f} {f['noise']:5.1f} |"
          f" {k['waver'][0]:4.1f} {k['waver'][1]:4.1f} / {f['waver'][0]:4.1f} {f['waver'][1]:4.1f}")
