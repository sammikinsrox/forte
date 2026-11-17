"""How a MIDI file plays: velocities, note lengths, pedal, other controllers, tempo.
python midi_stats.py a.mid [b.mid ...]"""
import struct
import os
import sys
from collections import Counter

import numpy as np


def parse(path):
    data = open(path, "rb").read()
    fmt, ntrk, div = struct.unpack(">HHH", data[8:14])
    pos, events = 14, []
    for _ in range(ntrk):
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        trk, pos = data[pos + 8:pos + 8 + size], pos + 8 + size
        i = tick = status = 0
        while i < len(trk):
            v = 0
            while True:
                b = trk[i]; i += 1
                v = (v << 7) | (b & 0x7F)
                if not b & 0x80:
                    break
            tick += v
            if trk[i] & 0x80:
                status = trk[i]; i += 1
            if status == 0xFF:
                kind = trk[i]; i += 1
                n = 0
                while True:
                    b = trk[i]; i += 1
                    n = (n << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                if kind == 0x51:
                    events.append((tick, "tempo", int.from_bytes(trk[i:i + 3], "big"), 0, 0))
                i += n
            elif status in (0xF0, 0xF7):
                n = trk[i]; i += 1 + n
            else:
                hi, ch = status & 0xF0, status & 0x0F
                nb = 1 if hi in (0xC0, 0xD0) else 2
                a = trk[i:i + nb]; i += nb
                events.append((tick, hi, ch, a[0], a[1] if nb == 2 else 0))
    return div, sorted(events, key=lambda e: e[0])


def seconds_map(div, events):
    tempos = [(0, 500000)] + [(e[0], e[2]) for e in events if e[1] == "tempo"]
    def sec(tick):
        s, last_t, last_tempo = 0.0, 0, 500000
        for t, tempo in tempos:
            if t > tick:
                break
            s += (t - last_t) * last_tempo / div / 1e6
            last_t, last_tempo = t, tempo
        return s + (tick - last_t) * last_tempo / div / 1e6
    return sec


for path in sys.argv[1:]:
    div, ev = parse(path)
    sec = seconds_map(div, ev)
    ons, lengths, vels, open_ = [], [], [], {}
    pedal, ccs, bends = [], Counter(), 0
    for t, kind, ch, a, b in ev:
        if kind == 0x90 and b > 0:
            open_[(ch, a)] = t; vels.append(b); ons.append(sec(t))
        elif kind == 0x80 or (kind == 0x90 and b == 0):
            if (ch, a) in open_:
                lengths.append(sec(t) - sec(open_.pop((ch, a))))
        elif kind == 0xB0:
            ccs[a] += 1
            if a == 64:
                pedal.append((sec(t), b))
        elif kind == 0xE0:
            bends += 1
    end = max(ons)
    down = 0.0
    for (t0, v0), (t1, _) in zip(pedal, pedal[1:]):
        if v0 >= 64:
            down += t1 - t0
    vals = Counter(v for _, v in pedal)
    v = np.array(vels); L = np.array(lengths)
    print(f"{path.split(chr(92))[-1].split('/')[-1]}: {len(v)} notes in {end/60:.1f} min, tempos {len([e for e in ev if e[1]=='tempo'])}")
    print(f"  velocity median {np.median(v):.0f}, 10-90% {np.percentile(v,10):.0f}-{np.percentile(v,90):.0f}, distinct {len(set(vels))}")
    print(f"  note length median {np.median(L):.2f} s, 10-90% {np.percentile(L,10):.2f}-{np.percentile(L,90):.2f} s")
    print(f"  pedal: {len(pedal)} events, down {100*down/end:.0f}% of the time, values {sorted(vals.items())[:12]}")
    print(f"  controllers {dict(sorted(ccs.items()))}, pitch bends {bends}")
