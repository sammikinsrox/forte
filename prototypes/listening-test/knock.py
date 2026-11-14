# PROTOTYPE (throwaway): onset "thock" check. Low-pass the note, compare the first-30 ms peak against
# the level 80-200 ms in. A wooden knock shows up as a big early peak that's gone by 80 ms.
import json
import numpy as np
from scipy.signal import butter, sosfiltfilt
import forte_proto as fp, song

SR = fp.SR
colour = json.loads((fp.OUT / "colour.json").read_text())
fp.COLOUR = (np.array(colour[0]), np.array(colour[1]))
fits = json.loads((fp.OUT / "song_fits.json").read_text())

def thock(x, lo, hi):
    m = x.mean(axis=1) if x.ndim == 2 else x
    y = sosfiltfilt(butter(4, [lo, hi], "bp", fs=SR, output="sos"), m[:int(0.4 * SR)])
    e = np.sqrt(np.convolve(y ** 2, np.ones(220) / 220, "same"))
    k0 = int(np.argmax(e > 0.05 * e.max()))
    early = e[k0:k0 + int(0.03 * SR)].max(); later = e[k0 + int(0.08 * SR):k0 + int(0.2 * SR)].mean()
    return 20 * np.log10(early / later)

def forte(m, fi, ks, phantom=1.0, board=True):
    song.VARIANT = dict(song.VARIANTS["b"], phantom=phantom)
    model = song.Note(m, fi, ks)
    dry = np.zeros(int(0.6 * SR)); song.render_note(model, [(0.01, 0.55, 1.5)], dry.size, dry)
    dry = song.sub_fundamental_hp(dry, fi["f0"])
    if not board: return dry[int(0.005 * SR):]
    ir = fp.region_ir(m)
    return np.stack([np.convolve(dry, ir[:, c])[:dry.size] for c in range(2)], 1)[int(0.005 * SR):]

bands = [(60, 250), (250, 600), (600, 1500)]
print("early-peak / later level, dB (bigger = more knock).  bands:", bands)
for name in ["C2", "G2", "D3", "C4", "E4", "G4", "C5"]:
    m = song.mid(name); fit = fits[str(m)]
    fi = {**fit, "partials": {int(k): v for k, v in fit["partials"].items()}}
    x = fp.read_aiff(fp.REFS / f"Piano.mf.{name}.aiff"); xr = x[fp.onset(x):]
    rows = [("Steinway", xr), ("Forte", forte(m, fi, fit["ks_mf"])),
            ("Forte no phantoms", forte(m, fi, fit["ks_mf"], phantom=0.0)),
            ("Forte no board", forte(m, fi, fit["ks_mf"], board=False))]
    print(name, "  ".join(f"{lab}: " + "/".join(f"{thock(y, lo, hi):+.1f}" for lo, hi in bands) for lab, y in rows))
    def lf_share(x):
        mm = x.mean(axis=1) if x.ndim == 2 else x
        e = np.abs(mm); k0 = int(np.argmax(e > 0.05 * e.max())); seg = mm[k0:k0 + int(0.03 * SR)]
        S = np.abs(np.fft.rfft(seg, 1 << 14)) ** 2; f = np.fft.rfftfreq(1 << 14, 1 / SR)
        return 10 * np.log10(S[(f > 60) & (f < 250)].sum() / S[f > 60].sum())
    print("   share of first-30 ms energy in 60-250 Hz (dB):  " + "  ".join(f"{lab} {lf_share(y):+.1f}" for lab, y in rows[:2]))
