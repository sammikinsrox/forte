"""Measurements for the hammer and felt fit (SPEC §3.3, issue 0021): a note's attack spectrum as
partial levels, a board-independent loudness, and a few octave-band brightness numbers, for Iowa
recordings and for Forte's strings without the soundboard (forte-string-probe).

Shared by fit_hammer.py; run it directly to print one note's measurements:

    python tools/fitting/hammer_measure.py C4

Definitions (the same for Iowa and Forte):

- Attack window: from the onset, max(0.15 s, 8 periods), Hann. Short, so it sees the strike's
  flash rather than the decay.
- Partial levels: peak magnitude near each partial (forte_audio.partial_levels), up to 10 kHz.
- Iowa noise floor: per partial, the rms magnitude at that frequency over frames of the same length
  from the quietest of the recording's lead-in and its last second. Partials less than 10 dB
  above it don't count (SPEC §3.3's rule for decays); pp loses many.
- Reference level: the power mean of the partials in one octave from max(f1, 200 Hz). For C4 and up
  that's the fundamental; in the bass it's the partials around 200-400 Hz, because the board
  radiates the lowest fundamentals weakly (issue 0021 §4) and that's the board's to match (§3.4),
  not the felt's.
- Loudness: the reference level. Differences between dynamics are then independent of the
  soundboard, the microphones and the room (all linear), which is what the speed solve needs.
- Band brightness: the median level of the partials in each octave above the reference band
  (octave k covers [2^k, 2^(k+1)) × the reference band's start), dB re the reference level. A
  median, not a power mean: a power mean follows the loudest partial, which in Iowa is often one
  picked out by a board mode (and in Forte one between the strike's spectral notches), while the
  twang is the level of the whole layer. Partials that don't count rank lowest; a band where
  most don't count is unknown (NaN).
"""

import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

from forte_audio import ROOT, iowa_file, midi_of, partial_frequency, partial_levels, read_aiff

PROBE = ROOT / "build" / "tools" / "fitting" / "probes" / "Release" / "forte-string-probe.exe"
MAX_HZ = 10000.0
FLOOR_MARGIN_DB = 10.0
PROMINENCE_DB = 6.0
LOWEST_DB = -120.0  # partial levels are clipped here (dB re the reference) before the band median
BANDS = 5  # octaves above the reference band


def strike_onset(mono, rate):
    """3 ms before the first sample at 30 % of the peak. forte_audio.onset's 5 % fires on Iowa pp's
    lead-in noise, a quarter of a second early; a strike reaches its peak within a few ms."""
    return max(0, int(np.argmax(np.abs(mono) > 0.3 * np.abs(mono).max())) - int(0.003 * rate))


def window_seconds(midi):
    return max(0.15, 8.0 / partial_frequency(midi, 1))


def reference_start(midi):
    return max(partial_frequency(midi, 1), 200.0)


def partial_numbers(midi, rate):
    limit = min(MAX_HZ, 0.45 * rate)
    n = 0
    while partial_frequency(midi, n + 1) < limit:
        n += 1
    return np.arange(1, n + 1)


def _noise_spectrum(segment, size, rate):
    """rms Hann-windowed magnitude of a noise segment, as a size-sample frame would see it, on
    size's frequency grid. Shorter segments use shorter frames, scaled by √(size / frame): noise
    magnitude grows with the square root of the frame length."""
    frame = min(size, len(segment))
    if frame < int(0.03 * rate):
        return None
    window = np.hanning(frame)
    hop = max(frame // 2, 1)
    power = np.mean([np.abs(np.fft.rfft(segment[i:i + frame] * window)) ** 2
                     for i in range(0, len(segment) - frame + 1, hop)], axis=0)
    magnitude = np.sqrt(power * size / frame)
    return np.interp(np.fft.rfftfreq(size, 1 / rate), np.fft.rfftfreq(frame, 1 / rate), magnitude)


def noise_floor(mono, rate, midi, seconds):
    """Per partial: rms magnitude (dB) at its frequency in noise-only frames, as partial_levels
    measures. From the lead-in and the last second, whichever is quieter at that partial: the
    lead-in is often short, and the end can still hold the note's lowest partials."""
    size = int(seconds * rate)
    freqs = np.fft.rfftfreq(size, 1 / rate)
    start = strike_onset(mono, rate)
    spectra = [s for s in (_noise_spectrum(mono[: max(0, start - int(0.01 * rate))], size, rate),
                           _noise_spectrum(mono[-int(max(1.0, 2 * seconds) * rate):], size, rate)) if s is not None]
    if not spectra:
        return None
    f0 = partial_frequency(midi, 1)
    half = max(0.02, 2.5 * rate / size / f0)
    out = []
    for n in partial_numbers(midi, rate):
        f = partial_frequency(midi, n)
        band = (freqs > f * (1 - half / n)) & (freqs < f * (1 + half / n))
        out.append(min(20 * np.log10(np.sqrt(np.mean(s[band] ** 2)) + 1e-15) for s in spectra))
    return np.array(out)


def power_mean_db(levels):
    levels = np.asarray(levels)
    levels = levels[np.isfinite(levels)]
    if len(levels) == 0:
        return np.nan
    return 10 * np.log10(np.mean(10 ** (levels / 10)))


def summarise(midi, levels, valid):
    """Reference (loudness) level and band brightness from partial levels and which count."""
    numbers = np.arange(1, len(levels) + 1)
    freqs = np.array([partial_frequency(midi, n) for n in numbers])
    start = reference_start(midi)
    in_ref = (freqs >= start * 0.999) & (freqs < 2 * start)
    reference = power_mean_db(np.where(valid, levels, np.nan)[in_ref])
    bands = []
    for k in range(1, BANDS + 1):
        inside = (freqs >= start * 2 ** k) & (freqs < start * 2 ** (k + 1))
        if not inside.any() or 2 * valid[inside].sum() <= inside.sum():
            bands.append(np.nan)
            continue
        ranked = np.where(valid[inside], np.maximum(levels[inside] - reference, LOWEST_DB), -np.inf)
        bands.append(float(np.median(ranked)))
    return reference, np.array(bands)


class Measurement:
    """One note at one dynamic: partial levels, which count, and the summary numbers."""

    def __init__(self, midi, levels, valid):
        self.midi = midi
        self.levels = levels
        self.valid = valid
        self.reference, self.bands = summarise(midi, levels, valid)

    def relative(self):
        """Partial levels re the reference level, NaN where they don't count."""
        return np.where(self.valid, self.levels - self.reference, np.nan)


def prominence(mono, rate, midi, start, seconds, levels):
    """Per partial: its level over the median level between it and its neighbours (dB). Broadband
    strike noise (the knock) has no peaks, so it scores near 0."""
    x = mono[start: start + int(seconds * rate)]
    magnitude = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    out = []
    for n in range(1, len(levels) + 1):
        f = partial_frequency(midi, n)
        spacing = partial_frequency(midi, n + 1) - f
        between = ((freqs > f + 0.3 * spacing) & (freqs < f + 0.7 * spacing)) | \
                  ((freqs > f - 0.7 * spacing) & (freqs < f - 0.3 * spacing) & (freqs > 0.5 * f))
        valley = 20 * np.log10(np.median(magnitude[between]) + 1e-15) if between.any() else -np.inf
        out.append(levels[n - 1] - valley)
    return np.array(out)


def measure_mono(mono, rate, midi, floor=None):
    """Partial levels in the attack window. With a noise floor (Iowa), a partial counts only if it
    is FLOOR_MARGIN_DB over the floor and PROMINENCE_DB over the spectrum between partials."""
    seconds = window_seconds(midi)
    start = strike_onset(mono, rate)
    levels = partial_levels(mono, rate, midi, start, seconds, max_partial=10_000, max_hz=MAX_HZ)
    valid = np.ones(len(levels), bool)
    if floor is not None:
        count = min(len(levels), len(floor))
        levels = levels[:count]
        valid = (levels >= floor[:count] + FLOOR_MARGIN_DB) & (prominence(mono, rate, midi, start, seconds, levels) >= PROMINENCE_DB)
    return Measurement(midi, levels, valid)


_iowa_cache = {}


def measure_iowa(midi, dynamic="mf"):
    """Iowa's measurement, or None if Iowa doesn't have the note at that dynamic."""
    key = (midi, dynamic)
    if key not in _iowa_cache:
        path = iowa_file(midi, dynamic)
        if path is None:
            _iowa_cache[key] = None
        else:
            x, rate = read_aiff(path)
            mono = x.mean(axis=1)
            floor = noise_floor(mono, rate, midi, window_seconds(midi))
            _iowa_cache[key] = measure_mono(mono, rate, midi, floor)
    return _iowa_cache[key]


def probe(requests, probe_exe=PROBE, rate=48000):
    """Renders [(name, {setting: value})] through forte-string-probe; returns {name: mono}."""
    with tempfile.TemporaryDirectory() as tmp:
        lines = [f"{name} " + " ".join(f"{k}={float(v)!r}" for k, v in settings.items()) for name, settings in requests]
        request_file = Path(tmp) / "requests.txt"
        request_file.write_text("\n".join(lines) + "\n")
        result = subprocess.run([str(probe_exe), str(request_file), tmp], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"forte-string-probe failed: {result.stderr.strip()}")
        return {name: np.fromfile(Path(tmp) / f"{name}.f32", "<f4").astype(np.float64) for name, _ in requests}


def measure_forte(midi, settings_list, probe_exe=PROBE, iowa_mask=None):
    """Forte's strings for several settings dicts (each needs speed or velocity); with iowa_mask,
    only the partials that count in Iowa count here too."""
    seconds = window_seconds(midi) + 0.1
    requests = [(f"r{i}", {"midi": midi, "seconds": seconds, **s}) for i, s in enumerate(settings_list)]
    out = probe(requests, probe_exe)
    measurements = []
    for name, _ in requests:
        m = measure_mono(out[name], 48000, midi)
        if iowa_mask is not None:
            count = min(len(m.levels), len(iowa_mask))
            m = Measurement(midi, m.levels[:count], m.valid[:count] & iowa_mask[:count])
        measurements.append(m)
    return measurements


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "C4"
    midi = midi_of(name)
    print(f"{name}: reference band from {reference_start(midi):.0f} Hz, window {window_seconds(midi) * 1000:.0f} ms")
    print("            ref dB  partials counted   octave bands above the reference (dB re reference)")
    for dynamic in ("pp", "mf", "ff"):
        m = measure_iowa(midi, dynamic)
        if m is None:
            continue
        bands = " ".join(f"{b:6.1f}" for b in m.bands)
        print(f"  Iowa {dynamic}  {m.reference:6.1f}  {m.valid.sum():4d} of {len(m.valid):4d}       {bands}")


if __name__ == "__main__":
    main()
