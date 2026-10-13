#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// Visual language: dark panel, cream silkscreen text, amber step pads that
// glow red when accented, and a chase light that walks the active step.
namespace ui
{
    const juce::Colour panel     { 0xff1b1916 };
    const juce::Colour panelHi   { 0xff26231f };
    const juce::Colour cream     { 0xffe9e1cf };
    const juce::Colour dim       { 0xff8a8275 };
    const juce::Colour padOff    { 0xff35312b };
    const juce::Colour padOn     { 0xffe8a33d };
    const juce::Colour padAccent { 0xffe0532f };
    const juce::Colour chase     { 0xfffff3d6 };
}

class KnobLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

//==============================================================================
// The 8 x 16 pattern grid. Click cycles off -> on -> accent -> off.
// Click and drag paints the same value across several steps.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (drumbox::DrumEngine& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    juce::Rectangle<float> cellBounds (int voice, int step) const;

private:
    void timerCallback() override;
    bool cellAt (juce::Point<float> p, int& voice, int& step) const;

    drumbox::DrumEngine& engine;
    int lastStep = -1, paintValue = 1;
};

//==============================================================================
class DrumBoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit DrumBoxEditor (DrumBoxProcessor&);
    ~DrumBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider& makeKnob (const juce::String& paramId, bool showValue = false);

    DrumBoxProcessor& proc;
    KnobLook look;
    StepGrid grid;

    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<SliderAttachment> knobAttachments;
    std::array<std::array<juce::Slider*, 4>, drumbox::numVoices> voiceKnobs {};
    juce::Slider *swing = nullptr, *vintage = nullptr, *master = nullptr, *bpm = nullptr;

    juce::TextButton playButton { "PLAY" };
    std::unique_ptr<ButtonAttachment> playAttachment;

    static constexpr int rowH = 46, nameW = 96, headerH = 64, footerH = 110, knobColW = 48;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBoxEditor)
};
