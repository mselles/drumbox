#pragma once
//==============================================================================
// Studio.h
// The whole band: the drum engine, four synth tracks and the sequencer that
// reads the Project and decides what plays when.
//
// Two modes:
//   loop   every track repeats its current variation (what you hear while
//          editing a pattern)
//   song   each lane plays whatever variation sits in each 2 bar slot
//
// The plugin calls process() for every audio block. The exporter calls the
// very same function in a loop, faster than real time, so an exported song
// sounds exactly like playback.
//==============================================================================
#include "DrumEngine.h"
#include "SynthEngine.h"
#include "Project.h"

namespace drumbox
{
struct StudioSettings
{
    EngineParams drums;   // drum mixer and vintage, from the plugin's knobs
    float swing = 0.54f;
    float master = 0.7f;
};

class Studio
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        drums.prepare (sr);
        for (auto& t : tracks) t.prepare (sr);
    }

    DrumEngine& drumEngine() { return drums; }

    // A short note on one track, for previewing while editing
    void preview (int track, int midi, float vel)
    {
        if (track < 0 || track >= numInstruments) return;
        NoteEvent e; e.midi = midi; e.vel = vel; e.lengthSamples = (int) (sr * 0.3);
        tracks[(size_t) track].addNote (e);
    }

    // Renders one block. beat = position in quarter notes at the block's first
    // sample. When not playing, only live MIDI hits and ringing tails sound.
    void process (float* L, float* R, int n, const Project& p, const StudioSettings& set,
                  double beat, double bpm, bool playing)
    {
        if (playing) schedule (p, set, beat, bpm, n);

        drums.render (L, R, n, set.drums);
        if (p.drums.mute) { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); }
        for (size_t t = 0; t < tracks.size(); ++t)
            tracks[t].render (L, R, n, p.inst[t].engine, p.inst[t].sound, p.inst[t].mute);

        for (int i = 0; i < n; ++i)
        {
            L[i] = std::tanh (L[i] * set.master * 1.2f) * 0.9f;
            R[i] = std::tanh (R[i] * set.master * 1.2f) * 0.9f;
        }
    }

    // The 16th step (0..15) the drums are on at a beat, for the chase light
    static int drumStepAt (double beat) { return (int) std::floor (beat / 0.25) % 16; }

private:
    void schedule (const Project& p, const StudioSettings& set, double b0, double bpm, int n)
    {
        const double spb = sr * 60.0 / bpm;               // samples per beat
        const double b1 = b0 + n / spb;
        const double swingOffset = (std::clamp (set.swing, 0.5f, 0.75f) - 0.5) * 0.5;

        const long firstSlot = (long) std::floor (b0 / slotBeats) - 1;
        const long lastSlot  = (long) std::floor (b1 / slotBeats);
        auto offsetOf = [&] (double t) { return std::clamp ((int) ((t - b0) * spb), 0, n - 1); };

        for (long k = firstSlot; k <= lastSlot; ++k)
        {
            if (k < 0) continue;
            if (p.songMode && k >= p.song.slots) continue;
            const double slotStart = (double) k * slotBeats;

            // drums: 32 sixteenths across the 2 bar slot, the pattern repeats each bar
            if (const int pat = p.patternFor (0, (int) k); pat >= 0)
            {
                const auto& grid = p.drums.patterns[(size_t) pat];
                for (int j = 0; j < 32; ++j)
                {
                    const double t = slotStart + j * 0.25 + ((j & 1) ? swingOffset : 0.0);
                    if (t < b0 || t >= b1) continue;
                    for (int v = 0; v < numVoices; ++v)
                        if (const int s = grid[(size_t) v][(size_t) (j % 16)]; s > 0)
                            drums.addTrigger (offsetOf (t), v, s == 2 ? accentVelocity : normalVelocity);
                }
            }

            // instruments
            for (int tr = 0; tr < numInstruments; ++tr)
            {
                const int pat = p.patternFor (tr + 1, (int) k);
                if (pat < 0) continue;
                const auto& pt = p.inst[(size_t) tr].patterns[(size_t) pat];
                for (const auto& o : pt.orbs)
                {
                    const double t = slotStart + o.start;
                    if (t < b0 || t >= b1) continue;
                    NoteEvent e;
                    e.sampleOffset = offsetOf (t);
                    e.midi = o.midi; e.vel = o.vel;
                    e.lengthSamples = std::max (1, (int) (o.len * spb));
                    for (const auto& s : pt.slides)
                        if (s.from == o.id)
                            for (const auto& to : pt.orbs)
                                if (to.id == s.to) { e.hasGlide = true; e.glideTo = to.midi; e.glideTime = s.time; e.cx = s.cx; e.cy = s.cy; }
                    tracks[(size_t) tr].addNote (e);
                }
            }
        }
    }

    double sr = 44100.0;
    DrumEngine drums;
    std::array<SynthTrack, numInstruments> tracks;
};

} // namespace drumbox
