#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/DrumPage.h"
#include "UI/InstrumentPage.h"
#include "UI/SongPage.h"

//==============================================================================
// Tempo readout you drag up and down (double click resets to 112)
class TempoBox : public juce::Component
{
public:
    explicit TempoBox (DrumBoxProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        ui::glass (g, getLocalBounds().toFloat(), 10.0f);
        auto r = getLocalBounds().reduced (14, 6);
        ui::caps (g, proc.hostIsPlaying.load() ? "Host tempo" : "Tempo", r.removeFromTop (12), ui::c::faint, 8.5f);
        g.setFont (ui::mono (17.0f)); g.setColour (ui::c::text);
        g.drawText (juce::String (proc.currentBpm(), 1), r, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent&) override { start = proc.apvts.getParameter ("bpm")->getValue(); proc.apvts.getParameter ("bpm")->beginChangeGesture(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* p = proc.apvts.getParameter ("bpm");
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, start - e.getDistanceFromDragStartY() / 600.0f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { proc.apvts.getParameter ("bpm")->endChangeGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        auto* p = proc.apvts.getParameter ("bpm");
        p->setValueNotifyingHost (p->convertTo0to1 (112.0f)); repaint();
    }
private:
    DrumBoxProcessor& proc;
    float start = 0.0f;
};

//==============================================================================
class PlayButton : public ui::PillButton
{
public:
    std::function<bool()> playing;
    void paint (juce::Graphics& g) override
    {
        const bool on = playing && playing();
        const auto r = getLocalBounds().toFloat();
        juce::Path disc; disc.addEllipse (r.reduced (4.0f));
        juce::DropShadow (juce::Colour (0x88000000), 8, { 0, 3 }).drawForPath (g, disc);
        juce::ColourGradient grad (on ? ui::c::accentHi : juce::Colour (0xff4e3569), r.getX() + r.getWidth() * 0.35f, r.getY() + 6,
                                   on ? ui::c::accentDeep : ui::c::raised, r.getRight(), r.getBottom(), true);
        g.setGradientFill (grad);
        g.fillEllipse (r.reduced (4.0f));
        auto ic = r.reduced (17.0f);
        juce::Path icon;
        if (on) icon.addRoundedRectangle (ic.reduced (1.0f), 2.0f);
        else icon.addTriangle (ic.getX() + 2, ic.getY(), ic.getRight() + 1, ic.getCentreY(), ic.getX() + 2, ic.getBottom());
        g.setColour (on ? ui::c::bgTop : ui::c::accentHi);
        g.fillPath (icon);
    }
};

//==============================================================================
class DrumBoxEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    explicit DrumBoxEditor (DrumBoxProcessor&);
    ~DrumBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void showPage (int index);
    void afterUndo();
    void togglePlay();

    DrumBoxProcessor& proc;
    ui::Look look;
    ui::DrumPage drumPage;
    std::array<std::unique_ptr<ui::InstrumentPage>, drumbox::numInstruments> instPages;
    ui::SongPage songPage;
    int page = 0;

    juce::OwnedArray<ui::PillButton> tabs;
    PlayButton playB;
    ui::PillButton loopB { "Loop" }, songB { "Song" }, undoB { "Undo" }, redoB { "Redo" };
    TempoBox tempo;
    juce::Rectangle<int> modeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBoxEditor)
};
