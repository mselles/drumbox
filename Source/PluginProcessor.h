#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumEngine.h"

//==============================================================================
// The bridge between the DAW and DrumEngine. It owns the parameters (so the
// DAW can automate and save them), reads the host's tempo and beat position,
// and passes MIDI notes through as drum hits.
//==============================================================================
class DrumBoxProcessor : public juce::AudioProcessor
{
public:
    DrumBoxProcessor();
    ~DrumBoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Shared with the editor
    drumbox::DrumEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> hostIsPlaying { false };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadDefaultPattern();

private:
    void readParams();

    drumbox::EngineParams params;
    std::array<std::atomic<float>*, drumbox::numVoices> levelP {}, tuneP {}, decayP {}, panP {};
    std::atomic<float>* swingP = nullptr;
    std::atomic<float>* vintageP = nullptr;
    std::atomic<float>* masterP = nullptr;
    std::atomic<float>* bpmP = nullptr;
    std::atomic<float>* playP = nullptr;

    double internalPpq = 0.0;
    bool wasInternalPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBoxProcessor)
};
