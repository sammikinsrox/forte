"""The strings' decay (SPEC §3.3, issue 0021 step 2), in the pipeline's shape:

    python tools/fitting/fit_decay.py measure   Iowa → data/reference-measurements/iowa-decay.json
    python tools/fitting/fit_decay.py measure-sweep <wav> --hold H --gap G
                                                the user's Keyscape render of a note sweep →
                                                data/reference-measurements/keyscape-decay.json
    python tools/fitting/fit_decay.py fit       measurements → data/model/decay.json
    python tools/fitting/fit_decay.py floor     the physical floor on data/model/decay.json, then build
    python tools/fitting/fit_decay.py build     data/model/decay.json → engine/src/DecayFit.inc
    python tools/fitting/fit_decay.py all       measure, fit, build
    python tools/fitting/fit_decay.py check     Forte's strings (the Release probe) against Iowa

Each committed step's output is byte-identical on every run.

What the engine takes, per note: a prompt-sound and an aftersound decay rate (σ, amplitude, 1/s)
for each measured partial, and above those the law σ = b1 + b3·f² for each (SPEC §3.3). The
unison's coupling follows from the two rates (NoteModel: ε = (σ_prompt − σ_after) / N), so with
the fixed unison spread (SPEC §2.3) they set the whole two-stage envelope: how fast the partial
falls at first, where the knee is, and how slowly it rings after.

The method:

1. measure (decay_measure.py): each Iowa mf partial's mean level in log-spaced windows from 0.05
   to 16 s, while 10 dB over its noise floor. Means over windows, not fitted lines: both Iowa's
   unisons and Forte's beat.
2. Targets: per note and partial, the median window levels of the same partial over the notes
   within ±NEIGHBOURS semitones. Iowa's single notes scatter by 2-5× in their rates, partly from
   each unison's own tuning, which sets its knee; Forte has one fixed spread, so a single note's
   values would carry that note's tuning into Forte. A note's measured partials are those whose
   median has its first windows above the floor.
3. Per partial, the two rates whose envelope, solved as the engine solves it (Weinreich's coupled
   modes with Forte's string count and spread, every string struck alike) and measured through the
   same frames and windows, best matches the target. Windows up to 2.4 s count fully: a played
   note rarely rings longer, and those are what the ear hears in a piece. The aftersound is pulled
   gently towards a quarter of the prompt rate (the seed's ratio) where the target can't tell them
   apart. Both rates are at least SIGMA_RANGE[0] (a 57 s T60): Iowa's tails near the floor
   can't resolve slower ones, and they would make the engine's reported tail run to minutes.
4. Outliers: a partial's rates more than OUTLIER_RATIO from the running median of its neighbours
   (the same partial, ±NEIGHBOURS semitones) take the median. The rest keep their own values.
5. The law above the measured partials: b1 and b3 fitted (robust, non-negative) to each note's
   measured rates, then smoothed across keys (robust piecewise-linear curves in log b1 and log b3,
   a knot every octave), held flat outside C2-C6. Iowa's rates level off above ~2 kHz, so b3
   comes out small: the upper partials decay about as fast as the measured ones (issue 0021 §2).
6. Notes outside Iowa's C2-C6 (and the edge notes without enough neighbours) borrow, partial by
   partial, the rates at the same frequency from the nearest note with measured partials, within
   that note's measured range (SPEC §3.3: the top octave borrows from its neighbours). Board
   damping depends mostly on frequency, and borrowing keeps the two-stage decay there; the law
   alone would lose it.

The check step renders Forte's bare strings through the probe and measures them as Iowa was
measured: partials 1-4 match the targets within 1.6-4.4 dB rms on every note. Larger numbers on
some notes' upper partials are modes reaching the engine's −100 dBFS resting level, not decay.
"""

import argparse
import json
import math
import re
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from scipy.optimize import minimize, minimize_scalar

import decay_measure as dm
from fit_hammer import _robust_piecewise_linear, _round, _round_sig, _write_json, _cpp
from forte_audio import ROOT, iowa_file, note_name, partial_frequency

MEASUREMENTS = ROOT / "data" / "reference-measurements" / "iowa-decay.json"
SWEEP_MEASUREMENTS = ROOT / "data" / "reference-measurements" / "keyscape-decay.json"
SWEEP_VELOCITIES = (64, 96)       # the sweep's layers the rates are fitted to
LONG_SWEEP_HOLD = 6.0             # s: a sweep held this long sets both rates; a shorter one only the prompt
PARAMETERS = ROOT / "data" / "model" / "decay.json"
TABLE = ROOT / "engine" / "src" / "DecayFit.inc"

FIT_RANGE = (36, 84)              # where Iowa mf exists
SPREAD_CENTS = 0.7                # the engine's default unison spread (PianoModel)
COUPLED_PARTIALS = 32             # NoteModel::coupledPartials
# What the ear hears most is 0.25-2.4 s: the strike's first dip (0.12-0.25 s) and the long tail
# count less. Weighted fully, the dip made the fit kill treble fundamentals with a very fast prompt
# rate whose aftersound the fixed spread can't hold, leaving their partials ringing glassy
# (issue 0021 §15).
WINDOW_WEIGHTS = np.array([0.4 if b <= 0.25 else 1.0 if b <= 2.4 else 0.3 for _, b in dm.WINDOWS])
RATIO_PRIOR = math.log(4.0)       # σ_prompt / σ_after, the seed's
RATIO_PULL = 0.5                  # dB² per unit of log ratio away from the prior
MAX_RATIO = math.log(3.0)         # σ_prompt / σ_after at most: beyond it the knee drops below what the spread holds
SIGMA_RANGE = (0.12, 60.0)        # 1/s, both rates. 0.12 is a T60 of 57 s, about a real bass
                                  # string's longest aftersound; Iowa's tails, near their noise
                                  # floor, can't tell slower rates apart
NEIGHBOURS = 3                    # semitones each side, for the smoothed targets and outliers
MIN_NOTES = 3                     # neighbours a smoothed window needs
OUTLIER_RATIO = 2.0
MIN_B3 = 1e-9                     # s; the law's f² term, at least (0.025/s at 5 kHz)
# The physical floor (issue 0021 §18): every partial's prompt rate is at least b1 + b3·f², with b1
# the note's prompt law (or its fundamental's rate if slower) and b3 = floor_b3(midi); its
# aftersound at least the same from the aftersound's b1, and at least a third of its prompt rate
# (MAX_RATIO). Keyscape's partials, measured only while 25 dB over its hiss, give b3 of about 4e-8
# at C3 (noisy: 7e-8 matches its median rates at 1.5-2.5 kHz, and the colour fit, issue 0021 §19,
# kept asking for less 1-4 kHz in the tenor with 4e-8), 1e-7 at C4 and 1.1-1.5e-7 at C5-C6; the fit read its faint upper partials near that
# hiss as ringing for up to a minute, so Forte's upper partials outlasted the fundamental and the
# note grew brighter as it rang: the metallic twang.
FLOOR_B3_KEYS = (48, 60, 72)      # Keyscape's clean rates: b3 rises with pitch, held outside
FLOOR_B3 = (7e-8, 1.0e-7, 1.2e-7) # s


def floor_b3(midi):
    return math.exp(float(np.interp(midi, FLOOR_B3_KEYS, np.log(FLOOR_B3))))
MODEL_STEP = 0.0025               # s, the model envelope's time step
LAW_MIN_PARTIALS = 3
CHECK_FLOOR_DB = 60.0


# --- the engine's unison, in numpy --------------------------------------------------------------

def strings_of(midi):
    """PianoModel's string counts."""
    return 1 if midi <= 30 else 2 if midi <= 48 else 3


SPREAD_KEYS, SPREAD_SCALES = [60, 76, 88], [1.0, 0.35, 0.15]   # PianoModel's, PROTOTYPE


def spread_scale(midi):
    """PianoModel's per-note spread scale: 1 up to C4, narrowing to 0.15 from E6."""
    return float(np.interp(midi, SPREAD_KEYS, SPREAD_SCALES))


def unison_offsets(strings, spread=SPREAD_CENTS):
    return {1: [0.0], 2: [-0.5 * spread, 0.5 * spread], 3: [-1.0 * spread, 0.2 * spread, 0.85 * spread]}[strings]


def envelope(midi, n, sigma_prompt, sigma_after, times):
    """|bridge sum| of partial n's unison, struck with every string alike, at times (s): the
    coupled modes of UnisonModes (dz_k/dt = s_k·z_k − ε·Σ z_l, ε = (σ_prompt − σ_after) / N),
    rotating at the partial's own frequency removed."""
    strings = strings_of(midi)
    f = partial_frequency(midi, n)
    offsets = unison_offsets(strings, SPREAD_CENTS * spread_scale(midi))
    coupled = strings > 1 and n <= COUPLED_PARTIALS
    coupling = max(0.0, (sigma_prompt - sigma_after) / strings) if coupled else 0.0
    sigma = sigma_after if coupling > 0 else sigma_prompt
    omega = np.array([2 * math.pi * f * (2 ** (o / 1200) - 1) for o in offsets])
    for nudge in (1.0, 1.01, 0.99, 1.02, 0.98, 1.05, 0.95):  # off an exceptional point, as the engine does
        a = np.diag(-sigma + 1j * omega) - coupling * nudge * np.ones((strings, strings))
        values, vectors = np.linalg.eig(a)
        if np.linalg.cond(vectors) < 1e6:
            break
    weights = (np.ones(strings) @ vectors) * np.linalg.solve(vectors, np.ones(strings))
    return np.abs(np.exp(np.outer(times, values)) @ weights)


def model_levels(midi, n, sigma_prompt, sigma_after):
    """The envelope's window levels re the first window, measured as decay_measure measures."""
    size = dm.frame_seconds(midi)
    t = np.arange(0.0, dm.WINDOWS[-1][1] + size, MODEL_STEP)
    a = envelope(midi, n, sigma_prompt, sigma_after, t)
    hann = np.hanning(max(3, int(round(size / MODEL_STEP))))
    # A frame's peak magnitude for a slowly changing partial is its window-weighted amplitude.
    frames = (np.convolve(a, hann / hann.sum(), mode="valid")) ** 2
    starts = np.arange(len(frames)) * MODEL_STEP
    keep = (np.arange(len(frames)) % int(round(dm.HOP / MODEL_STEP))) == 0
    levels, _ = dm.window_levels(starts[keep] + 0.5 * size, frames[keep], 0.0)
    return levels - levels[0]


# --- measure ------------------------------------------------------------------------------------

def _measure_one(midi):
    m = dm.measure_iowa(midi)
    return midi, m


def measure(_args):
    notes = {}
    midis = [m for m in range(21, 109) if iowa_file(m) is not None]
    with ProcessPoolExecutor() as pool:
        for midi, m in pool.map(_measure_one, midis):
            rows = [[_round(v) for v in row] for row in m.levels[: m.measured]]
            notes[note_name(midi)] = {"midi": midi, "measuredPartials": m.measured,
                                      "attackDb": [_round(v) for v in m.attack[: m.measured]],
                                      "windowLevelsDb": rows}
    _write_json(MEASUREMENTS, {
        "about": "Iowa Steinway mf decay measurements for the decay fit (tools/fitting/fit_decay.py; "
                 "decay_measure.py has the definitions). Per measured partial: the mean level in each "
                 "window after the strike, dB re the first window, null where it is under the noise "
                 "floor + " + repr(dm.FLOOR_MARGIN_DB) + " dB. Derived numbers only: no reference audio.",
        "windowsSeconds": [list(w) for w in dm.WINDOWS],
        "notes": notes,
    })


def measure_sweep(args):
    """The user's Keyscape render of the note sweep → keyscape-decay.json (derived numbers only)."""
    import make_note_sweep as ns
    from forte_audio import read_wav
    x, rate = read_wav(args.path)
    mono = x.mean(axis=1) if x.ndim == 2 else x
    schedule, t = [], ns.LEAD_IN
    for midi in range(21, 109):
        for velocity in ns.VELOCITIES:
            if velocity in SWEEP_VELOCITIES:
                schedule.append((t, midi, velocity))
            t += args.hold + args.gap
    measured = dm.measure_sweep(mono, int(rate), schedule, args.hold)
    notes = {}
    for (midi, velocity), m in sorted(measured.items()):
        entry = notes.setdefault(note_name(midi), {"midi": midi})
        entry[f"v{velocity}"] = {"measuredPartials": m.measured,
                                 "windowLevelsDb": [[_round(v) for v in row] for row in m.levels[: m.measured]]}
    _write_json(SWEEP_MEASUREMENTS, {
        "about": "Decay measurements of the user's Keyscape render of the note sweep "
                 "(tools/fitting/make_note_sweep.py; fit_decay.py measure-sweep). Per velocity and measured "
                 "partial: the mean level in each window after the strike, dB re the first window, null "
                 "under the noise floor + " + repr(dm.FLOOR_MARGIN_DB) + " dB or after "
                 + repr(dm.sweep_last_window(args.hold)) + " s (the notes are held " + repr(args.hold)
                 + " s). Derived numbers only: no audio.",
        "holdSeconds": args.hold,
        "windowsSeconds": [list(w) for w in dm.WINDOWS],
        "notes": notes,
    })


def _load_sweep():
    """{midi: [rows per velocity]} or {} if there's no sweep measurement."""
    if not SWEEP_MEASUREMENTS.exists():
        return {}
    data = json.loads(SWEEP_MEASUREMENTS.read_text())
    out = {}
    for e in data["notes"].values():
        for v in SWEEP_VELOCITIES:
            rows = e.get(f"v{v}", {}).get("windowLevelsDb", [])
            if rows:
                out.setdefault(e["midi"], []).append(
                    np.array([[np.nan if x is None else x for x in row] for row in rows]).reshape(-1, len(dm.WINDOWS)))
    return out


def smoothed_sweep_targets(sweep):
    """As smoothed_targets, pooling the velocities: per note and partial, the median window levels
    over the notes within NEIGHBOURS semitones and both layers."""
    pooled = {}
    for midi in sweep:
        smoothed = []
        for n in range(1, dm.MAX_PARTIALS + 1):
            near = [rows[n - 1] for m in sweep if abs(m - midi) <= NEIGHBOURS for rows in sweep[m] if len(rows) >= n]
            if len(near) < MIN_NOTES:
                break
            near = np.array(near)
            with np.errstate(all="ignore"):
                median = np.array([np.nanmedian(near[:, w]) if np.isfinite(near[:, w]).sum() >= MIN_NOTES else np.nan
                                   for w in range(near.shape[1])])
            failed = np.cumsum(~np.isfinite(median)) > 0
            median[failed] = np.nan
            if not np.isfinite(median[: dm.MIN_WINDOWS]).all():
                break
            smoothed.append(median)
        if smoothed:
            pooled[midi] = np.array(smoothed)
    return pooled


def fit_prompt(job):
    """σ_prompt for each partial of one note, with σ_after given: the sweep's early windows."""
    midi, rows, afters = job
    out = []
    for n, (target, sigma_a) in enumerate(zip(rows, afters), start=1):
        ok = np.isfinite(target)
        ok[0] = False

        def cost(log_p):
            model = model_levels(midi, n, math.exp(log_p), sigma_a)
            return float(np.mean((model[ok] - target[ok]) ** 2))

        low = math.log(max(sigma_a, SIGMA_RANGE[0]))
        result = minimize_scalar(cost, bounds=(low, math.log(SIGMA_RANGE[1])), method="bounded", options=dict(xatol=0.005))
        out.append((float(math.exp(result.x)), sigma_a, float(math.sqrt(result.fun))))
    return midi, out


def _load_measurements():
    data = json.loads(MEASUREMENTS.read_text())
    return {e["midi"]: np.array([[np.nan if v is None else v for v in row] for row in e["windowLevelsDb"]]).reshape(-1, len(dm.WINDOWS))
            for e in data["notes"].values()}


# --- fit ----------------------------------------------------------------------------------------

def _cost(x, midi, n, target):
    sigma_p = math.exp(x[0])
    sigma_a = sigma_p / math.exp(max(x[1], 0.0))
    model = model_levels(midi, n, sigma_p, sigma_a)
    ok = np.isfinite(target)
    ok[0] = False
    error = np.sum(WINDOW_WEIGHTS[ok] * (model[ok] - target[ok]) ** 2) / max(np.sum(WINDOW_WEIGHTS[ok]), 1e-9)
    penalty = 1e3 * max(0.0, -x[1]) ** 2 + RATIO_PULL * (max(x[1], 0.0) - RATIO_PRIOR) ** 2
    penalty += 1e3 * max(0.0, x[1] - MAX_RATIO) ** 2
    low, high = math.log(SIGMA_RANGE[0]), math.log(SIGMA_RANGE[1])
    log_after = x[0] - max(x[1], 0.0)
    penalty += 1e3 * (max(0.0, low - log_after) ** 2 + max(0.0, x[0] - high) ** 2)
    return error + penalty


def fit_partial(midi, n, target):
    """(σ_prompt, σ_after, rms dB) for one partial's window levels."""
    times = dm.window_times()
    ok = np.isfinite(target)
    early = ok & (times < 1.0)
    late = ok & (times > 1.0)
    rate = lambda sel: -np.polyfit(times[sel], target[sel], 1)[0] / 8.686 if sel.sum() >= 2 else 1.0
    s_early = float(np.clip(rate(early), 0.1, 30.0))
    s_late = float(np.clip(rate(late), 0.05, s_early)) if late.sum() >= 2 else s_early / 4
    starts = [np.array([math.log(s_early), math.log(max(s_early / s_late, 1.0))]),
              np.array([math.log(s_early), RATIO_PRIOR]),
              np.array([math.log(2 * s_early), math.log(8.0)]),
              np.array([math.log(0.5 * s_early), 0.3])]
    best = min((minimize(_cost, x0, args=(midi, n, target), method="Nelder-Mead",
                         options=dict(maxfev=150, xatol=0.01, fatol=0.01)) for x0 in starts), key=lambda r: r.fun)
    # The bounds are soft in the search; hold them exactly in the result.
    sigma_p = float(np.clip(math.exp(best.x[0]), *SIGMA_RANGE))
    sigma_a = float(np.clip(sigma_p / math.exp(max(best.x[1], 0.0)), SIGMA_RANGE[0], sigma_p))
    model = model_levels(midi, n, sigma_p, sigma_a)
    sel = np.isfinite(target)
    sel[0] = False
    return sigma_p, sigma_a, float(np.sqrt(np.mean((model[sel] - target[sel]) ** 2))) if sel.any() else 0.0


def fit_note(job):
    midi, rows = job
    return midi, [fit_partial(midi, n, row) for n, row in enumerate(rows, start=1)]


def smoothed_targets(targets):
    """Per note and measured partial: the median window levels of the same partial over the notes
    within NEIGHBOURS semitones that measured it. A window needs MIN_NOTES of them, and the
    first window to fail ends the row. A note's partials are measured as long as its own and the
    median's first MIN_WINDOWS windows both count."""
    out = {}
    for midi, rows in targets.items():
        smoothed = []
        for n, own in enumerate(rows, start=1):
            near = np.array([targets[m][n - 1] for m in targets if abs(m - midi) <= NEIGHBOURS and len(targets[m]) >= n])
            with np.errstate(all="ignore"):
                median = np.array([np.nanmedian(near[:, w]) if np.isfinite(near[:, w]).sum() >= MIN_NOTES else np.nan
                                   for w in range(near.shape[1])])
            failed = np.cumsum(~np.isfinite(median)) > 0
            median[failed] = np.nan
            if not np.isfinite(median[: dm.MIN_WINDOWS]).all():
                break
            smoothed.append(median)
        out[midi] = np.array(smoothed).reshape(-1, len(dm.WINDOWS))
    return out


def _law(freqs, sigmas):
    """Robust non-negative b1 + b3·f² through (freqs, sigmas), in log-σ error."""
    freqs, sigmas = np.asarray(freqs), np.asarray(sigmas)

    def cost(x):
        b1, b3 = math.exp(x[0]), math.exp(x[1])
        r = np.log(b1 + b3 * freqs ** 2) - np.log(sigmas)
        return float(np.sum(np.where(np.abs(r) < 0.5, r ** 2, np.abs(r) - 0.25)))

    best = min((minimize(cost, np.array([math.log(b1), math.log(b3)]), method="Nelder-Mead",
                         options=dict(maxfev=400, xatol=1e-4, fatol=1e-6))
                for b1 in (0.3 * sigmas.min(), sigmas.min()) for b3 in (1e-8, 1e-7, 1e-6)), key=lambda r: r.fun)
    return math.exp(best.x[0]), math.exp(best.x[1])


def _sweep_hold():
    if not SWEEP_MEASUREMENTS.exists():
        return 0.0
    return float(json.loads(SWEEP_MEASUREMENTS.read_text()).get("holdSeconds", 3.0))


def fit(_args):
    # The reference: the user's Keyscape sweep when its notes are held long enough to show the
    # aftersound (issue 0021 §15, the user's ground truth), else Iowa mf.
    long_sweep = _sweep_hold() >= LONG_SWEEP_HOLD
    targets = smoothed_sweep_targets(_load_sweep()) if long_sweep else smoothed_targets(_load_measurements())
    fit_range = (min(targets), max(targets)) if long_sweep else FIT_RANGE
    print("reference: " + ("the Keyscape sweep" if long_sweep else "Iowa mf") + f", laws over MIDI {fit_range[0]}-{fit_range[1]}")
    with ProcessPoolExecutor() as pool:
        fitted = dict(pool.map(fit_note, sorted(targets.items())))

    # Outliers: against the running median of the same partial on the neighbouring notes.
    cleaned, replaced = {}, 0
    for midi, rows in fitted.items():
        out = []
        for n, (sp, sa, rms) in enumerate(rows, start=1):
            near = [fitted[m][n - 1] for m in fitted if abs(m - midi) <= NEIGHBOURS and len(fitted[m]) >= n]
            med_p = float(np.exp(np.median([math.log(v[0]) for v in near])))
            med_a = float(np.exp(np.median([math.log(v[1]) for v in near])))
            if len(near) >= 3 and (abs(math.log(sp / med_p)) > math.log(OUTLIER_RATIO) or abs(math.log(sa / med_a)) > math.log(OUTLIER_RATIO)):
                sp, sa = med_p, min(med_a, med_p)
                replaced += 1
            out.append((sp, sa, rms))
        cleaned[midi] = out
    print(f"{sum(len(r) for r in cleaned.values())} partials fitted on {len(cleaned)} notes; {replaced} outliers took their neighbours' median")

    # The law per note, then smoothed across keys.
    laws = {}
    for midi, rows in cleaned.items():
        if len(rows) < LAW_MIN_PARTIALS:
            continue
        freqs = [partial_frequency(midi, n) for n in range(1, len(rows) + 1)]
        laws[midi] = tuple((b1, max(b3, MIN_B3)) for b1, b3 in (_law(freqs, [r[0] for r in rows]), _law(freqs, [r[1] for r in rows])))
    keys = sorted(laws)
    knots = np.arange(fit_range[0], fit_range[1] + 1, 12)
    curves = {name: _robust_piecewise_linear(keys, [math.log(laws[m][i][j]) for m in keys], knots)
              for name, (i, j) in {"b1p": (0, 0), "b3p": (0, 1), "b1a": (1, 0), "b3a": (1, 1)}.items()}

    def law_at(key, name):
        return math.exp(float(np.interp(np.clip(key, knots[0], knots[-1]), knots, curves[name])))

    # Notes Iowa doesn't measure borrow, partial by partial, the rates at the same frequency from
    # the nearest note that has measured partials, within that note's measured range.
    donors = [m for m in sorted(cleaned) if len(cleaned[m]) >= LAW_MIN_PARTIALS]
    for midi in range(21, 109):
        if cleaned.get(midi):
            continue
        donor = min(donors, key=lambda m: (abs(m - midi), m))
        donor_freqs = np.log([partial_frequency(donor, n) for n in range(1, len(cleaned[donor]) + 1)])
        rows = []
        for n in range(1, dm.MAX_PARTIALS + 1):
            f = math.log(partial_frequency(midi, n))
            if f > donor_freqs[-1]:
                break
            sp, sa = (math.exp(float(np.interp(f, donor_freqs, np.log([r[i] for r in cleaned[donor]])))) for i in (0, 1))
            rows.append((sp, min(sa, sp), None))
        cleaned[midi] = rows
        print(f"  {note_name(midi)} borrows {len(rows)} partials from {note_name(donor)}")

    # The prompt rates from the user's Keyscape sweep, where it measures (issue 0021 §14): Iowa's
    # close-miked Steinway keeps its middle partials as long as its fundamental, which sounds
    # nasal in Forte; the sweep's piano dies faster the higher the partial. Iowa keeps the
    # aftersound (the sweep's notes are held only 3 s), by frequency from the note's own rows,
    # and the aftersound law above them.
    prompt_law_at = lambda key: (law_at(key, "b1p"), law_at(key, "b3p"))
    from_sweep = set()
    sweep = {} if long_sweep else smoothed_sweep_targets(_load_sweep())
    if sweep:
        jobs = []
        for midi, rows in sorted(sweep.items()):
            own = cleaned.get(midi, [])
            afters = []
            for n in range(1, len(rows) + 1):
                f = partial_frequency(midi, n)
                sa = own[n - 1][1] if n <= len(own) else law_at(midi, "b1a") + law_at(midi, "b3a") * f * f
                afters.append(float(max(sa, SIGMA_RANGE[0])))
            jobs.append((midi, rows, afters))
        with ProcessPoolExecutor() as pool:
            refit = dict(pool.map(fit_prompt, jobs))
        replaced = 0
        for midi, rows in refit.items():
            out = []
            for n, (sp, sa, rms) in enumerate(rows, start=1):
                near = [refit[m][n - 1][0] for m in refit if abs(m - midi) <= NEIGHBOURS and len(refit[m]) >= n]
                med = float(np.exp(np.median(np.log(near))))
                if len(near) >= 3 and abs(math.log(sp / med)) > math.log(OUTLIER_RATIO):
                    sp, replaced = max(med, sa), replaced + 1
                out.append((sp, sa, rms))
            cleaned[midi] = out + cleaned.get(midi, [])[len(out):]
            from_sweep.add(midi)
        print(f"prompt rates of {sum(len(r) for r in refit.values())} partials on {len(refit)} notes from the sweep; "
              f"{replaced} outliers took their neighbours' median")

        # The prompt law from those rates, smoothed across the sweep's notes.
        sweep_laws = {m: _law([partial_frequency(m, n) for n in range(1, len(cleaned[m]) + 1)], [r[0] for r in cleaned[m]])
                      for m in sorted(from_sweep) if len(cleaned[m]) >= LAW_MIN_PARTIALS}
        sweep_keys = sorted(sweep_laws)
        sweep_knots = np.arange(sweep_keys[0], sweep_keys[-1] + 1, 12)
        b1_curve = _robust_piecewise_linear(sweep_keys, [math.log(sweep_laws[m][0]) for m in sweep_keys], sweep_knots)
        b3_curve = _robust_piecewise_linear(sweep_keys, [math.log(max(sweep_laws[m][1], MIN_B3)) for m in sweep_keys], sweep_knots)
        prompt_law_at = lambda key: tuple(math.exp(float(np.interp(np.clip(key, sweep_knots[0], sweep_knots[-1]), sweep_knots, c)))
                                          for c in (b1_curve, b3_curve))

    notes = {}
    for midi in range(21, 109):
        rows = cleaned.get(midi, [])
        entry = {
            "midi": midi,
            "prompt": {"b1": _round_sig(max(prompt_law_at(midi)[0], SIGMA_RANGE[0])), "b3": _round_sig(prompt_law_at(midi)[1])},
            "aftersound": {"b1": _round_sig(max(law_at(midi, "b1a"), SIGMA_RANGE[0])), "b3": _round_sig(law_at(midi, "b3a"))},
            "measuredPrompt": [_round_sig(r[0]) for r in rows],
            "measuredAftersound": [_round_sig(r[1]) for r in rows],
        }
        if midi in from_sweep:
            entry["promptFromSweep"] = True
        if rows and rows[0][2] is not None:
            entry["fitRmsDb"] = [_round(r[2]) for r in rows if r[2] is not None]
        elif rows:
            entry["borrowed"] = True
        notes[note_name(midi)] = apply_floor(entry)
    _write_json(PARAMETERS, {
        "about": "String decay per note (tools/fitting/fit_decay.py fit): amplitude decay rates σ (1/s) "
                 "of the prompt sound and the aftersound, for each measured partial (from 1), and above them "
                 "the law σ = b1 + b3·f² (f in Hz). With the unison spread they set the coupled two-stage "
                 "decay (engine/src/NoteModel.cpp). fitRmsDb: each measured partial's window-level error.",
        "unisonSpreadCents": SPREAD_CENTS,
        "notes": notes,
    })
    for midi in (36, 48, 60, 69, 72, 84):
        e = notes[note_name(midi)]
        print(f"  {note_name(midi):4s} {len(e['measuredPrompt']):2d} measured  prompt p1-4 "
              + " ".join(f"{v:6.2f}" for v in e["measuredPrompt"][:4]) + "  after "
              + " ".join(f"{v:6.3f}" for v in e["measuredAftersound"][:4])
              + f"   law prompt {e['prompt']['b1']:.3g} + {e['prompt']['b3']:.3g} f²")


def apply_floor(entry):
    """The physical floor (FLOOR_B3) on one note's entry of decay.json; idempotent."""
    midi = entry["midi"]
    # Based on the fundamental's own rates where they're slower than the law's b1, so the floor
    # only adds the f² term and never shortens a fundamental.
    b1p, b1a = entry["prompt"]["b1"], entry["aftersound"]["b1"]
    if entry["measuredPrompt"]:
        b1p = min(b1p, entry["measuredPrompt"][0])
        b1a = min(b1a, entry["measuredAftersound"][0])
    b3 = floor_b3(midi)
    entry["prompt"]["b3"] = _round_sig(max(entry["prompt"]["b3"], b3))
    entry["aftersound"]["b3"] = _round_sig(max(entry["aftersound"]["b3"], b3))
    prompts, afters = [], []
    for n, (sp, sa) in enumerate(zip(entry["measuredPrompt"], entry["measuredAftersound"]), start=1):
        f = partial_frequency(midi, n)
        sp = max(sp, b1p + b3 * f * f)
        sa = min(max(sa, b1a + b3 * f * f, sp / math.exp(MAX_RATIO)), sp)
        prompts.append(_round_sig(sp))
        afters.append(_round_sig(sa))
    entry["measuredPrompt"], entry["measuredAftersound"] = prompts, afters
    return entry


def floor(_args):
    """Applies the physical floor to data/model/decay.json in place, without refitting."""
    data = json.loads(PARAMETERS.read_text(encoding="utf-8"))
    for entry in data["notes"].values():
        apply_floor(entry)
    _write_json(PARAMETERS, data)
    print(f"floored {PARAMETERS.relative_to(ROOT)}")


# --- build --------------------------------------------------------------------------------------

def build(_args):
    data = json.loads(PARAMETERS.read_text(encoding="utf-8"))
    lines = [
        "// Generated by tools/fitting/fit_decay.py build from data/model/decay.json. Don't edit.",
        "// midi, prompt law b1, b3, aftersound law b1, b3, measured partials, then their prompt and aftersound σ (1/s)",
    ]
    for entry in data["notes"].values():
        prompt, after = entry["measuredPrompt"], entry["measuredAftersound"]
        head = [entry["prompt"]["b1"], entry["prompt"]["b3"], entry["aftersound"]["b1"], entry["aftersound"]["b3"]]
        lines.append(f"{{ {entry['midi']}, " + ", ".join(_cpp(v) for v in head) + f", {len(prompt)},"
                     + " { " + ", ".join(_cpp(v) for v in prompt) + " }, { " + ", ".join(_cpp(v) for v in after) + " } },")
    TABLE.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print(f"wrote {TABLE.relative_to(ROOT)}")


# --- check --------------------------------------------------------------------------------------

def _check_one(midi):
    return midi, dm.measure_forte(midi)


def check(args):
    """Forte's bare strings, through the probe, against Iowa's smoothed window levels."""
    targets = smoothed_targets(_load_measurements())
    notes = [m for m in sorted(targets) if not args.notes or note_name(m) in args.notes]
    with ProcessPoolExecutor() as pool:
        forte = dict(pool.map(_check_one, notes))
    print("rms error of the window levels (dB) per note, partials 1-4 | all measured partials; windows to 2.4 s")
    print(f"(partials Forte's strike leaves more than {CHECK_FLOOR_DB:.0f} dB under partial 1, near a node of the strike point, don't count)")
    early = dm.window_times() <= 2.4
    for midi in notes:
        t, f = targets[midi], forte[midi].levels[: len(targets[midi])]
        d = (f - t)[:, early]
        d[:, 0] = np.nan
        d[forte[midi].attack[: len(t)] < -CHECK_FLOOR_DB] = np.nan
        low = np.sqrt(np.nanmean(d[:4] ** 2))
        every = np.sqrt(np.nanmean(d ** 2))
        line = f"  {note_name(midi):4s} {low:5.1f} | {every:5.1f}"
        if args.notes:
            line += "\n" + "\n".join(f"    p{n:<2d} iowa " + " ".join("    ." if not np.isfinite(v) else f"{v:5.0f}" for v in t[n - 1])
                                     + "\n        forte " + " ".join("    ." if not np.isfinite(v) else f"{v:5.0f}" for v in f[n - 1])
                                     for n in range(1, min(len(t), 8) + 1))
        print(line)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("step", choices=["measure", "measure-sweep", "fit", "floor", "build", "all", "check"])
    parser.add_argument("notes", nargs="*", help="check: Iowa note names to print in full; measure-sweep: the sweep render (.wav)")
    parser.add_argument("--hold", type=float, default=3.0, help="measure-sweep: seconds each note was held")
    parser.add_argument("--gap", type=float, default=2.0, help="measure-sweep: seconds between notes")
    args = parser.parse_args()
    if args.step == "measure-sweep":
        args.path = args.notes[0]
        measure_sweep(args)
        return
    if args.step in ("measure", "all"):
        measure(args)
    if args.step in ("fit", "all"):
        fit(args)
    if args.step == "floor":
        floor(args)
    if args.step in ("build", "all", "floor"):
        build(args)
    if args.step == "check":
        check(args)


if __name__ == "__main__":
    main()
