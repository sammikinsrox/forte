"""Shared pieces for the measuring tools: the Iowa references, the seed model's partials, and
reading and writing the files involved (Iowa AIFF, forte-render WAV, MIDI).

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
    """Partial n on the seed's average string, tuned so partial 1 is equal temperament at A4 = 440."""
    b = inharmonicity(midi)
    return n * 440 * 2 ** ((midi - 69) / 12) / math.sqrt(1 + b) * math.sqrt(1 + b * n * n)


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
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        yield cid, pos + 8, size
        pos += 8 + size + (size & 1)


def read_wav(path):
    """WAV (32-bit float, as forte-render writes, or 16/24-bit PCM) as (frames × channels, rate)."""
    data = Path(path).read_bytes()
    fmt = channels = rate = bits = None
    for cid, start, size in _wav_chunks(data):
        body = data[start:start + size]
        if cid == b"fmt ":
            fmt, channels, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if fmt == 0xFFFE:
                fmt = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            if (fmt, bits) == (3, 32):
                x = np.frombuffer(body, "<f4").astype(np.float64)
            elif (fmt, bits) == (1, 16):
                x = np.frombuffer(body, "<i2") / 32768.0
            elif (fmt, bits) == (1, 24):
                b = np.frombuffer(body, np.uint8).reshape(-1, 3).astype(np.int32)
                x = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) ^ 0x800000) - 0x800000
                x = x / 8388608.0
            else:
                sys.exit(f"{path}: WAV format {fmt}, {bits} bits isn't handled")
            return x.reshape(-1, channels), rate
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
    """Renders one held note through forte-render; returns the mono mix and the sample rate."""
    with tempfile.TemporaryDirectory() as tmp:
        mid, wav = Path(tmp) / "note.mid", Path(tmp) / "note.wav"
        events = [(0.0, [0x90, midi, velocity]), (hold, [0x80, midi, 0])]
        if pedal:
            events.insert(0, (0.0, [0xB0, 64, 127]))
        write_midi(mid, events)
        subprocess.run([str(render), str(mid), str(wav), "--tail", str(tail)], check=True, capture_output=True)
        x, rate = read_wav(wav)
    return x.mean(axis=1), rate


def onset(mono, threshold=0.05):
    """The first sample above threshold × the peak."""
    return int(np.argmax(np.abs(mono) > threshold * np.abs(mono).max()))


def partial_levels(mono, rate, midi, start, seconds, max_partial=40, max_hz=None):
    """Peak magnitude (dB) at each partial in mono[start:start + seconds], Hann-windowed. The search
    band around each partial is ±2 %, widened when the window is too short to resolve it."""
    x = mono[start: start + int(seconds * rate)]
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
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
