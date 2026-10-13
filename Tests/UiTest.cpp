// UiTest: builds the real editor offscreen, saves a screenshot of every page,
// then drives the orb roll with synthetic mouse and key events and checks the
// results. Usage: UiTest [output folder]
#include "../Source/PluginEditor.h"

static int failures = 0;
#define CHECK(cond, what) do { if (cond) std::cout << "  ok   " << what << "\n"; else { std::cout << "  FAIL " << what << "\n"; ++failures; } } while (0)

template <typename T> static T* findChild (juce::Component* root)
{
    if (auto* t = dynamic_cast<T*> (root)) if (root->isShowing() || root->isVisible()) return t;
    for (auto* c : root->getChildren()) if (auto* t = findChild<T> (c)) return t;
    return nullptr;
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static juce::MouseEvent ev (juce::Component* c, juce::Point<float> p, juce::ModifierKeys mods, int clicks, juce::Point<float> downAt, bool dragged)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0, 0, 0, 0, c, c,
                             juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), clicks, dragged);
}

static void click (ui::OrbRoll* r, juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1)
{
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    r->mouseDown (ev (r, p, down, clicks, p, false));
    r->mouseUp (ev (r, p, mods, clicks, p, false));
}

static void drag (ui::OrbRoll* r, juce::Point<float> a, juce::Point<float> b, juce::ModifierKeys mods = {})
{
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    r->mouseDown (ev (r, a, down, 1, a, false));
    for (int i = 1; i <= 8; ++i) r->mouseDrag (ev (r, a + (b - a) * (i / 8.0f), down, 1, a, true));
    r->mouseUp (ev (r, b, mods, 1, a, true));
}

// centre of the cell for a note and 16th step
static juce::Point<float> at (int midi, int step) { return { (float) (ui::OrbRoll::LBL + step * ui::OrbRoll::STEP + ui::OrbRoll::STEP / 2), (float) ((drumbox::rollTop - midi) * ui::OrbRoll::ROW + ui::OrbRoll::ROW / 2) }; }

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    out.createDirectory();

    DrumBoxProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);

    const char* names[] = { "1_drums", "2_trackA", "3_trackB", "4_trackC", "5_trackD", "6_song" };
    for (int i = 0; i < 6; ++i)
    {
        ed->keyPressed (juce::KeyPress ('1' + i));
        pump (50);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::PNGImageFormat png; auto f = out.getChildFile (juce::String (names[i]) + ".png");
        f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os);
    }

    std::cout << "orb roll on track A\n";
    ed->keyPressed (juce::KeyPress ('2'));
    pump (50);
    auto* roll = findChild<ui::OrbRoll> (ed.get());
    CHECK (roll != nullptr, "found the roll");
    if (roll == nullptr) return 1;
    auto& tr = proc.project.inst[0];
    tr.current = 2;                       // an empty variation to test in
    roll->patternSwitched();
    auto count = [&] { return tr.patterns[2].orbs.size(); };

    click (roll, at (60, 0)); pump (400);
    CHECK (count() == 1, "click on empty space adds an orb (after the double click window)");
    CHECK (std::abs (tr.patterns[2].orbs[0].start - 0.0) < 1e-9 && tr.patterns[2].orbs[0].midi == 60, "it lands on C4, step 1");

    click (roll, at (60, 7), {}, 1); roll->mouseDown (ev (roll, at (60, 7), juce::ModifierKeys::leftButtonModifier, 2, at (60, 7), false));
    roll->mouseUp (ev (roll, at (60, 7), {}, 2, at (60, 7), false)); pump (400);
    CHECK (count() == 1, "double click ahead does not add a stray orb");
    CHECK (std::abs (tr.patterns[2].orbs[0].len - 2.0) < 1e-9, "double click stretches the orb to 2 beats");

    click (roll, at (64, 0)); pump (400);
    CHECK (count() == 2, "clicking the row next to an orb adds a note there");
    click (roll, at (67, 9)); pump (400);
    CHECK (count() == 3, "third orb");

    click (roll, at (60, 2));                                       // select the long C
    click (roll, at (67, 9), juce::ModifierKeys::shiftModifier);    // shift adds G
    CHECK (roll->sel.size() == 2, "shift + click builds a selection of two");

    const double before = tr.patterns[2].orbs[0].start;
    drag (roll, at (60, 2), at (62, 4));
    const auto& o0 = tr.patterns[2].orbs[0];
    CHECK (std::abs (o0.start - (before + 0.5)) < 1e-9 && o0.midi == 62, "dragging moves the selected orb two steps right and up a tone");
    CHECK (tr.patterns[2].orbs[2].midi == 69 && std::abs (tr.patterns[2].orbs[2].start - 2.75) < 1e-9, "the other selected orb moved with it");

    const float v0 = o0.vel;
    drag (roll, at (62, 3), at (62, 3) + juce::Point<float> (0, -60), juce::ModifierKeys::altModifier);
    CHECK (tr.patterns[2].orbs[0].vel > v0 + 0.2f, "alt + drag up raises velocity");

    roll->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (count() == 1, "delete removes the selection");

    CHECK (proc.undo() && count() == 3, "undo brings them back");
    proc.redo();
    CHECK (count() == 1, "redo removes them again");

    // slide from E4 to a new G4
    roll->sel = { tr.patterns[2].orbs[0].id };
    click (roll, at (67, 4)); pump (400);
    const int gId = tr.patterns[2].orbs.back().id;
    roll->sel = { tr.patterns[2].orbs[0].id }; roll->repaint();
    const auto& e4 = tr.patterns[2].orbs[0];
    const float handleX = (float) (ui::OrbRoll::LBL + e4.start / 0.25 * ui::OrbRoll::STEP) + 3.0f + (float) (12 + e4.vel * 18) + 14.0f;
    drag (roll, { handleX, at (e4.midi, 0).y }, at (67, 4));
    CHECK (tr.patterns[2].slides.size() == 1 && tr.patterns[2].slides[0].to == gId, "dragging the ring onto another orb makes a slide");

    // box select everything
    drag (roll, at (72, 15) + juce::Point<float> (40, -10), at (58, 0) + juce::Point<float> (-10, 10));
    CHECK (roll->sel.size() == count(), "dragging a box on empty space selects every orb inside");

    // out of key
    proc.project.keyRoot = 0; proc.project.minor = true;
    click (roll, at (64, 12)); pump (400);   // E natural, not in C minor
    int flagged = 0; for (auto& o : tr.patterns[2].orbs) if (! proc.project.inKey (o.midi) && o.okKey != proc.project.keySig()) ++flagged;
    CHECK (flagged == 2, "both E naturals in C minor are flagged");
    proc.project.inst[0].patterns[2].orbs.back().okKey = proc.project.keySig();
    flagged = 0; for (auto& o : tr.patterns[2].orbs) if (! proc.project.inKey (o.midi) && o.okKey != proc.project.keySig()) ++flagged;
    CHECK (flagged == 1, "keeping one leaves only the other flagged");

    ed->keyPressed (juce::KeyPress ('2')); pump (50);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds());
    { juce::PNGImageFormat png; auto f = out.getChildFile ("7_trackA_edited.png"); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os); }

    // state round trip
    juce::MemoryBlock mb; proc.getStateInformation (mb);
    DrumBoxProcessor proc2; proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (proc2.project.inst[0].patterns[2].orbs.size() == count() && proc2.project.inst[0].patterns[2].slides.size() == 1, "save and reload keeps orbs and slides");
    CHECK (proc2.project.song.lanes[1][6] == proc.project.song.lanes[1][6], "save and reload keeps the song");

    ed.reset();
    std::cout << (failures == 0 ? "ALL PASSED\n" : "SOME FAILED\n");
    return failures == 0 ? 0 : 1;
}
