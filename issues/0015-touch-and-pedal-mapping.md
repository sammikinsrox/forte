---
id: 15
title: Touch and pedal mapping
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: []
---

## Question

How do MIDI inputs drive the physical model?

- MIDI velocity → hammer speed (curve shape, range, how the Curve knob bends it).
- Sustain CC64 → damper position, including half-pedaling: continuous values, and hosts or keyboards that send only on/off.
- The soft pedal (CC67): on/off or continuous hammer shift.
- Release velocity, if any.
- What happens with notes outside A0–C8.

Input from [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md): per note, the three hammer speeds that match Iowa's pp, mf and ff loudness. Use them as calibration points for the velocity → hammer-speed curve.

## Resolution

Settled by grilling on 2026-11-14. Every answer took the recommended option.

### Velocity and Touch

1. **Velocity sets hammer speed only.** It never changes loudness or tone directly. Loudness and brightness come from the physics, including the hardening felt.
2. **Per-note curve.** Each note has its own curve through its fitted pp, mf and ff hammer speeds from [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md), placed at **velocities 32, 64 and 112**. The curve always rises and is smooth on a loudness-like scale (log hammer speed).
   - **Below pp:** it extends to velocity 1, about 12 dB quieter than pp.
   - **Above ff:** it extends to velocity 127, about 4–5 dB louder than ff. That top speed is capped where the hammer-stability test from [Choose the synthesis architecture](0007-choose-synthesis-architecture.md) still passes with the hardest felt.
   - Velocity is used at whatever resolution the host delivers. It is not rounded to 7 bits internally.
3. **Touch** (glossary term; replaces "Curve") is one 0–100 control, default 50, which is the calibrated curve. Lower values make the piano **lighter** and higher values make it **heavier**.
   - It bends normalized velocity before the per-note curve. Velocity 1 and 127 always stay the softest and hardest blows, and the curve can never fold back on itself.
   - It is a strike-time parameter, so it affects the next strike only.
   - There is no fixed-velocity mode in v1.

### Sustain pedal (CC64)

4. **Always read as continuous.** On/off pedals just jump between the two ends.
5. **Dead zones:**
   - 0–12 counts as fully down (dampers resting).
   - 116–127 counts as fully lifted.
   - The range in between passes through to the half-pedal gap model. The gap model itself is fitted and set by ear in [Parameter fitting pipeline](0014-parameter-fitting-pipeline.md).
6. **Smoothing:** damper position is smoothed over about 20 ms, the same ramp used for key-up, so stepped values don't make a zipper noise.
7. **Pedal invert setting** for keyboards whose pedal reports reversed. Where it lives is for [Editor control set](0017-editor-control-set.md).

### Soft pedal (CC67)

8. **Continuous 0–1 hammer shift** with the same dead zones as sustain. On/off pedals and the on-screen toggle use the two ends.
9. **Read at each strike.** Pressing or releasing the soft pedal doesn't affect notes that are already ringing.

### On-screen pedals

10. **One shared value per pedal, latest message wins.** The simple view's sustain slider (Up/Half/Down) and soft-pedal toggle move to show incoming MIDI pedal values live. Dragging or clicking them behaves exactly like an incoming pedal message, and nothing is added up or layered.

### Release, restrikes and range

11. **Release velocity is ignored.** Dampers always land with the fixed ramp. This is ruled out of scope for v1.
12. **Every note-on is a true restrike** on the note's current string state.
13. **Overlapping note-ons are counted.** A key stays down until every note-on for it has had its note-off, so overlapping notes in a piano roll don't get cut short.
14. **Notes outside A0–C8** (MIDI 21–108) are ignored silently.

### Other MIDI

15. **Omni:** all MIDI channels are accepted.
16. **Panic and reset messages:**
    - All Notes Off (CC123) releases every key, so the sustain pedal still holds the notes.
    - All Sound Off (CC120) silences everything immediately.
    - Reset All Controllers (CC121) sets both pedals up.
17. **Ignored:** pitch bend, mod wheel, channel and polyphonic aftertouch, and sostenuto (CC66, already out of scope).
18. **Sample-accurate timing:** every MIDI event is applied at its exact sample offset within the audio block.

### Hand-offs

- **To [Editor control set](0017-editor-control-set.md):** the Touch control (0–100, default 50) and the pedal invert setting. Both need a place in the editor.
- **To [Objective validation plan](0019-objective-validation-plan.md):**
  - At Touch 50, velocity 32/64/112 should reproduce pp/mf/ff loudness per note.
  - The curve rises monotonically at every Touch value.
  - Overlapping note-ons don't cut notes short.
  - Events are timed to the exact sample.
