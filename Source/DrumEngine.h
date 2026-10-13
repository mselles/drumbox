#pragma once
//==============================================================================
// DrumEngine.h
// The whole instrument minus the UI and the DAW plumbing:
//   * 8 voices + mixer (level, tune, decay, pan per voice)
//   * "Vintage" stage: bit depth + sample rate reduction for that 12 bit grit
//
// Hits arrive from the sequencer with sample offsets, so timing is sample accurate.
//==============================================================================
#include "DrumVoices.h"
#include <atomic>
#include <memory>
#include <vector>

namespace drumbox
{
constexpr int numVoices = 8;
constexpr int numSteps  = 16;

enum VoiceId { kKick, kSnare, kRim, kClap, kClosedHat, kOpenHat, kTom, kCowbell };

inline const char* voiceName (int v)
{
    static const char* names[numVoices] = { "KICK", "SNARE", "RIM", "CLAP", "CL HAT", "OP HAT", "TOM", "COWBELL" };
    return names[v];
}

// General MIDI drum notes, so a pad controller or the piano roll plays the kit.
inline int voiceForMidiNote (int note)
{
    switch (note)
    {
        case 35: case 36: return kKick;
        case 38: case 40: return kSnare;
        case 37:          return kRim;
        case 39:          return kClap;
        case 42: case 44: return kClosedHat;
        case 46:          return kOpenHat;
        case 41: case 43: case 45: case 47: case 48: case 50: return kTom;
        case 56:          return kCowbell;
        default:          return -1;
    }
}

struct TriggerEvent { int sampleOffset; int voice; float velocity; };

// Step values: 0 = off, 1 = on, 2 = accent
constexpr float normalVelocity = 0.72f;
constexpr float accentVelocity = 1.0f;

//==============================================================================
struct EngineParams
{
    std::array<float, numVoices> level {}, tune {}, decay {}, pan {};
    float swing   = 0.5f;   // 0.50 = straight, 0.66 = triplet feel, up to 0.75
    float vintage = 0.0f;   // 0..1
    float master  = 0.7f;   // linear gain

    EngineParams() { level.fill (0.8f); decay.fill (1.0f); }
};

//==============================================================================
class DrumEngine
{
public:
    DrumEngine()
    {
        voices[kKick]      = std::make_unique<Kick>();
        voices[kSnare]     = std::make_unique<Snare>();
        voices[kRim]       = std::make_unique<Rim>();
        voices[kClap]      = std::make_unique<Clap>();
        voices[kClosedHat] = std::make_unique<Hat> (0.045f);
        voices[kOpenHat]   = std::make_unique<Hat> (0.35f);
        voices[kTom]       = std::make_unique<Tom>();
        voices[kCowbell]   = std::make_unique<Cowbell>();
        events.reserve (1024);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v->prepare (sr);
        holdCounter = 0.0; heldL = heldR = 0.0f;
        outL.reset(); outR.reset();
    }

    //--------------------------------------------------------------------------
    // Queue a hit. The sequencer (Sequencer.h) decides when drums play;
    // this engine only makes the sound.
    void addTrigger (int sampleOffset, int voice, float velocity)
    {
        if (voice >= 0 && voice < numVoices && events.size() < events.capacity())
            events.push_back ({ sampleOffset, voice, velocity });
    }

    void addMidiTrigger (int sampleOffset, int note, float velocity)
    {
        addTrigger (sampleOffset, voiceForMidiNote (note), velocity);
    }

    //--------------------------------------------------------------------------
    // Renders numSamples into L and R (overwrites). Consumes queued events.
    void render (float* L, float* R, int numSamples, const EngineParams& p)
    {
        for (int v = 0; v < numVoices; ++v)
        {
            voices[(size_t) v]->setTune (p.tune[(size_t) v]);
            voices[(size_t) v]->setDecay (p.decay[(size_t) v]);
            // equal power pan, pan in -1..1
            const float a = (p.pan[(size_t) v] + 1.0f) * 0.25f * 3.14159265f;
            gainL[(size_t) v] = std::cos (a) * p.level[(size_t) v];
            gainR[(size_t) v] = std::sin (a) * p.level[(size_t) v];
        }

        std::stable_sort (events.begin(), events.end(),
                          [] (const TriggerEvent& a, const TriggerEvent& b) { return a.sampleOffset < b.sampleOffset; });

        // Vintage stage settings: 16 bits/no hold at 0, down to 8 bits at ~20 kHz at 1
        const float bits = 16.0f - 8.0f * p.vintage;
        const float quant = std::pow (2.0f, bits - 1.0f);
        const double holdLen = p.vintage > 0.001f ? sr / (sr + (20000.0 - sr) * p.vintage) : 1.0;
        // Output smoothing filter, like the reconstruction filters on the old
        // machines' DACs: opens fully at 0, closes to ~7 kHz at full vintage.
        outL.set (19000.0f - 12000.0f * p.vintage, 0.6f, sr);
        outR.set (19000.0f - 12000.0f * p.vintage, 0.6f, sr);

        size_t e = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (e < events.size() && events[e].sampleOffset <= i)
            {
                const auto& ev = events[e++];
                if (ev.voice == kClosedHat) voices[kOpenHat]->choke();   // closed hat chokes open hat
                voices[(size_t) ev.voice]->trigger (ev.velocity);
            }

            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < numVoices; ++v)
            {
                const float s = voices[(size_t) v]->process();
                l += s * gainL[(size_t) v];
                r += s * gainR[(size_t) v];
            }
            l *= p.master; r *= p.master;

            if (p.vintage > 0.001f)
            {
                holdCounter += 1.0;
                if (holdCounter >= holdLen)
                {
                    holdCounter -= holdLen;
                    heldL = std::round (std::clamp (l, -1.0f, 1.0f) * quant) / quant;
                    heldR = std::round (std::clamp (r, -1.0f, 1.0f) * quant) / quant;
                }
                outL.process (heldL); outR.process (heldR);
                l = outL.lp; r = outR.lp;
            }

            L[i] = l; R[i] = r;
        }
        events.clear();
    }

private:
    double sr = 44100.0;
    std::array<std::unique_ptr<Voice>, numVoices> voices;
    std::vector<TriggerEvent> events;
    std::array<float, numVoices> gainL {}, gainR {};
    double holdCounter = 0.0;
    float heldL = 0.0f, heldR = 0.0f;
    SVF outL, outR;
};

} // namespace drumbox
