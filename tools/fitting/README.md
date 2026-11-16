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
| `fit_decay.py` | The strings' decay (SPEC §3.3, issue 0021 step 2): `measure` → `data/reference-measurements/iowa-decay.json`, `fit` → `data/model/decay.json` (prompt and aftersound rates per measured partial, the law above), `build` → `engine/src/DecayFit.inc`, or `all`. `check` measures Forte's bare strings (the Release probe) against the targets. The fit solves the engine's coupled unison in numpy (it matches the probe to 0.2 dB), with the engine's per-note spread. `measure-sweep <wav> --hold H --gap G` measures the user's Keyscape render of a note sweep → `data/reference-measurements/keyscape-decay.json`; once a sweep with holds of at least 6 s is measured, `fit` uses it instead of Iowa (issue 0021 §15). |
| `decay_measure.py` | The decay fit's measurements: each partial's mean level in log-spaced windows from 0.05 to 16 s while it's over the noise floor, for Iowa and for Forte's bare strings. Run alone to print one note. |
| `partial_attack_decay.py` | Per partial: attack level (dB re partial 1) and decay rate while held, Forte against Iowa mf. The twang and decay findings. |
| `brightness_by_velocity.py` | A note's attack brightness at several velocities against Iowa pp/mf/ff (C2, C4, C6). The felt and velocity calibration. |
| `keyboard_evenness.py` | Partial-level scatter (§3.4's "irregularity") and neighbouring-note loudness jumps over every Iowa mf note. |
| `band_balance.py` | Third-octave balance and loudness per bridge region against Iowa: the start of §3.4's Microphones colour. |
| `pedal_difference.py` | The sympathetic bank's share of held notes (pedal-down minus pedal-up). §3.3's Resonance calibration. |
| `piece_levels.py` | Peak, RMS, crest factor and six band shares (<250 Hz, 250-500, 0.5-1k, 1-3.5k, 3.5-8k, >8k) of rendered pieces; `--match` level-matches a folder for A/B listening, `--extra` adds a reference render (Keyscape, Pianoteq) to the table. |
| `make_note_sweep.py` | A MIDI file of every note, one at a time, at velocities 32/64/96/127 (3 s held, 2 s apart, no pedal, 29 minutes; `--hold`/`--gap` for longer, e.g. 11/2 for the 76-minute decay sweep), plus a CSV of the onsets: for rendering a reference instrument and Forte note by note. `analysis/` has the comparison scripts. |
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
- A line fitted to a partial's envelope follows its beats. Mean levels over log-spaced windows
  (decay_measure.py) don't, and they show the two-stage shape: Iowa's C4 fundamental falls 32 dB
  in its first second, then ~1.7 dB/s.
- The 1-3.5 kHz share of a piece isn't the twang by itself: Keyscape's render of Yesterdays has
  more there than Forte and isn't twangy (issue 0021 §9).
- A reference render from another instrument may not keep the MIDI's timing exactly: the user's
  Keyscape render runs ~0.4% fast. Before comparing anything at note onsets, map the times
  (an offset every ~20 s, refined per note), never one global offset (issue 0021 §12).
- Compare renders level-matched: louder sounds better.
