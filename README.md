# DrumBox

A free, open source drum machine plugin inspired by the early 80s digital drum machines. AU, VST3 and Standalone, built with JUCE and C++.

"DrumBox" is a working name. Rename it in `CMakeLists.txt` (`PRODUCT_NAME`, and the `DRUMBOX` title in `Source/PluginEditor.cpp`).

## What it does right now

* **8 synthesized voices:** kick, snare, rim, clap, closed hat, open hat, tom, cowbell. All synthesized, so the project contains no third party samples and you can license it however you like.
* **16 step sequencer** with three states per step: off, on, accent.
* **Swing from 50% to 75%**, applied to every second 16th, the way the classic machines did it. 54% is the default; 66% is a triplet shuffle.
* **Follows your DAW's transport.** When the DAW plays, the pattern locks to its tempo and bar position. With the DAW stopped (or in Standalone), press PLAY to run on the internal clock.
* **Plays from MIDI** using General MIDI drum notes (36 kick, 38 snare, 37 rim, 39 clap, 42 closed hat, 46 open hat, 45 tom, 56 cowbell), so pads and the piano roll work.
* **Per voice mixer:** level, tune (±12 semitones), decay, pan. Closed hat chokes open hat.
* **Vintage knob:** bit depth reduction, sample rate reduction and an output filter, for that 12 bit grit.
* **Saves with your session:** knobs and pattern are stored in the DAW project.

## Build it on your Mac

You need Xcode (from the App Store) and CMake.

```bash
xcode-select --install          # command line tools, if you haven't
brew install cmake              # or download from cmake.org

cd drumbox
cmake -B build -G Xcode         # first run downloads JUCE, takes a minute
cmake --build build --config Release
```

The build copies the plugin into `~/Library/Audio/Plug-Ins/Components` (AU) and `~/Library/Audio/Plug-Ins/VST3`. Rescan plugins in your DAW. In Logic, it shows up under **AU Instruments > Miles Selles > DrumBox**.

The Standalone app lands in `build/DrumBox_artefacts/Release/Standalone/`. That is the fastest way to try changes.

To work in Xcode instead, open `build/DrumBox.xcodeproj`, choose the `DrumBox_Standalone` scheme and hit Run.

### Hear changes without a DAW

```bash
cmake --build build --config Release --target RenderTest
./build/RenderTest_artefacts/Release/RenderTest ./renders
```

That writes `demo_groove.wav` (four bars of the default groove) and `each_voice.wav` (every drum once). Use it when you're tuning a voice: edit `DrumVoices.h`, rebuild, listen.

## How the code is laid out

| File | What it is |
|---|---|
| `Source/DrumVoices.h` | The eight drum sounds. Each is a small class with `trigger()` and `process()`. Start here to change how anything sounds. |
| `Source/DrumEngine.h` | Mixer, sequencer and vintage stage. `addSequencerEvents()` turns a beat position into sample accurate hits, swing included. |
| `Source/PluginProcessor.*` | The DAW bridge: parameters, host tempo, MIDI, saving state. |
| `Source/PluginEditor.*` | The UI: the step grid with its chase light, the knobs, the look. |
| `Tests/RenderTest.cpp` | Offline renderer. |

The engine and voices don't depend on JUCE. That keeps the sound code simple to read, and you could reuse it on an ESP32 or in a web version later.

## Roadmap

Roughly in order of how much they'd change the instrument:

1. **Sample playback.** Let any voice load a WAV instead of the synth. Ship with sounds you record yourself (your own kit in the dorm is perfect source material). The Audio Programmer sampler series covers exactly this.
2. **Multiple patterns and song mode.** 8 or 16 pattern slots, then chain them.
3. **Per step velocity and flam** instead of just accent.
4. **Individual outputs** so each drum gets its own mixer channel in the DAW.
5. **Pattern length** per voice (polyrhythms).
6. **Real UI design pass.** The current look is a functional starting point.
7. **Presets** and a website, GitHub releases and a signed, notarized installer.

## On the name and sounds

Keep describing it as *inspired by* the LinnDrum rather than a clone. LinnDrum is Roger Linn's trademark and the original sounds are copyrighted, so a project that synthesizes or records its own sounds stays clean to release for free.

## License

JUCE is free to use under the GPLv3 for open source projects, so releasing this under GPLv3 is the simplest path. See juce.com for the JUCE license terms.
