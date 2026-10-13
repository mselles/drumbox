#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace drumbox;

static juce::String pid (const char* base, int v) { return juce::String (base) + juce::String (v); }

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DrumBoxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int v = 0; v < numVoices; ++v)
    {
        const String name (voiceName (v));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("level", v), 1 }, name + " Level",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.8f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("tune", v), 1 }, name + " Tune",
                                                           NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("decay", v), 1 }, name + " Decay",
                                                           NormalisableRange<float> (0.25f, 3.0f, 0.0f, 0.5f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("pan", v), 1 }, name + " Pan",
                                                           NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing",
                                                       NormalisableRange<float> (50.0f, 75.0f, 0.1f), 54.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vintage", 1 }, "Vintage",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo",
                                                       NormalisableRange<float> (60.0f, 180.0f, 0.1f), 112.0f,
                                                       AudioParameterFloatAttributes().withLabel ("bpm")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
    return layout;
}

//==============================================================================
DrumBoxProcessor::DrumBoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int v = 0; v < numVoices; ++v)
    {
        levelP[(size_t) v] = apvts.getRawParameterValue (pid ("level", v));
        tuneP[(size_t) v]  = apvts.getRawParameterValue (pid ("tune", v));
        decayP[(size_t) v] = apvts.getRawParameterValue (pid ("decay", v));
        panP[(size_t) v]   = apvts.getRawParameterValue (pid ("pan", v));
    }
    swingP   = apvts.getRawParameterValue ("swing");
    vintageP = apvts.getRawParameterValue ("vintage");
    masterP  = apvts.getRawParameterValue ("master");
    bpmP     = apvts.getRawParameterValue ("bpm");
    playP    = apvts.getRawParameterValue ("play");

    loadDefaultPattern();
}

// A starter groove so the plugin makes music the moment you press play.
void DrumBoxProcessor::loadDefaultPattern()
{
    const char* rows[numVoices] = {
        "1000001010100000",   // kick
        "0000100000001000",   // snare
        "0000000000000000",   // rim
        "0000000000002000",   // clap
        "2121212121212101",   // closed hat (2 = accent)
        "0000000000000020",   // open hat
        "0000000000000000",   // tom
        "0000000000000000",   // cowbell
    };
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
            engine.setStep (v, s, rows[v][s] - '0');
}

//==============================================================================
bool DrumBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DrumBoxProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

void DrumBoxProcessor::readParams()
{
    for (size_t v = 0; v < (size_t) numVoices; ++v)
    {
        params.level[v] = levelP[v]->load();
        params.tune[v]  = tuneP[v]->load();
        params.decay[v] = decayP[v]->load();
        params.pan[v]   = panP[v]->load();
    }
    params.swing   = swingP->load() / 100.0f;
    params.vintage = vintageP->load();
    params.master  = masterP->load();
}

//==============================================================================
void DrumBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    readParams();

    // 1. Live MIDI notes play the kit directly
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.addMidiTrigger (meta.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity());
    }

    // 2. The sequencer follows the DAW when it's playing,
    //    otherwise its own clock when you press Play on the plugin.
    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) hostPpq = *p; else hostPlaying = false;
            if (auto b = pos->getBpm()) hostBpm = *b;
        }
    hostIsPlaying.store (hostPlaying);

    const bool internalPlaying = playP->load() > 0.5f;

    if (hostPlaying)
    {
        engine.addSequencerEvents (hostPpq, hostBpm, n, params.swing);
    }
    else if (internalPlaying)
    {
        if (! wasInternalPlaying) internalPpq = 0.0;     // restart at step 1
        const double bpm = bpmP->load();
        engine.addSequencerEvents (internalPpq, bpm, n, params.swing);
        internalPpq += bpm / 60.0 / getSampleRate() * n;
    }
    else
    {
        engine.clearCurrentStep();
    }
    wasInternalPlaying = internalPlaying && ! hostPlaying;

    // 3. Render
    buffer.clear();
    engine.render (buffer.getWritePointer (0), buffer.getWritePointer (1), n, params);
}

//==============================================================================
// Saves knobs + the pattern into the DAW session.
void DrumBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree pattern ("PATTERN");
    for (int v = 0; v < numVoices; ++v)
    {
        juce::String row;
        for (int s = 0; s < numSteps; ++s) row << engine.getStep (v, s);
        pattern.setProperty (juce::Identifier ("v" + juce::String (v)), row, nullptr);
    }
    state.removeChild (state.getChildWithName ("PATTERN"), nullptr);
    state.appendChild (pattern, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;

    if (auto pattern = state.getChildWithName ("PATTERN"); pattern.isValid())
        for (int v = 0; v < numVoices; ++v)
        {
            const auto row = pattern.getProperty (juce::Identifier ("v" + juce::String (v))).toString();
            for (int s = 0; s < numSteps && s < row.length(); ++s)
                engine.setStep (v, s, juce::jlimit (0, 2, row[s] - '0'));
        }
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* DrumBoxProcessor::createEditor() { return new DrumBoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumBoxProcessor(); }
