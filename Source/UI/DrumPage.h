#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace ui
{
//==============================================================================
// Full screen drum machine: 8 voices x 16 steps, A to H variations, and the
// selected voice's sound on the right.
class DrumPage : public juce::Component
{
public:
    explicit DrumPage (DrumBoxProcessor& p) : proc (p)
    {
        for (auto* k : { &tune, &decay, &level, &pan }) addAndMakeVisible (*k);
        for (auto* k : { &swing, &vintage, &master }) addAndMakeVisible (*k);
        swingA   = std::make_unique<Att> (proc.apvts, "swing", swing.slider);
        vintageA = std::make_unique<Att> (proc.apvts, "vintage", vintage.slider);
        masterA  = std::make_unique<Att> (proc.apvts, "master", master.slider);
        attachVoice();

        slots.current = [this] { return proc.project.drums.current; };
        slots.filled = [this] (int i) { for (auto& row : proc.project.drums.patterns[(size_t) i]) for (auto v : row) if (v) return true; return false; };
        slots.onPick = [this] (int i, bool copy)
        {
            auto& d = proc.project.drums;
            if (copy && i != d.current) { proc.checkpoint(); d.patterns[(size_t) i] = d.patterns[(size_t) d.current]; }
            d.current = i;
            proc.changed();
        };
        addAndMakeVisible (slots);

        mute.state = [this] { return proc.project.drums.mute ? Pill::Accent : Pill::Off; };
        mute.onClick = [this] { proc.checkpoint(); proc.project.drums.mute = ! proc.project.drums.mute; proc.changed(); };
        addAndMakeVisible (mute);
        clear.onClick = [this] { proc.checkpoint(); for (auto& row : grid()) row.fill (0); proc.changed(); };
        addAndMakeVisible (clear);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (28, 20);
        auto footer = r.removeFromBottom (92);
        auto side = r.removeFromRight (250);
        r.removeFromRight (24);
        gridArea = r;

        auto s = side.withHeight (360).reduced (20);
        s.removeFromTop (110);
        const int kw = s.getWidth() / 2;
        auto row1 = s.removeFromTop (110), row2 = s.removeFromTop (110);
        tune.setBounds (row1.removeFromLeft (kw)); decay.setBounds (row1);
        level.setBounds (row2.removeFromLeft (kw)); pan.setBounds (row2);
        sidePanel = side.withHeight (360);

        footer.removeFromTop (20);
        auto f = footer;
        f.removeFromLeft (76);
        slots.setBounds (f.removeFromLeft (8 * 44 + 7 * 6).withSizeKeepingCentre (8 * 44 + 7 * 6, 44));
        f.removeFromLeft (14);
        mute.setBounds (f.removeFromLeft (76).withSizeKeepingCentre (76, 40));
        f.removeFromLeft (6);
        clear.setBounds (f.removeFromLeft (76).withSizeKeepingCentre (76, 40));
        for (auto* k : { &master, &vintage, &swing }) { k->setBounds (f.removeFromRight (78)); f.removeFromRight (10); }
        footerArea = footer;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& gr = grid();
        const int cur = currentStep();

        // step numbers
        for (int s = 0; s < 16; ++s)
        {
            const auto cr = cell (0, s);
            g.setFont (mono (10.0f));
            g.setColour (s == cur ? c::accentHi : s % 4 == 0 ? c::textSoft : c::faint);
            g.drawText (juce::String (s + 1), cr.getX(), gridArea.getY(), cr.getWidth(), 16, juce::Justification::centred);
        }
        for (int v = 0; v < drumbox::numVoices; ++v)
        {
            auto nameBox = juce::Rectangle<int> (gridArea.getX(), cell (v, 0).getY(), nameW - 8, cellH).toFloat();
            if (v == selected) { g.setColour (c::raised.withAlpha (0.8f)); g.fillRoundedRectangle (nameBox, 10.0f); }
            caps (g, drumbox::voiceName (v), nameBox.toNearestInt().withTrimmedLeft (12), v == selected ? c::text : c::textSoft, 11.5f, juce::Justification::centredLeft, true);

            for (int s = 0; s < 16; ++s)
            {
                const auto r = cell (v, s).toFloat();
                const int val = gr[(size_t) v][(size_t) s];
                pill (g, r, val == 2 ? Pill::Accent : val == 1 ? Pill::On : Pill::Dim, 9.0f);
                if (val == 0 && s % 4 == 0) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRoundedRectangle (r, 9.0f); }
                if (val == 1) { g.setColour (c::accentHi.withAlpha (0.55f)); g.fillEllipse (r.withSizeKeepingCentre (6, 6)); }
                if (s == cur) { g.setColour (c::accentHi); g.drawRoundedRectangle (r.expanded (2.5f), 11.0f, 1.6f); }
            }
        }

        // selected voice panel
        raisedPanel (g, sidePanel.toFloat());
        auto sp = sidePanel.reduced (20);
        caps (g, "Voice", sp.removeFromTop (20), c::label, 10.0f);
        g.setFont (font (24.0f)); g.setColour (c::text);
        g.drawText (drumbox::voiceName (selected), sp.removeFromTop (34), juce::Justification::centredLeft);
        caps (g, "Click a name to edit it", sp.removeFromTop (20), c::faint, 9.0f);

        g.setColour (c::line); g.fillRect (footerArea.withHeight (1));
        caps (g, "Pattern", footerArea.withTrimmedTop (20).withWidth (70).withHeight (44), c::label, 10.0f);
        caps (g, "Alt + click a letter to copy this pattern there", footerArea.withTrimmedTop (68).withTrimmedLeft (76), c::faint, 9.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int v = 0; v < drumbox::numVoices; ++v)
        {
            if (juce::Rectangle<int> (gridArea.getX(), cell (v, 0).getY(), nameW - 8, cellH).contains (e.getPosition()))
            { selected = v; attachVoice(); repaint(); return; }
            for (int s = 0; s < 16; ++s)
                if (cell (v, s).expanded (3).contains (e.getPosition()))
                {
                    proc.checkpoint();
                    auto& gr = grid();
                    paintValue = e.mods.isPopupMenu() ? 0 : (gr[(size_t) v][(size_t) s] + 1) % 3;
                    gr[(size_t) v][(size_t) s] = (uint8_t) paintValue;
                    proc.changed(); repaint();
                    return;
                }
        }
        paintValue = -1;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (paintValue < 0) return;
        for (int v = 0; v < drumbox::numVoices; ++v)
            for (int s = 0; s < 16; ++s)
                if (cell (v, s).contains (e.getPosition()) && grid()[(size_t) v][(size_t) s] != paintValue)
                { grid()[(size_t) v][(size_t) s] = (uint8_t) paintValue; proc.changed(); repaint(); }
    }

private:
    using Att = juce::AudioProcessorValueTreeState::SliderAttachment;

    drumbox::DrumGrid& grid() { return proc.project.drums.patterns[(size_t) proc.project.drums.current]; }
    int currentStep() const { const double b = proc.playBeat.load(); return b < 0 ? -1 : drumbox::Studio::drumStepAt (b); }
    juce::Rectangle<int> cell (int v, int s) const
    {
        const int x = gridArea.getX() + nameW + s * (cellW + 6) + (s / 4) * 8;
        return { x, gridArea.getY() + 22 + v * (cellH + 8), cellW, cellH };
    }
    void attachVoice()
    {
        tuneA.reset(); decayA.reset(); levelA.reset(); panA.reset();
        const juce::String n (selected);
        tuneA  = std::make_unique<Att> (proc.apvts, "tune" + n, tune.slider);
        decayA = std::make_unique<Att> (proc.apvts, "decay" + n, decay.slider);
        levelA = std::make_unique<Att> (proc.apvts, "level" + n, level.slider);
        panA   = std::make_unique<Att> (proc.apvts, "pan" + n, pan.slider);
    }

    DrumBoxProcessor& proc;
    Knob tune { "Tune" }, decay { "Decay" }, level { "Level" }, pan { "Pan" };
    Knob swing { "Swing" }, vintage { "Vintage" }, master { "Master" };
    std::unique_ptr<Att> tuneA, decayA, levelA, panA, swingA, vintageA, masterA;
    PatternSlots slots;
    PillButton mute { "Mute" }, clear { "Clear" };
    juce::Rectangle<int> gridArea, sidePanel, footerArea;
    int selected = 1, paintValue = -1;
    static constexpr int nameW = 120, cellW = 40, cellH = 50;
};

} // namespace ui
