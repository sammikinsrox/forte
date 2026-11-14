# Soundboard impulse response: where Forte gets one

Research for ticket [0012](../issues/0012-soundboard-impulse-response-source.md). Sources were checked on 2026-11-14. Each claim links to the source it came from. Statements marked **(estimate)** are my own engineering judgement. Statements marked **(unverified)** could not be confirmed from the primary source.

## What we need, in plain words

Ticket [0003](soundboard-coupling-dampers.md#2-soundboard-and-body) settled the plumbing. All strings add their force at the bridge into one signal. That signal is run through a filter that sounds like the soundboard and body, and the output is what you hear. The cheapest faithful filter is a **convolution**: the signal is "played through" a recording of how the soundboard rings after one sharp tap. That recording is the **impulse response (IR)**. Ticket 0003 sized it at about 0.4 s, stereo.

Forte ships under GPLv3, so whatever IR we ship must be ours or carry a licence GPLv3 can absorb. In practice that means CC0, CC BY 4.0, MIT/BSD, or GPL itself.

A useful fact: a piano soundboard's response "sounds like a colored, decaying noise" ([Bank 2000 §5.4.2](https://home.mit.bme.hu/~bank/thesis/)). Above about 200–300 Hz its resonances overlap so much that they can't be told apart ([Plath 2019 dissertation, §1 and §6.3](https://ediss.sub.uni-hamburg.de/bitstream/ediss/6308/1/Dissertation.pdf); [Ege, Boutillon & Rébillat 2013](https://arxiv.org/abs/1212.2323)). So only two things have to be right: the **colour** (how loud each frequency band is) and **how fast each band dies away**, plus a handful of distinct low resonances. The exact fine detail above a few hundred Hz is statistical. That is why "synthesize one" is a real option and not a compromise.

## TL;DR

| Option | Licence for Forte | Quality | Effort | Verdict |
|---|---|---|---|---|
| **A. Find a free measured grand-piano soundboard IR** | None found under a usable licence | — | — | **Doesn't exist.** The only free ones are uprights, and the best licensed one is a dismantled 1800s upright. |
| **B. Synthesize one from published soundboard data** (modal noise) | 100% ours (GPLv3). Papers give facts, not copyrighted audio. | Plausible and fully tunable, but "generic": the colour is a guess until calibrated. | Low: 1–2 days of Python **(estimate)** | **Build first** as the day-one IR and the generator for everything after. |
| **C. Estimate from the Plath concert-grand dataset** (bridge accelerometer + anechoic dummy head, CC BY 4.0) | CC BY 4.0, which the FSF lists as compatible with all GPL versions. Credit needed. | Best available: a real concert grand, measured with input and output at the same time in an anechoic room | Medium: about 1 week, plus a 4.6 GB local download that stays out of the repo **(estimate)** | **Recommended final source.** Use it to calibrate B. |
| **D. Estimate from the 0005 reference recordings** (Iowa, Salamander) | Iowa: unrestricted. Salamander: CC BY 3.0. Both fine for derived numbers. | Weaker: no input signal, close mics, and room/hammer noise mixed in. Gives colour only roughly. | Medium | **Cross-check only.** |
| **E. Physical plate model run offline** | Ours, but the tools differ (MAGPIE Python is GPL-3.0, MAGPIE MATLAB is MIT) | Potentially very good, but needs ribs, bridges and rim modelled, and the real-time result is the same filter as B or C | High: weeks | **Not for v1.** |

**Recommendation:** ship a **synthesized modal-noise IR (B) whose colour and decay are fitted to the Plath concert-grand measurements (C)**, and cross-check it by ear against Salamander and Iowa (D). Because the generator is our own code, the same tool later gives "body size" and "resonance" knobs for free. Details are in [Recommendation](#recommendation).

---

## Option A: free measured IRs

### What was searched

Zenodo, Freesound, IR libraries, the papers behind real-time piano models, and open-source piano synths.

### What exists

| Source | Piano | What it is | Licence | Usable? |
|---|---|---|---|---|
| [Freesound #135432 "Knock"](https://freesound.org/s/135432) (rhapsodize) | 1800s Brazilian-rosewood **upright**, being dismantled | A hammer strike on the soundboard, recorded with one mic. 6.6 s, 44.1 kHz. | **CC0** ("Free to use without attribution") | Licence yes. Quality poor for a concert grand: wrong instrument, wrong condition, unknown mic and room. Fine as a **throwaway placeholder** while building the convolution code. |
| [Ralph Gonzalez "Convolution Impulses"](https://sites.google.com/site/ralphgonz/music-impulses) | Unnamed **upright** | Soundboard tapped with a ceramic spoon, damper up and down, 24-bit/44.1 kHz stereo | Only "Copyright Ralph Gonzalez 2005". No licence. | **No.** All rights reserved. |
| [Freesound #35725 "piano convolution.wav"](https://freesound.org/s/35725) | Broken upright Steinway | An ambient sound-design drone, not an IR | CC BY 4.0 | **No.** It isn't an IR. |
| [Audiolabs Erlangen, Berendes et al., ISMIR 2023 LBD](https://audiolabs-erlangen.de/resources/MIR/2023-ISMIR-PianoSynth/) | **Yamaha C3 grand** | Contact speaker under the soundboard, 10 s exponential sweep from 20 Hz to 20 kHz, stereo mics above the open lid, 4 excitation points, strings on and tuned, non-anechoic room | No licence stated. The page embeds only demo audio (m4a), not the IRs. I checked the page source. | **Not available.** Worth an email (see New questions). |
| [Bank, Zambon & Fontana 2010](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf) | Real grand (impact-hammer measured, per [Zambon et al. 2008 §5](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf)) | 20,000-tap measured soundboard responses | Not published as data. I found none. | **No.** |
| [Bank's parallel-filter page](http://home.mit.bme.hu/~bank/parfilt/) | — | MATLAB code and example data (a room response and a guitar admittance, but **no piano data**) | "All rights … reserved to the author. Non-commercial use permitted" | **No, for both the data and the code.** Forte must reimplement the parallel-filter fit from the papers (the method itself is not restricted, only the code). |
| [qiano](https://github.com/claytonotey/qiano) (Clayton Otey) | Unknown | A waveguide piano. `src/reverb.cpp` hard-codes a 65,536-sample convolution table used as the soundboard. | Repo licence: GPL-2.0 (GitHub API). The source files have no headers, so "or later" is not stated, and **the IR's origin is undocumented**. | **No.** Unknown provenance, and GPLv2-only would be incompatible with GPLv3. |
| [Plath dataset, Zenodo 10.5281/zenodo.3274772](https://zenodo.org/record/3271369) | Concert grand | Played notes, not an IR. See option C. | CC BY 4.0 | Not an IR as-is, but see C. |

**Conclusion:** no measured grand-piano soundboard IR is available under a licence Forte can ship. The concert-grand driving-point-mobility measurements in Plath's work (impact hammer at 10 bridge points, all strings damped; [ISMA 2019 §5.5](https://pub.dega-akustik.de/ISMA2019/data/articles/000005.pdf)) are **not** in the Zenodo archive. I read the zip's file directory with a small HTTP range request instead of downloading 4.6 GB. It holds only `D1_PROD07_*` and `D1_PROD08_*` played-note takes: 880 CSV + 880 TXT files.

---

## Option B: synthesize an IR from published soundboard data

### How it works (plain language)

Build the IR offline as a sum of many decaying sine waves, one per soundboard resonance ("mode"):

1. **Low modes, placed by hand.** Below about 250–300 Hz a grand soundboard has distinct resonances ([Plath diss. §6.3](https://ediss.sub.uni-hamburg.de/bitstream/ediss/6308/1/Dissertation.pdf): "An upper frequency limit for distinct resonances between 250 Hz and 300 Hz"). Seed them from published concert-grand values. Wogram's 2.90 m concert grand (make not stated) has modes at **62, 90, 105, 127, 187, 222, 245 and 325 Hz** ([KTH lecture, Wogram](https://www.speech.kth.se/music/5_lectures/wogram/modal.html)). Plath notes that on a concert grand the first "breathing" mode can be as low as about 50 Hz, so "the strings of almost the first octave A0–G1 have fundamental frequencies below the first soundboard mode" (diss. §3.1).
2. **Everything above, statistically.** Scatter modes randomly at the right density, with random phases. Ege, Boutillon & Rébillat measured an upright board and found a mean mode spacing of about 22 Hz for the lowest modes, density rising toward about 0.06 modes/Hz below about 1.1 kHz, and falling apparent density above that as the ribs confine the waves ([arXiv 1212.2323](https://arxiv.org/abs/1212.2323), via its [HTML version](https://ar5iv.arxiv.org/html/1212.2323)). A concert-grand board is roughly twice the area of that 0.91 × 1.39 m upright board, so expect roughly twice the density **(estimate)**.
3. **Decay per mode.** The measured loss factor is mostly **1–3%, mean about 2.3%**, up to about 1.2 kHz, and stays "between 1% and 3% over several kHz" ([Ege et al. 2013](https://ar5iv.arxiv.org/html/1212.2323)). A loss factor η gives an amplitude decay rate of α = π·f·η per second, and a 60 dB drop takes about 6.9/α seconds. So at 2.3%, a 100 Hz mode takes about 1 s, a 500 Hz mode about 0.2 s, and a 2 kHz mode about 0.05 s **(estimate, from the standard relation)**. That agrees with Ege et al.'s measured mean of about 80 s⁻¹ below 1.2 kHz. So 0.4 s covers everything except the few lowest modes. Either accept a gentle fade on those, or lengthen the IR to about 1 s, which costs little with non-uniform partitioning ([Bank 2010 §VII-A](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)).
4. **Colour (overall spectral shape).** Set each mode's amplitude from a smooth target curve: bridge mobility × radiation efficiency. Ege & Boutillon show that the average bridge mobility follows from just three numbers, "the mass of the structure M, the modal density n(f), and the mean loss factor η(f)" ([arXiv 1210.5688](https://arxiv.org/abs/1210.5688)). For a concert grand, Plath measured that mean mobility "stays constant" from 500 Hz to 5 kHz once the board is glued into the rim (diss. §6.3). Radiation is weak at low frequencies, where the front and back of the board cancel (diss. §3.1). Boutillon & Ege also find a wider "subsonic" radiation range than a plain plate ([arXiv 1305.3057](https://arxiv.org/abs/1305.3057)).
5. **Stereo:** run the same modes with independent random amplitudes and phases per channel. Different bridge regions get different random weights over the same modes, which matches Bank's observation that the regions excite "the same modes, just at different levels" ([Bank 2000 §5.4.2](https://home.mit.bme.hu/~bank/thesis/)).

Smith's commuted-synthesis notes support the same idea from the signal side: a tapped piano soundboard resembles low-pass-filtered noise "whose bandwidth contracts over time", so it can be synthesized from noise through a time-varying low-pass filter ([PASP: Excitation Synthesis](https://ccrma.stanford.edu/~jos/pasp/Excitation_Synthesis.html)).

### Licence

The generator and its output are **entirely Forte's own work under GPLv3**. The inputs are numbers (mode frequencies, loss factors, densities) taken from papers. Those are facts. Cite them in `CREDITS.md`, as [reference-recordings.md §2](reference-recordings.md#2-licence-compatibility-with-gplv3) already recommends.

Seed geometry (board size, rib spacing) can come from Plath diss. Fig. 3.4: a modern concert grand has "17 ribs, average inter-rib distance: 12.65 cm, average rib height: 20 mm". It can also come from Chabassier & Duruflé's INRIA report. **(unverified):** I couldn't open that report, because HAL blocked automated fetches during this research.

### Quality and effort

- **Quality:** it sounds like a soundboard, and it is stable and fully controllable. Its weakness is that the **colour is invented**. Unless it is calibrated, it is "a" piano body, not "the" concert-grand body.
- **Bonus:** the same mode list *is* a fixed-pole parallel-filter design. That is the upgrade path ticket 0003 kept open. Scaling all mode frequencies gives a "body size" knob, and scaling the loss factors gives a "resonance" knob. Bank describes exactly these controls for his parallel filter ([Bank 2010 §VII-B](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)).
- **Effort:** about 150 lines of numpy plus listening. 1–2 days **(estimate)**.

---

## Option C: estimate from the Plath concert-grand dataset (recommended source)

### What the dataset is (verified)

- **Record:** Niko Plath (Univ. Hamburg), "Influence of Playing on the Tonal Characteristics of a Concert Piano – Dataset", Zenodo, [DOI 10.5281/zenodo.3274772](https://doi.org/10.5281/zenodo.3274772), v1.0.1, 2019. **Licence: CC BY 4.0.** It contains one `Data.zip` of 4.6 GB ([Zenodo record](https://zenodo.org/record/3271369)).
- **Instrument:** a new concert grand ("D1") from a Hamburg manufacturer, measured once new and once after a year of concert hire in Munich ([ISMA 2019 §3](https://pub.dega-akustik.de/ISMA2019/data/articles/000005.pdf); diss. Part II). **(unverified):** the documents I read don't name the make and model. The context (a Hamburg factory, rental concert pianos, Steinway terminology and Steinway history throughout the thesis) strongly suggests a Steinway D-274, but this is an inference.
- **Setup:** the piano is carried "into an anechoic room and the lid is dismounted". For each state there are 88 keys × 5 takes played forte. The keys were pressed with the sensor head of an impact hammer, so **key force** is recorded. A **dummy head (HSU 3.2) sits in player position**, and a **PCB 352C23 accelerometer sits on the bridge at that key's string termination**, normal to the board (ISMA 2019 §3).
- **Files (verified from the zip directory and one metadata file read by range request):** 880 takes in all, each a ~16 MB CSV plus a TXT header. The header reads: "Timebase: 50.0 kHz", "Sequences: 262144 [4 channels each]" (5.24 s), 287 ms pre-trigger. Channel 1 is key force (N), channel 2 is bridge acceleration (m/s²), and channels 3–4 are the dummy head left and right (Pa).

### Why this is the best data available

It is the only free data I found where the **input to the soundboard (bridge motion) and the output (sound pressure) are recorded together, on a concert grand, in an anechoic room**. That is what "measuring an impulse response" means. The string itself is the test signal. No room reverb gets baked into the IR, which matters because Forte has no built-in reverb and leaves room sound to the user.

### How to turn it into an IR (plain language)

1. For each key take, compare the sound at the ears with the motion at the bridge, frequency by frequency. The ratio is the **bridge-to-ear transfer function**. Standard cross-spectral averaging (the "H1 estimator") across many takes and keys removes noise.
2. A played note only puts energy at its partials, plus a broadband "knock" in the first few milliseconds. But 88 keys × many partials cover the spectrum densely from about 27 Hz to several kHz. So **pool across keys** to get a smooth colour curve per bridge region (bass bridge and treble bridge zones), and fit per-band decay times from the attack segments **(estimate / method proposal)**.
3. Forte's string model outputs bridge **force**, but the dataset measures bridge **acceleration**. To convert, multiply by jω and by the bridge mobility. Use a smooth mobility curve: Ege & Boutillon's three-parameter formula, flat from 500 Hz to 5 kHz per Plath's concert-grand measurement. In the plainest terms, this step tilts the curve, so it can also be tuned by ear **(estimate)**.
4. Feed that colour and decay into option B's generator. The result is a **synthesized IR with the measured concert-grand colour**, plus the low modes read straight off the measured bass-key transfer functions.

### Licence

CC BY 4.0. The FSF says CC BY 4.0 "is compatible with all versions of the GNU GPL; however, like all CC licenses, it should not be used on software" ([FSF licence list](https://www.gnu.org/licenses/license-list.html)). Forte isn't putting software under CC BY, only shipping data derived from CC BY data, so this works. The obligation is credit plus a note that changes were made. Add to `CREDITS.md`: "Soundboard response derived from N. Plath, *Influence of Playing on the Tonal Characteristics of a Concert Piano – Dataset*, Zenodo, doi:10.5281/zenodo.3274772, CC BY 4.0; modified." The 4.6 GB download stays out of the repo and installer. `tools/fetch-references` fetches it locally, as [0005](reference-recordings.md#5-suggested-measurement-pipeline-for-the-validation-ticket) proposed.

### Caveats

- **Dummy-head colouring.** The ears of an artificial head add their own frequency response (head and pinna). That gives an authentic "pianist's seat" perspective, but it can sound coloured on loudspeakers. Options: use only the overall colour and smooth out head-related detail above about 2 kHz, or make the perspective a product decision (see New questions).
- **One dynamic only (forte).** The soundboard is linear: Ege et al. measured nonlinearity about 40 dB below the linear response at fortissimo ([arXiv 1212.2323](https://arxiv.org/abs/1212.2323)). So one level is enough.
- **The accelerometer sits right at the string termination** (on the hitch pins), so it measures bridge motion plus some string-terminal motion. Pooling over keys and smoothing should handle this **(estimate)**.
- **5.24 s per take** is plenty for a 0.4 s IR.
- **A small anechoic room (2 × 4 m), 1980s build** (diss. §4). Expect it to be less effective at the lowest frequencies. Treat anything below about 100 Hz with suspicion, and lean on the hand-placed low modes there **(estimate)**.

### Effort

About a week **(estimate)**. That covers the one-off download, a CSV loader, the H1 estimate per key, pooling and smoothing, handing the result to the generator from B, and listening tests. It reuses the Python stack 0005 already chose (numpy, scipy, soundfile).

---

## Option D: estimate from the 0005 reference recordings (Iowa, Salamander, Piano in 162)

- **What's possible:** there is no input signal, so only *blind* estimates are possible. For example, take a long-term average of the residual left after removing the tracked partials, averaged over many notes. That gives a rough colour curve and per-band decay rates. This is the residual/excitation-extraction idea from commuted synthesis. The closest published method is for guitar, Lee, Duan & Smith, ICMC 2007 ([PDF](https://labsites.rochester.edu/air/publications/LeeDuanSmith_GuitarExcitation_ICMC07.pdf)). **(unverified):** I saw that paper only in a search listing and did not read it in full.
- **Problems:** the residual mixes the soundboard with the hammer and key-action thump, the mic positions (8" above the strings for Iowa, 12 cm for Salamander) and the room. Both pianos are also smaller than a D (Steinway B, Yamaha C5), as [reference-recordings.md](reference-recordings.md) notes.
- **Licence:** Iowa is "without restrictions" and Salamander is CC BY 3.0. Both are fine for derived parameters. Don't use Piano in 162 for anything shipped, because its licence text is unclear.
- **Use:** an **objective cross-check**. Render Forte notes with the C-derived IR, then compare long-term spectra and the attack "knock" against Iowa and Salamander, where the user already listens.

---

## Option E: physical soundboard model, solved offline

- **Idea:** compute the board's modes from its geometry and wood properties, then export them as an IR or parallel filter. Bank 2010 suggests exactly this at "calibration rate" ([§IX](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Full research models exist, such as the orthotropic Reissner–Mindlin plate with ribs and bridges of Chabassier et al. ([JASA 2013, HAL](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089); Chabassier & Duruflé, ViennaTalk 2015).
- **Open tools:** [MAGPIE (Python)](https://github.com/nemus-Project/magpie-python) is **GPL-3.0**, and [MAGPIE (MATLAB)](https://github.com/nemus-Project/magpie-matlab) is **MIT**, both licences checked with the GitHub API. They do modal analysis of thin rectangular plates with generalised elastic boundary conditions ([docs](https://nemus-project.github.io/magpie-python/html)). A grand board is not rectangular and is ribbed, so this would need real extension work.
- **Verdict:** high effort (weeks) **(estimate)**, and above about 300 Hz the result would still be statistical. The real-time result is the same filter that B and C produce. **Defer past v1.** It could come back if we want "build your own piano" presets.

---

## Recommendation

**Build one offline tool, `tools/make-soundboard-ir`, that synthesizes the IR (option B), and calibrate its colour and decay from the Plath concert-grand dataset (option C).**

Why, in plain terms:

1. **No ready-made IR is legally usable.** We have to make one either way.
2. **The fine detail of a soundboard response is random-like.** What you hear is its colour and how fast each band decays. A synthesized IR with the right colour and decay is, to the ear, as good as a measured one **(estimate; to be confirmed by listening)**.
3. **Plath's dataset is the only free measurement of a real concert grand's bridge-to-sound behaviour**, and it was recorded without room reverb. CC BY 4.0 is GPL-compatible and only needs a credit line.
4. **We own the generator**, so "body size" and "resonance" knobs are cheap later. The same mode list can drive the parallel-filter upgrade from ticket 0003 without new data.

Order of work:

1. **Day one (prototype):** generator with seeded values: Wogram low modes, about 2% loss factor, density scaled from Ege, flat mobility above 500 Hz, and a gentle low-frequency radiation roll-off. Ship this IR in early builds. The CC0 Freesound knock is fine for throwaway testing of the convolution code, but don't ship it.
2. **Calibration:** fetch the Plath data locally and fit the colour per bridge region plus band decays. Regenerate the IR. Check it against Iowa and Salamander (option D) and by ear.
3. **Ship:** the generated stereo IR (about 0.4 s, at the plugin's sample rates) plus the generator's parameter file, all under GPLv3, with the Plath credit line in `CREDITS.md`.

## New questions surfaced (candidate tickets)

1. **Listening perspective:** should Forte sound like the **pianist's seat** (Plath's dummy head, binaural-ish) or like a **stereo mic pair** over the open lid? This changes how option C's data is processed. It is a product decision for the user.
2. **Ask for better data (cheap, optional):** email Audiolabs Erlangen (Berendes/Schwär) about releasing their Yamaha C3 soundboard IRs under CC BY. Email N. Plath about releasing the concert-grand driving-point mobilities (10 bridge points) and confirming the piano model. Either would tighten the calibration, and neither blocks v1.
3. **Bridge-region count:** how many region shaping filters (ticket 0003 suggested 4–8) does the calibration support? Decide once the per-key transfer functions are seen.
4. **Sample-rate handling:** generate the IR natively at 44.1, 48 and 96 kHz, or resample one master? This feeds "Voice management and CPU strategy" on the map. Synthesis makes native generation trivial.

## Sources

- N. Plath, "Influence of Playing on the Tonal Characteristics of a Concert Piano – Dataset", Zenodo, 2019, CC BY 4.0. [Record](https://zenodo.org/record/3271369), [DOI 10.5281/zenodo.3274772](https://doi.org/10.5281/zenodo.3274772)
- N. Plath, "Influence of playing on the tonal characteristics of a concert piano – an observational study", ISMA 2019. [PDF](https://pub.dega-akustik.de/ISMA2019/data/articles/000005.pdf)
- N. Plath, doctoral dissertation, Univ. Hamburg. [PDF](https://ediss.sub.uni-hamburg.de/bitstream/ediss/6308/1/Dissertation.pdf)
- K. Ege, X. Boutillon, M. Rébillat, "Vibroacoustics of the piano soundboard: (non)linearity and modal properties in the low- and mid-frequency ranges", *J. Sound Vib.* 332(5), 1288–1305, 2013. [arXiv 1212.2323](https://arxiv.org/abs/1212.2323)
- K. Ege, X. Boutillon, "Synthetic description of the piano soundboard mechanical mobility", ISMA 2010. [arXiv 1210.5688](https://arxiv.org/abs/1210.5688)
- X. Boutillon, K. Ege, "Vibroacoustics of the piano soundboard: reduced models, mobility synthesis, and acoustical radiation regime", *J. Sound Vib.* 332(18), 4261–4279, 2013. [arXiv 1305.3057](https://arxiv.org/abs/1305.3057)
- K. Wogram, soundboard modal analysis, KTH lecture notes. [Page](https://www.speech.kth.se/music/5_lectures/wogram/modal.html)
- B. Bank, S. Zambon, F. Fontana, "A modal-based real-time piano synthesizer", *IEEE TASLP* 18(4), 2010. [PDF](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)
- B. Bank, M.Sc. thesis, 2000. [Page](https://home.mit.bme.hu/~bank/thesis/). Parallel filter code and terms: [page](http://home.mit.bme.hu/~bank/parfilt/)
- S. Zambon, H.-M. Lehtonen, B. Bank, DAFx-08. [PDF](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf)
- J. O. Smith, *PASP*: [Commuted Piano Synthesis](https://ccrma.stanford.edu/~jos/pasp/Commuted_Piano_Synthesis.html), [Excitation Synthesis](https://ccrma.stanford.edu/~jos/pasp/Excitation_Synthesis.html)
- H.-U. Berendes, S. Schwär, M. Schäfer, M. Müller, "Towards Differentiable Piano Synthesis Based on Physical Modeling", ISMIR 2023 LBD. [Page](https://audiolabs-erlangen.de/resources/MIR/2023-ISMIR-PianoSynth/)
- J. Chabassier, A. Chaigne, P. Joly, *JASA* 134(1), 2013. [HAL](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089)
- MAGPIE: [Python (GPL-3.0)](https://github.com/nemus-Project/magpie-python), [MATLAB (MIT)](https://github.com/nemus-Project/magpie-matlab)
- qiano: [GitHub](https://github.com/claytonotey/qiano)
- Freesound: [#135432 Knock (CC0)](https://freesound.org/s/135432), [#35725 (CC BY 4.0, not an IR)](https://freesound.org/s/35725). Ralph Gonzalez impulses: [page](https://sites.google.com/site/ralphgonz/music-impulses)
- FSF, licence list (CC BY 4.0, CC0). [Page](https://www.gnu.org/licenses/license-list.html)
- Lee, Duan, Smith, guitar excitation, ICMC 2007. [PDF](https://labsites.rochester.edu/air/publications/LeeDuanSmith_GuitarExcitation_ICMC07.pdf) (seen in search only)
