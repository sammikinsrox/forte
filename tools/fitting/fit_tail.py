"""The strings' second polarisation (issue 0022 §4, §6): per note, partials 1..MAX_PARTIALS, a fast
first stage and a slow, quieter tail, fitted to a sweep's decay measurements (fit_decay.py
measure-sweep; the corrected stereo measurement of the user's long Keyscape sweep).

Keyscape's partials fall fast (1-9/s, faster going up), then hold a tail 19-30 dB down that decays
at 0.1-0.8/s. Forte's coupled unison makes its tail only from the strings' detuning, so it had
too little tail where the unison is tight and none on single strings. The engine now gives each
of these partials one extra mode, the strings' horizontal motion, which the hammer starts at the
tail's level and doesn't feel (NoteModel); the vertical strings decay at the fast rate, uncoupled.

    python tools/fitting/fit_tail.py fit <measurements.json>   → data/model/tail.json
    python tools/fitting/fit_tail.py build                     → engine/src/TailFit.inc

Per note and partial: the median window levels over the notes within NEIGHBOURS semitones and
both velocity layers, fitted with power(t) ∝ (1 − h²)·e^(−2·fast·t) + h²·e^(−2·slow·t) (window
means, in dB). A partial counts while the fit is within MAX_RMS_DB and has MIN_WINDOWS windows;
a note's tail partials are the contiguous run from partial 1. Then each parameter is smoothed
over the keyboard (median over ±NEIGHBOURS notes, rates in log), and floored: the tail at least
the physical floor b3·f² of fit_decay.py (issue 0021 §18; without it, Keyscape's hiss plateau
fits as a tail that never decays: the twang), the fast stage at least FAST_OVER_SLOW × the tail,
the level within LEVEL_RANGE_DB.
"""

import argparse
import json
import math
import sys

import numpy as np
from scipy.optimize import least_squares

from fit_decay import floor_b3
from fit_hammer import _round, _round_sig, _write_json, ROOT
from forte_audio import engine_inharmonicity

PARAMETERS = ROOT / "data" / "model" / "tail.json"
TABLE = ROOT / "engine" / "src" / "TailFit.inc"

MAX_PARTIALS = 12          # NoteParameters::maxTailPartials is 16
MAX_HZ = 5000.0
NEIGHBOURS = 3
MIN_WINDOWS = 5
MAX_RMS_DB = 2.5
FAST_OVER_SLOW = 1.5
LEVEL_RANGE_DB = (-45.0, -6.0)
MIN_SLOW = 0.12            # 1/s, a 57 s T60: fit_decay.py's slowest rate, about a real bass string's


def partial_hz(midi, n):
    b = engine_inharmonicity(midi)
    return n * 440 * 2 ** ((midi - 69) / 12) / math.sqrt(1 + b) * math.sqrt(1 + b * n * n)


def model_db(params, windows, n_sub=20):
    fast, slow, level_db = params
    h2 = 10 ** (level_db / 10)
    out = []
    for a, b in windows:
        t = np.linspace(a, b, n_sub)
        out.append(10 * math.log10(((1 - h2) * np.exp(-2 * fast * t) + h2 * np.exp(-2 * slow * t)).mean()))
    out = np.array(out)
    return out - out[0]


def fit_one(target, windows):
    ok = np.isfinite(target)
    if ok.sum() < MIN_WINDOWS:
        return None
    best = None
    for x0 in ([3, 0.3, -15], [1, 0.2, -25], [8, 0.5, -10], [2, 0.8, -6]):
        r = least_squares(lambda q: (model_db(q, windows) - target)[ok], x0, bounds=([0.05, 0.01, -60], [60, 5, -0.01]))
        if best is None or r.cost < best.cost:
            best = r
    fast, slow, level = best.x
    if slow > fast:
        fast, slow, level = slow, fast, 10 * math.log10(max(1e-6, 1 - 10 ** (level / 10)))
    rms = float(np.sqrt(np.mean(((model_db([fast, slow, level], windows) - target)[ok]) ** 2)))
    return (float(fast), float(slow), float(level)) if rms <= MAX_RMS_DB else None


def fit(args):
    data = json.loads(open(args.measurements).read())
    windows = np.array(data["windowsSeconds"])
    notes = {e["midi"]: e for e in data["notes"].values()}

    def rows(m):
        out = []
        for v in ("v64", "v96"):
            r = notes.get(m, {}).get(v, {}).get("windowLevelsDb", [])
            out.append([[np.nan if q is None else q for q in row] for row in r])
        return out

    raw = {}
    for m in range(21, 109):
        per = []
        for n in range(1, MAX_PARTIALS + 1):
            if partial_hz(m, n) > MAX_HZ:
                break
            stack = [r[n - 1] for k in range(m - NEIGHBOURS, m + NEIGHBOURS + 1) for r in rows(k) if len(r) >= n]
            if len(stack) < 3:
                break
            with np.errstate(all="ignore"):
                target = np.nanmedian(np.array(stack, float), axis=0)
            f = fit_one(target, windows)
            if f is None:
                break
            per.append(f)
        raw[m] = per

    notes_out = {}
    for m in range(21, 109):
        count = len(raw[m])
        if count == 0:
            continue
        fast, slow, level = [], [], []
        for n in range(count):
            near = [raw[k][n] for k in range(m - NEIGHBOURS, m + NEIGHBOURS + 1) if k in raw and len(raw[k]) > n]
            f_fast = math.exp(float(np.median([math.log(q[0]) for q in near])))
            f_slow = math.exp(float(np.median([math.log(q[1]) for q in near])))
            f_level = float(np.median([q[2] for q in near]))
            hz = partial_hz(m, n + 1)
            f_slow = max(f_slow, floor_b3(m) * hz * hz, MIN_SLOW)
            f_fast = max(f_fast, FAST_OVER_SLOW * f_slow)
            f_level = float(np.clip(f_level, *LEVEL_RANGE_DB))
            fast.append(f_fast); slow.append(f_slow); level.append(f_level)
        notes_out[str(m)] = {"partials": count, "fast": [_round_sig(q) for q in fast],
                             "slow": [_round_sig(q) for q in slow], "levelDb": [_round(q) for q in level]}

    _write_json(PARAMETERS, {
        "about": "The strings' second polarisation (tools/fitting/fit_tail.py; issue 0022 §6): per note, for "
                 "partials 1..partials, the fast first stage's and the tail's amplitude decay rates (1/s) and the "
                 "tail's level (dB re the partial's strike). Fitted to " + str(args.measurements).replace("\\", "/").split("/")[-1]
                 + ", smoothed over ±" + str(NEIGHBOURS) + " notes, the tail floored at b3·f² (fit_decay.py).",
        "notes": notes_out,
    })


def build(_args):
    data = json.loads(PARAMETERS.read_text())
    lines = ["// Generated by tools/fitting/fit_tail.py build from data/model/tail.json. Don't edit.",
             "// midi, tail partials, then per partial: fast σ, tail σ (1/s), tail level (dB)"]
    for key, e in sorted(data["notes"].items(), key=lambda kv: int(kv[0])):
        def arr(values):
            return "{ " + ", ".join(repr(float(v)) for v in values) + " }"
        lines.append(f"{{ {int(key)}, {e['partials']}, {arr(e['fast'])}, {arr(e['slow'])}, {arr(e['levelDb'])} }},")
    TABLE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {TABLE.relative_to(ROOT)}: {len(data['notes'])} notes")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("step", choices=["fit", "build"])
    parser.add_argument("measurements", nargs="?", help="fit: a sweep's decay measurements (fit_decay.py measure-sweep)")
    args = parser.parse_args()
    if args.step == "fit":
        if not args.measurements:
            sys.exit("fit needs the measurements file")
        fit(args)
    build(args)


if __name__ == "__main__":
    main()
