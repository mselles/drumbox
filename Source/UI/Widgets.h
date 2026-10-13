#pragma once
#include "Theme.h"

namespace ui
{
//==============================================================================
// A clickable pill with a caps label. The state is drawn by the owner's
// lambda, so one class covers toggles, tabs and plain buttons.
class PillButton : public juce::Component
{
public:
    std::function<Pill()> state = [] { return Pill::Off; };
    std::function<void()> onClick;
    juce::String text;
    float fontSize = 11.0f;
    bool monoFont = false;

    explicit PillButton (juce::String t = {}) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); setWantsKeyboardFocus (false); }

    void paint (juce::Graphics& g) override
    {
        const auto st = state();
        pill (g, getLocalBounds().toFloat().reduced (1.0f), st, juce::jmin (10.0f, getHeight() * 0.3f));
        if (monoFont) { g.setFont (mono (fontSize)); g.setColour (onPill (st)); g.drawText (text, getLocalBounds(), juce::Justification::centred); }
        else caps (g, text, getLocalBounds(), onPill (st), fontSize, juce::Justification::centred, st == Pill::Accent);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }
};

//==============================================================================
// Rotary knob with its caption underneath
class Knob : public juce::Component
{
public:
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String caption;

    explicit Knob (juce::String cap = {}) : caption (std::move (cap))
    {
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setMouseDragSensitivity (180);
        addAndMakeVisible (slider);
    }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedBottom (18)); }
    void paint (juce::Graphics& g) override { caps (g, caption, getLocalBounds().removeFromBottom (14), c::label, 9.5f, juce::Justification::centred); }
};

//==============================================================================
// The A to H variation slots for a track.
//   click        edit / play that variation
//   alt + click  copy the variation you're on into that slot, then switch to it
class PatternSlots : public juce::Component
{
public:
    std::function<int()> current;
    std::function<bool (int)> filled;
    std::function<void (int, bool copy)> onPick;

    void paint (juce::Graphics& g) override
    {
        const int cur = current ? current() : 0;
        for (int i = 0; i < 8; ++i)
        {
            const auto r = slot (i).toFloat();
            const auto st = i == cur ? Pill::Accent : (filled && filled (i)) ? Pill::On : Pill::Dim;
            pill (g, r, st, 9.0f);
            g.setFont (mono (13.0f));
            g.setColour (onPill (st));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), r.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 8; ++i)
            if (slot (i).contains (e.getPosition()) && onPick) { onPick (i, e.mods.isAltDown()); return; }
    }

private:
    juce::Rectangle<int> slot (int i) const
    {
        const int w = (getWidth() - 7 * 6) / 8;
        return { i * (w + 6), 0, w, getHeight() };
    }
};

} // namespace ui
