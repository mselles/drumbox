#pragma once
//==============================================================================
// SynthEngine.h
// The four instrument engines. Each track owns 8 voices; every voice can be any
// engine, so switching a track's engine is instant.
//
//   POLY 6    two oscillators (saw + pulse) and a sub, a resonant low pass with
//             its own envelope, then the track's stereo chorus. The chorused
//             analog poly sound.
//   TINE      two operator FM with a short metallic "tine" partial on the attack.
//             The electric piano bark, softer when you play softer.
//   BRASS 80  three detuned saws through a slow opening filter, a small pitch
//             scoop into each note and delayed vibrato. Big poly brass.
//   ROM 90    a clean FM body plus a thin pulse layer. Bells up high, punchy
//             digital bass down low.
//
// Notes arrive with their length already known (the orb's length), so voices
// release themselves. Slides glide the pitch along the curve drawn in the
// slide editor, during the last part of the note.
//==============================================================================
#include "DrumVoices.h"   // SVF, Noise, decayCoef
#include "Project.h"
#include <vector>

namespace drumbox
{
//==============================================================================
struct NoteEvent
{
    int sampleOffset = 0;
    int midi = 60;
    float vel = 0.7f;
    int lengthSamples = 1000;
    bool hasGlide = false;
    int glideTo = 60;
    float glideTime = 0.5f, cx = 0.85f, cy = 0.1f;
};

inline float midiToHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

// Pitch progress (0..1) at time x (0..1) along the quadratic glide curve
inline float glideCurveY (float cx, float cy, float x)
{
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 20; ++i)
    {
        const float u = 0.5f * (lo + hi);
        const float xu = 2.0f * u * (1.0f - u) * cx + u * u;
        (xu < x ? lo : hi) = u;
    }
    const float u = 0.5f * (lo + hi);
    return 2.0f * u * (1.0f - u) * cy + u * u;
}

// Band limited saw / pulse building block (PolyBLEP), keeps highs clean
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float value = 0.0f, attackInc = 0.0f, decayCoef = 0.0f, sustain = 0.8f, releaseCoef = 0.0f;

    void set (float a, float d, float s, float r, double sr)
    {
        attackInc   = 1.0f / (float) std::max (1.0, a * sr);
        decayCoef   = drumbox::decayCoef (d, sr);
        sustain     = s;
        releaseCoef = drumbox::decayCoef (r, sr);
    }
    void noteOn()  { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }
    float next()
    {
        switch (stage)
        {
            case Attack:  value += attackInc; if (value >= 1.0f) { value = 1.0f; stage = Decay; } break;
            case Decay:   value = sustain + (value - sustain) * decayCoef; if (value - sustain < 1.0e-4f) stage = Sustain; break;
            case Sustain: value = sustain; break;
            case Release: value *= releaseCoef; if (value < 1.0e-5f) { value = 0.0f; stage = Idle; } break;
            case Idle:    break;
        }
        return value;
    }
    bool active() const { return stage != Idle; }
};

//==============================================================================
class SynthVoice
{
public:
    void prepare (double sampleRate) { sr = sampleRate; }
    bool active() const { return amp.active(); }
    int age = 0;

    void start (const NoteEvent& e, Engine eng, const SoundParams& p)
    {
        engine = eng;
        midi = (float) e.midi; vel = e.vel;
        gateLeft = std::max (1, e.lengthSamples);
        age = 0; t = 0;
        ph1 = ph2 = ph3 = phM = phT = 0.0f;
        glide = e.hasGlide;
        if (glide)
        {
            glideTo = (float) e.glideTo;
            glideLen = std::max (1, (int) ((float) e.lengthSamples * std::clamp (e.glideTime, 0.02f, 1.0f)));
            glideStart = e.lengthSamples - glideLen;
            for (int i = 0; i < 64; ++i) curve[(size_t) i] = glideCurveY (e.cx, e.cy, (float) i / 63.0f);
            // a slide connects the two notes, so let this one ring into the next
            gateLeft = e.lengthSamples + (int) (0.02 * sr);
        }

        const float attack  = 0.002f + p.attack * p.attack * 1.5f;
        const float release = 0.02f + p.release * p.release * 3.0f;
        switch (engine)
        {
            case Engine::Poly:  amp.set (attack, 0.4f, 0.8f, release, sr);                         fenv.set (0.004f, 0.35f, 0.25f, release, sr); break;
            case Engine::Tine:  amp.set (0.002f, 2.2f + vel, 0.0f, std::max (0.08f, release), sr); fenv.set (0.001f, 0.5f, 0.0f, 0.1f, sr); break;
            case Engine::Brass: amp.set (std::max (0.03f, attack), 0.5f, 0.85f, release, sr);       fenv.set (0.09f, 0.45f, 0.45f, release, sr); break;
            case Engine::Rom:   amp.set (0.002f, 0.6f, 0.45f, std::max (0.05f, release), sr);       fenv.set (0.001f, 0.25f, 0.1f, 0.1f, sr); break;
        }
        amp.noteOn(); fenv.noteOn();
        cutoffBase = 60.0f * std::pow (300.0f, p.cutoff);       // 60 Hz .. 18 kHz
        resoQ = 0.6f + p.reso * 7.0f;
        filter.reset();
    }

    void stealFade() { amp.releaseCoef = decayCoef (0.004f, sr); amp.noteOff(); fenv.noteOff(); }

    float process()
    {
        if (! amp.active()) return 0.0f;
        if (gateLeft > 0 && --gateLeft == 0) { amp.noteOff(); fenv.noteOff(); }

        float pitch = midi;
        if (glide && t >= glideStart)
        {
            const float x = std::min (1.0f, (float) (t - glideStart) / (float) glideLen);
            const float idx = x * 63.0f;
            const int i0 = (int) idx; const int i1 = std::min (63, i0 + 1);
            const float y = curve[(size_t) i0] + (curve[(size_t) i1] - curve[(size_t) i0]) * (idx - (float) i0);
            pitch = midi + (glideTo - midi) * y;
        }
        ++t; ++age;

        const float a = amp.next(), fe = fenv.next();
        float out = 0.0f;

        switch (engine)
        {
            case Engine::Poly:
            {
                const float f = midiToHz (pitch), dt = f / (float) sr;
                const float dt2 = dt * 1.0035f;
                ph1 += dt; if (ph1 >= 1.0f) ph1 -= 1.0f;
                ph2 += dt2; if (ph2 >= 1.0f) ph2 -= 1.0f;
                ph3 += dt * 0.5f; if (ph3 >= 1.0f) ph3 -= 1.0f;
                const float saw = 2.0f * ph1 - 1.0f - polyBlep (ph1, dt);
                const float pw = 0.5f + 0.18f * std::sin (6.2831853f * (float) t / (float) sr * 0.7f);
                float p2 = ph2 + (1.0f - pw); if (p2 >= 1.0f) p2 -= 1.0f;
                const float pulse = (ph2 < pw ? 1.0f : -1.0f) + polyBlep (ph2, dt2) - polyBlep (p2, dt2);
                const float sub = ph3 < 0.5f ? 0.18f : -0.18f;
                filter.set (std::min (18000.0f, cutoffBase * (1.0f + 3.0f * fe * vel)), resoQ, sr);
                filter.process (0.45f * saw + 0.35f * pulse + sub);
                out = filter.lp;
                break;
            }
            case Engine::Tine:
            {
                const float f = midiToHz (pitch);
                phM += f / (float) sr; if (phM >= 1.0f) phM -= 1.0f;
                phT += f * 14.0f / (float) sr; if (phT >= 1.0f) phT -= 1.0f;
                ph1 += f / (float) sr; if (ph1 >= 1.0f) ph1 -= 1.0f;
                const float bright = 0.4f + 1.2f * (cutoffBase / 18000.0f);
                const float index = (0.6f + 2.6f * vel * vel) * (0.25f + fe) * bright;
                const float mod = std::sin (6.2831853f * phM) * index + std::sin (6.2831853f * phT) * fe * fe * 0.9f * vel;
                out = std::sin (6.2831853f * ph1 + mod) * 0.8f;
                break;
            }
            case Engine::Brass:
            {
                const float scoop = -0.35f * std::max (0.0f, 1.0f - (float) t / (float) (0.07 * sr));
                const float vib = (float) t > 0.35f * (float) sr ? 0.12f * std::sin (6.2831853f * 5.4f * (float) t / (float) sr) : 0.0f;
                const float f = midiToHz (pitch + scoop + vib);
                const float d1 = f / (float) sr, d2 = d1 * 1.0046f, d3 = d1 * 0.9955f;
                ph1 += d1; if (ph1 >= 1.0f) ph1 -= 1.0f;
                ph2 += d2; if (ph2 >= 1.0f) ph2 -= 1.0f;
                ph3 += d3; if (ph3 >= 1.0f) ph3 -= 1.0f;
                const float s = (2.0f * ph1 - 1.0f - polyBlep (ph1, d1)) + (2.0f * ph2 - 1.0f - polyBlep (ph2, d2)) + (2.0f * ph3 - 1.0f - polyBlep (ph3, d3));
                filter.set (std::min (18000.0f, cutoffBase * (0.5f + 5.0f * fe * (0.5f + vel))), resoQ * 0.7f, sr);
                filter.process (s * 0.45f);
                out = filter.lp;
                break;
            }
            case Engine::Rom:
            {
                const float f = midiToHz (pitch);
                phM += 2.0f * f / (float) sr; if (phM >= 1.0f) phM -= 1.0f;
                ph1 += f / (float) sr; if (ph1 >= 1.0f) ph1 -= 1.0f;
                ph2 += f / (float) sr; if (ph2 >= 1.0f) ph2 -= 1.0f;
                const float body = std::sin (6.2831853f * ph1 + std::sin (6.2831853f * phM) * (0.3f + 2.2f * fe * vel));
                float p2 = ph2 + 0.75f; if (p2 >= 1.0f) p2 -= 1.0f;
                const float dt = f / (float) sr;
                const float pulse = (ph2 < 0.25f ? 1.0f : -1.0f) + polyBlep (ph2, dt) - polyBlep (p2, dt);
                filter.set (std::min (18000.0f, cutoffBase * (1.0f + 2.0f * fe)), resoQ * 0.5f, sr);
                filter.process (pulse * 0.25f);
                out = body * 0.7f + filter.lp;
                break;
            }
        }
        return out * a * (0.35f + 0.65f * vel);
    }

private:
    double sr = 44100.0;
    Engine engine = Engine::Poly;
    float midi = 60.0f, vel = 0.7f;
    int gateLeft = 0, t = 0;
    float ph1 = 0, ph2 = 0, ph3 = 0, phM = 0, phT = 0;
    bool glide = false; float glideTo = 60.0f; int glideStart = 0, glideLen = 1;
    std::array<float, 64> curve {};
    float cutoffBase = 2000.0f, resoQ = 0.7f;
    Adsr amp, fenv;
    SVF filter;
};

//==============================================================================
// Stereo chorus in the spirit of the classic bucket brigade ones: two delay
// lines swept in opposite directions, left and right.
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buf.assign ((size_t) (sr * 0.03) + 4, 0.0f);
        w = 0; lfo = 0.0f;
    }
    void process (float in, float mix, float& l, float& r)
    {
        const size_t n = buf.size();
        buf[w] = in;
        lfo += 0.6f / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
        const float tri = 4.0f * std::abs (lfo - 0.5f) - 1.0f;   // -1..1
        auto tap = [&] (float ms)
        {
            const float d = ms * 0.001f * (float) sr;
            float rp = (float) w - d; while (rp < 0) rp += (float) n;
            const size_t i0 = (size_t) rp; const size_t i1 = (i0 + 1) % n; const float fr = rp - (float) i0;
            return buf[i0] + (buf[i1] - buf[i0]) * fr;
        };
        const float wl = tap (7.0f + 3.0f * tri), wr = tap (7.0f - 3.0f * tri);
        l = in * (1.0f - 0.5f * mix) + wl * mix * 0.7f;
        r = in * (1.0f - 0.5f * mix) + wr * mix * 0.7f;
        w = (w + 1) % n;
    }
private:
    double sr = 44100.0;
    std::vector<float> buf;
    size_t w = 0;
    float lfo = 0.0f;
};

//==============================================================================
class SynthTrack
{
public:
    static constexpr int numVoices = 10;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sr);
        chorus.prepare (sr);
        events.reserve (512);
    }
    void addNote (const NoteEvent& e) { if (events.size() < events.capacity()) events.push_back (e); }

    // Adds this track into L and R.
    void render (float* L, float* R, int n, Engine engine, const SoundParams& p, bool muted)
    {
        std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.sampleOffset < b.sampleOffset; });
        const float pa = (p.pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = std::cos (pa) * p.level * 0.8f, gr = std::sin (pa) * p.level * 0.8f;
        size_t e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < events.size() && events[(size_t) e].sampleOffset <= i)
                startVoice (events[e++], engine, p);
            float s = 0.0f;
            for (auto& v : voices) s += v.process();
            float l, r;
            chorus.process (s, engine == Engine::Tine ? p.chorus * 0.4f : p.chorus, l, r);
            if (! muted) { L[i] += std::tanh (l) * gl; R[i] += std::tanh (r) * gr; }
        }
        events.clear();
    }

private:
    void startVoice (const NoteEvent& ev, Engine engine, const SoundParams& p)
    {
        SynthVoice* best = nullptr;
        for (auto& v : voices) if (! v.active()) { best = &v; break; }
        if (best == nullptr)
        {
            best = &voices[0];
            for (auto& v : voices) if (v.age > best->age) best = &v;   // steal the oldest
        }
        best->start (ev, engine, p);
    }

    double sr = 44100.0;
    std::array<SynthVoice, numVoices> voices;
    Chorus chorus;
    std::vector<NoteEvent> events;
};

} // namespace drumbox
