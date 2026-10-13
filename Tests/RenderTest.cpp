// RenderTest: exercises the studio offline, with no DAW or UI.
//   demo_song.wav    the demo project's full song, bounced by the real exporter
//   demo_song.mid    the same arrangement as a MIDI file
//   engines.wav      each synth engine playing a phrase with a slide, one after another
// Usage: RenderTest [output folder]
#include <juce_audio_formats/juce_audio_formats.h>
#include "Exporter.h"

using namespace drumbox;

int main (int argc, char* argv[])
{
    const juce::File out = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    out.createDirectory();
    StudioSettings set;
    set.drums.vintage = 0.3f;

    // 1. The demo song through the exporter
    Project song = makeDemoProject();
    song.songMode = true;
    std::atomic<float> progress { 0.0f };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto err = exportWav (song, set, 112.0, 48000.0, out.getChildFile ("demo_song.wav"), &progress);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    if (err.isNotEmpty()) { std::cerr << err << "\n"; return 1; }
    std::cout << "song: " << prepareForExport (song) / 4 << " bars, rendered in " << (int) ms << " ms\n";
    err = exportMidi (song, 112.0, out.getChildFile ("demo_song.mid"));
    if (err.isNotEmpty()) { std::cerr << err << "\n"; return 1; }

    // 2. Each engine on its own: loop mode, drums muted, one track at a time
    juce::AudioBuffer<float> all (2, 0);
    for (int e = 0; e < 4; ++e)
    {
        Project p;
        p.songMode = false;
        p.drums.mute = true;
        auto& tr = p.inst[0];
        tr.engine = (Engine) e;
        int id = 1;
        auto add = [&] (int m, double s, double l, float v) { Orb o; o.id = id++; o.midi = m; o.start = s; o.len = l; o.vel = v; tr.patterns[0].orbs.push_back (o); return o.id; };
        const int base = e == 3 ? 36 : 60;
        add (base, 0, 1, 0.8f); add (base + 3, 1, 0.5f, 0.6f); add (base + 7, 1.5, 0.5f, 0.9f);
        const int a = add (base + 10, 2, 1.5, 0.8f);
        const int b = add (base + 12, 3.5, 0.5, 0.7f);
        Slide sl; sl.id = id++; sl.from = a; sl.to = b; sl.time = 0.7f; tr.patterns[0].slides.push_back (sl);
        for (int m : { 0, 3, 7 }) add (base + m, 4, 3.5, 0.75f);

        Studio studio; studio.prepare (48000.0);
        const int n = (int) (48000.0 * 60.0 / 112.0 * 8) + 24000;
        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        const double spb = 48000.0 * 60.0 / 112.0;
        for (int pos = 0; pos < n; pos += 256)
        {
            const int len = std::min (256, n - pos);
            studio.process (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), len, p, set, pos / spb, 112.0, pos < n - 24000);
        }
        std::cout << engineName ((Engine) e) << " peak " << buf.getMagnitude (0, n) << "\n";
        const int old = all.getNumSamples();
        all.setSize (2, old + n, true, true);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, old, buf, c, 0, n);
    }
    juce::File f = out.getChildFile ("engines.wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
