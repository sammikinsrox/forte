"""Level-match renders to the first by RMS, lower all together so the loudest peak is -1 dBFS,
print RMS, peak and the six band shares. python match_song.py in_dir out_dir a b c ..."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import numpy as np
import shutil
from forte_audio import read_wav, scale_float_wav

src, dst, names = sys.argv[1], sys.argv[2], sys.argv[3:]
os.makedirs(dst, exist_ok=True)
xs = {}
for n in names:
    x, rate = read_wav(os.path.join(src, n + ".wav"))
    xs[n] = x
rms = lambda x: np.sqrt(np.mean(x ** 2))
ref = rms(xs[names[0]])
gains = {}
for n in names:
    gains[n] = ref / rms(xs[n])
    xs[n] = xs[n] * gains[n]
peak = max(np.abs(x).max() for x in xs.values())
g = 10 ** (-1 / 20) / peak
edges = [0, 250, 500, 1000, 3500, 8000, rate / 2]
print(f"common gain {20*np.log10(g):+.1f} dB; bands <250 250-500 0.5-1k 1-3.5k 3.5-8k >8k (dB share)")
for n in names:
    y = xs[n] * g
    out = os.path.join(dst, n + ".wav")
    shutil.copyfile(os.path.join(src, n + ".wav"), out)
    scale_float_wav(out, g * gains[n])
    m = y.mean(axis=1)
    s = np.abs(np.fft.rfft(m)) ** 2
    f = np.fft.rfftfreq(len(m), 1 / rate)
    tot = s.sum()
    shares = [10 * np.log10(s[(f >= a) & (f < b)].sum() / tot) for a, b in zip(edges[:-1], edges[1:])]
    print(f"{n:32s} peak {20*np.log10(np.abs(y).max()):6.1f}  " + " ".join(f"{v:6.1f}" for v in shares))
