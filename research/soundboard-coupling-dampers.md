# Unison coupling, soundboard, dampers, sympathetic resonance and soft pedal

Research for ticket [0003](../issues/0003-soundboard-coupling-damper-models.md). Sources were checked on 2026-11-14. Each claim links to the source it came from. Statements marked **(estimate)** are my own arithmetic or engineering judgement, not a published figure.

## TL;DR (plain language)

None of these five parts is the expensive bit of a piano model. The strings and hammers (ticket [0002](../issues/0002-string-hammer-modeling-approaches.md)) are. With the methods recommended below, all five parts together cost roughly **7–22% of Forte's CPU budget** (about 2–5% of the whole core) **(estimate)**. That leaves about 80% or more for the strings and hammers.

| Part | Recommendation | Rough cost (share of the budget*) |
|---|---|---|
| **Unison coupling** (beats, two-stage decay) | Give each low partial of each note 2–3 slightly different "modes" (simple ringing filters). Compute their pitches, decay rates and loudness offline from a small coupled-string model, following Weinreich's physics. If Forte ends up with waveguide strings, couple 2–3 waveguides at one shared bridge filter instead. | 3–13% (3–6% if only audibly beating partials are coupled) |
| **Soundboard / body** | One shared filter that colours the summed bridge signal. Start with **partitioned FFT convolution** of a soundboard impulse response (about 0.4 s, stereo), plus a few cheap "bridge region" tone filters. Keep the **parallel second-order filter** form as the upgrade path if we want "body size" or "resonance" controls. | 2–4% |
| **Dampers + half-pedaling** | When a damper lands, raise each mode's decay rate smoothly. Partials that have a node at the damper's position get less damping. For half-pedaling, add a "gap" model: the damper only grabs the string while the string's swing is bigger than the gap that the pedal position sets. This reproduces the measured fast-decay-then-ringing behaviour. | < 1% |
| **Sympathetic resonance** | A **bank of about 1,000 cheap resonators**, grouped per key (8–16 low partials each). They are driven one-way by the summed bridge signal of all sounding strings, and each key's group is switched by its damper state (key held or pedal down). Don't run full string models for silent strings. | 3–6% |
| **Soft pedal (una corda)** | No extra DSP. Change *which* strings of the unison the hammer drives (the unstruck string then rings only through coupling). Also shift the hammer to softer, more "per-string-heavy" felt settings. | ~0% |

\*Budget = 25% of one ~4 GHz 2020 mid-range core at 48 kHz, which is about **20,800 CPU cycles per output sample**, or about 2.7 million cycles per 128-sample buffer **(estimate)**. "4%" here means 4% of that budget, which is 1% of the whole core.

**The one open dependency:** the cheapest and most controllable versions above assume **modal strings**. In a modal model, each string is a bank of ringing filters, one per overtone, as in [Bank, Zambon & Fontana 2010](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf). If ticket 0002 or 0007 picks waveguide strings, every part still works, with the variants noted per section.

---

## 0. How the parts fit around a string model

Every published real-time piano model reviewed here uses the same plumbing. The hammer excites the strings, and the strings produce a **force at the bridge**. That force goes two ways:

1. Into the **soundboard radiation filter**, which turns bridge force into sound.
2. Into a **sympathetic string register**, the other strings that can ring along.

The soundboard's *loading* of the string (it drains energy and slightly shifts each overtone's pitch) is not simulated as a separate feedback loop. It is folded into each string mode's decay time and frequency. Sources: [Bank, Zambon & Fontana 2010, §VII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf) ("only the radiation effects of the soundboard are implemented here, while the impedance effects are taken into account in the string model") and [Zambon, Lehtonen & Bank, DAFx-08](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf) (feed-forward model of body and string register, with body termination effects absorbed "by changing the partial frequencies and decay times of the string model").

This one-way ("feed-forward") design is what makes everything cheap and **unconditionally stable**. A full two-way simulation of strings, bridge and soundboard exists. [Chabassier, Chaigne & Joly, JASA 134(1) 2013](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089) is a time-domain global grand-piano model with nonlinear stiff strings coupled to a 3-D-radiating soundboard. It is an offline research tool and not a real-time option, so it is useful only as a reference.

**How the choice of string model changes things:**

| | Modal strings | Digital waveguide strings | Finite-difference strings |
|---|---|---|---|
| Unison coupling | Pre-computed coupled modes, or a secondary resonator bank ([Bank 2010 §VI-A](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)) | 2–3 waveguides joined at one bridge filter, physically natural ([Smith, PASP: Two ideal strings coupled](https://ccrma.stanford.edu/~jos/pasp/Two_Ideal_Strings_Coupled.html)) | Natural, but costly |
| Sympathetic | One-way feed into resonators (two-way is "more complicated", per Bank 2010 §VI-B) | Two-way through a common bridge is "relatively straightforward" (Bank 2010 §VI-B, citing [Borin, Rocchesso & Scalcon, ICMC 1997]) | Natural, but costly |
| Dampers | Change per-mode decay. Position-dependent damping is easy. | Change the loop filter. Bank's thesis switches to a pre-"warmed" damping filter to avoid clicks ([Bank 2000 §5.2.4](https://home.mit.bme.hu/~bank/thesis/)). | A real felt contact force is possible ([Bilbao et al., collisions](https://arxiv.org/abs/1405.2589)) |
| Soft pedal | Change the excitation weights | Change which waveguides get hammer force | Same |

---

## 1. Unison coupling (two-stage decay and beating)

### The physics in plain words

Most notes have 2–3 strings tuned almost, but not exactly, alike. They all press on the same bridge. Weinreich showed that this coupling explains the piano's "prompt sound" and "aftersound". The tone starts loud and dies fast while the strings move together and pump energy into the bridge. Then the strings drift out of step, partly cancel at the bridge, and the tone settles into a quieter, much longer tail. Weinreich called this "precisely the miracle of the piano tone": it sounds loud *and* sustained. ([Weinreich lecture notes, KTH](http://www.speech.kth.se/music/5_lectures/weinreic/weinreic.html); original paper: G. Weinreich, "Coupled piano strings," *JASA* 62(6), 1474–1484, 1977, [doi:10.1121/1.381677](https://doi.org/10.1121/1.381677)). Bank, Zambon & Fontana 2010 sum it up: beating is "a low-frequency amplitude modulation of partial envelopes", and two-stage decay "means that the partial decay is faster in the early part than in the latter" ([§VI](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Each string also vibrates in two planes (vertical and horizontal), and these are coupled through the bridge as well. That gives a second source of double decay ([Smith, PASP: String coupling effects](https://ccrma.stanford.edu/~jos/pasp/String_Coupling_Effects.html); [Smith, PASP: Coupled piano strings](https://ccrma.stanford.edu/~jos/pasp/Coupled_Piano_Strings.html), which says "multiple filtered delay loops should be employed for each note").

The key point for Forte: **simply adding two independent detuned strings gives beats but not a true two-stage decay.** Two-stage decay comes from the coupling, which makes the "in-step" and "out-of-step" combinations decay at different rates (Weinreich 1977).

### Methods

| Method | How it works | Cost | Quality / notes |
|---|---|---|---|
| **A. Full coupled strings** | 2–3 complete string models per note, joined at a shared bridge impedance. In waveguides, one shared filter computes the bridge velocity from all incoming waves, and that filter "generalizes to N strings" ([Smith, PASP](https://ccrma.stanford.edu/~jos/pasp/Two_Ideal_Strings_Coupled.html)). | 2–3× the string cost of each note. The single largest cost multiplier in the whole model. | Most physical. Beats and two-stage decay emerge on their own. |
| **B. Secondary resonator bank** (Bank) | One main string plus a bank of extra resonators driven by the same hammer force. Either a few (5–10) resonators on only the partials where the effect is audible, or a full but smaller second string. ([Bank 2010 §VI-A](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf); [Bank 2000 thesis §5.3](https://home.mit.bme.hu/~bank/thesis/)) | 3 multiplies per resonator per sample with input, 2 without ([Bank 2010 §VIII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Much cheaper than A in the bass. | Beating and two-stage decay "can be set separately" per partial, and it is cheaper than two parallel waveguides ([Bank 2000 §5.3](https://home.mit.bme.hu/~bank/thesis/)). Bank 2010 reuses the same resonators for sympathetic resonance. |
| **C. Beating equalizer** (Rauhala) | Post-processing: a peak EQ tuned to a partial, with its gain modulated, imposes beating on that partial's envelope ([DAFx-07](https://dafx.de/paper-archive/details/JFtCSJ3pxmYIqWrznQMSHw)). | Very cheap | A signal-level trick, not physical. It doesn't produce a true two-stage decay. |
| **D. Pre-computed coupled modes** (my synthesis of A + B for modal strings) | For each low partial, solve the small 2×2 or 3×3 coupled-oscillator problem that Weinreich analysed, *offline* or when parameters change. Smith works through the same eigen-analysis in [PASP: Coupled strings eigenanalysis](https://ccrma.stanford.edu/~jos/pasp/Two_Coupled_Strings.html). This gives 2–3 modes, each with its own frequency and decay. Run them as ordinary independent resonators, with amplitudes set by which strings the hammer hit. | Same as B: one extra resonator per extra string, per coupled partial | Physically derived pitches and decays (so it is calibratable against recordings), always stable, and the soft pedal comes free (section 5). **(estimate / design proposal)** |

### Cost estimate

Suppose about 20 low partials per note get 2 extra coupled modes (about 3 modes in total) across about 70 trichord or bichord notes. That adds about **2,800 resonators**, or about 1,400–2,800 cycles per sample, which is **7–13% of the budget** **(estimate)**. Coupling only the partials where beating is audible ("5–10" per note, per Bank 2010 §VI-A) brings that down to **3–6%**. A full-string version (method A) would instead double or triple the string budget.

### Recommendation

**Modal strings: method D**, coupling the lowest ~10–20 partials per note and keeping higher partials single. Calibrate detuning and coupling per note against two-stage decay measured on the reference recordings (ticket 0005). **Waveguide strings: method A**, with 2 coupled waveguides (3 only where the budget allows) and a cheaper single loop for the top octave or so.

---

## 2. Soundboard and body

### What it does

The soundboard turns the strings' bridge force into sound. It adds the piano's characteristic colour and the "knock" of the attack. A real soundboard's response depends on *where* on the bridge it is driven ([Bank 2010 §VII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Its impulse response "sounds like a colored, decaying noise" ([Bank 2000 §5.4.2](https://home.mit.bme.hu/~bank/thesis/)).

### Methods

| Method | How it works | Cost | Pros / cons |
|---|---|---|---|
| **Commuted synthesis** | Because body and string are linear, they can swap order. The soundboard impulse response is stored as a wavetable that *excites* the string, and the hammer becomes a filter ([Smith, PASP: Commuted piano synthesis](https://ccrma.stanford.edu/~jos/pasp/Commuted_Piano_Synthesis.html); Smith & Van Duyne, ICMC 1995). | Lowest of all | It assumes linearity and time-invariance, so "the restrike of the same string or the nonlinear vibration of strings, cannot be precisely modeled" ([Bank 2010 §VII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). **It clashes with Forte's nonlinear felt-hammer requirement.** |
| **Direct FIR** | Convolve with a measured response | 1,000–2,000 taps for "good tonality". For the treble knock, "ten thousands of taps are needed", so it is "not advantageous" done directly ([Bank 2010 §VII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). | Too slow as plain FIR |
| **Partitioned FFT convolution** | The same convolution done in blocks with FFTs. With a partition the size of the audio buffer, it adds **no extra latency** ([Bank 2010 §VII-A](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). | A 20,000-tap response needs about **633 flops per sample** for the frequency-domain multiplies (the block-size symbol was lost from the PDF text; 633 matches a 128-sample block under the paper's formula). In Bank's synth, four 20k-tap convolutions cost about a quarter of the strings' cost. | Exact reproduction of a measured board. Simple. Needs an impulse response. Non-uniform partitioning cuts the cost further ([Bank 2010 §VII-A](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). |
| **Fixed-pole parallel second-order filters** (Bank) | About 100 resonators on a log-frequency grid, with gains fit to the measured response by least squares. Fig. 6 of Bank 2010 shows a 200th-order (100-section) fit. Stereo shares the poles, so only the output gains are duplicated ([Bank 2010 §VII-B](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf); [Bank, parallel filter page](https://home.mit.bme.hu/~bank/parfilt)). | About 100 × 4–5 multiplies per sample | Zero latency, little memory, a "fully modal" piano. Musical controls come free: moving pole frequencies acts as "a bigger or smaller instrument body", and pole radii set body decay. Bank found it "seems to be larger" in cost than convolution for the same quality, and he had not yet used it in real time. |
| **Feedback delay network (FDN) + shaping filter** | A reverb-style network. Bank used 8 delay lines (37…721 samples, chosen as mutual primes) with one-pole loss filters, then a 100-tap FIR to shape the spectrum ([Bank 2000 §5.4.2](https://home.mit.bme.hu/~bank/thesis/)). | The cheapest true filter: about 8 delays + an 8×8 mix + 100 taps | Very high mode density for little work, but "the resonances cannot be controlled separately". Delays longer than ~200–300 samples sound "rather cello-like". Statistically right, not exact. |
| **Physical plate (finite differences / elements)** | Simulate the board itself ([Chabassier et al. 2013](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089)) | Far beyond real time | Bank 2010 suggests computing the response offline from such a model and then running it as a filter, so geometry can change at "calibration rate" ([§IX](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). |

**Bridge-position variation:** use one high-order soundboard filter (or a stereo pair) plus cheap low-order shaping filters per bridge region, rather than a full filter per region ([Bank 2010 §VII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf); [Bank 2000 §5.4.2](https://home.mit.bme.hu/~bank/thesis/): "there is no need to implement separate feedback delay networks for the different parts", because the same modes are excited, just at different levels).

### Cost estimate (stereo, 48 kHz, 128-sample partitions)

- **Convolution:** about 20,000 taps (0.42 s) per channel. That is about 1,300 flops per sample for the multiplies plus a little for the FFTs, so about 250–500 cycles per sample with AVX2, which is **~1.5–2.5% of the budget**. Memory is about 160 KB of spectra per channel, which fits in L2 cache. Four bridge-region convolutions would be about 5–10% **(estimate)**.
- **Parallel filter:** 100 sections shared by both channels is about 600 multiplies per sample, or **~1–2%** **(estimate)**.
- **FDN:** **< 1%**.
- Low-order region shaping filters (4–8 regions × ~8th-order IIR): **< 1%**.

Bank's soundboard ran in a separate thread through a ring buffer, which "introduced some additional latency" ([§VIII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Forte doesn't need that, because the cost is small enough for the audio thread.

### Recommendation

**One summed bridge-force bus → a few region shaping filters → stereo partitioned convolution.** It is the simplest and most faithful option, and the cost is trivial. Design the bus so the convolution can later be swapped for a **parallel second-order filter** fitted to the same response, if we want "body size / resonance" knobs in the editor or a fully modal structure. Don't use commuted synthesis (it breaks the nonlinear hammer). Don't use a physical plate model in real time.

**Open item:** we need a soundboard impulse response under a GPL-compatible licence. Measured ones need an impact hammer on a real bridge, as in [Zambon et al. 2008 §5](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf). The alternatives are to synthesize one offline from published soundboard modal data or a plate model, or to estimate one from the reference recordings. See "New questions" below.

---

## 3. Dampers and half-pedaling

### What the literature says

- **The simple method:** when the key is released, lower the string's loop gain or shorten the mode decay times. Bank 2010's real-time piano does exactly this: "dampers are implemented by simply decreasing the decay times of the resonators" ([§IX](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). Bank's thesis found a loop gain factor of 0.8 workable but "too clean". It works in the treble, but "in the middle and low registers of the piano more refined methods are needed" ([Bank 2000 §5.2.4](https://home.mit.bme.hu/~bank/thesis/)).
- **Avoiding clicks:** changing filter coefficients abruptly "could introduce some transients". Bank's fix is to keep a damping filter running in parallel, already "trained" on the signal, and switch to its output at release ([Bank 2000 §5.2.4](https://home.mit.bme.hu/~bank/thesis/)). For modal resonators, ramping the decay coefficient over a few milliseconds does the same job **(estimate)**.
- **What makes bass dampers sound real:** "every 7th partial is damped inefficiently, since the damper cannot act well on a mode which has a node at the damper position". The difference "can be heard especially at the lowest two octaves" ([Bank 2000 §5.2.5](https://home.mit.bme.hu/~bank/thesis/)). Bank proposed a nonlinear damper that reuses the hammer contact model (gravity pushing the felt down, with losses added). He never built it because "there has been no data in the literature concerning the damper characteristics of real pianos".
- **Measured half-pedaling (part-pedaling):** a JASA Express Letter by Lehtonen, Askenfelt & Välimäki (2009) splits a part-pedaled tone into **three phases**: initial free vibration; damper-string contact, where "the decay is rapid and the timbre changes" because of nonlinear limits on string motion; and a final free vibration once the swing is smaller than the damper's gap ([PubMed 19640015](https://pubmed.ncbi.nlm.nih.gov/19640015/); *JASA* 126(2), EL49, 2009). A follow-up theoretical model (Stulov, Välimäki & Lehtonen, 2012, *Proc. Estonian Acad. Sci.*) shows that the damper's amplitude limiting moves energy from lower to higher partials in the bass, and "can excite missing modes".
- **"Proper" contact simulation:** energy-stable collision schemes for finite-difference models can handle a felt barrier against a string ([Bilbao, Torin & Chatziioannou, "Numerical modeling of collisions in musical instruments," arXiv:1405.2589](https://arxiv.org/abs/1405.2589)). This is heavy and needs a finite-difference string.

### Methods for Forte

| Method | What it gets right | Cost |
|---|---|---|
| **D1. Ramped per-mode decay increase** on release | Basic note-off. Treble OK. | Negligible (control rate) |
| **D2. D1 + damper-position weighting.** Each mode's extra damping ∝ (its shape at the damper point)², i.e. sin²(nπ·x_d/L). Partials with a node at the damper ring on. | Bass "every 7th partial" ringing. Physically derived, no extra data needed. | Negligible |
| **D3. D2 + "gap" half-pedal model.** The pedal position sets a gap g. Extra damping only applies while the note's estimated swing (an envelope follower on the string output) exceeds g, scaled by how much it exceeds g. Optionally, a small soft-clip on that note's output during contact adds the energy spread to higher partials that Stulov et al. describe. | Reproduces the measured fast-decay-then-ring behaviour (Lehtonen et al. 2009), continuous with pedal depth | About 10–20 operations per active note per sample. **< 1%** **(estimate)** |
| **D4. Physical felt contact** (Bank's nonlinear damper, or Bilbao collisions) | Most physical | Moderate to high, and no calibration data exists |

### Recommendation

**Build D3**, as one damper state per note driven by the key (up/down) and the sustain-pedal position, with D1 and D2 inside it. Key releases and half-pedaling then share one model. The same damper state also gates sympathetic resonance (section 4). Treat D4 as out of scope for v1. The pedal-to-gap curve and contact strength get tuned by ear against reference recordings. That needs part-pedaled recordings, which may not exist in Salamander (ticket 0005 should check).

---

## 4. Sympathetic resonance

### What it is

When dampers are lifted, every undamped string can pick up energy from the bridge, ringing wherever its overtones match the sounding notes. That changes the timbre and lengthens the tail. Lehtonen et al. (2007) showed that the pedal changes partial decay rates, especially in the middle range, changes beating, and raises the energy of the residual "wash" ([cited in Zambon et al. DAFx-08 §1](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf); H.-M. Lehtonen, H. Penttinen, J. Rauhala, V. Välimäki, "Analysis and modeling of piano sustain-pedal effects," *JASA* 122, 1787, 2007). Held silent keys (without pedal) also resonate, which is the same mechanism for one string group.

### Methods

| Method | How it works | Cost | Limits |
|---|---|---|---|
| **S1. All strings as full models, two-way bridge coupling** | Every string runs all the time and shares a bridge admittance ([Borin, Rocchesso & Scalcon, ICMC 1997], via Bank 2010 §VI-B and Zambon 2008 §1) | Full polyphony, always on while the pedal is down | Most physical, most expensive. Two-way coupling of modal strings is hard to keep stable (Bank 2010 §VI-B). |
| **S2. One-way secondary resonators per key** (Bank 2010) | Sum the main string outputs, scale by a small region gain matrix, and feed the result into each key's secondary resonator bank. "Energy is flowing from the main string models to the secondary ones, but not vice-versa", which makes it "structurally stable". A keyboard-region gain matrix cost "an additional 64 multiplications and additions" ([§VI-B](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). | 2–3 multiplies per resonator per sample | It reuses the unison-coupling resonators, so they "cannot be deactivated" (§VIII). |
| **S3. Global parallel resonator bank** (Zambon, Lehtonen & Bank) | One parallel bank of K second-order sections, fitted to a measured "pedal-down minus pedal-up" soundboard response, using R = 2–8 keyboard regions with a gain matrix ([DAFx-08](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf)). The fit used K = 1,500 sections. A "physically informed" alternative takes 4–32 low partials per key from the string model, "700–1200 resonators up to 10 kHz", which "should be enough… since the sustain-pedal effect is relevant only in the low and middle frequency range". | 4 multiply-adds per resonator per sample. K ≈ 1,000 is "remarkably higher" than reverb-style methods, but can be run at a lower sample rate and is "highly vectorizable" (§4.2). | A global bank assumes all dampers up. Half-pedal and held-key behaviour need per-key grouping. |
| **S4. Reverb-like string register** | About 12 simplified string models for the lowest tones (Lehtonen et al. 2007), or 18 low strings + 10 variable strings (De Poli et al. patent), act as a resonant reverb (both via [Zambon 2008 §1](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf)) | Very cheap | A plausible "wash", but not note-accurate |
| **S5. Commuted / pedal-down body response** | Swap or crossfade to a soundboard response measured with the strings undamped (Van Duyne & Smith 1995, via Zambon 2008 §1) | ~0 extra on top of the convolution | No dependence on which keys are held. Weak half-pedal. Only works in a commuted or convolution architecture. |

### Key insight for the CPU budget

The budget test case, an 88-key sustain glissando, already has every note sounding. In that test, sympathetic resonance doesn't add strings. It only adds the resonator bank. The bank must not be made of full string models (S1), because then *every* pedal-down passage would cost as much as the worst case.

### Cost estimate

Use S2/S3 with about 1,000 resonators (about 12 low partials × 88 keys) at full rate. That is about 3,000–4,000 multiply-adds per sample, or about 500–1,000 cycles per sample vectorized, which is **~3–5% of the budget**. Running the bank at 24 kHz roughly halves that, since the effect is low and mid frequency (Zambon 2008 §3.3, §4.2) **(estimate)**. A 4×4–8×8 region gain matrix is negligible.

### Recommendation

**S2/S3 hybrid, organized per key:** about 8–16 low-partial resonators per key, tuned from that key's own string model (Zambon's "physically informed" calibration, so no measured pedal response is needed). Feed them one-way from the summed bridge-force bus through a 4–8 region gain matrix, and set each key's group input gain and decay from **the same damper state as section 3**. The pedal, half-pedal, held silent keys and key releases then all behave consistently. If the modal unison-coupling modes (section 1, method D) already exist, these can be the *same* resonators, as in Bank 2010. Keep S5 (crossfading to a pedal-down body response) as a cheap fallback if the budget gets tight.

---

## 5. Soft pedal (una corda)

### What it is

On a grand, the soft pedal "shifts the entire action sideways. Consequently, the hammers strike only two strings out of three in the treble, or one out of two in the midrange. This causes only about 1 dB reduction of the sound pressure level, but changes the timbre." There are two reasons:

1. "The third string gains energy from the vibration of the other two. The initial conditions of the coupled vibration are changed, resulting in a slower decay." This is Weinreich 1977's coupled-string physics again.
2. "When the same hammer hits a smaller number of strings, it appears to be heavier with respect to the single strings, causing longer contact times and a softer timbre."

Both quotes are from [Bank 2000 thesis, the section on the piano's pedals](https://home.mit.bme.hu/~bank/thesis/), citing Fletcher & Rossing 1998 and Weinreich 1977. It is also widely reported that the shifted hammer meets a less-compacted part of the felt, which makes it softer still. I didn't find a primary measurement of that, so treat it as a tuning parameter.

### Methods

- **In the coupled-unison model (section 1):** set the hammer's drive to zero on the unstruck string. With coupled modes (method D), that only changes the initial amplitudes of the modes, so the unstruck string's contribution and the slower decay come out of the physics. Bank's thesis does the same in his resonator-bank structure: "feeding the resonators with the output of the digital waveguide through a small coupling coefficient", with the hammer weight "set accordingly" ([Bank 2000 ch. 5, parallel resonator structure](https://home.mit.bme.hu/~bank/thesis/)).
- **In the hammer model (ticket 0002):** raise the hammer mass per struck string, and lower the felt stiffness and nonlinearity a little.
- **Bass single-string notes:** only the felt change applies.
- A continuous pedal position could crossfade between full and shifted excitation. Whether v1 supports partial shift is a product decision.

**Cost:** zero extra DSP. It changes parameters, not structure.

### Recommendation

Model the soft pedal as **(a) a change in the per-string excitation weights inside the coupled unison model, plus (b) a softer, effectively heavier hammer setting**. It is free, and it is physically grounded as long as unison coupling is modelled properly. This is one more reason to prefer real coupled modes (section 1, D) over a signal-level beating trick (section 1, C), which can't represent "the third string rings only through coupling".

---

## 6. Whole-model CPU sanity check

**Assumptions (estimate):** a 2020 mid-range desktop core (Ryzen 5 3600 / Core i5-10400 class) at about 4 GHz with AVX2. At 25% of it, that is about 1.0×10⁹ cycles per second, or **about 20,800 cycles per sample at 48 kHz**. A well-vectorized bank of second-order resonators costs roughly **0.25–1 cycle per resonator per sample**, depending on memory layout and on whether its input is zero.

**Published anchor:** Bank, Zambon & Fontana's full modal piano, with **10,000 second-order resonators plus four 20,000-tap convolutions at full polyphony**, ran at "an approximate load of 30% on a Intel's Core 2 Duo@2.4-GHz laptop" with 4-wide SSE ([§VIII](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf)). A 2020 core is very roughly 4–6× faster per core on this kind of code: about 1.7× the clock, higher instructions per cycle, and 8-wide AVX2 instead of 4-wide SSE **(estimate)**. On that basis, a model of the same size would use roughly 5–15% of one 2020 core, comfortably inside Forte's 25% **(estimate)**. The paper doesn't say whether "30%" means one core or both.

| Part (recommended method) | Cycles / sample (estimate) | % of budget |
|---|---|---|
| Unison coupling: extra modes (about 1,400–2,800) | 700–2,800 | 3–13% |
| Soundboard: stereo convolution + region filters | 300–600 | 1.5–3% |
| Dampers + half-pedal | < 150 | < 1% |
| Sympathetic bank (about 1,000 resonators) | 500–1,000 | 2.5–5% |
| Soft pedal | 0 | 0% |
| **Total for the parts in this ticket** | **about 1,500–4,500** | **about 7–22%** |

These are the same 7–22% the TL;DR gives. They leave about 16,000–19,000 cycles per sample, about 80% of the budget, for the main strings and hammers of 88 notes. That is about 180–215 cycles per note per sample, or about 200–800 single-string modes per note at 0.25–1 cycle each. That is in line with what Bank 2010's Fig. 7 calibration uses **(estimate)**. **We should confirm with a micro-benchmark early** (see New questions).

---

## New questions surfaced (candidate tickets)

1. **Soundboard impulse response source.** Where does a GPL-compatible soundboard response come from? A freely licensed measurement, offline synthesis from a modal or plate soundboard model, or estimation from the Salamander or other reference recordings? This blocks the soundboard part of the spec.
2. **Resonator-bank CPU micro-benchmark.** Measure the cycles per resonator per sample of a SIMD second-order resonator bank, plus one 20k-tap partitioned convolution, on a 2020-class CPU at a 128-sample buffer. This turns every estimate above into a measurement. It feeds "Voice management and CPU strategy" on the map.
3. **Calibrating unison detuning and coupling from recordings.** Fit Weinreich-style coupled-mode parameters per note from measured two-stage decay and beating. This is an extension of ticket 0005's measurements.
4. **Part-pedal and damper reference data.** Do any freely licensed recordings include half-pedal or detailed release samples? If not, half-pedal tuning is by ear only. Related: the mapping from host MIDI CC64 (0–127; only 7-bit in standard VST3/MIDI 1.0 hosts) to the damper "gap", and whether the extra resolution of MIDI 2.0 matters.
5. **Continuous soft-pedal position.** Should the soft pedal be on/off or continuous (partial hammer shift)? This is a product decision.
6. **Does this design hold if 0002 picks waveguide strings?** The recommendations change form (sections 0, 1). Ticket 0007 should check the combined design end to end.

## Sources

- B. Bank, S. Zambon, F. Fontana, "A modal-based real-time piano synthesizer," *IEEE TASLP* 18(4), 809–821, 2010. [Open-access PDF (Univ. Verona)](https://iris.univr.it/retrieve/e14ff6e2-e533-0209-e053-6605fe0ad24c/allegatooa_6366.pdf), [sound examples](https://home.mit.bme.hu/~bank/publist/taslp-piano/index.html)
- S. Zambon, H.-M. Lehtonen, B. Bank, "Simulation of piano sustain-pedal effect by parallel second-order filters," DAFx-08. [PDF](https://dafx.de/paper-archive/2008/papers/dafx08_37.pdf)
- B. Bank, "Physics-based sound synthesis of the piano," M.Sc. thesis, BME, 2000 (HUT Report 54). [Thesis page / PDF](https://home.mit.bme.hu/~bank/thesis/)
- B. Bank, fixed-pole parallel filters. [Page](https://home.mit.bme.hu/~bank/parfilt)
- J. O. Smith, *Physical Audio Signal Processing* (online book): [Commuted piano synthesis](https://ccrma.stanford.edu/~jos/pasp/Commuted_Piano_Synthesis.html), [Coupled piano strings](https://ccrma.stanford.edu/~jos/pasp/Coupled_Piano_Strings.html), [Two ideal strings coupled](https://ccrma.stanford.edu/~jos/pasp/Two_Ideal_Strings_Coupled.html), [Two coupled strings / eigenanalysis](https://ccrma.stanford.edu/~jos/pasp/Two_Coupled_Strings.html), [String coupling effects](https://ccrma.stanford.edu/~jos/pasp/String_Coupling_Effects.html)
- G. Weinreich, "Coupled piano strings," *JASA* 62(6), 1474–1484, 1977, [doi:10.1121/1.381677](https://doi.org/10.1121/1.381677). Lecture summary: [KTH](http://www.speech.kth.se/music/5_lectures/weinreic/weinreic.html)
- H.-M. Lehtonen, A. Askenfelt, V. Välimäki, part-pedaling letter, *JASA* 126(2), EL49, 2009. [PubMed 19640015](https://pubmed.ncbi.nlm.nih.gov/19640015/)
- A. Stulov, V. Välimäki, H.-M. Lehtonen, "Modeling of the part-pedaling effect in the piano," *Proc. Estonian Acad. Sci.*, 2012 (abstract only seen)
- H.-M. Lehtonen, H. Penttinen, J. Rauhala, V. Välimäki, "Analysis and modeling of piano sustain-pedal effects," *JASA* 122, 1787, 2007 (citation via Zambon et al. 2008; full text not read)
- J. Rauhala et al., "The beating equalizer…," DAFx-07. [Archive page](https://dafx.de/paper-archive/details/JFtCSJ3pxmYIqWrznQMSHw)
- J. Chabassier, A. Chaigne, P. Joly, "Modeling and simulation of a grand piano," *JASA* 134(1), 648–665, 2013. [HAL](https://hal-ensta-paris.archives-ouvertes.fr/hal-00873089)
- S. Bilbao, A. Torin, V. Chatziioannou, "Numerical modeling of collisions in musical instruments," [arXiv:1405.2589](https://arxiv.org/abs/1405.2589)
- B. Bank, J. Chabassier, "Model-based digital pianos: from physics to sound synthesis," *IEEE Signal Processing Magazine* 36(1), 2019. [HAL record](https://hal-univ-pau.archives-ouvertes.fr/hal-01894219) (a useful overview; the full text was blocked to my fetcher, so no claims above rely on it)
