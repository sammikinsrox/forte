"""How alive a held note is: its partials' level and pitch movement, per register, a reference
against Forte (issue 0022 §6: one steady tail mode per partial measured 0.00 dB of wavering against
Keyscape's 0.4-1.9 dB and sounded "fake, casio-tone"). Run it on every voicing change.

Per note (v64 and v96), partials 1-3, envelopes of 50 ms frames every 10 ms, channels' powers
averaged; a frame counts while the partial is 25 dB over the spectrum between partials:
- fast: std (dB) around the envelope's 0.5 s running median, 0.3 s to the end (beats under ~1 s);
- slow: std (dB) of the 0.5 s-smoothed envelope around its 3 s running median (beats of 1-4 s;
  long sweeps only, 0.3-8 s);
- wobble: std (cents) of partial 1's frequency (0.1 s frames every 50 ms) around a line.

python wavering.py hold reference.wav forte.wav [forte2.wav ...]   (hold: 3 or 11, the sweep's)"""
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import numpy as np
import forte_audio as fa

HERE = os.path.dirname(os.path.abspath(__file__))
REG = [(21, 35, "A0-B1"), (36, 47, "C2-B2"), (48, 59, "C3-B3"), (60, 71, "C4-B4"), (72, 83, "C5-B5"),
       (84, 95, "C6-B6"), (96, 104, "C7-G#7")]
MARGIN_DB = 25.0


def running_median(a, n):
    n = max(1, min(n, len(a)))
    pad = np.pad(a, (n // 2, n - 1 - n // 2), mode="edge")
    return np.median(np.lib.stride_tricks.sliding_window_view(pad, n), axis=1)


def note(x, rate, st, m, end):
    seg = x[int(st * rate): int((st + end) * rate)]
    first = seg[:rate]
    found = fa.track_partials(fa.power_spectrum(first), np.fft.rfftfreq(len(first), 1 / rate), m, 3, max_hz=18000)
    if len(found) < 2:
        return None
    F = found[1][0] - found[0][0]
    size, hop = int(0.05 * rate), int(0.01 * rate)
    starts = np.arange(int(0.3 * rate), len(seg) - size, hop)
    S = fa.stft_power(seg, size, starts)
    fr = np.fft.rfftfreq(size, 1 / rate)
    fast, slow = [], []
    for f, _ in found:
        w = max(0.25 * F, 1.5 * (fr[1] - fr[0]))
        band = (fr > f - w) & (fr < f + w)
        side = ((fr > f + 0.35 * F) & (fr < f + 0.65 * F)) | ((fr > f - 0.65 * F) & (fr < f - 0.35 * F))
        if not band.any() or not side.any():
            continue
        env = 10 * np.log10(S[:, band].max(axis=1) + 1e-30)
        noise = 10 * np.log10(running_median(np.median(S[:, side], axis=1), 51) + 1e-30)
        ok = env > noise + MARGIN_DB
        # the counting run from the start
        run = int(np.argmin(ok)) if not ok.all() else len(ok)
        if run < 100:
            continue
        e = env[:run]
        fast.append(np.std((e - running_median(e, 51))[25:-25]))
        if run >= 400:
            smooth = running_median(e, 51)
            slow.append(np.std((smooth - running_median(smooth, 301))[150:-150]))
    # partial 1's pitch
    f1 = found[0][0]
    ts = np.arange(0.3, end - 0.15, 0.05)
    nfft = 1 << 16
    frs = np.fft.rfftfreq(nfft, 1 / rate)
    sel = (frs > f1 * 0.98) & (frs < f1 * 1.02)
    idx = np.where(sel)[0]
    freqs = []
    for t in ts:
        p = fa.power_spectrum(seg[int(t * rate): int((t + 0.1) * rate)], nfft)
        i = idx[np.argmax(p[idx])]
        y0, y1, y2 = np.log(p[i - 1: i + 2] + 1e-40)
        d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
        freqs.append(frs[i] + d * (frs[1] - frs[0]))
    cents = 1200 * np.log2(np.array(freqs) / np.mean(freqs))
    wobble = float(np.std(cents - np.polyval(np.polyfit(ts, cents, 1), ts)))
    return (np.median(fast) if fast else np.nan, np.median(slow) if slow else np.nan, wobble)


def measure(path, hold):
    cache = os.path.join(HERE, f"wv-{os.path.basename(path)}-{hold}-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = fa.read_wav(path)
    sched, t = [], 2.0
    for m in range(21, 109):
        for v in (32, 64, 96, 127):
            sched.append((t, m, v)); t += hold + 2.0
    strikes = fa.find_strikes(x, rate, [s for s, _, _ in sched])
    end = min(hold - 0.3, 8.3)
    out = {}
    for (s, m, v), st in zip(sched, strikes):
        if st is not None and v in (64, 96) and m <= 104:
            r = note(x, rate, st, m, end)
            if r:
                out[(m, v)] = r
    pickle.dump(out, open(cache, "wb"))
    return out


if __name__ == "__main__":
    hold = float(sys.argv[1])
    files = sys.argv[2:]
    data = [(os.path.basename(p)[:24], measure(p, hold)) for p in files]
    print(f"medians per register over v64/v96: fast waver (dB) | slow waver (dB) | p1 pitch wobble (cents)")
    print("          " + " | ".join(f"{label:>24s}" for label, _ in data))
    for lo, hi, name in REG:
        cells = []
        for _, d in data:
            rows = np.array([d[k] for k in d if lo <= k[0] <= hi])
            if len(rows) == 0:
                cells.append(" " * 24); continue
            med = np.nanmedian(rows, axis=0)
            cells.append(f"{med[0]:6.2f} {med[1]:6.2f} {med[2]:6.2f}   ")
        print(f"  {name:7s} " + " | ".join(cells))
