#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace drumbox;

static juce::String pid (const char* base, int v) { return juce::String (base) + juce::String (v); }
static juce::SpinLock liveLock;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DrumBoxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    for (int v = 0; v < numVoices; ++v)
    {
        const String name (voiceName (v));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("level", v), 1 }, name + " Level", NormalisableRange<float> (0.0f, 1.0f), 0.8f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("tune", v), 1 }, name + " Tune", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("decay", v), 1 }, name + " Decay", NormalisableRange<float> (0.25f, 3.0f, 0.0f, 0.5f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid ("pan", v), 1 }, name + " Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (50.0f, 75.0f, 0.1f), 54.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vintage", 1 }, "Vintage", NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo", NormalisableRange<float> (60.0f, 180.0f, 0.1f), 112.0f, AudioParameterFloatAttributes().withLabel ("bpm")));
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
    swingP = apvts.getRawParameterValue ("swing");
    vintageP = apvts.getRawParameterValue ("vintage");
    masterP = apvts.getRawParameterValue ("master");
    bpmP = apvts.getRawParameterValue ("bpm");
    playP = apvts.getRawParameterValue ("play");

    project = makeDemoProject();
    live = std::make_shared<const Project> (project);
    startTimer (500);
}

DrumBoxProcessor::~DrumBoxProcessor() { stopTimer(); }

//==============================================================================
void DrumBoxProcessor::checkpoint()
{
    history.push_back (project);
    if (history.size() > 150) history.erase (history.begin());
    future.clear();
}

void DrumBoxProcessor::changed()
{
    auto next = std::make_shared<const Project> (project);
    {
        const juce::SpinLock::ScopedLockType sl (liveLock);
        std::swap (live, next);
    }
    graveyard.push_back (std::move (next));    // the old copy, freed later on this thread
    sendChangeMessage();
}

bool DrumBoxProcessor::undo()
{
    if (history.empty()) return false;
    future.push_back (project);
    project = history.back(); history.pop_back();
    changed();
    return true;
}

bool DrumBoxProcessor::redo()
{
    if (future.empty()) return false;
    history.push_back (project);
    project = future.back(); future.pop_back();
    changed();
    return true;
}

void DrumBoxProcessor::timerCallback()
{
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const std::shared_ptr<const Project>& p) { return p.use_count() == 1; }),
                     graveyard.end());
}

double DrumBoxProcessor::currentBpm() const
{
    const double h = hostBpm.load();
    return hostIsPlaying.load() && h > 0 ? h : (double) bpmP->load();
}

StudioSettings DrumBoxProcessor::currentSettings() const
{
    StudioSettings s;
    for (size_t v = 0; v < (size_t) numVoices; ++v)
    {
        s.drums.level[v] = levelP[v]->load(); s.drums.tune[v] = tuneP[v]->load();
        s.drums.decay[v] = decayP[v]->load(); s.drums.pan[v] = panP[v]->load();
    }
    s.drums.vintage = vintageP->load();
    s.drums.master = 1.0f;
    s.swing = swingP->load() / 100.0f;
    s.master = masterP->load();
    return s;
}

//==============================================================================
bool DrumBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DrumBoxProcessor::prepareToPlay (double sampleRate, int)
{
    studio.prepare (sampleRate);
}

void DrumBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    std::shared_ptr<const Project> p;
    {
        const juce::SpinLock::ScopedLockType sl (liveLock);
        p = live;
    }

    // Live MIDI plays the drums
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) studio.drumEngine().addMidiTrigger (meta.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity());
    }

    // Notes the UI wants to preview
    {
        const auto r = auditionFifo.read (auditionFifo.getNumReady());
        for (int i = 0; i < r.blockSize1; ++i) { const auto& a = auditionBuf[(size_t) (r.startIndex1 + i)]; studio.preview (a.track, a.midi, a.vel); }
        for (int i = 0; i < r.blockSize2; ++i) { const auto& a = auditionBuf[(size_t) (r.startIndex2 + i)]; studio.preview (a.track, a.midi, a.vel); }
    }

    bool hostPlaying = false;
    double hostPpq = 0.0, bpm = bpmP->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) hostPpq = *q; else hostPlaying = false;
            if (auto b = pos->getBpm()) { hostBpm.store (*b); if (hostPlaying) bpm = *b; }
        }
    hostIsPlaying.store (hostPlaying);

    const bool internalPlaying = playP->load() > 0.5f && ! hostPlaying;
    double beat = 0.0;
    bool playing = false;
    if (hostPlaying) { beat = hostPpq; playing = true; }
    else if (internalPlaying)
    {
        if (! wasInternalPlaying) internalBeat = 0.0;
        if (p->songMode && internalBeat >= p->songBeats()) internalBeat = 0.0;   // the song loops
        beat = internalBeat; playing = true;
        internalBeat += bpm / 60.0 / getSampleRate() * n;
    }
    wasInternalPlaying = internalPlaying;
    playBeat.store (playing ? beat : -1.0);

    buffer.clear();
    studio.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, *p, currentSettings(), beat, bpm, playing);
}

//==============================================================================
void DrumBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("PROJECT"), nullptr);
    state.removeChild (state.getChildWithName ("PATTERN"), nullptr);
    state.appendChild (projectToTree (project), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void DrumBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;

    Project loaded;
    if (treeToProject (state.getChildWithName ("PROJECT"), loaded))
    {
        project = std::move (loaded);
    }
    else if (auto old = state.getChildWithName ("PATTERN"); old.isValid())   // a session saved by version 0.1
    {
        for (int v = 0; v < numVoices; ++v)
        {
            const auto row = old.getProperty (juce::Identifier ("v" + juce::String (v))).toString();
            for (int s = 0; s < 16 && s < row.length(); ++s)
                project.drums.patterns[0][(size_t) v][(size_t) s] = (uint8_t) juce::jlimit (0, 2, row[s] - '0');
        }
    }
    history.clear(); future.clear();
    state.removeChild (state.getChildWithName ("PROJECT"), nullptr);
    state.removeChild (state.getChildWithName ("PATTERN"), nullptr);
    apvts.replaceState (state);
    changed();
}

juce::AudioProcessorEditor* DrumBoxProcessor::createEditor() { return new DrumBoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumBoxProcessor(); }
