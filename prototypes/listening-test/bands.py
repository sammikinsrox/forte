# PROTOTYPE (throwaway): octave-band balance of the two prelude renders, relative to the 500 Hz band.
import wave
from pathlib import Path
import numpy as np

def load(p):
    w = wave.open(str(p)); b = np.frombuffer(w.readframes(w.getnframes()), np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16); v = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608
    return v.reshape(-1, 2).mean(axis=1)

OUT = Path(__file__).resolve().parent / "out"
bands = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
for n in ["prelude_ref", "prelude_forte_a", "prelude_forte_b", "prelude_forte_c"]:
    x = load(OUT / f"{n}.wav")[:44100 * 120]
    S = np.abs(np.fft.rfft(x)) ** 2; f = np.fft.rfftfreq(x.size, 1 / 44100)
    p = [10 * np.log10(S[(f >= b / 1.414) & (f < b * 1.414)].sum()) for b in bands]
    print(n.ljust(14), " ".join(f"{b}:{v - p[3]:+5.1f}" for b, v in zip(bands, p)))
