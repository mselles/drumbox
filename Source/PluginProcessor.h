#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Studio.h"
#include "ProjectIO.h"

//==============================================================================
// The bridge between the DAW and the Studio.
//
// The UI edits `project` on the message thread, then calls changed(). That
// publishes an immutable copy for the audio thread to read, so audio never
// sees a half finished edit and never waits on a lock. Old copies are freed
// on the message thread, never the audio thread.
//==============================================================================
class DrumBoxProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    DrumBoxProcessor();
    ~DrumBoxProcessor() override;

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
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //--------------------------------------------------------------------------
    // Editing (message thread only)
    drumbox::Project project;
    void checkpoint();            // call BEFORE an edit you want to be undoable
    void changed();               // call AFTER any edit: publishes it to the audio thread
    bool undo();
    bool redo();
    bool canUndo() const { return ! history.empty(); }
    bool canRedo() const { return ! future.empty(); }

    // Let the UI preview a note on a track (lock free, safe from any thread)
    void audition (int track, int midi, float vel)
    {
        const auto w = auditionFifo.write (1);
        if (w.blockSize1 > 0) auditionBuf[(size_t) w.startIndex1] = { track, midi, vel };
    }

    // Playback info for the UI
    std::atomic<double> playBeat { -1.0 };   // -1 when stopped
    std::atomic<bool> hostIsPlaying { false };
    double currentBpm() const;
    drumbox::StudioSettings currentSettings() const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;    // frees old project copies

    std::shared_ptr<const drumbox::Project> live;
    std::vector<std::shared_ptr<const drumbox::Project>> graveyard;
    std::vector<drumbox::Project> history, future;

    drumbox::Studio studio;
    struct Audition { int track, midi; float vel; };
    juce::AbstractFifo auditionFifo { 64 };
    std::array<Audition, 64> auditionBuf {};
    std::array<std::atomic<float>*, drumbox::numVoices> levelP {}, tuneP {}, decayP {}, panP {};
    std::atomic<float>* swingP = nullptr;
    std::atomic<float>* vintageP = nullptr;
    std::atomic<float>* masterP = nullptr;
    std::atomic<float>* bpmP = nullptr;
    std::atomic<float>* playP = nullptr;
    std::atomic<double> hostBpm { 0.0 };

    double internalBeat = 0.0;
    bool wasInternalPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBoxProcessor)
};
