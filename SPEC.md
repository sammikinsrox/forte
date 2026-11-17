# Forte v1 specification

Forte is a free, open-source, physically modeled concert-grand piano plugin for Windows (VST3 + Standalone). It synthesises the sound by simulating the hammer, strings, dampers and soundboard. It does not play back recordings.

This document compiles every decision from the planning map ([Map: Forte v1 spec](issues/0001-map-forte-v1-spec.md)). Each section links to the ticket that holds the full reasoning. If this document and a ticket disagree, the **later ticket wins**. Please report any disagreement so this file can be fixed. Vocabulary follows [GLOSSARY.md](GLOSSARY.md).

Status: build-ready, 2026-11-14.

---

## 1. Product

- **Licence:** Forte's code is GPLv3-or-later. JUCE is used under AGPLv3 (combinable per GPLv3 §13). The VST3 SDK is MIT.
- **Platform:** Windows only. VST3 and Standalone. The test hosts are **FL Studio** and **Reaper**.
- **One piano model:** a modern concert grand (Steinway D values), built on a parameterised piano model so other pianos can be added later as new model files.
- **Physics in v1:**
  - felt-hammer/string interaction;
  - string stiffness (inharmonicity);
  - 1–3 coupled, detuned strings per note (two-stage decay);
  - phantom partials;
  - soundboard and body;
  - dampers with sustain pedal, including half-pedaling;
  - sympathetic resonance;
  - soft pedal.
- **Performance target:** a full 88-key sustain-pedal glissando plays without dropouts at a 128-sample buffer, using **≤ 25% of one core** on a mid-range 2020 PC (reference machine: the user's Ryzen 5 5500) at 48 kHz.
- **Judging the sound:** the reference recordings are the objective comparison. The user's ear makes the final call. Since 2026-11-16 the voicing target is the user's own **Keyscape** renders (§3.1), not Iowa: the user hears Keyscape as "clear, smooth, rich and deep" and judges Forte against it ([Voicing against Iowa](issues/0021-voicing-against-iowa.md) §14–15). Forte is for the user's personal use, so numbers fitted from those renders are committed.
- **Out of scope for v1:**
  - macOS, AU and Linux;
  - built-in reverb;
  - sostenuto pedal;
  - duplex scaling;
  - key and pedal mechanical noises;
  - lid and microphone position controls;
  - other piano models;
  - historical temperaments, MTS-ESP and MIDI Tuning Standard;
  - release velocity;
  - MIDI program change;
  - transpose and fine-tune.

## 2. Synthesis engine

Sources: [Choose the synthesis architecture](issues/0007-choose-synthesis-architecture.md), [Single-note listening test](issues/0009-single-note-listening-test.md), research in [String & hammer modeling](issues/0002-string-hammer-modeling-approaches.md) and [Soundboard, coupling, dampers](issues/0003-soundboard-coupling-damper-models.md).

### 2.1 Signal path

```
MIDI → Hammer (felt spring, 4× contact, soft-pedal shift)
     → Note's Unison as modal resonators (stretch, coupled two-stage decay,
       phantom partials, damper position + gap)
     → one-way bridge force (+ the knock, past the note's high-pass)
     → [sympathetic bank (12/key, damper-gated)] → bridge region bus ×4
     → per-region stereo convolution (active perspective; crossfade on switch)
     → Brightness tilt per region → sum → Tape → Room → Width → Volume → stereo out
```

Energy flows **one way**: strings → bridge regions and the sympathetic bank, with nothing flowing back. The soundboard's energy drain is already included in the measured decay times.

### 2.2 Notes, not voices

- **88 permanent note models.** There are no voices and no voice stealing. A note goes idle once it has decayed and its damper is down. Individual modes stop computing once they fall below −100 dB.
- **Every note-on is a restrike** of that note's current string state.
- **Sample rate:** the engine runs at the host rate (44.1–96 kHz) with no internal resampling. This is revisited only if 96 kHz misses its CPU gate (§9.2).

### 2.3 Strings

- **Modal strings:** one resonator per partial, up to 20 kHz or just under the sample rate's limit, whichever is lower.
- **Unisons:** real Steinway D string counts (about 10 single, 18 double, the rest triple).
- **Coupling:** Weinreich coupled modes (two-stage decay) for partials 1–32. Above 32, the strings are detuned and uncoupled.
- **Unison tuning offsets:** fixed per string and the same in cents on every partial.
  - 3 strings: −1, +0.2 and +0.85 × spread.
  - 2 strings: ±0.5 × spread.
  - The default spread is 0.7 cent (§5), scaled per note: × 1 up to C4, narrowing to × 0.35 at E5 and × 0.15 from E6. With the full spread the treble's upper partials beat into a sustained glassy ring that Keyscape's don't have, and the decay fit matched Keyscape's treble 2–3× worse ([Voicing against Iowa](issues/0021-voicing-against-iowa.md) §15). The decay fit models the same per-note spread.
- **Phantom partials:** on the lowest ~30 notes. Their path runs **continuously, including during hammer contact**, with a ~40 Hz high-pass on the longitudinal force.
- **Bridge output** is the string slope **at the bridge end**, so its sign alternates per mode. (Taking it at the agraffe end caused the "wooden knock".)
- **Per-note high-pass at f0/2** on notes above 100 Hz removes the sub-fundamental strike transient.

### 2.4 Hammer

- **Model:** a lumped mass with a nonlinear, hysteretic felt: Stulov's hereditary model, F = Q0·[c^p − (ε/τ)·∫ c^p(ξ)·e^{−(t−ξ)/τ} dξ]. Compression changes faster than τ meet the instantaneous stiffness Q0, slower ones the relaxed Q0·(1 − ε). With a short τ (0.02 ms) only the fastest compression meets the stiff felt. A soft relaxed felt then keeps the 1–3.5 kHz layer down (no twang), while the stiff onset adds air above 4 kHz. A τ comparable to the contact (~1 ms) reshapes the whole pulse instead and brings the twang back. τ is 0.02 ms everywhere; the relaxed stiffness and ε are voiced per register against Keyscape (§3.3): ε 0.8–0.9 up to C6, falling to 0 (an elastic felt) from F#6, where the stiff onset made a metallic zing ([Voicing against Iowa](issues/0021-voicing-against-iowa.md) §7, §14).
- **Contact speed:** the felt meets the strings at mf × (v / mf)^0.5 rather than at the blow's own speed v, and the strike's drive is scaled by v over that, so a note keeps the real blow's loudness while its brightness changes half as much with velocity. A physical felt went from 9–14 dB too dark at velocity 32 to 2–7 dB too bright at 127 against Keyscape (§15). Per note, `contactSpeedExponent` (1 = physical).
- **Knock:** while the hammer touches the strings, a share of its force goes straight to the bridge bus through a band-pass (two one-pole high-passes at 300 Hz, two one-pole low-passes at 2 kHz), never through the note's high-pass (§2.3): the impact's woody mid-band, not a thump. Keyscape's treble strike noise sits at 0.25-2 kHz; a 150 Hz low-pass put 8-24 dB too much below 250 Hz (issue 0022 §6). Its level per register: none up to C4, rising to 0.14 at F#5 and 0.45 at F#6 (from Keyscape's attack crest factor), then 0.17 at D7 (its 0.5-2 kHz strike noise).
- **Contact:** computed sample by sample at **4× oversampling** during the contact window only.
- **Strike position:** ~1/8 of the string length. It shapes which partials are excited.
- **Per-note values:** mass, stiffness Q0, exponent p, relaxation ε and relaxation time τ come from the model file.
- **Stability test:** every note at maximum velocity, with the hardest felt, at every supported sample rate.

### 2.5 Dampers and half-pedaling

- **Per-partial damping** driven by a 0–1 damper position, plus a gap model for half-pedaling.
- **Key-up** ramps the dampers down over ~20 ms.
- **Undamped notes:** roughly the top 18 notes have no damper. The exact boundary comes from the Steinway D data.

### 2.6 Sympathetic resonance

- **Bank:** a separate set of **12 modes per key**, fed from the bridge and gated by that key's damper.
- **Routing:** the sympathetic bank feeds the soundboard in its own bridge region. Sympathetic strings never feed each other or the played strings.

### 2.7 Soft pedal

A continuous 0–1 hammer shift that changes which strings are struck and the felt hardness. It is read **at each strike**, so it doesn't affect notes already ringing.

### 2.8 Soundboard and output

- **Four bridge regions:** bass, tenor, middle and treble. Each is convolved with its own stereo IR (20k taps, ~0.42 s at 48 kHz).
- **Perspective:** only the active Listening perspective's IR set runs. Switching perspective crossfades between the IR sets.
- **Stereo:** placement comes from the regions, with no panning. Bass is on the left, in both perspectives.
- **Brightness:** a treble tilt on each region's output.
- **Tape:** after the regions are summed, a tape-style saturator, as in the default patch of the user's Keyscape: (tanh(d·x + b) − tanh b) / (d·(1 − tanh² b)) with b = 0.15 (even harmonics as well as odd), unity gain for small signals. The drive d is 8, chosen by ear (§15); 0 = none. Computed in the algebraically equal form tanh(d·x) / (d·(1 + tanh b · tanh(d·x))) so tiny signals stay exact in float.
- **Direct path (issue 0021 §19):** each region's IR is the board's resonant modes plus a smooth direct path. Above the low mids the direct path is small (0.1), so the board, not a pickup, carries the sound; a +30 dB minimum-phase shelf at 400 Hz keeps it for the bass, where a board's lowest modes radiate as one piston anyway. The modes' density is constant above 1.1 kHz, as a plate's is, so they overlap in the treble.
- **Room, Width, then Volume:** applied after the tape (§2.11).

### 2.9 Live parameter changes

| Kind | Controls | Behaviour |
|---|---|---|
| Strike-time | Hardness, Touch, soft-pedal felt | Next strike only |
| Tuning | A4, Stretch, Detune | Ringing modes and sympathetic strings glide over ~50 ms; each note takes its exact new modes the next time it starts from rest |
| Unison | Spread | A note's next start from rest (it changes the unison's coupled modes, which can't glide) |
| Output | Volume, Width, Brightness, Resonance | Immediate, smoothed over ~20 ms |
| Perspective | Listening perspective | Crossfade between IR sets |
| Voicing lab (§6.5) | Hammer, string and output voicing | Strike-time ones on the next strike; string ones re-prepare a note when it next starts from rest; output ones immediate |

Ringing notes are never cut off by a parameter change, a preset load or automation.

### 2.10 Measured cost

The prototype benchmark ([Modal resonator CPU benchmark](issues/0011-modal-resonator-cpu-benchmark.md)) measured the core parts at ~4% average / 6% p99 of one core with AVX2, and 8% / 12% with SSE2, at 48 kHz. Allowing 2–3× for the rest of the real engine, the expected total is ~15–25%.

### 2.11 Room

The room the piano stands in, after the tape and before Width and Volume (issue 0021 §19). The soundboard is the instrument; the room is where it's heard.

- **Pre-delay, then early reflections:** ten taps per side from both channels (odd taps cross over, widening the image), spaced by Size, through a one-pole low-pass set by Damping, at the Early level.
- **Late tail:** the pre-delayed input and the early reflections, diffused by four series all-passes per side, into an 8-line feedback delay network with a Hadamard mix. Each line's gain gives the Decay time (RT60 at low frequencies) for its length; a one-pole low-pass in each loop shortens the highs by Damping (Jot). Each line's length wanders slowly (±0.25 ms, 0.1–0.9 Hz), so no mode rings on into a metallic tone. Left and right are two orthogonal sums of the lines: decorrelated.
- **Level:** the wet sound is added to the dry at a gain that rises as the square of Room (1.5 at 100), so its low half is fine-grained; the tail's level is kept near even as Decay and Size move. **Room 0 is the dry piano, bit for bit.**
- **The engine alone is dry** (its default Room is 0, the piano as committed, which `forte-render` renders); the plugin's parameters default to the **Studio** room.
- **Determinism and cost:** every sample depends only on earlier samples and a parameter clock counted from reset, so output doesn't depend on block splits (§7.4); the controls glide over ~50 ms on that clock. Asleep (no work) at Room 0 and once its input has been silent longer than its tail. Its tail joins the engine's reported tail. Under a point of one core at 48 kHz.

## 3. Piano model data and fitting

Sources: [Parameter fitting pipeline](issues/0014-parameter-fitting-pipeline.md), [Reference recordings](issues/0005-reference-recordings-and-measurements.md), [Soundboard impulse response source](issues/0012-soundboard-impulse-response-source.md).

### 3.1 References

| Source | Used for | Licence |
|---|---|---|
| Iowa MIS Steinway B | Per-note measurements at pp/mf/ff | Free use |
| Salamander Grand (Yamaha C5) | Velocity-layer shape, release samples | CC BY 3.0 |
| Piano in 162 | Pedal-down sympathetic level | Measured locally, never shipped |
| Plath concert-grand dataset | Soundboard and Pianist calibration | CC BY 4.0 |
| Published Steinway D tables (Chabassier & Duruflé, Chaigne & Askenfelt, …) | Seed values | Cited |
| The user's Keyscape renders (dry, its default patch with tape saturation): note sweeps from `tools/fitting/make_note_sweep.py` (3 s and 11 s holds, velocities 32/64/96/127) and whole pieces | **The voicing target** since 2026-11-16: inharmonicity, decay, felt voicing, knock, colour, velocity response | The user's own renders of a commercial library, for personal use. Derived numbers are committed (`data/reference-measurements/keyscape-decay.json`, `data/model/board-colour.json`); the audio stays in the user's Downloads |

**No reference audio ever ships.** Only derived numbers ship. `tools/fetch-references` downloads the recordings into the git-ignored `refs/`. Where Iowa and Keyscape disagree, Keyscape wins.

### 3.2 Pipeline

Offline Python (numpy, scipy, soundfile) in `tools/fitting/`, in three steps, each with committed output:

1. **measure:** recordings → `data/reference-measurements/`.
2. **fit:** seeds + measurements → model parameters.
3. **build:** parameters + **voicing overrides** (a hand-edited file of per-note brightness, level and decay offsets) → the shipped model file and IRs.

There is one command per step, plus `all`. Fixed seeds make every run's output byte-identical. The plugin never fits anything.

### 3.3 What is fitted

- **Inharmonicity B:** measured on the Keyscape sweep and smoothed (`PianoModel.cpp`; issue 0021 §14). About half the old seed curve from C#1 to E2, 1.2–1.8× it from the tenor up. (The plan was the Steinway D string scale, with Iowa checking the trend.)
- **f0** is not fitted. The tuner computes it (§5).
- **Decay**, from the long Keyscape sweep (`fit_decay.py measure-sweep <wav> --hold 11 --gap 2`, then `fit`); Iowa mf when no sweep of at least 6 s holds is measured:
  - Partials at least 10 dB above the noise floor keep their own measured values: a prompt-sound and an aftersound rate each, fitted through the engine's coupled unison (with its per-note spread, §2.3) to the partial's level over windows from 0.05 s to the end of the hold. The targets are medians over ±3 semitones and the sweep's velocities 64 and 96 ([Voicing against Iowa](issues/0021-voicing-against-iowa.md) §10, §15).
  - The fit weights 0.25–2.4 s fully, the strike's first dip (to 0.25 s) at 0.4 and the tail past 2.4 s at 0.3, and the prompt rate is at most 3× the aftersound. Weighted evenly, the fit chased Keyscape's first dip with prompt rates the fixed spread can't follow with an aftersound, which killed the treble fundamentals and left their partials ringing (§15).
  - Higher partials follow σ = b1 + b3·f², with the coefficients smoothed across keys and outliers rejected.
  - Notes outside the reference's range borrow the rates at the same frequency from the nearest measured note.
  - A noisy tail is never trusted over the law.
- **Unison coupling** per partial: derived from the fast and slow decay and the fixed spread.
- **Phantom levels:** fitted against Iowa ff.
- **Hammer:**
  - **Mass:** a smooth curve from the seeds, not fitted.
  - **Felt stiffness and exponent:** fitted per note across pp, mf and ff together. For each dynamic, the hammer speed that matches the loudness is solved first, then the felt is fitted to match brightness at all three dynamics. Salamander checks the curve shape between those points.
  - **Hand-off:** the three matched speeds per note become the velocity calibration points (§4).
  - **For now, voiced per register.** A first fit along these lines (`tools/fitting/fit_hammer.py fit`) matched Iowa's attack spectra but sounded worse: more twang over whole notes, and a click at each strike ([Voicing against Iowa](issues/0021-voicing-against-iowa.md) §7). The felt is now `fit_hammer.py voice` with `VOICING`: (key, relaxed-stiffness scale × the seed felt, ε) anchors, chosen from a 3 × 3 grid of felts by the strike's partials 2–6 against the Keyscape sweep at velocities 64–127 (§14). The speeds are the seed's.
  - **Contact speed exponent** 0.5 on every note (§2.4), from the Keyscape sweep's brightness per velocity (§15).
- **Knock:** level per register from the Keyscape sweep's attack crest factor (§2.4, issue 0021 §14).
- **Soft-pedal felt:** a literature offset from the normal felt, then set by ear.
- **Dampers:**
  - damper-down law from Salamander's releases;
  - half-pedal gap model seeded from Lehtonen et al. and Stulov et al., then set by ear;
  - undamped region from the D data.
- **Sympathetic bank:** frequencies and decays come from the fitted strings. Its 100% level is calibrated against the pedal-down minus pedal-up difference in Piano in 162.

### 3.4 Soundboard IRs

- **One generator, two colours.** Synthesised "modal noise": low modes placed from published concert-grand data, the rest statistical. Its shared low modes and per-band decays come from Plath's bridge accelerometer.
- **Microphones colour:** render-and-compare against Iowa per third-octave band per region, with Salamander as the cross-check.
- **Pianist colour:** Plath's bridge-to-dummy-head transfer, pooled per region, with detail above ~2 kHz smoothed out.
- **Irregularity:** the mix of resonant modes against a smooth direct path. It's fitted to match the reference's partial-level scatter. It is not a user control.
- **Now (seed IRs, until the calibration above exists):** the generator's board settings are set by ear (issue 0021 §8, §11): a +12 dB low shelf at 250 Hz and gain 0.5 on the direct path, modes rolled off from 12 kHz, +8 dB/octave on the modes above 1 kHz. On top, a **colour curve per region** (`data/model/board-colour.json`, third-octave gains): the single notes' whole-note spectra (velocities 64 and 96) of the Keyscape sweep against Forte's, noise-masked, smoothed, level-neutral and capped, applied in two passes, plus +4.5 dB above 1 kHz by ear (issue 0021 §14–15). The seed board is the likeliest home of Forte's remaining "CP-70" character: 0.42 s of random modes and a pickup-like direct path.

### 3.5 What ships

- **Model file:** `data/model/concert-grand.json`, with a schema version. It holds:
  - per-note B, decay laws and measured partials;
  - coupling and phantom levels;
  - hammer and damper values;
  - the sympathetic level;
  - the IR generator parameters.
- **IRs:** 32-bit float WAV at 48 kHz, 4 regions × 2 perspectives, stereo, ~3 MB. They're resampled once at load for other host rates.
- **Packaging:** both are **embedded in the plugin binary** (§9.4).

## 4. MIDI, touch and pedals

Source: [Touch and pedal mapping](issues/0015-touch-and-pedal-mapping.md).

### 4.1 Velocity and Touch

- **Velocity sets hammer speed only.** Loudness and brightness emerge from the physics.
- **Per-note curve:** each note's curve passes through its fitted pp, mf and ff hammer speeds at **velocities 32, 64 and 112**.
  - It always rises and is smooth on a log-hammer-speed scale.
  - Below pp it extends to velocity 1, about 12 dB under pp.
  - Above ff it extends to velocity 127, about 4–5 dB over ff, capped where the hammer stability test still passes.
  - Velocity is used at full host resolution, not rounded to 7 bits.
- **Touch** (0–100, default 50 = calibrated):
  - It bends normalised velocity before the per-note curve: lower is lighter, higher is heavier.
  - Velocity 1 and 127 always stay the softest and hardest blows, and the curve can never fold back on itself.
  - There is no fixed-velocity mode.

### 4.2 Pedals

- **Sustain (CC64):** always read as continuous.
  - **Dead zones:** 0–12 = fully down (dampers resting); 116–127 = fully lifted. Values in between drive the gap model.
  - **Smoothing:** damper position is smoothed over ~20 ms.
- **Soft (CC67):** a continuous 0–1 shift with the same dead zones, read at each strike.
- **Pedal invert:** a separate setting for each pedal, for reversed pedals (§6.4).
- **On-screen pedals** share one value per pedal with MIDI, and the latest message wins.

### 4.3 Notes and other MIDI

- **Overlapping note-ons are counted.** A key stays down until every note-on has had its note-off.
- **Range:** notes outside A0–C8 (MIDI 21–108) are ignored silently.
- **Channels:** omni.
- **Timing:** sample-accurate. Every event is applied at its exact offset in the block.
- **CC123** (All Notes Off) releases every key, so the sustain pedal still holds notes.
- **CC120** (All Sound Off) silences everything immediately.
- **CC121** (Reset All Controllers) sets both pedals up.
- **Ignored:** pitch bend, mod wheel, aftertouch, sostenuto (CC66) and release velocity.

## 5. Tuning

Source: [Tuning system](issues/0016-tuning-system.md).

- **A simulated tuner** sets every note's average string pitch from B, A4 and Stretch (`engine/src/Tuner.cpp`).
  - It starts at A4 and sets the temperament octave A3–A4 in equal temperament: a beatless 4:2 octave divided into 12 equal steps. (With A3 at an exact 2:1, each octave copy of the temperament carried a 1–2 cent sawtooth.)
  - Then it tunes outward one note at a time, each against the note an octave inside.
  - **About C3–C6:** 4:2 octaves.
  - **Below about C3:** 6:3.
  - **Above about C6:** 2:1.
  - Across an octave centred on C3 and on C6 it moves gradually from one pair of partials to the next. A switch at one note left a 2.5-cent step at C6.
  - With the Keyscape-fitted B, the calculated curve runs from −8.6 cents at A0 through −1.3 at A3 and +4.6 at C6 to +30 at C8.
- **When it runs:** whenever A4, Stretch or Detune changes. It's 88 notes of arithmetic with no allocation, so it runs on the audio thread at the change's render position, which keeps offline bounces deterministic. Ringing modes glide to the new pitches over ~50 ms; each note is re-prepared at its exact new pitch the next time it starts from rest.
- **Stretch 0 at A4 = 440** is the model's own equal temperament exactly, as Forte was tuned before the tuner existed.
- **Stretch:** 0–100, default 50. 0 = exact 2:1 octaves, 50 = the calculated curve, 100 = double. It is a smooth multiplier on each note's distance from equal temperament and doesn't switch which partials the tuner matches.
- **A4:** 415–466 Hz, in whole Hz, default 440.
- **Unison spread:** a per-note curve (flat 0.7 cent by default) × one overall amount (the Spread control). Individual notes can be adjusted in the voicing overrides.
- **Stretched equal temperament only.** No other temperaments.
- **Detune:** 0–100, default 0 (as tuned). From a piano a few weeks after its tuning to one nobody has tuned in years: every note but A4 sits off its tuned pitch by its own fixed amount (`detuneShape`, a hash of the note, even over ±1), the same in every session. The range is linear up to ±3 cents at 50, then doubles every 13.5: ±6 at 63.5, ±12 at 77, ±39 at 100 (the user wanted the extremes to get "whacky"). It moves the whole note, so its unison spread is untouched. Added on the user's ear (issue 0022 §6): the tuning "so perfect that it's uncanny".

## 6. Editor

Sources: [Editor look & feel direction](issues/0006-editor-look-and-feel.md), [Editor rendering approach](issues/0010-editor-rendering-approach.md), [Listening perspective](issues/0013-listening-perspective.md), [Editor control set](issues/0017-editor-control-set.md). The visual reference is `prototypes/editor-look-and-feel/index.html?variant=D`. Control names and groups follow ticket 17, which overrides the prototype's labels.

### 6.1 Technology

- **Native JUCE drawing**, with no WebView: a custom `LookAndFeel` (rotary dials, faders, pill and segment buttons).
- **Resizing:** a scaled content component keeps a fixed aspect ratio.
- **Typed values:** editable integer labels.
- **Fonts:** Jost and Italiana are embedded unmodified as binary data. Jost is a variable font, so the 400/500 weights are cloned explicitly, and `withPointHeight` matches the CSS sizes.
- **Visualisations:** read lock-free atomics that the audio thread publishes. They repaint at ~30 Hz, and only when the data changes.

### 6.2 Look

- **Fallboard design:**
  - FORTE wordmark set in Italiana; everything else in Jost.
  - A red felt strip (#9E1B32) above a full-width, playable 88-key keyboard.
- **Light and Dark themes:**
  - **Light:** white lacquer with ebony knobs.
  - **Dark:** ebony lacquer with ivory knobs.
  - The felt strip and the keyboard are the same in both.
- **The advanced view** uses the same visual language and adds:
  - a live overtone display of the last note played (stiff-string partials against perfect harmonics, decaying in real time);
  - the stretch-tuning curve across all 88 notes;
  - keys lit by how strongly they ring.

### 6.3 Controls

**Value rules:**
- Every sound control runs 0–100, and **50 = the real piano**.
- Values are whole numbers and can be typed: Enter commits, ↑/↓ steps by 1, Esc cancels.
- **Double-click resets** a control to its default. Pedal controls are the exception.

| Control | Range | Default | Meaning | Views |
|---|---|---|---|---|
| Hardness | 0–100 | 50 = fitted felt | Log-scale multiplier on the felt stiffness. Span set by ear. Strike-time. | simple dial, advanced knob |
| Brightness | 0–100 | 50 = no tilt | Treble tilt, about ±12 dB at 10 kHz (exact by ear) | both |
| Resonance | 0–100 | 50 = the voiced level | Sympathetic bank level. 0 = none to 50 linear, then doubling every 12.5: ×2 at 62.5, ×4 at 75, ×16 at 100, where it swamps the note. 50 is twice the old calibration (0.2): at 0.1 the bank was −16.5 dB of a pedalled piece and barely audible from 0 to 100; doubled, with longer sustain below A6, "fixes almost all the issues" (issue 0022 §6). | both |
| Volume | −60 to +6 dB | 0 dB | −60 shows as "Off" | simple fader, advanced knob |
| Listening perspective | Microphones \| Pianist | Microphones | Selects the IR set | both headers |
| Sustain | 0–100 | live | Raw pedal position (0 = Up, 100 = Down). Up / Half / Down slider. | both |
| Soft | toggle | off | Shows on past halfway. A click sends fully down or fully up. | both |
| Touch | 0–100 | 50 | §4.1 | advanced |
| A4 | 415–466 Hz | 440 | §5 | advanced |
| Stretch | 0–100 | 50 | §5 | advanced |
| Spread | 0–100 | 50 = 0.7 cent | 0 = perfect unison, 100 = 1.4 cent | advanced |
| Detune | 0–100 | 0 = as tuned | each note off by its own fixed amount: up to ±3 cents at 50, ±39 at 100 (§5) | advanced |
| Width | 0–100 | 100 | 0 = mono | advanced |
| Room | 0–100 | 22 (Studio) | How much room (§2.11). 0 = the dry piano. | advanced |
| Room size | 0–100 | 30 | The reflections' spacing and the tail's density | advanced |
| Room decay | 0.2–10 s (in 0.1 s) | 0.8 s | The tail's RT60 at low frequencies | advanced |
| Room damping | 0–100 | 55 | How much sooner the highs die (0 = as long as the lows) | advanced |
| Room pre-delay | 0–100 ms | 4 ms | Before the first reflection | advanced |
| Room early | 0–100 | 60 | The early reflections' level (50 = their own) | advanced |

### 6.4 Layout

- **Three views**, switched by Simple | Advanced | Lab tabs in the header: the simple view, the advanced view and the voicing lab (§6.5). The open view is saved with the project.
- **Simple view:**
  - Hardness, Brightness and Resonance dials;
  - Volume fader;
  - Microphones | Pianist toggle under the preset name;
  - Soft toggle and Sustain slider;
  - Advanced button, theme toggle and Settings gear;
  - preset name with ‹ › arrows;
  - the keyboard.
- **Advanced view groups:**
  - **Hammer:** Hardness · Touch
  - **Tuning:** A4 · Stretch · Detune
  - **Strings:** Spread · Resonance
  - **Output:** Brightness · Width · Volume
  - **Pedals:** Sustain · Soft
  - **Room** (under the tuning curve): Room · Size · Decay · Damping · Pre-delay · Early, and the room presets' menu (Dry, Studio, Chamber, Recital Hall, Concert Hall, Cathedral; "Custom" when the knobs match none). A room preset sets only the six room controls.
- **Header:**
  - **Settings gear** (both views): *Invert sustain pedal*, *Invert soft pedal*, and an **About** panel with credits and licence notices (§9.6).
  - **Advanced view meters:** **Notes** (notes currently sounding) and **CPU** (% of one core).
- **Not in v1:** a soft-pedal amount control and a separate half-pedal display.

### 6.5 Voicing lab

The user voices the model by ear from inside the host, to find what the measurements can't (issue 0021 §15: a "tinny", "CP-70" quality in the high notes that no comparison with Keyscape pins down). Forte is for personal use, so the lab is part of the plugin, not a developer build.

- **Form:** plugin parameters in a **Lab** group, automatable and saved with the host project. They follow §6.3's value rules: 0–100, **50 = the committed voicing**, double-click resets. The lab is a permanent part of Forte: the editor's third view, **Lab** (§6.4).
- **The Lab view:**
  - one knob per control, in Hammer, Strings, Board and Output panels, each saying when its changes are heard;
  - a register selector, **Bass · Middle · Treble · All**: the knobs show and move that register (All moves the three together, by the same amount);
  - each knob shows its value in physical terms (×1.32 ring, ε 0.80, +3.0 dB, …), its three registers as small bars (click one to select that register), and a dot when any register has moved;
  - the control under the mouse is drawn across the whole keyboard, committed (dashed) and now, with a handle at each register's anchor (C2, C4, C7) to drag;
  - an info panel: what the control does, where each register stands, Reset control and Reset lab; each panel has its own Reset;
  - **Lab | Committed:** an A/B by ear. Committed plays every control at 50 (the committed voicing) while the lab keeps its values. It isn't saved.
  - **Export voicing** as a button, with the result shown;
  - the keyboard, to audition.
- **Registers:** a per-register control has three values, **Bass / Middle / Treble**, anchored at C2, C4 and C7 and interpolated across the keys (log for multipliers, linear otherwise), held flat outside.
- **Controls:**

| Group | Control | Per register | 0 … 50 … 100 |
|---|---|---|---|
| Hammer | Felt hardness | yes | relaxed stiffness ÷4 … fitted … ×4 (log) |
| Hammer | Felt spike (ε, the stiff onset) | yes | none … fitted … 0.95 |
| Hammer | Velocity brightness (contact speed exponent) | — | 0 (brightness fixed at mf) … 0.5 … 1 (physical) |
| Hammer | Strike position | yes | ×0.6 … fitted … ×1.4 |
| Hammer | Hammer mass | yes | ×0.5 … fitted … ×2 (log) |
| Hammer | Knock level | yes | none … fitted … ×3 |
| Strings | Inharmonicity | yes | ×0.5 … fitted … ×2 (log) |
| Strings | Unison spread scale | yes | ×0 … fitted (§2.3's curve) … ×2, on top of Spread (§6.3) |
| Strings | Sustain (every rate) | yes | rates ×2 (shorter) … fitted … ×0.5 (longer), log |
| Strings | Overtone decay (partial n's rates × (f_n / f_1)^k) | yes | k = −0.5 (upper partials ring longer) … 0 … +0.5 (die faster) |
| Strings | Second stage (aftersound rate) | yes | ×2 … fitted … ×0.5, log |
| Strings | Phantom level | — | none … fitted … ×2 |
| Board | Pickup (direct-path gain) | yes | 0 … 0.1 (fitted) … 2 |
| Board | Board tilt above 1 kHz | — | −4 … +6 (fitted) … +16 dB/octave |
| Board | Colour Low / Mid / High (shelves at 250 Hz and 2.5 kHz over the fitted curve) | yes | −12 … 0 … +12 dB |
| Output | Tape drive | — | 0 (off) … 8 … 24 |

- **Live changes (§2.9):** hammer controls act on the next strike. String controls need a note's modes recomputed: a note is re-prepared, off the audio thread, the next time it starts from rest (not while it rings, so nothing clicks); the lab shows nothing until it's struck again. Board controls rebuild the four IRs from their kept parts (resonant modes, direct path, colour curve), off the audio thread, then crossfade to them as a perspective switch does (§2.8). Output controls are immediate.
- **Export:** an **Export voicing** control (a momentary 0 → 1) writes the current lab values to `Documents/Forte/voicing.json`. That file is §3.2's voicing overrides: `tools/fitting/fold_voicing.py` folds it into the committed defaults (a per-note overrides table on top of the fitted hammer and decay tables, the tape drive, the board's settings and colour), and every lab control returns to 50 on the new voicing.
- **Voicing revision:** each fold bumps it. An export records the revision it was made on, and the fold refuses one from another revision. A project saved on an older revision opens with the lab at its 50s, since its lab values moved a voicing that's now committed.
- **Costs:** the lab must not change the sound at all its 50s (bit-identical renders) and stays within §1's CPU target with every control at its extreme.

## 7. Presets, saved state and automation

Source: [Presets, saved state and automation](issues/0018-presets-state-automation.md), [ADR 0001](docs/adr/0001-projects-follow-latest-model.md).

| Control | Preset | Automatable | Project state |
|---|---|---|---|
| Hardness, Brightness, Resonance, Touch, Stretch, Spread, Detune, Width | ✓ | ✓ | ✓ |
| Listening perspective | ✓ | ✓ (two-state) | ✓ |
| Room, Room size, Room decay, Room damping, Room pre-delay, Room early | ✓ | ✓ | ✓ |
| Volume, A4 | — | ✓ | ✓ |
| Sustain, Soft | — | — (MIDI only) | — (they start released) |
| Pedal inverts, theme | — | — | — (per computer, `%APPDATA%\Forte\settings.json`) |

### 7.1 Presets

- **Contents:** the 8 character controls, the room's 6 (§2.11) and Detune (§5). A file without the room loads the Studio room; one without Detune loads it at 0.
- **Factory presets:** about 5–8, built in and read-only. **"Concert Grand"** (all defaults, the Studio room) comes first. Then Pianist's Seat (Chamber), Mellow (Recital Hall), Bright, Intimate (Dry), Honest Equal and Concert Hall (Concert Hall). The final list is set by ear.
- **Room presets:** Dry, Studio, Chamber, Recital Hall, Concert Hall, Cathedral: the room's 6 values only, from the Room panel's menu (§6.4). First-build values, set by ear.
- **User preset files:** `Documents\Forte\Presets\<name>.fortepreset`, as JSON. Each holds a format version, the model id, the name and the 14 whole-number values (Room decay in tenths of a second). A file without the room's values (written before the room) loads them as the defaults.
- **Arrows:** ‹ › step through the factory presets, then the user presets alphabetically, and wrap at the ends.
- **Changed indicator:** `*` marks unsaved changes. Arrowing away discards them silently.
- **Name menu:** Factory and User lists, Save, Save as…, Rename, Delete and Open presets folder.

### 7.2 Automation

- The 10 sound controls are automatable.
- **Parameter IDs are permanent.**
- The host shows the editor's own text.

### 7.3 Saved state

- **The project saves:** the 10 values, the preset name and its `*` flag, the open view and a state-format version.
- **Model updates:** projects follow the latest piano model, and release notes flag sound changes.
- **Compatibility:** a newer Forte opens older states. An older Forte ignores fields it doesn't know.

### 7.4 Engine edge cases

- **Sample-rate change:** the engine rebuilds while audio is stopped (recompute modes, resample the IRs, rerun the tuner). The target is well under 1 s.
- **Offline bounce:** renders bit-identically to real-time playback. All randomness uses fixed seeds.

## 8. Validation

Source: [Objective validation plan](issues/0019-objective-validation-plan.md).

### 8.1 Tier 1: engine checks

Run on every change and **never waivable**. They sit alongside the safety net in §9.3.

| Check | Pass condition |
|---|---|
| Velocity calibration | At Touch 50, velocities 32/64/112 reproduce the fitted pp/mf/ff loudness within ±1 dB |
| Velocity curve | Monotonic at every Touch value |
| Overlapping note-ons | Never cut a note short |
| MIDI timing | Onset lands on the event's exact sample |
| Determinism | Two renders of the same project are bit-identical |
| Live changes | A preset change or automation while notes ring causes no level drop and no click |
| State compatibility | The previous release's saved-state fixtures load with every value intact |

### 8.2 Tier 2: sound-match checks

Run nightly and before release. They measure real-engine renders against `data/reference-measurements/`. A failure can be **waived by the user's ear**, with a written waiver.

| Area | Gate |
|---|---|
| Inharmonicity | ±10% of the D scale. Bass at or below Iowa's. |
| Decay | ±15% of measured; no note more than 25% off its neighbours |
| Hammers | Spectral centroid ÷ f0 within ±10% of Iowa at pp/mf/ff |
| Microphones IR | ±2 dB per third-octave band vs Iowa, 100 Hz–10 kHz. Below 250 Hz, up to +3 dB is allowed. |
| Pianist IR | ±2 dB vs Plath dummy head, 100 Hz–5 kHz |
| Irregularity | Partial-level scatter ±2 dB |
| Stretch | Railsback shape, ±5 cents of Iowa's trend. Up to 5 cents less stretched in the outer octaves. |
| Knock | 60–250 Hz share of the first 30 ms at most 3 dB above the reference |
| Pops | Zero clicks on the Prelude, a fast repeated-note test and a slow pedal sweep |
| Half-pedal | Endpoints match free-ringing and damper-down; decay only lengthens as the pedal rises; high partials die faster at mid-pedal |

### 8.3 Tier 3: listening

- **Release sign-off:** a blind A/B page with the Prelude, plus C2, C4 and C6 at pp, mf and ff, each paired with its reference. The bar is "a convincing piano next to the reference".
- **Voicing pass:** all 88 notes at mf and pp, one clip per octave. It runs after the first full fit and after each refit that changes the sound. Fixes go into the voicing overrides.

### 8.4 Tooling

- **`forte-render`:** a command-line renderer (MIDI → WAV, 48 kHz default) built from the real engine.
- **`tools/validate/`:** the checks, in Python, reusing the measuring code in `tools/fitting/`.
- **Report:** each run writes an HTML report with pass/fail per gate, the numbers, waivers and the listening pages.

## 9. Build, CI and release

Source: [Build, installer and release](issues/0020-build-installer-release.md), research in [research/code-signing-and-vst-branding.md](research/code-signing-and-vst-branding.md).

### 9.1 Toolchain

- **Compiler:** MSVC (Visual Studio 2026), with `/O2 /fp:fast`. Flush-to-zero and denormals-are-zero are set inside the engine.
- **JUCE:** a git submodule pinned to **JUCE 9.0.5**. Upgrading it is a deliberate commit.
- **Build:** CMake. The targets are the VST3, the Standalone, `forte-render` and the tests. They all link **one shared engine library**.
- **Version:** one semantic version number in CMake. Preview builds for the user's experiments are `0.9.x` (from `0.9.0`, 2026-11-17); `1.0.0` is the finished v1, after the sign-off.

### 9.2 CPU paths and performance gate

- **One binary, two engine paths:** the hot loops are compiled for AVX2 and for SSE2, and a CPU check at startup picks one.
- **Manual release gate** on the Ryzen 5 5500. Each check is a full 88-key sustain glissando at a 128-sample buffer:
  - AVX2 at 48 kHz: **≤ 25%** of one core.
  - AVX2 at 96 kHz: **≤ 50%** of one core.
  - SSE2 at 48 kHz: **no dropouts** (CPU % is recorded, but not gated).
- **CI** only flags render-time regressions above 1.5×.

### 9.3 Safety net (GitHub Actions, Windows)

- **Every push and pull request:**
  - Debug and Release builds;
  - **Catch2** DSP unit tests;
  - audio-thread tests: a guard fails any allocation on the audio thread, and a build rule rejects `<mutex>` and the like in DSP sources;
  - **pluginval** at strictness 10;
  - the Tier 1 checks.
- **Nightly and on release tags:** the Tier 2 checks and the HTML report.

### 9.4 Hosting and installer

- **Hosting:** a public GitHub repo, with Releases and Actions.
- **Installer (Inno Setup):**
  - per-machine install, run as admin;
  - `Forte.vst3` → `C:\Program Files\Common Files\VST3\`;
  - `Forte.exe` → `C:\Program Files\Forte\`, with a Start-menu shortcut;
  - checkboxes for VST3 and Standalone, plus an uninstaller;
  - never touches `Documents\Forte\Presets`.
- **Portable zip** of the same two files.
- **Self-contained:** the model file and IRs are embedded in the binaries.

### 9.5 Release flow

1. Bump the version in `CMakeLists.txt` and commit it.
2. Either push a tag `vX.Y.Z` (`.github/workflows/release.yml`) or run `tools/release/make-release.ps1` on the user's PC. Both build Release, run every test with pluginval, package the installer and the zip (`tools/release/package.ps1`, `installer/forte.iss`) and create a **draft** GitHub Release, visible only to the owner. Tier 2 and its report join this step once they exist.
3. The user installs the draft's build, and for 1.0.0 and later runs the blind A/B sign-off and the performance gate.
4. The user publishes (previews `0.9.x` as pre-releases).

### 9.6 Release contents, notices and branding

- **Files:**
  - `LICENSE` (GPLv3) and the AGPLv3 text, with a link to the matching source tag;
  - `CREDITS.md`: the references the voicing was measured from (Iowa MIS, the user's Keyscape renders; Salamander, Piano in 162 and Plath once used) and the papers;
  - `THIRD-PARTY.md`: JUCE, the VST3 SDK (MIT), Jost and Italiana (OFL, embedded unmodified), and every JUCE-bundled library actually in the binary, checked against the real build (0.9.0: FLAC, zlib, libpng, IJG JPEG, libwebp, HarfBuzz, SheenBidi, LunaSVG, PlutoVG); the package carries each one's licence text in `licenses/`.
- **About panel:** the same credits and notices.
- **Records:**
  - the validation report is attached to each release;
  - waivers are committed as `docs/validation/<version>-waivers.md`;
  - each release commits its saved-state files as fixtures for the next release's compatibility check.
- **VST branding** (Steinberg's usage guidelines, a release-checklist item):
  - the VST Compatible logo next to any "VST3" claim (README, Releases page, About panel);
  - ® on the first use in each document;
  - the line "VST is a registered trademark of Steinberg Media Technologies GmbH.";
  - no "VSTi" or other variants, and "VST" never in the product name.
- **Code signing:** v1.0 ships **unsigned**, with a SmartScreen note on the download page.
  - Known gap: Smart App Control, where enforcing, blocks unsigned code.
  - Once Forte has users, apply to SignPath Foundation. If refused: Microsoft Artifact Signing (US or Canada residents only) or Certum's open-source certificate.

## 10. Settled in the first build

These are specified, but their final values are explicitly left to the first real build, by ear or by measurement:

- **Set by ear:**
  - the span of the Hardness control (first build: the felt stiffness ÷4 to ×4, as the lab's Felt hardness);
  - Brightness's exact dB range;
  - soft-pedal felt;
  - the half-pedal gap model settings;
  - the final factory preset list and values.
- **Measured:**
  - real-engine CPU against §9.2, and whether 96 kHz needs internal resampling;
  - sample-rate rebuild time (target well under 1 s).
- **Checked in the editor:**
  - whether the five advanced groups fit the side column;
  - typed values (host keystroke capture) and resizing in **FL Studio**, which the editor prototype wasn't tested in.
- **From data:**
  - the exact undamped-region boundary;
  - availability of the Steinway D string tables (the fallback is in §3.3).
