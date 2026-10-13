#pragma once
#include "Widgets.h"
#include "../Exporter.h"
#include "../PluginProcessor.h"
#include <thread>

namespace ui
{
//==============================================================================
// The song: five lanes (drums, A to D) of 2 bar slots across the whole song.
//
//   pick a letter in a lane's header   that's the brush for the lane
//   click or drag along a lane         paints that variation into the slots
//   alt + click or drag                erases
//   right click a slot                 choose any variation for that slot
//
// The whole song fits on screen, up to 128 bars. Export bounces it to a WAV
// (or writes MIDI) in the background while you keep working.
class SongPage : public juce::Component, private juce::Timer
{
public:
    explicit SongPage (DrumBoxProcessor& p) : proc (p)
    {
        shorter.onClick = [this] { resize (-4); };
        longer.onClick  = [this] { resize (4); };
        exportWav.onClick  = [this] { startExport (true); };
        exportMidi.onClick = [this] { startExport (false); };
        exportWav.state = [] { return Pill::Accent; };
        for (auto* b : { &shorter, &longer, &exportWav, &exportMidi }) addAndMakeVisible (b);
        for (int l = 0; l < drumbox::numLanes; ++l)
        {
            auto* m = mutes.add (new PillButton ("M"));
            m->fontSize = 10.0f;
            m->state = [this, l] { return laneMuted (l) ? Pill::Accent : Pill::Off; };
            m->onClick = [this, l] { proc.checkpoint(); laneMuted (l) = ! laneMuted (l); proc.changed(); repaint(); };
            addAndMakeVisible (m);
        }
        brush.fill (0);
    }

    ~SongPage() override
    {
        cancel.store (true);
        if (worker != nullptr && worker->joinable()) worker->join();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (28, 16);
        auto top = r.removeFromTop (44);
        topArea = top;
        exportMidi.setBounds (top.removeFromRight (120)); top.removeFromRight (8);
        exportWav.setBounds (top.removeFromRight (130));
        top.removeFromLeft (64);
        shorter.setBounds (top.removeFromLeft (40)); top.removeFromLeft (180);
        longer.setBounds (top.removeFromLeft (40));
        r.removeFromTop (18);
        ruler = r.removeFromTop (40);
        lanesArea = r.removeFromTop (drumbox::numLanes * (laneH + 10));
        hintArea = r.removeFromTop (40);
        for (int l = 0; l < drumbox::numLanes; ++l)
            mutes[l]->setBounds (juce::Rectangle<int> (lanesArea.getX() + headW - 46, laneRect (l).getY() + 10, 34, 28));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& P = proc.project;
        // top row
        caps (g, "Length", topArea.withWidth (60), c::label, 10.0f);
        auto lenBox = juce::Rectangle<int> (shorter.getRight() + 8, topArea.getY(), 164, topArea.getHeight());
        glass (g, lenBox.toFloat(), 10.0f);
        g.setFont (mono (15.0f)); g.setColour (c::text);
        g.drawText (juce::String (P.song.slots * 2) + " BARS  " + timeString (P.song.slots * drumbox::slotBeats), lenBox, juce::Justification::centred);
        if (exporting.load())
        {
            auto bar = juce::Rectangle<int> (longer.getRight() + 24, topArea.getCentreY() - 3, 220, 6).toFloat();
            g.setColour (c::line); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (c::accentHi); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress.load()), 3.0f);
            caps (g, "Exporting", bar.toNearestInt().translated (0, -18).withHeight (14), c::label, 9.0f);
        }
        else if (status.isNotEmpty())
        {
            g.setFont (font (13.0f)); g.setColour (c::textSoft);
            g.drawText (status, longer.getRight() + 24, topArea.getY(), exportWav.getX() - longer.getRight() - 40, topArea.getHeight(), juce::Justification::centredLeft);
        }

        // ruler: bar numbers and minutes
        const auto tl = timeline();
        for (int k = 0; k < P.song.slots; k += 4)
        {
            const int x = slotX (k);
            g.setColour (c::lineStrong); g.fillRect (x, ruler.getY() + 22, 1, 12);
            g.setFont (mono (10.0f)); g.setColour (c::textSoft);
            g.drawText (juce::String (k * 2 + 1), x + 4, ruler.getY() + 4, 40, 14, juce::Justification::centredLeft);
            g.setColour (c::faint);
            g.drawText (timeString (k * drumbox::slotBeats), x + 4, ruler.getY() + 20, 50, 14, juce::Justification::centredLeft);
        }

        // lanes
        for (int l = 0; l < drumbox::numLanes; ++l)
        {
            const auto lr = laneRect (l);
            const auto head = lr.withWidth (headW - 12);
            raisedPanel (g, head.toFloat(), 14.0f);
            g.setFont (font (15.0f, true)); g.setColour (c::text);
            g.drawText (laneName (l), head.reduced (14, 0).withTrimmedTop (8).withHeight (22), juce::Justification::centredLeft);
            g.setFont (mono (10.0f)); g.setColour (c::label);
            g.drawText (laneSub (l), head.reduced (14, 0).withTrimmedTop (28).withHeight (16), juce::Justification::centredLeft);
            for (int i = 0; i < 8; ++i)
            {
                const auto br = brushRect (l, i).toFloat();
                const auto st = i == brush[(size_t) l] ? Pill::Accent : patternFilled (l, i) ? Pill::On : Pill::Dim;
                pill (g, br, st, 5.0f);
                g.setFont (mono (10.0f)); g.setColour (onPill (st));
                g.drawText (drumbox::patternLetter (i), br.toNearestInt(), juce::Justification::centred);
            }

            const auto track = lr.withTrimmedLeft (headW);
            deepPanel (g, track.toFloat(), 14.0f);
            for (int k = 4; k < P.song.slots; k += 4) { g.setColour (c::line); g.fillRect (slotX (k), track.getY() + 6, 1, track.getHeight() - 12); }

            // runs of the same variation become one block
            int k = 0;
            while (k < P.song.slots)
            {
                const int v = P.song.lanes[(size_t) l][(size_t) k];
                int e = k + 1;
                while (e < P.song.slots && P.song.lanes[(size_t) l][(size_t) e] == v) ++e;
                if (v >= 0)
                {
                    auto b = juce::Rectangle<int> (slotX (k) + 2, track.getY() + 8, slotX (e) - slotX (k) - 4, track.getHeight() - 16).toFloat();
                    const bool hot = v == 0;
                    pill (g, b, hot ? Pill::On : Pill::Accent, 9.0f);
                    for (int s = k + 1; s < e; ++s) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRect ((float) slotX (s), b.getY() + 6, 1.0f, b.getHeight() - 12); }
                    if (b.getWidth() > 16)
                    {
                        g.setFont (mono (12.0f)); g.setColour (onPill (hot ? Pill::On : Pill::Accent));
                        g.drawText (drumbox::patternLetter (v), b.toNearestInt().reduced (8, 0), juce::Justification::centredLeft);
                    }
                }
                k = e;
            }
        }

        // playhead
        const double beat = proc.playBeat.load();
        if (beat >= 0 && P.songMode)
        {
            const float x = (float) tl.getX() + (float) (beat / drumbox::slotBeats) * slotW();
            if (x <= tl.getRight())
            {
                g.setColour (c::accentHi.withAlpha (0.3f)); g.fillRect (x - 3.0f, (float) ruler.getY(), 6.0f, (float) (lanesArea.getBottom() - ruler.getY()));
                g.setColour (c::accentHi); g.fillRect (x - 1.0f, (float) ruler.getY(), 2.0f, (float) (lanesArea.getBottom() - ruler.getY()));
            }
        }

        g.setFont (font (12.5f)); g.setColour (c::label);
        g.drawFittedText (P.songMode ? "Pick a letter in a lane, then click or drag along the lane to paint it in. Alt + drag erases. Right click a slot for any variation."
                                     : "You're in LOOP mode: every track repeats the variation you're editing. Switch to SONG in the top bar to play this arrangement.",
                          hintArea.withTrimmedLeft (headW), juce::Justification::centredLeft, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int l = 0; l < drumbox::numLanes; ++l)
            for (int i = 0; i < 8; ++i)
                if (brushRect (l, i).contains (e.getPosition())) { brush[(size_t) l] = i; repaint(); return; }

        paintLane = -1;
        const int l = laneAt (e.y), k = slotAt (e.x);
        if (l < 0 || k < 0) return;
        if (e.mods.isPopupMenu()) { showSlotMenu (l, k); return; }
        paintLane = l;
        paintValue = e.mods.isAltDown() ? -1 : brush[(size_t) l];
        if (paintValue >= 0 && proc.project.song.lanes[(size_t) l][(size_t) k] == paintValue) paintValue = -1;   // clicking a filled slot clears it
        proc.checkpoint();
        setSlot (l, k);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (paintLane < 0) return;
        if (const int k = slotAt (e.x); k >= 0) setSlot (paintLane, k);
    }

    void timerCallback() override
    {
        repaint();
        if (! exporting.load())
        {
            stopTimer();
            if (worker != nullptr && worker->joinable()) worker->join();
            worker.reset();
        }
    }

private:
    juce::Rectangle<int> laneRect (int l) const { return { lanesArea.getX(), lanesArea.getY() + l * (laneH + 10), lanesArea.getWidth(), laneH }; }
    juce::Rectangle<int> timeline() const { return lanesArea.withTrimmedLeft (headW).reduced (0, 0); }
    juce::Rectangle<int> brushRect (int l, int i) const
    {
        const auto lr = laneRect (l);
        const int w = 17;
        return { lr.getX() + 12 + i * (w + 2), lr.getBottom() - 32, w, 22 };
    }
    float slotW() const { return (float) timeline().getWidth() / (float) proc.project.song.slots; }
    int slotX (int k) const { return timeline().getX() + (int) std::round (k * slotW()); }
    int slotAt (int x) const
    {
        const auto tl = timeline();
        if (x < tl.getX() || x >= tl.getRight()) return -1;
        return juce::jlimit (0, proc.project.song.slots - 1, (int) ((x - tl.getX()) / slotW()));
    }
    int laneAt (int y) const
    {
        for (int l = 0; l < drumbox::numLanes; ++l) if (laneRect (l).contains (laneRect (l).getCentreX(), y)) return l;
        return -1;
    }
    void setSlot (int l, int k)
    {
        auto& v = proc.project.song.lanes[(size_t) l][(size_t) k];
        if (v == paintValue) return;
        v = (int8_t) paintValue;
        proc.changed(); repaint();
    }
    void showSlotMenu (int l, int k)
    {
        juce::PopupMenu m;
        const int cur = proc.project.song.lanes[(size_t) l][(size_t) k];
        for (int i = 0; i < 8; ++i)
            m.addItem (i + 1, juce::String ("Variation ") + drumbox::patternLetter (i) + (patternFilled (l, i) ? "" : "  (empty)"), true, cur == i);
        m.addSeparator();
        m.addItem (100, "Empty", true, cur < 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }),
                         [safe = juce::Component::SafePointer<SongPage> (this), l, k] (int r)
                         {
                             if (safe == nullptr || r == 0) return;
                             safe->proc.checkpoint();
                             safe->proc.project.song.lanes[(size_t) l][(size_t) k] = (int8_t) (r == 100 ? -1 : r - 1);
                             safe->proc.changed(); safe->repaint();
                         });
    }
    void resize (int delta)
    {
        auto& s = proc.project.song.slots;
        const int n = juce::jlimit (4, drumbox::maxSlots, s + delta);
        if (n == s) return;
        proc.checkpoint(); s = n; proc.changed(); repaint();
    }

    bool& laneMuted (int l) { return l == 0 ? proc.project.drums.mute : proc.project.inst[(size_t) l - 1].mute; }
    bool patternFilled (int l, int i) const
    {
        if (l == 0) { for (auto& row : proc.project.drums.patterns[(size_t) i]) for (auto v : row) if (v) return true; return false; }
        return ! proc.project.inst[(size_t) l - 1].patterns[(size_t) i].empty();
    }
    juce::String laneName (int l) const { return l == 0 ? juce::String ("Drums") : juce::String ("Track ") + juce::String::charToString ((juce::juce_wchar) ("ABCD"[l - 1])); }
    juce::String laneSub (int l) const { return l == 0 ? juce::String ("DRUMBOX") : juce::String (drumbox::engineName (proc.project.inst[(size_t) l - 1].engine)); }
    juce::String timeString (double beats) const
    {
        const int secs = (int) std::round (beats * 60.0 / proc.currentBpm());
        return juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
    }

    void startExport (bool wav)
    {
        if (exporting.load()) return;
        chooser = std::make_unique<juce::FileChooser> (wav ? "Export the song as a WAV" : "Export the song as MIDI",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (wav ? "DrumBox song.wav" : "DrumBox song.mid"),
                                                       wav ? "*.wav" : "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, wav] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                file = file.withFileExtension (wav ? "wav" : "mid");
                const auto project = proc.project;
                const auto settings = proc.currentSettings();
                const double bpm = proc.currentBpm();
                if (! wav)
                {
                    const auto err = drumbox::exportMidi (project, bpm, file);
                    status = err.isEmpty() ? "Saved " + file.getFileName() : err;
                    repaint(); return;
                }
                const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
                exporting.store (true); progress.store (0.0f); cancel.store (false);
                worker = std::make_unique<std::thread> ([this, project, settings, bpm, sr, file]
                {
                    const auto err = drumbox::exportWav (project, settings, bpm, sr, file, &progress, &cancel);
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongPage> (this), err, file]
                    {
                        if (safe == nullptr) return;
                        safe->status = err.isEmpty() ? "Saved " + file.getFileName() + " in " + file.getParentDirectory().getFullPathName() : err;
                        safe->repaint();
                    });
                    exporting.store (false);
                });
                startTimerHz (20);
            });
    }

    DrumBoxProcessor& proc;
    PillButton shorter { "-" }, longer { "+" }, exportWav { "Export WAV" }, exportMidi { "Export MIDI" };
    juce::OwnedArray<PillButton> mutes;
    std::array<int, drumbox::numLanes> brush {};
    juce::Rectangle<int> topArea, ruler, lanesArea, hintArea;
    int paintLane = -1, paintValue = 0;
    static constexpr int headW = 200, laneH = 92;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<std::thread> worker;
    std::atomic<bool> exporting { false }, cancel { false };
    std::atomic<float> progress { 0.0f };
    juce::String status;
};

} // namespace ui
