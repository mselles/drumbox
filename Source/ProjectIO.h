#pragma once
//==============================================================================
// ProjectIO.h
// Saves a Project into a JUCE ValueTree (and back), so the whole studio,
// patterns, sounds and the song, is stored in your DAW session.
//==============================================================================
#include <juce_data_structures/juce_data_structures.h>
#include "Project.h"

namespace drumbox
{
inline juce::ValueTree projectToTree (const Project& p)
{
    using juce::ValueTree; using juce::String;
    ValueTree root ("PROJECT");
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("keyRoot", p.keyRoot, nullptr);
    root.setProperty ("minor", p.minor, nullptr);
    root.setProperty ("songMode", p.songMode, nullptr);
    root.setProperty ("nextId", p.nextId, nullptr);

    ValueTree drums ("DRUMS");
    drums.setProperty ("current", p.drums.current, nullptr);
    drums.setProperty ("mute", p.drums.mute, nullptr);
    for (int i = 0; i < numPatterns; ++i)
    {
        String s;
        for (auto& row : p.drums.patterns[(size_t) i]) for (auto v : row) s << (int) v;
        drums.setProperty (juce::Identifier ("p" + String (i)), s, nullptr);
    }
    root.appendChild (drums, nullptr);

    for (int t = 0; t < numInstruments; ++t)
    {
        const auto& tr = p.inst[(size_t) t];
        ValueTree tn ("TRACK");
        tn.setProperty ("engine", (int) tr.engine, nullptr);
        tn.setProperty ("current", tr.current, nullptr);
        tn.setProperty ("mute", tr.mute, nullptr);
        const auto& s = tr.sound;
        tn.setProperty ("sound", String (s.cutoff) + " " + String (s.reso) + " " + String (s.attack) + " " + String (s.release) + " "
                                  + String (s.chorus) + " " + String (s.level) + " " + String (s.pan), nullptr);
        for (int i = 0; i < numPatterns; ++i)
        {
            const auto& pt = tr.patterns[(size_t) i];
            if (pt.empty()) continue;
            ValueTree pn ("PATTERN");
            pn.setProperty ("index", i, nullptr);
            for (const auto& o : pt.orbs)
            {
                ValueTree on ("ORB");
                on.setProperty ("id", o.id, nullptr); on.setProperty ("midi", o.midi, nullptr);
                on.setProperty ("start", o.start, nullptr); on.setProperty ("len", o.len, nullptr);
                on.setProperty ("vel", o.vel, nullptr); on.setProperty ("chord", o.chord, nullptr);
                on.setProperty ("okKey", o.okKey, nullptr);
                pn.appendChild (on, nullptr);
            }
            for (const auto& sl : pt.slides)
            {
                ValueTree sn ("SLIDE");
                sn.setProperty ("id", sl.id, nullptr); sn.setProperty ("from", sl.from, nullptr); sn.setProperty ("to", sl.to, nullptr);
                sn.setProperty ("cx", sl.cx, nullptr); sn.setProperty ("cy", sl.cy, nullptr); sn.setProperty ("time", sl.time, nullptr);
                pn.appendChild (sn, nullptr);
            }
            tn.appendChild (pn, nullptr);
        }
        root.appendChild (tn, nullptr);
    }

    ValueTree song ("SONG");
    song.setProperty ("slots", p.song.slots, nullptr);
    for (int l = 0; l < numLanes; ++l)
    {
        String s;
        for (int k = 0; k < maxSlots; ++k)
        {
            const int v = p.song.lanes[(size_t) l][(size_t) k];
            s << (v < 0 ? juce::String (".") : juce::String (patternLetter (v)));
        }
        song.setProperty (juce::Identifier ("lane" + String (l)), s, nullptr);
    }
    root.appendChild (song, nullptr);
    return root;
}

inline bool treeToProject (const juce::ValueTree& root, Project& p)
{
    if (! root.hasType ("PROJECT")) return false;
    Project q;
    q.keyRoot = root.getProperty ("keyRoot", 0);
    q.minor = root.getProperty ("minor", true);
    q.songMode = root.getProperty ("songMode", false);
    q.nextId = root.getProperty ("nextId", 1000);

    const auto drums = root.getChildWithName ("DRUMS");
    q.drums.current = juce::jlimit (0, numPatterns - 1, (int) drums.getProperty ("current", 0));
    q.drums.mute = drums.getProperty ("mute", false);
    for (int i = 0; i < numPatterns; ++i)
    {
        const auto s = drums.getProperty (juce::Identifier ("p" + juce::String (i))).toString();
        for (int v = 0; v < 8; ++v)
            for (int st = 0; st < 16; ++st)
            {
                const int idx = v * 16 + st;
                q.drums.patterns[(size_t) i][(size_t) v][(size_t) st] = (uint8_t) (idx < s.length() ? juce::jlimit (0, 2, s[idx] - '0') : 0);
            }
    }

    int t = 0;
    for (const auto tn : root)
    {
        if (! tn.hasType ("TRACK") || t >= numInstruments) continue;
        auto& tr = q.inst[(size_t) t++];
        tr.engine = (Engine) juce::jlimit (0, 3, (int) tn.getProperty ("engine", 0));
        tr.current = juce::jlimit (0, numPatterns - 1, (int) tn.getProperty ("current", 0));
        tr.mute = tn.getProperty ("mute", false);
        juce::StringArray sv; sv.addTokens (tn.getProperty ("sound").toString(), " ", "");
        if (sv.size() == 7)
            tr.sound = { sv[0].getFloatValue(), sv[1].getFloatValue(), sv[2].getFloatValue(), sv[3].getFloatValue(),
                         sv[4].getFloatValue(), sv[5].getFloatValue(), sv[6].getFloatValue() };
        for (const auto pn : tn)
        {
            const int i = pn.getProperty ("index", -1);
            if (i < 0 || i >= numPatterns) continue;
            auto& pt = tr.patterns[(size_t) i];
            for (const auto c : pn)
            {
                if (c.hasType ("ORB"))
                {
                    Orb o; o.id = c.getProperty ("id"); o.midi = juce::jlimit (rollBottom, rollTop, (int) c.getProperty ("midi"));
                    o.start = c.getProperty ("start"); o.len = c.getProperty ("len"); o.vel = (float) (double) c.getProperty ("vel");
                    o.chord = c.getProperty ("chord"); o.okKey = c.getProperty ("okKey", -1);
                    pt.orbs.push_back (o);
                }
                else if (c.hasType ("SLIDE"))
                {
                    Slide s; s.id = c.getProperty ("id"); s.from = c.getProperty ("from"); s.to = c.getProperty ("to");
                    s.cx = (float) (double) c.getProperty ("cx"); s.cy = (float) (double) c.getProperty ("cy"); s.time = (float) (double) c.getProperty ("time");
                    pt.slides.push_back (s);
                }
            }
        }
    }

    const auto song = root.getChildWithName ("SONG");
    q.song.slots = juce::jlimit (1, maxSlots, (int) song.getProperty ("slots", 42));
    for (int l = 0; l < numLanes; ++l)
    {
        const auto s = song.getProperty (juce::Identifier ("lane" + juce::String (l))).toString();
        for (int k = 0; k < maxSlots && k < s.length(); ++k)
        {
            const auto ch = s[k];
            q.song.lanes[(size_t) l][(size_t) k] = (int8_t) (ch >= 'A' && ch <= 'H' ? ch - 'A' : -1);
        }
    }
    p = std::move (q);
    return true;
}

} // namespace drumbox
