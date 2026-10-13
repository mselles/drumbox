#pragma once
//==============================================================================
// Theme.h
// The look in one place: deep purple surfaces, dark gold for anything live
// (lit steps, playhead, selected notes, knob arcs), warm cream type.
// Every screen draws with these helpers, so changing the palette here
// restyles the whole studio.
//==============================================================================
#include <juce_gui_extra/juce_gui_extra.h>

namespace ui
{
using juce::Colour;
namespace c
{
    // surfaces
    const Colour bgTop      { 0xff1c1027 };   // window, top
    const Colour bgBot      { 0xff110a19 };   // window, bottom
    const Colour panelTop   { 0xff2b1b3c };   // raised panels
    const Colour panelBot   { 0xff21152f };
    const Colour deepTop    { 0xff150c1f };   // sunken areas: roll, bucket, displays
    const Colour deepBot    { 0xff1b1027 };
    const Colour lane       { 0xff1e1229 };
    const Colour laneAlt    { 0xff22152e };
    const Colour line       { 0xff3a2a4d };   // grid lines
    const Colour lineStrong { 0xff57446e };
    const Colour raised     { 0xff3b2752 };   // "on" keys
    const Colour raisedHi   { 0xff4e3569 };
    const Colour offKey     { 0xff2a1c3a };
    // type
    const Colour text       { 0xfff1e8d6 };   // warm cream
    const Colour textSoft   { 0xffcabfd9 };
    const Colour label      { 0xff9d92b3 };
    const Colour faint      { 0xff6f6487 };
    // gold
    const Colour accent     { 0xffb8892e };   // dark gold
    const Colour accentHi   { 0xffe0b65a };
    const Colour accentDeep { 0xff7d5a19 };
    // out of key notes
    const Colour warn       { 0xffe8738f };
    const Colour warnBg     { 0xff3a1c2c };
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

// Wide-tracked caps, the panel's "silkscreen" labels
inline void caps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, Colour col, float size = 10.0f,
                  juce::Justification j = juce::Justification::centredLeft, bool bold = false)
{
    g.setFont (font (size, bold).withExtraKerningFactor (0.16f));
    g.setColour (col);
    g.drawText (text.toUpperCase(), r, j, false);
}

inline void windowBackground (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setGradientFill (juce::ColourGradient (c::bgTop, 0, (float) r.getY(), c::bgBot, 0, (float) r.getBottom(), false));
    g.fillRect (r);
}

inline void raisedPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 18.0f)
{
    juce::DropShadow (juce::Colour (0x66000000), 24, { 0, 10 }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (juce::ColourGradient (c::panelTop, 0, r.getY(), c::panelBot, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void deepPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 18.0f)
{
    g.setGradientFill (juce::ColourGradient (c::deepTop, 0, r.getY(), c::deepBot, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void glass (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setGradientFill (juce::ColourGradient (Colour (0xff0b0610), 0, r.getY(), Colour (0xff160d1f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (c::accent.withAlpha (0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// A key or pill: On = lifted purple, Accent = gold, Off = quiet purple, Dim = sunken
enum class Pill { Off, On, Accent, Dim };
inline void pill (juce::Graphics& g, juce::Rectangle<float> r, Pill state, float radius = 10.0f)
{
    switch (state)
    {
        case Pill::On:
            juce::DropShadow (juce::Colour (0x66000000), 6, { 0, 2 }).drawForRectangle (g, r.toNearestInt());
            g.setGradientFill (juce::ColourGradient (c::raisedHi, 0, r.getY(), c::raised, 0, r.getBottom(), false)); break;
        case Pill::Accent:
            juce::DropShadow (c::accent.withAlpha (0.45f), 12, { 0, 2 }).drawForRectangle (g, r.toNearestInt());
            g.setGradientFill (juce::ColourGradient (c::accentHi, 0, r.getY(), c::accent, 0, r.getBottom(), false)); break;
        case Pill::Off:
            g.setGradientFill (juce::ColourGradient (Colour (0xff302042), 0, r.getY(), c::offKey, 0, r.getBottom(), false)); break;
        case Pill::Dim:
            g.setColour (Colour (0xff1a1024)); break;
    }
    g.fillRoundedRectangle (r, radius);
    g.setColour (state == Pill::Dim ? juce::Colours::black.withAlpha (0.4f) : juce::Colours::white.withAlpha (state == Pill::Accent ? 0.25f : 0.06f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// Text colour that reads on a given pill state
inline Colour onPill (Pill p) { return p == Pill::Accent ? Colour (0xff1c1027) : p == Pill::Dim ? c::faint : c::text; }

inline void goldKnob (juce::Graphics& g, juce::Rectangle<float> r, float pos, bool bipolar)
{
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    auto b = r.withSizeKeepingCentre (size, size);
    const auto cen = b.getCentre();
    const float start = juce::MathConstants<float>::pi * -0.8333f, end = juce::MathConstants<float>::pi * 0.8333f;
    const float angle = start + pos * (end - start);
    const float ring = size * 0.5f - 2.0f, stroke = juce::jmax (3.0f, size * 0.07f);

    juce::Path track; track.addCentredArc (cen.x, cen.y, ring, ring, 0, start, end, true);
    g.setColour (c::line);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? 0.0f : start;
    juce::Path val; val.addCentredArc (cen.x, cen.y, ring, ring, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (c::accentHi);
    g.strokePath (val, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto cap = b.reduced (stroke + 4.0f);
    juce::DropShadow (juce::Colour (0x88000000), 8, { 0, 3 }).drawForRectangle (g, cap.toNearestInt());
    juce::ColourGradient metal (Colour (0xfff3dc96), cap.getX() + cap.getWidth() * 0.35f, cap.getY() + cap.getHeight() * 0.28f,
                                c::accentDeep, cap.getRight(), cap.getBottom(), true);
    metal.addColour (0.45, c::accent);
    g.setGradientFill (metal);
    g.fillEllipse (cap);

    juce::Path tick;
    tick.addRoundedRectangle (-1.2f, -cap.getHeight() * 0.42f, 2.4f, cap.getHeight() * 0.28f, 1.2f);
    g.setColour (Colour (0xff1c1027));
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (cen));
}

//==============================================================================
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::Slider::textBoxTextColourId, c::textSoft);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::backgroundColourId, Colour (0xff0b0610));
        setColour (juce::TextEditor::textColourId, c::text);
        setColour (juce::TextEditor::outlineColourId, c::accent);
        setColour (juce::TextEditor::focusedOutlineColourId, c::accentHi);
        setColour (juce::TextEditor::highlightColourId, c::accent.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, c::accentHi);
        setColour (juce::PopupMenu::backgroundColourId, c::panelTop);
        setColour (juce::PopupMenu::textColourId, c::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, c::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, Colour (0xff1c1027));
        setColour (juce::BubbleComponent::backgroundColourId, c::panelTop);
        setColour (juce::BubbleComponent::outlineColourId, c::accent);
        setColour (juce::TooltipWindow::textColourId, c::text);
        setColour (juce::AlertWindow::backgroundColourId, c::panelTop);
        setColour (juce::AlertWindow::textColourId, c::text);
        setColour (juce::ScrollBar::thumbColourId, c::lineStrong);
        setColour (juce::ResizableWindow::backgroundColourId, c::bgBot);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s) override
    {
        goldKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, s.getMinimum() < 0 && s.getMaximum() > 0);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 3.0f, (float) w, 6.0f);
        g.setColour (c::line);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (c::accent);
        g.fillRoundedRectangle (r.withRight (pos), 3.0f);
        auto thumb = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ pos, r.getCentreY() });
        juce::DropShadow (juce::Colour (0x88000000), 5, { 0, 2 }).drawForRectangle (g, thumb.toNearestInt());
        g.setGradientFill (juce::ColourGradient (Colour (0xfff3dc96), thumb.getX() + 6, thumb.getY() + 5, c::accentDeep, thumb.getRight(), thumb.getBottom(), true));
        g.fillEllipse (thumb);
    }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (mono (11.0f));
        return l;
    }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool) override
    {
        auto r = vertical ? juce::Rectangle<int> (x + w / 2 - 3, thumbStart, 6, thumbSize) : juce::Rectangle<int> (thumbStart, y + h / 2 - 3, thumbSize, 6);
        g.setColour (over ? c::accent : c::lineStrong);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }
};

} // namespace ui
