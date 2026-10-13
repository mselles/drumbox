#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace ui
{
//==============================================================================
// The orb roll: a piano roll with orbs instead of rectangles.
//
//   click empty space      add an orb (waits a beat in case it's a double click)
//   double click ahead     stretch the nearest orb on that row out to there
//   drag an orb            move it (moves the whole selection)
//   alt + drag             velocity: orbs grow and shrink
//   shift + click          add to / remove from the selection
//   drag on empty space    box select (shift adds to the selection)
//   drag the ring          from a selected orb to another orb: a slide
//   click a slide          select it to shape it in the inspector
//   arrows                 nudge; shift + up/down moves an octave
//   delete, cmd/ctrl + A, esc
//
// 37 rows (C5 down to C2) x 32 dividers (2 bars of 16ths). The roll lives in a
// Viewport so it scrolls down into the bass.
class OrbRoll : public juce::Component,
                public juce::DragAndDropTarget,
                private juce::Timer
{
public:
    static constexpr int LBL = 56, STEP = 28, ROW = 32, NROWS = drumbox::rollTop - drumbox::rollBottom + 1;
    static constexpr int GRID_W = 32 * STEP;

    OrbRoll (DrumBoxProcessor& p, int trackIndex) : proc (p), track (trackIndex)
    {
        setSize (LBL + GRID_W, NROWS * ROW);
        setWantsKeyboardFocus (true);
    }

    std::function<void()> onChange;   // selection or content changed
    std::vector<int> sel;
    int slideSel = -1;

    drumbox::InstTrack& trk() { return proc.project.inst[(size_t) track]; }
    drumbox::InstPattern& pat() { return trk().patterns[(size_t) trk().current]; }
    drumbox::Orb* orbById (int id) { for (auto& o : pat().orbs) if (o.id == id) return &o; return nullptr; }
    drumbox::Slide* slideById (int id) { for (auto& s : pat().slides) if (s.id == id) return &s; return nullptr; }
    bool isSel (int id) const { return std::find (sel.begin(), sel.end(), id) != sel.end(); }

    void clearSelection() { sel.clear(); slideSel = -1; notify(); }
    void patternSwitched() { sel.clear(); slideSel = -1; link = -1; marquee = false; repaint(); }

    void deleteSelection()
    {
        if (sel.empty() && slideSel < 0) return;
        proc.checkpoint();
        auto& p = pat();
        if (! sel.empty())
        {
            p.orbs.erase (std::remove_if (p.orbs.begin(), p.orbs.end(), [this] (const drumbox::Orb& o) { return isSel (o.id); }), p.orbs.end());
            p.slides.erase (std::remove_if (p.slides.begin(), p.slides.end(), [this] (const drumbox::Slide& s) { return isSel (s.from) || isSel (s.to); }), p.slides.end());
            sel.clear();
        }
        else
        {
            p.slides.erase (std::remove_if (p.slides.begin(), p.slides.end(), [this] (const drumbox::Slide& s) { return s.id == slideSel; }), p.slides.end());
            slideSel = -1;
        }
        commit();
    }

    void commit() { proc.changed(); notify(); }
    void notify() { repaint(); if (onChange) onChange(); }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto& P = proc.project;
        // lanes and labels
        for (int r = 0; r < NROWS; ++r)
        {
            const int m = drumbox::rollTop - r;
            const bool ik = P.inKey (m), root = (m % 12) == P.keyRoot, isC = m % 12 == 0;
            auto lane = juce::Rectangle<int> (LBL, r * ROW, GRID_W, ROW);
            g.setColour (root ? c::accent.withAlpha (0.10f) : ik ? (r % 2 ? c::laneAlt : c::lane) : c::deepTop);
            g.fillRect (lane);
            if (! ik)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                for (int x = -ROW; x < GRID_W; x += 7) g.drawLine ((float) (LBL + x), (float) (r * ROW + ROW), (float) (LBL + x + ROW), (float) (r * ROW), 1.0f);
            }
            if (isC) { g.setColour (c::lineStrong); g.fillRect (0, r * ROW + ROW - 1, LBL + GRID_W, 1); }
            g.setFont (mono (11.0f));
            g.setColour (root ? c::accentHi : ik ? c::textSoft : c::faint);
            g.drawText (noteName (m), 0, r * ROW, LBL - 10, ROW, juce::Justification::centredRight);
        }
        for (int i = 0; i <= 32; ++i)
        {
            g.setColour (i % 16 == 0 ? c::lineStrong : i % 4 == 0 ? c::line : c::line.withAlpha (0.45f));
            g.fillRect (LBL + i * STEP, 0, i % 16 == 0 ? 2 : 1, getHeight());
        }

        g.saveState();
        g.reduceClipRegion (LBL, 0, GRID_W, getHeight());

        // slides
        for (const auto& s : pat().slides)
        {
            juce::Point<float> a, b;
            if (! slideEnds (s, a, b)) continue;
            auto path = slidePath (a, b);
            const bool on = s.id == slideSel;
            if (on) { g.setColour (c::accent.withAlpha (0.25f)); g.strokePath (path, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            g.setColour (c::accentHi);
            if (on) g.strokePath (path, juce::PathStrokeType (3.0f));
            else
            {
                juce::Path dashed; const float dl[] = { 6.0f, 5.0f };
                juce::PathStrokeType (2.5f).createDashedStroke (dashed, path, dl, 2);
                g.fillPath (dashed);
            }
            g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre (b));
        }
        if (link >= 0)
        {
            if (auto* o = orbById (link))
            {
                const auto h = handleCentre (*o);
                juce::Path p; p.startNewSubPath (h);
                p.cubicTo (h.x + 40, h.y, linkAt.x - 40, linkAt.y, linkAt.x, linkAt.y);
                juce::Path dashed; const float dl[] = { 4.0f, 4.0f };
                juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dl, 2);
                g.setColour (c::accentHi); g.fillPath (dashed);
            }
        }

        // orbs
        const int sig = P.keySig();
        for (const auto& o : pat().orbs)
        {
            const auto b = orbBox (o);
            const bool selected = isSel (o.id), flagged = ! P.inKey (o.midi) && o.okKey != sig;
            if (flagged)
            {
                juce::Path ring; ring.addRoundedRectangle (b.expanded (4.0f), b.getHeight() * 0.5f + 4.0f);
                juce::Path dashed; const float dl[] = { 4.0f, 3.0f };
                juce::PathStrokeType (2.0f).createDashedStroke (dashed, ring, dl, 2);
                g.setColour (c::warn); g.fillPath (dashed);
            }
            juce::DropShadow (juce::Colour (0x88000000), 6, { 0, 2 }).drawForRectangle (g, b.toNearestInt());
            juce::Colour top, bot, txt;
            if (selected)              { top = c::accentHi; bot = c::accent; txt = juce::Colour (0xff1c1027); }
            else if (o.chord || o.len >= 2.0) { top = juce::Colour (0xff8a68b5); bot = juce::Colour (0xff5b3f7e); txt = c::text; }
            else                       { top = juce::Colour (0xfff6efe2); bot = juce::Colour (0xffb2a7c4); txt = juce::Colour (0xff1c1027); }
            g.setGradientFill (juce::ColourGradient (top, b.getX() + b.getWidth() * 0.3f, b.getY(), bot, b.getX(), b.getBottom(), false));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            if (selected) { g.setColour (c::accentHi.withAlpha (0.35f)); g.drawRoundedRectangle (b.expanded (3.0f), b.getHeight() * 0.5f + 3.0f, 2.0f); }
            if (b.getWidth() > 30)
            {
                const bool offGrid = std::abs (o.len / 0.25 - std::round (o.len / 0.25)) > 1.0e-3;
                g.setFont (mono (11.0f)); g.setColour (txt);
                g.drawText (fmt (o.len) + (offGrid ? juce::String (" *") : juce::String()), b.toNearestInt().withTrimmedLeft (9), juce::Justification::centredLeft, false);
            }
        }

        // slide handle on a single selected orb
        if (sel.size() == 1)
            if (auto* o = orbById (sel[0]))
            {
                const auto h = handleCentre (*o);
                auto r = juce::Rectangle<float> (20, 20).withCentre (h);
                g.setColour (c::deepTop); g.fillEllipse (r);
                g.setColour (c::accentHi); g.drawEllipse (r.reduced (1.2f), 2.4f);
                juce::Path arrow; arrow.startNewSubPath (h.x - 4, h.y); arrow.lineTo (h.x + 3.5f, h.y);
                arrow.startNewSubPath (h.x + 0.5f, h.y - 3); arrow.lineTo (h.x + 3.5f, h.y); arrow.lineTo (h.x + 0.5f, h.y + 3);
                g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

        if (marquee)
        {
            auto r = juce::Rectangle<float> (marqueeA, marqueeB);
            g.setColour (c::accent.withAlpha (0.12f)); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (c::accentHi); g.drawRoundedRectangle (r, 5.0f, 1.5f);
        }

        // playhead
        const double beat = proc.playBeat.load();
        if (beat >= 0)
        {
            const double local = std::fmod (beat, (double) drumbox::slotBeats);
            const float x = (float) (LBL + local / 0.25 * STEP);
            g.setColour (c::accentHi.withAlpha (0.3f)); g.fillRect (x - 3.0f, 0.0f, 6.0f, (float) getHeight());
            g.setColour (c::accentHi); g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }
        g.restoreState();
    }

    //--------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pt = e.position;
        downAt = pt;
        moved = false;
        if (pt.x < LBL) return;

        if (sel.size() == 1)
            if (auto* o = orbById (sel[0]))
                if (pt.getDistanceFrom (handleCentre (*o)) < 14.0f) { link = o->id; linkAt = pt; repaint(); return; }

        const auto c = cellAt (pt);
        auto* hitOrb = hit (pt);

        if (e.getNumberOfClicks() >= 2 && hitOrb == nullptr && ! e.mods.isShiftDown())
        {
            stopTimer(); pendingCreate = false;
            drumbox::Orb* left = nullptr;
            for (auto& o : pat().orbs) if (o.midi == c.midi && o.start < c.beat - 1.0e-6 && (left == nullptr || o.start > left->start)) left = &o;
            if (left != nullptr)
            {
                proc.checkpoint();
                left->len = c.beat + 0.25 - left->start;
                sel = { left->id }; slideSel = -1;
                commit();
            }
            return;
        }

        if (hitOrb != nullptr)
        {
            const int id = hitOrb->id;
            if (e.mods.isShiftDown())
            {
                if (isSel (id)) sel.erase (std::remove (sel.begin(), sel.end(), id), sel.end()); else sel.push_back (id);
                slideSel = -1; notify(); return;
            }
            if (! isSel (id)) sel = { id };
            slideSel = -1;
            dragAnchor = id;
            dragVelocity = e.mods.isAltDown();
            origins.clear();
            for (const auto& o : pat().orbs) if (isSel (o.id)) origins.push_back ({ o.id, o.start, o.midi, o.vel });
            dragging = true;
            notify();
            return;
        }

        if (auto* s = hitSlide (pt)) { slideSel = s->id; sel.clear(); notify(); return; }

        pendingCreate = true;
        pendingShift = e.mods.isShiftDown();
        pendingCell = c;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto pt = e.position;
        if (link >= 0) { linkAt = pt; repaint(); return; }

        if (pendingCreate && pt.getDistanceFrom (downAt) > 5.0f)
        {
            pendingCreate = false;
            marquee = true;
            marqueeBase = pendingShift ? sel : std::vector<int>{};
        }
        if (marquee)
        {
            marqueeA = downAt; marqueeB = pt;
            const auto box = juce::Rectangle<float> (marqueeA, marqueeB);
            sel = marqueeBase;
            for (const auto& o : pat().orbs) if (box.intersects (orbBox (o)) && ! isSel (o.id)) sel.push_back (o.id);
            slideSel = -1;
            notify();
            return;
        }
        if (! dragging) return;
        if (! moved && pt.getDistanceFrom (downAt) < 4.0f) return;
        if (! moved) { proc.checkpoint(); moved = true; }

        if (dragVelocity || e.mods.isAltDown())
        {
            const float dv = (downAt.y - pt.y) / 140.0f;
            for (const auto& og : origins) if (auto* o = orbById (og.id)) o->vel = juce::jlimit (0.05f, 1.0f, og.vel + dv);
            commit();
            return;
        }
        int ds = (int) std::round ((pt.x - downAt.x) / STEP);
        int dr = (int) std::round ((pt.y - downAt.y) / ROW);
        double minStart = 99, maxStart = -1; int maxMidi = 0, minMidi = 999;
        for (const auto& og : origins) { minStart = std::min (minStart, og.start); maxStart = std::max (maxStart, og.start); maxMidi = std::max (maxMidi, og.midi); minMidi = std::min (minMidi, og.midi); }
        ds = juce::jlimit ((int) std::round (-minStart / 0.25), (int) std::round ((7.75 - maxStart) / 0.25), ds);
        dr = juce::jlimit (-(drumbox::rollTop - maxMidi), minMidi - drumbox::rollBottom, dr);

        bool pitchChanged = false;
        for (const auto& og : origins)
            if (auto* o = orbById (og.id))
            {
                const int midi = og.midi - dr;
                if (midi != o->midi) { o->okKey = -1; if (og.id == dragAnchor) pitchChanged = true; }
                o->start = og.start + ds * 0.25;
                o->midi = midi;
            }
        if (pitchChanged) if (auto* a = orbById (dragAnchor)) proc.audition (track, a->midi, a->vel);
        commit();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (link >= 0)
        {
            auto* target = hit (e.position);
            if (target != nullptr && target->id != link)
            {
                proc.checkpoint();
                auto& slides = pat().slides;
                auto existing = std::find_if (slides.begin(), slides.end(), [this] (const drumbox::Slide& s) { return s.from == link; });
                if (existing != slides.end()) { existing->to = target->id; slideSel = existing->id; }
                else { drumbox::Slide s; s.id = proc.project.nextId++; s.from = link; s.to = target->id; slides.push_back (s); slideSel = s.id; }
                sel.clear();
                commit();
            }
            link = -1; repaint();
            return;
        }
        if (marquee) { marquee = false; notify(); return; }
        if (dragging) { dragging = false; return; }
        if (pendingCreate)
        {
            pendingCreate = false;
            if (pendingShift) return;
            startTimer (220);   // a second click in this window means "stretch", not "new note"
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto mods = k.getModifiers();
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
        if (k == juce::KeyPress::escapeKey) { clearSelection(); return true; }
        if (mods.isCommandDown() && k.getKeyCode() == 'A')
        {
            sel.clear(); for (const auto& o : pat().orbs) sel.push_back (o.id);
            slideSel = -1; notify(); return true;
        }
        int ds = 0, dm = 0;
        if (k.getKeyCode() == juce::KeyPress::upKey)    dm = mods.isShiftDown() ? 12 : 1;
        if (k.getKeyCode() == juce::KeyPress::downKey)  dm = mods.isShiftDown() ? -12 : -1;
        if (k.getKeyCode() == juce::KeyPress::leftKey)  ds = -1;
        if (k.getKeyCode() == juce::KeyPress::rightKey) ds = 1;
        if ((ds || dm) && ! sel.empty())
        {
            for (const auto& o : pat().orbs)
                if (isSel (o.id) && (o.start + ds * 0.25 < 0 || o.start + ds * 0.25 > 7.75 || o.midi + dm > drumbox::rollTop || o.midi + dm < drumbox::rollBottom))
                    return true;
            proc.checkpoint();
            for (auto& o : pat().orbs) if (isSel (o.id)) { o.start += ds * 0.25; o.midi += dm; if (dm) o.okKey = -1; }
            commit();
            return true;
        }
        return false;
    }

    // chords dragged in from the bucket: "chord:<degree>:<minor 0/1>"
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("chord:"); }
    void itemDropped (const SourceDetails& d) override
    {
        juce::StringArray parts; parts.addTokens (d.description.toString(), ":", "");
        const int deg = parts[1].getIntValue();
        const bool chordMinor = parts[2].getIntValue() != 0;
        const auto c = cellAt (d.localPosition.toFloat());
        const auto& P = proc.project;
        const int pc = (P.keyRoot + P.scale()[deg]) % 12;
        int root = c.midi - (((c.midi - pc) % 12) + 12) % 12;
        if (root + 7 > drumbox::rollTop) root -= 12;
        if (root < drumbox::rollBottom) root += 12;
        proc.checkpoint();
        sel.clear(); slideSel = -1;
        for (int iv : { 0, chordMinor ? 3 : 4, 7 })
        {
            drumbox::Orb o; o.id = proc.project.nextId++; o.midi = root + iv; o.start = c.beat; o.len = 2.0; o.vel = 0.7f; o.chord = true;
            pat().orbs.push_back (o); sel.push_back (o.id);
            proc.audition (track, o.midi, 0.6f);
        }
        commit();
    }

    static juce::String noteName (int m)
    {
        static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return juce::String (n[m % 12]) + juce::String (m / 12 - 1);
    }
    static juce::String fmt (double v) { return juce::String (std::round (v * 100.0) / 100.0); }

private:
    struct Cell { int step, row, midi; double beat; };
    struct Origin { int id; double start; int midi; float vel; };

    void timerCallback() override
    {
        stopTimer();
        proc.checkpoint();
        drumbox::Orb o; o.id = proc.project.nextId++; o.midi = pendingCell.midi; o.start = pendingCell.beat; o.len = 0.25; o.vel = 0.7f;
        pat().orbs.push_back (o);
        sel = { o.id }; slideSel = -1;
        proc.audition (track, o.midi, o.vel);
        commit();
    }

    Cell cellAt (juce::Point<float> p) const
    {
        const int step = juce::jlimit (0, 31, (int) std::floor ((p.x - LBL) / STEP));
        const int row = juce::jlimit (0, NROWS - 1, (int) std::floor (p.y / ROW));
        return { step, row, drumbox::rollTop - row, step * 0.25 };
    }
    juce::Rectangle<float> orbBox (const drumbox::Orb& o) const
    {
        const float d = std::round (12.0f + o.vel * 18.0f);
        const double steps = o.len / 0.25;
        const float w = steps <= 1.001 ? d : std::max (d, (float) (steps * STEP - 6));
        const float left = (float) (LBL + o.start / 0.25 * STEP) + (steps <= 1.001 ? (STEP - d) * 0.5f : 3.0f);
        const int row = drumbox::rollTop - o.midi;
        return { left, row * ROW + (ROW - d) * 0.5f, w, d };
    }
    drumbox::Orb* hit (juce::Point<float> p)
    {
        const int row = (int) std::floor (p.y / ROW);
        auto& orbs = pat().orbs;
        for (int i = (int) orbs.size() - 1; i >= 0; --i)
        {
            const auto b = orbBox (orbs[(size_t) i]);
            if (drumbox::rollTop - orbs[(size_t) i].midi == row && p.x >= b.getX() - 4 && p.x <= b.getRight() + 4) return &orbs[(size_t) i];
        }
        return nullptr;
    }
    juce::Point<float> handleCentre (const drumbox::Orb& o) const { const auto b = orbBox (o); return { b.getRight() + 14.0f, b.getCentreY() }; }
    bool slideEnds (const drumbox::Slide& s, juce::Point<float>& a, juce::Point<float>& b)
    {
        auto* from = orbById (s.from); auto* to = orbById (s.to);
        if (from == nullptr || to == nullptr) return false;
        const auto ba = orbBox (*from), bb = orbBox (*to);
        a = { ba.getRight(), ba.getCentreY() }; b = { bb.getX(), bb.getCentreY() };
        return true;
    }
    static juce::Path slidePath (juce::Point<float> a, juce::Point<float> b)
    {
        juce::Path p; p.startNewSubPath (a); p.cubicTo (a.x + 36, a.y, b.x - 36, b.y, b.x, b.y); return p;
    }
    drumbox::Slide* hitSlide (juce::Point<float> pt)
    {
        for (auto& s : pat().slides)
        {
            juce::Point<float> a, b;
            if (! slideEnds (s, a, b)) continue;
            juce::Point<float> nearest;
            auto path = slidePath (a, b);
            path.getNearestPoint (pt, nearest);
            if (nearest.getDistanceFrom (pt) < 9.0f) return &s;
        }
        return nullptr;
    }

    DrumBoxProcessor& proc;
    const int track;
    juce::Point<float> downAt, linkAt, marqueeA, marqueeB;
    bool moved = false, dragging = false, dragVelocity = false, pendingCreate = false, pendingShift = false, marquee = false;
    int dragAnchor = -1, link = -1;
    Cell pendingCell {};
    std::vector<Origin> origins;
    std::vector<int> marqueeBase;
};

} // namespace ui
