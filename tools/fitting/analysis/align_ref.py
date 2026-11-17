"""Aligns a reference render of a piece to a Forte render of the same MIDI: onset-strength envelopes
(10 ms hop) cross-correlated over the first and the last 60 s of music, so a drift shows as two
different lags. Writes the reference shifted (and, if it drifts, resampled) onto Forte's timeline.
python align_ref.py forte.wav reference.wav out.wav"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "seed-irs"))
from pathlib import Path
import numpy as np
from forte_audio import read_wav
from make_seed_irs import float_wav

HOP = 0.01


def onset_env(x, rate):
    p = (x.astype(np.float64) ** 2).mean(axis=1)    # the channels' power, not their sum's
    h = int(HOP * rate)
    e = np.log10(p[: len(p) // h * h].reshape(-1, h).mean(axis=1) + 1e-10)
    d = np.maximum(np.diff(e, prepend=e[0]), 0)
    return d - d.mean()


def lag_between(a, b, a_from, a_to, search=40.0):
    """Lag (s) of b against a, from a's frames a_from..a_to (s)."""
    seg = a[int(a_from / HOP): int(a_to / HOP)]
    best, best_lag = -np.inf, 0.0
    for k in range(int(-search / HOP), int(search / HOP)):
        start = int(a_from / HOP) + k
        if start < 0 or start + len(seg) > len(b):
            continue
        c = float(np.dot(seg, b[start: start + len(seg)]))
        if c > best:
            best, best_lag = c, k * HOP
    return best_lag


fx, rate = read_wav(sys.argv[1])
rx, rrate = read_wav(sys.argv[2])
assert rate == rrate
fa, ra = onset_env(fx, rate), onset_env(rx, rate)
active = np.where(np.abs(fx).max(axis=1) > 0.01)[0]
first, last = active[0] / rate, active[-1] / rate
early = lag_between(fa, ra, first, min(first + 60, last))
late = lag_between(fa, ra, max(first, last - 60), last)
span = max(last - 60 - first, 1.0)
drift = (late - early) / span
print(f"lag early {early:+.3f} s, late {late:+.3f} s, drift {drift * 100:+.3f} %")
# reference time = forte time * (1 + drift) + early - first*drift
t = np.arange(len(fx)) / rate
src = (t - first) * (1 + drift) + first + early
out = np.stack([np.interp(src * rate, np.arange(len(rx)), rx[:, c], left=0, right=0) for c in range(rx.shape[1])], axis=1)
Path(sys.argv[3]).write_bytes(float_wav(out.astype(np.float32), rate))
