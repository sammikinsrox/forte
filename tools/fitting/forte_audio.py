"""Shared pieces for the measuring tools: the Iowa references, the seed model's partials, and
reading and writing the files involved (Iowa AIFF, forte-render WAV, MIDI).

Recordings stay stereo (frames × channels). Never sum the channels before a spectrum: the two
microphones hear each partial at its own phase, and the sum cancels some partials by up to ~20 dB
(issue 0022 §1). Spectra are each channel's power, averaged (power_spectrum, stft_power).

numpy only. The references are measured locally and never ship (SPEC §3.1)."""

import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
IOWA = ROOT / "refs" / "iowa"
RENDER = ROOT / "build" / "tools" / "forte-render" / "forte_render_artefacts" / "Release" / "forte-render.exe"

NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]


def note_name(midi):
    """Iowa's note names: C4 = MIDI 60, flats spelled as in the file names."""
    return f"{NAMES[midi % 12]}{midi // 12 - 1}"


def midi_of(name):
    return NAMES.index(name[:-1]) + 12 * (int(name[-1]) + 1)


def iowa_file(midi, dynamic="mf"):
    """The Iowa recording of a note at a dynamic (pp, mf, ff), or None if Iowa doesn't have it."""
    path = IOWA / f"Piano.{dynamic}.{note_name(midi)}.aiff"
    return path if path.exists() else None


def inharmonicity(midi):
    """The seed model's B (engine/src/PianoModel.cpp)."""
    index = midi - 21
    log10b = -3.7 - 0.3 * index / 26 if index < 26 else -4.0 + 2.0 * (index - 26) / 61
    return 10 ** log10b


def partial_frequency(midi, n):
    """Partial n on the seed's average string, tuned so partial 1 is equal temperament at A4 = 440.
    Only a first guess: the engine's B (engine_inharmonicity) is Keyscape's, up to half the seed's
    in the bass, and above partial ~20 the gap outgrows a partial's spacing. Measure partials where
    track_partials finds them."""
    b = inharmonicity(midi)
    return n * 440 * 2 ** ((midi - 69) / 12) / math.sqrt(1 + b) * math.sqrt(1 + b * n * n)


ENGINE_B_KEYS = (21.0, 26.0, 31.0, 36.0, 41.0, 45.0, 91.0, 108.0)   # PianoModel.cpp, bKeys/bLogs
ENGINE_B_LOGS = (-3.76, -3.90, -4.15, -4.12, -4.10, -4.04, -2.32, -1.68)


def engine_inharmonicity(midi):
    """The engine's B (PianoModel.cpp), fitted to the user's Keyscape sweep (issue 0021 §14)."""
    return 10 ** float(np.interp(midi, ENGINE_B_KEYS, ENGINE_B_LOGS))


def track_partials(power, freqs, midi, count, max_hz=None, search=0.3, floor_db=50.0):
    """Partials 1..count as found in a power spectrum: [(frequency, power)], stopping at max_hz or
    the spectrum's top. Each partial is looked for within ±search × f0 of where the partials found
    so far put it (n·F·√(1 + B·n²), F and B refitted by least squares after every partial that is
    within floor_db of the strongest so far and close to where it was expected), starting from equal
    temperament and the engine's B. So a note's own stretch is followed to its high partials, which
    the seed's curve (partial_frequency) misses by more than a partial's spacing in the bass (G1's
    partial 28 there lands on 29, issue 0022 §2)."""
    b = b_engine = engine_inharmonicity(midi)
    f_base = 440 * 2 ** ((midi - 69) / 12) / math.sqrt(1 + b)
    limit = min(max_hz or 1e12, freqs[-1])
    out, good, strongest = [], [], 0.0
    for n in range(1, count + 1):
        expected = n * f_base * math.sqrt(1 + b * n * n)
        width = max(0.03 * expected if n == 1 else search * f_base, 3 * (freqs[1] - freqs[0]))
        if expected + width > limit:
            break
        lo, hi = np.searchsorted(freqs, [expected - width, expected + width])
        if hi <= lo:
            break
        i = lo + int(np.argmax(power[lo:hi]))
        if 0 < i < len(power) - 1 and power[i - 1] > 0 and power[i + 1] > 0:
            y0, y1, y2 = np.log(power[i - 1: i + 2])          # parabolic peak, in log power
            d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
            f = float(freqs[i] + d * (freqs[1] - freqs[0]))
        else:
            f = float(freqs[i])
        out.append((f, float(power[i])))
        strongest = max(strongest, float(power[i]))
        if power[i] > strongest * 10 ** (-floor_db / 10) and abs(f - expected) < 0.5 * width:
            good.append((n, f))
        if good:
            ns = np.array([g[0] for g in good], float)
            fs = np.array([g[1] for g in good])
            if len(good) >= 5:
                slope, intercept = np.polyfit(ns ** 2, (fs / ns) ** 2, 1)
                if intercept > 0:
                    f_base = math.sqrt(intercept)
                    b = float(np.clip(slope / intercept, b_engine / 4, b_engine * 4))
            else:   # too few to tell B from noise: the engine's B, F fitted
                f_base = math.sqrt(np.mean((fs / ns) ** 2 / (1 + b * ns ** 2)))
    return out


def channels(x):
    """x as (frames × channels): a mono signal becomes one channel."""
    x = np.asarray(x)
    return x[:, None] if x.ndim == 1 else x


def envelope_signal(x):
    """One time signal for onsets and levels, never for spectra: the channels' rms."""
    c = channels(x)
    return np.sqrt((c.astype(np.float64) ** 2).mean(axis=1))


def power_spectrum(segment, n=None, window=None):
    """Windowed (Hann by default) power spectrum of a (frames,) or (frames × channels) segment, on
    rfft's grid of n (default: the segment's length): each channel's |FFT|², averaged over the
    channels."""
    c = channels(segment).astype(np.float64)
    w = np.hanning(len(c)) if window is None else window
    return (np.abs(np.fft.rfft(c * w[:, None], n, axis=0)) ** 2).mean(axis=1)


def stft_power(x, size, starts, window=None):
    """Power spectra (frames × bins) of x[i:i + size] for each i in starts, channels averaged as
    power_spectrum does."""
    c = channels(x)
    w = (np.hanning(size) if window is None else window)[:, None]
    out = np.zeros((len(starts), size // 2 + 1))
    for ch in range(c.shape[1]):
        frames = np.stack([c[i:i + size, ch].astype(np.float64) for i in starts]) * w[:, 0]
        out += np.abs(np.fft.rfft(frames, axis=1)) ** 2
    return out / c.shape[1]


def read_aiff(path):
    """16-bit AIFF (Iowa) as (frames × channels float array, sample rate)."""
    data = Path(path).read_bytes()
    pos, channels, bits, rate = 12, None, None, None
    while pos < len(data):
        cid, size = data[pos:pos + 4], struct.unpack(">I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"COMM":
            channels, _, bits = struct.unpack(">hIh", body[:8])
            exponent = struct.unpack(">H", body[8:10])[0] & 0x7FFF
            rate = struct.unpack(">Q", body[10:18])[0] * 2.0 ** (exponent - 16383 - 63)
        elif cid == b"SSND":
            if bits != 16:
                sys.exit(f"{path}: {bits}-bit AIFF isn't handled")
            offset = struct.unpack(">I", body[:4])[0]
            raw = body[8 + offset:]
            frames = len(raw) // (2 * channels)
            return np.frombuffer(raw[: frames * 2 * channels], ">i2").reshape(-1, channels) / 32768.0, rate
        pos += 8 + size + (size & 1)
    sys.exit(f"{path}: no sound data")


def _wav_chunks(data):
    pos = 12
    while pos < len(data):
        cid, size = bytes(data[pos:pos + 4]), struct.unpack("<I", bytes(data[pos + 4:pos + 8]))[0]
        yield cid, pos + 8, size
        pos += 8 + size + (size & 1)


def read_wav(path):
    """WAV (32-bit float, as forte-render writes, or 16/24-bit PCM) as (frames × channels float32,
    rate). float32 holds 16- and 24-bit samples exactly and halves the memory of the long sweeps
    (76 minutes of stereo); the spectrum helpers compute in float64."""
    data = np.memmap(path, np.uint8, "r")
    fmt = channels = rate = bits = None
    for cid, start, size in _wav_chunks(data):
        body = data[start:start + size]
        if cid == b"fmt ":
            fmt, channels, rate, _, _, bits = struct.unpack("<HHIIHH", bytes(body[:16]))
            if fmt == 0xFFFE:
                fmt = struct.unpack("<H", bytes(body[24:26]))[0]
        elif cid == b"data":
            if (fmt, bits) == (3, 32):
                x = np.array(body[: size // 4 * 4].view("<f4"))
            elif (fmt, bits) == (1, 16):
                x = (body[: size // 2 * 2].view("<i2") / np.float32(32768.0)).astype(np.float32)
            elif (fmt, bits) == (1, 24):
                count = size // 3
                x = np.empty(count, np.float32)
                step = 1 << 22
                for a in range(0, count, step):
                    b = np.asarray(body[3 * a: 3 * min(count, a + step)]).reshape(-1, 3).astype(np.int32)
                    v = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) ^ 0x800000) - 0x800000
                    x[a: a + len(v)] = v / np.float32(8388608.0)
            else:
                sys.exit(f"{path}: WAV format {fmt}, {bits} bits isn't handled")
            return x[: len(x) // channels * channels].reshape(-1, channels), rate
    sys.exit(f"{path}: no sound data")


def scale_float_wav(path, gain):
    """Multiplies a 32-bit float WAV's samples by gain, in place."""
    data = bytearray(Path(path).read_bytes())
    for cid, start, size in _wav_chunks(bytes(data)):
        if cid == b"data":
            x = np.frombuffer(bytes(data[start:start + size]), "<f4")
            data[start:start + size] = (x * gain).astype("<f4").tobytes()
            Path(path).write_bytes(bytes(data))
            return
    sys.exit(f"{path}: no sound data")


def write_midi(path, events, ticks_per_second=960):
    """A one-track MIDI file of (seconds, message bytes) events, at 120 bpm."""
    def var_len(v):
        out = [v & 0x7F]
        v >>= 7
        while v:
            out.append(0x80 | (v & 0x7F))
            v >>= 7
        return bytes(reversed(out))

    track = bytearray(var_len(0) + b"\xff\x51\x03" + (500_000).to_bytes(3, "big"))
    now = 0
    for seconds, message in sorted(events, key=lambda e: e[0]):
        tick = int(round(seconds * ticks_per_second))
        track += var_len(tick - now) + bytes(message)
        now = tick
    track += var_len(0) + b"\xff\x2f\x00"
    Path(path).write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ticks_per_second // 2)
                           + b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def render_note(render, midi, velocity=64, hold=2.5, tail=0.5, pedal=False):
    """Renders one held note through forte-render; returns the stereo render (frames × 2) and the
    sample rate."""
    with tempfile.TemporaryDirectory() as tmp:
        mid, wav = Path(tmp) / "note.mid", Path(tmp) / "note.wav"
        events = [(0.0, [0x90, midi, velocity]), (hold, [0x80, midi, 0])]
        if pedal:
            events.insert(0, (0.0, [0xB0, 64, 127]))
        write_midi(mid, events)
        subprocess.run([str(render), str(mid), str(wav), "--tail", str(tail)], check=True, capture_output=True)
        x, rate = read_wav(wav)
    return x.astype(np.float64), rate


def onset(x, threshold=0.05):
    """The first sample above threshold × the peak (of the channels' rms, for stereo)."""
    e = envelope_signal(x)
    return int(np.argmax(e > threshold * e.max()))


def find_strikes(x, rate, times, search=(-0.5, 3.0)):
    """The strike of each note scheduled at times (s, ascending) in a sweep render x: within the
    search window around where the schedule expects it, corrected by the offset found at the note
    before (a render can start late or jump: the user's long Keyscape sweep plays its first note
    on time and every later one 2.3 s late), the biggest rise of the 1 ms peak level over the
    5-30 ms before; then 3 ms before the first sample at 30 % of the next 50 ms's peak, as
    hammer_measure.strike_onset. Returns the strike times (s); None where the window is outside x."""
    out, offset = [], 0.0
    for t in times:
        a, b = int((t + offset + search[0]) * rate), int((t + offset + search[1]) * rate)
        if a < 0 or b > len(x):
            out.append(None)
            continue
        e = envelope_signal(x[a:b])
        hop = rate // 1000
        frames = len(e) // hop
        level = 20 * np.log10(e[: frames * hop].reshape(frames, hop).max(axis=1) + 1e-9)
        before = np.full(frames, np.inf)
        for k in range(30, frames):
            before[k] = level[k - 30: k - 5].max()
        k = int(np.argmax(level - before))
        i = max(0, (k - 10) * hop)
        seg = e[i: i + int(0.05 * rate)]
        j = i + int(np.argmax(seg > 0.3 * seg.max())) - int(0.003 * rate)
        found = (a + j) / rate
        out.append(found)
        offset = found - t
    return out


def partial_levels(x, rate, midi, start, seconds, max_partial=40, max_hz=None):
    """Peak magnitude (dB) at each partial in x[start:start + seconds] (mono or stereo, channels'
    powers averaged), Hann-windowed. The search band around each partial is ±2 %, widened when the
    window is too short to resolve it."""
    x = x[start: start + int(seconds * rate)]
    spectrum = np.sqrt(power_spectrum(x))
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    f0 = partial_frequency(midi, 1)
    half = max(0.02, 2.5 * rate / len(x) / f0)
    limit = min(max_hz or 1e9, 0.45 * rate)
    levels = []
    for n in range(1, max_partial + 1):
        f = partial_frequency(midi, n)
        if f > limit:
            break
        band = (freqs > f * (1 - half / n)) & (freqs < f * (1 + half / n))
        levels.append(20 * np.log10(spectrum[band].max() + 1e-15))
    return np.array(levels)
