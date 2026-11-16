# Voicing analysis scripts

Scratch-quality scripts from the Keyscape voicing work ([issue 0021](../../../issues/0021-voicing-against-iowa.md) §14–15), kept so a later session can rerun the comparisons. They are not part of the pipeline (`fit_*.py` are) and their output isn't committed. Each prints its own usage in its docstring. They cache measurements as `*.pkl` beside themselves (git-ignored), keyed by the input's name and modification time.

Run them with plain `python` (not `-I`: numpy). Note sweeps come from `../make_note_sweep.py`; a sweep's schedule is the CSV beside its MIDI (`FORTE_SWEEP_CSV` picks one other than `build/ab/single-notes/note-sweep.csv`).

| Script | What it compares |
|---|---|
| `sweep_align.py` | Finds each scheduled note of a sweep render (used by the others) |
| `sweep_compare.py` | Note by note: inharmonicity, partials over time, strike noise share, wavering |
| `evolution.py`, `evolution_treble.py` | Partials 2–8 against partial 1 at the strike, 0.3–0.6 s and 1–2 s, per register |
| `strike_partials.py` | Partials 2–8 at the strike, per register, several files against a reference |
| `grid_fit.py` | Picks a felt (scale, ε) per half-octave from a grid of sweeps |
| `partial_sigmas.py`, `top_decay.py` | Decay rates per band / per partial; how the 5–16 kHz band dies |
| `velocity_colour.py`, `vel_slope.py` | Brightness and loudness per velocity against a reference |
| `attack_map.py`, `nasal.py` | Octave/third-octave balance of the attack and the ringing note |
| `colour_fit.py`, `colour_by_vel.py` | The per-region colour curve (`data/model/board-colour.json` came from two passes of it) |
| `ltas.py` | Long-term third-octave spectrum of whole pieces against a reference |
| `align_ref.py` | Aligns a reference render of a piece to Forte's timeline (checks drift) |
| `excerpt.py`, `match_song.py` | Level-matched A/B excerpts (32-bit float WAV) |
| `midi_stats.py`, `midi_tracks.py` | Velocities, note lengths, pedal, controllers, doubled tracks |
