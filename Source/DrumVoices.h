#pragma once
//==============================================================================
// DrumVoices.h
// Eight synthesized drum voices in the spirit of an early 80s digital drum
// machine. They are synthesized rather than sampled so the project ships with
// no third party audio and stays fully open source. You can swap any of these
// for a sample player later (see README, "Roadmap").
//
// No JUCE in this file on purpose: plain C++ is easier to test and reason about.
//==============================================================================
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>

namespace drumbox
{
constexpr float twoPi = 6.28318530718f;

// Time constant -> per sample multiplier for an exponential decay.
inline float decayCoef (float seconds, double sr)
{
    return (float) std::exp (-1.0 / (std::max (0.0005, (double) seconds) * sr));
}

inline float semitonesToRatio (float st) { return std::pow (2.0f, st / 12.0f); }

//==============================================================================
// Tiny white noise source (xorshift). Realtime safe, no allocation.
struct Noise
{
    uint32_t state = 0x12345678u;
    float next()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
};

//==============================================================================
// State variable filter (Zavalishin TPT form). One filter gives LP, BP and HP.
struct SVF
{
    float g = 0, k = 1, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void set (float cutoffHz, float q, double sr)
    {
        cutoffHz = std::clamp (cutoffHz, 20.0f, (float) (sr * 0.45));
        g = (float) std::tan (3.14159265359 * cutoffHz / sr);
        k = 1.0f / std::max (0.1f, q);
    }
    void reset() { ic1 = ic2 = 0; }
    void process (float x)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + g * a1 * v3;
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

//==============================================================================
// Base class. tune is in semitones, decay is a multiplier on the voice's
// natural length (1.0 = default).
struct Voice
{
    virtual ~Voice() = default;
    virtual void prepare (double sampleRate) { sr = sampleRate; }
    virtual void trigger (float velocity) = 0;
    virtual float process() = 0;           // one mono sample
    virtual void choke() { amp = 0.0f; }

    void setTune (float st)   { tuneRatio = semitonesToRatio (st); }
    void setDecay (float m)   { decayMul = std::clamp (m, 0.1f, 4.0f); }
    bool isActive() const     { return amp > 1.0e-4f; }

protected:
    double sr = 44100.0;
    float tuneRatio = 1.0f, decayMul = 1.0f;
    float amp = 0.0f, ampCoef = 0.0f, vel = 1.0f;
    Noise noise;
};

//==============================================================================
struct Kick : Voice
{
    float phase = 0, pitchEnv = 0, pitchCoef = 0, click = 0, clickCoef = 0;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; pitchEnv = 1.0f; click = 1.0f; phase = 0.0f;
        ampCoef   = decayCoef (0.32f * decayMul, sr);
        pitchCoef = decayCoef (0.035f, sr);
        clickCoef = decayCoef (0.002f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        const float f = (48.0f + 110.0f * pitchEnv) * tuneRatio;
        phase += f / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
        float s = std::sin (twoPi * phase) * amp + noise.next() * click * 0.25f;
        amp *= ampCoef; pitchEnv *= pitchCoef; click *= clickCoef;
        return std::tanh (2.0f * s) * 1.25f * vel;
    }
};

//==============================================================================
struct Snare : Voice
{
    float p1 = 0, p2 = 0, noiseAmp = 0, noiseCoef = 0;
    SVF hp, lp;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; noiseAmp = 1.0f; p1 = p2 = 0.0f;
        lp.set (8000.0f * tuneRatio, 0.7f, sr);
        ampCoef   = decayCoef (0.05f * decayMul, sr);
        noiseCoef = decayCoef (0.11f * decayMul, sr);
        hp.set (1800.0f * tuneRatio, 0.7f, sr);
    }
    float process() override
    {
        if (! isActive() && noiseAmp < 1.0e-4f) return 0.0f;
        p1 += 185.0f * tuneRatio / (float) sr; if (p1 >= 1) p1 -= 1;
        p2 += 330.0f * tuneRatio / (float) sr; if (p2 >= 1) p2 -= 1;
        const float tone = (std::sin (twoPi * p1) + 0.6f * std::sin (twoPi * p2)) * amp * 0.6f;
        hp.process (noise.next());
        lp.process (hp.hp);
        const float n = lp.lp * noiseAmp * 0.55f;
        amp *= ampCoef; noiseAmp *= noiseCoef;
        return (tone + n) * vel;
    }
    void choke() override { amp = noiseAmp = 0.0f; }
};

//==============================================================================
struct Rim : Voice
{
    float phase = 0;
    SVF bp;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; phase = 0.0f;
        ampCoef = decayCoef (0.012f * decayMul, sr);
        bp.set (2500.0f * tuneRatio, 3.0f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        phase += 1700.0f * tuneRatio / (float) sr; if (phase >= 1) phase -= 1;
        bp.process (noise.next());
        const float s = (std::sin (twoPi * phase) * 0.7f + bp.bp * 1.5f) * amp;
        amp *= ampCoef;
        return s * vel;
    }
};

//==============================================================================
// Hand clap: three fast noise bursts, then a longer tail.
struct Clap : Voice
{
    int sampleCount = 0;
    float burstCoef = 0;
    SVF bp;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; sampleCount = 0;
        burstCoef = decayCoef (0.006f, sr);
        ampCoef   = decayCoef (0.13f * decayMul, sr);
        bp.set (1150.0f * tuneRatio, 1.4f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        const int burst = (int) (0.011 * sr);   // 11 ms between hands
        float env;
        if (sampleCount < burst * 3)
            env = std::pow (burstCoef, (float) (sampleCount % burst));
        else
        {
            amp *= ampCoef;
            env = amp;
        }
        ++sampleCount;
        bp.process (noise.next());
        return bp.bp * env * 2.2f * vel;
    }
};

//==============================================================================
// Metallic hats: six detuned square waves (the classic analog trick) through
// a band pass and high pass. Closed and open share the design, differ in decay.
struct Hat : Voice
{
    explicit Hat (float baseDecaySeconds) : baseDecay (baseDecaySeconds) {}

    std::array<float, 6> phases {};
    SVF bp, hp;
    float baseDecay;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f;
        ampCoef = decayCoef (baseDecay * decayMul, sr);
        bp.set (10000.0f * tuneRatio, 1.2f, sr);
        hp.set (7000.0f * tuneRatio, 0.7f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        static constexpr float freqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float sum = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            phases[(size_t) i] += freqs[i] * 1.7f * tuneRatio / (float) sr;
            if (phases[(size_t) i] >= 1.0f) phases[(size_t) i] -= 1.0f;
            sum += phases[(size_t) i] < 0.5f ? 1.0f : -1.0f;
        }
        bp.process (sum * 0.2f + noise.next() * 0.15f);
        hp.process (bp.bp);
        const float s = hp.hp * amp * 2.0f;
        amp *= ampCoef;
        return s * vel;
    }
};

//==============================================================================
struct Tom : Voice
{
    float phase = 0, pitchEnv = 0, pitchCoef = 0;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; pitchEnv = 1.0f; phase = 0.0f;
        ampCoef   = decayCoef (0.22f * decayMul, sr);
        pitchCoef = decayCoef (0.08f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        const float f = (110.0f + 60.0f * pitchEnv) * tuneRatio;
        phase += f / (float) sr; if (phase >= 1) phase -= 1;
        const float s = std::sin (twoPi * phase) * amp + noise.next() * amp * pitchEnv * 0.05f;
        amp *= ampCoef; pitchEnv *= pitchCoef;
        return std::tanh (1.3f * s) * vel;
    }
};

//==============================================================================
struct Cowbell : Voice
{
    float p1 = 0, p2 = 0, fast = 0, fastCoef = 0;
    SVF bp;

    void trigger (float v) override
    {
        vel = v; amp = 1.0f; fast = 1.0f; p1 = p2 = 0.0f;
        ampCoef  = decayCoef (0.18f * decayMul, sr);
        fastCoef = decayCoef (0.015f, sr);
        bp.set (2600.0f * tuneRatio, 1.0f, sr);
    }
    float process() override
    {
        if (! isActive()) return 0.0f;
        p1 += 587.0f * tuneRatio / (float) sr; if (p1 >= 1) p1 -= 1;
        p2 += 845.0f * tuneRatio / (float) sr; if (p2 >= 1) p2 -= 1;
        const float sq = (p1 < 0.5f ? 1.0f : -1.0f) + (p2 < 0.5f ? 1.0f : -1.0f);
        bp.process (sq);
        const float env = amp * 0.6f + fast * 0.4f;
        amp *= ampCoef; fast *= fastCoef;
        return bp.bp * env * 0.9f * vel;
    }
};

} // namespace drumbox
