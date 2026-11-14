# Reference recordings and what we can measure from them

Research for ticket [0005](../issues/0005-reference-recordings-and-measurements.md). Researched 2026-11-14.

**Question:** Which freely licensed recordings of real grand pianos can be Forte's objective reference, what can we reliably measure from them, and what published parameter tables can seed the physical model directly?

## TL;DR / recommendation

1. **Use three reference sets, each for what it does best:**
   - **University of Iowa MIS, Steinway Model B.** Every note from A0 to C8 at pp, mf and ff, with unrestricted use. This is the **primary per-note measurement source** for inharmonicity, partial frequencies and decay rates.
   - **Salamander Grand Piano V3, Yamaha C5, CC BY 3.0.** It has 16 velocity layers and release samples. Use it for **velocity-dependent behaviour** (brightness vs velocity, attack) and **release/damper behaviour**, and as the **main listening A/B reference** for the user.
   - **Piano in 162, Ivy Audio, Steinway Model B.** It has separate pedal-up and pedal-down samples, which makes it the only practical free source for **sympathetic resonance / sustain-pedal measurements**. Its licence text is unclear, so use it for measurement only. Never redistribute it.
2. **Seed the model from published tables, then refine against the recordings.**
   - Seed string scale (length, diameter, tension, wrapping) and soundboard/hammer values from Chabassier & Duruflé's Steinway D parameter report and the Chabassier–Chaigne–Joly grand-piano paper.
   - Use Chaigne & Askenfelt (1994) for hammer and loss values at C2, C4 and C7.
   - Use Conklin (1996, Part III) for scale-design rules.
   - Then fit inharmonicity B, the loss filters and the two-stage decay per note against the Iowa and Salamander measurements.
3. **Ship numbers, not audio.** Commit the derived per-note parameter/measurement tables (CSV/JSON) to the GPLv3 repo, with credit lines for each source. Keep the audio out of the repo and installer. A `tools/fetch-references` script downloads it locally for the validation suite.
4. **Tooling:** Python with numpy, scipy, soundfile and librosa. Use the Rauhala et al. algorithm for B, a long-window STFT with partial tracking for frequencies and decays, and a two-slope or two-exponential fit (or FZ-ARMA/ESPRIT when partials beat) for double decay.

Caveat: none of these pianos is a 9-ft concert grand. The Yamaha C5 is about 200 cm and the Steinway B about 211 cm, while Forte targets a concert grand like a Steinway D (274 cm). Bass inharmonicity in particular will run **higher** on these smaller pianos than on a D, because they have shorter bass strings. So the recordings validate *behaviour and trends*, and the published Steinway D tables set the *absolute targets*.

## 1. Recording sets

| Set | Piano | Licence | Notes sampled | Velocity layers | Release / pedal | Format | Size |
|---|---|---|---|---|---|---|---|
| **Salamander Grand Piano V3** (Alexander Holm) | Yamaha C5 | CC BY 3.0 Unported | Every minor third from lowest A (~30 notes) | 16 | Hammer-noise releases chromatic (1 layer); string-resonance releases in minor thirds (3 layers); pedal *noise* only, no pedal-down note samples | 48 kHz/24-bit FLAC/WAV; 44.1/16 WAV; SF2 | 0.3–1.2 GB |
| **Univ. of Iowa MIS Piano** | Steinway & Sons Model B (2001) | No named licence. The site says files "may be downloaded and used for any projects, without restrictions" | **Chromatic.** pp Bb0–C8, mf B0–C8, ff A0–C8 | 3 (pp, mf, ff) | Single notes, no release or pedal samples | 44.1 kHz/16-bit stereo AIFF | ~ few hundred MB |
| **Piano in 162** (Ivy Audio) | Steinway Model B | Described as "free for personal and commercial use". No licence text found in public sources | Not stated on SFZ page | 5, with 2 round-robins | **Pedal-on and pedal-off samples**; 2 mic positions | Kontakt / SFZ | ~4.9 GB |
| Headroom Piano (Bengt Nilsson) | Yamaha C3 | CC BY 4.0 | not stated | not stated | not stated | SFZ | 156 MB |
| Splendid Grand Piano | "Steinway" (AKAI origin) | Claimed public domain ("released as public domain in early 2000 by Akai") | not stated | 4 (+1 synthetic) | not stated | SFZ/FLAC | 77 MB |
| MAPS ISOL (Télécom Paris) | Yamaha Disklavier (plus virtual pianos) | CC BY-NC-SA 2.0 FR | Isolated notes, known MIDI velocity | many | sustain pedal on/off variants | WAV | large |
| YDP Grand (FreePats) | Yamaha Disklavier Pro (Zenph) | CC BY 3.0 | not stated | not stated | — | SF2 | 36 MB |

Sources:
- Salamander: official FreePats page <https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html> (it also lists YDP Grand), and the SFZ repo with its licence statement and pedal-noise note <https://github.com/sfzinstruments/SalamanderGrandPiano>.
- Iowa MIS: piano page <https://theremin.music.uiowa.edu/MISpiano.html> and the terms on the main page <https://theremin.music.uiowa.edu/MIS.html>. Recording setup: 2× Neumann KM 84, 8" above bass and treble strings, non-anechoic. One mf file (Gb7) has a broken link.
- Piano in 162: <https://sfzinstruments.github.io/pianos/in_162>, plus press coverage stating free personal/commercial use: <https://rekkerd.org/ivy-audio-releases-piano-162-sample-library/>, <https://bedroomproducersblog.com/2014/05/11/piano-in-162/>.
- Headroom, Splendid and the full free-piano catalogue: <https://sfzinstruments.github.io/pianos>, <https://sfzinstruments.github.io/pianos/headroom_piano>, <https://github.com/sfzinstruments/SplendidGrandPiano>.
- MAPS: <https://adasp.telecom-paris.fr/resources/2010-07-08-maps-database>.
- The SFZ catalogue also lists a "Church Steinway" (Model D, licence not stated). It is a concert grand, but its unknown licence makes it unusable as a documented reference.

### Rejected or secondary

- **Splendid Grand:** the public-domain claim can't be verified (AKAI factory samples, no licence file), and the source model is unknown. Don't rely on it.
- **MAPS:** the NC (non-commercial) clause conflicts with GPL "any purpose" freedoms if we ever redistributed anything derived from the audio itself. The piano is a Disklavier, not a concert grand. It is useful only as a cross-check, and it is not needed.
- **Headroom Piano:** a small grand (C3) with an undocumented sample map. CC BY 4.0 is the cleanest licence, but the source adds little over Salamander.

## 2. Licence compatibility with GPLv3

What we plan to ship is **derived numbers**: per-note B, partial decay times, beat rates, centroid curves and fitted model parameters. We do not plan to ship the audio.

- **Measured physical quantities are facts.** In the US, facts are not copyrightable and only creative selection/arrangement is protected (*Feist v. Rural*, 499 U.S. 340 (1991), <https://supreme.justia.com/cases/federal/us/499/340/>). A table of inharmonicity coefficients measured from a recording is very unlikely to be an "Adaptation" of the sound recording. This is not legal advice, but it is the standard position.
- **Salamander (CC BY 3.0):** even if derived data counted as an Adaptation, CC BY 3.0 only requires credit identifying the use of the Work, and labelling that changes were made (§3(b), §4(b) of <https://creativecommons.org/licenses/by/3.0/legalcode>). That is easy to satisfy in a CREDITS file.
  - The FSF lists CC BY **4.0** as GPL-compatible. I could not re-fetch the FSF page during this research (rate-limited): <https://www.gnu.org/licenses/license-list.html#ccby>. Version 3.0's explicit status is less clear-cut.
  - That only matters if we bundled Salamander *audio* inside the GPL distribution, which we won't.
- **Iowa MIS:** "used for any projects, without restrictions." No conflict. Credit it as a courtesy.
- **Piano in 162:** there is no licence text, so treat it as "all rights reserved, use permitted". Measure locally, publish only derived numbers, and never commit or redistribute the samples. If we want stronger footing, ask Ivy Audio by email for permission to publish derived measurements.
- **CC BY-SA 4.0 → GPLv3** is a one-way compatible path (<https://wiki.creativecommons.org/wiki/ShareAlike_compatibility:_GPLv3>). It is relevant only if we add a BY-SA set later.
- **Published papers:** parameter values from journal papers are facts. Cite them, but don't copy figures or tables verbatim as images.

**Practice:**
- Add a `CREDITS.md` naming Alexander Holm/Salamander (CC BY 3.0), the University of Iowa Electronic Music Studios, Ivy Audio, and each paper used for parameters.
- Add a download script that fetches the audio, so no audio sits in the repo.

## 3. What we can measure, and how

All of the following run on single-note samples. Each method is aimed at Python with `numpy`, `scipy`, `soundfile` and `librosa`.

### 3.1 Inharmonicity coefficient B (per note)

- **Model:** stiff-string partials follow f_n = n·f0·√(1 + B·n²) (Fletcher 1964, "Normal vibration frequencies of a stiff piano string", JASA 36, 203; Fletcher, Blackham & Stratton 1962, "Quality of piano tones", JASA 34, 749–761, <https://physics.byu.edu/docs/publication/1504>).
- **Method:**
  1. Take a long-window FFT (≥0.5 s, Hann, zero-padded) of the steady part after the attack.
  2. Pick peaks near the predicted f_n, refine them with parabolic/quadratic interpolation, and fit f0 and B by least squares on f_n²/n² = f0²(1 + B·n²). That relation is linear in n², so a plain `numpy.polyfit` works.
  3. Iterate the prediction windows outward, which is the Rauhala, Lehtonen & Välimäki (2007) "Fast automatic inharmonicity estimation algorithm", JASA 121(5) EL184–EL189, doi:10.1121/1.2719043. It needs only f0 and is robust to noise.
  4. Alternative: inharmonic comb filters (Galembo & Askenfelt 1999, IEEE Trans. Speech Audio Proc. 7(2), 197).
- **Reliability:**
  - Very good in the mid and bass ranges, where many partials are present.
  - In the top octave only a few partials exist, so B is poorly constrained there. Extrapolate from the string scale instead.
  - Bass notes include **phantom partials** (longitudinal coupling) and **longitudinal modes** (Conklin lecture notes: <https://www.speech.kth.se/music/5_lectures/conklin/longitudinal.html>). Robust peak association (nearest to the prediction, with an amplitude threshold) is needed.
- **Best source:** Iowa MIS (chromatic, so all 88 notes), checked against the Salamander minor-third notes.

### 3.2 Partial frequencies and decay rates, including two-stage decay

- **Physics:** with 2–3 slightly mistuned unison strings coupled through the bridge, vertical and horizontal polarisations produce a fast "prompt sound" followed by a slow "aftersound", plus beating (Weinreich 1977, "Coupled piano strings", JASA 62, 1474–1484; Weinreich's lecture: <https://www.speech.kth.se/music/5_lectures/weinreic/weinreic.html>).
- **Method:**
  1. Track each partial with an STFT. Use a window of around 4096–16384 samples at 48 kHz, scaled to the note's f0. Alternatively heterodyne each partial (multiply by e^{-jω_n t} and low-pass) to get its amplitude envelope directly.
  2. Convert the envelope to dB and fit **two lines**, a piecewise-linear "knee" fit. That yields T60 for the prompt and aftersound stages plus the knee time and level. For a smoother result, fit A1·e^{-t/τ1} + A2·e^{-t/τ2} with `scipy.optimize.curve_fit`.
  3. Beating partials show periodic ripples. Estimate the beat frequency from the envelope's modulation spectrum.
  4. For closely spaced pairs that the STFT can't separate, use high-resolution methods:
     - FZ-ARMA (Karjalainen et al. 2002, JAES 50(12), <https://www.aes.org/e-lib/browse.cfm?elib=11054>).
     - The string-specific version (Esquef, Karjalainen & Välimäki 2003, EURASIP JASP, <https://doaj.org/article/f0ffe293867c4bf6a8796fb2f5ca1bb8>).
     - ESPRIT, the standard subspace method.
- **How this feeds the model:** Bank's thesis covers calibrating digital-waveguide loss filters and a parallel resonator bank for beating and two-stage decay from exactly these measurements (Bank 2000, *Physics-based sound synthesis of the piano*, HUT report 54, <https://home.mit.bme.hu/~bank/thesis/>; the later PhD and the Bank/Zambon/Fontana IEEE TASLP paper are linked there). Bensa et al., "Parameter fitting for piano sound synthesis by physical modeling" (<https://hal.archives-ouvertes.fr/hal-00105865>), fits string-model loss and dispersion parameters to recordings.
- **Reliability caveats:**
  - **Noise floor.** Iowa is 16-bit and Salamander is 24-bit, but both have room and mic noise. Late aftersound in the treble falls into the noise within seconds, so only fit above floor + 10 dB.
  - **Sample truncation.** Sample libraries often fade or cut tails. Check each file's tail before trusting long T60s.
  - **Room.** The mics are close (12 cm in Salamander, 8" in Iowa), so room reverb barely affects decay. Iowa is explicitly non-anechoic.

### 3.3 Spectral centroid (brightness) vs velocity

- **Method:** run `librosa.feature.spectral_centroid` (or a centroid computed manually over harmonic peaks, normalised by f0) over the first ~50–200 ms. Plot it per note against the velocity layer.
- **Best source:** Salamander's 16 layers give a smooth curve. Iowa's three dynamics serve as a cross-check.
- **Caveat:** sample velocity layers are not calibrated to hammer speed. Compare *trends* (centroid/f0 rising with level), and use relative level (RMS of the attack) as the x-axis instead of the layer index.

### 3.4 Attack transients

- **Measurements:** onset detection (`librosa.onset`), rise time (10%→90% of peak envelope), attack spectrum, and pre-tone thump/precursor.
- **Caveat:** close mics exaggerate hammer and action noise. These measurements are good for comparison, not for absolute targets. Hammer-felt behaviour is better constrained by the published hammer models (§4).

### 3.5 Release behaviour (dampers)

- **Source:** Salamander's string-resonance release samples (3 layers, minor thirds) and chromatic hammer-noise releases.
- **Method:** measure the post-release decay rate per partial (the same envelope fit as §3.2) and the residual level and spectrum of the release thump. The release sample alone gives decay *after* key-up. The rate change at the moment of release can't be measured from a sample library, because releases are separate recordings.
- **Gap:** no free set provides **half-pedal** releases. Half-pedal behaviour comes from the literature only:
  - Lehtonen et al., part-pedaling analysis and modeling, JASA 2007/2009. I could not confirm volume/page numbers from primary sources during this research.
  - Stulov, Välimäki & Lehtonen, "Modeling of the part-pedaling effect in the piano".

### 3.6 Sympathetic resonance and the sustain pedal

- **Source:** Piano in 162 pedal-on vs pedal-off samples of the same note and velocity.
- **Method:** take long-window spectra of both and difference them in dB. Look for added energy at the partials of undamped strings and a raised noise floor or "halo". Also compare the change in decay time for the struck note's partials.
- **Literature:** Zambon, Lehtonen & Bank, "Simulation of piano sustain-pedal effect by parallel second-order filters", DAFx-08 (<https://dafx.de/paper-archive/details/5mJ31JDQWZTHNiqjmf19mQ>), which is calibrated with FZ-ARMA. Schutz et al. (AES 2008, <https://eurecom.fr/publication/2478/download/cm-schuan-080517.pdf>) report that the sustain pedal increases partial decay times and adds a broadband noise floor.
- **Reliability:** qualitative to semi-quantitative. Two separate takes differ slightly in velocity, so compare normalised spectra.

## 4. Published parameter tables that can seed the model

| Source | What it gives | Piano | Access |
|---|---|---|---|
| Chabassier & Duruflé, *Physical parameters for piano modeling*, INRIA RT-0425 (2012), 24 pp. | Soundboard and air, hammer, and per-string parameters in two variants: (a) measured lengths with a virtual denser material, (b) unwrapped steel strings with lengthened bass | The "reference piano" of their simulations (the companion JASA paper compares against a Steinway D) | HAL hal-00688679 <https://hal.archives-ouvertes.fr/hal-00688679v1>; record <https://oskar-bordeaux.fr/handle/20.500.12278/189837> |
| Chabassier, Chaigne & Joly, "Modeling and simulation of a grand piano", JASA 134, 648–665 (2013) | Full model (stiff, lossy, nonlinear strings, hammer, soundboard, radiation) validated against Steinway D recordings | Steinway D | HAL <https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089> |
| Chaigne & Askenfelt, "Numerical simulations of piano strings I/II", JASA 95, 1112–1118 and 1631–1640 (1994) | Per-note string (tension, mass, stiffness ε, loss b1/b3, strike point) and hammer (mass, K, p) values for representative notes (C2, C4, C7, as I recall; check against the PDF) | Steinway grand | <https://web.phys.ntnu.no/~stovneng/TFY4160_2007/oving8/numerical_simulations_of_pianostrings_I.pdf> (host refused connection during this research) |
| Conklin, "Design and tone in the mechanoacoustic piano, Parts I–III", JASA 99/100 (1996); Part III "Piano strings and scale design", JASA 100, 1286–1298 | Scale-design principles: tension, stress, inharmonicity profile, longitudinal modes | Steinway (Conklin was Baldwin's chief engineer) | Lecture version free at <https://www.speech.kth.se/music/5_lectures/conklin/strings.html> |
| Stulov, "Hysteretic model of the grand piano hammer felt", JASA 97(4), 2577 (1995) | Felt stiffness/hysteresis parameters per hammer | grand | JASA (paywalled) |
| Young, "Inharmonicity of plain wire piano strings", JASA 24, 267 (1952) | Measured B for many pianos | various | JASA (paywalled) |

The first and third rows are the practical seed. For the Steinway D strings, use Chabassier/Duruflé's table. Then compute B = π³·E·d⁴/(64·T·L²) (plain wire, Fletcher 1964) per note, and check it against the Iowa-measured B scaled for the longer D bass strings.

## 5. Suggested measurement pipeline (for the validation ticket)

1. `tools/fetch-references`: download Iowa (88 × 3 AIFF) and the Salamander FLAC into a git-ignored `refs/` folder. Piano in 162 is a manual download (donation link), with instructions only.
2. `analysis/measure.py <wav> <midi_note>` writes a JSON with f0, B, partial list (freq, initial dB, T60 prompt, T60 after, knee time, beat Hz), centroid/f0, rise time, and release T60.
3. Run it over every reference note and commit the resulting `data/reference-measurements/*.json`. Only the numbers go in the repo.
4. Run the same script on Forte renders, so validation is a diff of two JSON sets with tolerances. The objective-validation ticket decides the tolerances.

## Open issues found during research

- **No free concert-grand (Steinway D class) recording with a clear licence.** Should we accept smaller-grand references plus Steinway D literature, or ask a pianist/studio to record a D under CC BY or CC0?
- **Piano in 162 licence:** email Ivy Audio to confirm that publishing derived measurements is fine.
- **Half-pedal / part-pedal** behaviour has no free reference recordings. Validating it will depend on literature and the user's ear.
- **Velocity calibration:** sample velocity layers are not tied to hammer velocity. The model's velocity→hammer-speed mapping needs its own decision.
- Exact citation details for the Lehtonen et al. sustain/part-pedal JASA papers, and the Chaigne & Askenfelt table values, could not be confirmed from primary sources during this pass. Check them when the relevant ticket is implemented.
