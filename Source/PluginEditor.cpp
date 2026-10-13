#include "PluginEditor.h"

using namespace ui;

DrumBoxEditor::DrumBoxEditor (DrumBoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), drumPage (p), songPage (p), tempo (p)
{
    setLookAndFeel (&look);
    juce::LookAndFeel::setDefaultLookAndFeel (&look);   // popups and file choosers too

    for (int t = 0; t < drumbox::numInstruments; ++t)
        instPages[(size_t) t] = std::make_unique<InstrumentPage> (p, t);

    const char* names[] = { "Drums", "A", "B", "C", "D", "Song" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = tabs.add (new PillButton (names[i]));
        b->state = [this, i] { return page == i ? Pill::Accent : Pill::Dim; };
        b->onClick = [this, i] { showPage (i); };
        if (i >= 1 && i <= 4)
        {
            b->fontSize = 10.0f;
            b->monoFont = false;
        }
        addAndMakeVisible (b);
    }

    playB.playing = [this] { return proc.playBeat.load() >= 0; };
    playB.onClick = [this] { togglePlay(); };
    loopB.state = [this] { return proc.project.songMode ? Pill::Dim : Pill::On; };
    songB.state = [this] { return proc.project.songMode ? Pill::On : Pill::Dim; };
    loopB.onClick = [this] { proc.project.songMode = false; proc.changed(); repaint(); songPage.repaint(); };
    songB.onClick = [this] { proc.project.songMode = true; proc.changed(); repaint(); songPage.repaint(); };
    undoB.state = [this] { return proc.canUndo() ? Pill::Off : Pill::Dim; };
    redoB.state = [this] { return proc.canRedo() ? Pill::Off : Pill::Dim; };
    undoB.onClick = [this] { if (proc.undo()) afterUndo(); };
    redoB.onClick = [this] { if (proc.redo()) afterUndo(); };
    for (auto* c : std::initializer_list<juce::Component*> { &playB, &loopB, &songB, &undoB, &redoB, &tempo }) addAndMakeVisible (c);

    addChildComponent (drumPage);
    for (auto& ip : instPages) addChildComponent (*ip);
    addChildComponent (songPage);

    setWantsKeyboardFocus (true);
    setSize (1280, 800);
    showPage (0);
    startTimerHz (30);
}

DrumBoxEditor::~DrumBoxEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DrumBoxEditor::showPage (int index)
{
    page = index;
    drumPage.setVisible (page == 0);
    for (int t = 0; t < drumbox::numInstruments; ++t) instPages[(size_t) t]->setVisible (page == t + 1);
    songPage.setVisible (page == 5);
    if (page >= 1 && page <= 4) instPages[(size_t) page - 1]->refresh();
    for (auto* t : tabs) t->repaint();
    repaint();
}

void DrumBoxEditor::afterUndo()
{
    for (auto& ip : instPages) ip->refresh();
    drumPage.repaint(); songPage.repaint(); repaint();
}

void DrumBoxEditor::togglePlay()
{
    auto* p = proc.apvts.getParameter ("play");
    p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void DrumBoxEditor::paint (juce::Graphics& g)
{
    windowBackground (g, getLocalBounds());
    auto bar = getLocalBounds().removeFromTop (72);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (bar);
    g.setColour (c::accent.withAlpha (0.35f)); g.fillRect (bar.removeFromBottom (1));

    g.setFont (font (19.0f, true).withExtraKerningFactor (0.16f));
    g.setColour (c::accentHi);
    g.drawText ("DRUMBOX", 28, 0, 170, 72, juce::Justification::centredLeft);
    caps (g, "Studio", { 150, 2, 60, 72 }, c::label, 9.0f);

    pill (g, modeArea.toFloat(), Pill::Dim, 12.0f);

}

void DrumBoxEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (72).reduced (28, 14);
    bar.removeFromLeft (182);
    for (int i = 0; i < 6; ++i)
    {
        tabs[i]->text = i >= 1 && i <= 4 ? juce::String::charToString ((juce::juce_wchar) ("ABCD"[i - 1])) + "  " + drumbox::engineName (proc.project.inst[(size_t) i - 1].engine) : tabs[i]->text;
        tabs[i]->setBounds (bar.removeFromLeft (i >= 1 && i <= 4 ? 100 : 74));
        bar.removeFromLeft (4);
    }
    redoB.setBounds (bar.removeFromRight (62)); bar.removeFromRight (4);
    undoB.setBounds (bar.removeFromRight (62)); bar.removeFromRight (14);
    tempo.setBounds (bar.removeFromRight (96)); bar.removeFromRight (10);
    playB.setBounds (bar.removeFromRight (44).withSizeKeepingCentre (44, 44)); bar.removeFromRight (14);
    modeArea = bar.removeFromRight (124);
    auto m = modeArea.reduced (3);
    loopB.setBounds (m.removeFromLeft (m.getWidth() / 2)); songB.setBounds (m);

    const auto content = getLocalBounds().withTrimmedTop (72);
    drumPage.setBounds (content);
    for (auto& ip : instPages) ip->setBounds (content);
    songPage.setBounds (content);
}

bool DrumBoxEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown() ? proc.redo() : proc.undo()) afterUndo();
        return true;
    }
    if (mods.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y')) { if (proc.redo()) afterUndo(); return true; }
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (! mods.isAnyModifierKeyDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '6') { showPage (k.getKeyCode() - '1'); return true; }
    return false;
}

void DrumBoxEditor::timerCallback()
{
    static double lastBeat = -2.0;
    const double b = proc.playBeat.load();
    if (std::abs (b - lastBeat) > 1.0e-9)
    {
        lastBeat = b;
        if (page == 0) drumPage.repaint();
        else if (page == 5) songPage.repaint();
        else instPages[(size_t) page - 1]->repaint();
        playB.repaint();
    }
    // engine names in the tabs follow the engine you pick
    for (int i = 1; i <= 4; ++i)
    {
        const auto want = juce::String::charToString ((juce::juce_wchar) ("ABCD"[i - 1])) + "  " + drumbox::engineName (proc.project.inst[(size_t) i - 1].engine);
        if (tabs[i]->text != want) { tabs[i]->text = want; tabs[i]->repaint(); }
    }
    undoB.repaint(); redoB.repaint(); tempo.repaint();
}
