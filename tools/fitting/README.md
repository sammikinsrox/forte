# Measuring tools

Early measuring tools for voicing the seed piano against the Iowa Steinway, written while listening
(issue 0021). They are not yet the fitting pipeline of SPEC §3.2 (measure → fit → build, with
committed output), but that pipeline will need the same measurements, so it should grow from here.

Python with numpy (no `-I`: numpy lives in the user site-packages). Run from the repo root. The
Iowa recordings must be in `refs/iowa` (`tools/fetch-references`); they are measured locally and
never ship. Tools that render default to the Release `forte-render` in `build/`; `--render` takes
another (several where it says `label=path`), such as a copy built with a change to compare.

| Tool | What it measures |
|---|---|
| `partial_attack_decay.py` | Per partial: attack level (dB re partial 1) and decay rate while held, Forte against Iowa mf. The twang and decay findings. |
| `brightness_by_velocity.py` | A note's attack brightness at several velocities against Iowa pp/mf/ff (C2, C4, C6). The felt and velocity calibration. |
| `keyboard_evenness.py` | Partial-level scatter (§3.4's "irregularity") and neighbouring-note loudness jumps over every Iowa mf note. |
| `band_balance.py` | Third-octave balance and loudness per bridge region against Iowa: the start of §3.4's Microphones colour. |
| `pedal_difference.py` | The sympathetic bank's share of held notes (pedal-down minus pedal-up). §3.3's Resonance calibration. |
| `piece_levels.py` | Peak, RMS and low/high energy shares of rendered pieces; `--match` level-matches a folder for A/B listening. |
| `render_variants.ps1` | Renders one MIDI file through a folder of forte-render builds, level-matched: an A/B set. |
| `probes/StringProbe.cpp` | A note model's raw bridge force, without the soundboard, for hammer variants. Not built by default; see the file. |
| `forte_audio.py` | Shared: Iowa files and note names, the seed's partials, AIFF/WAV/MIDI reading and writing, note rendering. |

Things learned the hard way (issue 0021 has the full findings):

- Measure the hammer without the board. Through the board, its modes colour single partials by
  10–20 dB and swamp the felt's effect.
- Iowa's own note-to-note brightness scatters by 10–20 dB, so fit smooth curves over many notes,
  never single notes.
- Iowa pp is near the noise floor: it reads brighter than mf. Use mf and ff.
- Keep decay fits inside the held part of the note (`render_note` holds for 2.5 s), or the damper
  gets fitted too.
- Compare renders level-matched: louder sounds better.
