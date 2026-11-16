"""Per track: name, channels with note counts and pitch range, programs. python midi_tracks.py file.mid"""
import struct
import os
import sys
from collections import defaultdict

data = open(sys.argv[1], "rb").read()
fmt, ntrk, div = struct.unpack(">HHH", data[8:14])
print(f"format {fmt}, {ntrk} tracks, division {div}")
pos = 14
tracks_events = []
for t in range(ntrk):
    assert data[pos:pos + 4] == b"MTrk"
    size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
    trk, pos = data[pos + 8:pos + 8 + size], pos + 8 + size
    i, status, name = 0, 0, ""
    notes = defaultdict(list)
    programs = set()

    def vlq():
        global i
        v = 0
        while True:
            b = trk[i]; i += 1
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v
    tick = 0
    events = []
    while i < len(trk):
        tick += vlq()
        b = trk[i]
        if b & 0x80:
            status = b; i += 1
        if status == 0xFF:
            kind = trk[i]; i += 1
            n = vlq()
            if kind == 3:
                name = trk[i:i + n].decode("latin-1")
            i += n
        elif status in (0xF0, 0xF7):
            i += vlq()
        else:
            hi, ch = status & 0xF0, status & 0x0F
            nbytes = 1 if hi in (0xC0, 0xD0) else 2
            args = trk[i:i + nbytes]; i += nbytes
            if hi == 0x90 and args[1] > 0:
                notes[ch].append(args[0])
                events.append((tick, args[0], args[1]))
            if hi == 0xC0:
                programs.add((ch, args[0]))
    desc = ", ".join(f"ch{c + 1}: {len(v)} notes {min(v)}-{max(v)}" for c, v in sorted(notes.items()))
    print(f"track {t} '{name}': {desc or 'no notes'}; programs {sorted(programs)}")
    tracks_events.append(events)
same = [e[:2] for e in tracks_events[1]] == [e[:2] for e in tracks_events[2]] if ntrk > 2 else None
print("tracks 1 and 2 same notes and times:", same, "| same velocities too:", tracks_events[1] == tracks_events[2] if ntrk > 2 else None)
for e in tracks_events[1:3]:
    print("  first:", e[:5], "last tick", e[-1][0] if e else None)
