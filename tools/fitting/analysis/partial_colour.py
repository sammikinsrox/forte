"""Per bridge region, a third-octave colour correction from partial levels (issue 0021 §20): for the
region's notes at velocities 64 and 96, each partial's level over the strike and early ring
(5-400 ms), Keyscape against Forte, each note's partials normalised to their own total, the
differences pooled by the partial's frequency into the colour's bands (median), smoothed, made
level-neutral, clamped. Partials near either file's floor don't count.

colour_fit.py compares whole-note band power instead; in the middle register that hid an ~8 dB hole
at partials 4-7 of C4-B4 (1-2 kHz), because the octaves of higher notes in the same band were
louder than Keyscape's. Partial by partial, the board's response is what's left.

python partial_colour.py reference_sweep.wav forte_sweep.wav out.json"""
import json
import os
import pickle
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import power_spectrum, read_wav
import sweep_align as sa

CENTRES = [40 * 2 ** (k / 3) for k in range(0, 29)]   # 40 Hz .. 16 kHz, as board-colour.json
REGION_KEYS = {"bass": (21, 47), "tenor": (48, 59), "middle": (60, 75), "treble": (76, 108)}
VELS = (64, 96)
WINDOW = (0.005, 0.4)
FLOOR_DB = 50.0          # a partial counts if within this of its note's strongest
MIN_SAMPLES = 6          # partials a band needs to be corrected
CLAMP = (-6.0, 6.0)
TOP_HZ = 9000.0          # above it, the reference's hiss: no correction
HERE = os.path.dirname(os.path.abspath(__file__))


def partials(spec, freqs, f0, nmax=40):
    """(frequency, power) of partials 1..nmax, following the stretch."""
    out, f1, b = [], f0, 5e-4
    for n in range(1, nmax + 1):
        fn = n * f1 * np.sqrt(1 + b * n * n)
        if fn > 16000:
            break
        lo, hi = np.searchsorted(freqs, [fn * 0.985, fn * 1.015])
        if hi <= lo:
            break
        i = lo + int(np.argmax(spec[lo:hi]))
        out.append((freqs[i], spec[i]))
        if n == 1:
            f1 = freqs[i]
        elif spec[i] > out[0][1] * 1e-6:
            r = (freqs[i] / (n * f1)) ** 2
            bn = (r - 1) / (n * n - r)
            if 0 < bn < 0.02:
                b = 0.5 * b + 0.5 * bn
    return out


def measure(path):
    cache = os.path.join(HERE, "pc-" + os.path.basename(path) + f"-{int(os.path.getmtime(path))}.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v not in VELS:
            continue
        seg = mono[int((found + WINDOW[0]) * rate): int((found + WINDOW[1]) * rate)]
        if np.abs(mono[int(found * rate): int((found + 0.3) * rate)]).max() > 0.99:
            continue
        nfft = 1 << int(np.ceil(np.log2(len(seg) * 4)))
        spec = power_spectrum(seg, nfft)
        freqs = np.fft.rfftfreq(nfft, 1 / rate)
        out[(m, v)] = partials(spec, freqs, 440 * 2 ** ((m - 69) / 12))
    pickle.dump(out, open(cache, "wb"))
    return out


def main():
    ref, forte = measure(sys.argv[1]), measure(sys.argv[2])
    edges = [(c / 2 ** (1 / 6), c * 2 ** (1 / 6)) for c in CENTRES]
    result = {"centresHz": [round(c, 1) for c in CENTRES], "regions": {}}
    for region, (lo, hi) in REGION_KEYS.items():
        samples = [[] for _ in CENTRES]
        for key in ref:
            if not lo <= key[0] <= hi or key not in forte:
                continue
            k, f = ref[key], forte[key]
            n = min(len(k), len(f))
            if n < 2:
                continue
            kp = np.array([p for _, p in k[:n]]); fp = np.array([p for _, p in f[:n]])
            kd = 10 * np.log10(kp / kp.sum() + 1e-30); fd = 10 * np.log10(fp / fp.sum() + 1e-30)
            ok = (kd > kd.max() - FLOOR_DB) & (fd > fd.max() - FLOOR_DB)
            for (freq, _), d, good in zip(k[:n], kd - fd, ok):
                if good:
                    for b, (a, z) in enumerate(edges):
                        if a <= freq < z:
                            samples[b].append(d)
        d = np.array([np.median(s) if len(s) >= MIN_SAMPLES else np.nan for s in samples])
        counts = [len(s) for s in samples]
        valid = ~np.isnan(d)
        d = np.where(valid, d, 0.0)
        d = np.convolve(np.pad(d, 1, mode="edge"), np.ones(3) / 3, mode="valid")
        d = np.where(valid, d, 0.0)
        d -= np.average(d[valid], weights=np.array(counts)[valid]) if valid.any() else 0.0
        d = np.where(np.array(CENTRES) > TOP_HZ, 0.0, np.clip(d, *CLAMP))
        d = np.where(valid, d, 0.0)
        result["regions"][region] = [round(float(v), 2) for v in d]
        print(f"{region:7s} " + " ".join(f"{v:+5.1f}" if ok else "   . " for v, ok in zip(d, valid)))
    print("centres " + " ".join(f"{c / 1000:5.2g}" for c in CENTRES))
    json.dump(result, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()
