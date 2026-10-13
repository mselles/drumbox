// RenderTest: runs DrumEngine offline and writes WAV files.
//   demo_groove.wav  four bars of the default groove at 112 bpm
//   each_voice.wav   every drum once, half a second apart
// Usage: RenderTest [output folder]
#include <juce_audio_formats/juce_audio_formats.h>
#include "DrumEngine.h"

using namespace drumbox;

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                              .withSampleRate (sr)
                                              .withNumChannels (b.getNumChannels())
                                              .withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    std::cout << "wrote " << f.getFullPathName() << "  peak " << b.getMagnitude (0, b.getNumSamples()) << "\n";
}

int main (int argc, char* argv[])
{
    const double sr = 48000.0;
    const int block = 256;
    const juce::File out = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    out.createDirectory();

    EngineParams p;
    p.swing = 0.54f;
    p.vintage = argc > 2 ? (float) std::atof (argv[2]) : 0.3f;

    // 1. Groove
    {
        DrumEngine e;
        e.prepare (sr);
        const char* rows[numVoices] = { "1000001010100000", "0000100000001000", "0000000000000000", "0000000000002000",
                                        "2121212121212101", "0000000000000020", "0000000000000000", "0000000000000000" };
        for (int v = 0; v < numVoices; ++v)
            for (int s = 0; s < numSteps; ++s) e.setStep (v, s, rows[v][s] - '0');

        const double bpm = 112.0;
        const int total = (int) (sr * 60.0 / bpm * 16) + (int) sr;   // 4 bars + tail
        juce::AudioBuffer<float> buf (2, total);
        double ppq = 0.0;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            if (ppq < 16.0) e.addSequencerEvents (ppq, bpm, n, p.swing);
            e.render (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), n, p);
            ppq += bpm / 60.0 / sr * n;
        }
        writeWav (out.getChildFile ("demo_groove.wav"), buf, sr);
    }

    // 2. Each voice alone
    {
        DrumEngine e;
        e.prepare (sr);
        const int gap = (int) (sr * 0.5);
        const int total = gap * numVoices + gap;
        juce::AudioBuffer<float> buf (2, total);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            for (int v = 0; v < numVoices; ++v)
                if (const int t = v * gap; t >= pos && t < pos + n)
                {
                    static const int notes[numVoices] = { 36, 38, 37, 39, 42, 46, 45, 56 };
                    e.addMidiTrigger (t - pos, notes[v], 1.0f);
                }
            e.render (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), n, p);
        }
        writeWav (out.getChildFile ("each_voice.wav"), buf, sr);
    }
    return 0;
}
