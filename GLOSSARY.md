# Forte

Forte is a physically modeled piano instrument plugin. Its sound comes from simulating how a piano's parts move, not from playing back recordings.

## The instrument

**Note**:
One of the 88 keys of the piano and everything that sounds when it is played.
_Avoid_: Key (when meaning the sound), voice

**Unison**:
The group of one to three strings that a single hammer strikes for one note.
_Avoid_: Choir, string set

**Hammer**:
The felt-covered mallet that strikes a note's unison. Its hardness shapes the note's brightness.

**Soundboard**:
The large wooden plate that the strings drive through the bridge, which radiates most of the piano's sound.

**Damper**:
The felt pad that rests on a note's strings to silence them when the key is released. The highest notes have none.

**Bridge**:
The wooden rail the strings cross on their way to the soundboard, carrying their vibration into it. Different parts of the bridge (bass, tenor, middle, treble) colour and place the sound differently.

## Playing

**Sustain pedal**:
The pedal that lifts all dampers so notes keep ringing after their keys are released.
_Avoid_: Damper pedal, loud pedal

**Half-pedaling**:
Holding the sustain pedal partway, so dampers only lightly touch the strings.

**Soft pedal**:
The pedal that shifts the hammers so they strike fewer strings of each unison, giving a softer, darker tone.
_Avoid_: Una corda (acceptable as a synonym in technical writing), left pedal

**Touch**:
How hard you have to play for a given loudness, from light (gentle playing comes out loud) to heavy (you must play hard to get loud).
_Avoid_: Velocity curve, curve, sensitivity

**Sympathetic resonance**:
Strings that aren't being played ringing because other strings excite them.

**Restrike**:
Playing a note again while its strings are still ringing, so the hammer strikes moving strings rather than starting a fresh sound.
_Avoid_: Retrigger, new voice

## Sound

**Inharmonicity**:
The stretching of a string's overtones above exact whole-number multiples of its fundamental, caused by string stiffness.

**Two-stage decay**:
A note's loud, fairly quick initial fade followed by a long, quiet ringing tail, caused by the strings of its unison exchanging energy through the bridge.
_Avoid_: Double decay

**Stretch tuning**:
Tuning octaves slightly wider than an exact 2:1, so each note's overtones line up with the notes around it. It leaves the top treble sharp and the low bass flat compared with textbook equal temperament.
_Avoid_: Railsback (that's the name of the resulting curve, not the tuning), stretched octaves

**Unison spread**:
How far apart in pitch the strings of one unison are tuned, which makes the gentle beating that keeps a note alive.
_Avoid_: Detune, chorus

**Phantom partials**:
Extra metallic overtones in loud bass notes, caused by the strings stretching along their length as well as swinging sideways.
_Avoid_: Longitudinal modes (acceptable in technical writing)

**Listening perspective**:
Where the listener is placed relative to the piano. Forte offers two: **Microphones** and **Pianist**.
_Avoid_: Mic position, view, mode

**Microphones** (perspective):
Listening from a close stereo microphone pair over the open lid, the familiar sound of a recorded piano.
_Avoid_: Mics, studio

**Pianist** (perspective):
Listening from the player's seat at the keyboard.
_Avoid_: Player, binaural, performer

**Hardness**:
How hard the hammer felt is, which sets how bright and percussive notes become as you play louder.
_Avoid_: Hammer (when meaning the control), felt hardness, voicing

**Brightness**:
An overall tilt of the sound towards or away from the treble, applied after the strings and soundboard. Separate from Hardness, which changes how the strings are struck.
_Avoid_: Tone, EQ

**Resonance**:
How strongly sympathetic resonance rings, from none, through the real piano's level, to double.
_Avoid_: Sympathetic (as a control name), ambience

**Width**:
How far the piano's sound spreads between the left and right speakers, from mono to full stereo.
_Avoid_: Stereo spread, image

**Reference recording**:
A freely licensed recording of a real grand piano used as the objective comparison for Forte's sound.

**Piano model**:
The complete set of measured and fitted values that describes one particular piano to Forte. v1 has one, the concert grand; other pianos would be other piano models.
_Avoid_: Instrument, preset (a preset is the user's settings, not the piano itself)

**Preset**:
A named snapshot of Forte's character controls that the user can recall. It describes how the piano is set up to sound, not the session it's played in, so it leaves out volume, concert pitch (A4) and the pedals.
_Avoid_: Patch, program, sound (when meaning the saved settings)

**Factory preset**:
A preset that ships inside Forte and can't be changed or deleted. Saving one under a new name makes a **user preset**.

**Voicing**:
Adjusting individual notes' tone (brightness, level, sustain) by ear so the keyboard sounds even, as a piano technician does by working the hammer felt.
_Avoid_: Tweaks, overrides (when meaning the act)

**Voicing pass**:
Listening through the whole keyboard, one octave at a time, to find notes that stand out in brightness, level, sustain or beating, so they can be voiced.

## Releasing

**Release gate**:
A check that must pass before a version of Forte is released.
_Avoid_: Test (too broad), acceptance check (that's the fitting pipeline's own done-criteria)

**Waiver**:
A written decision, made by ear, to release even though a sound-match release gate failed. It records which check failed, by how much, and why.
_Avoid_: Exception, override
