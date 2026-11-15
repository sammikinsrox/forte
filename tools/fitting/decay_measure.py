"""Measurements for the decay fit (SPEC §3.3, issue 0021 step 2): how each partial's level falls
while the key is held, for Iowa recordings and for Forte's strings without the soundboard
(forte-string-probe).

Shared by fit_decay.py; run it directly to print one note's measurements:

    python tools/fitting/decay_measure.py C4 [--forte]

Definitions (the same for Iowa and Forte):

- Partial envelopes: short-time spectra from the strike (hammer_measure.strike_onset), frames of
  max(0.1 s, 6 periods) every 10 ms, Hann. A partial's envelope is the peak power within ±0.25 f0
  of its frequency, found in the note's first second (Iowa's stretch differs from the seed's).
- Window levels: mean power over WINDOWS (log-spaced, 0.05-16 s after the strike), dB. A mean
  over a window, not a fitted line: real and modelled unisons both beat, and a line through a
  beating envelope follows the beat. Window levels are relative to the partial's first window.
- Noise floor: per partial, the median power at its frequency over the lead-in and the last
  second, whichever is quieter (for the probe, which has no noise, PROBE_FLOOR_DB under partial
  1's peak: the frames' leakage). A window counts only if it is FLOOR_MARGIN_DB over the floor,
  and once one window fails, every later window fails too (a noisy tail is never trusted, SPEC
  §3.3). A partial is measured if its first MIN_WINDOWS windows count.
"""

import sys

import numpy as np

import hammer_measure as hm
from forte_audio import iowa_file, midi_of, partial_frequency, read_aiff

WINDOWS = [(0.05, 0.12), (0.12, 0.25), (0.25, 0.45), (0.45, 0.8), (0.8, 1.4), (1.4, 2.4),
           (2.4, 4.0), (4.0, 6.5), (6.5, 10.0), (10.0, 16.0)]
HOP = 0.01
MAX_PARTIALS = 32      # the coupled partials (SPEC §2.3); above them the law takes over
MAX_HZ = 10000.0
FLOOR_MARGIN_DB = 10.0
MIN_WINDOWS = 4        # 0.05-0.8 s: a partial measured for less than that has no decay to fit
PROBE_SECONDS = 16.5
PROBE_FLOOR_DB = 90.0  # under partial 1's peak: Hann frames' leakage, for renders with no noise


def window_times():
    return np.array([np.sqrt(a * b) for a, b in WINDOWS])


def frame_seconds(midi):
    return max(0.1, 6.0 / partial_frequency(midi, 1))


def envelopes(mono, rate, midi, partials=MAX_PARTIALS):
    """(frame centre times from the strike, [power envelope per partial], [noise floor power per
    partial]). Frames cover the lead-in too, for the floor."""
    start = hm.strike_onset(mono, rate)
    size, hop = int(frame_seconds(midi) * rate), int(HOP * rate)
    window = np.hanning(size)
    frames_at = np.arange(0, len(mono) - size, hop)
    spectra = np.abs(np.fft.rfft(np.stack([mono[i:i + size] * window for i in frames_at]), axis=1)) ** 2
    freqs = np.fft.rfftfreq(size, 1 / rate)
    times = (frames_at - start) / rate        # frame start, from the strike: a frame starting
    times = times + 0.5 * size / rate          # at the strike is centred half a frame later

    first = mono[start: start + int(rate)]
    long_spectrum = np.abs(np.fft.rfft(first * np.hanning(len(first))))
    long_freqs = np.fft.rfftfreq(len(first), 1 / rate)
    f0 = partial_frequency(midi, 1)
    lead_in = frames_at + size < start - int(0.01 * rate)
    tail = frames_at > len(mono) - size - rate

    out, floors = [], []
    for n in range(1, partials + 1):
        expected = partial_frequency(midi, n)
        if expected > min(MAX_HZ, 0.45 * rate):
            break
        near = (long_freqs > expected - 0.3 * f0) & (long_freqs < expected + 0.3 * f0)
        actual = long_freqs[near][np.argmax(long_spectrum[near])]
        band = (freqs > actual - 0.25 * f0) & (freqs < actual + 0.25 * f0)
        envelope = spectra[:, band].max(axis=1)
        candidates = [np.median(envelope[sel]) for sel in (lead_in, tail) if sel.sum() >= 5]
        out.append(envelope)
        floors.append(min(candidates) if candidates else 0.0)
    return times, out, floors


def window_levels(times, envelope, floor):
    """Per window: mean power (dB) and whether it counts."""
    levels, valid = [], []
    ok = True
    for a, b in WINDOWS:
        sel = (times >= a) & (times < b)
        if sel.sum() < 2:
            levels.append(np.nan); valid.append(False); ok = False
            continue
        mean = float(envelope[sel].mean())
        level = 10 * np.log10(mean + 1e-30)
        ok = ok and (floor <= 0 or level > 10 * np.log10(floor) + FLOOR_MARGIN_DB)
        levels.append(level); valid.append(ok)
    return np.array(levels), np.array(valid)


class Measurement:
    """levels[n - 1][w]: window w's level re window 0 (dB), NaN where it doesn't count;
    attack[n - 1]: window 0's level re partial 1's (dB)."""

    def __init__(self, midi, levels, attack):
        self.midi, self.levels, self.attack = midi, levels, attack

    @property
    def measured(self):
        """How many partials, from the first, are measured (all of 1..count)."""
        count = 0
        for row in self.levels:
            if np.isfinite(row[:MIN_WINDOWS]).all():
                count += 1
            else:
                break
        return count


def measure_mono(mono, rate, midi, partials=MAX_PARTIALS):
    times, envs, floors = envelopes(mono, rate, midi, partials)
    if not any(f > 0 for f in floors):
        # A render with no noise (the probe): spectral leakage from the loud partials is the floor.
        floors = [float(envs[0].max()) * 10 ** (-PROBE_FLOOR_DB / 10)] * len(envs)
    rows, firsts = [], []
    for envelope, floor in zip(envs, floors):
        levels, valid = window_levels(times, envelope, floor)
        firsts.append(levels[0])
        rows.append(np.where(valid, levels - levels[0], np.nan))
    return Measurement(midi, np.array(rows), np.array(firsts) - firsts[0])


def measure_iowa(midi):
    path = iowa_file(midi)
    if path is None:
        return None
    x, rate = read_aiff(path)
    return measure_mono(x.mean(axis=1), int(rate), midi)


def measure_forte(midi, settings=None, probe_exe=hm.PROBE):
    """Forte's bare strings, key held, at the mf speed."""
    request = dict(midi=midi, seconds=PROBE_SECONDS, **(settings or {}))
    y = hm.probe([("d", request)], probe_exe)["d"]
    # The probe renders from the strike with no lead-in or noise: the floor is the float's.
    return measure_mono(np.concatenate([np.zeros(4800), y]), 48000, midi)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    name = sys.argv[1] if len(sys.argv) > 1 else "C4"
    midi = midi_of(name)
    m = measure_forte(midi) if "--forte" in sys.argv else measure_iowa(midi)
    print(f"{name}: {m.measured} partials measured; window levels re the first window (dB)")
    print("  n  attack " + " ".join(f"{t:6.2f}" for t in window_times()))
    for n, (row, attack) in enumerate(zip(m.levels, m.attack), start=1):
        print(f"{n:3d} {attack:7.1f} " + " ".join("     ." if not np.isfinite(v) else f"{v:6.1f}" for v in row))


if __name__ == "__main__":
    main()
