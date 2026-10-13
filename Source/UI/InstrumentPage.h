#pragma once
#include "OrbRoll.h"

namespace ui
{
//==============================================================================
// Drag the point to shape how a slide's pitch travels from one note to the next
class CurveEditor : public juce::Component
{
public:
    std::function<drumbox::Slide*()> slide;
    std::function<void()> onBegin, onChange;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        deepPanel (g, r, 10.0f);
        auto* s = slide ? slide() : nullptr;
        if (s == nullptr) return;
        const auto a = r.getBottomLeft() + juce::Point<float> (12, -12), b = r.getTopRight() + juce::Point<float> (-12, 12);
        const auto cp = control (*s);
        g.setColour (c::line);
        g.drawLine (a.x, a.y, b.x, a.y); g.drawLine (a.x, b.y, b.x, b.y);
        float dl[] = { 3.0f, 3.0f };
        g.setColour (c::faint);
        g.drawDashedLine ({ a, cp }, dl, 2); g.drawDashedLine ({ b, cp }, dl, 2);
        juce::Path p; p.startNewSubPath (a); p.quadraticTo (cp, b);
        g.setColour (c::accentHi); g.strokePath (p, juce::PathStrokeType (2.5f));
        g.setColour (c::deepTop); g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (cp));
        g.setColour (c::accentHi); g.drawEllipse (juce::Rectangle<float> (14, 14).withCentre (cp), 2.5f);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (onBegin) onBegin(); set (e); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e); }

private:
    juce::Point<float> control (const drumbox::Slide& s) const
    {
        const auto r = getLocalBounds().toFloat().reduced (12);
        return { r.getX() + s.cx * r.getWidth(), r.getBottom() - s.cy * r.getHeight() };
    }
    void set (const juce::MouseEvent& e)
    {
        auto* s = slide ? slide() : nullptr;
        if (s == nullptr) return;
        const auto r = getLocalBounds().toFloat().reduced (12);
        s->cx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        s->cy = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        repaint();
        if (onChange) onChange();
    }
};

//==============================================================================
// Major / minor chords on every note of the key, dragged into the roll
class ChordBucket : public juce::Component
{
public:
    explicit ChordBucket (DrumBoxProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        deepPanel (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().reduced (16);
        auto head = r.removeFromTop (18);
        caps (g, "Chord bucket", head, c::label, 10.0f);
        g.setFont (mono (11.0f)); g.setColour (c::accentHi);
        g.drawText (keyName(), head, juce::Justification::centredRight);
        r.removeFromTop (10);
        const auto t = toggle();
        pill (g, t.toFloat(), Pill::Dim, 10.0f);
        auto half = t.reduced (3);
        auto left = half.removeFromLeft (half.getWidth() / 2);
        pill (g, (minor ? half : left).toFloat(), Pill::On, 8.0f);
        caps (g, "Major", left, minor ? c::faint : c::text, 10.0f, juce::Justification::centred);
        caps (g, "Minor", half, minor ? c::text : c::faint, 10.0f, juce::Justification::centred);

        const auto& P = proc.project;
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        for (int d = 0; d < 7; ++d)
        {
            const auto cr = chip (d).toFloat();
            const bool fits = diatonic (d);
            pill (g, cr, fits ? Pill::On : Pill::Off, 12.0f);
            g.setFont (font (16.0f)); g.setColour (fits ? c::text : c::textSoft);
            g.drawText (juce::String (names[(P.keyRoot + P.scale()[d]) % 12]) + (minor ? " min" : " maj"), cr.toNearestInt().reduced (12, 0).withTrimmedBottom (16), juce::Justification::bottomLeft);
            g.setFont (mono (10.0f)); g.setColour (fits ? c::accentHi : c::faint);
            g.drawText ((fits ? juce::String ("* ") : juce::String()) + juce::String (d + 1), cr.toNearestInt().reduced (12, 6), juce::Justification::bottomLeft);
        }
        g.setFont (font (11.0f)); g.setColour (c::faint);
        g.drawFittedText ("Drag onto a row. * = in the key.", r.removeFromBottom (18), juce::Justification::bottomLeft, 1);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (toggle().contains (e.getPosition())) { minor = e.x > toggle().getCentreX(); repaint(); return; }
        dragDeg = -1;
        for (int d = 0; d < 7; ++d) if (chip (d).contains (e.getPosition())) dragDeg = d;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragDeg < 0 || e.getDistanceFromDragStart() < 6) return;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            juce::Image img (juce::Image::ARGB, chip (dragDeg).getWidth(), chip (dragDeg).getHeight(), true);
            { juce::Graphics g (img); pill (g, img.getBounds().toFloat(), Pill::Accent, 12.0f); }
            dc->startDragging ("chord:" + juce::String (dragDeg) + ":" + (minor ? "1" : "0"), this, juce::ScaledImage (img));
        }
        dragDeg = -1;
    }

private:
    juce::Rectangle<int> toggle() const { return { 16, 44, getWidth() - 32, 40 }; }
    juce::Rectangle<int> chip (int d) const
    {
        const int w = (getWidth() - 32 - 8) / 2;
        return { 16 + (d % 2) * (w + 8), 96 + (d / 2) * 58, w, 52 };
    }
    bool diatonic (int d) const
    {
        static const bool minorDeg[2][7] = { { false, true, true, false, false, true, false }, { true, false, false, true, true, false, false } };
        const bool keyMinor = proc.project.minor;
        if (d == (keyMinor ? 1 : 6)) return false;   // the diminished chord
        return minorDeg[keyMinor ? 1 : 0][d] == minor;
    }
    juce::String keyName() const
    {
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return juce::String (names[proc.project.keyRoot]) + (proc.project.minor ? " MIN" : " MAJ");
    }

    DrumBoxProcessor& proc;
    bool minor = false;
    int dragDeg = -1;
};

//==============================================================================
class InstrumentPage : public juce::Component
{
public:
    InstrumentPage (DrumBoxProcessor& p, int t) : proc (p), track (t), roll (p, t), bucket (p)
    {
        // engine picker
        for (int i = 0; i < 4; ++i)
        {
            auto* b = engines.add (new PillButton (drumbox::engineName ((drumbox::Engine) i)));
            b->state = [this, i] { return (int) trk().engine == i ? Pill::Accent : Pill::Off; };
            b->onClick = [this, i] { proc.checkpoint(); trk().engine = (drumbox::Engine) i; proc.changed(); proc.audition (track, 60, 0.8f); repaint(); };
            addAndMakeVisible (b);
        }
        // sound knobs, straight onto the track's sound
        const char* names[] = { "Cutoff", "Reso", "Attack", "Release", "Chorus", "Level" };
        for (int i = 0; i < 6; ++i)
        {
            auto* k = knobs.add (new Knob (names[i]));
            k->slider.setRange (0.0, 1.0);
            k->slider.onDragStart = [this] { proc.checkpoint(); };
            k->slider.onValueChange = [this, i, k] { *soundField (i) = (float) k->slider.getValue(); proc.changed(); };
            addAndMakeVisible (k);
        }
        // key
        keyRoot.setFont (mono (20.0f));
        keyRoot.setJustification (juce::Justification::centred);
        keyRoot.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        keyRoot.setColour (juce::TextEditor::textColourId, c::accentHi);
        keyRoot.onTextChange = [this] { const int k = parseKey (keyRoot.getText()); if (k >= 0 && k != proc.project.keyRoot) { proc.checkpoint(); proc.project.keyRoot = k; proc.changed(); refresh(); } };
        addAndMakeVisible (keyRoot);
        majB.state = [this] { return proc.project.minor ? Pill::Dim : Pill::On; };
        minB.state = [this] { return proc.project.minor ? Pill::On : Pill::Dim; };
        majB.onClick = [this] { proc.checkpoint(); proc.project.minor = false; proc.changed(); refresh(); };
        minB.onClick = [this] { proc.checkpoint(); proc.project.minor = true; proc.changed(); refresh(); };
        addAndMakeVisible (majB); addAndMakeVisible (minB);

        slots.current = [this] { return trk().current; };
        slots.filled = [this] (int i) { return ! trk().patterns[(size_t) i].empty(); };
        slots.onPick = [this] (int i, bool copy)
        {
            if (copy && i != trk().current) { proc.checkpoint(); trk().patterns[(size_t) i] = trk().patterns[(size_t) trk().current]; }
            trk().current = i; proc.changed(); roll.patternSwitched(); refresh();
        };
        addAndMakeVisible (slots);
        mute.state = [this] { return trk().mute ? Pill::Accent : Pill::Off; };
        mute.onClick = [this] { proc.checkpoint(); trk().mute = ! trk().mute; proc.changed(); repaint(); };
        addAndMakeVisible (mute);
        octUp.onClick = [this] { scrollOctave (-1); };
        octDown.onClick = [this] { scrollOctave (1); };
        addAndMakeVisible (octUp); addAndMakeVisible (octDown);

        viewport.setViewedComponent (&roll, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
        roll.onChange = [this] { refreshInspector(); };

        // inspector: notes
        lenEd.setFont (mono (15.0f));
        lenEd.setTextToShowWhenEmpty ("mixed", c::faint);
        lenEd.onReturnKey = [this] { applyLength(); roll.grabKeyboardFocus(); };
        lenEd.onFocusLost = [this] { applyLength(); };
        velSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        velSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        velSlider.setRange (1, 127, 1);
        velSlider.onDragStart = [this] { proc.checkpoint(); };
        velSlider.onValueChange = [this]
        {
            if (updating) return;
            for (auto& o : roll.pat().orbs) if (roll.isSel (o.id)) o.vel = (float) velSlider.getValue() / 127.0f;
            proc.changed(); roll.repaint(); repaint();
        };
        delB.onClick = [this] { roll.deleteSelection(); };
        for (auto* comp : std::initializer_list<juce::Component*> { &lenEd, &velSlider, &delB }) addChildComponent (comp);

        // inspector: slide
        const std::array<std::pair<const char*, std::pair<float, float>>, 3> pres { { { "Linear", { 0.5f, 0.5f } }, { "Late", { 0.95f, 0.05f } }, { "Early", { 0.05f, 0.95f } } } };
        for (const auto& [label, pt] : pres)
        {
            auto* b = presets.add (new PillButton (label));
            const float cx = pt.first, cy = pt.second;
            b->fontSize = 9.5f;
            b->state = [this, cx, cy] { auto* s = selSlide(); return s && std::abs (s->cx - cx) < 0.02f && std::abs (s->cy - cy) < 0.02f ? Pill::Accent : Pill::Off; };
            b->onClick = [this, cx, cy] { if (auto* s = selSlide()) { proc.checkpoint(); s->cx = cx; s->cy = cy; proc.changed(); refreshInspector(); } };
            addChildComponent (b);
        }
        curve.slide = [this] { return selSlide(); };
        curve.onBegin = [this] { proc.checkpoint(); };
        curve.onChange = [this] { proc.changed(); for (auto* b : presets) b->repaint(); };
        addChildComponent (curve);
        glide.setSliderStyle (juce::Slider::LinearHorizontal);
        glide.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        glide.setRange (5, 100, 1);
        glide.onDragStart = [this] { proc.checkpoint(); };
        glide.onValueChange = [this] { if (updating) return; if (auto* s = selSlide()) { s->time = (float) glide.getValue() / 100.0f; proc.changed(); repaint(); } };
        removeSlide.onClick = [this] { roll.deleteSelection(); };
        for (auto* comp : std::initializer_list<juce::Component*> { &glide, &removeSlide }) addChildComponent (comp);

        // out of key notice
        for (int i = 0; i < 3; ++i) { auto* b = flagButtons.add (new PillButton()); b->fontSize = 9.5f; addChildComponent (b); }

        addAndMakeVisible (bucket);
        refresh();
    }

    void refresh()
    {
        updating = true;
        for (int i = 0; i < 6; ++i) knobs[i]->slider.setValue (*soundField (i), juce::dontSendNotification);
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        if (parseKey (keyRoot.getText()) != proc.project.keyRoot) keyRoot.setText (names[proc.project.keyRoot], false);
        updating = false;
        // drop selections that no longer exist (after undo)
        roll.sel.erase (std::remove_if (roll.sel.begin(), roll.sel.end(), [this] (int id) { return roll.orbById (id) == nullptr; }), roll.sel.end());
        if (roll.slideSel >= 0 && roll.slideById (roll.slideSel) == nullptr) roll.slideSel = -1;
        refreshInspector();
        repaint(); roll.repaint(); bucket.repaint();
    }

    void scrollToMiddleC() { viewport.setViewPosition (0, 0); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (28, 12);
        auto top = r.removeFromTop (48);
        for (auto* b : engines) { b->setBounds (top.removeFromLeft (96)); top.removeFromLeft (6); }
        top.removeFromLeft (18);
        keyArea = top.removeFromLeft (44 + 54 + 120 + 20);
        auto ka = keyArea.withTrimmedLeft (44).reduced (0, 2);
        keyRoot.setBounds (ka.removeFromLeft (50).reduced (2, 6));
        ka.removeFromLeft (4);
        auto tg = ka.removeFromLeft (124).reduced (3, 7);
        majB.setBounds (tg.removeFromLeft (tg.getWidth() / 2)); minB.setBounds (tg);
        octDown.setBounds (top.removeFromRight (40).reduced (0, 4)); top.removeFromRight (4);
        octUp.setBounds (top.removeFromRight (40).reduced (0, 4)); top.removeFromRight (16);
        mute.setBounds (top.removeFromRight (70).reduced (0, 4)); top.removeFromRight (10);
        slots.setBounds (top.removeFromRight (8 * 30 + 7 * 6).reduced (0, 4));
        slotsLabel = slots.getBounds().translated (-84, 0).withWidth (80);

        r.removeFromTop (12);
        auto right = r.removeFromRight (240);
        r.removeFromRight (24);
        rollPanel = r.removeFromTop (12 + 22 + 416 + 12);
        viewport.setBounds (rollPanel.getX(), rollPanel.getY() + 34, OrbRoll::LBL + OrbRoll::GRID_W + 8, 416);
        r.removeFromTop (10);
        inspector = r.removeFromTop (108);

        soundPanel = right.removeFromTop (250);
        auto sp = soundPanel.reduced (16).withTrimmedTop (46);
        const int kw = sp.getWidth() / 3;
        for (int i = 0; i < 6; ++i)
            knobs[i]->setBounds (sp.getX() + (i % 3) * kw, sp.getY() + (i / 3) * 94, kw, 90);
        right.removeFromTop (12);
        bucket.setBounds (right);
        layoutInspector();
    }

    void paint (juce::Graphics& g) override
    {
        caps (g, "Key", keyArea.withWidth (40), c::label, 10.0f);
        glass (g, keyArea.withTrimmedLeft (44).toFloat().reduced (0, 2), 12.0f);
        caps (g, "Variation", slotsLabel, c::label, 10.0f, juce::Justification::centredRight);

        raisedPanel (g, rollPanel.toFloat());
        for (int b = 0; b < 8; ++b)
        {
            g.setFont (mono (10.0f)); g.setColour (b % 4 == 0 ? c::textSoft : c::faint);
            g.drawText (b % 4 == 0 ? juce::String (b / 4 + 1) : juce::String (b / 4 + 1) + "." + juce::String (b % 4 + 1),
                        rollPanel.getX() + OrbRoll::LBL + b * 4 * OrbRoll::STEP, rollPanel.getY() + 12, 4 * OrbRoll::STEP, 18, juce::Justification::centredLeft);
        }

        raisedPanel (g, soundPanel.toFloat());
        auto sp = soundPanel.reduced (16);
        caps (g, juce::String ("Track ") + juce::String::charToString ((juce::juce_wchar) ("ABCD"[track])), sp.removeFromTop (16), c::label, 10.0f);
        g.setFont (font (22.0f)); g.setColour (c::text);
        g.drawText (drumbox::engineName (trk().engine), sp.removeFromTop (30), juce::Justification::centredLeft);

        // inspector panel
        auto ins = inspectArea();
        deepPanel (g, ins.toFloat(), 16.0f);
        auto in = ins.reduced (16, 12);
        if (mode == Mode::None)
        {
            g.setFont (font (13.0f)); g.setColour (c::label);
            g.drawFittedText ("Select a note to edit it. Shift + click or drag a box to select several. Click a slide to shape it.", in, juce::Justification::centredLeft, 2);
        }
        else if (mode == Mode::Notes)
        {
            caps (g, "Selected", juce::Rectangle<int> (in.getX(), in.getY() + 18, 110, 14), c::label, 9.0f);
            g.setFont (mono (16.0f)); g.setColour (c::text);
            g.drawText (selLabel, in.getX(), in.getY() + 36, 110, 24, juce::Justification::centredLeft);
            caps (g, "Length  beats", lenEd.getBounds().translated (0, -18).withHeight (14), c::label, 9.0f);
            caps (g, "Velocity  " + velText, velSlider.getBounds().translated (0, -18).withHeight (14), c::label, 9.0f);
        }
        else
        {
            g.setFont (mono (14.0f)); g.setColour (c::text);
            caps (g, "Slide", juce::Rectangle<int> (in.getX(), in.getY(), 140, 14), c::label, 9.0f);
            g.drawText (slideLabel, in.getX(), in.getY() + 16, 140, 20, juce::Justification::centredLeft);
            caps (g, "Glide time  " + juce::String ((int) glide.getValue()) + "% of note", glide.getBounds().translated (0, -16).withHeight (14), c::label, 9.0f);
        }

        // out of key notice
        if (flagged.size() > 0)
        {
            auto t = toastArea().toFloat();
            g.setColour (c::warnBg); g.fillRoundedRectangle (t, 16.0f);
            g.setColour (c::warn); g.drawRoundedRectangle (t.reduced (0.75f), 16.0f, 1.5f);
            g.setFont (font (13.0f)); g.setColour (c::text);
            g.drawFittedText (flagText, toastArea().reduced (16, 12).removeFromTop (40), juce::Justification::topLeft, 2);
        }
    }

private:
    enum class Mode { None, Notes, Slide };

    drumbox::InstTrack& trk() { return proc.project.inst[(size_t) track]; }
    drumbox::Slide* selSlide() { return roll.slideSel >= 0 ? roll.slideById (roll.slideSel) : nullptr; }
    float* soundField (int i)
    {
        auto& s = trk().sound;
        float* f[] = { &s.cutoff, &s.reso, &s.attack, &s.release, &s.chorus, &s.level };
        return f[i];
    }
    static int parseKey (juce::String t)
    {
        t = t.trim().toUpperCase();
        const char* k[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const char* f[] = { "C", "DB", "D", "EB", "E", "F", "GB", "G", "AB", "A", "BB", "B" };
        for (int i = 0; i < 12; ++i) if (t == k[i] || t == f[i]) return i;
        return -1;
    }
    void scrollOctave (int dir)
    {
        const int y = juce::jlimit (0, roll.getHeight() - viewport.getHeight(), viewport.getViewPositionY() + dir * 12 * OrbRoll::ROW);
        viewport.setViewPosition (0, y);
    }
    juce::Rectangle<int> inspectArea() const { return flagged.empty() ? inspector : inspector.withTrimmedRight (310); }
    juce::Rectangle<int> toastArea() const { return inspector.withTrimmedLeft (inspector.getWidth() - 300); }

    void applyLength()
    {
        const double v = lenEd.getText().getDoubleValue();
        if (v <= 0.0 || v > 16.0) return;
        bool changedAny = false;
        for (auto& o : roll.pat().orbs) if (roll.isSel (o.id) && std::abs (o.len - v) > 1.0e-6) changedAny = true;
        if (! changedAny) return;
        proc.checkpoint();
        for (auto& o : roll.pat().orbs) if (roll.isSel (o.id)) o.len = v;
        proc.changed(); roll.repaint();
    }

    void refreshInspector()
    {
        std::vector<drumbox::Orb*> sel;
        for (auto& o : roll.pat().orbs) if (roll.isSel (o.id)) sel.push_back (&o);
        mode = ! sel.empty() ? Mode::Notes : selSlide() ? Mode::Slide : Mode::None;
        updating = true;
        if (mode == Mode::Notes)
        {
            selLabel = sel.size() == 1 ? OrbRoll::noteName (sel[0]->midi) : juce::String ((int) sel.size()) + " NOTES";
            const bool sameLen = std::all_of (sel.begin(), sel.end(), [&] (auto* o) { return std::abs (o->len - sel[0]->len) < 1.0e-6; });
            const bool sameVel = std::all_of (sel.begin(), sel.end(), [&] (auto* o) { return std::abs (o->vel - sel[0]->vel) < 0.01f; });
            if (! lenEd.hasKeyboardFocus (true)) lenEd.setText (sameLen ? OrbRoll::fmt (sel[0]->len) : juce::String(), false);
            float avg = 0; for (auto* o : sel) avg += o->vel; avg /= (float) sel.size();
            velSlider.setValue (std::round (avg * 127.0f), juce::dontSendNotification);
            velText = sameVel ? juce::String ((int) std::round (sel[0]->vel * 127.0f)) : juce::String ("mixed");
        }
        else if (mode == Mode::Slide)
        {
            auto* s = selSlide();
            auto* a = roll.orbById (s->from); auto* b = roll.orbById (s->to);
            slideLabel = (a ? OrbRoll::noteName (a->midi) : juce::String ("?")) + " -> " + (b ? OrbRoll::noteName (b->midi) : juce::String ("?"));
            glide.setValue (std::round (s->time * 100.0f), juce::dontSendNotification);
        }
        updating = false;

        // out of key check
        flagged.clear();
        const auto& P = proc.project;
        for (auto& o : roll.pat().orbs) if (! P.inKey (o.midi) && o.okKey != P.keySig()) flagged.push_back (o.id);
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const juce::String keyName = juce::String (names[P.keyRoot]) + (P.minor ? " minor" : " major");
        for (auto* b : flagButtons) b->setVisible (false);
        if (flagged.size() == 1)
        {
            auto* o = roll.orbById (flagged[0]);
            const int dn = P.nearestInKey (o->midi, -1), up = P.nearestInKey (o->midi, 1);
            const int id = o->id;
            flagText = OrbRoll::noteName (o->midi) + " isn't in " + keyName + ".";
            setFlagButton (0, "Keep it", true, [this, id] { if (auto* q = roll.orbById (id)) { proc.checkpoint(); q->okKey = proc.project.keySig(); roll.commit(); } });
            setFlagButton (1, juce::String ("To ") + names[dn % 12], false, [this, id, dn] { if (auto* q = roll.orbById (id)) { proc.checkpoint(); q->midi = dn; proc.audition (track, dn, q->vel); roll.commit(); } });
            setFlagButton (2, juce::String ("To ") + names[up % 12], false, [this, id, up] { if (auto* q = roll.orbById (id)) { proc.checkpoint(); q->midi = up; proc.audition (track, up, q->vel); roll.commit(); } });
        }
        else if (flagged.size() > 1)
        {
            flagText = juce::String ((int) flagged.size()) + " notes are outside " + keyName + ".";
            auto ids = flagged;
            setFlagButton (0, "Keep them", true, [this, ids] { proc.checkpoint(); for (int id : ids) if (auto* q = roll.orbById (id)) q->okKey = proc.project.keySig(); roll.commit(); });
            setFlagButton (1, "Fit to key", false, [this, ids] { proc.checkpoint(); for (int id : ids) if (auto* q = roll.orbById (id)) q->midi = proc.project.nearestInKey (q->midi, -1); roll.commit(); });
        }
        layoutInspector();
        repaint();
    }

    void setFlagButton (int i, juce::String text, bool primary, std::function<void()> fn)
    {
        auto* b = flagButtons[i];
        b->text = std::move (text);
        b->state = [primary] { return primary ? Pill::Accent : Pill::On; };
        b->onClick = std::move (fn);
        b->setVisible (true);
    }

    void layoutInspector()
    {
        const bool notes = mode == Mode::Notes, slide = mode == Mode::Slide;
        for (auto* comp : std::initializer_list<juce::Component*> { &lenEd, &velSlider, &delB }) comp->setVisible (notes);
        for (auto* comp : std::initializer_list<juce::Component*> { &glide, &removeSlide, &curve }) comp->setVisible (slide);
        for (auto* b : presets) b->setVisible (slide);

        auto in = inspectArea().reduced (16, 12);
        if (notes)
        {
            auto row = in.withTrimmedLeft (130).withTrimmedTop (36).withHeight (36);
            lenEd.setBounds (row.removeFromLeft (90)); row.removeFromLeft (24);
            velSlider.setBounds (row.removeFromLeft (190)); row.removeFromLeft (24);
            delB.setBounds (row.removeFromLeft (90));
        }
        if (slide)
        {
            auto pr = juce::Rectangle<int> (in.getX(), in.getY() + 48, 140, 34);
            const int w = (pr.getWidth() - 8) / 3;
            for (auto* b : presets) { b->setBounds (pr.removeFromLeft (w)); pr.removeFromLeft (4); }
            curve.setBounds (in.getX() + 156, in.getY() - 2, 200, in.getHeight() + 4);
            auto col = juce::Rectangle<int> (in.getX() + 376, in.getY() + 18, 170, in.getHeight() - 18);
            glide.setBounds (col.removeFromTop (30));
            col.removeFromTop (12);
            removeSlide.setBounds (col.removeFromTop (34));
        }
        auto t = toastArea().reduced (14, 12);
        auto br = t.removeFromBottom (34);
        int n = 0; for (auto* b : flagButtons) if (b->isVisible()) ++n;
        for (auto* b : flagButtons) if (b->isVisible()) { b->setBounds (br.removeFromLeft ((t.getWidth() - (n - 1) * 6) / std::max (1, n))); br.removeFromLeft (6); }
    }

    DrumBoxProcessor& proc;
    const int track;
    OrbRoll roll;
    juce::Viewport viewport;
    ChordBucket bucket;
    juce::OwnedArray<PillButton> engines, presets, flagButtons;
    juce::OwnedArray<Knob> knobs;
    juce::TextEditor keyRoot;
    PillButton majB { "Major" }, minB { "Minor" }, mute { "Mute" }, octUp { "Up" }, octDown { "Down" }, delB { "Delete" }, removeSlide { "Remove slide" };
    PatternSlots slots;
    juce::TextEditor lenEd;
    juce::Slider velSlider, glide;
    CurveEditor curve;
    juce::Rectangle<int> keyArea, slotsLabel, rollPanel, inspector, soundPanel;
    Mode mode = Mode::None;
    juce::String selLabel, velText, slideLabel, flagText;
    std::vector<int> flagged;
    bool updating = false;
};

} // namespace ui
