#pragma once
//==============================================================================
// Project.h
// Everything that makes up a song, as plain data:
//   * the drum machine: 8 pattern variations (A to H), 8 voices x 16 steps
//   * four instrument tracks, each with an engine, a sound and 8 orb roll
//     pattern variations (2 bars each)
//   * the song: five lanes of 2 bar slots, each slot naming which variation
//     of that track plays there (or nothing)
//
// The UI edits one Project on the message thread. After every edit the
// processor publishes an immutable copy for the audio thread (see
// PluginProcessor::publish), so the two never touch the same memory.
//==============================================================================
#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <tuple>

namespace drumbox
{
constexpr int numPatterns    = 8;     // variations per track: A to H
constexpr int numInstruments = 4;     // tracks A to D
constexpr int numLanes       = 1 + numInstruments;   // drums + instruments
constexpr int slotBeats      = 8;     // one song slot = one 2 bar loop
constexpr int maxSlots       = 64;    // 128 bars: about 4.5 minutes at 112 bpm
constexpr int rollTop        = 72;    // orb roll range: C5 ...
constexpr int rollBottom     = 36;    // ... down to C2

enum class Engine : int { Poly = 0, Tine, Brass, Rom };

inline const char* engineName (Engine e)
{
    switch (e) { case Engine::Poly: return "POLY 6"; case Engine::Tine: return "TINE";
                 case Engine::Brass: return "BRASS 80"; case Engine::Rom: return "ROM 90"; }
    return "";
}

inline const char* patternLetter (int i) { static const char* l[] = { "A","B","C","D","E","F","G","H" }; return l[std::clamp (i, 0, 7)]; }

//==============================================================================
struct Orb
{
    int id = 0;
    int midi = 60;
    double start = 0.0;    // beats from the start of the 2 bar pattern
    double len = 0.25;     // beats; typed values may sit between dividers
    float vel = 0.7f;
    bool chord = false;
    int okKey = -1;        // key signature this out-of-key note was approved for
};

struct Slide
{
    int id = 0, from = 0, to = 0;
    float cx = 0.85f, cy = 0.1f;   // glide curve control point (0..1 each)
    float time = 0.5f;             // fraction of the first note the glide takes
};

struct InstPattern
{
    std::vector<Orb> orbs;
    std::vector<Slide> slides;
    bool empty() const { return orbs.empty(); }
};

struct SoundParams
{
    float cutoff = 0.6f, reso = 0.2f, attack = 0.05f, release = 0.35f, chorus = 0.5f, level = 0.75f, pan = 0.0f;
};

struct InstTrack
{
    Engine engine = Engine::Poly;
    SoundParams sound;
    std::array<InstPattern, numPatterns> patterns;
    int current = 0;      // the variation you're editing (and that plays in loop mode)
    bool mute = false;
};

using DrumGrid = std::array<std::array<uint8_t, 16>, 8>;   // [voice][step], 0 off 1 on 2 accent

struct DrumTrack
{
    std::array<DrumGrid, numPatterns> patterns {};
    int current = 0;
    bool mute = false;
};

struct Song
{
    int slots = 42;   // 84 bars, three minutes at 112 bpm
    std::array<std::array<int8_t, maxSlots>, numLanes> lanes;   // pattern index or -1
    Song() { for (auto& l : lanes) l.fill (-1); }
};

struct Project
{
    DrumTrack drums;
    std::array<InstTrack, numInstruments> inst;
    Song song;
    int keyRoot = 0;
    bool minor = true;
    bool songMode = false;   // false: every track loops its current variation
    int nextId = 1000;

    int keySig() const { return keyRoot + (minor ? 100 : 0); }

    const int* scale() const
    {
        static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
        return minor ? mnr : maj;
    }
    bool inKey (int midi) const
    {
        const int pc = ((midi % 12) - keyRoot + 12) % 12;
        const int* s = scale();
        for (int i = 0; i < 7; ++i) if (s[i] == pc) return true;
        return false;
    }
    int nearestInKey (int midi, int dir) const
    {
        for (int k = 1; k < 12; ++k) if (inKey (midi + dir * k)) return midi + dir * k;
        return midi;
    }

    // Which variation a lane plays at a song slot (or, in loop mode, its current one)
    int patternFor (int lane, int slot) const
    {
        if (! songMode) return lane == 0 ? drums.current : inst[(size_t) lane - 1].current;
        if (slot < 0 || slot >= song.slots) return -1;
        return song.lanes[(size_t) lane][(size_t) slot];
    }

    double songBeats() const { return (double) song.slots * slotBeats; }
};

//==============================================================================
// A starter project: a groove on the drums and a few parts, laid out as a song,
// so the studio makes music the moment it opens.
inline Project makeDemoProject()
{
    Project p;
    const char* groove[8] = { "1000001010100000", "0000100000001000", "0000000000000000", "0000000000002000",
                              "2121212121212101", "0000000000000020", "0000000000000000", "0000000000000000" };
    const char* fill[8]   = { "1000001010100010", "0000100000001011", "0000000100000000", "0000000000002000",
                              "2121212121210000", "0000000000000000", "0000000000001110", "0000000000000000" };
    for (int v = 0; v < 8; ++v)
        for (int s = 0; s < 16; ++s)
        {
            p.drums.patterns[0][(size_t) v][(size_t) s] = (uint8_t) (groove[v][s] - '0');
            p.drums.patterns[1][(size_t) v][(size_t) s] = (uint8_t) (fill[v][s] - '0');
        }

    int id = 1;
    auto orb = [&id] (int midi, double start, double len, float vel, bool chord = false)
    { Orb o; o.id = id++; o.midi = midi; o.start = start; o.len = len; o.vel = vel; o.chord = chord; return o; };

    // A: pad chords (C minor, A flat major)
    auto& pad = p.inst[0].patterns[0];
    for (int m : { 60, 63, 67 }) pad.orbs.push_back (orb (m, 0, 4, 0.7f, true));
    for (int m : { 60, 63, 68 }) pad.orbs.push_back (orb (m, 4, 4, 0.7f, true));

    // B: electric piano line
    p.inst[1].engine = Engine::Tine;
    auto& ep = p.inst[1].patterns[0];
    for (auto [m, s, l] : std::vector<std::tuple<int, double, double>> { { 67, 0.5, 0.5 }, { 70, 1.5, 0.5 }, { 72, 2.5, 1.0 }, { 70, 4.5, 0.5 }, { 68, 5.5, 0.5 }, { 67, 6.5, 1.0 } })
        ep.orbs.push_back (orb (m, s, l, 0.75f));

    // C: brass stabs
    p.inst[2].engine = Engine::Brass;
    auto& brass = p.inst[2].patterns[0];
    for (int m : { 63, 67, 72 }) brass.orbs.push_back (orb (m, 3.5, 0.5, 0.85f, true));
    for (int m : { 63, 68, 72 }) brass.orbs.push_back (orb (m, 7.5, 0.5, 0.85f, true));

    // D: bass with a slide
    p.inst[3].engine = Engine::Rom;
    p.inst[3].sound.cutoff = 0.45f;
    auto& bass = p.inst[3].patterns[0];
    bass.orbs.push_back (orb (36, 0, 1.5, 0.9f));
    bass.orbs.push_back (orb (36, 2, 0.5, 0.7f));
    bass.orbs.push_back (orb (43, 2.75, 0.75, 0.8f));
    bass.orbs.push_back (orb (44, 4, 1.5, 0.9f));
    bass.orbs.push_back (orb (43, 6, 1.0, 0.8f));
    const int slideFrom = bass.orbs.back().id;
    bass.orbs.push_back (orb (36, 7.25, 0.75, 0.8f));
    Slide sl; sl.id = id++; sl.from = slideFrom; sl.to = bass.orbs.back().id; sl.time = 0.6f;
    bass.slides.push_back (sl);
    p.nextId = id + 100;

    // Song: intro, verse, hook, verse, hook, outro
    auto put = [&p] (int lane, int from, int to, int pat) { for (int s = from; s < to; ++s) p.song.lanes[(size_t) lane][(size_t) s] = (int8_t) pat; };
    p.song.slots = 42;
    put (1, 0, 4, 0);                                   // intro: pad alone
    put (0, 4, 20, 0); put (4, 4, 20, 0); put (1, 4, 20, 0); put (2, 8, 20, 0);
    put (3, 12, 20, 0);
    for (int s = 7; s < 20; s += 4) p.song.lanes[0][(size_t) s] = 1;   // drum fill every 4th slot
    put (0, 20, 36, 0); put (4, 20, 36, 0); put (1, 20, 36, 0); put (2, 20, 36, 0); put (3, 24, 36, 0);
    for (int s = 23; s < 36; s += 4) p.song.lanes[0][(size_t) s] = 1;
    put (1, 36, 42, 0); put (2, 36, 40, 0);              // outro
    return p;
}

} // namespace drumbox
