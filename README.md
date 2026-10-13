# DrumBox Studio

A bite sized, self contained studio as a free, open source plugin: a drum machine inspired by the early 80s digital machines, four synth tracks you write with the **orb roll**, and a song builder that bounces your track to WAV. AU, VST3 and Standalone, built with JUCE and C++.

"DrumBox" is a working name. Rename it in `CMakeLists.txt` (`PRODUCT_NAME`) and the wordmark in `Source/PluginEditor.cpp`.

## The screens

Switch screens with the tabs or the number keys **1 to 6**. **Space** plays and stops. **Cmd/Ctrl + Z** undoes, **Shift + Cmd/Ctrl + Z** redoes, everywhere.

**1 · Drums.** 8 synthesized voices x 16 steps. Click a step for on, again for accent, again for off; drag to paint; right click clears. Click a voice name to edit its tune, decay, level and pan. Swing, vintage (12 bit grit) and master at the bottom.

**2 to 5 · Tracks A to D.** Each track has an engine, a sound and its own orb roll.

| Engine | What it is |
|---|---|
| POLY 6 | Chorused analog poly: saw + pulse + sub through a resonant filter |
| TINE | Electric piano: FM with a metallic tine on the attack, softer when you play softer |
| BRASS 80 | Big poly brass: three detuned saws, slow opening filter, a scoop into each note, delayed vibrato |
| ROM 90 | Digital workstation: an FM body with a pulse layer, bells up high and punchy bass down low |

**6 · Song.** Five lanes, drums plus A to D, laid out in 2 bar slots across the whole song (up to 128 bars, about 4.5 minutes at 112 bpm; the demo is 84 bars, three minutes).

## The orb roll

| Do this | To |
|---|---|
| Click empty space | Add an orb |
| Double click further along a row | Stretch the orb before it out to there |
| Drag an orb | Move it (and everything selected with it) |
| Option/Alt + drag | Change velocity: the orb grows and shrinks |
| Shift + click | Add or remove an orb from the selection |
| Drag on empty space | Box select (Shift adds to the selection) |
| Drag the ring on a selected orb onto another orb | Make a slide |
| Click a slide | Shape it: Linear, Late or Early presets, drag the curve, set the glide time |
| Type in Length | Any number of beats, even between dividers (marked with *) |
| Arrows | Nudge; Shift + up/down moves an octave |
| Delete, Esc, Cmd/Ctrl + A | Delete, deselect, select all |

The roll runs from C5 down to C2; scroll or use **Up / Down** for the bass. Set the **key** at the top. Notes outside the key get a dashed rose ring and a small notice under the roll offering **Keep it** or **move it** to the nearest note in the key. Drag chords from the **chord bucket** onto a row: they land in that octave, and `*` marks the chords that belong to the key.

## Variations and the song

Every track has 8 variations, **A to H**. Click a letter to edit that variation; **Alt + click** a letter to copy the one you're on into it, then change it.

The top bar's **LOOP / SONG** switch decides what plays:
- **LOOP**: every track repeats the variation you're editing. This is how you write.
- **SONG**: each lane plays whatever sits in each slot of the song. This is how you arrange.

On the Song screen, pick a letter in a lane's header (the brush), then click or drag along the lane to paint it in. Alt + drag erases. Right click a slot to choose any variation. **M** mutes a lane. **- / +** change the song's length by 8 bars.

When the DAW is playing, DrumBox follows its tempo and position, so song bar 1 is the DAW's bar 1. On its own, the play button runs the internal clock and the song loops.

## Export

On the Song screen:
- **Export WAV** bounces the song to a 24 bit stereo WAV in the background (much faster than real time), with a couple of seconds for tails to ring out. In LOOP mode, or with an empty song, it bounces four passes of the loop.
- **Export MIDI** writes the arrangement as a MIDI file: drums on channel 10 (General MIDI notes), tracks A to D on channels 1 to 4.

Everything, patterns, sounds and the song, is saved inside your DAW session.

## Build it on your Mac

You need the Xcode Command Line Tools and CMake (the installer from cmake.org is easiest on macOS 13).

```bash
cd drumbox
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first run downloads JUCE
cmake --build build -j4
open build/DrumBox_artefacts/Release/Standalone/DrumBox.app
```

The build copies the AU and VST3 into `~/Library/Audio/Plug-Ins`. Rescan in your DAW. In FL Studio: Options > Manage plugins > Find installed plugins, then star DrumBox.

## Tests

```bash
cmake --build build --target RenderTest UiTest
./build/RenderTest_artefacts/Release/RenderTest ./renders   # bounces the demo song + MIDI, plays each engine
./build/UiTest_artefacts/Release/UiTest ./shots             # screenshots every screen, drives the orb roll
```

## How the code is laid out

| File | What it is |
|---|---|
| `Source/Project.h` | The song as plain data: drum patterns, tracks, orbs, slides, the arrangement, the key |
| `Source/Studio.h` | The sequencer and mixer. Live playback and export both run through it |
| `Source/DrumVoices.h`, `DrumEngine.h` | The eight drum sounds, their mixer and the vintage stage |
| `Source/SynthEngine.h` | The four synth engines, slides, and the stereo chorus |
| `Source/Exporter.h` | WAV bounce and MIDI export |
| `Source/ProjectIO.h` | Saving the project into the DAW session |
| `Source/PluginProcessor.*` | The DAW bridge, undo history, and handing the audio thread a safe copy of every edit |
| `Source/UI/Theme.h` | The palette (deep purple and dark gold) and drawing helpers. Change colors here |
| `Source/UI/OrbRoll.h` | The orb roll |
| `Source/UI/InstrumentPage.h`, `DrumPage.h`, `SongPage.h` | The screens |

The engine code doesn't depend on JUCE's UI, so it can move to an ESP32 or the web later.

## Roadmap

1. **Sample playback** for drum voices, so you can load your own kit.
2. **Slide automation across the song**, for example a glide that gets longer each bar.
3. **Per track effects**: delay and a spring style reverb.
4. **Live MIDI into the instruments**, to play the selected track from a keyboard.
5. **Stem export**, one WAV per lane.
6. **Presets** and a signed, notarized installer.

## On the name and sounds

Describe it as *inspired by* classic machines rather than a clone. LinnDrum, Juno and Rhodes are other companies' trademarks, and every sound here is synthesized, so a free release stays clean.

## License

JUCE is free for open source projects under the GPLv3, so GPLv3 is the simplest license for this project.
