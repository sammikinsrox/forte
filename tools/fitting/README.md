# Measuring tools

Measuring tools for voicing the seed piano against the Iowa Steinway, written while listening
(issue 0021), and the first piece of SPEC §3.2's fitting pipeline grown from them: the hammer and
felt fit, in the pipeline's shape (measure → fit → build, committed output, byte-identical runs).

Python with numpy (no `-I`: numpy lives in the user site-packages). Run from the repo root. The
Iowa recordings must be in `refs/iowa` (`tools/fetch-references`); they are measured locally and
never ship. Tools that render default to the Release `forte-render` in `build/`; `--render` takes
another (several where it says `label=path`), such as a copy built with a change to compare.

| Tool | What it does |
|---|---|
| `fit_hammer.py` | The hammer and felt (SPEC §3.3): `measure` → `data/reference-measurements/iowa-hammer.json`, `voice` (the felt set by ear) → `data/model/hammer.json`, `build` → `engine/src/HammerFit.inc`, or `all`. `fit` runs the Iowa fit into `build/fitting/` as a reference: it matched Iowa's attack spectra but sounded worse (issue 0021 §7). |
| `hammer_measure.py` | The fit's measurements: attack partial levels, Iowa's noise floor, octave-band brightness and a board-independent loudness, for Iowa and for Forte's bare strings. Run alone to print one note. |
| `partial_attack_decay.py` | Per partial: attack level (dB re partial 1) and decay rate while held, Forte against Iowa mf. The twang and decay findings. |
| `brightness_by_velocity.py` | A note's attack brightness at several velocities against Iowa pp/mf/ff (C2, C4, C6). The felt and velocity calibration. |
| `keyboard_evenness.py` | Partial-level scatter (§3.4's "irregularity") and neighbouring-note loudness jumps over every Iowa mf note. |
| `band_balance.py` | Third-octave balance and loudness per bridge region against Iowa: the start of §3.4's Microphones colour. |
| `pedal_difference.py` | The sympathetic bank's share of held notes (pedal-down minus pedal-up). §3.3's Resonance calibration. |
| `piece_levels.py` | Peak, RMS and low/high energy shares of rendered pieces; `--match` level-matches a folder for A/B listening. |
| `render_variants.ps1` | Renders one MIDI file through a folder of forte-render builds, level-matched: an A/B set. |
| `probes/StringProbe.cpp` | `forte-string-probe`: note models' raw bridge force, without the soundboard, for hammer and felt variants (a batch of `key=value` requests in, raw float files out). Built with the engine. |
| `forte_audio.py` | Shared: Iowa files and note names, the seed's partials, AIFF/WAV/MIDI reading and writing, note rendering. |

Things learned the hard way (issue 0021 has the full findings):

- Measure the hammer without the board. Through the board, its modes colour single partials by
  10–20 dB and swamp the felt's effect.
- Iowa's own note-to-note brightness scatters by 10–20 dB, so fit smooth curves over many notes,
  never single notes.
- Iowa pp is near the noise floor, and its lead-in noise fools a 5 % onset (it fires ~0.25 s early,
  so pp reads brighter than mf). With a 30 % onset, a per-partial noise floor and a peak-prominence
  test (hammer_measure.py), pp is usable at half weight, except C6 pp, still brighter than its mf.
- Band changes between dynamics belong to the felt alone: the board, microphones and room are
  linear, so they cancel.
- Attack-window spectra are the wrong target for the ear. The Iowa felt fit matched them and
  sounded worse: the twang is the whole note's 1-3.5 kHz layer. Check any fit on whole pieces
  (`piece_levels.py`, band shares) and by ear before trusting it.
- Iowa above ~6 kHz is the recording's hiss (the same level before the note starts), so it can't
  say how much air a piano has.
- Keep decay fits inside the held part of the note (`render_note` holds for 2.5 s), or the damper
  gets fitted too.
- Compare renders level-matched: louder sounds better.
