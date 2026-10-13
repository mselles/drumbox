#pragma once
//==============================================================================
// Exporter.h
// Bounces the song to a 24 bit stereo WAV by running a private copy of the
// Studio over the whole arrangement, faster than real time. Also writes the
// arrangement as a standard MIDI file (drums on channel 10, tracks A to D on
// channels 1 to 4) so you can keep working on it in any DAW.
//==============================================================================
#include <juce_audio_formats/juce_audio_formats.h>
#include "Studio.h"

namespace drumbox
{
// Index of the last song slot with anything in it, or -1 for an empty song
inline int lastUsedSlot (const Project& p)
{
    int last = -1;
    for (const auto& lane : p.song.lanes)
        for (int k = 0; k < p.song.slots; ++k) if (lane[(size_t) k] >= 0) last = std::max (last, k);
    return last;
}

// What gets bounced: the song in song mode (if it has anything in it),
// otherwise four passes of the current loop. Adjusts p to match.
inline double prepareForExport (Project& p)
{
    if (p.songMode && lastUsedSlot (p) >= 0) return (lastUsedSlot (p) + 1) * (double) slotBeats;
    p.songMode = false;
    return 4.0 * slotBeats;
}

// Renders to file. progress goes 0..1; set cancel to stop early. Returns an
// empty string on success, otherwise what went wrong.
inline juce::String exportWav (Project p, const StudioSettings& set, double bpm, double sampleRate,
                               const juce::File& file, std::atomic<float>* progress = nullptr,
                               std::atomic<bool>* cancel = nullptr)
{
    const double beats = prepareForExport (p);

    Studio studio;
    studio.prepare (sampleRate);
    const int block = 512;
    const double spb = sampleRate * 60.0 / bpm;
    const juce::int64 musicSamples = (juce::int64) std::ceil (beats * spb);
    const juce::int64 total = musicSamples + (juce::int64) (sampleRate * 2.5);   // let tails ring out

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen()) return "Couldn't write to " + file.getFullPathName();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (sampleRate)
                                                  .withNumChannels (2)
                                                  .withBitsPerSample (24));
    if (writer == nullptr) return "Couldn't create the WAV file.";

    juce::AudioBuffer<float> buf (2, block);
    juce::int64 pos = 0;
    while (pos < total)
    {
        if (cancel != nullptr && cancel->load()) { writer.reset(); file.deleteFile(); return "Export cancelled."; }
        const int n = (int) std::min<juce::int64> (block, total - pos);
        buf.clear();
        const double beat = (double) pos / spb;
        studio.process (buf.getWritePointer (0), buf.getWritePointer (1), n, p, set, beat, bpm, pos < musicSamples);
        writer->writeFromAudioSampleBuffer (buf, 0, n);
        pos += n;
        if (progress != nullptr) progress->store ((float) pos / (float) total);
    }
    return {};
}

inline juce::String exportMidi (Project p, double bpm, const juce::File& file)
{
    constexpr int tpq = 960;
    static const int drumNotes[numVoices] = { 36, 38, 37, 39, 42, 46, 45, 56 };
    const double beats = prepareForExport (p);
    const int slots = (int) std::ceil (beats / slotBeats);

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    mf.addTrack (tempo);

    auto ticks = [] (double beat) { return beat * tpq; };

    juce::MidiMessageSequence drums;
    drums.addEvent (juce::MidiMessage::textMetaEvent (3, "Drums"), 0);
    for (int k = 0; k < slots; ++k)
    {
        const int pat = p.patternFor (0, k);
        if (pat < 0) continue;
        for (int j = 0; j < 32; ++j)
            for (int v = 0; v < numVoices; ++v)
                if (const int s = p.drums.patterns[(size_t) pat][(size_t) v][(size_t) (j % 16)]; s > 0)
                {
                    const double t = k * (double) slotBeats + j * 0.25;
                    drums.addEvent (juce::MidiMessage::noteOn (10, drumNotes[v], (juce::uint8) (s == 2 ? 120 : 90)), ticks (t));
                    drums.addEvent (juce::MidiMessage::noteOff (10, drumNotes[v]), ticks (t + 0.2));
                }
    }
    drums.updateMatchedPairs();
    mf.addTrack (drums);

    for (int tr = 0; tr < numInstruments; ++tr)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String ("Track ") + juce::String::charToString ((juce::juce_wchar) ("ABCD"[tr])) + " " + engineName (p.inst[(size_t) tr].engine)), 0);
        for (int k = 0; k < slots; ++k)
        {
            const int pat = p.patternFor (tr + 1, k);
            if (pat < 0) continue;
            for (const auto& o : p.inst[(size_t) tr].patterns[(size_t) pat].orbs)
            {
                const double t = k * (double) slotBeats + o.start;
                seq.addEvent (juce::MidiMessage::noteOn (tr + 1, o.midi, (juce::uint8) juce::jlimit (1, 127, (int) (o.vel * 127))), ticks (t));
                seq.addEvent (juce::MidiMessage::noteOff (tr + 1, o.midi), ticks (t + o.len));
            }
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (out.failedToOpen() || ! mf.writeTo (out)) return "Couldn't write " + file.getFullPathName();
    return {};
}

} // namespace drumbox
