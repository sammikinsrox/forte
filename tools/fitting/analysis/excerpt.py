"""Cuts start..start+seconds from each raw render (20 ms fades), level-matches them to the first by
RMS, lowers all together to a -1 dBFS peak, writes 32-bit float WAVs.
python excerpt.py raw_dir out_dir start seconds name ..."""
import os
import sys
from pathlib import Path
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "seed-irs"))
import numpy as np
from forte_audio import read_wav
from make_seed_irs import float_wav

src, dst, start, seconds, names = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), sys.argv[5:]
os.makedirs(dst, exist_ok=True)
clips = {}
for n in names:
    x, rate = read_wav(os.path.join(src, n + ".wav"))
    c = x[int(start * rate): int((start + seconds) * rate)].copy()
    fade = int(0.02 * rate)
    ramp = np.linspace(0, 1, fade)[:, None]
    c[:fade] *= ramp
    c[-fade:] *= ramp[::-1]
    clips[n] = c
rms = lambda c: np.sqrt(np.mean(c ** 2))
ref = rms(clips[names[0]])
for n in names:
    clips[n] *= ref / rms(clips[n])
g = 10 ** (-1 / 20) / max(np.abs(c).max() for c in clips.values())
for n in names:
    Path(dst, n + ".wav").write_bytes(float_wav((clips[n] * g).astype(np.float32), int(rate)))
    print(f"{n:28s} peak {20 * np.log10(np.abs(clips[n] * g).max()):6.1f} dBFS")
