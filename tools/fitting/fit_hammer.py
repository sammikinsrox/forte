"""The hammer and felt (SPEC §3.3, issue 0021 step 1), in the pipeline's shape:

    python tools/fitting/fit_hammer.py measure   Iowa → data/reference-measurements/iowa-hammer.json
    python tools/fitting/fit_hammer.py voice     the felt set by ear → data/model/hammer.json
    python tools/fitting/fit_hammer.py build     data/model/hammer.json → engine/src/HammerFit.inc
    python tools/fitting/fit_hammer.py all       measure, voice, build
    python tools/fitting/fit_hammer.py fit       the Iowa fit → build/fitting/hammer-iowa-fit.json

Each committed step's output is byte-identical on every run.

The committed felt is set by ear (`voice`), not fitted. The Iowa fit (`fit`) is kept as a
reference, but its felt sounded worse than the seed: more twang (the pieces' 1-3.5 kHz share up
7 dB on the user's favourite) and a plastic click at every strike (crest factor +6 dB). It matched
attack-window spectra, and what the ear objects to is the whole note (issue 0021 §7). The voiced felt:

- relaxed stiffness Q0·(1 − ε): the seed felt × FELT_SCALE (0.25 removed the twang by ear);
- ε = RELAXATION, τ = RELAXATION_TIME: a short relaxation, so only the fastest compression meets
  the stiff felt. That adds air above 4 kHz with the 1-3.5 kHz layer barely moving (a long τ,
  as the fit chose, brings the mids back);
- the seed's exponents, and the seed's pp/mf/ff speeds (0.7 / 2.0 / 4.5 m/s, 8 m/s at most).

`measure` needs refs/iowa; `fit` needs the Release forte-string-probe (built with the engine, so
it renders the engine's own note models). Its method, for the record:

1. Targets. Iowa's note-to-note brightness scatters by 10-20 dB, so the absolute target for a
   note's mf attack is the running median of Iowa mf's octave bands over the notes within
   NEIGHBOURS semitones, never the single note.
2. The felt's shape at C2 and C4, where Iowa has pp, mf and ff (C6 has them too, but only one
   octave band above its noise floor). The hammer speed for each dynamic is solved so that Forte's
   loudness change from mf matches Iowa's (the mf speed is fixed). Then Q0, p, ε and τ are fitted
   together to (a) the band changes between dynamics, which no linear stage (the soundboard, the
   microphones, the room) can alter, so they belong to the felt alone, and (b) the smoothed mf
   target. pp counts half (Iowa pp is near its noise floor).
3. p, ε and τ run linearly in key between C2 and C4, flat outside.
4. Q0 and ε per note, C2-C6: a fit to the note's smoothed mf target, with ε pulled gently towards
   the anchors' curve, then smoothed again across keys (robust piecewise-linear curves in log Q0
   and logit ε, a knot every octave). ε must vary: held at C4's, the hysteresis's flash keeps
   C5's 2-4 kHz band 30 dB brighter than Iowa's at any stiffness.
5. Outside Iowa's range: below C2, log Q0 continues its C2-C3 slope. Above C6 there's no
   reference, and a felt held at C6's softness barely excites C8 (the seed's lesson), so Q0 is
   solved per note to keep the mf loudness level with C6's, then smoothed to a straight line in
   log Q0 from C6's value: the top octave keeps speaking. (Following the rising G5-C6 trend
   instead needed a felt so stiff that velocity 127 clipped in the top octave.)
6. The velocity calibration: with each note's final felt, the pp and ff speeds whose loudness
   change from mf matches Iowa's, at C2, C4 and C6; their ratios to mf run linearly in key
   between those, flat outside. Velocity 127 is 4.5 dB of speed over ff (SPEC §4.1).
7. Accuracy in the top octaves. Above C6 the stiffest, fastest blows outrun the 4× oversampled
   contact (at 44.1 kHz a strike then peaks several dB hotter than at 96 kHz, and clips). So
   Q0's rise above C6 is capped where every note stays within ACCURACY_DB at ff, and velocity
   127 at the fastest accurate speed, never faster than the note below.
"""

import argparse
import json
import math
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import minimize, minimize_scalar

import hammer_measure as hm
from forte_audio import ROOT, iowa_file, note_name

MEASUREMENTS = ROOT / "data" / "reference-measurements" / "iowa-hammer.json"
PARAMETERS = ROOT / "data" / "model" / "hammer.json"
IOWA_FIT = ROOT / "build" / "fitting" / "hammer-iowa-fit.json"   # a reference, never built
TABLE = ROOT / "engine" / "src" / "HammerFit.inc"

SHAPE_ANCHORS = [36, 60]          # C2, C4: the felt's p, ε, τ (C6 has one usable band)
SPEED_ANCHORS = [36, 60, 84]      # C2, C4, C6: the velocity calibration
FIT_RANGE = (36, 84)              # where Iowa mf exists
NEIGHBOURS = 4                    # semitones each side for the smoothed target
MF_SPEED = 2.0                    # m/s, the seed's; not identifiable from the recordings
SPEEDS = np.geomspace(0.25, 14.0, 16)
BAND_WEIGHTS = np.array([1.0, 1.0, 1.0, 0.5, 0.25])
ERROR_CAP_DB = 25.0               # errors are soft-capped: a few deep notches mustn't dominate
PP_WEIGHT = 0.5
SEED = 1                          # the anchors' extra starting points
STARTS = 4
EVALUATIONS = 120
RELAXATION_PULL = 2.0           # dB² per unit of logit ε away from the anchors' curve
VELOCITY_127_ABOVE_FF_DB = 4.5    # SPEC §4.1
ACCURACY_DB = 1.0                 # 44.1 kHz against 96 kHz, a strike's peak on the strings

# The voiced felt (issue 0021 §7): chosen by ear on Yesterdays, board #9.
FELT_SCALE = 0.5                  # relaxed stiffness, × the seed felt
RELAXATION = 0.8                  # ε
RELAXATION_TIME = 2e-5            # τ, s
EXPONENT_SHIFT = 0.0              # added to the seed's p
# Per register (key, felt scale, ε), interpolated (scale in log), flat outside; None = FELT_SCALE
# and RELAXATION everywhere. Fitted to the user's Keyscape sweep over a grid of felts (issue 0021
# §14): the strike's partials 2-6 at velocities 64-127. The top octaves match best with no
# relaxation (ε = 0); in the bass the felt makes no difference.
VOICING = [(36, 0.25, 0.9), (48, 0.12, 0.9), (60, 0.5, 0.8), (66, 0.5, 0.9), (72, 0.15, 0.9), (84, 0.15, 0.6), (90, 0.25, 0.0), (108, 0.25, 0.0)]
REFERENCE_COMPRESSION = 5e-4      # m, where a shifted exponent leaves the force unchanged
SEED_SPEEDS = (0.7, 2.0, 4.5)     # m/s at velocities 32, 64, 112
SEED_MAX_SPEED = 8.0              # m/s, velocity 127 never exceeds it


# --- measure ------------------------------------------------------------------------------------

def measure(_args):
    notes = {}
    for midi in range(21, 109):
        entry = {}
        for dynamic in ("pp", "mf", "ff"):
            if iowa_file(midi, dynamic) is None:
                continue
            m = hm.measure_iowa(midi, dynamic)
            entry[dynamic] = {
                "reference_db": _round(m.reference),
                "bands_db": [_round(b) for b in m.bands],
                "partials_db": [_round(v) if ok else None for v, ok in zip(m.levels - m.reference, m.valid)],
            }
        if entry:
            notes[note_name(midi)] = {"midi": midi, **entry}
    _write_json(MEASUREMENTS, {
        "about": "Iowa Steinway attack measurements for the hammer fit (tools/fitting/fit_hammer.py, "
                 "hammer_measure.py has the definitions). Derived numbers only: no reference audio.",
        "window": "max(0.15 s, 8 periods) from the strike, Hann",
        "noise_floor_margin_db": hm.FLOOR_MARGIN_DB,
        "prominence_db": hm.PROMINENCE_DB,
        "notes": notes,
    })


# --- fit ----------------------------------------------------------------------------------------

def _load_measurements():
    data = json.loads(MEASUREMENTS.read_text())
    notes = {}
    for entry in data["notes"].values():
        notes[entry["midi"]] = {d: {"reference": _num(v["reference_db"]), "bands": np.array([_num(b) for b in v["bands_db"]])}
                                for d, v in entry.items() if d != "midi"}
    return notes


def smoothed_targets(notes):
    """Per Iowa mf note: the running median of the mf bands over its neighbours."""
    mf = {m: n["mf"]["bands"] for m, n in notes.items() if "mf" in n}
    targets = {}
    for midi in mf:
        near = np.array([mf[m] for m in mf if abs(m - midi) <= NEIGHBOURS])
        with np.errstate(all="ignore"):
            targets[midi] = np.array([np.nanmedian(near[:, k]) if np.isfinite(near[:, k]).sum() >= 3 else np.nan
                                      for k in range(near.shape[1])])
    return targets


def _band_error(forte, target):
    ok = np.isfinite(forte) & np.isfinite(target)
    if not ok.any():
        return 0.0
    error = ERROR_CAP_DB * np.tanh((forte[ok] - target[ok]) / ERROR_CAP_DB)
    return float(np.sum(BAND_WEIGHTS[: len(ok)][ok] * error ** 2) / np.sum(BAND_WEIGHTS[: len(ok)][ok]))


def _felt(q0, p, eps, tau):
    return {"feltStiffness": q0, "feltExponent": p, "feltRelaxation": eps, "feltRelaxationTime": tau}


def _ladder(midi, felt):
    """Forte's reference level and bands over SPEEDS, for one felt."""
    ms = hm.measure_forte(midi, [dict(speed=s, **felt) for s in SPEEDS])
    refs = np.maximum.accumulate([m.reference for m in ms])  # loudness never falls with speed
    return refs, np.array([m.bands for m in ms])


def _at(refs, bands, speed):
    x = np.log(SPEEDS)
    return (float(np.interp(np.log(speed), x, refs)),
            np.array([np.interp(np.log(speed), x, bands[:, k]) for k in range(bands.shape[1])]))


def _speed_for(refs, level):
    """The speed at which Forte's reference level reaches level (refs rise with speed)."""
    rising = np.concatenate([[True], np.diff(refs) > 1e-9])
    return float(np.exp(np.interp(level, refs[rising], np.log(SPEEDS)[rising])))


def anchor_cost(midi, felt, iowa, target, verbose=False):
    refs, bands = _ladder(midi, felt)
    ref_mf, _ = _at(refs, bands, MF_SPEED)
    rows = {}
    for d in ("pp", "mf", "ff"):
        speed = MF_SPEED if d == "mf" else _speed_for(refs, ref_mf + iowa[d]["reference"] - iowa["mf"]["reference"])
        rows[d] = (speed, _at(refs, bands, speed)[1])
    cost = _band_error(rows["mf"][1], target)
    for low, high, weight in (("pp", "mf", PP_WEIGHT), ("mf", "ff", 1.0)):
        cost += weight * _band_error(rows[high][1] - rows[low][1], iowa[high]["bands"] - iowa[low]["bands"])
    if verbose:
        for d in ("pp", "mf", "ff"):
            print(f"    {d} {rows[d][0]:5.2f} m/s  forte " + " ".join(f"{v:6.1f}" for v in rows[d][1][:4])
                  + "   iowa " + " ".join(f"{v:6.1f}" for v in iowa[d]["bands"][:4]))
    return cost, {d: rows[d][0] for d in rows}


def _unpack(x):
    q0, p, eps_logit, tau = math.exp(x[0]), x[1], x[2], math.exp(x[3])
    return _felt(q0, float(np.clip(p, 1.5, 5.0)), 1.0 / (1.0 + math.exp(-eps_logit)), float(np.clip(tau, 1e-5, 1e-2)))


def fit_anchor(job):
    midi, iowa, target = job
    # Starting points: one soft-and-very-relaxed felt, one harder; then seeded jitter.
    starts = [np.array([math.log(3e10), 3.0, math.log(0.98 / 0.02), math.log(1e-3)]),
              np.array([math.log(3e9), 2.8, math.log(0.95 / 0.05), math.log(5e-4)])]
    rng = np.random.default_rng(SEED + midi)
    starts += [starts[i % 2] + rng.normal(0, [0.8, 0.3, 0.8, 0.5]) for i in range(STARTS - 2)]
    best = [math.inf, None]

    def f(x):
        c, _ = anchor_cost(midi, _unpack(x), iowa, target)
        if c < best[0]:
            best[:] = [c, x.copy()]
        return c

    for x0 in starts:
        minimize(f, x0, method="Nelder-Mead", options=dict(maxfev=EVALUATIONS, xatol=0.01, fatol=0.02))
    minimize(f, best[1], method="Nelder-Mead", options=dict(maxfev=EVALUATIONS, xatol=0.005, fatol=0.01))
    felt = _unpack(best[1])
    cost, speeds = anchor_cost(midi, felt, iowa, target)
    return midi, felt, speeds, math.sqrt(cost)


def _interp_key(key, xs, ys):
    return float(np.interp(key, xs, ys))


def _logit(eps):
    return math.log(eps / (1.0 - eps))


def _sigmoid(x):
    return 1.0 / (1.0 + math.exp(-x))


def felt_for_note(job):
    """Q0 and ε for one note, with p and τ from the anchors: a fit to the smoothed mf target, ε
    pulled gently towards the anchors' curve (which the dynamics, not just mf, set). A coarse grid
    first: the error has several valleys."""
    midi, felt, target = job
    log_q0_centre, logit_centre = math.log(felt["feltStiffness"]), _logit(felt["feltRelaxation"])

    def cost(x):
        m = hm.measure_forte(midi, [dict(speed=MF_SPEED, **{**felt, "feltStiffness": math.exp(x[0]), "feltRelaxation": _sigmoid(x[1])})])[0]
        return _band_error(m.bands, target) + RELAXATION_PULL * (x[1] - logit_centre) ** 2

    grid = [np.array([q, e]) for q in log_q0_centre + np.linspace(-4.0, 4.0, 9) for e in logit_centre + np.array([-3.0, -1.5, 0.0, 1.5])]
    start = min(grid, key=cost)
    result = minimize(cost, start, method="Nelder-Mead", options=dict(maxfev=60, xatol=0.01, fatol=0.02, initial_simplex=[start, start + [0.5, 0], start + [0, 0.5]]))
    x = result.x if result.fun < cost(start) else start
    m = hm.measure_forte(midi, [dict(speed=MF_SPEED, **{**felt, "feltStiffness": math.exp(x[0]), "feltRelaxation": _sigmoid(x[1])})])[0]
    return midi, float(x[0]), float(x[1]), float(math.sqrt(_band_error(m.bands, target)))


def _robust_piecewise_linear(keys, values, knots, iterations=10):
    """A piecewise-linear curve through knots, fitted to (keys, values) with Huber weights."""
    keys, values = np.asarray(keys, float), np.asarray(values, float)
    basis = np.array([np.interp(keys, knots, np.eye(len(knots))[i]) for i in range(len(knots))]).T
    weights = np.ones(len(keys))
    for _ in range(iterations):
        w = np.sqrt(weights)
        coef, *_ = np.linalg.lstsq(basis * w[:, None], values * w, rcond=None)
        residual = values - basis @ coef
        scale = 1.4826 * np.median(np.abs(residual)) + 1e-9
        r = np.abs(residual) / (1.345 * scale)
        weights = np.where(r <= 1, 1.0, 1.0 / r)
    return coef


def loudness_at_mf(job):
    midi, felt = job
    return midi, hm.measure_forte(midi, [dict(speed=MF_SPEED, **felt)])[0].reference


def speeds_for_note(job):
    """pp and ff speeds whose loudness change from mf matches Iowa's, for the note's final felt."""
    midi, felt, iowa = job
    refs, bands = _ladder(midi, felt)
    ref_mf, _ = _at(refs, bands, MF_SPEED)
    return midi, {d: _speed_for(refs, ref_mf + iowa[d]["reference"] - iowa["mf"]["reference"]) for d in ("pp", "ff")}


def fit(_args):
    notes = _load_measurements()
    targets = smoothed_targets(notes)

    with ProcessPoolExecutor() as pool:
        print("Felt shape at C2 and C4: Q0, p, ε, τ from the band changes between dynamics and the smoothed mf target")
        anchors = {}
        for midi, felt, speeds, rms in pool.map(fit_anchor, [(m, notes[m], targets[m]) for m in SHAPE_ANCHORS]):
            anchors[midi] = (felt, rms)
            print(f"  {note_name(midi)}: rms {rms:.1f} dB  Q0 {felt['feltStiffness']:.3g}  p {felt['feltExponent']:.2f}  "
                  f"ε {felt['feltRelaxation']:.4f}  τ {felt['feltRelaxationTime'] * 1e3:.3f} ms")
            anchor_cost(midi, felt, notes[midi], targets[midi], verbose=True)

        xs = SHAPE_ANCHORS
        curve = {
            "feltExponent": [anchors[m][0]["feltExponent"] for m in xs],
            "logitRelaxation": [math.log(anchors[m][0]["feltRelaxation"] / (1 - anchors[m][0]["feltRelaxation"])) for m in xs],
            "logRelaxationTime": [math.log(anchors[m][0]["feltRelaxationTime"]) for m in xs],
            "logStiffness": [math.log(anchors[m][0]["feltStiffness"]) for m in xs],
        }

        def felt_at(key, log_q0=None, eps_logit=None):
            if eps_logit is None:
                eps_logit = _interp_key(key, xs, curve["logitRelaxation"])
            return _felt(math.exp(log_q0 if log_q0 is not None else _interp_key(key, xs, curve["logStiffness"])),
                         _interp_key(key, xs, curve["feltExponent"]),
                         _sigmoid(eps_logit),
                         math.exp(_interp_key(key, xs, curve["logRelaxationTime"])))

        print("Q0 and ε per note, C2-C6, against the smoothed mf targets")
        fitted = sorted(pool.map(felt_for_note, [(m, felt_at(m), targets[m]) for m in sorted(targets)]))
        keys = [m for m, *_ in fitted]
        knots = np.arange(FIT_RANGE[0], FIT_RANGE[1] + 1, 12)
        coef_q = _robust_piecewise_linear(keys, [q for _, q, _, _ in fitted], knots)
        coef_e = _robust_piecewise_linear(keys, [e for _, _, e, _ in fitted], knots)

        def log_q0_smooth(key):
            if key < knots[0]:
                return coef_q[0] + (coef_q[1] - coef_q[0]) * (key - knots[0]) / (knots[1] - knots[0])
            return float(np.interp(key, knots, coef_q))

        def eps_smooth(key):
            return float(np.interp(key, knots, coef_e))

        for m, q, e, rms in fitted:
            print(f"  {note_name(m):4s} Q0 {math.exp(q):9.3g} -> {math.exp(log_q0_smooth(m)):9.3g}   "
                  f"ε {_sigmoid(e):.4f} -> {_sigmoid(eps_smooth(m)):.4f}   target rms {rms:5.1f} dB")

        # Above C6: keep the mf loudness level with C6's (from the fitted G5-C6 trend).
        trend_keys = list(range(79, 85))
        trend = dict(pool.map(loudness_at_mf, [(m, felt_at(m, log_q0_smooth(m), eps_smooth(m))) for m in trend_keys]))
        slope, intercept = np.polyfit(trend_keys, [trend[m] for m in trend_keys], 1)
        level = slope * FIT_RANGE[1] + intercept
        jobs = [(m, felt_at(m, log_q0_smooth(m), eps_smooth(m)), level) for m in range(85, 109)]
        top = dict(pool.map(_stiffness_for_loudness, jobs))
        # Smooth the top octaves too: a straight line in log Q0 on from C6's value (least squares
        # through that point, so the curve is continuous), never softening.
        top_keys = np.array(sorted(top))
        c6 = log_q0_smooth(FIT_RANGE[1])
        rise = max(0.0, float(np.sum((top_keys - FIT_RANGE[1]) * (np.array([top[m] for m in top_keys]) - c6))
                             / np.sum((top_keys - FIT_RANGE[1]) ** 2)))
        print(f"  above C6, for C6's loudness: log Q0 rises {rise:.3f} per semitone (×{math.exp(rise):.3f})")

        print("Velocity calibration: pp and ff speeds at C2, C4, C6 for the final felt")
        speeds = dict(pool.map(speeds_for_note, [(m, felt_at(m, log_q0_smooth(m), eps_smooth(m)), notes[m]) for m in SPEED_ANCHORS]))
        for m in SPEED_ANCHORS:
            print(f"  {note_name(m)}: {speeds[m]['pp']:.2f} / {MF_SPEED:.2f} / {speeds[m]['ff']:.2f} m/s")
        log_pp = [math.log(speeds[m]["pp"] / MF_SPEED) for m in SPEED_ANCHORS]
        log_ff = [math.log(speeds[m]["ff"] / MF_SPEED) for m in SPEED_ANCHORS]

        def ff_at(key):
            return MF_SPEED * math.exp(_interp_key(key, SPEED_ANCHORS, log_ff))

        # The 4× oversampled contact stops being accurate for the stiffest, fastest blows in the
        # top octaves (44.1 kHz then runs several dB hot against 96 kHz). Q0's rise is capped so
        # that every note above C6 stays accurate at ff, and velocity 127 at the fastest
        # accurate speed (SPEC §4.1: capped where the hammer is stable).
        top_notes = list(range(FIT_RANGE[1] + 1, 109))

        def accurate_at(candidate, speed_of):
            jobs = [(m, felt_at(m, c6 + candidate * (m - FIT_RANGE[1]), eps_smooth(m)), speed_of(m)) for m in top_notes]
            return all(pool.map(_accurate, jobs))

        low, high = 0.0, rise
        if not accurate_at(high, ff_at):
            for _ in range(8):
                middle = 0.5 * (low + high)
                low, high = (middle, high) if accurate_at(middle, ff_at) else (low, middle)
            rise = low
        print(f"  above C6, accurate at ff: log Q0 rises {rise:.3f} per semitone (×{math.exp(rise):.3f})")

        def log_q0_final(key):
            return log_q0_smooth(key) if key <= FIT_RANGE[1] else c6 + rise * (key - FIT_RANGE[1])

        hardest = {}
        for m, speed in pool.map(_fastest_accurate, [(m, felt_at(m, log_q0_final(m), eps_smooth(m)), ff_at(m)) for m in top_notes]):
            hardest[m] = speed
        cap = math.inf
        for m in top_notes:  # never faster than the note below: a smooth, falling cap
            cap = min(cap, hardest[m])
            hardest[m] = cap
        print("  velocity 127 above C6: " + " ".join(f"{note_name(m)} {hardest[m]:.1f}" for m in top_notes[::4]) + " m/s")

    per_note_rms = {m: rms for m, _, _, rms in fitted}
    out = {}
    for midi in range(21, 109):
        felt = felt_at(midi, log_q0_final(midi), eps_smooth(midi))
        pp = MF_SPEED * math.exp(_interp_key(midi, SPEED_ANCHORS, log_pp))
        ff = MF_SPEED * math.exp(_interp_key(midi, SPEED_ANCHORS, log_ff))
        out[note_name(midi)] = {
            "midi": midi,
            "feltStiffness": _round_sig(felt["feltStiffness"]),
            "feltExponent": _round_sig(felt["feltExponent"]),
            "feltRelaxation": _round_sig(felt["feltRelaxation"], 5),
            "feltRelaxationTime": _round_sig(felt["feltRelaxationTime"]),
            "hammerSpeeds": [_round_sig(pp), _round_sig(MF_SPEED), _round_sig(ff)],
            "maxHammerSpeed": _round_sig(min(ff * 10 ** (VELOCITY_127_ABOVE_FF_DB / 20), hardest.get(midi, math.inf))),
            **({"targetRmsDb": _round(per_note_rms[midi])} if midi in per_note_rms else {}),
        }

    _write_json(IOWA_FIT, {
        "about": "The Iowa fit's hammer and felt values per note (tools/fitting/fit_hammer.py fit), "
                 "a reference: not built into the engine. "
                 "Felt: Stulov, F = Q0·[c^p − ε·(c^p low-passed with τ)]. Speeds in m/s at velocities "
                 "32, 64 and 112; maxHammerSpeed is velocity 127.",
        "shapeAnchors": {note_name(m): {"rmsDb": _round(anchors[m][1])} for m in SHAPE_ANCHORS},
        "notes": out,
    })


def _peak_db(midi, felt, speed, rate):
    x = hm.probe([("p", dict(midi=midi, speed=speed, rate=rate, seconds=0.05, **felt))])["p"]
    return 20 * math.log10(float(np.abs(x).max()) + 1e-30)


def _accurate(job):
    """True if the strike's peak at 44.1 kHz is within ACCURACY_DB of the same strike at 96 kHz."""
    midi, felt, speed = job
    return abs(_peak_db(midi, felt, speed, 44100) - _peak_db(midi, felt, speed, 96000)) <= ACCURACY_DB


def _fastest_accurate(job):
    """The fastest accurate speed up to 4.5 dB over ff, bisected; a little over ff at the least."""
    midi, felt, ff = job
    top = ff * 10 ** (VELOCITY_127_ABOVE_FF_DB / 20)
    if _accurate((midi, felt, top)):
        return midi, top
    low, high = math.log(ff), math.log(top)
    for _ in range(7):
        middle = 0.5 * (low + high)
        low, high = (middle, high) if _accurate((midi, felt, math.exp(middle))) else (low, middle)
    return midi, max(math.exp(low), 1.05 * ff)


def _stiffness_for_loudness(job):
    midi, felt, level = job

    def f(log_q0):
        m = hm.measure_forte(midi, [dict(speed=MF_SPEED, **{**felt, "feltStiffness": math.exp(log_q0)})])[0]
        return (m.reference - level) ** 2

    centre = math.log(felt["feltStiffness"])
    result = minimize_scalar(f, bounds=(centre - 1.0, centre + 6.0), method="bounded", options=dict(xatol=0.01))
    return midi, float(result.x)


# --- voice --------------------------------------------------------------------------------------

def _interp_seed(x, xs, ys):
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1])
    return ys[-1]


def seed_felt(midi):
    """The seed's elastic felt (stiffness, exponent), as engine/src/PianoModel.cpp had it before
    step 1: Chaigne & Askenfelt seeds with the listening-test prototype's stiffness scales."""
    anchors = [36.0, 60.0, 84.0, 96.0, 108.0]
    log_stiffness = _interp_seed(midi, anchors, [math.log(v) for v in (4e8, 4.5e9, 3e10, 1e11, 3e11)])
    scale = _interp_seed(midi, anchors, [0.681, 0.681, 0.215, 0.6, 1.0])
    return math.exp(log_stiffness) * scale, _interp_seed(midi, anchors, [2.3, 2.5, 2.8, 3.0, 3.0])


def voice(_args):
    out = {}
    pp, mf, ff = SEED_SPEEDS
    for midi in range(21, 109):
        stiffness, exponent = seed_felt(midi)
        # A shifted exponent keeps the force at REFERENCE_COMPRESSION: Q0·c^p is unchanged there.
        new_exponent = exponent + EXPONENT_SHIFT
        stiffness *= REFERENCE_COMPRESSION ** (exponent - new_exponent)
        scale, relaxation = FELT_SCALE, RELAXATION
        if VOICING:
            keys = [v[0] for v in VOICING]
            scale = math.exp(_interp_seed(midi, keys, [math.log(v[1]) for v in VOICING]))
            relaxation = round(_interp_seed(midi, keys, [v[2] for v in VOICING]), 4)
        out[note_name(midi)] = {
            "midi": midi,
            "feltStiffness": _round_sig(scale * stiffness / (1.0 - relaxation)),
            "feltExponent": _round_sig(new_exponent),
            "feltRelaxation": relaxation,
            "feltRelaxationTime": RELAXATION_TIME,
            "hammerSpeeds": [pp, mf, ff],
            "maxHammerSpeed": SEED_MAX_SPEED,
        }
    _write_json(PARAMETERS, {
        "about": "Hammer and felt values per note, set by ear (tools/fitting/fit_hammer.py voice; "
                 "issue 0021 §7). Felt: Stulov, F = Q0·[c^p − ε·(c^p low-passed with τ)]; the relaxed "
                 "stiffness Q0·(1 − ε) is the seed felt × " + repr(FELT_SCALE) + ". Speeds in m/s at "
                 "velocities 32, 64 and 112; maxHammerSpeed caps velocity 127.",
        "notes": out,
    })


# --- build --------------------------------------------------------------------------------------

def build(_args):
    data = json.loads(PARAMETERS.read_text())
    lines = [
        "// Generated by tools/fitting/fit_hammer.py build from data/model/hammer.json (set by ear). Don't edit.",
        "// midi, Q0, p, ε, τ (s), pp/mf/ff hammer speeds (m/s), velocity 127's speed (m/s)",
    ]
    for entry in data["notes"].values():
        pp, mf, ff = entry["hammerSpeeds"]
        values = [entry["feltStiffness"], entry["feltExponent"], entry["feltRelaxation"], entry["feltRelaxationTime"], pp, mf, ff, entry["maxHammerSpeed"]]
        lines.append(f"{{ {entry['midi']}, " + ", ".join(_cpp(v) for v in values) + " },")
    text = "\n".join(lines) + "\n"
    TABLE.write_bytes(text.encode("utf-8"))
    print(f"wrote {TABLE.relative_to(ROOT)}")


# --- helpers ------------------------------------------------------------------------------------

def _num(v):
    return np.nan if v is None else float(v)


def _round(v):
    return None if v is None or not np.isfinite(v) else round(float(v), 2)


def _round_sig(v, digits=4):
    return float(f"{float(v):.{digits}g}")


def _cpp(v):
    text = repr(float(v))
    return text if ("e" in text or "." in text) else text + ".0"


def _write_json(path, data):
    """Indented JSON with lists of numbers kept on one line, so diffs stay readable."""
    text = json.dumps(data, indent=1, ensure_ascii=False)
    text = re.sub(r"\[\s+([^\[\]{}]*?)\s+\]", lambda m: "[" + re.sub(r",\s+", ", ", m.group(1)) + "]", text)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((text + "\n").encode("utf-8"))
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # ε and τ on a cp1252 console
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("step", choices=["measure", "voice", "build", "all", "fit"])
    args = parser.parse_args()
    if args.step in ("measure", "all"):
        measure(args)
    if args.step in ("voice", "all"):
        voice(args)
    if args.step in ("build", "all"):
        build(args)
    if args.step == "fit":
        fit(args)


if __name__ == "__main__":
    main()
