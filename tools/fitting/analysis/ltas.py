"""Long-term third-octave spectrum of whole renders, each re its own total, printed as dB minus the
reference's (first file). Mono sum, silence (below -60 dBFS short-term) left out.
python ltas.py reference.wav a.wav b.wav ..."""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import numpy as np
from forte_audio import read_wav

CENTRES = [50 * 2 ** (k / 3) for k in range(0, 28)]   # 50 Hz .. 12.7 kHz


def ltas(path):
    x, rate = read_wav(path)
    m = x.mean(axis=1)
    size = 8192
    frames = np.lib.stride_tricks.sliding_window_view(m, size)[::size // 2]
    rms = np.sqrt((frames ** 2).mean(axis=1))
    frames = frames[rms > 10 ** (-60 / 20) * rms.max()]
    p = (np.abs(np.fft.rfft(frames * np.hanning(size), axis=1)) ** 2).mean(axis=0)
    f = np.fft.rfftfreq(size, 1 / rate)
    bands = np.array([p[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum() for c in CENTRES])
    return 10 * np.log10(bands / bands.sum())


ref = ltas(sys.argv[1])
print("third-octave centres (kHz): " + " ".join(f"{c/1000:5.2g}" for c in CENTRES))
print(f"{'reference (dB re total)':30s} " + " ".join(f"{v:5.0f}" for v in ref))
for p in sys.argv[2:]:
    d = ltas(p) - ref
    print(f"{os.path.basename(p)[:30]:30s} " + " ".join(f"{v:+5.1f}" for v in d))
