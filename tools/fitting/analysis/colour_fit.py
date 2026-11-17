"""Per bridge region, a third-octave colour correction that takes Forte's single notes to the
reference's: for the region's notes at velocities 64 and 96, the power summed over each whole note
(0-2.5 s) per band, reference over forte, smoothed (3 bands), level-neutral over 100 Hz-1 kHz,
clamped. python colour_fit.py reference_sweep.wav forte_sweep.wav out.json"""
import json
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from forte_audio import power_spectrum, read_wav
import sweep_align as sa

CENTRES = [40 * 2 ** (k / 3) for k in range(0, 29)]   # 40 Hz .. 16 kHz
REGION_KEYS = {"bass": (21, 47), "tenor": (48, 59), "middle": (60, 75), "treble": (76, 108)}
VELS = (64, 96)
CLAMP = (-6.0, 6.0)
TOP_CLAMP_HZ, TOP_CLAMP_DB = 9000.0, 4.0   # the reference's hiss lives up here: don't chase it


def band_powers(path):
    """{(midi, velocity): band powers over the note, the file's noise (from its first 1.8 s, as a
    2.5 s note would see it) subtracted}, and that noise."""
    x, rate = read_wav(path)
    mono = x   # stereo: spectra average the channels' powers (issue 0022 §1)
    lead = mono[: int(1.8 * rate)]
    size = int(2.5 * rate)
    lead = np.resize(lead, (size, lead.shape[1]))   # tile the lead-in to the note's length
    pn = power_spectrum(lead)
    fn = np.fft.rfftfreq(size, 1 / rate)
    noise = np.array([pn[(fn >= c / 2 ** (1 / 6)) & (fn < c * 2 ** (1 / 6))].sum() for c in CENTRES])
    out = {}
    for t, m, v, found, _ in sa.onsets(mono, rate):
        if v not in VELS:
            continue
        seg = mono[int((found - 0.004) * rate): int((found + 2.5) * rate)]
        if np.abs(seg).max() > 0.999:
            continue
        p = power_spectrum(seg)
        f = np.fft.rfftfreq(len(seg), 1 / rate)
        bands = np.array([p[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum() for c in CENTRES])
        out[(m, v)] = (np.maximum(bands - noise, 1e-30), bands > NOISE_MARGIN * noise)
    return out


NOISE_MARGIN = 10.0   # a band counts where the note is 10 dB over the noise
ref, forte = band_powers(sys.argv[1]), band_powers(sys.argv[2])
ref_ok = {k: v[1] for k, v in ref.items()}
ref = {k: v[0] for k, v in ref.items()}
forte = {k: v[0] for k, v in forte.items()}
result = {"centresHz": [round(c, 1) for c in CENTRES], "regions": {}}
for region, (lo, hi) in REGION_KEYS.items():
    keys = [k for k in ref if lo <= k[0] <= hi and k in forte]
    # each note weighted equally: normalise each note's powers to its own total
    r = sum(ref[k] / ref[k].sum() for k in keys)
    f = sum(forte[k] / forte[k].sum() for k in keys)
    d = 10 * np.log10(r / f)
    # a band only counts where most of the region's notes are clear of the reference's noise
    clear = np.mean([ref_ok[k] for k in keys], axis=0) >= 0.5
    d = np.where(clear, d, 0.0)
    d = np.convolve(np.pad(d, 1, mode="edge"), np.ones(3) / 3, mode="valid")
    # level-neutral: Forte's notes keep their total power through the correction
    d -= 10 * np.log10(np.sum(f * 10 ** (d / 10)) / np.sum(f))
    d = np.clip(d, *CLAMP)
    d = np.where(np.array(CENTRES) > TOP_CLAMP_HZ, np.minimum(d, TOP_CLAMP_DB), d)
    result["regions"][region] = [round(float(v), 2) for v in d]
    print(f"{region:7s} ({len(keys)} notes) " + " ".join(f"{v:+5.1f}" for v in d))
print("centres  " + " ".join(f"{c/1000:5.2g}" for c in CENTRES))
json.dump(result, open(sys.argv[3], "w"), indent=1)
