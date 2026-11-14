"""Writes the MIDI fixtures in this folder. Standard library only; the output is byte-identical
on every run.

    python tests/fixtures/make_midi_fixtures.py
"""

from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
TICKS_PER_QUARTER = 480


def var_len(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def midi_file(events: list[tuple[int, bytes]]) -> bytes:
    """events: (absolute tick, message bytes). Writes a single-track format-0 file at 120 bpm."""
    track = bytearray()
    track += var_len(0) + b"\xff\x51\x03" + (500_000).to_bytes(3, "big")  # tempo: 120 bpm
    now = 0
    for tick, message in sorted(events, key=lambda e: e[0]):
        track += var_len(tick - now) + message
        now = tick
    track += var_len(0) + b"\xff\x2f\x00"  # end of track
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TICKS_PER_QUARTER)
    return header + b"MTrk" + struct.pack(">I", len(track)) + bytes(track)


def smoke() -> bytes:
    """A C major scale with the sustain pedal, a restrike, overlapping note-ons and a chord."""
    q = TICKS_PER_QUARTER
    events: list[tuple[int, bytes]] = [(0, bytes([0xB0, 64, 127]))]
    for i, note in enumerate([60, 62, 64, 65, 67, 69, 71, 72]):
        events.append((i * q // 2, bytes([0x90, note, 40 + i * 10])))
        events.append((i * q // 2 + q // 3, bytes([0x80, note, 0])))
    events.append((4 * q, bytes([0xB0, 64, 0])))
    events += [(5 * q, bytes([0x90, 60, 100])), (5 * q + 10, bytes([0x90, 60, 80]))]  # overlapping
    events += [(6 * q, bytes([0x80, 60, 0])), (6 * q + 1, bytes([0x80, 60, 0]))]
    for note in (36, 48, 55, 64, 72):
        events += [(7 * q, bytes([0x90, note, 112])), (9 * q, bytes([0x80, note, 0]))]
    return midi_file(events)


if __name__ == "__main__":
    (HERE / "smoke.mid").write_bytes(smoke())
    print("wrote", HERE / "smoke.mid")
