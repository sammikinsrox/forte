"""Measurements for the decay fit (SPEC §3.3, issue 0021 step 2): how each partial's level falls
while the key is held, for Iowa recordings and for Forte's strings without the soundboard
(forte-string-probe).

Shared by fit_decay.py; run it directly to print one note's measurements:

    python tools/fitting/decay_measure.py C4 [--forte]

Definitions (the same for Iowa and Forte):

- Partial envelopes: short-time spectra from the strike (hammer_measure.strike_onset), frames of
  max(0.1 s, 6 periods) every 10 ms, Hann, each channel's power averaged (never the channels'
  sum: it cancels some partials, issue 0022 §1). A partial's envelope is the peak power within
  ±0.25 f0 of its frequency, found in the note's first second by forte_audio.track_partials,
  which follows the note's own stretch (the seed's curve put G1's partial 28 on partial 29, issue
  0022 §2).
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
from forte_audio import iowa_file, midi_of, partial_frequency, power_spectrum, read_aiff, stft_power, track_partials

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


def envelopes(x, rate, midi, partials=MAX_PARTIALS, tail_floor=True, start=None):
    """(frame centre times from the strike, [power envelope per partial], [noise floor power per
    partial]) of x, mono or stereo (frames × channels). Frames cover the lead-in too, for the
    floor; with tail_floor, the last second too. start: the strike's sample, if already found."""
    if start is None:
        start = hm.strike_onset(x, rate)
    size, hop = int(frame_seconds(midi) * rate), int(HOP * rate)
    frames_at = np.arange(0, len(x) - size, hop)
    spectra = stft_power(x, size, frames_at)
    freqs = np.fft.rfftfreq(size, 1 / rate)
    times = (frames_at - start) / rate        # frame start, from the strike: a frame starting
    times = times + 0.5 * size / rate          # at the strike is centred half a frame later

    first = x[start: start + int(rate)]
    long_spectrum = power_spectrum(first)
    long_freqs = np.fft.rfftfreq(len(first), 1 / rate)
    f0 = partial_frequency(midi, 1)
    lead_in = frames_at + size < start - int(0.01 * rate)
    tail = (frames_at > len(x) - size - rate) & tail_floor

    out, floors = [], []
    found = track_partials(long_spectrum, long_freqs, midi, partials, max_hz=min(MAX_HZ, 0.45 * rate))
    for actual, _ in found:
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


def measure_mono(x, rate, midi, partials=MAX_PARTIALS):
    """x: mono or stereo (frames × channels)."""
    times, envs, floors = envelopes(x, rate, midi, partials)
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
    return measure_mono(x, int(rate), midi)


SWEEP_LEAD_IN = 1.0       # s before the strike for the floor: the previous note's dampers have had 1 s


def sweep_last_window(hold):
    """The last window that ends before a held note's release (with 0.5 s to spare)."""
    return max(b for _, b in WINDOWS if b <= hold - 0.5)


def measure_sweep(x, rate, schedule, hold, strikes=None, velocities=None):
    """A note-sweep render x (make_note_sweep.py, notes held hold s; mono or stereo): {(midi,
    velocity): Measurement} over the windows that end before the release. schedule: [(seconds,
    midi, velocity)], every note of the sweep in order. Each strike is found by
    forte_audio.find_strikes (it follows a render that starts late or jumps), unless strikes gives
    them; the floor is the second before it. velocities: only those layers are measured."""
    from forte_audio import find_strikes
    last_window = sweep_last_window(hold)
    if strikes is None:
        strikes = find_strikes(x, rate, [s for s, _, _ in schedule])
    out = {}
    for (seconds, midi, velocity), found in zip(schedule, strikes):
        if found is None or (velocities and velocity not in velocities):
            continue
        start = int(round(found * rate))
        if start - int(SWEEP_LEAD_IN * rate) < 0 or start + int((hold - 0.1) * rate) > len(x):
            continue
        segment = x[start - int(SWEEP_LEAD_IN * rate): start + int((hold - 0.1) * rate)]
        times, envs, floors = envelopes(segment, rate, midi, tail_floor=False, start=int(SWEEP_LEAD_IN * rate))
        rows, firsts = [], []
        for envelope, floor in zip(envs, floors):
            levels, valid = window_levels(times, envelope, floor)
            valid &= np.array([w[1] <= last_window for w in WINDOWS])
            firsts.append(levels[0])
            rows.append(np.where(valid, levels - levels[0], np.nan))
        out[(midi, velocity)] = Measurement(midi, np.array(rows), np.array(firsts) - firsts[0])
    return out


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
