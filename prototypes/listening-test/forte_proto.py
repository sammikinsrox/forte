# PROTOTYPE (throwaway) - single-note listening test for "Choose the synthesis architecture".
# Analyses Iowa MIS reference notes, then renders the same notes with the chosen physical model:
# modal strings + nonlinear felt hammer (4x contact) + Weinreich coupled unisons + phantom partials
# + synthesized stereo soundboard IR for the note's bridge region. Writes WAVs and an A/B page.
#
#   python -I forte_proto.py            (refs expected in ../../refs/iowa)

import json, math, struct, sys, wave
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve, stft

HERE = Path(__file__).resolve().parent
REFS = HERE.parents[1] / "refs" / "iowa"
OUT = HERE / "out"
SR = 44100
NOTES = {"C2": 36, "C4": 60, "C6": 84}
DYNS = ["pp", "mf", "ff"]
RNG = np.random.default_rng(7)

# ---------- reference I/O ----------------------------------------------------------------------

def read_aiff(path):
    b = Path(path).read_bytes()
    assert b[:4] == b"FORM" and b[8:12] == b"AIFF"
    i, ch, bits, data = 12, None, None, None
    while i < len(b):
        cid, size = b[i:i + 4], struct.unpack(">I", b[i + 4:i + 8])[0]
        body = b[i + 8:i + 8 + size]
        if cid == b"COMM":
            ch, _, bits = struct.unpack(">hIh", body[:8])
        elif cid == b"SSND":
            off = struct.unpack(">I", body[:4])[0]
            data = body[8 + off:]
        i += 8 + size + (size & 1)
    assert bits == 16
    x = np.frombuffer(data, dtype=">i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch)

def write_wav(path, x):
    x = np.clip(x, -1, 1)
    v = (x * 8388607).astype("<i4")
    pcm = np.stack([(v & 0xFF), (v >> 8) & 0xFF, (v >> 16) & 0xFF], axis=-1).astype(np.uint8)
    with wave.open(str(path), "wb") as w:                       # 24-bit, so quiet tails don't truncate
        w.setnchannels(x.shape[1]); w.setsampwidth(3); w.setframerate(SR)
        w.writeframes(pcm.tobytes())

def onset(x):
    # 5 ms RMS envelope; first point above 10% of its maximum (robust to pre-roll noise on pp files)
    m = np.sqrt(np.convolve((x ** 2).mean(axis=1), np.ones(220) / 220, mode="same"))
    return max(0, int(np.argmax(m > 0.1 * m.max())) - int(0.008 * SR))

# ---------- analysis: partial frequencies, inharmonicity, two-stage decay -----------------------

def analyse(x, midi):
    f0n = 440 * 2 ** ((midi - 69) / 12)
    mono = x.mean(axis=1)
    o = max(0, onset(x)); mono = mono[o:]
    seg = mono[int(0.05 * SR):int(1.55 * SR)] * np.hanning(int(1.5 * SR))
    nfft = 1 << 18
    S = np.abs(np.fft.rfft(seg, nfft)); fr = np.fft.rfftfreq(nfft, 1 / SR)
    floor_db = 20 * np.log10(np.median(S) + 1e-12)

    def peak_near(f, w):
        k = (fr > f - w) & (fr < f + w)
        if not k.any(): return None, -300
        j = np.flatnonzero(k)[np.argmax(S[k])]
        return fr[j], 20 * np.log10(S[j] + 1e-12)

    f0, B = f0n, 0.0
    found = {}
    for n in range(1, 60):
        pred = n * f0 * math.sqrt(1 + B * n * n)
        if pred > 0.45 * SR: break
        f, db = peak_near(pred, 0.25 * f0)
        if f is None or db < floor_db + 20: continue
        found[n] = f
        if len(found) >= 3:                       # refit (f_n/n)^2 = f0^2 + f0^2 B n^2
            ns = np.array(list(found)); fs = np.array([found[k] for k in ns])
            a, c = np.polyfit(ns ** 2.0, (fs / ns) ** 2, 1)
            f0, B = math.sqrt(c), max(0.0, a / c)

    # decay tracking on an STFT, per found partial
    win = 8192 if midi < 48 else 4096
    fz, tz, Z = stft(mono, SR, nperseg=win, noverlap=win - 1024)
    A = 20 * np.log10(np.abs(Z) + 1e-12)
    nf = 20 * np.log10(np.median(np.abs(Z), axis=0) + 1e-12)     # per-frame noise-ish floor
    noise = np.percentile(A[:, -20:], 50, axis=1)                 # tail = background noise per bin
    partials = {}
    for n, f in found.items():
        j = int(round(f / (SR / win)))
        tr = A[max(0, j - 1):j + 2].max(axis=0)
        fl = noise[max(0, j - 1):j + 2].max() + 10
        pk = int(np.argmax(tr[:40]))
        t, d = tz[pk:], tr[pk:]
        valid = d > fl
        if valid.sum() < 6: continue
        last = pk + int(np.flatnonzero(valid)[-1]) if valid.any() else pk
        t, d = tz[pk:last + 1], tr[pk:last + 1]
        drop = d[0] - d
        e = drop < min(12, drop.max() * 0.5)
        early = np.polyfit(t[e], d[e], 1) if e.sum() >= 3 else np.polyfit(t[:3], d[:3], 1)
        l = drop > 15
        late = np.polyfit(t[l], d[l], 1) if l.sum() >= 4 else early
        se, sl = -early[0] / 8.686, -late[0] / 8.686            # amplitude decay rate, 1/s
        se, sl = max(se, 0.05), max(sl, 0.05)
        if sl > se: sl = se
        slow_frac = 10 ** ((np.polyval(late, t[0]) - np.polyval(early, t[0])) / 20)
        partials[n] = dict(f=float(f), db=float(d[0]), sig_fast=float(se), sig_slow=float(sl),
                           slow_frac=float(min(1.0, slow_frac)))
    return dict(f0=f0, B=B, partials=partials, onset=o)

# ---------- physical parameters (Chaigne & Askenfelt 1994 seeds, interpolated) --------------------

def string_params(midi):
    # length, tension, strings per unison, strike ratio, hammer mass/stiffness/exponent
    t = (midi - 36) / 48.0                      # 0 at C2 .. 1 at C6
    L = float(np.exp(np.interp(midi, [36, 60, 84, 96], np.log([1.90, 0.62, 0.18, 0.09]))))
    T = 700.0 + 50 * math.cos(t * math.pi)
    N = 2 if midi < 42 else 3                  # bichords below ~F#2 on a concert grand
    a = float(np.interp(midi, [36, 60, 84, 96], [0.12, 0.12, 0.10, 0.0625]))
    Mh = float(np.interp(midi, [36, 60, 84, 96], [4.9e-3, 2.97e-3, 2.5e-3, 2.2e-3]))
    p = float(np.interp(midi, [36, 60, 84, 96], [2.3, 2.5, 2.8, 3.0]))
    K = float(np.exp(np.interp(midi, [36, 60, 84, 96], np.log([4e8, 4.5e9, 3e10, 1e11]))))
    return dict(L=L, T=T, N=N, a=a, Mh=Mh, p=p, K=K)

def decay_model(fit, f0):
    """Per-partial (fast, slow, slow_frac) with b1 + b3 f^2 extrapolation above the measured ones."""
    P = fit["partials"]
    ns = np.array(sorted(P)); fs = np.array([P[n]["f"] for n in ns])
    def fitlaw(key):
        y = np.array([P[n][key] for n in ns])
        A = np.vstack([np.ones_like(fs), fs ** 2]).T
        c, *_ = np.linalg.lstsq(A, y, rcond=None)
        return lambda f: max(0.3, c[0] + max(c[1], 1e-7) * f * f)
    lf, ls = fitlaw("sig_fast"), fitlaw("sig_slow")
    fr = float(np.median([P[n]["slow_frac"] for n in ns]))
    def get(n, f):
        if n in P: return P[n]["sig_fast"], P[n]["sig_slow"], P[n]["slow_frac"]
        sf = lf(f); return sf, min(ls(f), sf), fr
    return get

# ---------- Weinreich coupled unison modes ----------------------------------------------------

def coupled_modes(N, w, sig_fast, sig_slow, slow_frac):
    """Return eigenvalues lam (N,), output weights out (N,), and V, Vinv for string<->modal coords."""
    s0 = -sig_slow + 1j * w
    eps = (sig_fast - sig_slow) / N
    best = None
    for d in np.geomspace(1e-5, 3e-3, 40):     # relative detuning spread
        det = np.linspace(-d, d, N) if N > 1 else np.zeros(1)
        M = np.diag(s0 + 1j * w * det) - eps * np.ones((N, N))
        lam, V = np.linalg.eig(M); Vi = np.linalg.inv(V)
        wexc = Vi @ np.ones(N) / N                       # hammer drives strings equally
        outw = np.ones(N) @ V
        amp = np.abs(outw * wexc)
        slow = amp[np.argmax(lam.real)] / amp.sum()
        err = abs(math.log(max(slow, 1e-6)) - math.log(max(slow_frac, 1e-6)))
        if best is None or err < best[0]: best = (err, lam, V, Vi)
    return best[1], best[2], best[3]

# ---------- synthesized soundboard IR (research 0012, option B; uncalibrated) ----------------------

def soundboard_ir(region_seed, length=1.0):
    r = np.random.default_rng(region_seed)
    t = np.arange(int(length * SR)) / SR
    low = [50, 62, 90, 105, 127, 187, 222, 245, 325]
    f_hi = []
    f = 300.0
    while f < 12000:                              # concert grand: ~2x upright density below 1.1 kHz
        dens = 0.12 if f < 1100 else 0.12 * (1100 / f) ** 0.5
        f += r.exponential(1 / dens); f_hi.append(f)
    freqs = np.array(low + f_hi)
    eta = r.uniform(0.01, 0.03, freqs.size)
    alpha = math.pi * freqs * eta
    # colour: radiation rises to ~500 Hz, mobility flat 500 Hz-5 kHz, gentle roll-off above
    col = (freqs / 500) ** 1.5 / (1 + (freqs / 500) ** 1.5) / (1 + (freqs / 6000) ** 2)
    ir = np.zeros((t.size, 2))
    for c in range(2):
        for _ in range(1):
            g = col * r.normal(0, 1, freqs.size) / np.sqrt(alpha)
            ph = r.uniform(0, 2 * np.pi, freqs.size)
            for k0 in range(0, freqs.size, 64):
                sl = slice(k0, k0 + 64)
                ir[:, c] += (g[sl, None] * np.exp(-alpha[sl, None] * t) *
                             np.sin(2 * np.pi * freqs[sl, None] * t + ph[sl, None])).sum(0)
    ir[:, :] /= np.sqrt((ir ** 2).sum(0).max())
    # Smooth (non-resonant) part. At 0.35 the random modes alone swung partial levels by up to
    # +/-20 dB (C4 came out hollow); 2.0 keeps the swings around +/-3-6 dB. Plath calibration must set this.
    direct = np.zeros_like(ir); direct[0] = [2.0, 2.0]
    ir = ir + direct
    if COLOUR is not None:                                  # one global calibration curve (stand-in for Plath)
        F = np.fft.rfft(ir, axis=0); f = np.fft.rfftfreq(ir.shape[0], 1 / SR)
        gdb = np.interp(np.log2(np.maximum(f, 20)), np.log2(COLOUR[0]), COLOUR[1])
        ir = np.fft.irfft(F * 10 ** (gdb[:, None] / 20), n=ir.shape[0], axis=0)
    return ir

COLOUR = None      # (band centre Hz, gain dB), fitted in main()
_IR = {}
def region_ir(midi):
    key = (region_of(midi), None if COLOUR is None else COLOUR[1].tobytes())
    if key not in _IR: _IR[key] = soundboard_ir(region_of(midi))
    return _IR[key]

def region_of(midi):            # bridge regions: 1 bass, 2 tenor, 3 middle, 4 treble
    return 1 if midi < 48 else 2 if midi < 60 else 3 if midi < 76 else 4

# ---------- render one note -----------------------------------------------------------------

def render(midi, fit, vh, Kscale=1.0, dur=None, phantoms=True):
    sp = string_params(midi)
    L, T, N, a, Mh, p = sp["L"], sp["T"], sp["N"], sp["a"], sp["Mh"], sp["p"]
    K = sp["K"] * Kscale
    f0, B = fit["f0"], fit["B"]
    mu = T / (4 * L * L * f0 * f0); Ms = mu * L
    dec = decay_model(fit, f0)
    nmax = 1
    while nmax * f0 * math.sqrt(1 + B * nmax * nmax) < min(20000, 0.45 * SR): nmax += 1
    ns = np.arange(1, nmax)
    fn = ns * f0 * np.sqrt(1 + B * ns * ns)
    wn = 2 * np.pi * fn
    phi = np.sin(ns * np.pi * a)
    g = 2 * phi / Ms                               # modal force gain per string
    sig = np.array([dec(n, f)[0] for n, f in zip(ns, fn)])

    # --- hammer contact at 4x, strings uncoupled (contact lasts a few ms) ---
    os = 4; dt = 1 / (SR * os)
    s = -sig + 1j * wn
    E = np.exp(s * dt); beta = g / wn
    c = np.zeros(ns.size, complex)                # identical for each string (equal drive)
    yh, v = 0.0, vh
    prev = 0.0; alpha_h = 1e-5
    steps = 0; contact_over = 0
    while steps < int(0.02 * SR * os):
        ys = float(np.dot(phi, c.imag))
        d = yh - ys
        dp = max(d, 0.0) ** p
        F = K * (dp + alpha_h * (dp - prev) / dt) if d > 0 else 0.0
        F = max(F, 0.0); prev = dp
        c = E * c + beta * dt * (F / N)
        v -= F / Mh * dt; yh += v * dt
        steps += 1
        if d <= 0 and v < 0:
            contact_over += 1
            if contact_over > 200: break
        else:
            contact_over = 0
    t_contact = steps / os                       # in output samples (fractional)

    # --- free decay with coupled unison modes, evaluated in closed form ---
    n_out = int((dur or 6.0) * SR)
    t = np.arange(n_out) / SR
    y = np.zeros(n_out)
    qbar = []                                     # mean string displacement per partial (for phantoms)
    for k, (n, f) in enumerate(zip(ns, fn)):
        sf, sl, fr = dec(n, f)
        c0 = c[k] * np.exp(-s[k] * 0)            # state at end of contact (same on each string)
        slope = T * n * np.pi / L * (-1.0) ** n        # slope at the bridge end (x = L), not the agraffe
        if n <= 32 and N > 1:
            lam, V, Vi = coupled_modes(N, wn[k], sf, sl, fr)
            wm = Vi @ np.full(N, c0)
            out = np.ones(N) @ V
            mean = V.mean(axis=0)
            z = (wm[:, None] * np.exp(lam[:, None] * t))
            y += slope * (out @ z).imag
            if phantoms and n <= 30: qbar.append((n, (mean @ z).imag))
        else:
            z = c0 * np.exp(s[k] * t)
            y += slope * N * z.imag
            if phantoms and n <= 30: qbar.append((n, z.imag))

    # --- phantom partials (longitudinal), lowest ~30 notes only ---
    if phantoms and midi <= 50:
        EA = 2e11 * math.pi * (0.6e-3) ** 2 * 1.6          # core wire + winding stiffness (rough)
        fL1 = 1100.0 * (1.9 / L) ** 0 if midi < 45 else 1500.0
        yl = np.zeros(n_out)
        Q = dict(qbar)
        for kL in range(1, 11):
            D = np.zeros(n_out)
            for m in Q:
                for n2 in (kL - m, m - kL, m + kL):
                    if n2 in Q and n2 >= 1:
                        D += m * n2 * Q[m] * Q[n2]
            D *= (math.pi / L) ** 2 * EA / 4
            wL = 2 * math.pi * kL * fL1; zeta = 1 / (2 * 60)
            # driven longitudinal mode (normalised to unity static gain), bilinear 2-pole
            from scipy.signal import bilinear, lfilter
            bz, az = bilinear([wL * wL], [1, 2 * zeta * wL, wL * wL], SR)
            yl += lfilter(bz, az, D)
        y += 0.3 * N * yl

    # pre-contact delay so attack timing roughly matches
    y = np.concatenate([np.zeros(int(t_contact)), y])[:n_out]
    ir = region_ir(midi)
    out = np.stack([fftconvolve(y, ir[:, 0])[:n_out], fftconvolve(y, ir[:, 1])[:n_out]], axis=1)
    return out

# ---------- calibration helpers -----------------------------------------------------------

def partial_profile(x, fit, midi, t0=0.02, t1=0.32, absolute=False):
    seg = x[int(t0 * SR):int(t1 * SR)].mean(axis=1) * np.hanning(int((t1 - t0) * SR))
    nfft = 1 << 17
    S = np.abs(np.fft.rfft(seg, nfft)); fr = np.fft.rfftfreq(nfft, 1 / SR)
    f0, B = fit["f0"], fit["B"]
    prof = []
    for n in range(1, 25):
        f = n * f0 * math.sqrt(1 + B * n * n)
        if f > 12000: break
        k = (fr > f - 0.2 * f0) & (fr < f + 0.2 * f0)
        prof.append(20 * math.log10(S[k].max() + 1e-12))
    prof = np.array(prof)
    return prof if absolute else prof - prof.max()

def partial_freqs(fit, count):
    return [n * fit["f0"] * math.sqrt(1 + fit["B"] * n * n) for n in range(1, count + 1)]

def rms(x, t1=1.0): return float(np.sqrt(np.mean(x[:int(t1 * SR)] ** 2)))

def centroid(x, t1=0.5):
    seg = x[:int(t1 * SR)].mean(axis=1)
    S = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(seg.size, 1 / SR)
    return float((S * f).sum() / S.sum())

# ---------- main ------------------------------------------------------------------------------

def fit_note(midi, fit, refs):
    if True:
        # 1) hammer stiffness scale: match ff partial profile (brightness), velocity 4.5 m/s
        pr_ref = partial_profile(refs["ff"], fit, midi)
        best = None
        for ks in np.geomspace(0.1, 10, 13):
            y = render(midi, fit, 4.5, ks, dur=0.4, phantoms=False)
            pr = partial_profile(y, fit, midi)
            m = min(len(pr), len(pr_ref))
            err = float(np.mean(np.abs(np.maximum(pr[:m], -60) - np.maximum(pr_ref[:m], -60))))
            if best is None or err < best[0]: best = (err, ks)
        ks = best[1]
        # 2) level: one gain per note, from ff
        yff = render(midi, fit, 4.5, ks, dur=1.0, phantoms=False)
        gain = rms(refs["ff"]) / rms(yff)
        # 3) pp/mf hammer velocity: match loudness relative to ff (brightness then follows the physics)
        vel = {"ff": 4.5}
        for dy in ("pp", "mf"):
            target = rms(refs[dy]) / gain
            lo, hi = 0.05, 4.5
            for _ in range(14):
                mid = math.sqrt(lo * hi)
                if rms(render(midi, fit, mid, ks, dur=1.0, phantoms=False)) < target: lo = mid
                else: hi = mid
            vel[dy] = math.sqrt(lo * hi)
        return ks, gain, vel, best[0]

def main():
    global COLOUR
    OUT.mkdir(exist_ok=True)
    notes = {}
    for name, midi in NOTES.items():
        refs = {dy: (lambda x: x[onset(x):])(read_aiff(REFS / f"Piano.{dy}.{name}.aiff")) for dy in DYNS}
        fit = analyse(read_aiff(REFS / f"Piano.mf.{name}.aiff"), midi)
        print(f"{name}: f0={fit['f0']:.2f} Hz  B={fit['B']:.2e}  partials measured={len(fit['partials'])}")
        notes[name] = (midi, fit, refs)

    # pass 1: uncalibrated soundboard colour -> one global colour curve from all notes (mf + ff)
    pts = []
    for name, (midi, fit, refs) in notes.items():
        ks, gain, vel, _ = fit_note(midi, fit, refs)
        for dy in ("mf", "ff"):
            y = render(midi, fit, vel[dy], ks, dur=0.4, phantoms=False) * gain
            pr, pf = partial_profile(refs[dy], fit, midi, absolute=True), partial_profile(y, fit, midi, absolute=True)
            for f, a, b in zip(partial_freqs(fit, len(pr)), pr, pf):
                if a > pr.max() - 50: pts.append((f, a - b))
    pts = np.array(pts)
    centres = 50 * 2 ** (np.arange(0, 29) / 3)            # 1/3-octave bands, 50 Hz .. 16 kHz
    g = np.full(centres.size, np.nan)
    for i, c in enumerate(centres):
        k = (pts[:, 0] > c / 2 ** (1 / 6)) & (pts[:, 0] < c * 2 ** (1 / 6))
        if k.sum(): g[i] = np.median(pts[k, 1])
    ok = ~np.isnan(g)
    g = np.interp(np.arange(centres.size), np.flatnonzero(ok), g[ok])
    g = np.convolve(np.pad(g, 2, mode="edge"), np.ones(5) / 5, mode="valid")
    g = np.clip(g - np.median(g), -18, 18)
    COLOUR = (centres, g)
    (OUT / "colour.json").write_text(json.dumps([centres.tolist(), g.tolist()]))
    print("colour curve dB:", " ".join(f"{c:.0f}:{v:+.0f}" for c, v in zip(centres, g)))

    # pass 2: refit hammer + level with calibrated colour, render
    report = {}
    for name, (midi, fit, refs) in notes.items():
        ks, gain, vel, perr = fit_note(midi, fit, refs)
        best = (perr, ks)
        rep = dict(f0=fit["f0"], B=fit["B"], Kscale=ks, profile_err_db=best[0], vel=vel, dyn={})
        for dy in DYNS:
            dur = min(12.0, refs[dy].shape[0] / SR)
            y = render(midi, fit, vel[dy], ks, dur=dur) * gain
            r = refs[dy][:int(dur * SR)]
            peak = max(np.abs(y).max(), np.abs(r).max())
            norm = 0.9 / peak if peak > 0.9 else 1.0
            write_wav(OUT / f"{name}_{dy}_forte.wav", y * norm)
            write_wav(OUT / f"{name}_{dy}_ref.wav", r * norm)
            rep["dyn"][dy] = dict(centroid_forte=centroid(y), centroid_ref=centroid(r))
            print(f"  {dy}: v={vel[dy]:.2f} m/s  centroid forte {centroid(y):.0f} Hz / ref {centroid(r):.0f} Hz")
        report[name] = rep
    (OUT / "report.json").write_text(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
