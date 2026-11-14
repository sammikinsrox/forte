---
id: 18
title: Presets, saved state and automation
labels: [wayfinder:grilling]
status: closed
assignee: sam
parent: 1
blocked_by: [17]
---

## Question

Cover the following:

- **Presets:** what one contains, whether v1 ships factory presets, and the preset file format.
- **Automation:** which parameters the DAW can automate.
- **Saved state:** how it survives host save and load, including versioning for future model changes.
- **Edge cases:** how parameter changes behave during playback (already set in Choose the synthesis architecture, Q14), and what happens on sample-rate changes and offline bounce.

Input from [Editor control set](0017-editor-control-set.md):
- The parameter list is fixed there: Hardness, Brightness, Resonance, Volume, Listening perspective, Sustain, Soft, Touch, A4, Stretch, Spread and Width.
- Decide which of those are automatable and which are saved in a preset. Sustain and Soft mirror live MIDI pedals, so they probably aren't saved in a preset.
- Decide what the preset name and ‹ › arrows do.
- Decide whether the Settings menu's *Invert sustain pedal* / *Invert soft pedal* and the Light/Dark theme are saved per machine or per project.

## Resolution (2026-11-14)

Decided with the user in one grilling round, all recommendations accepted. Everything below is spec. New glossary terms: **Preset**, **Factory preset** (see [GLOSSARY.md](../GLOSSARY.md)). ADR: [Saved projects follow the latest piano model](../docs/adr/0001-projects-follow-latest-model.md).

### Where each control is saved

| Control | Preset | Automatable | Project state |
|---|---|---|---|
| Hardness, Brightness, Resonance, Touch, Stretch, Spread, Width | ✓ | ✓ | ✓ |
| Listening perspective | ✓ | ✓ (two-state) | ✓ |
| Volume | — | ✓ | ✓ |
| A4 | — | ✓ | ✓ |
| Sustain, Soft | — | — (they arrive as MIDI CC64/CC67) | — (they start released) |
| Invert sustain / Invert soft, Light/Dark theme | — | — | — (saved per computer) |

### Presets

1. **Contents:** the 8 character controls only. Volume (mix level), A4 (session tuning) and the pedals stay put when you change preset.
2. **Factory presets:** a small curated set of about 5–8, built in and read-only. **"Concert Grand"** (all defaults) is listed first and is the default sound. Candidates are Pianist's Seat, Mellow, Bright, Intimate and Honest Equal (Stretch 0). The final list and values are set by ear in the first playable build.
3. **Format:** user presets are one JSON file each, `Documents\Forte\Presets\<name>.fortepreset`. Each file holds a format version, the piano model id (`concert-grand`), the name, and the 8 values as whole numbers 0–100.
4. **Name and ‹ › arrows:**
   - **Arrows:** step through the factory presets, then user presets (alphabetical), wrapping at the ends.
   - **Changed indicator:** the name shows a `*` once any preset control differs from the loaded preset.
   - **Clicking the name** opens a menu with the Factory and User lists, Save (user presets only), Save as…, Rename, Delete and Open presets folder.
   - **Unsaved changes:** arrowing away discards them silently.
   - **No MIDI program change** in v1.
5. **Loading a preset while notes ring** works like simultaneous automation and follows decision 14 of [Choose the synthesis architecture](0007-choose-synthesis-architecture.md):
   - Hardness and Touch apply to the next strike;
   - Stretch glides over ~50 ms;
   - output controls smooth over ~20 ms;
   - perspective crossfades.

   Ringing notes are never cut off.

### Automation

6. **Automatable:** the 10 sound controls in the table above. They follow the same live-change rules.
7. **Not automatable:** Sustain and Soft, because they'd compete with incoming MIDI pedals.
8. **Parameter IDs are permanent** and never renamed or reused.
9. **Host display:** the DAW shows the editor's own text ("50", "−6 dB", "Off", "440 Hz", "Pianist").

### Saved state

10. **The project saves:** the 10 automatable values, the preset name and its `*` flag, the open view (simple/advanced) and a state-format version. Pedals are not saved; they start released.
11. **Versioning** (see the ADR):
    - projects follow the latest piano model, and release notes flag sound changes;
    - a newer Forte always opens older states;
    - an older Forte loads the fields it knows and ignores the rest.
12. **Per computer:** the pedal-invert switches and theme live in `%APPDATA%\Forte\settings.json`. They apply to every project and to the standalone app.

### Engine edge cases

13. **Sample-rate change:** the engine rebuilds while audio is stopped (recompute modes, convert the 48 kHz IRs to the new rate, rerun the tuner), and ringing notes stop. Rebuild time is measured in the first build; the target is well under 1 s.
14. **Offline bounce:** a bounce renders exactly what plays in real time, with no separate render-quality mode. Renders are deterministic: any randomness uses fixed seeds, so the same project gives bit-identical audio every time.

### Handed on

- To [Objective validation plan](0019-objective-validation-plan.md), as candidate checks:
  - bit-identical repeat renders;
  - a preset change mid-note never cuts notes off;
  - a state saved by one build loads in the next.
