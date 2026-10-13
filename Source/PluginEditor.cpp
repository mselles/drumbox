#include "PluginEditor.h"

using namespace drumbox;

//==============================================================================
// KnobLook
void KnobLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                 float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float stroke = juce::jmax (2.0f, r * 0.14f);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - stroke, r - stroke, 0.0f, startAngle, endAngle, true);
    g.setColour (ui::padOff);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar knobs fill from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (c.x, c.y, r - stroke, r - stroke, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (ui::padOn);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    const float inner = r - stroke * 2.4f;
    g.setColour (ui::cream);
    g.fillEllipse (c.x - inner * 0.55f, c.y - inner * 0.55f, inner * 1.1f, inner * 1.1f);
    juce::Path tick;
    tick.addRoundedRectangle (-stroke * 0.35f, -inner * 0.5f, stroke * 0.7f, inner * 0.42f, stroke * 0.3f);
    g.setColour (ui::panel);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c));
}

void KnobLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (b.getToggleState() ? ui::padAccent : (over ? ui::panelHi.brighter (0.15f) : ui::panelHi));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (ui::dim.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
// StepGrid
StepGrid::StepGrid (DrumEngine& e) : engine (e) { startTimerHz (60); }

juce::Rectangle<float> StepGrid::cellBounds (int voice, int step) const
{
    constexpr float cellW = 38.0f, groupGap = 10.0f, rowH = 46.0f;
    const float x = (float) step * cellW + (float) (step / 4) * groupGap;
    return { x + 2.0f, (float) voice * rowH + 5.0f, cellW - 4.0f, rowH - 10.0f };
}

bool StepGrid::cellAt (juce::Point<float> p, int& voice, int& step) const
{
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
            if (cellBounds (v, s).expanded (2.0f, 5.0f).contains (p)) { voice = v; step = s; return true; }
    return false;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int cur = engine.getCurrentStep();
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
        {
            const auto r = cellBounds (v, s);
            const int val = engine.getStep (v, s);
            auto col = val == 2 ? ui::padAccent : val == 1 ? ui::padOn : ui::padOff;
            if (val == 0 && (s % 4) == 0) col = col.brighter (0.12f);   // downbeat hint
            if (s == cur) col = col.brighter (val > 0 ? 0.35f : 0.25f);

            g.setColour (col);
            g.fillRoundedRectangle (r, 4.0f);
            if (val > 0)
            {   // little lit "LED" sheen on active pads
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.35f).reduced (3.0f, 2.0f), 3.0f);
            }
        }

    // chase light under the current column
    if (cur >= 0)
    {
        const auto r = cellBounds (numVoices - 1, cur);
        g.setColour (ui::chase);
        g.fillRoundedRectangle (r.getX() + 6.0f, r.getBottom() + 6.0f, r.getWidth() - 12.0f, 3.0f, 1.5f);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int v, s;
    if (! cellAt (e.position, v, s)) return;
    paintValue = (engine.getStep (v, s) + 1) % 3;      // off -> on -> accent -> off
    if (e.mods.isRightButtonDown() || e.mods.isCtrlDown()) paintValue = 0;
    engine.setStep (v, s, paintValue);
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    int v, s;
    if (cellAt (e.position, v, s) && engine.getStep (v, s) != paintValue)
    {
        engine.setStep (v, s, paintValue);
        repaint();
    }
}

void StepGrid::timerCallback()
{
    if (const int cur = engine.getCurrentStep(); cur != lastStep) { lastStep = cur; repaint(); }
}

//==============================================================================
// Editor
DrumBoxEditor::DrumBoxEditor (DrumBoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), grid (p.engine)
{
    setLookAndFeel (&look);
    addAndMakeVisible (grid);

    for (int v = 0; v < numVoices; ++v)
    {
        voiceKnobs[(size_t) v][0] = &makeKnob ("level" + juce::String (v));
        voiceKnobs[(size_t) v][1] = &makeKnob ("tune"  + juce::String (v));
        voiceKnobs[(size_t) v][2] = &makeKnob ("decay" + juce::String (v));
        voiceKnobs[(size_t) v][3] = &makeKnob ("pan"   + juce::String (v));
    }
    swing   = &makeKnob ("swing", true);
    vintage = &makeKnob ("vintage", true);
    master  = &makeKnob ("master", true);

    bpm = &makeKnob ("bpm", true);
    bpm->setSliderStyle (juce::Slider::LinearBar);
    bpm->setColour (juce::Slider::trackColourId, ui::padOff);
    bpm->setColour (juce::Slider::textBoxTextColourId, ui::cream);
    bpm->setNumDecimalPlacesToDisplay (0);

    playButton.setClickingTogglesState (true);
    playButton.setColour (juce::TextButton::textColourOffId, ui::cream);
    playButton.setColour (juce::TextButton::textColourOnId, ui::cream);
    addAndMakeVisible (playButton);
    playAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "play", playButton);

    setSize (20 + nameW + 638 + 16 + knobColW * 4 + 20, headerH + 22 + numVoices * rowH + footerH);
}

DrumBoxEditor::~DrumBoxEditor() { setLookAndFeel (nullptr); }

juce::Slider& DrumBoxEditor::makeKnob (const juce::String& paramId, bool showValue)
{
    auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag,
                                           showValue ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox));
    s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s->setColour (juce::Slider::textBoxTextColourId, ui::dim);
    s->setPopupDisplayEnabled (! showValue, true, this);
    s->setNumDecimalPlacesToDisplay (2);
    addAndMakeVisible (s);
    knobAttachments.add (new SliderAttachment (proc.apvts, paramId, *s));
    s->setDoubleClickReturnValue (true, proc.apvts.getParameter (paramId)->convertFrom0to1 (
                                            proc.apvts.getParameter (paramId)->getDefaultValue()));
    return *s;
}

void DrumBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::panel);

    // header
    g.setColour (ui::panelHi);
    g.fillRect (0, 0, getWidth(), headerH);
    g.setColour (ui::cream);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("DRUMBOX", 20, 0, 300, headerH, juce::Justification::centredLeft);
    g.setColour (ui::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("DIGITAL RHYTHM COMPUTER", 160, 4, 300, headerH, juce::Justification::centredLeft);
    if (proc.hostIsPlaying.load())
        g.drawText ("FOLLOWING HOST TEMPO", getWidth() - 440, 0, 200, headerH, juce::Justification::centredRight);

    // column labels
    const int labelsY = headerH + 4;
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    const int gridX = 20 + nameW;
    for (int s = 0; s < numSteps; ++s)
    {
        const auto r = grid.cellBounds (0, s);
        g.setColour (s % 4 == 0 ? ui::cream : ui::dim);
        g.drawText (juce::String (s + 1), gridX + (int) r.getX(), labelsY, (int) r.getWidth(), 14, juce::Justification::centred);
    }
    const char* knobNames[4] = { "LEVEL", "TUNE", "DECAY", "PAN" };
    const int knobsX = gridX + 638 + 16;
    g.setColour (ui::dim);
    for (int k = 0; k < 4; ++k)
        g.drawText (knobNames[k], knobsX + k * knobColW, labelsY, knobColW, 14, juce::Justification::centred);

    // voice names
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const int gridY = headerH + 22;
    for (int v = 0; v < numVoices; ++v)
    {
        g.setColour (ui::cream);
        g.drawText (voiceName (v), 20, gridY + v * rowH, nameW, rowH, juce::Justification::centredLeft);
        g.setColour (ui::padOff.withAlpha (0.6f));
        g.drawHorizontalLine (gridY + (v + 1) * rowH, 20.0f, (float) getWidth() - 20.0f);
    }

    // footer labels
    const int footY = gridY + numVoices * rowH;
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (ui::dim);
    const char* footNames[3] = { "SWING", "VINTAGE", "MASTER" };
    for (int k = 0; k < 3; ++k)
        g.drawText (footNames[k], 20 + k * 100, footY + 8, 80, 14, juce::Justification::centred);

    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText ("Click a step: on, again: accent, again: off.  Drag to paint.  Right click clears.\n"
                      "Plays with your DAW's transport, or press PLAY to run on its own.  MIDI notes 36 to 56 play the kit.",
                      340, footY + 20, getWidth() - 360, 60, juce::Justification::centredLeft, 3);
}

void DrumBoxEditor::resized()
{
    const int gridX = 20 + nameW, gridY = headerH + 22;
    grid.setBounds (gridX, gridY, 638, numVoices * rowH);

    playButton.setBounds (getWidth() - 220, 16, 80, headerH - 32);
    bpm->setBounds (getWidth() - 130, 16, 110, headerH - 32);

    const int knobsX = gridX + 638 + 16;
    for (int v = 0; v < numVoices; ++v)
        for (int k = 0; k < 4; ++k)
            voiceKnobs[(size_t) v][(size_t) k]->setBounds (knobsX + k * knobColW + 6, gridY + v * rowH + 4,
                                                           knobColW - 12, rowH - 8);

    const int footY = gridY + numVoices * rowH;
    juce::Slider* foot[3] = { swing, vintage, master };
    for (int k = 0; k < 3; ++k)
        foot[k]->setBounds (20 + k * 100 + 5, footY + 24, 70, 80);
}
