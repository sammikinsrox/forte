"""Felt (scale, ε) per half-octave from a grid of sweeps: per note and velocity (64, 96, 127), the
mean |difference| of partials 2-6 at the strike (dB re partial 1, both floored at -60 dB) from the
reference; per window of ±3 semitones the grid point with the least median error.
python grid_fit.py reference.wav scale:eps:sweep.wav ..."""
import math
import os
import sys
here = os.path.dirname(os.path.abspath(__file__))
my_argv = list(sys.argv)
sys.argv = [my_argv[0], "x", "y"]
exec(open(os.path.join(here, "strike_partials.py")).read().split("vel = int(sys.argv[1])")[0])
sys.argv = my_argv

FLOOR = -60.0
ref_path = my_argv[1]
grid = []
for spec in my_argv[2:]:
    scale, eps, path = spec.split(":", 2)
    grid.append((float(scale), float(eps), path))
err = {}   # (midi, grid index) -> list of errors
for vel in (64, 96, 127):
    ref = per_note(ref_path, vel)
    for gi, (_, _, path) in enumerate(grid):
        d = per_note(path, vel)
        for m in range(21, 109):
            if m in ref and m in d:
                a, b = np.maximum(d[m][:5], FLOOR), np.maximum(ref[m][:5], FLOOR)
                e = np.nanmean(np.abs(a - b))
                if np.isfinite(e):
                    err.setdefault((m, gi), []).append(e)
anchors = list(range(24, 109, 6))
print("anchor: best (scale, eps) and its median error | each grid point's error")
print("        " + " ".join(f"{s:g}/{e:g}".rjust(9) for s, e, _ in grid))
chosen = []
for a in anchors:
    scores = []
    for gi in range(len(grid)):
        vals = [v for m in range(a - 3, a + 4) for v in err.get((m, gi), [])]
        scores.append(np.median(vals) if vals else np.inf)
    gi = int(np.argmin(scores))
    chosen.append((a, grid[gi][0], grid[gi][1]))
    print(f"  {a:3d}: ({grid[gi][0]:g}, {grid[gi][1]:g}) {scores[gi]:5.1f} | " + " ".join(f"{v:9.1f}" for v in scores))
print("VOICING = [" + ", ".join(f"({a}, {s:g}, {e:g})" for a, s, e in chosen) + "]")
