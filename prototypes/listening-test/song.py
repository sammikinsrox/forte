# PROTOTYPE (throwaway): render J.S. Bach, Prelude in C major BWV 846 (public domain) twice:
#   1. Forte's physical model (event-driven: strikes, restrikes of ringing strings, damper release)
#   2. a reference built from the Iowa MIS Steinway mf recordings of each note (simple sampler)
# Notes are my own transcription of the public-domain score.
#
#   python song.py           (needs out/colour.json and out/report.json from forte_proto.py)

import json, math
from pathlib import Path
import numpy as np
import sys
from scipy.signal import fftconvolve, bilinear, lfilter, butter, sosfilt
import forte_proto as fp

# Listening variants (pick by ear): spread = unison detuning in cents (None = old per-partial fit)
VARIANTS = {
    "a": dict(spread=None, phantom=1.0),   # brighter hammers only
    "b": dict(spread=0.7, phantom=1.0),    # + real unison detuning on every partial (shimmer)
    "c": dict(spread=1.4, phantom=2.0),    # + wider detuning, stronger phantom partials (more twang)
}
VARIANT = VARIANTS["a"]

SR = fp.SR
OUT = fp.OUT
NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
def nm(m): return f"{NAMES[m % 12]}{m // 12 - 1}"
def mid(s):
    s = s.replace("#", "s")
    sharp = {"Cs": "Db", "Ds": "Eb", "Fs": "Gb", "Gs": "Ab", "As": "Bb"}
    p, o = (s[:-1], int(s[-1]))
    p = sharp.get(p, p)
    return NAMES.index(p) + 12 * (o + 1)

# ---------- score -------------------------------------------------------------------------

BARS = """C4 E4 G4 C5 E5|C4 D4 A4 D5 F5|B3 D4 G4 D5 F5|C4 E4 G4 C5 E5|C4 E4 A4 E5 A5|C4 D4 F#4 A4 D5|
B3 D4 G4 D5 G5|B3 C4 E4 G4 C5|A3 C4 E4 G4 C5|D3 A3 D4 F#4 C5|G3 B3 D4 G4 B4|G3 Bb3 E4 G4 C#5|
F3 A3 D4 A4 D5|F3 Ab3 D4 F4 B4|E3 G3 C4 G4 C5|E3 F3 A3 C4 F4|D3 F3 A3 C4 F4|G2 D3 G3 B3 F4|
C3 E3 G3 C4 E4|C3 G3 Bb3 C4 E4|F2 F3 A3 C4 E4|F#2 C3 A3 C4 Eb4|Ab2 F3 B3 C4 D4|G2 F3 G3 B3 D4|
G2 E3 G3 C4 E4|G2 D3 G3 C4 F4|G2 D3 G3 B3 F4|G2 Eb3 A3 C4 F#4|G2 E3 G3 C4 G4|G2 D3 G3 C4 F4|
G2 D3 G3 B3 F4|C2 C3 G3 Bb3 E4"""
BAR33 = ("C2", "C3", "F3 A3 C4 F4 C4 A3 C4 A3 F3 A3 F3 D3 F3 D3")
BAR34 = ("C2", "B2", "G3 B3 D4 F4 D4 B3 D4 B3 G3 B3 D3 F3 E3 D3")
FINAL = "C2 C3 E4 G4 C5"

def score(seed=3):
    r = np.random.default_rng(seed)
    bars = [b.split() for b in BARS.replace("\n", "").split("|")]
    ev = []                    # (t_on, t_off, midi, velocity 1..127)
    t = 0.0
    s16 = 60 / 66 / 4          # quarter = 66
    total = len(bars) + 3
    def shape(bi): return 62 + 14 * math.sin(math.pi * min(bi, 30) / 30) ** 1.5
    def hv(v): return float(np.clip(v + r.normal(0, 3), 30, 110))
    def ht(x): return x + r.normal(0, 0.006)
    for bi, ch in enumerate(bars):
        p = [mid(x) for x in ch]
        for half in range(2):
            st = [t + k * s16 for k in range(8)]
            base = shape(bi + half / 2)
            ev.append((ht(st[0]), st[0] + 8 * s16 - 0.02, p[0], hv(base + 6)))
            ev.append((ht(st[1]), st[0] + 8 * s16 - 0.02, p[1], hv(base)))
            for k, idx in enumerate([2, 3, 4, 2, 3, 4]):
                on = ht(st[2 + k]); ev.append((on, on + 1.15 * s16, p[idx], hv(base - 4 + 3 * (k % 3 == 2))))
            t += 8 * s16
    for bi, (b1, b2, run) in enumerate([BAR33, BAR34]):
        s = s16 * (1.06 + 0.08 * bi)
        st = [t + k * s for k in range(16)]
        ev.append((ht(st[0]), st[0] + 16 * s - 0.02, mid(b1), hv(70)))
        ev.append((ht(st[1]), st[0] + 16 * s - 0.02, mid(b2), hv(64)))
        for k, x in enumerate(run.split()):
            on = ht(st[2 + k]); ev.append((on, on + 1.15 * s, mid(x), hv(64 - 2 * bi - k * 0.4)))
        t += 16 * s
    t += 0.1
    for k, x in enumerate(FINAL.split()):     # gently rolled final chord, held ~5 s
        ev.append((t + 0.03 * k, t + 5.5, mid(x), hv(66)))
    return sorted(ev), t + 8.5

# ---------- per-note model ------------------------------------------------------------------

def damper_sigma(f): return 8 + 15 * np.sqrt(f / 1000)   # extra decay (1/s) with damper fully down

class Note:
    def __init__(self, midi, fit, ks):
        sp = fp.string_params(midi)
        self.midi, self.N, self.Mh, self.p = midi, sp["N"], sp["Mh"], sp["p"]
        self.K = sp["K"] * ks
        L, T, a = sp["L"], sp["T"], sp["a"]
        f0, B = fit["f0"], fit["B"]
        Ms = T / (4 * L * L * f0 * f0) * L
        dec = fp.decay_model(fit, f0)
        nmax = 1
        while nmax * f0 * math.sqrt(1 + B * nmax * nmax) < min(20000, 0.45 * SR): nmax += 1
        lam, exc, out, mean, pid = [], [], [], [], []
        self.fn = []
        # Fixed per-string tuning offsets (cents), the same for every partial of a string, as on a
        # real piano. spread=None keeps the old per-partial fit of detuning to the measured aftersound.
        if VARIANT["spread"] is not None and self.N > 1:
            c = VARIANT["spread"]
            det = (np.array([-0.5, 0.5]) if self.N == 2 else np.array([-1.0, 0.2, 0.85])) * c
            det = 2 ** (det / 1200) - 1
        else:
            det = None
        for n in range(1, nmax):
            f = n * f0 * math.sqrt(1 + B * n * n); w = 2 * math.pi * f
            sf, sl, fr = dec(n, f)
            if det is not None and n <= 32:
                eps = (sf - sl) / self.N
                M = np.diag(-sl + 1j * w * (1 + det)) - eps * np.ones((self.N, self.N))
                l, V = np.linalg.eig(M); Vi = np.linalg.inv(V)
                lam += list(l); exc += list(Vi @ np.ones(self.N)); out += list(np.ones(self.N) @ V)
                mean += list(V.mean(axis=0)); pid += [n] * self.N
            elif det is not None:                                  # upper partials: detuned, uncoupled
                lam += list(-sf + 1j * w * (1 + det)); exc += [1.0] * self.N; out += [1.0] * self.N
                mean += [1.0 / self.N] * self.N; pid += [n] * self.N
            elif n <= 32 and self.N > 1:
                l, V, Vi = fp.coupled_modes(self.N, w, sf, sl, fr)
                lam += list(l); exc += list(Vi @ np.ones(self.N)); out += list(np.ones(self.N) @ V)
                mean += list(V.mean(axis=0)); pid += [n] * self.N
            else:
                lam.append(-sf + 1j * w); exc.append(1.0); out.append(float(self.N)); mean.append(1.0); pid.append(n)
            self.fn.append(f)
        pid = np.array(pid); self.pid = pid
        fpart = pid * f0 * np.sqrt(1 + B * pid * pid)
        self.lam = np.array(lam)
        phi = np.sin(pid * np.pi * a)
        beta = (2 * phi / Ms) / (2 * np.pi * fpart)
        self.drive = np.array(exc) * beta / self.N                 # per unit hammer force
        self.ysc = phi * np.array(mean)                            # strike-point displacement
        # Bridge force = string slope at the BRIDGE end (x = L), far from the hammer (strike at a*L from
        # the agraffe). Slope of sin(n pi x / L) there carries (-1)^n. Measuring at x = 0 (next to the
        # hammer) passed ~88% of the hammer blow straight to the soundboard: the onset "thock".
        self.outc = (T * pid * np.pi / L) * (-1.0) ** pid * np.array(out)
        self.meanc = np.array(mean)
        self.sd = damper_sigma(fpart)
        self.w = np.zeros(self.lam.size, complex)
        self.L = L
        # phantom partials (lowest ~30 notes)
        self.ph = midi <= 50
        if self.ph:
            self.EA = 2e11 * math.pi * (0.6e-3) ** 2 * 1.6
            fL1 = 1100.0 if midi < 45 else 1500.0
            self.phf = []
            for kL in range(1, 11):
                wL = 2 * math.pi * kL * fL1; z = 1 / 120
                b, aa = bilinear([wL * wL], [1, 2 * z * wL, wL * wL], SR)
                self.phf.append([b, aa, np.zeros(2)])
            self.phsel = pid <= 30
            self.hp = list(butter(1, 40, "hp", fs=SR)) + [np.zeros(1)]   # no slow tension "thump"

    def partial_disp(self, Wt):
        """Mean string displacement per partial (31 x n) from modal states over time (modes x n)."""
        sel = self.phsel
        Qm = (self.meanc[sel][:, None] * Wt[sel]).imag
        Q = np.zeros((31, Wt.shape[1])); np.add.at(Q, self.pid[sel], Qm)
        return Q

    def phantom(self, Q):
        n = Q.shape[1]; yl = np.zeros(n)
        for kL, filt in enumerate(self.phf, start=1):
            D = np.zeros(n)
            for m in range(1, 31):
                for n2 in (kL - m, m - kL, m + kL):
                    if 1 <= n2 <= 30: D += m * n2 * Q[m] * Q[n2]
            D *= (math.pi / self.L) ** 2 * self.EA / 4
            o, filt[2] = lfilter(filt[0], filt[1], D, zi=filt[2])
            yl += o
        yl, self.hp[2] = lfilter(self.hp[0], self.hp[1], yl, zi=self.hp[2])
        return VARIANT["phantom"] * 0.3 * self.N * yl

    def strike(self, vh, damp):
        """Hammer contact at 4x from the current (possibly ringing) string state. Returns output samples."""
        os = 4; dt = 1 / (SR * os)
        E = np.exp((self.lam - self.sd * damp) * dt)
        w = self.w
        yh = float(np.dot(self.ysc, w).imag); v = vh; prev = 0.0
        outs = []; over = 0; states = []
        for step in range(int(0.02 * SR * os)):
            ys = float(np.dot(self.ysc, w).imag)
            d = yh - ys
            dp = max(d, 0.0) ** self.p
            F = max(0.0, self.K * (dp + 1e-5 * (dp - prev) / dt)) if d > 0 else 0.0
            prev = dp
            w = E * w + self.drive * (dt * F)
            v -= F / self.Mh * dt; yh += v * dt
            if step % os == 0:
                outs.append(float(np.dot(self.outc, w).imag))
                if self.ph: states.append(w)
            if d <= 0 and v < 0:
                over += 1
                if over > 200 and step % os == os - 1: break
            else:
                over = 0
        self.w = w
        y = np.array(outs)
        if self.ph:     # keep the phantom-partial path running through the contact (was the pop)
            y = y + self.phantom(self.partial_disp(np.array(states).T))
        return y

    def evolve(self, n, damp):
        lam = self.lam - self.sd * damp
        t = np.arange(n) / SR
        Z = np.exp(np.outer(lam, t))
        y = ((self.outc * self.w) @ Z).imag
        if self.ph:
            y = y + self.phantom(self.partial_disp(self.w[:, None] * Z))
        self.w = self.w * np.exp(lam * n / SR)
        return y

    def level(self): return float(np.abs(self.outc * self.w).sum())

def sub_fundamental_hp(x, f0):
    """2nd-order high-pass at half the fundamental: a string has no sustained energy below f0, so this
    only removes the low strike transient (G4/C5 were still 7-12 dB thumpier than the Steinway)."""
    if f0 < 100: return x
    return sosfilt(butter(2, 0.5 * f0, "hp", fs=SR, output="sos"), x)

def render_note(model, events, length, bus):
    """events: list of (t_on, t_off, hammer_speed). Adds this note's output into bus (float array)."""
    CH = 4096
    pos = 0; damp = 1.0; peak = 1e-30
    ramps = []   # (sample, damp_value) breakpoints
    strikes = [(int(round(on * SR)), vh) for on, off, vh in events]
    ons = [int(round(on * SR)) for on, _, _ in events]
    for on, off, vh in events:
        s0 = int(round(off * SR))
        for k in range(1, 5):
            s = s0 + k * int(0.005 * SR)
            if not any(s0 < o <= s for o in ons):       # a new strike lifts the damper again
                ramps.append((s, 0.25 * k))
        ramps.append((int(round(on * SR)), 0.0))
    marks = sorted(set([s for s, _ in strikes] + [s for s, _ in ramps] + [length]))
    si = 0
    for mk in marks:
        # evolve up to mk
        while pos < mk:
            n = min(CH, mk - pos)
            if model.level() < peak * 1e-5 and damp >= 1.0:
                pos = mk; break                                   # resting: inaudible and damped
            bus[pos:pos + n] += model.evolve(n, damp)
            pos += n
        if mk >= length: break
        # apply what happens at mk: damper changes, then strikes
        for s, d in ramps:
            if s == mk: damp = d
        for s, vh in strikes:
            if s == mk:
                y = model.strike(vh, 0.0)
                bus[mk:mk + y.size] += y[:max(0, length - mk)]
                pos = mk + y.size
                peak = max(peak, model.level())

def main():
    global VARIANT, VNAME
    VNAME = sys.argv[1] if len(sys.argv) > 1 else "a"
    VARIANT = VARIANTS[VNAME]
    colour = json.loads((OUT / "colour.json").read_text())
    fp.COLOUR = (np.array(colour[0]), np.array(colour[1]))
    rep = json.loads((OUT / "report.json").read_text())
    kmid = [fp.NOTES[k] for k in rep]; kval = [math.log(rep[k]["Kscale"]) for k in rep]
    events, length_s = score()
    length = int(length_s * SR)
    pitches = sorted({m for _, _, m, _ in events})
    print(f"{len(events)} notes, {len(pitches)} keys ({nm(pitches[0])}-{nm(pitches[-1])}), {length_s:.0f} s")

    cache = OUT / "song_fits.json"
    fits = json.loads(cache.read_text()) if cache.exists() else {}
    buses = np.zeros((5, length)); ref = np.zeros((length + 10 * SR, 2))
    V_MF = 1.5
    for m in pitches:
        x = fp.read_aiff(fp.REFS / f"Piano.mf.{nm(m)}.aiff"); xr = x[fp.onset(x):]
        ks = math.exp(np.interp(m, kmid, kval))
        if str(m) not in fits:
            fit = fp.analyse(x, m)
            fit["partials"] = {str(k): v for k, v in fit["partials"].items()}
            y = fp.render(m, {**fit, "partials": {int(k): v for k, v in fit["partials"].items()}}, V_MF, ks, dur=1.0, phantoms=False)
            fit["gain"] = fp.rms(xr) / fp.rms(y)
            fits[str(m)] = fit
            cache.write_text(json.dumps(fits))
        fit = fits[str(m)]
        fit_i = {**fit, "partials": {int(k): v for k, v in fit["partials"].items()}}
        if "ks_mf" not in fit:
            # per-note hammer felt fitted to this note's mf recording (the Prelude is played around mf)
            pr_ref = fp.partial_profile(xr, fit_i, m)
            best = None
            for k in np.geomspace(0.1, 10, 11):
                pr = fp.partial_profile(fp.render(m, fit_i, V_MF, k, dur=0.4, phantoms=False), fit_i, m)
                q = min(len(pr), len(pr_ref))
                err = float(np.mean(np.abs(np.maximum(pr[:q], -60) - np.maximum(pr_ref[:q], -60))))
                if best is None or err < best[0]: best = (err, k)
            fit["ks_mf"] = float(best[1])
            fit["gain_mf"] = fp.rms(xr) / fp.rms(fp.render(m, fit_i, V_MF, fit["ks_mf"], dur=1.0, phantoms=False))
            cache.write_text(json.dumps(fits))
        ks = fit["ks_mf"]
        evs = [(on, off, V_MF * (vel / 66) ** 2) for on, off, mm, vel in events if mm == m]
        model = Note(m, fit_i, ks)
        b = np.zeros(length)
        render_note(model, evs, length, b)
        buses[fp.region_of(m)] += sub_fundamental_hp(b, fit["f0"]) * fit["gain_mf"]
        # reference sampler: Iowa mf note, gain by velocity, damper approximated by a fade after key-up
        for on, off, mm, vel in events:
            if mm != m: continue
            s0 = int(on * SR); n = min(xr.shape[0], ref.shape[0] - s0)
            env = np.ones(n); k0 = int((off - on) * SR)
            if k0 < n: env[k0:] = np.exp(-np.arange(n - k0) / (0.08 * SR))
            ref[s0:s0 + n] += xr[:n] * env[:, None] * (vel / 66) ** 2
        print(f"  {nm(m)}: {len(evs)} strikes, hammer felt x{ks:.2f}")

    out = np.zeros((length, 2))
    for r in range(1, 5):
        if not buses[r].any(): continue
        ir = fp.region_ir([0, 36, 50, 62, 80][r])
        for c in range(2): out[:, c] += fftconvolve(buses[r], ir[:, c])[:length]
    ref = ref[:length]
    out *= math.sqrt((ref ** 2).mean() / (out ** 2).mean())        # same average loudness
    norm = 0.6 / np.abs(ref).max()                                  # fixed, so every variant shares one level
    fname = f"prelude_forte_{VNAME}.wav"
    for name, x in ((fname, out), ("prelude_ref.wav", ref)):
        fp.write_wav(OUT / name, x * norm)
    print("written", OUT / fname, "peak", round(float(np.abs(out).max() * norm), 2))

if __name__ == "__main__":
    main()
