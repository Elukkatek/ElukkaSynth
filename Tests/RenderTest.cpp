// Offline test: renders every factory preset and checks the output for NaNs, silence and
// clipping, measures render speed, and checks tempo detection on a synthetic loop.
#include <JuceHeader.h>
#include "../Source/PluginEditor.h"
#include "BinaryData.h"
#include "ClickProbe.h"
#include "UnisonTests.h"

static void playhead (double) {}

#include "ResynthEval.h"
#include "ReverbEval.h"

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 128.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

#include "Bench.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--bench")
        return Bench::run (argc > 2 ? juce::File (argv[2]) : juce::File(), argc > 3 ? juce::String (argv[3]) : juce::String());
    if (argc > 3 && juce::String (argv[1]) == "--bench-compare")
        return Bench::compare (juce::File (argv[2]), juce::File (argv[3]));
    // Resynthesis tools (see ResynthEval.h):
    //   --resynth <dir>                      accuracy against generated/system test sources (WAVs written to <dir>)
    //   --resynth-dir <samples> [outDir]     every audio file in a folder
    //   --resynth-compare <a.wav> <b.wav> [segSec]   band levels of two files side by side over time
    //   --resynth-tracks <source> [untilSec] the partial tracks found in a generated source
    //   --resynth-pitch <samples>            main note and note events per file
    //   --resynth-notes <bell|chords|melody|overlap> [from] [to]   per-frame notes of the tuning test sources
    //   --resynth-debug [source]             per-frame detail: harmonics, offline model vs real-time render, residual level
    //   (ELUKKA_NO_NOISE=1 / ELUKKA_NO_PARTIALS=1 render the partials or the residual alone)
    if (argc > 1 && juce::String (argv[1]) == "--reverb")
        return ReverbEval::run (argc > 2 ? juce::File (argv[2]) : juce::File());
    if (argc > 2 && juce::String (argv[1]) == "--resynth-dir")
        return ResynthEval::runFolder (juce::File (argv[2]), argc > 3 ? juce::File (argv[3]) : juce::File());
    if (argc > 2 && juce::String (argv[1]) == "--resynth-notes")
        return ResynthEval::debugNotes (argv[2], argc > 3 ? juce::String (argv[3]).getDoubleValue() : 0.0, argc > 4 ? juce::String (argv[4]).getDoubleValue() : 1.0e9);
    if (argc > 2 && juce::String (argv[1]) == "--resynth-pitch")
        return ResynthEval::runPitchFolder (juce::File (argv[2]));
    if (argc > 3 && juce::String (argv[1]) == "--resynth-compare")
        return ResynthEval::compareFiles (juce::File (argv[2]), juce::File (argv[3]), argc > 4 ? juce::String (argv[4]).getDoubleValue() : 0.1);
    if (argc > 2 && juce::String (argv[1]) == "--resynth-tracks")
        return ResynthEval::debugTracks (argv[2], argc > 3 ? juce::String (argv[3]).getDoubleValue() : 0.5);
    if (argc > 1 && juce::String (argv[1]) == "--resynth-debug")
        return ResynthEval::debugSaw (argc > 2 ? argv[2] : "saw");
    if (argc > 2 && juce::String (argv[1]) == "--resynth")
        return ResynthEval::run (juce::File (argv[2]));
    // --listen <dir>: unison listening renders
    if (argc > 2 && juce::String (argv[1]) == "--listen")
    {
        UnisonTests::renderListening (juce::File (argv[2]));
        return 0;
    }

    // --ui <dir>: snapshot every editor page to PNG instead of running the audio tests.
    if (argc > 2 && juce::String (argv[1]) == "--ui")
    {
        ElukkaAudioProcessor proc;
        proc.presets.load (argc > 3 ? juce::String (argv[3]).getIntValue() : 60);
        proc.setResynth (ResynthData::fromImage (juce::ImageCache::getFromMemory (BinaryData::logo_jpg, BinaryData::logo_jpgSize), "logo.jpg"));
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        std::vector<juce::Component*> pages;
        for (auto* c : ed->getChildren())
            for (auto* gc : c->getChildren())
                if (dynamic_cast<Page*> (gc) != nullptr) pages.push_back (gc);
        for (size_t i = 0; i < pages.size(); ++i)
        {
            for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible (i == j);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (argv[2]).getChildFile ("page" + juce::String ((int) i) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        std::cout << "Saved " << pages.size() << " page snapshots\n";

        // Knob options window: type a value, map to Macro 1, snapshot it.
        {
            KnobOptions opts (proc.apvts, "ampD");
            ed->addAndMakeVisible (opts);
            opts.setTopLeftPosition (400, 300);
            for (auto* c : opts.getChildren())
            {
                if (auto* te = dynamic_cast<juce::TextEditor*> (c)) { te->setText ("2.5"); te->onReturnKey(); }
            }
            for (auto* c : opts.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Macro 1") b->onClick();
            int mappedSlot = -1;
            for (int i = 0; i < P::numMatrixSlots; ++i)
                if ((int) proc.apvts.getRawParameterValue (P::mx (i, "dst"))->load() == P::DstAmpD
                    && (int) proc.apvts.getRawParameterValue (P::mx (i, "src"))->load() == P::SrcMacro1)
                    mappedSlot = i;
            std::cout << "Typed decay value: " << proc.apvts.getRawParameterValue ("ampD")->load()
                      << " s, mapped to Macro 1 in matrix row " << (mappedSlot + 1)
                      << " at amount " << (mappedSlot >= 0 ? proc.apvts.getRawParameterValue (P::mx (mappedSlot, "amt"))->load() : 0.0f) << "\n";
            {
                const float expected = 1.0f - proc.apvts.getParameter ("ampD")->getValue();
                const float got = mappedSlot >= 0 ? proc.apvts.getRawParameterValue (P::mx (mappedSlot, "amt"))->load() : 0.0f;
                std::cout << (std::abs (got - expected) < 0.001f ? "ok   " : "FAIL ") << "New mapping amount = what is left above the knob ("
                          << juce::String (expected, 3) << ")\n";
            }
            // a knob at 20 % gets +80 % (knob + amount = 100 %); a pitch knob keeps the full +48 st
            for (auto [id, value, expected] : { std::tuple<const char*, float, float> { "o1_level", 0.2f, 0.8f }, { "o1_coarse", 0.0f, 1.0f } })
            {
                auto* prm = proc.apvts.getParameter (id);
                prm->setValueNotifyingHost (prm->convertTo0to1 (value));
                KnobOptions o (proc.apvts, id);
                for (auto* c : o.getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Mod 1") b->onClick();
                float got = -1.0f;
                for (int i = 0; i < P::numMatrixSlots; ++i)
                    if ((int) proc.apvts.getRawParameterValue (P::mx (i, "dst"))->load() == P::destForParam (id)
                        && (int) proc.apvts.getRawParameterValue (P::mx (i, "src"))->load() == P::SrcMod1)
                        got = proc.apvts.getRawParameterValue (P::mx (i, "amt"))->load();
                std::cout << (std::abs (got - expected) < 0.001f ? "ok   " : "FAIL ") << id << " at " << value << ": Mod 1 mapped at amount " << got
                          << " (expected " << expected << ")\n";
            }
            auto img = ed->createComponentSnapshot (opts.getBounds().expanded (4), true, 1.0f);
            juce::File f = juce::File (argv[2]).getChildFile ("knob_options.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            // inside a real call-out box: Enter and Reset must close it
            auto opts2 = std::make_unique<KnobOptions> (proc.apvts, "ampD");
            auto* optsPtr = opts2.get();
            juce::CallOutBox box (*opts2, { 400, 300, 40, 40 }, ed.get());
            for (auto* c : optsPtr->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Reset to default") b->onClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            std::cout << "Reset closes the window: " << (box.isVisible() ? "no" : "yes") << "\n";
            {
                auto opts3 = std::make_unique<KnobOptions> (proc.apvts, "ampD");
                auto* o3 = opts3.get();
                juce::CallOutBox box3 (*opts3, { 400, 300, 40, 40 }, ed.get());
                for (auto* c : o3->getChildren())
                    if (auto* te = dynamic_cast<juce::TextEditor*> (c)) { te->setText ("0.75"); te->onReturnKey(); }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                std::cout << "Enter applies " << proc.apvts.getRawParameterValue ("ampD")->load() << " s and closes the window: " << (box3.isVisible() ? "no" : "yes") << "\n";
            }
            for (auto* c : opts.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Reset to default") b->onClick();
            std::cout << "Reset to default: Amp Decay back to " << proc.apvts.getRawParameterValue ("ampD")->load() << " s\n";
            ed->removeChildComponent (&opts);
        }

        // Double-click value box: type 1.25 and press Enter
        {
            ValueEntry entry (*proc.apvts.getParameter ("ampD"));
            entry.editor.setText ("1.25");
            entry.editor.onReturnKey();
            std::cout << "Value box: typed 1.25 -> Amp Decay " << proc.apvts.getRawParameterValue ("ampD")->load() << " s\n";
        }

        // Pattern arrows and the Reset button
        for (auto* pg : pages)
            for (auto* c : pg->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == ">")
                {
                    const auto before = proc.pattern.name;
                    b->onClick();
                    std::cout << "Next pattern arrow: " << before << " -> " << proc.pattern.name << "\n";
                }
        proc.presets.load (20);
        for (auto* c : ed->getChildren())
            for (auto* gc : c->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (gc); b != nullptr && b->getButtonText() == "Reset")
                {
                    const auto before = proc.presets.currentName;
                    b->onClick();
                    std::cout << "Reset button: " << before << " -> " << proc.presets.currentName << "\n";
                }

        // Clicking the logo opens the credits pop-up
        {
            // the logo's click area (the editor is not on screen here, so hit-testing by position does not work)
            juce::Component* hit = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                for (auto* ch : c.getChildren())
                {
                    if (ch->getBounds() == juce::Rectangle<int> (14, 6, 150, 66)) hit = ch;
                    find (*ch);
                }
            };
            find (*ed);
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const juce::Point<float> at { 20.0f, 20.0f };
            if (hit != nullptr)
                hit->mouseUp (juce::MouseEvent (src, at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, hit, hit, juce::Time::getCurrentTime(),
                                                at, juce::Time::getCurrentTime(), 1, false));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            juce::CallOutBox* box = nullptr;
            for (auto* c : ed->getChildren()) if (auto* b = dynamic_cast<juce::CallOutBox*> (c)) box = b;
            juce::String shown;
            if (box != nullptr)
                for (auto* c : box->getChildren()) if (auto* l = dynamic_cast<juce::Label*> (c)) shown = l->getText();
            const bool ok = shown == "Vibecoded by Elukka\nAll the work done by Claude\nA special thanks to Aku Kangas";
            std::cout << (ok ? "ok   " : "FAIL ") << "Logo click shows the credits: " << shown.replace ("\n", " / ") << "\n";
            auto img = ed->createComponentSnapshot (ed->getLocalBounds().removeFromTop (180).removeFromLeft (400), true, 1.0f);
            juce::File f = juce::File (argv[2]).getChildFile ("credits.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            if (box != nullptr) box->dismiss();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }

        // Coarse knob detents: drag a coarse knob from -48 to +48 one pixel at a time. Every semitone must be reachable,
        // each octave must hold for about 18 pixels, and grabbing the knob at a value
        // next to an octave must not jump.
        {
            Knob k (proc.apvts, P::osc (0, "coarse"), "Coarse");
            k.slider.setDetents (12.0, 4.8);   // as on the oscillator pages
            k.slider.setMouseDragSensitivity (320);
            ed->addAndMakeVisible (k);
            k.setBounds (0, 0, 60, 74);
            auto* coarseParam = proc.apvts.getParameter (P::osc (0, "coarse"));
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const juce::Point<float> start { 30.0f, 30.0f };
            auto ev = [&] (juce::Point<float> pos, bool dragged)
            {
                return juce::MouseEvent (src, pos, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &k.slider, &k.slider, juce::Time::getCurrentTime(),
                                         start, juce::Time::getCurrentTime(), 1, dragged);
            };
            auto value = [&] { return (int) std::lround (proc.apvts.getRawParameterValue (P::osc (0, "coarse"))->load()); };
            coarseParam->setValueNotifyingHost (coarseParam->convertTo0to1 (-48.0f));
            std::map<int, int> pixels;
            k.slider.mouseDown (ev (start, false));
            for (int dy = 0; dy <= 600 && value() < 48; ++dy)
            {
                k.slider.mouseDrag (ev ({ start.x, start.y - (float) dy }, true));
                ++pixels[value()];
            }
            k.slider.mouseUp (ev (start, true));
            bool allReached = true;
            double octavePx = 0.0, otherPx = 0.0;
            int octaves = 0, others = 0;
            for (int v = -48; v <= 48; ++v)
            {
                allReached &= pixels[v] > 0;
                if (v == -48 || v == 48) continue;   // the ends only have half a stop
                if (v % 12 == 0) { octavePx += pixels[v]; ++octaves; } else { otherPx += pixels[v]; ++others; }
            }
            octavePx /= octaves; otherPx /= others;
            // grab at +11 and nudge by one pixel either way: stays next to where it was
            coarseParam->setValueNotifyingHost (coarseParam->convertTo0to1 (11.0f));
            k.slider.mouseDown (ev (start, false));
            k.slider.mouseDrag (ev ({ start.x, start.y - 1.0f }, true));
            const int up1 = value();
            k.slider.mouseDrag (ev ({ start.x, start.y + 1.0f }, true));
            const int down1 = value();
            k.slider.mouseUp (ev (start, true));
            const bool ok = allReached && std::abs (octavePx - 18.0) < 2.0 && otherPx > 1.8 && up1 >= 11 && up1 <= 12 && down1 >= 10 && down1 <= 11;
            std::cout << (ok ? "ok   " : "FAIL ") << "Coarse detents: every semitone reachable " << (allReached ? "yes" : "no") << ", octaves hold "
                      << juce::String (octavePx, 1) << " px vs " << juce::String (otherPx, 1) << " px for other values; grabbed at +11, 1 px up -> "
                      << up1 << ", 1 px down -> " << down1 << "\n";
            coarseParam->setValueNotifyingHost (coarseParam->getDefaultValue());
            ed->removeChildComponent (&k);
        }

        // Modulation amount detents (pitch destinations): a 400 px amount slider like the Matrix page's, dragged from -1
        // to +1 one pixel at a time. With the row on a pitch destination each octave (0.25 = 12 st) holds for ~18 px;
        // on another destination there are no stops.
        {
            auto sweep = [&] (bool pitch, double& octavePx, int& grabJumpPx)
            {
                DetentSlider sl;
                sl.setSliderStyle (juce::Slider::LinearHorizontal);
                sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                sl.setRange (-1.0, 1.0, 0.0);
                setPitchAmountDetents (sl);
                sl.detentsActive = [pitch] { return pitch; };
                ed->addAndMakeVisible (sl);
                sl.setBounds (0, 0, 400, 24);
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto ev = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged)
                {
                    return juce::MouseEvent (src, pos, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &sl, &sl, juce::Time::getCurrentTime(),
                                             down, juce::Time::getCurrentTime(), 1, dragged);
                };
                sl.setValue (-1.0, juce::dontSendNotification);
                const juce::Point<float> start { (float) sl.getPositionOfValue (-1.0), 12.0f };
                std::map<int, int> atOctave;   // pixels spent on each octave value
                sl.mouseDown (ev (start, start, false));
                for (int dx = 0; dx <= 420; ++dx)
                {
                    sl.mouseDrag (ev ({ start.x + (float) dx, 12.0f }, start, true));
                    const double v = sl.getValue(), oct = std::round (v / 0.25) * 0.25;
                    if (std::abs (v - oct) < 1.0e-9) ++atOctave[(int) std::lround (oct * 4.0)];
                }
                sl.mouseUp (ev (start, start, true));
                octavePx = 0.0;
                for (int o = -3; o <= 3; ++o) octavePx += atOctave[o];
                octavePx /= 7.0;
                // grab the thumb at 0.3 and do not move: no jump
                sl.setValue (0.3, juce::dontSendNotification);
                const juce::Point<float> at { (float) sl.getPositionOfValue (0.3), 12.0f };
                sl.mouseDown (ev (at, at, false));
                sl.mouseDrag (ev (at, at, true));
                grabJumpPx = (int) std::lround (std::abs (sl.getPositionOfValue (sl.getValue()) - at.x));
                sl.mouseUp (ev (at, at, true));
                ed->removeChildComponent (&sl);
            };
            double pitchPx = 0.0, otherPx = 0.0;
            int pitchJump = 0, otherJump = 0;
            sweep (true, pitchPx, pitchJump);
            sweep (false, otherPx, otherJump);
            const bool ok = std::abs (pitchPx - 18.0) < 3.0 && otherPx < 2.0 && pitchJump <= 1 && otherJump <= 1;
            std::cout << (ok ? "ok   " : "FAIL ") << "Mod amount detents: pitch destination octaves hold " << juce::String (pitchPx, 1)
                      << " px, other destinations " << juce::String (otherPx, 1) << " px; grabbing the thumb moves it " << pitchJump << " px\n";
        }

        // Drag the Distortion box from slot 2 to the end of the FX chain with synthetic mouse events
        for (auto* pg : pages)
            for (auto* c : pg->getChildren())
                if (auto* box = dynamic_cast<FxBox*> (c); box != nullptr && box->fxId == P::FxDist)
                {
                    auto src = juce::Desktop::getInstance().getMainMouseSource();
                    auto ev = [&] (juce::Point<float> pos, bool dragged)
                    {
                        return juce::MouseEvent (src, pos, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, box, box, juce::Time::getCurrentTime(),
                                                 { 20.0f, 10.0f }, juce::Time::getCurrentTime(), 1, dragged);
                    };
                    box->mouseDown (ev ({ 20.0f, 10.0f }, false));
                    for (float dx = 0; dx <= 1000.0f; dx += 100.0f) box->mouseDrag (ev ({ 20.0f + dx, 10.0f }, true));
                    box->mouseUp (ev ({ 1020.0f, 10.0f }, true));
                    juce::StringArray names;
                    for (auto& f : proc.fxOrder) names.add (P::fxKeys[f.load()]);
                    std::cout << "FX order after dragging Distortion to the end: " << names.joinIntoString (" > ") << "\n";
                    for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible (pages[j] == pg);
                    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    juce::File f = juce::File (argv[2]).getChildFile ("fx_dragged.png");
                    f.deleteFile();
                    juce::FileOutputStream os (f);
                    juce::PNGImageFormat().writeImageToStream (img, os);
                    goto dragDone;
                }
        dragDone:

        // Scale the UI to 150%
        ed->setSize (1710, 1185);
        {
            for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible (j == 0);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (argv[2]).getChildFile ("scaled.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "Scaled editor: " << ed->getWidth() << "x" << ed->getHeight() << ", saved scale " << proc.uiScale << "\n";
        }
        ed->setSize (1140, 790);

        // Drag-and-drop: write a 4-bar 128 BPM click loop to a wav and drop it on the Resynth page.
        juce::File wavFile = juce::File (argv[2]).getChildFile ("drop_test.wav");
        {
            const double sr = 44100.0, bpm = 128.0;
            const int len = (int) (sr * 60.0 / bpm * 16);
            juce::AudioBuffer<float> loop (1, len);
            loop.clear();
            juce::Random r (2);
            for (int b = 0; b < 32; ++b)
                for (int s = 0; s < 3000; ++s)
                {
                    int i = (int) (b * sr * 30.0 / bpm) + s;
                    if (i < len) loop.setSample (0, i, (b % 2 ? 0.4f : 1.0f) * std::exp (-s / 600.0f) * (r.nextFloat() * 2.0f - 1.0f));
                }
            wavFile.deleteFile();
            juce::WavAudioFormat wav;
            auto os = wavFile.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 1, 16, {}, 0));
            os.release();
            w->writeFromAudioSampleBuffer (loop, 0, len);
        }
        for (auto* c : pages)
            if (auto* rp = dynamic_cast<juce::FileDragAndDropTarget*> (c))
            {
                juce::StringArray files { wavFile.getFullPathName() }, notAudio { juce::File (argv[2]).getChildFile ("page0.png").getFullPathName() };
                std::cout << "Accepts wav: " << rp->isInterestedInFileDrag (files) << "\n";
                rp->filesDropped (files, 10, 10);
                auto d = proc.getResynth();
                std::cout << "After drop: " << (d ? d->name : juce::String ("nothing")) << ", BPM param "
                          << proc.apvts.getRawParameterValue ("rs_bpm")->load() << "\n";
                for (auto* gc : c->getChildren())
                    if (auto* lbl = dynamic_cast<juce::Label*> (gc); lbl != nullptr && lbl->isEditable())
                    {
                        lbl->setText ("97.5", juce::sendNotificationSync);
                        std::cout << "Typed loop BPM 97.5 -> BPM param " << proc.apvts.getRawParameterValue ("rs_bpm")->load() << "\n";
                    }
                {
                    for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible (pages[j] == c);
                    proc.uiRsPos = 0.4f;   // pretend a note is playing 40% of the way through
                    dynamic_cast<Page*> (c)->tick();
                    auto shot = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    juce::File af = juce::File (argv[2]).getChildFile ("drop_audio.png");
                    af.deleteFile();
                    juce::FileOutputStream aos (af);
                    juce::PNGImageFormat().writeImageToStream (shot, aos);
                }
                juce::File txt = wavFile.getSiblingFile ("not_media.txt");
                txt.replaceWithText ("x");
                juce::StringArray image { juce::File (argv[2]).getChildFile ("page1.png").getFullPathName() },
                                  other { txt.getFullPathName() };
                std::cout << "Accepts png: " << rp->isInterestedInFileDrag (image) << ", accepts txt: " << rp->isInterestedInFileDrag (other) << "\n";
                rp->filesDropped (image, 10, 10);
                d = proc.getResynth();
                std::cout << "After image drop: " << (d ? d->name + (d->isImage ? " (image)" : " (audio)") : juce::String ("nothing")) << "\n";
                for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible (pages[j] == c);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream pos (juce::File (argv[2]).getChildFile ("drop.png"));
                pos.setPosition (0); pos.truncate();
                juce::PNGImageFormat().writeImageToStream (img, pos);
            }
        return 0;
    }

    // --host <plugin.vst3>: load the built plugin like a DAW would, play it, and round-trip its state.
    if (argc > 2 && juce::String (argv[1]) == "--host")
    {
        juce::AudioPluginFormatManager fm;
        fm.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> found;
        juce::KnownPluginList list;
        for (auto* format : fm.getFormats())
            list.scanAndAddFile (argv[2], true, found, *format);
        if (found.isEmpty()) { std::cout << "Plugin not found\n"; return 1; }
        std::cout << "Found: " << found[0]->name << " (" << found[0]->pluginFormatName << ", instrument="
                  << (found[0]->isInstrument ? "yes" : "no") << ")\n";
        juce::String err;
        auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
        if (inst == nullptr) { std::cout << "Load failed: " << err << "\n"; return 1; }
        inst->setPlayConfigDetails (0, 2, 48000.0, 512);
        inst->prepareToPlay (48000.0, 512);
        std::cout << "Parameters: " << inst->getParameters().size() << "\n";
        // the patch a new instance starts with (should be the Init preset: three sines, FM chain Osc 3 -> Osc 2 -> Osc 1)
        for (auto* prm : inst->getParameters())
        {
            const auto name = prm->getName (64);
            if (name.startsWith ("Osc ") && (name.endsWith (" Wave") || name.endsWith (" FM Source") || name.endsWith (" Level") || name.endsWith (" On")))
                std::cout << "  start: " << name << " = " << prm->getCurrentValueAsText() << "\n";
        }

        auto render = [&] (int note)
        {
            juce::AudioBuffer<float> buf (2, 512);
            float peak = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                if (b == 150) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                inst->processBlock (buf, midi);
                peak = std::max (peak, buf.getMagnitude (0, 512));
            }
            return peak;
        };
        std::cout << "Default patch peak: " << render (60) << "\n";

        // Change a parameter, save, change again, restore and compare.
        auto* cutoff = inst->getParameters()[0];
        cutoff->setValueNotifyingHost (0.25f);
        juce::MemoryBlock state;
        inst->getStateInformation (state);
        cutoff->setValueNotifyingHost (0.9f);
        inst->setStateInformation (state.getData(), (int) state.getSize());
        std::cout << "State round-trip: " << (std::abs (cutoff->getValue() - 0.25f) < 0.01f ? "ok" : "FAIL")
                  << " (" << state.getSize() << " bytes)\n";

        if (auto* ed = inst->createEditorIfNeeded())
        {
            std::cout << "Editor: " << ed->getWidth() << "x" << ed->getHeight() << "\n";
            delete ed;
        }
        std::cout << "Peak after restore: " << render (48) << "\n";
        inst->releaseResources();
        return 0;
    }

    const double sr = 48000.0;
    const int block = 512;
    const double seconds = 4.0;
    juce::String wavDir = argc > 1 ? juce::String (argv[1]) : juce::String();

    ElukkaAudioProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    int failures = 0;
    double worstRealtime = 0.0;
    auto& entries = proc.presets.entries;
    std::cout << "Presets: " << entries.size() << "\n";

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        if (! entries[(size_t) i].isFactory) continue;
        proc.presets.load (i);
        proc.prepareToPlay (sr, block);
        ph.ppq = 0.0;

        const bool isSeq = proc.apvts.getRawParameterValue ("sq_on")->load() > 0.5f;
        const bool isPoly = proc.apvts.getRawParameterValue ("polyMode")->load() < 0.5f;
        juce::AudioBuffer<float> out (2, (int) (sr * seconds));
        juce::AudioBuffer<float> buf (2, block);
        const int numBlocks = out.getNumSamples() / block;

        auto start = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, isSeq ? 48 : 57, (juce::uint8) 100), 0);
                if (isPoly && ! isSeq)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
                }
            }
            if (b == numBlocks * 3 / 4)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * block, buf, ch, 0, block);
            ph.ppq += block / sr * ph.bpm / 60.0;
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        const double realtime = ms / (seconds * 1000.0);
        worstRealtime = std::max (worstRealtime, realtime);

        bool nan = false;
        float peak = 0.0f;
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < out.getNumSamples(); ++s)
            {
                float v = out.getSample (ch, s);
                if (! std::isfinite (v)) nan = true;
                peak = std::max (peak, std::abs (v));
                sum += v * v;
            }
        const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (2.0 * out.getNumSamples())));
        const float peakDb = juce::Decibels::gainToDecibels (peak);
        const bool bad = nan || peak < 1.0e-3f || peakDb > 3.0f;
        if (bad) ++failures;

        std::cout << (bad ? "FAIL " : "ok   ") << juce::String (entries[(size_t) i].category).paddedRight (' ', 12)
                  << juce::String (entries[(size_t) i].name).paddedRight (' ', 22)
                  << " peak " << juce::String (peakDb, 1).paddedLeft (' ', 6) << " dB  rms " << juce::String (rmsDb, 1).paddedLeft (' ', 6)
                  << " dB  cpu " << juce::String (realtime * 100.0, 1) << "%" << (nan ? "  NaN!" : "") << "\n";

        if (wavDir.isNotEmpty())
        {
            juce::File f = juce::File (wavDir).getChildFile (juce::File::createLegalFileName (entries[(size_t) i].name) + ".wav");
            f.deleteFile();
            juce::WavAudioFormat wav;
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)))
                {
                    os.release();
                    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
                }
        }
    }

    // Tempo detection on a synthetic 4-bar drum loop at several tempos.
    for (double bpm : { 90.0, 124.0, 128.0, 140.0, 174.0 })
    {
        const int beats = 16;
        const int len = (int) (sr * 60.0 / bpm * beats);
        juce::AudioBuffer<float> loop (1, len);
        loop.clear();
        juce::Random r (1);
        for (int b = 0; b < beats * 2; ++b)
        {
            int start = (int) (b * sr * 30.0 / bpm);
            float amp = b % 2 == 0 ? 1.0f : 0.4f;
            for (int s = 0; s < 3000 && start + s < len; ++s)
                loop.setSample (0, start + s, amp * std::exp (-s / 600.0f) * (r.nextFloat() * 2.0f - 1.0f));
        }
        auto d = ResynthData::fromAudio (loop, sr, "loop");
        std::cout << "Tempo detect: true " << bpm << " -> detected " << (d ? d->detectedBpm : -1.0) << "\n";
    }

    // ---- Resynthesis tests ----
    auto setP = [] (ElukkaAudioProcessor& pr, const char* id, float v)
    {
        auto* p = pr.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto renderNotes = [&] (ElukkaAudioProcessor& pr, std::vector<int> notes, double secs, double* cpu = nullptr)
    {
        pr.prepareToPlay (sr, block);
        juce::AudioBuffer<float> out (2, ((int) (sr * secs) / block) * block);   // whole blocks only
        out.clear();
        juce::AudioBuffer<float> buf (2, block);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < out.getNumSamples() / block; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127), 0);
            pr.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
        }
        if (cpu) *cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (secs * 1000.0);
        return out;
    };
    // Magnitude (linear amplitude) of the strongest component within +-2% of `hz`, Hann window, 1 s from 0.5 s.
    auto measure = [&] (const juce::AudioBuffer<float>& b, int ch, float hz)
    {
        const int order = 15, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
        const int start = (int) (0.5 * sr);
        for (int i = 0; i < N && start + i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (ch, start + i) * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (x.data());
        const float binHz = (float) sr / N;
        float best = 0.0f, bestHz = 0.0f;
        for (int k = (int) (hz * 0.98f / binHz); k <= (int) (hz * 1.02f / binHz); ++k)
            if (x[(size_t) k] > best)
            {
                // parabolic interpolation on log magnitude for sub-bin frequency
                const float a = std::log (x[(size_t) k - 1] + 1e-12f), b2 = std::log (x[(size_t) k] + 1e-12f), c = std::log (x[(size_t) k + 1] + 1e-12f);
                const float d = a - 2.0f * b2 + c;
                const float p = std::abs (d) > 1e-9f ? 0.5f * (a - c) / d : 0.0f;
                best = x[(size_t) k];
                bestHz = ((float) k + p) * binHz;
            }
        return std::make_pair (best * 4.0f / N, bestHz);
    };

    {
        // Stereo test tone with inharmonic partials and one partial panned left.
        struct Part { float hz, amp, left, right; };
        const std::vector<Part> parts { { 110.0f, 0.30f, 1.0f, 1.0f }, { 220.0f, 0.20f, 1.0f, 1.0f }, { 331.7f, 0.15f, 1.0f, 0.4f },
                                        { 447.5f, 0.10f, 1.0f, 1.0f }, { 1234.5f, 0.05f, 1.0f, 1.0f }, { 3100.0f, 0.03f, 1.0f, 1.0f } };
        const int len = (int) (sr * 2.5);
        juce::AudioBuffer<float> tone (2, len);
        tone.clear();
        for (auto& pt : parts)
            for (int i = 0; i < len; ++i)
            {
                const float v = pt.amp * std::sin (juce::MathConstants<float>::twoPi * pt.hz * (float) i / (float) sr);
                tone.addSample (0, i, v * pt.left);
                tone.addSample (1, i, v * pt.right);
            }
        auto data = ResynthData::fromAudio (tone, sr, "tone");

        ElukkaAudioProcessor rp;
        rp.setPlayHead (&ph);
        rp.setPlayConfigDetails (0, 2, sr, block);
        setP (rp, "o1_wave", (float) P::WResynth); setP (rp, "o1_level", 1.0f); setP (rp, "master", 0.0f);
        setP (rp, "rs_sync", 0.0f); setP (rp, "rs_loop", 0.0f); setP (rp, "ampA", 0.001f); setP (rp, "ampS", 1.0f);
        rp.setResynth (data);

        auto out = renderNotes (rp, { 60 }, 2.0);
        float worstDb = 0.0f, worstHz = 0.0f;
        for (auto& pt : parts)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto o = measure (tone, ch, pt.hz), r = measure (out, ch, pt.hz);
                worstDb = std::max (worstDb, std::abs (juce::Decibels::gainToDecibels (r.first / o.first)));
                worstHz = std::max (worstHz, std::abs (r.second - o.second));
            }
        const bool replicaOk = worstDb < 1.5f && worstHz < 1.0f;
        if (! replicaOk) ++failures;
        std::cout << (replicaOk ? "ok   " : "FAIL ") << "Replica at C3, tune 0%: worst partial level error "
                  << juce::String (worstDb, 2) << " dB, worst frequency error " << juce::String (worstHz, 2) << " Hz\n";

        // Pitch detection: the tone's fundamental is 110 Hz (A2); the nearest C is C3 (130.81 Hz), 3 semitones up
        const bool rootOk = std::abs (1200.0f * std::log2 (data->detectedFundamentalHz / 110.0f)) < 2.0f && data->numNoteEvents == 1;
        if (! rootOk) ++failures;
        std::cout << (rootOk ? "ok   " : "FAIL ") << "Detected pitch " << juce::String (data->detectedFundamentalHz, 2)
                  << " Hz (share " << juce::String (data->pitchConfidence, 2) << "), " << data->numNoteEvents << " note event\n";

        // Tune 100% at middle C: the whole tone moves to the nearest C, C3, 3 semitones up (every partial by the same
        // ratio, so the inharmonic spacing stays): 220 -> 261.6, 331.7 -> 394.5, 1234.5 -> 1468.0 Hz; nothing left at 220 Hz
        setP (rp, "rs_tune", 100.0f);
        out = renderNotes (rp, { 60 }, 2.0);
        const float ratio3 = std::exp2 (3.0f / 12.0f);
        auto at = [&] (float hz) { return juce::Decibels::gainToDecibels (measure (out, 0, hz).first, -120.0f); };
        float worstTuned = 0.0f;
        for (auto& pt : parts) worstTuned = std::max (worstTuned, std::abs (1200.0f * std::log2 (measure (out, 0, pt.hz * ratio3).second / (pt.hz * ratio3))));
        const bool tunedOk = worstTuned < 2.0f && at (220.0f) < -50.0f;   // only the residual's noise floor may remain at 220 Hz
        if (! tunedOk) ++failures;
        std::cout << (tunedOk ? "ok   " : "FAIL ") << "Tune 100%: every partial up 3 semitones (A2 -> C3), worst error " << juce::String (worstTuned, 2)
                  << " c (220 Hz -> " << juce::String (measure (out, 0, 220.0f * ratio3).second, 1) << " Hz), level left at 220 Hz " << juce::String (at (220.0f), 1) << " dB\n";

        // Playing an octave up doubles every partial
        setP (rp, "rs_tune", 0.0f);
        out = renderNotes (rp, { 72 }, 2.0);
        auto up = measure (out, 0, 2 * 331.7f);
        const bool octaveOk = std::abs (up.second - 2 * 331.7f) < 1.5f;
        if (! octaveOk) ++failures;
        std::cout << (octaveOk ? "ok   " : "FAIL ") << "C4 transposes up an octave: 331.7 Hz partial -> " << juce::String (up.second, 1) << " Hz\n";

        // Lift raises the quietest partial relative to the loudest
        auto ratioDb = [&] { return juce::Decibels::gainToDecibels (measure (out, 0, 3100.0f).first / measure (out, 0, 110.0f).first); };
        out = renderNotes (rp, { 60 }, 2.0);
        const float before = ratioDb();
        setP (rp, "rs_lift", 100.0f);
        out = renderNotes (rp, { 60 }, 2.0);
        const float after = ratioDb();
        const bool liftOk = after > before + 10.0f;
        if (! liftOk) ++failures;
        std::cout << (liftOk ? "ok   " : "FAIL ") << "Lift 100%: quietest partial vs loudest " << juce::String (before, 1) << " dB -> "
                  << juce::String (after, 1) << " dB\n";

        // Gate: partials more than the threshold below the loudest at the moment fade out, before Lift. The 3100 Hz partial
        // is 20 dB below the 110 Hz one: a gate at -26 dB leaves the tone alone, at -12 dB it removes 3100 Hz (and only
        // the partials below -12 dB: 110 and 220 Hz stay), and with Lift 100% on top it stays removed.
        {
            auto levelDb = [&] (float hz) { return juce::Decibels::gainToDecibels (measure (out, 0, hz).first, -140.0f); };
            setP (rp, "rs_lift", 0.0f);
            out = renderNotes (rp, { 60 }, 2.0);
            const float ref3100 = levelDb (3100.0f), ref110 = levelDb (110.0f), ref220 = levelDb (220.0f);
            setP (rp, "rs_gate", -26.0f);
            out = renderNotes (rp, { 60 }, 2.0);
            const float open3100 = levelDb (3100.0f) - ref3100;
            setP (rp, "rs_gate", -12.0f);
            out = renderNotes (rp, { 60 }, 2.0);
            const float shut3100 = levelDb (3100.0f) - ref3100, kept110 = levelDb (110.0f) - ref110, kept220 = levelDb (220.0f) - ref220;
            setP (rp, "rs_lift", 100.0f);
            out = renderNotes (rp, { 60 }, 2.0);
            const float lifted3100 = levelDb (3100.0f) - ref3100;
            setP (rp, "rs_lift", 0.0f); setP (rp, "rs_gate", -80.0f);
            const bool gateOk = std::abs (open3100) < 0.5f && shut3100 < -40.0f && std::abs (kept110) < 0.5f && std::abs (kept220) < 0.5f
                                && lifted3100 < -40.0f && P::destForParam ("rs_gate") == P::DstRsGate;
            if (! gateOk) ++failures;
            std::cout << (gateOk ? "ok   " : "FAIL ") << "Gate: at -26 dB the 3100 Hz partial (-20 dB) changes " << juce::String (open3100, 2)
                      << " dB; at -12 dB it drops " << juce::String (shut3100, 1) << " dB (110 Hz " << juce::String (kept110, 2) << ", 220 Hz "
                      << juce::String (kept220, 2) << " dB), with Lift 100% after the gate " << juce::String (lifted3100, 1) << " dB\n";
        }

        // Audio source survives the plugin state round trip
        juce::MemoryBlock state;
        rp.getStateInformation (state);
        ElukkaAudioProcessor rp2;
        rp2.setStateInformation (state.getData(), (int) state.getSize());
        auto back = rp2.getResynth();
        const bool stateOk = back && back->numFrames == data->numFrames && back->amp.size() == data->amp.size();
        if (! stateOk) ++failures;
        std::cout << (stateOk ? "ok   " : "FAIL ") << "Audio resynth state round-trip (" << state.getSize() / 1024 << " KB state)\n";

        // Image round trip
        juce::Image img (juce::Image::RGB, 64, 64, true);
        juce::Graphics g (img);
        g.setColour (juce::Colours::white);
        g.drawLine (0, 63, 63, 0, 3.0f);
        auto imgData = ResynthData::fromImage (img, "test");
        auto imgBack = ResynthData::fromTree (imgData->toTree());
        const bool imgOk = imgBack && imgBack->amp == imgData->amp;
        if (! imgOk) ++failures;
        std::cout << (imgOk ? "ok   " : "FAIL ") << "Image resynth state round-trip\n";

        // CPU: dense loop (tones + noise hits), 8 unison voices, 4-note chord
        const int loopLen = (int) (sr * 4.0);
        juce::AudioBuffer<float> loop (2, loopLen);
        juce::Random rnd (5);
        for (int i = 0; i < loopLen; ++i)
        {
            float v = 0.2f * std::sin (0.0362f * i) + 0.1f * std::sin (0.0911f * i) + 0.05f * std::sin (0.231f * i);
            const int beat = (int) (sr * 0.25);
            v += 0.5f * std::exp (-(float) (i % beat) / 900.0f) * (rnd.nextFloat() * 2.0f - 1.0f);
            loop.setSample (0, i, v);
            loop.setSample (1, i, v);
        }
        auto dense = ResynthData::fromAudio (loop, sr, "dense");
        int maxActive = 0;
        for (int f = 0; f < dense->numFrames; ++f)
        {
            int n = 0;
            for (int s = 0; s < dense->numSlots; ++s) if (dense->amp[(size_t) f * dense->numSlots + s] > 0.0f) ++n;
            maxActive = std::max (maxActive, n);
        }
        rp.setResynth (dense);
        setP (rp, "rs_lift", 0.0f); setP (rp, "rs_loop", 1.0f); setP (rp, "o1_uniVoices", 8.0f); setP (rp, "o1_uniDetune", 0.3f);
        double cpu1 = 0.0, cpu4 = 0.0, cpu4p = 0.0;
        renderNotes (rp, { 60 }, 3.0, &cpu1);
        auto chord = renderNotes (rp, { 48, 55, 60, 64 }, 3.0, &cpu4);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < chord.getNumSamples(); ++i) finite &= std::isfinite (chord.getSample (ch, i));
        // the same without the noise residual: the difference is what the residual costs
        auto partialsOnly = std::make_shared<ResynthData> (*dense);
        partialsOnly->noiseL.clear(); partialsOnly->noiseR.clear(); partialsOnly->noiseCoh.clear();
        rp.setResynth (partialsOnly);
        renderNotes (rp, { 48, 55, 60, 64 }, 3.0, &cpu4p);
        if (! finite) ++failures;
        std::cout << (finite ? "ok   " : "FAIL ") << "Dense loop: up to " << maxActive << " partials per frame, unison 8: 1 note "
                  << juce::String (cpu1 * 100.0, 1) << "% CPU, 4 notes " << juce::String (cpu4 * 100.0, 1) << "% CPU (one core; partials alone "
                  << juce::String (cpu4p * 100.0, 1) << "%, residual " << juce::String ((cpu4 - cpu4p) * 100.0, 1) << "%)\n";
        rp.setPlayHead (nullptr);
    }

    // ---- Filter, chorus, EQ, S&H and FX order ----
    {
        ElukkaAudioProcessor fp;
        fp.setPlayHead (&ph);
        fp.setPlayConfigDetails (0, 2, sr, block);
        setP (fp, "master", 0.0f); setP (fp, "ampA", 0.001f); setP (fp, "ampS", 1.0f); setP (fp, "o1_wave", (float) P::WSaw);
        const float c3 = 261.6256f;
        auto db = [&] (const juce::AudioBuffer<float>& b, float hz) { return juce::Decibels::gainToDecibels (measure (b, 0, hz).first, -140.0f); };
        auto open = renderNotes (fp, { 60 }, 2.0);
        // attenuation of the 8th harmonic (2093 Hz) relative to the open filter, cutoff 500 Hz
        setP (fp, "flt_cutoff", 500.0f);
        float att[3];
        for (int slope = 0; slope < 3; ++slope)
        {
            setP (fp, "flt_slope", (float) slope);
            auto out = renderNotes (fp, { 60 }, 2.0);
            att[slope] = db (open, 8 * c3) - db (out, 8 * c3);
        }
        const bool slopesOk = att[0] > att[1] + 6.0f && att[1] > att[2] + 3.0f && att[2] > 6.0f;
        if (! slopesOk) ++failures;
        std::cout << (slopesOk ? "ok   " : "FAIL ") << "Low pass at 500 Hz, 8th harmonic cut by: 24 dB slope " << juce::String (att[0], 1)
                  << " dB, 12 dB " << juce::String (att[1], 1) << " dB, 6 dB " << juce::String (att[2], 1) << " dB\n";

        // Filter routing: every oscillator goes through the filter by default; with Osc 2's "To Filter" off, Osc 2
        // (a saw a tritone up) keeps its 8th harmonic while Osc 1's is still cut. Old saves without the switch keep it on.
        {
            setP (fp, "flt_slope", 0.0f);
            // a tritone up: its 8th harmonic (2960 Hz) lies clear of Osc 1's 11th and 12th
            setP (fp, "o2_on", 1.0f); setP (fp, "o2_level", 0.8f); setP (fp, "o2_wave", (float) P::WSaw); setP (fp, "o2_coarse", 6.0f);
            const float c5th = c3 * std::exp2 (6.0f / 12.0f);
            const bool defaultsOn = fp.apvts.getRawParameterValue ("o1_toFilter")->load() > 0.5f && fp.apvts.getRawParameterValue ("o2_toFilter")->load() > 0.5f
                                    && fp.apvts.getRawParameterValue ("o3_toFilter")->load() > 0.5f;
            auto both = renderNotes (fp, { 60 }, 2.0);
            setP (fp, "o2_toFilter", 0.0f);
            auto routed = renderNotes (fp, { 60 }, 2.0);
            setP (fp, "flt_cutoff", 20000.0f);
            auto openBoth = renderNotes (fp, { 60 }, 2.0);
            const float o2Kept = db (openBoth, 8 * c5th) - db (routed, 8 * c5th);    // ~0 dB: osc 2 bypasses
            const float o2CutBefore = db (openBoth, 8 * c5th) - db (both, 8 * c5th);  // large: both filtered
            const float o1Cut = db (openBoth, 8 * c3) - db (routed, 8 * c3);         // still large
            // an old save (no toFilter entries) loads with every oscillator routed to the filter
            juce::MemoryBlock state;
            fp.getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            bool oldOk = false;
            if (xml != nullptr)
            {
                auto tree = juce::ValueTree::fromXml (*xml);
                std::function<void (juce::ValueTree)> strip = [&] (juce::ValueTree t)
                {
                    for (int i = t.getNumChildren() - 1; i >= 0; --i)
                    {
                        auto ch = t.getChild (i);
                        if (ch.getProperty ("id").toString().endsWith ("_toFilter")) t.removeChild (i, nullptr);
                        else strip (ch);
                    }
                };
                strip (tree);
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
                ElukkaAudioProcessor fresh;
                setP (fresh, "o2_toFilter", 0.0f);
                fresh.setStateInformation (old.getData(), (int) old.getSize());
                oldOk = fresh.apvts.getRawParameterValue ("o2_toFilter")->load() > 0.5f;
            }
            const bool routeOk = defaultsOn && std::abs (o2Kept) < 0.5f && o2CutBefore > 20.0f && o1Cut > 20.0f && oldOk;
            if (! routeOk) ++failures;
            std::cout << (routeOk ? "ok   " : "FAIL ") << "Filter routing: all oscillators to the filter by default (" << (defaultsOn ? "yes" : "no")
                      << "); Osc 2 off -> its 8th harmonic changes by " << juce::String (o2Kept, 2) << " dB (was cut " << juce::String (o2CutBefore, 1)
                      << " dB), Osc 1 still cut " << juce::String (o1Cut, 1) << " dB; old save loads with To Filter on (" << (oldOk ? "yes" : "no") << ")\n";
            setP (fp, "o2_on", 0.0f); setP (fp, "o2_level", 0.0f); setP (fp, "o2_coarse", 0.0f); setP (fp, "o2_toFilter", 1.0f); setP (fp, "flt_cutoff", 500.0f);
        }

        setP (fp, "flt_slope", 0.0f); setP (fp, "flt_type", 1.0f); setP (fp, "flt_cutoff", 1500.0f);
        auto hp = renderNotes (fp, { 60 }, 2.0);
        const float hpCut = db (open, c3) - db (hp, c3);
        setP (fp, "flt_type", 2.0f); setP (fp, "flt_cutoff", 8 * c3);
        auto bp = renderNotes (fp, { 60 }, 2.0);
        const float bpLow = db (open, c3) - db (bp, c3), bpCentre = db (open, 8 * c3) - db (bp, 8 * c3);
        const bool typesOk = hpCut > 30.0f && bpLow > 20.0f && std::abs (bpCentre) < 6.0f;
        if (! typesOk) ++failures;
        std::cout << (typesOk ? "ok   " : "FAIL ") << "High pass at 1.5 kHz cuts the fundamental by " << juce::String (hpCut, 1)
                  << " dB; band pass at the 8th harmonic cuts the fundamental by " << juce::String (bpLow, 1)
                  << " dB and changes the 8th by " << juce::String (bpCentre, 1) << " dB\n";

        // Envelope opens the filter: compare brightness of the first 50 ms with 1 s later
        setP (fp, "flt_type", 0.0f); setP (fp, "flt_cutoff", 200.0f); setP (fp, "flt_env", 0.6f);
        setP (fp, "flt_A", 0.001f); setP (fp, "flt_D", 0.3f); setP (fp, "flt_S", 0.0f);
        auto envOut = renderNotes (fp, { 60 }, 1.5);
        auto rmsAt = [&] (const juce::AudioBuffer<float>& b, double t) { return b.getRMSLevel (0, (int) (t * sr), (int) (0.05 * sr)); };
        const bool envOk = rmsAt (envOut, 0.01) > rmsAt (envOut, 1.2) * 2.0f;
        if (! envOk) ++failures;
        std::cout << (envOk ? "ok   " : "FAIL ") << "Filter envelope: level at note start " << juce::String (juce::Decibels::gainToDecibels (rmsAt (envOut, 0.01)), 1)
                  << " dB, after decay " << juce::String (juce::Decibels::gainToDecibels (rmsAt (envOut, 1.2)), 1) << " dB\n";

        // Full resonance + drive stays stable
        setP (fp, "flt_env", 0.0f); setP (fp, "flt_cutoff", 800.0f); setP (fp, "flt_res", 1.0f); setP (fp, "flt_drive", 1.0f);
        auto res = renderNotes (fp, { 36, 60, 84 }, 2.0);
        const float resPeak = res.getMagnitude (0, res.getNumSamples());
        const bool resOk = std::isfinite (resPeak) && resPeak < 8.0f;
        if (! resOk) ++failures;
        std::cout << (resOk ? "ok   " : "FAIL ") << "Max resonance + drive, 3 notes: peak " << juce::String (resPeak, 2) << " (stable)\n";
        setP (fp, "flt_cutoff", 20000.0f); setP (fp, "flt_res", 0.0f); setP (fp, "flt_drive", 0.0f);

        // Comb wet low pass: with the comb fully wet, a 500 Hz low pass removes the 8th harmonic
        setP (fp, "comb_on", 1.0f); setP (fp, "comb_mix", 1.0f);
        auto combOpen = renderNotes (fp, { 60 }, 2.0);
        setP (fp, "comb_lp", 500.0f);
        auto combLp = renderNotes (fp, { 60 }, 2.0);
        const float combCut = db (combOpen, 8 * c3) - db (combLp, 8 * c3);
        setP (fp, "comb_on", 0.0f); setP (fp, "comb_lp", 20000.0f);
        const bool combOk = combCut > 20.0f;
        if (! combOk) ++failures;
        std::cout << (combOk ? "ok   " : "FAIL ") << "Comb wet low pass at 500 Hz cuts the 8th harmonic by " << juce::String (combCut, 1) << " dB\n";

        // Unsynced delay: a short pluck echoes 250 ms later
        setP (fp, "ampS", 0.0f); setP (fp, "ampD", 0.03f); setP (fp, "ampR", 0.01f);
        setP (fp, "dly_on", 1.0f); setP (fp, "dly_sync", 0.0f); setP (fp, "dly_time", 250.0f); setP (fp, "dly_fb", 0.0f); setP (fp, "dly_mix", 1.0f);
        auto echo = renderNotes (fp, { 60 }, 1.0);
        auto win = [&] (double t) { return juce::Decibels::gainToDecibels (echo.getRMSLevel (0, (int) (t * sr), (int) (0.02 * sr)), -120.0f); };
        const bool delayOk = win (0.255) > win (0.15) + 20.0f && win (0.255) > -40.0f;
        if (! delayOk) ++failures;
        std::cout << (delayOk ? "ok   " : "FAIL ") << "Unsynced delay 250 ms: level at 150 ms " << juce::String (win (0.15), 1)
                  << " dB, at 255 ms " << juce::String (win (0.255), 1) << " dB\n";

        // Delay low pass on the wet signal: echo of the 8th harmonic is cut
        setP (fp, "dly_lp", 500.0f);
        auto darkEcho = renderNotes (fp, { 60 }, 1.0);
        auto hfAt = [&] (const juce::AudioBuffer<float>& b2)
        {
            juce::AudioBuffer<float> seg (1, (int) (0.1 * sr));
            seg.copyFrom (0, 0, b2, 0, (int) (0.25 * sr), seg.getNumSamples());
            juce::dsp::IIR::Filter<float> hp;
            hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 1500.0);
            for (int i = 0; i < seg.getNumSamples(); ++i) seg.setSample (0, i, hp.processSample (seg.getSample (0, i)));
            return juce::Decibels::gainToDecibels (seg.getRMSLevel (0, 0, seg.getNumSamples()), -120.0f);
        };
        const float dlyCut = hfAt (echo) - hfAt (darkEcho);
        const bool dlyLpOk = dlyCut > 12.0f;   // 12 dB/oct: ~19 dB down at 1.5 kHz, more above
        if (! dlyLpOk) ++failures;
        std::cout << (dlyLpOk ? "ok   " : "FAIL ") << "Delay low pass 500 Hz: high frequencies in the echo cut by " << juce::String (dlyCut, 1) << " dB\n";
        // ...and only there: with feedback, the second repeat is no darker than the first (the loop is unfiltered)
        {
            setP (fp, "dly_fb", 0.6f); setP (fp, "dly_pp", 0.0f);   // not ping-pong: every repeat on the measured (left) side
            auto repeats = renderNotes (fp, { 60 }, 1.0);
            auto tilt = [&] (double t)   // level above 1.5 kHz relative to the whole echo, dB
            {
                juce::AudioBuffer<float> seg (1, (int) (0.1 * sr));
                seg.copyFrom (0, 0, repeats, 0, (int) (t * sr), seg.getNumSamples());
                const float all = juce::Decibels::gainToDecibels (seg.getRMSLevel (0, 0, seg.getNumSamples()), -120.0f);
                juce::dsp::IIR::Filter<float> hp;
                hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 1500.0);
                for (int i = 0; i < seg.getNumSamples(); ++i) seg.setSample (0, i, hp.processSample (seg.getSample (0, i)));
                return juce::Decibels::gainToDecibels (seg.getRMSLevel (0, 0, seg.getNumSamples()), -120.0f) - all;
            };
            const float darker = tilt (0.25) - tilt (0.5);
            const bool loopOk = std::abs (darker) < 1.5f;
            if (! loopOk) ++failures;
            std::cout << (loopOk ? "ok   " : "FAIL ") << "Delay filters only the wet signal: 2nd repeat's highs vs the 1st " << juce::String (-darker, 2) << " dB\n";
            setP (fp, "dly_fb", 0.0f); setP (fp, "dly_pp", 1.0f);
        }
        setP (fp, "dly_on", 0.0f); setP (fp, "dly_lp", 20000.0f); setP (fp, "dly_sync", 1.0f);

        setP (fp, "ampS", 1.0f); setP (fp, "ampD", 0.3f); setP (fp, "ampR", 0.2f);

        // Reverb (FDN): decay accuracy, stereo from mono, stability at the extremes, no clicks on changes
        {
            using ReverbEval::run;
            VintageReverb::Params rp;   // defaults: Smooth, 3 s
            const float t60 = ReverbEval::measureT60 (run (ReverbEval::fdn<4> (rp), ReverbEval::impulse (6.0)));
            const auto st = ReverbEval::stereoStats (run (ReverbEval::fdn<4> (rp), ReverbEval::impulse (4.0, false)));   // mono (left only) in

            // worst case: longest decay, full modulation and size, Lush; noise in, then silence while modes and size change
            VintageReverb rv;
            rv.prepare (sr);
            VintageReverb::Params wp;
            wp.decay = 15.0f; wp.mod = 1.0f; wp.size = 1.0f; wp.mode = 2; wp.mix = 1.0f;
            juce::Random r (4);
            float peak = 0.0f, lateRms = 0.0f, earlyRms = 0.0f;
            bool finite = true;
            std::vector<float> l (256), rr (256);
            for (int b = 0; b < (int) (40.0 * sr / 256); ++b)
            {
                const double t = b * 256 / sr;
                for (int k = 0; k < 256; ++k) { const float x = t < 2.0 ? 0.2f * (r.nextFloat() * 2.0f - 1.0f) : 0.0f; l[(size_t) k] = x; rr[(size_t) k] = x; }
                wp.mode = (int) (t / 3.0) % 3;
                wp.size = 0.5f + 0.5f * (float) std::sin (t * 0.7);
                rv.process (l.data(), rr.data(), 256, wp);
                for (int k = 0; k < 256; ++k)
                {
                    finite &= std::isfinite (l[(size_t) k]) && std::isfinite (rr[(size_t) k]);
                    peak = std::max (peak, std::abs (l[(size_t) k]));
                }
                if (t > 3.0 && t < 4.0) earlyRms = std::max (earlyRms, juce::FloatVectorOperations::findMaximum (l.data(), 256));
                if (t > 39.0) lateRms = std::max (lateRms, juce::FloatVectorOperations::findMaximum (l.data(), 256));
            }
            const bool stable = finite && peak < 20.0f && lateRms < earlyRms;

            // clicks: a steady tone through the reverb while every control and the mode move
            VintageReverb cv;
            cv.prepare (sr);
            VintageReverb::Params cp;
            cp.mix = 0.5f;
            juce::AudioBuffer<float> out (2, (int) (6.0 * sr));
            double ph = 0.0;
            for (int b = 0; b < out.getNumSamples() / 128; ++b)
            {
                const double t = b * 128 / sr;
                for (int k = 0; k < 128; ++k)
                {
                    ph += 220.0 / sr;
                    const float x = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                    out.setSample (0, b * 128 + k, x); out.setSample (1, b * 128 + k, x);
                }
                cp.mode = (int) (t / 0.5) % 3;
                cp.decay = 1.0f + 6.0f * (float) (0.5 + 0.5 * std::sin (t * 1.3));
                cp.tone = (float) (0.5 + 0.5 * std::sin (t * 2.1));
                cp.mod = (float) (0.5 + 0.5 * std::sin (t * 1.7));
                cp.size = (float) (0.5 + 0.5 * std::sin (t * 0.9));
                cp.mix = (float) (0.3 + 0.3 * std::sin (t * 2.7));
                cv.process (out.getWritePointer (0, b * 128), out.getWritePointer (1, b * 128), 128, cp);
            }
            const auto clicks = detectClicks (out, 0.02f, (int) (0.1 * sr));

            const bool revOk = std::abs (t60 - 3.0f) < 0.45f && st.first < 0.5f && st.second > 0.8f && stable && clicks.clicks == 0;
            if (! revOk) ++failures;
            std::cout << (revOk ? "ok   " : "FAIL ") << "Reverb: decay 3 s measures " << juce::String (t60, 2) << " s; mono in -> L/R correlation "
                      << juce::String (st.first, 2) << ", mono sum keeps " << juce::String (st.second, 2) << " of the energy; 15 s decay + full mod + size/mode changes "
                      << (stable ? "stable" : "UNSTABLE") << " (peak " << juce::String (peak, 2) << "); " << clicks.clicks << " clicks while all controls move\n";
        }

        // Older saves (previous reverb): Damping -> Tone, room size -> Size + Decay, removed controls dropped
        {
            auto src = std::make_unique<ElukkaAudioProcessor>();
            auto tree = src->saveState();
            for (auto id : { "rev_decay", "rev_tone" }) tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
            for (auto [id, v] : { std::pair<const char*, float> { "rev_damp", 0.3f }, { "rev_size", 0.85f }, { "rev_width", 1.0f }, { "rev_hp", 200.0f } })
            {
                auto c = tree.getChildWithProperty ("id", id);
                if (! c.isValid()) { c = juce::ValueTree ("PARAM"); c.setProperty ("id", id, nullptr); tree.appendChild (c, nullptr); }
                c.setProperty ("value", v, nullptr);
            }
            auto dst = std::make_unique<ElukkaAudioProcessor>();
            dst->loadState (tree);
            auto v = [&] (const char* id) { return dst->apvts.getRawParameterValue (id)->load(); };
            const bool migOk = std::abs (v ("rev_tone") - 70.0f) < 0.5f && std::abs (v ("rev_size") - 0.85f) < 0.01f
                               && std::abs (v ("rev_decay") - P::reverbDecayForOldSize (0.85f)) < 0.05f;
            if (! migOk) ++failures;
            std::cout << (migOk ? "ok   " : "FAIL ") << "Old reverb settings: damping 0.3 -> tone " << juce::String (v ("rev_tone"), 1) << "%, size 0.85 -> decay "
                      << juce::String (v ("rev_decay"), 2) << " s\n";
        }

        // EQ bell: +12 dB at 1 kHz on a 2nd-osc sine at 1 kHz (B5 = 987.8 Hz)
        setP (fp, "o1_wave", (float) P::WSine);
        auto flat = renderNotes (fp, { 83 }, 2.0);
        setP (fp, "eq_on", 1.0f); setP (fp, "eq_midF", 987.8f); setP (fp, "eq_mid", 12.0f); setP (fp, "eq_midQ", 1.0f);
        auto boosted = renderNotes (fp, { 83 }, 2.0);
        const float gain = db (boosted, 987.8f) - db (flat, 987.8f);
        const bool eqOk = std::abs (gain - 12.0f) < 0.5f;
        if (! eqOk) ++failures;
        std::cout << (eqOk ? "ok   " : "FAIL ") << "EQ bell +12 dB measured as " << juce::String (gain, 2) << " dB\n";
        setP (fp, "eq_on", 0.0f); setP (fp, "o1_wave", (float) P::WSaw);

        // S&H at 1/16 and 120 BPM: 16 new values per bar (2 s)
        ph.bpm = 120.0; ph.ppq = 0.0;
        fp.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        int changes = 0;
        float lastSh = fp.uiSh.load();
        const int blocks = (int) (sr * 2.0) / block;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            fp.processBlock (buf, midi);
            ph.ppq += block / sr * ph.bpm / 60.0;
            if (fp.uiSh.load() != lastSh) { ++changes; lastSh = fp.uiSh.load(); }
        }
        const bool shOk = changes >= 15 && changes <= 17;
        if (! shOk) ++failures;
        std::cout << (shOk ? "ok   " : "FAIL ") << "S&H 1/16 at 120 BPM: " << changes << " new values in one bar\n";
        ph.bpm = 128.0;

        // Unipolar LFO: a sine-shaped Mod unit stays within 0..100%
        {
            ModUnit lfoUnit;
            ModShape sineShape;
            ModUnitParams mp;
            mp.rateHz = 1.0;
            juce::Random r2;
            lfoUnit.trigger (mp, r2, 0.0);
            float lo = 1.0f, hi = 0.0f;
            for (int i = 0; i < 400; ++i) { const float v = lfoUnit.process (mp, sineShape, 128, sr, r2); lo = std::min (lo, v); hi = std::max (hi, v); }
            const bool uniOk = lo >= 0.0f && lo < 0.02f && hi <= 1.0f && hi > 0.98f;
            if (! uniOk) ++failures;
            std::cout << (uniOk ? "ok   " : "FAIL ") << "LFO output range " << juce::String (lo, 3) << " .. " << juce::String (hi, 3) << " (unipolar)\n";
        }

        // Pitch modulation: Macro 1 -> Osc 1 Pitch at 25% = +12 semitones; Coarse reaches +-48
        {
            setP (fp, "mx1_src", (float) P::SrcMacro1); setP (fp, "mx1_dst", (float) P::DstO1Pitch); setP (fp, "mx1_amt", 0.25f);
            setP (fp, "macro1", 1.0f);
            auto up = renderNotes (fp, { 60 }, 2.0);
            const float f = measure (up, 0, 2 * c3).second;
            setP (fp, "mx1_src", 0.0f); setP (fp, "mx1_dst", 0.0f); setP (fp, "mx1_amt", 0.0f); setP (fp, "macro1", 0.0f);
            juce::Slider amountSlider;
            amountSlider.setRange (-1.0, 1.0);
            setP (fp, "mx2_dst", (float) P::DstO1Pitch);
            setupAmountText (amountSlider, fp.apvts, 1);
            const auto pitchText = amountSlider.getTextFromValue (0.25);
            setP (fp, "mx2_dst", (float) P::DstCutoff);
            const auto otherText = amountSlider.getTextFromValue (0.25);
            setP (fp, "mx2_dst", 0.0f);
            auto* coarse = fp.apvts.getParameter ("o1_coarse");
            const float coarseMax = coarse->convertFrom0to1 (1.0f), coarseMin = coarse->convertFrom0to1 (0.0f);
            const bool pitchOk = std::abs (f - 2 * c3) < 1.0f && pitchText == "+12.0 st" && otherText == "+25%" && coarseMax == 48.0f && coarseMin == -48.0f;
            if (! pitchOk) ++failures;
            std::cout << (pitchOk ? "ok   " : "FAIL ") << "Pitch mod 25% -> fundamental " << juce::String (f, 1) << " Hz (an octave up); amount shown as '"
                      << pitchText << "' for pitch, '" << otherText << "' otherwise; Coarse range " << coarseMin << " .. " << coarseMax << "\n";
        }

        // Init pattern is the default
        {
            ElukkaAudioProcessor fresh;
            const bool initOk = fresh.pattern.name == "Init" && fresh.presets.patterns.front().name == "Init";
            if (! initOk) ++failures;
            std::cout << (initOk ? "ok   " : "FAIL ") << "Default sequencer pattern: " << fresh.pattern.name << " (first in the list: "
                      << fresh.presets.patterns.front().name << ")\n";
        }

        // FX order survives the plugin state
        const int custom[] { P::FxReverb, P::FxEq, P::FxFilter, P::FxOtt, P::FxDist, P::FxComb, P::FxDelay };
        for (int i = 0; i < P::NumFx; ++i) fp.fxOrder[(size_t) i] = custom[i];
        juce::MemoryBlock st;
        fp.getStateInformation (st);
        ElukkaAudioProcessor fp2;
        fp2.setStateInformation (st.getData(), (int) st.getSize());
        bool orderOk = true;
        for (int i = 0; i < P::NumFx; ++i) orderOk &= fp2.fxOrder[(size_t) i].load() == custom[i];
        if (! orderOk) ++failures;
        std::cout << (orderOk ? "ok   " : "FAIL ") << "Custom FX order restored from saved state\n";
        fp.setPlayHead (nullptr);
    }

    // ---- Click hunting: sine patches (smooth by nature) through situations that can click ----
    {
        struct Scenario { const char* name; std::function<void (ElukkaAudioProcessor&)> setup; std::vector<std::pair<int, juce::MidiMessage>> midi; double seconds; };
        auto sineBase = [&] (ElukkaAudioProcessor& q)
        {
            setP (q, "o1_wave", (float) P::WSine); setP (q, "master", 0.0f); setP (q, "ampA", 0.005f); setP (q, "ampS", 1.0f);
            setP (q, "ampR", 0.05f);
            q.shapes[0].setPoints (ModShape::preset (0));   // sine LFO shape (the default Mod 1 shape is a decay, i.e. a saw as an LFO)
        };
        auto on = [] (int note, int vel = 100) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) vel); };
        auto off = [] (int note) { return juce::MidiMessage::noteOff (1, note); };
        const int S = (int) sr;
        std::vector<Scenario> scenarios
        {
            { "Single note on/off", sineBase, { { 0, on (48) }, { S / 2, off (48) } }, 1.0 },
            { "Mono retrigger (overlapping notes)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "polyMode", 1.0f); },
              { { 0, on (48) }, { S / 4, on (55) }, { S / 2, on (52) }, { S * 3 / 4, off (52) } }, 1.0 },
            { "Voice stealing (10 notes, poly)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "ampR", 2.0f); },
              { { 0, on (40) }, { 2000, on (42) }, { 4000, on (44) }, { 6000, on (46) }, { 8000, on (48) }, { 10000, on (50) },
                { 12000, on (52) }, { 14000, on (54) }, { 20000, on (56) }, { 26000, on (58) } }, 1.0 },
            { "Sequencer 1/16 retriggers", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "sq_on", 1.0f); setP (q, "sq_gate", 1.0f); },
              { { 0, on (48) } }, 2.0 },
            { "Mono retrigger, filter engaged (19 kHz, res 0.5)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "polyMode", 1.0f);
                setP (q, "flt_cutoff", 19000.0f); setP (q, "flt_res", 0.5f); },
              { { 0, on (48) }, { S / 4, on (55) }, { S / 2, on (52) }, { S * 3 / 4, off (52) } }, 1.0 },
            { "Voice stealing, filter engaged (19 kHz, res 0.5)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "ampR", 2.0f);
                setP (q, "flt_cutoff", 19000.0f); setP (q, "flt_res", 0.5f); },
              { { 0, on (40) }, { 2000, on (42) }, { 4000, on (44) }, { 6000, on (46) }, { 8000, on (48) }, { 10000, on (50) },
                { 12000, on (52) }, { 14000, on (54) }, { 20000, on (56) }, { 26000, on (58) } }, 1.0 },
            { "LFO on osc level (fast)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 8.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Level); setP (q, "mx1_amt", -0.8f); },
              { { 0, on (48) } }, 1.5 },
            { "Square LFO on osc level (Smooth 3 ms)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 4.0f);
                q.shapes[0].setPoints (ModShape::preset (4));
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Level); setP (q, "mx1_amt", -0.9f); },
              { { 0, on (48) } }, 1.5 },
            { "S&H on osc level", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "mx1_src", (float) P::SrcSH); setP (q, "mx1_dst", (float) P::DstO1Level); setP (q, "mx1_amt", -0.8f); },
              { { 0, on (48) } }, 1.5 },
            { "S&H on master volume", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "mx1_src", (float) P::SrcSH); setP (q, "mx1_dst", (float) P::DstMaster); setP (q, "mx1_amt", -0.5f); },
              { { 0, on (48) } }, 1.5 },
            { "LFO on filter cutoff (crossing fully open)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "flt_res", 0.5f);
                setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 3.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstFltCutoff); setP (q, "mx1_amt", -0.9f); },
              { { 0, on (36) } }, 1.5 },
            { "LFO on comb frequency", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "comb_on", 1.0f);
                setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 2.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstCombFreq); setP (q, "mx1_amt", 0.4f); },
              { { 0, on (48) } }, 1.5 },
            { "Unison count modulated", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 3.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Voices); setP (q, "mx1_amt", 0.5f); },
              { { 0, on (48) } }, 1.5 },
            { "Bass Mono + 7-voice unison, pitch LFO across Bass Freq", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_uniVoices", 7.0f); setP (q, "o1_uniDetune", 40.0f); setP (q, "o1_bassMono", 1.0f);
                setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 3.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Pitch); setP (q, "mx1_amt", 0.15f); },
              { { 0, on (45) } }, 1.5 },
            { "Bass Mono resynth + 7-voice unison, pitch LFO across Bass Freq", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_wave", (float) P::WResynth); setP (q, "rs_sync", 0.0f);
                setP (q, "o1_uniVoices", 7.0f); setP (q, "o1_uniDetune", 40.0f); setP (q, "o1_bassMono", 1.0f);
                setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 3.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Pitch); setP (q, "mx1_amt", 0.15f); },
              { { 0, on (45) } }, 1.5 },
            { "Bass Mono resynth + unison, Bass Freq swept by LFO", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_wave", (float) P::WResynth); setP (q, "rs_sync", 0.0f);
                setP (q, "o1_uniVoices", 7.0f); setP (q, "o1_uniDetune", 40.0f); setP (q, "o1_bassMono", 1.0f);
                setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 4.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstRsBassFreq); setP (q, "mx1_amt", 0.6f); },
              { { 0, on (60) } }, 1.5 },
            { "Prism amount stepped by S&H (triangle, unison 5)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "o1_wave", (float) P::WTriangle);
                setP (q, "o1_uniVoices", 5.0f); setP (q, "sh_smooth", 3.0f); setP (q, "hm_prismCenter", 4.0f);
                setP (q, "mx1_src", (float) P::SrcSH); setP (q, "mx1_dst", (float) P::DstPrism); setP (q, "mx1_amt", 1.0f); },
              { { 0, on (48) } }, 1.5 },
            { "Shimmer 100% fast (triangle)", [&] (ElukkaAudioProcessor& q) { sineBase (q); setP (q, "o1_wave", (float) P::WTriangle);
                setP (q, "hm_shimmer", 100.0f); setP (q, "hm_shimmerRate", 10.0f); },
              { { 0, on (48) } }, 1.5 },
            { "Prism + Shimmer on resynth", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_wave", (float) P::WResynth); setP (q, "rs_sync", 0.0f); setP (q, "hm_prism", 100.0f); setP (q, "hm_prismCenter", 2.0f);
                setP (q, "hm_shimmer", 60.0f); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 3.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstPrismCenter); setP (q, "mx1_amt", 0.4f); },
              { { 0, on (60) } }, 1.5 },
            { "Pan LFO with unison", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_uniVoices", 5.0f); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 6.0f);
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstO1Pan); setP (q, "mx1_amt", 1.0f); },
              { { 0, on (48) } }, 1.5 },
            { "FM into resynth osc", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_wave", (float) P::WResynth); setP (q, "o2_on", 1.0f); setP (q, "o2_wave", (float) P::WSine); setP (q, "o2_level", 0.0f);
                setP (q, "o1_fmSrc", 2.0f); setP (q, "o1_fmAmt", 0.3f); setP (q, "rs_sync", 0.0f); },
              { { 0, on (60) } }, 1.5 },
        };

        // a tonal resynth source for the FM scenario
        juce::AudioBuffer<float> tone (2, (int) (sr * 3));
        for (int i = 0; i < tone.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) sr;
            const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 130.8f * t) + 0.15f * std::sin (juce::MathConstants<float>::twoPi * 261.6f * t * (1.0f + 0.01f * std::sin (t * 3.0f)));
            tone.setSample (0, i, v); tone.setSample (1, i, v);
        }
        auto toneData = ResynthData::fromAudio (tone, sr, "tone");

        // a partial that jumps 1000 <-> 1020 Hz every 40 ms (tracked as one partial) for the FM test
        juce::AudioBuffer<float> jumpy (2, (int) (sr * 3));
        {
            double ph2 = 0.0;
            for (int i = 0; i < jumpy.getNumSamples(); ++i)
            {
                const double f = ((i / (int) (0.04 * sr)) % 2 == 0) ? 1000.0 : 1020.0;
                ph2 += f / sr;
                const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                jumpy.setSample (0, i, v); jumpy.setSample (1, i, v);
            }
        }
        auto jumpyData = ResynthData::fromAudio (jumpy, sr, "jumpy");

        int clickyScenarios = 0;
        scenarios.push_back ({ "FM into resynth (moving partial)", [&] (ElukkaAudioProcessor& q) { sineBase (q);
                setP (q, "o1_wave", (float) P::WResynth); setP (q, "o2_on", 1.0f); setP (q, "o2_wave", (float) P::WSine); setP (q, "o2_level", 0.0f);
                setP (q, "o2_coarse", -24.0f); setP (q, "o1_fmSrc", 2.0f); setP (q, "o1_fmAmt", 0.3f); setP (q, "rs_sync", 0.0f); },
              { { 0, on (60) } }, 1.5 });
        for (auto& sc : scenarios)
        {
            ElukkaAudioProcessor q;
            q.setPlayHead (&ph);
            q.setPlayConfigDetails (0, 2, sr, block);
            q.setResynth (juce::String (sc.name).contains ("moving") ? jumpyData : toneData);
            sc.setup (q);
            q.prepareToPlay (sr, block);
            ph.ppq = 0.0;
            const int total = ((int) (sr * sc.seconds) / block) * block;
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            out.clear();
            for (int b = 0; b < total / block; ++b)
            {
                juce::MidiBuffer midi;
                for (auto& [t, m] : sc.midi)
                    if (t >= b * block && t < (b + 1) * block) midi.addEvent (m, t - b * block);
                q.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
                ph.ppq += block / sr * ph.bpm / 60.0;
            }
            auto rep = detectClicks (out);
            if (rep.clicks > 0) ++clickyScenarios;
            std::cout << (rep.clicks == 0 ? "ok   " : "CLICK") << " " << juce::String (sc.name).paddedRight (' ', 44)
                      << " worst jump " << juce::String (rep.worst, 4) << (rep.clicks > 0 ? "  (" + juce::String (rep.clicks) + " events, first at " + juce::String (rep.firstAt) + ")" : juce::String()) << "\n";
            q.setPlayHead (nullptr);
        }
        if (clickyScenarios > 0) failures += clickyScenarios;
    }

    // ---- Resynth Position jumps: each jump crossfades (15 ms) instead of switching the spectrum in one block. The
    // source has nothing above 500 Hz, so any energy above 2 kHz in the render is the jump's click. ----
    {
        // two sections (110 Hz tone, then a quieter 165 Hz one) crossfaded over 0.2 s, with 20 ms fades at the ends,
        // so the source itself has no click (a jump to its start would otherwise replay the start's own click)
        juce::AudioBuffer<float> sections (2, (int) (sr * 3));
        for (int i = 0; i < sections.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) sr;
            const float x = juce::jlimit (0.0f, 1.0f, (t - 1.4f) / 0.2f);
            const float edge = juce::jmin (1.0f, t / 0.02f, (3.0f - t) / 0.02f);
            float v = 0.0f;
            for (int hh = 1; hh <= 3; ++hh)
                v += (1.0f - x) * 0.3f / (float) hh * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) hh * t + (float) hh)
                     + x * 0.15f / (float) hh * std::sin (juce::MathConstants<float>::twoPi * 165.0f * (float) hh * t + (float) hh);
            v *= edge;
            sections.setSample (0, i, v); sections.setSample (1, i, v);
        }
        auto data = ResynthData::fromAudio (sections, sr, "sections");
        auto render = [&] (bool sh)
        {
            auto qp = std::make_unique<ElukkaAudioProcessor>();
            auto& q = *qp;
            q.setPlayHead (&ph);
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "o1_wave", (float) P::WResynth); setP (q, "o1_level", 1.0f); setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
            setP (q, "rs_sync", 0.0f); setP (q, "ampA", 0.001f);
            if (sh)
            {
                setP (q, "sh_smooth", 0.0f); setP (q, "sh_syncRate", (float) P::syncRateIndex (0.25f));
                setP (q, "mx1_src", (float) P::SrcSH); setP (q, "mx1_dst", (float) P::DstResPos); setP (q, "mx1_amt", 1.0f);
            }
            else
            {
                setP (q, "rs_pos", 0.2f); setP (q, "m1_mode", 0.0f); setP (q, "m1_rate", 4.0f); setP (q, "m1_smooth", 0.0f);
                q.shapes[0].setPoints (ModShape::preset (4));   // square
                setP (q, "mx1_src", (float) P::SrcMod1); setP (q, "mx1_dst", (float) P::DstResPos); setP (q, "mx1_amt", 0.5f);
            }
            q.seedRandom (1);   // the S&H values must not change between runs
            q.setResynth (data);
            q.prepareToPlay (sr, block);
            ph.ppq = 0.0;
            const int total = ((int) (sr * 2.0) / block) * block;
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            out.clear();
            for (int b = 0; b < total / block; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
                q.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
                ph.ppq += block / sr * ph.bpm / 60.0;   // the S&H steps with the song position
            }
            q.setPlayHead (nullptr);
            return out;
        };
        // loudest 5 ms above 2 kHz (4th-order high pass) relative to the render's RMS, ignoring the note's start
        auto hfPeak = [&] (const juce::AudioBuffer<float>& b)
        {
            juce::dsp::IIR::Filter<float> f1, f2;
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2000.0f);
            f1.coefficients = coeffs; f2.coefficients = coeffs;
            std::vector<float> y ((size_t) b.getNumSamples());
            for (int i = 0; i < b.getNumSamples(); ++i) y[(size_t) i] = f2.processSample (f1.processSample (b.getSample (0, i)));
            const int win = (int) (sr * 0.005);
            double worst = 0.0;
            for (int st = (int) (sr * 0.05); st + win < b.getNumSamples(); st += win / 2)
            {
                double e = 0.0;
                for (int i = 0; i < win; ++i) e += y[(size_t) (st + i)] * y[(size_t) (st + i)];
                worst = std::max (worst, std::sqrt (e / win));
            }
            return juce::Decibels::gainToDecibels ((float) worst / b.getRMSLevel (0, 0, b.getNumSamples()), -120.0f);
        };
        const float sq = hfPeak (render (false)), shv = hfPeak (render (true)), srcHf = hfPeak (sections);
        const bool ok = sq < -50.0f && shv < -50.0f;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Resynth Position jumps: loudest click energy above 2 kHz relative to the signal: square LFO "
                  << juce::String (sq, 1) << " dB, S&H 1/16 " << juce::String (shv, 1) << " dB (source itself " << juce::String (srcHf, 1) << " dB)\n";
    }

    // ---- FM on resynth: no aliasing. A 6 kHz partial frequency-modulated by a 261.6 Hz sine only has legitimate
    // components at 6000 + k*261.6 Hz; anything folded back from above Nyquist lands between those lines. ----
    {
        juce::AudioBuffer<float> src6k (2, (int) (sr * 2.5));
        for (int i = 0; i < src6k.getNumSamples(); ++i)
        {
            const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 6000.0f * (float) i / (float) sr);
            src6k.setSample (0, i, v); src6k.setSample (1, i, v);
        }
        ElukkaAudioProcessor fq;
        fq.setPlayConfigDetails (0, 2, sr, block);
        setP (fq, "o1_wave", (float) P::WResynth); setP (fq, "master", 0.0f); setP (fq, "rs_sync", 0.0f); setP (fq, "ampS", 1.0f);
        setP (fq, "o2_on", 1.0f); setP (fq, "o2_wave", (float) P::WSine); setP (fq, "o2_level", 0.0f);
        setP (fq, "o1_fmSrc", 2.0f); setP (fq, "o1_fmAmt", 0.5f);
        fq.setResynth (ResynthData::fromAudio (src6k, sr, "6k"));
        auto out = renderNotes (fq, { 60 }, 2.0);
        const int order = 15, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
        for (int i = 0; i < N; ++i) x[(size_t) i] = out.getSample (0, (int) (0.4 * sr) + i) * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (x.data());
        const double binHz = sr / N, grid = 261.6256;
        double legit = 0.0, alias = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * binHz;
            double offset = std::fmod (hz - 6000.0, grid);
            if (offset < 0) offset += grid;
            const double dist = std::min (offset, grid - offset);
            const double e = (double) x[(size_t) k] * x[(size_t) k];
            if (dist < 15.0) legit += e; else if (dist > 40.0) alias += e;
        }
        const double ratioDb = 10.0 * std::log10 (alias / juce::jmax (1.0e-30, legit));
        const bool aliasOk = ratioDb < -40.0;
        if (! aliasOk) ++failures;
        std::cout << (aliasOk ? "ok   " : "FAIL ") << "FM on resynth, aliasing energy relative to the real FM sidebands: " << juce::String (ratioDb, 1) << " dB\n";
    }

    // ---- OTT, bass mono / sub lift, defaults ----
    {
        // OTT on a quiet and a loud sine: quiet gets lifted, loud gets reduced
        auto ottLevel = [&] (float inputGain, bool enabled)
        {
            FXChain chain;
            chain.prepare (sr, block);
            FXParams fp2;
            fp2.ottOn = enabled; fp2.ottAmount = 1.0f; fp2.ottSpeed = 0.5f;
            FXChain::Order order;
            for (int i = 0; i < P::NumFx; ++i) order[(size_t) i] = P::defaultFxOrder[i];
            std::vector<float> l ((size_t) block), r ((size_t) block);
            float rms = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < block; ++i)
                    l[(size_t) i] = r[(size_t) i] = inputGain * std::sin (juce::MathConstants<float>::twoPi * 500.0f * (float) (b * block + i) / (float) sr);
                chain.process (l.data(), r.data(), block, fp2, 120.0, order);
                if (b > 150) { float sq = 0; for (auto v : l) sq += v * v; rms = std::max (rms, std::sqrt (sq / (float) block)); }
            }
            return juce::Decibels::gainToDecibels (rms, -140.0f);
        };
        const float quietDry = ottLevel (0.01f, false), quietOtt = ottLevel (0.01f, true);
        const float loudDry = ottLevel (0.9f, false), loudOtt = ottLevel (0.9f, true);
        const bool ottOk = quietOtt > quietDry + 6.0f && loudOtt < loudDry - 6.0f;
        if (! ottOk) ++failures;
        std::cout << (ottOk ? "ok   " : "FAIL ") << "OTT 100%: quiet sine " << juce::String (quietDry, 1) << " -> " << juce::String (quietOtt, 1)
                  << " dB, loud sine " << juce::String (loudDry, 1) << " -> " << juce::String (loudOtt, 1) << " dB\n";

        // Bass mono and sub lift on a stereo source: 60 Hz partial hard left (bass), 1 kHz partial hard left too
        juce::AudioBuffer<float> st (2, (int) (sr * 2.5));
        st.clear();
        for (int i = 0; i < st.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) sr;
            st.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 60.0f * t) + 0.01f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * t));
        }
        auto stData = ResynthData::fromAudio (st, sr, "stereo");
        ElukkaAudioProcessor bp;
        bp.setPlayConfigDetails (0, 2, sr, block);
        setP (bp, "o1_wave", (float) P::WResynth); setP (bp, "master", 0.0f); setP (bp, "rs_sync", 0.0f); setP (bp, "ampS", 1.0f);
        bp.setResynth (stData);
        auto lr = [&] (const juce::AudioBuffer<float>& b2, float hz) { return std::make_pair (measure (b2, 0, hz).first, measure (b2, 1, hz).first); };
        auto plain = renderNotes (bp, { 60 }, 2.0);
        setP (bp, "o1_bassMono", 1.0f);
        auto mono = renderNotes (bp, { 60 }, 2.0);
        const auto pl = lr (plain, 60.0f), mo = lr (mono, 60.0f), hiMono = lr (mono, 1000.0f);
        const bool monoOk = pl.second < pl.first * 0.1f && std::abs (mo.first - mo.second) < mo.first * 0.05f && hiMono.second < hiMono.first * 0.1f;
        if (! monoOk) ++failures;
        std::cout << (monoOk ? "ok   " : "FAIL ") << "Bass Mono: 60 Hz partial L/R " << juce::String (pl.first, 3) << "/" << juce::String (pl.second, 3)
                  << " -> " << juce::String (mo.first, 3) << "/" << juce::String (mo.second, 3) << "; 1 kHz partial stays left\n";
        setP (bp, "o1_bassMono", 0.0f);
        // sub lift raises the quiet bass partial only... here: make the bass quiet by lifting the 1 kHz instead
        setP (bp, "rs_subLift", 100.0f);
        auto lifted = renderNotes (bp, { 60 }, 2.0);
        const float bassGain = juce::Decibels::gainToDecibels (measure (lifted, 0, 60.0f).first / measure (plain, 0, 60.0f).first);
        const float highGain = juce::Decibels::gainToDecibels (measure (lifted, 0, 1000.0f).first / measure (plain, 0, 1000.0f).first);
        setP (bp, "rs_subLift", 0.0f); setP (bp, "rs_lift", 100.0f);
        auto liftedHi = renderNotes (bp, { 60 }, 2.0);
        const float highGain2 = juce::Decibels::gainToDecibels (measure (liftedHi, 0, 1000.0f).first / measure (plain, 0, 1000.0f).first);
        const bool subOk = std::abs (highGain) < 0.5f && highGain2 > 10.0f && std::abs (bassGain) < 0.5f;
        if (! subOk) ++failures;
        std::cout << (subOk ? "ok   " : "FAIL ") << "Sub Lift leaves the 1 kHz partial alone (" << juce::String (highGain, 1) << " dB) and the loudest bass partial unchanged ("
                  << juce::String (bassGain, 1) << " dB); Lift raises the quiet 1 kHz partial by " << juce::String (highGain2, 1) << " dB\n";

        // Bass Mono on a wavetable saw with wide unison: below Bass Freq one centred undetuned voice
        {
            auto sawUni = [&] (ElukkaAudioProcessor& q, int voices, bool bassMono, float pan)
            {
                q.setPlayConfigDetails (0, 2, sr, block);
                setP (q, "o1_wave", (float) P::WSaw);
                setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
                setP (q, "o1_uniVoices", (float) voices); setP (q, "o1_uniDetune", 50.0f); setP (q, "o1_phaseMode", 1.0f); setP (q, "o1_rand", 100.0f);
                setP (q, "o1_bassMono", bassMono ? 1.0f : 0.0f); setP (q, "o1_pan", pan);
                return renderNotes (q, { 36 }, 2.0);   // C2: fundamental 65 Hz (bass), 4th harmonic 262 Hz (above the 120 Hz crossover)
            };
            auto midSide = [&] (const juce::AudioBuffer<float>& b2, float hz)
            {
                juce::AudioBuffer<float> ms (2, b2.getNumSamples());
                for (int i = 0; i < b2.getNumSamples(); ++i)
                {
                    ms.setSample (0, i, 0.5f * (b2.getSample (0, i) + b2.getSample (1, i)));
                    ms.setSample (1, i, 0.5f * (b2.getSample (0, i) - b2.getSample (1, i)));
                }
                return std::make_pair (measure (ms, 0, hz).first, measure (ms, 1, hz).first);
            };
            ElukkaAudioProcessor q1, q2, q3, q4, q5;
            const float f1 = 65.41f, f4 = 4.0f * 65.41f;
            auto wide = sawUni (q1, 7, false, 0.0f), monoB = sawUni (q2, 7, true, 0.0f), single = sawUni (q3, 1, false, 0.0f);
            const auto wideF = midSide (wide, f1), monoF = midSide (monoB, f1), monoH = midSide (monoB, f4);
            const float vsSingle = juce::Decibels::gainToDecibels (monoF.first / measure (single, 0, f1).first * 1.0f);
            const bool bmOk = wideF.second > wideF.first * 0.1f && monoF.second < monoF.first * 0.001f && monoH.second > monoH.first * 0.1f
                              && std::abs (vsSingle) < 0.5f;
            if (! bmOk) ++failures;
            std::cout << (bmOk ? "ok   " : "FAIL ") << "Saw unison Bass Mono: 65 Hz side/mid " << juce::String (wideF.second / wideF.first, 3) << " -> "
                      << juce::String (monoF.second / monoF.first, 5) << " (undetuned, " << juce::String (vsSingle, 2) << " dB vs 1 voice); 262 Hz stays wide ("
                      << juce::String (monoH.second / monoH.first, 2) << ")\n";

            // Pan: hard left keeps a centred sound's loudness (+3 dB on the left), right side silent; Bass Mono bass stays centred
            auto left = sawUni (q4, 1, false, -100.0f);
            const float lGain = juce::Decibels::gainToDecibels (measure (left, 0, f1).first / measure (single, 0, f1).first);
            const float rRel = measure (left, 1, f1).first / measure (left, 0, f1).first;
            auto leftMono = sawUni (q5, 7, true, -100.0f);
            const auto lmF = lr (leftMono, f1), lmH = lr (leftMono, f4);
            const bool panOk = std::abs (lGain - 3.01f) < 0.3f && rRel < 0.001f && std::abs (lmF.first - lmF.second) < lmF.first * 0.01f
                               && lmH.second < lmH.first * 0.01f;
            if (! panOk) ++failures;
            std::cout << (panOk ? "ok   " : "FAIL ") << "Pan -100%: left " << juce::String (lGain, 2) << " dB, right " << juce::String (juce::Decibels::gainToDecibels (rRel, -200.0f), 0)
                      << " dB; with Bass Mono the 65 Hz bass stays centred (L/R " << juce::String (lmF.first, 3) << "/" << juce::String (lmF.second, 3)
                      << "), 262 Hz goes left (R " << juce::String (juce::Decibels::gainToDecibels (lmH.second / lmH.first, -200.0f), 0) << " dB)\n";
        }

        // Old saves: the Resynth Bass Mono switch moves to the oscillators that play the resynth
        {
            ElukkaAudioProcessor src;
            setP (src, "o2_on", 1.0f); setP (src, "o2_wave", (float) P::WResynth);
            auto tree = src.saveState();
            juce::ValueTree old ("PARAM");
            old.setProperty ("id", "rs_bassMono", nullptr);
            old.setProperty ("value", 1.0f, nullptr);
            tree.appendChild (old, nullptr);
            for (auto id : { "o1_bassMono", "o2_bassMono", "o3_bassMono" })
                tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
            ElukkaAudioProcessor dst;
            dst.loadState (tree);
            auto v = [&] (const char* id) { return dst.apvts.getRawParameterValue (id)->load(); };
            const bool migOk = v ("o1_bassMono") < 0.5f && v ("o2_bassMono") > 0.5f && v ("o3_bassMono") < 0.5f;
            if (! migOk) ++failures;
            std::cout << (migOk ? "ok   " : "FAIL ") << "Old save with Resynth Bass Mono on -> Bass Mono on the resynth oscillator only\n";
        }

        ElukkaAudioProcessor fresh;
        const bool defOk = (int) fresh.apvts.getRawParameterValue ("m1_mode")->load() == P::MEnvelope
                           && std::abs (fresh.shapes[0].eval (0.0f) - 1.0f) < 0.01f && fresh.shapes[0].eval (1.0f) < 0.01f
                           && fresh.fxOrder[1].load() == P::FxOtt && fresh.fxOrder[2].load() == P::FxDist
                           && std::abs (fresh.apvts.getRawParameterValue ("comb_freq")->load() - 40.0f) < 0.01f;
        if (! defOk) ++failures;
        std::cout << (defOk ? "ok   " : "FAIL ") << "Defaults: Mod 1 = decay envelope, OTT before Distortion, comb 40 Hz\n";
    }

    // ---- Sequencer release tails, preset clears the resynth, idle mod display ----
    {
        // C, G alternating 1/16 notes (125 ms at 120 BPM), short gate, 1 s release: C must still ring under the G
        auto tailOf = [&] (float relSec)
        {
            auto q = std::make_unique<ElukkaAudioProcessor>();
            q->setPlayConfigDetails (0, 2, sr, block);
            setP (*q, "o1_wave", (float) P::WSine); setP (*q, "master", 0.0f); setP (*q, "ampS", 1.0f); setP (*q, "ampR", relSec);
            setP (*q, "sq_on", 1.0f); setP (*q, "sq_gate", 0.3f);
            q->pattern = SeqPattern::fromText ("t", "xx", "C G");
            auto out = renderNotes (*q, { 60 }, 0.6);
            // level of the C (261.6 Hz) between 180 and 230 ms, while the G plays
            const int s0 = (int) (0.18 * sr), n = (int) (0.05 * sr);
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * 261.63 * i / sr, x = out.getSample (0, s0 + i);
                re += x * std::cos (ph); im += x * std::sin (ph);
            }
            return juce::Decibels::gainToDecibels ((float) (2.0 * std::hypot (re, im) / n), -200.0f);
        };
        const float withTail = tailOf (1.0f), noTail = tailOf (0.005f);

        // presets: an image preset sets the resynth source, the next preset without one clears it
        auto q = std::make_unique<ElukkaAudioProcessor>();
        auto& pm = q->presets;
        int imagePreset = -1, plainPreset = -1;
        for (int i = 0; i < (int) pm.entries.size(); ++i)
        {
            if (pm.entries[(size_t) i].name == "Rain On Glass") imagePreset = i;
            if (pm.entries[(size_t) i].name == "Morning Haze") plainPreset = i;
        }
        pm.load (imagePreset);
        const bool hadImage = q->getResynth() != nullptr;
        pm.load (plainPreset);
        const bool cleared = q->getResynth() == nullptr;
        pm.load (imagePreset);
        pm.load (0);   // Reset / Init
        const bool initCleared = q->getResynth() == nullptr;

        // mod display: still while nothing plays, moving while a note plays
        q->setPlayConfigDetails (0, 2, sr, block);
        q->prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer none;
        for (int b = 0; b < 20; ++b) q->processBlock (buf, none);
        const bool idleStill = ! q->uiPlaying.load() && q->uiMod[1].load() == 0.0f;
        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        q->processBlock (buf, noteOn);
        const bool playingShown = q->uiPlaying.load();

        const bool ok = withTail > noTail + 20.0f && withTail > -40.0f && hadImage && cleared && initCleared && idleStill && playingShown;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Sequencer tails: previous note under the next one " << juce::String (withTail, 1) << " dB (release 1 s) vs "
                  << juce::String (noTail, 1) << " dB (5 ms); presets clear the resynth source (" << (hadImage && cleared && initCleared ? "yes" : "no")
                  << "); mod displays idle without notes (" << (idleStill && playingShown ? "yes" : "no") << ")\n";
    }

    // ---- Sequencer pitch range, old pattern format, randomize ----
    {
        // older saves: note:octave:accent:slide -> semitones
        juce::ValueTree old ("Pattern");
        old.setProperty ("length", 2, nullptr); old.setProperty ("numPitches", 2, nullptr);
        old.setProperty ("steps", "00", nullptr); old.setProperty ("pitches", "7:1:1:0 3:-1:0:1", nullptr);
        SeqPattern pa;
        pa.fromTree (old);
        const bool oldOk = pa.pitches[0].semis == 19 && pa.pitches[0].accent && pa.pitches[1].semis == -9 && pa.pitches[1].slide;
        SeqPattern pb;
        pa.pitches[0].semis = -29;
        pb.fromTree (pa.toTree());
        const bool roundOk = pb.pitches[0].semis == -29 && pb.pitches[1].semis == -9;

        // a +31 step plays 31 semitones above the held key
        SeqPattern wide;
        wide.length = 1; wide.numPitches = 1; wide.pitches[0].semis = 31;
        SeqEngine eng;
        std::vector<SeqEvent> ev;
        eng.process (wide, 4096, sr, 120.0, false, 0.0, true, 48, 0.25, 0.5f, 0.5f, ev);
        int played = -1;
        for (auto& e2 : ev) if (e2.on) { played = e2.note; break; }

        // the Randomize button: minor scale, +-1 octave (the defaults)
        auto q = std::make_unique<ElukkaAudioProcessor>();
        SeqPage page (*q);
        bool clicked = false, inScale = true, varied = false;
        for (auto* c : page.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Randomize Pitch") { b->onClick(); clicked = true; }
        const std::vector<int> minor { 0, 2, 3, 5, 7, 8, 10 };
        for (auto& e2 : q->pattern.pitches)
        {
            inScale &= std::abs (e2.semis) <= 12 && std::find (minor.begin(), minor.end(), ((e2.semis % 12) + 12) % 12) != minor.end();
            varied |= e2.semis != q->pattern.pitches[0].semis;
        }
        const bool ok = oldOk && roundOk && played == 48 + 31 && clicked && inScale && varied;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Sequencer: old patterns convert (" << (oldOk ? "yes" : "no") << "), -29 survives saving ("
                  << (roundOk ? "yes" : "no") << "), +31 step plays note " << played << " over key 48, Randomize gives minor-scale pitches within an octave ("
                  << (clicked && inScale && varied ? "yes" : "no") << ")\n";
    }

    // ---- Knob modulation ring and badge ----
    {
        auto q = std::make_unique<ElukkaAudioProcessor>();
        auto raw = [&] (const char* id) { return q->apvts.getRawParameterValue (id)->load(); };
        Knob plain (q->apvts, "hm_stretch", "Stretch");
        setP (*q, "mx1_src", (float) P::SrcMod1); setP (*q, "mx1_dst", (float) P::DstCutoff); setP (*q, "mx1_amt", 0.3f);
        setP (*q, "mx2_src", (float) P::SrcMacro1); setP (*q, "mx2_dst", (float) P::DstCutoff); setP (*q, "mx2_amt", 0.2f);
        Knob knob (q->apvts, "hm_cutoff", "Cutoff");
        knob.setBounds (0, 0, 66, 66);
        ModBadge* badge = nullptr;
        for (auto* c : knob.getChildren()) if (auto* b = dynamic_cast<ModBadge*> (c)) badge = b;
        bool plainBadge = false;
        for (auto* c : plain.getChildren()) if (auto* b = dynamic_cast<ModBadge*> (c)) plainBadge |= b->isVisible();
        bool ok = badge != nullptr && badge->isVisible() && ! plainBadge;
        float up1 = 0, up2 = 0, dn1 = 0, dn2 = 0;
        if (ok)
        {
            badge->onDragStart(); badge->onDrag (0.25f, false); badge->onDragEnd();     // total 0.5 -> 0.75
            up1 = raw ("mx1_amt"); up2 = raw ("mx2_amt");
            badge->onDragStart(); badge->onDrag (-1.25f, false); badge->onDragEnd();    // total 0.75 -> -0.5
            dn1 = raw ("mx1_amt"); dn2 = raw ("mx2_amt");
            ok = std::abs (up1 - 0.45f) < 0.01f && std::abs (up2 - 0.30f) < 0.01f && std::abs (dn1 + 0.30f) < 0.01f && std::abs (dn2 + 0.20f) < 0.01f;
        }
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Mod badge drag scales both mappings: 0.30/0.20 -> " << juce::String (up1, 2) << "/" << juce::String (up2, 2)
                  << " -> " << juce::String (dn1, 2) << "/" << juce::String (dn2, 2) << "; unmodulated knob has no badge\n";
    }

    // Badge on a pitch knob: dragged with synthetic mouse events one pixel at a time from 0 to +48 st, each octave of
    // the total (0.25 = 12 st) holds for ~40 px and the (continuous) amount between two octaves takes ~22 px, about
    // 2 px per semitone; Shift-drag has no stops.
    {
        auto q = std::make_unique<ElukkaAudioProcessor>();
        setP (*q, "mx1_src", (float) P::SrcMod1); setP (*q, "mx1_dst", (float) P::DstO1Pitch); setP (*q, "mx1_amt", 0.0f);
        Knob knob (q->apvts, "o1_coarse", "Coarse");
        knob.setBounds (0, 0, 66, 66);
        ModBadge* badge = nullptr;
        for (auto* c : knob.getChildren()) if (auto* b = dynamic_cast<ModBadge*> (c)) badge = b;
        auto sweep = [&] (bool shift, std::map<int, int>& semitonePx, int& between)
        {
            setP (*q, "mx1_amt", 0.0f);
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const juce::Point<float> start { 6.0f, 6.0f };
            const auto mods = shift ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier) : juce::ModifierKeys();
            auto ev = [&] (float dx, bool dragged)
            {
                return juce::MouseEvent (src, { start.x + dx, start.y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, badge, badge, juce::Time::getCurrentTime(),
                                         start, juce::Time::getCurrentTime(), 1, dragged);
            };
            badge->mouseDown (ev (0.0f, false));
            for (int dx = 0; dx <= 600; ++dx)
            {
                badge->mouseDrag (ev ((float) dx, true));
                const double st = q->apvts.getRawParameterValue ("mx1_amt")->load() * P::pitchModSemitones;
                if (std::abs (st - std::round (st)) < 1.0e-3) ++semitonePx[(int) std::lround (st)];
                if (st > 12.001 && st < 23.999) ++between;
            }
            badge->mouseUp (ev (600.0f, true));
        };
        bool ok = badge != nullptr && badge->isVisible();
        double octavePx = 0.0, otherPx = 0.0, shiftOctavePx = 0.0;
        if (ok)
        {
            std::map<int, int> px, fine;
            int between = 0, fineBetween = 0;
            sweep (false, px, between);
            sweep (true, fine, fineBetween);
            for (int stv : { 12, 24, 36 }) octavePx += px[stv] / 3.0;
            otherPx = between / 12.0;
            shiftOctavePx = fine[12];
            ok = std::abs (octavePx - 40.0) < 3.0 && std::abs (otherPx - 2.0) < 0.6 && shiftOctavePx < 3.0 && px[48] > 0;
        }
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Mod badge on a pitch knob stops at octaves: " << juce::String (octavePx, 1) << " px per octave, "
                  << juce::String (otherPx, 1) << " px per other semitone; with Shift " << juce::String (shiftOctavePx, 0) << " px at +12 st\n";
    }

    // ---- Resynthesis accuracy (see ResynthEval.h; `--resynth <dir>` runs the full comparison) ----
    {
        double cpu = 0.0, ms = 0.0;
        juce::AudioBuffer<float> saw (2, (int) (sr * 2.0));
        double ph = 0.0;
        for (int i = 0; i < saw.getNumSamples(); ++i)
        {
            ph += 110.0 / sr; ph -= std::floor (ph);
            saw.setSample (0, i, 0.4f * (float) (2.0 * ph - 1.0));
        }
        saw.copyFrom (1, 0, saw, 0, 0, saw.getNumSamples());
        const float shape = ResynthEval::waveCorrelation (saw, ResynthEval::resynthesize (saw, cpu, ms));

        // a bright tone that starts after 0.5 s of silence: nothing may be heard before it (pre-echo)
        juce::AudioBuffer<float> burst (2, (int) (sr * 1.0));
        burst.clear();
        ph = 0.0;
        for (int i = (int) (sr * 0.5); i < burst.getNumSamples(); ++i)
        {
            ph += 220.0 / sr; ph -= std::floor (ph);
            const float t = (float) (i - (int) (sr * 0.5)) / (float) sr;
            burst.setSample (0, i, 0.5f * (float) (2.0 * ph - 1.0) * std::exp (-t * 8.0f));
        }
        burst.copyFrom (1, 0, burst, 0, 0, burst.getNumSamples());
        auto out = ResynthEval::resynthesize (burst, cpu, ms);
        // 40..15 ms before the onset (levels glide between analysis frames 5.8 ms apart, so the last few ms ramp in)
        const float before = out.getRMSLevel (0, (int) (sr * 0.46), (int) (sr * 0.025));
        const float after = out.getRMSLevel (0, (int) (sr * 0.5), (int) (sr * 0.05));
        const float preDb = juce::Decibels::gainToDecibels (before / after, -200.0f);
        const bool rsOk = shape > 0.95f && preDb < -30.0f;
        if (! rsOk) ++failures;
        std::cout << (rsOk ? "ok   " : "FAIL ") << "Resynthesis: sawtooth waveform match " << juce::String (shape, 3)
                  << ", pre-echo before an onset " << juce::String (preDb, 1) << " dB\n";
    }

    // ---- Resynthesis engine acceptance (pure sines, moving partials, transients, images, source swaps, fidelity) ----
    {
        // Strongest component near hz over a 1 s Hann window from 0.5 s: (amplitude, frequency)
        auto peakNear = [&] (const juce::AudioBuffer<float>& b, float hz)
        {
            const int order = 15, N = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
            const int start = (int) (0.5 * sr);
            for (int i = 0; i < N && start + i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (0, start + i) * w[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (x.data());
            const float binHz = (float) sr / N;
            float best = 0.0f, bestHz = 0.0f;
            for (int k = (int) (hz * 0.9f / binHz); k <= (int) (hz * 1.1f / binHz); ++k)
                if (x[(size_t) k] > best)
                {
                    const float a = std::log (x[(size_t) k - 1] + 1e-12f), b2 = std::log (x[(size_t) k] + 1e-12f), c = std::log (x[(size_t) k + 1] + 1e-12f);
                    const float d = a - 2.0f * b2 + c;
                    best = x[(size_t) k];
                    bestHz = ((float) k + (std::abs (d) > 1e-9f ? 0.5f * (a - c) / d : 0.0f)) * binHz;
                }
            return std::make_pair (best * 4.0f / N, bestHz);
        };
        // Level stability: spread (dB) of the RMS over 50 ms windows between 0.5 s and 0.1 s before the end (the
        // analysis windows run off the end of the source, so its last tens of ms fade)
        auto levelSpread = [&] (const juce::AudioBuffer<float>& b)
        {
            const int win = (int) (sr * 0.05);
            float lo = 1e9f, hi = -1e9f;
            for (int s = (int) (sr * 0.5); s + win <= b.getNumSamples() - (int) (sr * 0.1); s += win)
            {
                const float db = juce::Decibels::gainToDecibels (b.getRMSLevel (0, s, win), -120.0f);
                lo = std::min (lo, db); hi = std::max (hi, db);
            }
            return hi - lo;
        };

        // Test 1: pure sines. Frequency, level and steadiness (no beating from duplicated or wobbling partials).
        bool sineOk = true;
        juce::String sineReport;
        for (float hz : { 55.0f, 440.0f, 3520.0f })
        {
            juce::AudioBuffer<float> tone (2, (int) (sr * 2.0));
            for (int i = 0; i < tone.getNumSamples(); ++i)
            {
                const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) sr);
                tone.setSample (0, i, v); tone.setSample (1, i, v);
            }
            double cpu = 0.0, ms = 0.0;
            auto out = ResynthEval::resynthesize (tone, cpu, ms);
            // the source goes through the same measurements, so window effects cancel
            const auto [amp, f] = peakNear (out, hz);
            const auto [ampSrc, fSrc] = peakNear (tone, hz);
            const float levelDb = juce::Decibels::gainToDecibels (amp / ampSrc, -120.0f), cents = 1200.0f * std::log2 (f / fSrc);
            const float spreadOut = levelSpread (out), spreadSrc = levelSpread (tone);
            const bool ok = std::abs (levelDb) < 1.0f && std::abs (cents) < 3.0f && spreadOut - spreadSrc < 0.5f;
            sineOk &= ok;
            sineReport << (int) hz << " Hz: " << juce::String (levelDb, 2) << " dB, " << juce::String (cents, 1) << " c, spread " << juce::String (spreadOut, 2) << " vs source " << juce::String (spreadSrc, 2) << " dB; ";
        }
        if (! sineOk) ++failures;
        std::cout << (sineOk ? "ok   " : "FAIL ") << "Resynth pure sines (level error, pitch error, level spread beyond the source's): " << sineReport << "\n";

        // Test 5: moving partials. A glide with vibrato must come back smooth: low track wobble, small spectral
        // distance, no clicks.
        {
            juce::AudioBuffer<float> glide (2, (int) (sr * 2.0));
            double phs = 0.0;
            for (int i = 0; i < glide.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) sr;
                const float hz = 220.0f * std::exp2 (t * 0.5f) * (1.0f + 0.01f * std::sin (juce::MathConstants<float>::twoPi * 5.0f * t));
                phs += hz / sr;
                float v = 0.0f;
                for (int h = 1; h <= 6; ++h) v += 0.2f / (float) h * (float) std::sin (juce::MathConstants<double>::twoPi * h * phs);
                glide.setSample (0, i, v); glide.setSample (1, i, v);
            }
            double cpu = 0.0, ms = 0.0;
            float wob = 0.0f;
            auto out = ResynthEval::resynthesize (glide, cpu, ms, &wob);
            const float dist = ResynthEval::spectralDistance (glide, out);
            const auto rep = detectClicks (out, 0.05f, 1000);
            const bool ok = wob < 4.0f && dist < 3.0f && rep.clicks == 0;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Resynth glide + vibrato: track wobble " << juce::String (wob, 2) << " c, distance " << juce::String (dist, 2)
                      << " dB, clicks " << rep.clicks << "\n";
        }

        // Test 4: transients. The plucked string's attacks (first 15 ms of each note) must land at the right time
        // and level, and the silence before the first one must stay silent.
        {
            juce::AudioBuffer<float> pluck;
            for (auto& s : ResynthEval::generatedSources()) if (s.name == "pluck") pluck = s.audio;
            double cpu = 0.0, ms = 0.0;
            auto out = ResynthEval::resynthesize (pluck, cpu, ms);
            float worst = 0.0f;
            const int win = (int) (sr * 0.015);
            for (int n = 0; n < 3; ++n)
            {
                const int onset = (int) (n * sr);
                const float a = pluck.getRMSLevel (0, onset, win), b = out.getRMSLevel (0, onset, win);
                worst = std::max (worst, std::abs (juce::Decibels::gainToDecibels (b / a, -120.0f)));
            }
            // level just before the second and third onsets (the previous note has decayed; nothing may jump in early)
            float pre = -200.0f;
            for (int n = 1; n < 3; ++n)
                pre = std::max (pre, juce::Decibels::gainToDecibels (out.getRMSLevel (0, (int) (n * sr) - 3 * win, 2 * win) / pluck.getRMSLevel (0, (int) (n * sr), win), -200.0f)
                                     - juce::Decibels::gainToDecibels (pluck.getRMSLevel (0, (int) (n * sr) - 3 * win, 2 * win) / pluck.getRMSLevel (0, (int) (n * sr), win), -200.0f));
            const bool ok = worst < 3.0f && pre < 6.0f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Resynth pluck attacks: worst level error in the first 15 ms " << juce::String (worst, 2)
                      << " dB, pre-onset excess " << juce::String (pre, 1) << " dB\n";
        }

        // Test 6: fidelity thresholds on the generated set (the full comparison is `--resynth <dir>`)
        {
            struct Lim { const char* name; float maxDist; };
            const Lim lims[] { { "saw", 3.0f }, { "noise", 5.0f }, { "drums", 6.0f }, { "voice", 3.0f }, { "chord", 3.5f } };
            bool ok = true;
            juce::String rep;
            for (auto& l : lims)
                for (auto& s : ResynthEval::generatedSources())
                    if (s.name == l.name)
                    {
                        double cpu = 0.0, ms = 0.0;
                        const float dist = ResynthEval::spectralDistance (s.audio, ResynthEval::resynthesize (s.audio, cpu, ms));
                        ok &= dist < l.maxDist;
                        rep << l.name << " " << juce::String (dist, 2) << " (< " << juce::String (l.maxDist, 1) << ")  ";
                    }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Resynth fidelity, spectral distance dB: " << rep << "\n";
        }

        // Test 7: images. Horizontal line = one steady harmonic; diagonal = rising harmonic; isolated bright spot;
        // smooth gradient = rising level. No clicks, finite output, predictable levels.
        {
            // 256 rows = the 256 harmonic slots (bottom row = fundamental); content stays in the bottom 24 rows (up to 6.3 kHz)
            auto makeImage = [] (std::function<void (juce::Graphics&)> draw)
            {
                juce::Image img (juce::Image::RGB, 128, 256, true);
                juce::Graphics g (img);
                g.fillAll (juce::Colours::black);
                g.setColour (juce::Colours::white);
                draw (g);
                return img;
            };
            struct Img { const char* name; juce::Image image; };
            const std::vector<Img> images {
                { "line", makeImage ([] (juce::Graphics& g) { g.fillRect (0, 248, 128, 2); }) },             // harmonic 8
                { "diagonal", makeImage ([] (juce::Graphics& g) { g.drawLine (0.0f, 255.0f, 127.0f, 232.0f, 2.0f); }) },   // harmonics 1 -> 24
                { "spot", makeImage ([] (juce::Graphics& g) { g.fillEllipse (56.0f, 236.0f, 16.0f, 12.0f); }) },
                { "gradient", makeImage ([] (juce::Graphics& g) { g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, 0.0f, juce::Colours::white, 127.0f, 0.0f, false)); g.fillRect (0, 240, 128, 16); }) } };
            bool ok = true;
            juce::String rep;
            for (auto& im : images)
            {
                ElukkaAudioProcessor ip;
                ip.setPlayHead (&ph);
                ip.setPlayConfigDetails (0, 2, sr, block);
                setP (ip, "o1_wave", (float) P::WResynth); setP (ip, "o1_level", 1.0f); setP (ip, "master", 0.0f);
                setP (ip, "rs_sync", 0.0f); setP (ip, "rs_loop", 0.0f); setP (ip, "ampA", 0.001f); setP (ip, "ampS", 1.0f);
                ip.setResynth (ResynthData::fromImage (im.image, im.name));
                auto out = renderNotes (ip, { 60 }, 2.0);
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (ch, i));
                // (no click probe here: it flags any strong content above ~5 kHz; the image rows go up to 6.3 kHz)
                ClickReport rep2;
                float q[4];
                for (int k = 0; k < 4; ++k) q[k] = juce::Decibels::gainToDecibels (out.getRMSLevel (0, k * out.getNumSamples() / 4, out.getNumSamples() / 4), -120.0f);
                bool shape = true;
                if (juce::String (im.name) == "line") shape = std::abs (q[0] - q[3]) < 1.0f;                        // steady
                if (juce::String (im.name) == "gradient") shape = q[1] > q[0] + 3.0f && q[2] > q[1] + 1.0f && q[3] > q[2];   // rising
                if (juce::String (im.name) == "spot") shape = q[1] > q[0] + 20.0f && q[1] > q[3] + 20.0f;             // only in the middle
                if (juce::String (im.name) == "diagonal") shape = std::abs (q[0] - q[3]) < 6.0f && q[3] > -60.0f;      // keeps playing to the top harmonic
                ok &= finite && rep2.clicks == 0 && shape;
                rep << im.name << (finite && rep2.clicks == 0 && shape ? " ok" : " FAIL") << " (" << juce::String (q[0], 0) << "/" << juce::String (q[1], 0) << "/" << juce::String (q[2], 0) << "/" << juce::String (q[3], 0) << " dB" << (rep2.clicks > 0 ? ", clicks" : "") << ") ";
                ip.setPlayHead (nullptr);
            }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Image resynthesis: " << rep << "\n";
        }

        // Test 8: swapping or clearing the source while a note plays never interrupts the audio thread
        {
            juce::AudioBuffer<float> a (2, (int) sr), b (2, (int) sr);
            juce::Random swapRnd (3);
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) sr;
                a.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 200.0f * t)); a.setSample (1, i, a.getSample (0, i));
                b.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 300.0f * t) + 0.05f * (swapRnd.nextFloat() - 0.5f)); b.setSample (1, i, b.getSample (0, i));
            }
            auto da = ResynthData::fromAudio (a, sr, "a"), db = ResynthData::fromAudio (b, sr, "b");
            ElukkaAudioProcessor sp;
            sp.setPlayHead (&ph);
            sp.setPlayConfigDetails (0, 2, sr, block);
            setP (sp, "o1_wave", (float) P::WResynth); setP (sp, "o1_level", 1.0f); setP (sp, "master", 0.0f); setP (sp, "rs_sync", 0.0f); setP (sp, "ampS", 1.0f);
            sp.setResynth (da);
            sp.prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block);
            bool finite = true;
            float peak = 0.0f;
            for (int n = 0; n < 200; ++n)
            {
                juce::MidiBuffer midi;
                if (n == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
                if (n % 25 == 10) sp.setResynth (n % 50 == 10 ? db : da);
                if (n == 120) sp.setResynth (nullptr);
                if (n == 150) sp.setResynth (da);
                sp.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) { finite &= std::isfinite (buf.getSample (ch, i)); peak = std::max (peak, std::abs (buf.getSample (ch, i))); }
            }
            const bool ok = finite && peak < 2.0f && peak > 0.01f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Resynth source swapped/cleared mid-note: output finite, peak " << juce::String (peak, 3) << "\n";
            sp.setPlayHead (nullptr);
        }
    }

    // ---- Sample tuning: pitch detection; Tune 100% moves every note to the nearest note of the key's pitch class ----
    {
        auto tp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *tp;
        q.setPlayHead (&ph);
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "o1_wave", (float) P::WResynth); setP (q, "o1_level", 1.0f); setP (q, "master", 0.0f);
        setP (q, "rs_sync", 0.0f); setP (q, "rs_loop", 0.0f); setP (q, "ampA", 0.001f); setP (q, "ampS", 1.0f);

        auto makeTone = [&] (double secs, std::function<float (float t)> fn)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * secs));
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = fn ((float) i / (float) sr); b.setSample (0, i, v); b.setSample (1, i, v); }
            return b;
        };
        auto cents = [] (float a, float b) { return 1200.0f * std::log2 (a / b); };
        auto midiOf = [] (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); };
        auto hzOf = [] (float midi) { return 440.0f * std::exp2 ((midi - 69.0f) / 12.0f); };
        // where Tune 100% puts a note of `hz` when MIDI key `key` is played: the nearest note of the key's pitch class
        // (halfway goes up), moved by the key's octave from middle C (written out independently of the engine)
        auto nearest = [&] (float hz, int key)
        {
            const int keyOctave = (int) std::floor ((key - 60) / 12.0), keyClass = key - 12 * keyOctave;   // 60..71
            const float m = midiOf (hz);
            return hzOf ((float) keyClass + 12.0f * std::floor ((m - (float) keyClass) / 12.0f + 0.5f) + 12.0f * (float) keyOctave);
        };
        // Independent pitch estimate: normalised autocorrelation of channel 0 over `len` samples from `start`.
        auto acfPitch = [&] (const juce::AudioBuffer<float>& b, int start, int len, float loHz, float hiHz)
        {
            const float* x = b.getReadPointer (0) + start;
            const int lagLo = juce::jmax (2, (int) (sr / hiHz)), lagHi = (int) (sr / loHz);
            std::vector<double> r ((size_t) lagHi + 2, 0.0);
            for (int lag = lagLo - 1; lag <= lagHi + 1; ++lag)
            {
                double sxy = 0.0, sxx = 0.0, syy = 0.0;
                for (int i = 0; i < len; ++i) { sxy += x[i] * x[i + lag]; sxx += x[i] * x[i]; syy += x[i + lag] * x[i + lag]; }
                r[(size_t) lag] = sxy / std::sqrt (sxx * syy + 1.0e-20);
            }
            double rMax = -1.0;
            for (int lag = lagLo; lag <= lagHi; ++lag) rMax = std::max (rMax, r[(size_t) lag]);
            int best = lagLo;   // the first local maximum within 10 % of the highest: multiples of the period peak too
            for (int lag = lagLo; lag <= lagHi; ++lag)
                if (r[(size_t) lag] >= 0.9 * rMax && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { best = lag; break; }
            const double a = r[(size_t) best - 1], c = r[(size_t) best + 1], d = a - 2.0 * r[(size_t) best] + c;
            return (float) (sr / ((double) best + (std::abs (d) > 1.0e-12 ? 0.5 * (a - c) / d : 0.0)));
        };
        // median pitch and excursion (max - min, cents) over 46 ms windows between 0.3 s and 0.2 s before the end
        auto pitchTrack = [&] (const juce::AudioBuffer<float>& b, float loHz, float hiHz, float& excursion)
        {
            std::vector<float> f;
            const int len = 2048;
            for (int st = (int) (sr * 0.3); st + len + (int) (sr / loHz) + 2 < b.getNumSamples() - (int) (sr * 0.2); st += 1024)
                f.push_back (acfPitch (b, st, len, loHz, hiHz));
            std::sort (f.begin(), f.end());
            excursion = cents (f.back(), f.front());
            return f[f.size() / 2];
        };
        // Note tags that change and change back within 50 ms while a partial sounds (each would wiggle its pitch under Tune)
        auto shortTagSegments = [] (const ResynthData& d)
        {
            const int S = d.numSlots, hold = (int) std::lround (0.05 * d.frameRate);
            int bad = 0;
            for (int s = 0; s < S; ++s)
                for (int f = 1, run = 1; f < d.numFrames; ++f)
                {
                    const size_t i = (size_t) f * (size_t) S + (size_t) s;
                    const bool on = d.amp[i] > 0.0f, wasOn = d.amp[i - (size_t) S] > 0.0f;
                    if (on && wasOn && std::abs (d.noteMidi[i] - d.noteMidi[i - (size_t) S]) > 0.01f)
                    {
                        int j = f;
                        while (j < d.numFrames && d.amp[(size_t) j * (size_t) S + (size_t) s] > 0.0f
                               && std::abs (d.noteMidi[(size_t) j * (size_t) S + (size_t) s] - d.noteMidi[i]) < 0.01f) ++j;
                        if (j < d.numFrames && d.amp[(size_t) j * (size_t) S + (size_t) s] > 0.0f && j - f < hold) ++bad;
                    }
                    run = on ? run + 1 : 0;
                }
            return bad;
        };
        auto renderTuned = [&] (std::shared_ptr<ResynthData> d, int note, float tunePct, double secs)
        {
            q.setResynth (d);
            setP (q, "rs_tune", tunePct);
            return renderNotes (q, { note }, secs);
        };

        // Test A: pure sines across the range. Detected within 2 cents, no octave errors; Tune 100% lands them on
        // the C of their own octave within 2 cents (measured on the output with a long FFT).
        {
            bool ok = true;
            juce::String rep;
            for (float hz : { 40.0f, 65.41f, 110.0f, 196.5f, 440.0f, 1760.0f })
            {
                auto d = ResynthData::fromAudio (makeTone (2.5, [hz] (float t) { return 0.3f * std::sin (juce::MathConstants<float>::twoPi * hz * t); }), sr, "sine");
                const float detErr = cents (d->detectedFundamentalHz, hz);
                auto out = renderTuned (d, 60, 100.0f, 2.0);
                const float target = nearest (hz, 60), tuneErr = cents (measure (out, 0, target).second, target);
                ok &= std::abs (detErr) < 2.0f && std::abs (tuneErr) < 2.0f && d->pitchConfidence > 0.9f;
                rep << (int) hz << " Hz: " << juce::String (detErr, 1) << " c, tuned " << juce::String (tuneErr, 1) << " c; ";
            }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning A, pure sines (detection error, error after Tune 100%): " << rep << "\n";
        }

        // Test B: missing fundamental. Harmonics 2..8 of 150 Hz only: the detector must still find 150 Hz (not 300).
        {
            auto d = ResynthData::fromAudio (makeTone (2.5, [] (float t)
            {
                float v = 0.0f;
                for (int h = 2; h <= 8; ++h) v += 0.25f / (float) h * std::sin (juce::MathConstants<float>::twoPi * 150.0f * (float) h * t);
                return v;
            }), sr, "missing");
            const float err = cents (d->detectedFundamentalHz, 150.0f);
            // Tune 100%: 150 Hz -> C3 (130.81 Hz); the 2nd harmonic lands on 261.63 Hz
            auto out = renderTuned (d, 60, 100.0f, 2.0);
            const float h2 = cents (measure (out, 0, 261.63f).second, 261.63f);
            const bool ok = std::abs (err) < 3.0f && std::abs (h2) < 2.0f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning B, missing fundamental: detected " << juce::String (d->detectedFundamentalHz, 1) << " Hz (" << juce::String (err, 1)
                      << " c from 150, confidence " << juce::String (d->pitchConfidence, 2) << "), tuned 2nd harmonic " << juce::String (h2, 1) << " c from C4\n";
        }

        // Test C + E + G: a harmonic tone (8 harmonics) at G3 +4 c with 5.5 Hz vibrato of +-20 c. Detection within 5 c;
        // Tune 100% puts it on C4 (the nearest C) and keeps the vibrato; other keys and Tune 50% land where the maths says
        // (independent autocorrelation estimate, median over the sustain, within 2 c).
        {
            const float base = 196.5f;
            auto harm = [base] (float depth) { return [base, depth] (float t)
            {
                const float hz = base * std::exp2 (depth / 1200.0f * std::sin (juce::MathConstants<float>::twoPi * 5.5f * t));
                // integrate the frequency analytically: phase = base t + (depth term) handled by small-angle approx is not exact,
                // so use a cumulative phase via the closed form of the sine's integral
                const float phs = base * t - base * depth / 1200.0f * std::log (2.0f) * std::cos (juce::MathConstants<float>::twoPi * 5.5f * t) / (juce::MathConstants<float>::twoPi * 5.5f);
                juce::ignoreUnused (hz);
                float v = 0.0f;
                for (int h = 1; h <= 8; ++h) v += 0.25f / (float) h * std::sin (juce::MathConstants<float>::twoPi * (float) h * phs);
                return v;
            }; };
            auto vib = makeTone (2.5, harm (20.0f));
            auto dv = ResynthData::fromAudio (vib, sr, "vibrato");
            const float detErr = cents (dv->detectedFundamentalHz, base);
            float excSrc = 0.0f, excOut = 0.0f;
            const float srcMedian = pitchTrack (vib, 150.0f, 600.0f, excSrc);
            auto out = renderTuned (dv, 60, 100.0f, 2.0);
            const float outMedian = pitchTrack (out, 90.0f, 400.0f, excOut);
            const float medErr = cents (outMedian, 261.63f), srcMedErr = cents (srcMedian, base);
            const bool vibOk = std::abs (detErr) < 5.0f && std::abs (medErr - srcMedErr) < 3.0f && std::abs (excOut - excSrc) < 0.25f * excSrc;
            if (! vibOk) ++failures;
            std::cout << (vibOk ? "ok   " : "FAIL ") << "Tuning C/E, harmonic tone with vibrato: detected " << juce::String (detErr, 1) << " c off; Tune 100% median pitch "
                      << juce::String (medErr, 1) << " c from C4 (source measured " << juce::String (srcMedErr, 1) << " c from its nominal), vibrato excursion "
                      << juce::String (excOut, 0) << " c vs source " << juce::String (excSrc, 0) << " c\n";

            auto steady = makeTone (2.5, harm (0.0f));
            auto ds = ResynthData::fromAudio (steady, sr, "steady");
            bool ok = true;
            juce::String rep;
            float exc = 0.0f;
            // the nearest note of the key's pitch class, moved by the key's octave from middle C: C4 puts the G3 tone on
            // C4, C2 on C2, C5 on C5; G4 on G3, G3 on G2
            for (int note : { 36, 55, 60, 67, 72 })
            {
                const float target = nearest (base, note);
                const float err = cents (pitchTrack (renderTuned (ds, note, 100.0f, 2.0), target * 0.7f, target * 1.4f, exc), target);
                ok &= std::abs (err) < 2.0f;
                rep << "key " << note << " -> " << juce::String (target, 1) << " Hz " << juce::String (err, 1) << " c; ";
            }
            // Tune 50%: half the cents; Fine +25 c on top
            const float half = std::sqrt (base * nearest (base, 60));
            const float errHalf = cents (pitchTrack (renderTuned (ds, 60, 50.0f, 2.0), half * 0.7f, half * 1.4f, exc), half);
            setP (q, "o1_fine", 25.0f);
            const float fineTarget = 261.63f * std::exp2 (25.0f / 1200.0f);
            const float errFine = cents (pitchTrack (renderTuned (ds, 60, 100.0f, 2.0), fineTarget * 0.7f, fineTarget * 1.4f, exc), fineTarget);
            setP (q, "o1_fine", 0.0f);
            ok &= std::abs (errHalf) < 2.0f && std::abs (errFine) < 2.0f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning G, Tune 100% at other keys: " << rep << "Tune 50% " << juce::String (errHalf, 1) << " c; Fine +25 c " << juce::String (errFine, 1) << " c\n";
        }

        // Test D: inharmonic bell (partials at 1, 2.756, 5.404, 8.933 x 200 Hz). Whatever pitch is chosen, Tune 100%
        // keeps the partial ratios (no snapping to a harmonic series).
        {
            const float ratios[] { 1.0f, 2.756f, 5.404f, 8.933f };
            auto d = ResynthData::fromAudio (makeTone (2.5, [&] (float t)
            {
                float v = 0.0f;
                for (int i = 0; i < 4; ++i) v += 0.2f / (float) (i + 1) * std::sin (juce::MathConstants<float>::twoPi * 200.0f * ratios[i] * t);
                return v;
            }), sr, "bell");
            auto out = renderTuned (d, 60, 100.0f, 2.0);
            const float ratio = hzOf (ResynthData::tuneTarget (d->firstNoteMidi, 60.0f)) / hzOf (d->firstNoteMidi);   // the note's shift
            const float f1 = measure (out, 0, 200.0f * ratio).second;
            float worst = 0.0f;
            for (int i = 1; i < 4; ++i) worst = std::max (worst, std::abs (cents (measure (out, 0, 200.0f * ratios[i] * ratio).second / f1, ratios[i])));
            const bool ok = worst < 3.0f && std::abs (cents (f1, 200.0f * ratio)) < 2.0f && d->numNoteEvents == 1;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning D, inharmonic bell: pitch taken as " << juce::String (d->detectedFundamentalHz, 1) << " Hz, Tune 100% shifts "
                      << juce::String (12.0f * std::log2 (ratio), 2) << " st, partial ratios kept within " << juce::String (worst, 2) << " c\n";
        }

        // Test F: transients. Tune 100% on the plucked string: onsets at the same time and level as Tune 0%.
        {
            juce::AudioBuffer<float> pluck;
            for (auto& s : ResynthEval::generatedSources()) if (s.name == "pluck") pluck = s.audio;
            auto d = ResynthData::fromAudio (pluck, sr, "pluck");
            auto ref = renderTuned (d, 60, 0.0f, 2.5), out = renderTuned (d, 60, 100.0f, 2.5);
            auto onsetAt = [&] (const juce::AudioBuffer<float>& b, int from)
            {
                const float thr = 0.1f * b.getMagnitude (from, (int) (sr * 0.2));
                for (int i = from; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > thr) return i;
                return -1;
            };
            float worstMs = 0.0f, worstDb = 0.0f;
            const int win = (int) (sr * 0.015);
            for (int n = 0; n < 3; ++n)
            {
                const int at = (int) (n * sr) - (n > 0 ? win : 0);
                worstMs = std::max (worstMs, std::abs ((float) (onsetAt (out, at) - onsetAt (ref, at)) * 1000.0f / (float) sr));
                worstDb = std::max (worstDb, std::abs (juce::Decibels::gainToDecibels (out.getRMSLevel (0, (int) (n * sr), win) / ref.getRMSLevel (0, (int) (n * sr), win), -120.0f)));
            }
            const float shiftSt = ResynthData::tuneTarget (d->firstNoteMidi, 60.0f) - d->firstNoteMidi;
            const bool ok = worstMs < 1.0f && worstDb < 3.0f && shiftSt != 0.0f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning F, pluck attacks with Tune 100% (" << juce::String (shiftSt, 2) << " st): onset timing within "
                      << juce::String (worstMs, 2) << " ms, first 15 ms level within " << juce::String (worstDb, 2) << " dB of Tune 0%\n";
        }

        // Notes per segment of an output: median autocorrelation pitch in the middle of each `segSec` segment
        auto segmentPitch = [&] (const juce::AudioBuffer<float>& b, int seg, double segSec, float loHz, float hiHz)
        {
            std::vector<float> f;
            const int len = 2048, from = (int) ((seg + 0.25) * segSec * sr), to = (int) ((seg + 0.85) * segSec * sr);
            for (int st = from; st + len + (int) (sr / loHz) + 2 < to; st += 1024) f.push_back (acfPitch (b, st, len, loHz, hiHz));
            std::sort (f.begin(), f.end());
            return f[f.size() / 2];
        };
        auto harmonicNote = [] (float hz, float t, float gainAmp = 0.25f)
        {
            float v = 0.0f;
            for (int h = 1; h <= 8; ++h) v += gainAmp / (float) h * std::sin (juce::MathConstants<float>::twoPi * hz * (float) h * t);
            return v;
        };

        // Test I: a melody of separate notes (C#3 D#3 F3 G3 A3 B3, 0.4 s each). Key C puts each on its nearest C
        // (C3 C3 C3 C4 C4 C4), key D on its nearest D (D3 D3 D3 D3 D4 D4). Each note measured on its own.
        {
            const float melody[] { 138.59f, 155.56f, 174.61f, 196.0f, 220.0f, 246.94f };
            const double segSec = 0.4;
            auto src = makeTone (6 * segSec + 0.1, [&] (float t)
            {
                const int seg = juce::jlimit (0, 5, (int) (t / segSec));
                const float local = t - (float) seg * (float) segSec;
                const float env = juce::jmin (1.0f, local * 200.0f) * juce::jmin (1.0f, ((float) segSec - local) * 200.0f);   // 5 ms edges
                return env * harmonicNote (melody[seg], local);
            });
            auto d = ResynthData::fromAudio (src, sr, "melody");
            bool ok = d->numNoteEvents >= 6 && d->numNoteEvents <= 9   // a few short ones may appear at the 5 ms edges
                      && std::abs (d->firstNoteMidi - 49.0f) < 0.5f;
            juce::String rep;
            for (int key : { 60, 62 })
            {
                auto out = renderTuned (d, key, 100.0f, 6 * segSec);
                rep << "key " << (key == 60 ? "C" : "D") << ":";
                for (int seg = 0; seg < 6; ++seg)
                {
                    const float target = nearest (melody[seg], key);
                    const float err = cents (segmentPitch (out, seg, segSec, 100.0f, 600.0f), target);
                    ok &= std::abs (err) < 2.0f;
                    rep << " " << juce::String (target, 0) << "Hz " << juce::String (err, 1) << "c";
                }
                rep << "; ";
            }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning I, melody (" << d->numNoteEvents << " note events found, first note MIDI "
                      << juce::String (d->firstNoteMidi, 2) << "): " << rep << "\n";
        }

        // Test J: chord progression (two-note chords E3+G#3, F3+A3, D3+B3, 0.5 s each). Key C moves each note of every
        // chord to its own nearest C (the lower notes to C3, the upper to C4): different amounts within one chord.
        {
            struct Chord { float lo, hi; };
            const Chord chords[] { { 164.81f, 207.65f }, { 174.61f, 220.0f }, { 146.83f, 246.94f } };
            const double segSec = 0.5;
            auto src = makeTone (3 * segSec + 0.1, [&] (float t)
            {
                const int seg = juce::jlimit (0, 2, (int) (t / segSec));
                const float local = t - (float) seg * (float) segSec;
                const float env = juce::jmin (1.0f, local * 200.0f) * juce::jmin (1.0f, ((float) segSec - local) * 200.0f);
                return env * (harmonicNote (chords[seg].lo, local, 0.2f) + harmonicNote (chords[seg].hi, local, 0.2f));
            });
            auto d = ResynthData::fromAudio (src, sr, "chords");
            auto out = renderTuned (d, 60, 100.0f, 3 * segSec + 0.1);
            bool ok = d->numNoteEvents >= 6 && std::abs (d->firstNoteMidi - 52.0f) < 0.5f;
            juce::String rep;
            // spectrum of the steady part of each chord (0.2 - 0.39 s in; the analysis blurs the chord change itself
            // for ~0.1 s either side): both targets present, both original fundamentals gone
            auto peakIn = [&] (const juce::AudioBuffer<float>& b, int seg, float hz)
            {
                const int N = 1 << 13, start = (int) ((seg + 0.2) * segSec * sr);
                juce::dsp::FFT fft (13);
                std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
                juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
                for (int i = 0; i < N; ++i) x[(size_t) i] = b.getSample (0, start + i) * w[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (x.data());
                const float binHz = (float) sr / N;
                float best = 0.0f, bestHz = 0.0f;
                for (int k = (int) (hz * 0.985f / binHz); k <= (int) (hz * 1.015f / binHz); ++k)
                    if (x[(size_t) k] > best)
                    {
                        const float a = std::log (x[(size_t) k - 1] + 1e-12f), bb = std::log (x[(size_t) k] + 1e-12f), c = std::log (x[(size_t) k + 1] + 1e-12f);
                        const float dd = a - 2.0f * bb + c;
                        best = x[(size_t) k]; bestHz = ((float) k + (std::abs (dd) > 1e-9f ? 0.5f * (a - c) / dd : 0.0f)) * binHz;
                    }
                return std::make_pair (juce::Decibels::gainToDecibels (best * 4.0f / N, -120.0f), bestHz);
            };
            for (int seg = 0; seg < 3; ++seg)
            {
                for (float hz : { chords[seg].lo, chords[seg].hi })
                {
                    const float target = nearest (hz, 60);
                    const auto [db, f] = peakIn (out, seg, target);
                    const float err = cents (f, target);
                    // what remains at the original pitch: the analysis blurs each chord change for ~0.1 s, and those
                    // gliding partials carry one note's correction, so faint remains (> 18 dB down) are expected.
                    // Both notes of each chord land on the C3 series (one at C3, one at C4), so every upper-note
                    // partial coincides with a lower-note partial and the two interfere; a target may measure 20 dB
                    // down when the pair happens to cancel there, so its presence is only checked loosely.
                    const auto [left, leftHz] = peakIn (out, seg, hz);
                    ok &= std::abs (err) < 2.0f && db > -45.0f && left < db - 18.0f;
                    rep << juce::String (hz, 0) << "->" << juce::String (target, 0) << "Hz " << juce::String (err, 1) << "c (" << juce::String (db, 0) << " dB, left " << juce::String (left, 0) << " dB at " << juce::String (leftHz, 1) << "); ";
                }
            }
            // (timing is checked on the plucked string in test F: Tune never touches amplitudes or their timing)
            const int flips = shortTagSegments (*d);
            ok &= flips == 0;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning J, chord progression (" << d->numNoteEvents << " note events, " << flips << " short tag flips): " << rep << "\n";
        }

        // Test K: overlapping notes. A3 (220 Hz) sustains 0-1.5 s; E4 (329.6 Hz) joins at 0.75 s. Key C puts both on
        // their nearest C, C4 (261.6 Hz): A3 moves up 3 semitones, E4 down 4 while it overlaps A3.
        // Measured: before the overlap a clean C4; during the overlap nothing left at E4's own pitches.
        {
            auto src = makeTone (1.6, [&] (float t)
            {
                float v = harmonicNote (220.0f, t, 0.2f);
                if (t >= 0.75f) v += juce::jmin (1.0f, (t - 0.75f) * 200.0f) * harmonicNote (329.63f, t - 0.75f, 0.2f);
                return v;
            });
            auto d = ResynthData::fromAudio (src, sr, "overlap");
            auto out = renderTuned (d, 60, 100.0f, 1.6);
            float exc = 0.0f;
            // first half: a clean C4 by autocorrelation
            juce::AudioBuffer<float> head (2, (int) (sr * 0.7)); head.clear(); head.copyFrom (0, 0, out, 0, 0, head.getNumSamples());
            juce::AudioBuffer<float> padded (2, (int) (sr * 1.0)); padded.clear(); padded.copyFrom (0, (int) (sr * 0.3), head, 0, 0, head.getNumSamples());
            const float headErr = cents (pitchTrack (padded, 150.0f, 500.0f, exc), 261.63f);
            // second half: E4's fundamental and 3rd harmonic (329.6, 988.9 Hz) gone; C4 (the target) there.
            // (E4's 2nd harmonic, 659.3 Hz, sits next to the target's 5th, 654.1 Hz.)
            auto peakAt = [&] (float hz)
            {
                const int N = 1 << 14, start = (int) (sr * 1.1);
                juce::dsp::FFT fft (14);
                std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
                juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
                for (int i = 0; i < N && start + i < out.getNumSamples(); ++i) x[(size_t) i] = out.getSample (0, start + i) * w[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (x.data());
                const float binHz = (float) sr / N;
                float best = 0.0f;
                for (int k = (int) (hz * 0.985f / binHz); k <= (int) (hz * 1.015f / binHz); ++k) best = std::max (best, x[(size_t) k]);
                return juce::Decibels::gainToDecibels (best * 4.0f / N, -120.0f);
            };
            const float atC4 = peakAt (261.63f), atB5 = peakAt (988.88f), atE4 = peakAt (329.63f);
            const int flips = shortTagSegments (*d);
            const bool ok = std::abs (headErr) < 2.0f && atC4 > -30.0f && atB5 < atC4 - 25.0f && atE4 < -45.0f && d->numNoteEvents >= 2 && d->numNoteEvents <= 4
                            && std::abs (d->firstNoteMidi - 57.0f) < 0.5f && flips == 0;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning K, overlapping notes (" << d->numNoteEvents << " events, " << flips << " short tag flips): A3 alone -> C4 " << juce::String (headErr, 1)
                      << " c; with E4 joining: level at C4 " << juce::String (atC4, 1) << " dB, left at B5 (E4's 3rd harmonic) " << juce::String (atB5, 1) << " dB, at E4 " << juce::String (atE4, 1) << " dB\n";
        }

        // Test L: octave boundaries. Each note goes to its nearest C: B2 +40 c -> C3 (60 c away, C2 is 1140); C3 -40 c
        // -> C3; F#3 (exactly halfway between two Cs) goes up -> C4; a vibrato of +-30 c around F#3 +10 c -> C4 with the
        // vibrato kept and no flipping between the octaves.
        {
            bool ok = true;
            juce::String rep;
            struct Case { const char* name; float midi; float expected; };
            for (auto c : { Case { "B2 +40 c", 47.4f, 130.81f }, Case { "C3 -40 c", 47.6f, 130.81f }, Case { "F#3", 54.0f, 261.63f } })
            {
                const float hz = hzOf (c.midi);
                auto d = ResynthData::fromAudio (makeTone (2.0, [&] (float t) { return harmonicNote (hz, t); }), sr, "boundary");
                float exc = 0.0f;
                const float f = pitchTrack (renderTuned (d, 60, 100.0f, 2.0), c.expected * 0.7f, c.expected * 1.4f, exc);
                ok &= std::abs (cents (f, c.expected)) < 2.0f && exc < 3.0f;
                rep << c.name << " -> " << juce::String (f, 1) << " Hz (" << juce::String (cents (f, c.expected), 1) << " c from " << (int) c.expected << "), wobble " << juce::String (exc, 1) << " c; ";
            }
            {
                const float base = 184.997f * std::exp2 (10.0f / 1200.0f);
                auto d = ResynthData::fromAudio (makeTone (2.0, [&] (float t)
                {
                    const float phs = base * t - base * 30.0f / 1200.0f * std::log (2.0f) * std::cos (juce::MathConstants<float>::twoPi * 5.5f * t) / (juce::MathConstants<float>::twoPi * 5.5f);
                    float v = 0.0f;
                    for (int h = 1; h <= 8; ++h) v += 0.25f / (float) h * std::sin (juce::MathConstants<float>::twoPi * (float) h * phs);
                    return v;
                }), sr, "boundary vibrato");
                float exc = 0.0f;
                const float f = pitchTrack (renderTuned (d, 60, 100.0f, 2.0), 150.0f, 500.0f, exc);
                ok &= std::abs (cents (f, 261.63f)) < 3.0f && exc > 40.0f && exc < 75.0f && d->numNoteEvents == 1;
                rep << "F#3 vibrato -> " << juce::String (f, 1) << " Hz, excursion " << juce::String (exc, 0) << " c, " << d->numNoteEvents << " event";
            }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning L, octave boundaries (nearest C, halfway goes up): " << rep << "\n";
        }

        // Test M: sources whose notes are D2 then A3, G4 then C3, and A3 then E5 (plus a two-note chord at the end).
        // Tune 100%: each note lands on the nearest note of the key's pitch class in its own register (D2 with C4 ->
        // C2 and A3 -> C4; G4 with D4 -> D4 and C3 -> D3; A3 with F4 -> F3 and E5 -> F5), and that many octaves away
        // for the same key in another octave (C5, D3, F6). Tune 0% at middle C: unchanged; at D4: up 2 semitones.
        {
            // keyA is in middle C's octave; keyB is the same pitch class in another octave
            struct Src { const char* name; float first, second, chordLo, chordHi; int keyA, keyB; float firstMidi; float targetA[2]; };
            const Src srcs[] { { "D2", hzOf (38), hzOf (57), hzOf (40), hzOf (48), 60, 72, 38.0f, { hzOf (36), hzOf (60) } },
                               { "G4", hzOf (67), hzOf (48), hzOf (65), hzOf (71), 62, 50, 67.0f, { hzOf (62), hzOf (50) } },
                               { "A3", hzOf (57), hzOf (76), hzOf (36), hzOf (55), 65, 77, 57.0f, { hzOf (53), hzOf (77) } } };
            const double segSec = 0.5;
            bool ok = true;
            juce::String rep;
            for (auto& sc : srcs)
            {
                auto src = makeTone (3 * segSec + 0.1, [&] (float t)
                {
                    const int seg = juce::jlimit (0, 2, (int) (t / segSec));
                    const float local = t - (float) seg * (float) segSec;
                    const float env = juce::jmin (1.0f, local * 200.0f) * juce::jmin (1.0f, ((float) segSec - local) * 200.0f);
                    if (seg == 0) return env * harmonicNote (sc.first, local);
                    if (seg == 1) return env * harmonicNote (sc.second, local);
                    return env * (harmonicNote (sc.chordLo, local, 0.2f) + harmonicNote (sc.chordHi, local, 0.2f));
                });
                auto d = ResynthData::fromAudio (src, sr, sc.name);
                ok &= std::abs (d->firstNoteMidi - sc.firstMidi) < 0.5f;
                rep << sc.name << " (first note MIDI " << juce::String (d->firstNoteMidi, 2) << "): ";
                const float octaves = (float) (sc.keyB - sc.keyA) / 12.0f;
                float seen[2][2] {};
                for (int k = 0; k < 2; ++k)
                {
                    const int key = k == 0 ? sc.keyA : sc.keyB;
                    auto out = renderTuned (d, key, 100.0f, 3 * segSec);
                    for (int seg = 0; seg < 2; ++seg)   // the single-note segments (chords are measured in test J)
                    {
                        const float target = sc.targetA[seg] * std::exp2 (k == 0 ? 0.0f : octaves);
                        ok &= std::abs (cents (nearest (seg == 0 ? sc.first : sc.second, key), target)) < 0.1f;
                        seen[k][seg] = segmentPitch (out, seg, segSec, target * 0.7f, target * 1.4f);
                        ok &= std::abs (cents (seen[k][seg], target)) < 2.0f;
                        rep << "key " << key << ": " << juce::String (seg == 0 ? sc.first : sc.second, 0) << " -> " << juce::String (target, 1) << " Hz "
                            << juce::String (cents (seen[k][seg], target), 1) << " c; ";
                    }
                }
                float keyDiff = 0.0f;   // the two keys exactly the octaves apart they should be
                for (int seg = 0; seg < 2; ++seg)
                    keyDiff = std::max (keyDiff, std::abs (cents (seen[1][seg], seen[0][seg]) - 1200.0f * octaves));
                ok &= keyDiff < 1.0f;
                // Tune 0%: middle C plays the original, D4 two semitones up (single-note segments)
                auto plain = renderTuned (d, 60, 0.0f, 3 * segSec), up = renderTuned (d, 62, 0.0f, 3 * segSec);
                float plainErr = 0.0f, upErr = 0.0f;
                for (int seg = 0; seg < 2; ++seg)
                {
                    const float hz = seg == 0 ? sc.first : sc.second, hzUp = hz * std::exp2 (2.0f / 12.0f);
                    plainErr = std::max (plainErr, std::abs (cents (segmentPitch (plain, seg, segSec, hz * 0.7f, hz * 1.4f), hz)));
                    upErr = std::max (upErr, std::abs (cents (segmentPitch (up, seg, segSec, hzUp * 0.7f, hzUp * 1.4f), hzUp)));
                }
                ok &= plainErr < 2.0f && upErr < 2.0f;
                rep << "octave step off by " << juce::String (keyDiff, 2) << " c; Tune 0%: unchanged within " << juce::String (plainErr, 1)
                    << " c, key D4 +2 st within " << juce::String (upErr, 1) << " c; ";
            }
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning M, every note to the nearest note of the key's pitch class, moved by the key's octave: " << rep << "\n";
        }

        // Test H: Tune swept from 0 to 100% during a note: no clicks, finite output; pitch ends on target.
        {
            auto d = ResynthData::fromAudio (makeTone (2.5, [] (float t)
            {
                float v = 0.0f;
                for (int h = 1; h <= 8; ++h) v += 0.25f / (float) h * std::sin (juce::MathConstants<float>::twoPi * 233.0f * (float) h * t);
                return v;
            }), sr, "sweep");
            q.setResynth (d);
            setP (q, "rs_tune", 0.0f);
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> out (2, (int) (sr * 2.0) / block * block), buf (2, block);
            out.clear();
            for (int b = 0; b < out.getNumSamples() / block; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
                const double t = (double) b * block / sr;
                setP (q, "rs_tune", (float) juce::jlimit (0.0, 100.0, (t - 0.5) * 200.0));   // 0 -> 100% between 0.5 s and 1.0 s
                q.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
            }
            bool finite = true;
            for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i));
            const auto rep = detectClicks (out, 0.05f, 1000);
            float exc = 0.0f;
            const int tail = (int) (sr * 1.2);
            juce::AudioBuffer<float> end (2, out.getNumSamples() - tail + (int) (sr * 0.3));
            end.clear();
            end.copyFrom (0, (int) (sr * 0.3), out, 0, tail, out.getNumSamples() - tail);
            const float endErr = cents (pitchTrack (end, 150.0f, 500.0f, exc), 261.63f);   // 233 Hz (A#3) -> C4
            const bool ok = finite && rep.clicks == 0 && std::abs (endErr) < 2.0f;
            if (! ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << "Tuning H, Tune swept 0 -> 100% mid-note: clicks " << rep.clicks << ", worst second difference " << juce::String (rep.worst, 4)
                      << ", pitch after the sweep " << juce::String (endErr, 1) << " c from C3\n";
            setP (q, "rs_tune", 0.0f);
        }
    }

    // ---- Prism and Shimmer ----
    {
        auto sawNote = [&] (std::function<void (ElukkaAudioProcessor&)> setup, int note = 48)
        {
            ElukkaAudioProcessor q;
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
            setP (q, "o1_wave", (float) P::WSaw);
            setup (q);
            return renderNotes (q, { note }, 2.0);
        };
        const float f0 = 130.81f;
        auto clean = sawNote ([] (ElukkaAudioProcessor&) {});
        auto prismed = sawNote ([&] (ElukkaAudioProcessor& q) { setP (q, "hm_prism", 100.0f); setP (q, "hm_prismCenter", 8.0f); setP (q, "hm_prismWidth", 2.0f); });
        const float fundDb = juce::Decibels::gainToDecibels (measure (prismed, 0, f0).first / measure (clean, 0, f0).first);
        auto exactBin = [&] (const juce::AudioBuffer<float>& b2, float hz)   // level at exactly hz (+-0.3%)
        {
            const int order = 15, N = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
            for (int i = 0; i < N; ++i) x[(size_t) i] = b2.getSample (0, (int) (0.5 * sr) + i) * w[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (x.data());
            const float binHz = (float) sr / N;
            float best = 0.0f;
            for (int k = (int) (hz * 0.997f / binHz); k <= (int) (hz * 1.003f / binHz) + 1; ++k) best = std::max (best, x[(size_t) k]);
            return best;
        };
        int moved = 0;
        for (int hn = 6; hn <= 11; ++hn)
            if (exactBin (prismed, f0 * (float) hn) < exactBin (clean, f0 * (float) hn) * 0.1f) ++moved;
        const float h2Db = juce::Decibels::gainToDecibels (exactBin (prismed, 2.0f * f0) / exactBin (clean, 2.0f * f0));
        const bool prismOk = std::abs (fundDb) < 0.1f && moved >= 5 && std::abs (h2Db) < 0.5f;
        if (! prismOk) ++failures;
        std::cout << (prismOk ? "ok   " : "FAIL ") << "Prism 100% around harmonic 8: fundamental " << juce::String (fundDb, 2) << " dB, harmonic 2 "
                  << juce::String (h2Db, 2) << " dB (outside the band), " << moved << " of harmonics 6-11 moved off their place\n";

        auto shim = sawNote ([&] (ElukkaAudioProcessor& q) { setP (q, "hm_shimmer", 100.0f); setP (q, "hm_shimmerRate", 2.0f); });
        const float shFund = juce::Decibels::gainToDecibels (exactBin (shim, f0) / exactBin (clean, f0));
        // the drift is random, so look at several harmonics: most must have left their exact pitch
        int drifting = 0;
        for (int hn = 3; hn <= 12; ++hn)
            if (exactBin (shim, f0 * (float) hn) < exactBin (clean, f0 * (float) hn) * 0.5f) ++drifting;
        const bool shimOk = std::abs (shFund) < 0.2f && drifting >= 7;
        if (! shimOk) ++failures;
        std::cout << (shimOk ? "ok   " : "FAIL ") << "Shimmer 100%: fundamental " << juce::String (shFund, 2) << " dB (stays), "
                  << drifting << " of harmonics 3-12 drifting off their exact pitch\n";

        // Handover: turning Prism on mid-note (tiny amount, so nothing should change) must sound identical
        auto handover = [&] (float stretchAmt, bool engage)
        {
            ElukkaAudioProcessor q;
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "master", 0.0f); setP (q, "ampS", 1.0f); setP (q, "o1_uniVoices", 3.0f); setP (q, "o1_uniDetune", 30.0f);
            setP (q, "o1_pan", 30.0f); setP (q, "o1_bassMono", 1.0f); setP (q, "hm_stretch", stretchAmt);
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> out (2, block * 120), buf (2, block);
            for (int b = 0; b < 120; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
                if (b == 60 && engage) setP (q, "hm_prism", 0.1f);   // 0.1% at harmonic 8: a shift of ~0.07 Hz
                q.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
            }
            return out;
        };
        // (after the switch the two versions slowly drift apart in phase through float rounding, and with Stretch the
        // sine bank puts partials exactly where the wavetable could only approximate them, so compare the switch
        // itself and the harmonic levels afterwards)
        for (float st : { 0.0f, 0.15f })
        {
            const int blocks = (int) (2.0 * sr) / block;
            auto run = [&] (bool engage)
            {
                ElukkaAudioProcessor q;
                q.setPlayConfigDetails (0, 2, sr, block);
                setP (q, "master", 0.0f); setP (q, "ampS", 1.0f); setP (q, "o1_uniVoices", 3.0f); setP (q, "o1_uniDetune", 30.0f);
                setP (q, "o1_pan", 30.0f); setP (q, "o1_bassMono", 1.0f); setP (q, "hm_stretch", st);
                q.prepareToPlay (sr, block);
                juce::AudioBuffer<float> out (2, block * blocks), buf (2, block);
                for (int b2 = 0; b2 < blocks; ++b2)
                {
                    juce::MidiBuffer midi;
                    if (b2 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
                    if (b2 == 60 && engage) setP (q, "hm_prism", 0.1f);   // 0.1% at harmonic 8: a shift of ~0.03 Hz
                    q.processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b2 * block, buf, ch, 0, block);
                }
                return out;
            };
            auto ref = run (false), sw = run (true);
            float diff = 0.0f, peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 60 * block; i < 61 * block; ++i)
                {
                    diff = std::max (diff, std::abs (ref.getSample (ch, i) - sw.getSample (ch, i)));
                    peak = std::max (peak, std::abs (ref.getSample (ch, i)));
                }
            // energy in a +-4% band around each harmonic (independent of how the detuned unison copies beat, and wide
            // enough for the wavetable's 1/8-harmonic rounding of stretched partials)
            auto bandEnergy = [&] (const juce::AudioBuffer<float>& b2, int ch, float hz)
            {
                const int order = 15, N = 1 << order;
                juce::dsp::FFT fft (order);
                std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
                juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
                for (int i = 0; i < N; ++i) x[(size_t) i] = b2.getSample (ch, (int) (0.5 * sr) + i) * w[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (x.data());
                const float binHz = (float) sr / N;
                double e = 0.0;
                for (int k = (int) (hz * 0.96f / binHz); k <= (int) (hz * 1.04f / binHz); ++k) e += (double) x[(size_t) k] * x[(size_t) k];
                return (float) std::sqrt (e);
            };
            float worstLevel = 0.0f;
            for (int hn = 2; hn <= 12; ++hn)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float hz = 65.41f * (float) hn * std::pow ((float) hn, st * 0.25f);
                    worstLevel = std::max (worstLevel, std::abs (juce::Decibels::gainToDecibels (bandEnergy (sw, ch, hz) / bandEnergy (ref, ch, hz))));
                }
            const float switchDb = juce::Decibels::gainToDecibels (diff / peak, -200.0f);
            const bool hoOk = (st != 0.0f || switchDb < -50.0f) && worstLevel < 0.2f;
            if (! hoOk) ++failures;
            std::cout << (hoOk ? "ok   " : "FAIL ") << "Prism switching on mid-note (stretch " << st << ", unison 3, pan, bass mono): "
                      << (st == 0.0f ? "difference in the switching block " + juce::String (switchDb, 1) + " dB, " : juce::String())
                      << "harmonics 2-12 afterwards within " << juce::String (worstLevel, 2) << " dB\n";
        }
    }

    // CPU of Prism/Shimmer (information): 8-note chord of 7-voice unison saws
    {
        auto chordCpu = [&] (int mangle)
        {
            ElukkaAudioProcessor q;
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "master", 0.0f); setP (q, "ampS", 1.0f); setP (q, "o1_uniVoices", 7.0f);
            if (mangle >= 1) setP (q, "hm_prism", 50.0f);   // default band: 2 octaves around harmonic 8
            if (mangle >= 2) setP (q, "hm_shimmer", 30.0f);
            double cpu = 0.0;
            renderNotes (q, { 36, 43, 48, 52, 55, 60, 64, 67 }, 3.0, &cpu);
            return cpu;
        };
        const double off = chordCpu (0), pr = chordCpu (1), both = chordCpu (2);
        std::cout << "info CPU, 8 notes x 7-voice unison saw: " << juce::String (off * 100.0, 1) << "% of real time, Prism "
                  << juce::String (pr * 100.0, 1) << "%, Prism + Shimmer " << juce::String (both * 100.0, 1) << "%\n";
    }

    // ---- Synced LFOs follow the song position (Retrig off) ----
    {
        auto lfoAt = [&] (bool withNote, bool retrig, std::vector<double> jumps, std::vector<float>& values)
        {
            ElukkaAudioProcessor q;
            FakePlayHead host;
            host.bpm = 140.0; host.playing = true;
            q.setPlayHead (&host);
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "master", 0.0f); setP (q, "ampS", 1.0f); setP (q, "m1_smooth", 0.0f);
            setP (q, "m1_mode", 0.0f); setP (q, "m1_sync", 1.0f); setP (q, "m1_syncRate", (float) P::syncRateIndex (16.0f));   // 4 bars
            setP (q, "m1_retrig", retrig ? 1.0f : 0.0f);
            q.shapes[0].setPoints (ModShape::preset (3));   // saw up: value = position in the 4 bars
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block);
            for (double startPpq : jumps)
            {
                host.ppq = startPpq;
                for (int b = 0; b < 4; ++b)
                {
                    juce::MidiBuffer midi;
                    if (withNote && b == 0 && startPpq == jumps.front()) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                    q.processBlock (buf, midi);
                    host.ppq += block / sr * host.bpm / 60.0;
                }
                values.push_back (withNote ? q.uiMod[0].load() : q.globalModValue[0].load());   // after 4 blocks (~0.03 beats)
            }
            q.setPlayHead (nullptr);
        };
        // play from bar 1, jump to bar 3 (beat 8), to bar 2 beat 3 (beat 6), back to bar 1
        const std::vector<double> jumps { 0.0, 8.0, 6.0, 0.0 };
        const float expect[] { 0.0f, 0.5f, 0.375f, 0.0f };
        for (bool withNote : { false, true })
        {
            std::vector<float> got;
            lfoAt (withNote, false, jumps, got);
            float worst = 0.0f;
            for (size_t k = 0; k < jumps.size(); ++k) worst = std::max (worst, std::abs (got[k] - expect[k]));
            const bool lockOk = worst < 0.01f;
            if (! lockOk) ++failures;
            std::cout << (lockOk ? "ok   " : "FAIL ") << "4-bar LFO follows the song (" << (withNote ? "held note" : "no note, FX modulation")
                      << "): bar 1 " << juce::String (got[0], 3) << ", jump to bar 3 " << juce::String (got[1], 3)
                      << ", bar 2 beat 3 " << juce::String (got[2], 3) << ", back to bar 1 " << juce::String (got[3], 3) << "\n";
        }
        std::vector<float> trig;
        lfoAt (true, true, { 8.0 }, trig);
        const bool trigOk = trig[0] < 0.01f;
        if (! trigOk) ++failures;
        std::cout << (trigOk ? "ok   " : "FAIL ") << "Retrig on: a note at bar 3 restarts the LFO (" << juce::String (trig[0], 3) << ")\n";
    }

    // ---- Release tails with Sustain 0, synced rate knobs ----
    {
        // Envelope updated every block (as the voice does): releasing during the decay with Sustain 0 must
        // fall from where it was to zero over the Release time.
        Envelope env;
        env.setSampleRate (sr);
        Envelope::Parameters ep { 0.001f, 2.0f, 0.0f, 0.5f };
        env.setParameters (ep);
        env.noteOn();
        float atOff = 0.0f;
        int releaseSamples = 0;
        for (int i = 0; i < (int) (sr * 2); ++i)
        {
            if (i % block == 0) env.setParameters (ep);
            if (i == (int) (sr * 0.2)) { atOff = env.getNextSample(); env.noteOff(); continue; }
            const float v = env.getNextSample();
            if (i > (int) (sr * 0.2) && env.isActive()) ++releaseSamples;
            (void) v;
        }
        const float relSec = (float) releaseSamples / (float) sr;
        const bool envOk = atOff > 0.8f && std::abs (relSec - 0.5f) < 0.01f;
        if (! envOk) ++failures;
        std::cout << (envOk ? "ok   " : "FAIL ") << "Sustain 0: released at level " << juce::String (atOff, 2) << " -> tail of "
                  << juce::String (relSec, 3) << " s (Release 0.5 s)\n";

        // The same through the synth: note off at 0.2 s, the sound is still there 0.3 s later
        ElukkaAudioProcessor q;
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "o1_wave", (float) P::WSine); setP (q, "master", 0.0f);
        setP (q, "ampA", 0.001f); setP (q, "ampD", 2.0f); setP (q, "ampS", 0.0f); setP (q, "ampR", 1.0f);
        q.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        float before = 0.0f, after = 0.0f, end = 0.0f;
        for (int b = 0; b < (int) (sr * 1.6) / block; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            if (b == (int) (sr * 0.2) / block) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            q.processBlock (buf, midi);
            const float m = buf.getMagnitude (0, 0, block);
            const double t = (double) b * block / sr;
            if (t > 0.15 && t < 0.19) before = juce::jmax (before, m);
            if (t > 0.5 && t < 0.52) after = juce::jmax (after, m);
            if (t > 1.3) end = juce::jmax (end, m);
        }
        const bool tailOk = after > before * 0.6f && after < before * 0.8f && end == 0.0f;
        if (! tailOk) ++failures;
        std::cout << (tailOk ? "ok   " : "FAIL ") << "Amp release with Sustain 0: level " << juce::String (before, 3) << " at note off, "
                  << juce::String (after, 3) << " 0.3 s later (Release 1 s), silent after the tail\n";

        // Synced LFO rate knob: dotted values, and modulation (Macro 1 -> Mod 1 Sync Rate)
        auto lfoCycles = [&] (float beats, float macroAmt)
        {
            ElukkaAudioProcessor m;
            m.setPlayConfigDetails (0, 2, sr, block);
            setP (m, "master", 0.0f); setP (m, "ampS", 1.0f);
            setP (m, "m1_mode", 0.0f); setP (m, "m1_sync", 1.0f); setP (m, "m1_syncRate", (float) P::syncRateIndex (beats));
            m.shapes[0].setPoints (ModShape::preset (3));   // saw up: count the wraps
            setP (m, "macro1", 1.0f);
            if (macroAmt != 0.0f) { setP (m, "mx1_src", (float) P::SrcMacro1); setP (m, "mx1_dst", (float) P::DstM1SyncRate); setP (m, "mx1_amt", macroAmt); }
            m.prepareToPlay (sr, block);
            juce::AudioBuffer<float> b2 (2, block);
            int wraps = 0;
            float last = 0.0f;
            for (int b = 0; b < (int) (sr * 3.1) / block; ++b)   // a little over 3 s so the wrap at 3.0 s counts
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                m.processBlock (b2, midi);
                const float v = m.uiMod[0].load();
                if (v < last - 0.5f) ++wraps;
                last = v;
            }
            return wraps;
        };
        // 120 BPM over 3 s: 1/4 = 6 cycles, 1/4 D = 4, Macro moving 1/4 by +6 steps = 1/16 = 24
        const int quarter = lfoCycles (1.0f, 0.0f), dotted = lfoCycles (1.5f, 0.0f), modded = lfoCycles (1.0f, 6.0f / 18.0f);
        const bool syncOk = quarter == 6 && dotted == 4 && modded == 24
                            && P::syncRateNames[P::syncRateIndex (1.0f) - 2] == "1/4 D" && P::syncRateNames[P::syncRateIndex (1.0f) + 2] == "1/4 T";
        if (! syncOk) ++failures;
        std::cout << (syncOk ? "ok   " : "FAIL ") << "Synced LFO over 3 s at 120 BPM: 1/4 " << quarter << " cycles, 1/4 D " << dotted
                  << ", 1/4 modulated +6 steps -> " << modded << " (1/16)\n";

        // S&H rate modulated: 1 Bar, Macro pushes it to 1/16
        auto shSteps = [&] (float macroAmt)
        {
            ElukkaAudioProcessor m;
            m.setPlayConfigDetails (0, 2, sr, block);
            setP (m, "sh_syncRate", (float) P::syncRateIndex (4.0f)); setP (m, "macro1", 1.0f);
            if (macroAmt != 0.0f) { setP (m, "mx1_src", (float) P::SrcMacro1); setP (m, "mx1_dst", (float) P::DstShRate); setP (m, "mx1_amt", macroAmt); }
            m.prepareToPlay (sr, block);
            juce::AudioBuffer<float> b2 (2, block);
            int changes = 0;
            float last = -1.0f;
            for (int b = 0; b < (int) (sr * 2) / block; ++b)
            {
                juce::MidiBuffer midi;
                m.processBlock (b2, midi);
                const float v = m.uiSh.load();
                if (b > 0 && v != last) ++changes;
                last = v;
            }
            return changes;
        };
        const int shBar = shSteps (0.0f), shFast = shSteps ((float) (P::syncRateIndex (0.25f) - P::syncRateIndex (4.0f)) / (float) (P::numSyncRates - 1));
        const bool shOk = shBar <= 1 && shFast >= 15 && shFast <= 16;   // 1/16 at 120 BPM = 8 per second
        if (! shOk) ++failures;
        std::cout << (shOk ? "ok   " : "FAIL ") << "S&H rate knob modulated: 1 Bar -> " << shBar << " new values in 2 s, pushed to 1/16 -> " << shFast << "\n";

        // Old saves: the previous rate lists are mapped to the new knob values
        ElukkaAudioProcessor src;
        auto tree = src.saveState();
        for (auto id : { "m1_syncRate", "sh_syncRate" }) tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
        for (auto [id, v] : { std::pair<const char*, float> { "m1_div", 11.0f }, { "sh_rate", 6.0f } })
        {
            juce::ValueTree old ("PARAM");
            old.setProperty ("id", id, nullptr);
            old.setProperty ("value", v, nullptr);
            tree.appendChild (old, nullptr);
        }
        ElukkaAudioProcessor dst;
        dst.loadState (tree);
        const int m1 = (int) dst.apvts.getRawParameterValue ("m1_syncRate")->load(), shr = (int) dst.apvts.getRawParameterValue ("sh_syncRate")->load();
        const bool migOk = P::syncRateNames[m1] == "1/4 D" && P::syncRateNames[shr] == "1/16";
        if (! migOk) ++failures;
        std::cout << (migOk ? "ok   " : "FAIL ") << "Old saves: Mod 1 '1/4 D' -> " << P::syncRateNames[m1] << ", S&H '1/16' -> " << P::syncRateNames[shr] << "\n";

        // Saves from before "8 Bars" (no "syncRates" marker, no S&H Retrigger): indices move up one, Retrigger stays off
        auto pre8 = src.saveState();
        pre8.removeProperty ("syncRates", nullptr);
        pre8.removeChild (pre8.getChildWithProperty ("id", "sh_retrig"), nullptr);
        pre8.getChildWithProperty ("id", "m1_syncRate").setProperty ("value", 9.0f, nullptr);    // "1/4" in the old list
        pre8.getChildWithProperty ("id", "sh_syncRate").setProperty ("value", 0.0f, nullptr);    // "4 Bars" in the old list
        ElukkaAudioProcessor dst8;
        dst8.loadState (pre8);
        const int m1n = (int) dst8.apvts.getRawParameterValue ("m1_syncRate")->load(), shn = (int) dst8.apvts.getRawParameterValue ("sh_syncRate")->load();
        const bool retrigOff = dst8.apvts.getRawParameterValue ("sh_retrig")->load() < 0.5f, retrigDefault = src.apvts.getRawParameterValue ("sh_retrig")->load() > 0.5f;
        const bool pre8Ok = P::syncRateNames[m1n] == "1/4" && P::syncRateNames[shn] == "4 Bars" && P::syncRateNames[0] == "8 Bars" && retrigOff && retrigDefault;
        if (! pre8Ok) ++failures;
        std::cout << (pre8Ok ? "ok   " : "FAIL ") << "Saves before 8 Bars: Mod 1 '1/4' -> " << P::syncRateNames[m1n] << ", S&H '4 Bars' -> " << P::syncRateNames[shn]
                  << ", S&H Retrigger " << (retrigOff ? "off" : "on") << " (new default " << (retrigDefault ? "on" : "off") << ")\n";
    }

    // ---- Comb filter tail: after the note ends, the comb must die away ----
    {
        ElukkaAudioProcessor q;
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "comb_on", 1.0f); setP (q, "master", 0.0f);
        q.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        float lastRms = 0.0f;
        const int blocks = (int) (sr * 4.0) / block;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            if (b == 40) midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
            q.processBlock (buf, midi);
            if (b >= blocks - 20) lastRms = std::max (lastRms, buf.getRMSLevel (0, 0, block));
        }
        const bool combOk = lastRms < 1.0e-4f;
        if (! combOk) ++failures;
        std::cout << (combOk ? "ok   " : "FAIL ") << "Comb filter tail 3.5 s after the note: " << juce::String (juce::Decibels::gainToDecibels (lastRms, -140.0f), 1) << " dB\n";
    }

    // Sequencer start step: a 4-step pattern C C# D D# starting at step 3 plays D D# C C# (root 60)
    {
        auto pat = SeqPattern::fromText ("t", "xxxx", "C C# D D#");
        pat.startStep = 2;
        SeqEngine eng;
        std::vector<SeqEvent> ev;
        juce::String played;
        for (int b = 0; b < 8; ++b)
        {
            ev.clear();
            eng.process (pat, 3000, 48000.0, 120.0, false, 0.0, true, 60, 0.25, 0.5f, 0.6f, ev);
            for (auto& e : ev) if (e.on) played << (e.note - 60) << " ";
        }
        SeqPattern back;
        back.fromTree (pat.toTree());
        const bool startOk = played.trim() == "2 3 0 1" && back.startStep == 2;
        if (! startOk) ++failures;
        std::cout << (startOk ? "ok   " : "FAIL ") << "Sequencer start step 3: semitones played " << played.trim() << "; start step saved with the pattern\n";
        juce::StringArray names;
        for (auto& f : SeqPattern::factory()) names.add (f.name);
        std::cout << "     Patterns: " << names.joinIntoString (", ") << "\n";
    }

    // Sequencer retrigger. Engine: host playing from beat 1.25 (16ths, pattern C C# D D#), a new note at beat 1.625
    // (half way through a step) restarts the pattern there: C at once, C# a 16th later. Without it the pattern follows
    // the song position. Processor: with Retrigger on, a second key while the first is held restarts at the start step.
    {
        auto pat = SeqPattern::fromText ("t", "xxxx", "C C# D D#");
        const double stepBeats = 0.25, spb = 48000.0 * 60.0 / 120.0;   // samples per beat at 120 BPM
        auto run = [&] (bool retrigger)
        {
            SeqEngine eng;
            std::vector<SeqEvent> ev;
            juce::String played;
            double ppq = 1.25;
            for (int b = 0; b < 8; ++b)   // blocks of 1/8 beat
            {
                if (retrigger && b == 3) eng.retrigger();   // at beat 1.625
                ev.clear();
                eng.process (pat, (int) (spb / 8), 48000.0, 120.0, true, ppq, true, 60, stepBeats, 0.5f, 0.6f, ev);
                for (auto& e : ev) if (e.on) played << (e.note - 60) << "@" << juce::String (ppq + e.offset / spb, 3) << " ";
                ppq += 0.125;
            }
            return played.trim();
        };
        const auto plain = run (false), retrig = run (true);
        const bool engineOk = plain == "1@1.250 2@1.500 3@1.750 0@2.000" && retrig == "1@1.250 2@1.500 0@1.625 1@1.875 2@2.125";

        auto qp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *qp;
        FakePlayHead fph; fph.bpm = 120.0; fph.ppq = 0.0;
        q.setPlayHead (&fph);
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "sq_on", 1.0f); setP (q, "sq_retrig", 1.0f); setP (q, "sq_rate", 1.0f);
        q.prepareToPlay (sr, block);
        q.pattern = SeqPattern::fromText ("t", "xxxx xxxx xxxx xxxx", "C C C C C C C C C C C C C C C C");
        juce::AudioBuffer<float> buf (2, block);
        int stepAfterSecond = -1;
        for (int b = 0; b < 60; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == 40) midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);   // ~0.43 s in: step 6 or so
            q.processBlock (buf, midi);
            if (b == 40) stepAfterSecond = q.seq.displayStep.load();
            fph.ppq += block / sr * fph.bpm / 60.0;
        }
        const bool procOk = stepAfterSecond == 0;
        q.setPlayHead (nullptr);
        const bool ok = engineOk && procOk;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Sequencer retrigger: notes (semitones@beat) without " << plain << "; with a note at 1.625 " << retrig
                  << "; in the plugin a second key restarts at step " << (stepAfterSecond + 1) << "\n";
    }

    // Ping-pong delay: a click into a fully wet ping-pong (1/8 at 120 BPM, feedback 0.5): echoes alternate left and right,
    // the first right echo as loud as the first left one, both sides equally loud overall, and each round trip (left to
    // left) down by feedback squared
    {
        FXChain chain;
        const int len = (int) (sr * 3.0);
        chain.prepare (sr, len);
        FXParams fp;
        fp.dlyOn = true; fp.dlySync = true; fp.dlyBeats = 0.5f; fp.dlyFb = 0.5f; fp.dlyMix = 1.0f; fp.dlyPingPong = true;
        std::vector<float> L ((size_t) len, 0.0f), R ((size_t) len, 0.0f);
        L[0] = R[0] = 1.0f;
        FXChain::Order order;
        for (int i = 0; i < P::NumFx; ++i) order[(size_t) i] = P::defaultFxOrder[i];
        chain.process (L.data(), R.data(), len, fp, 120.0, order);
        const int d = (int) (0.25 * sr);
        auto peak = [&] (const std::vector<float>& x, int k) { float m = 0.0f; for (int i = k * d - 50; i < k * d + 50; ++i) m = std::max (m, std::abs (x[(size_t) i])); return m; };
        auto db = [] (float a, float b) { return juce::Decibels::gainToDecibels (a / b, -200.0f); };
        double eL = 0.0, eR = 0.0;
        for (int i = 0; i < len; ++i) { eL += L[(size_t) i] * L[(size_t) i]; eR += R[(size_t) i] * R[(size_t) i]; }
        const float firstLR = db (peak (R, 2), peak (L, 1)), overall = (float) (10.0 * std::log10 (eR / eL)), roundTrip = db (peak (L, 3), peak (L, 1));
        const bool alternate = peak (R, 1) < 0.01f * peak (L, 1) && peak (L, 2) < 0.01f * peak (R, 2);
        const bool ok = std::abs (firstLR) < 0.5f && std::abs (overall) < 0.5f && std::abs (roundTrip - 20.0f * std::log10 (0.25f)) < 0.5f && alternate;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Ping-pong delay: first right echo vs first left " << juce::String (firstLR, 2) << " dB, right vs left overall "
                  << juce::String (overall, 2) << " dB, round trip " << juce::String (roundTrip, 1) << " dB (feedback 0.5 squared = -12.0), alternating "
                  << (alternate ? "yes" : "no") << "\n";
    }

    // Mod 3: a third mod unit like Mod 1/2. A square LFO at 4 Hz on Osc 1 Level (per voice) and on Master (global) makes
    // the level swing; its shape is saved with the state; a save from before Mod 3 gets the sine shape; presets set it.
    {
        auto swing = [&] (int dst)
        {
            auto qp = std::make_unique<ElukkaAudioProcessor>();
            auto& q = *qp;
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "o1_wave", (float) P::WSine); setP (q, "o1_level", 0.5f); setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
            setP (q, "m3_mode", 0.0f); setP (q, "m3_rate", 4.0f);
            q.shapes[2].setPoints (ModShape::preset (4));   // square
            setP (q, "mx1_src", (float) P::SrcMod3); setP (q, "mx1_dst", (float) dst); setP (q, "mx1_amt", -0.8f);
            auto out = renderNotes (q, { 60 }, 1.5);
            float lo = 1.0e9f, hi = 0.0f;   // RMS per 20 ms window over the sustain
            for (int st = (int) (0.3 * sr); st + (int) (0.02 * sr) < out.getNumSamples(); st += (int) (0.02 * sr))
            {
                const float r = out.getRMSLevel (0, st, (int) (0.02 * sr));
                lo = std::min (lo, r); hi = std::max (hi, r);
            }
            return juce::Decibels::gainToDecibels (hi / std::max (lo, 1.0e-9f));
        };
        const float voiceSwing = swing (P::DstO1Level), globalSwing = swing (P::DstMaster);

        auto ap = std::make_unique<ElukkaAudioProcessor>(), bp = std::make_unique<ElukkaAudioProcessor>();   // heap: main's stack is small
        auto& a = *ap;
        auto& b = *bp;
        a.shapes[2].setPoints (ModShape::preset (8));
        const auto saved = a.saveState();
        b.loadState (saved);
        bool shapeOk = b.shapes[2].getPoints().size() == ModShape::preset (8).size();
        auto old = saved.createCopy();
        old.removeChild (old.getChildWithName ("Shape3"), nullptr);
        b.loadState (old);
        shapeOk &= b.shapes[2].getPoints().size() == ModShape::preset (0).size();
        b.shapes[2].setPoints (ModShape::preset (8));
        b.presets.load (0);   // Init
        shapeOk &= b.shapes[2].getPoints().size() == ModShape::preset (0).size();
        const bool namesOk = P::modSources[P::SrcMod3] == "Mod 3" && P::destForParam ("m3_rate") == P::DstM3Rate
                             && P::tableModSrcMap[P::tableModSources.indexOf ("Mod 3")] == P::SrcMod3;

        const bool ok = voiceSwing > 6.0f && globalSwing > 6.0f && shapeOk && namesOk;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Mod 3: square LFO swings Osc 1 Level by " << juce::String (voiceSwing, 1) << " dB (per voice) and Master by "
                  << juce::String (globalSwing, 1) << " dB (global); shape saved, old saves and presets reset it (" << (shapeOk ? "yes" : "no") << ")\n";
    }

    // Cutoff knobs (voice filter and FX filter) are logarithmic: every octave takes the same knob distance, 20 Hz at 0,
    // 632 Hz in the middle, 20 kHz at the end; values survive the round trip
    {
        auto qp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *qp;
        bool ok = true;
        juce::String rep;
        for (auto id : { "flt_cutoff", "fxf_cutoff" })
        {
            auto* prm = q.apvts.getParameter (id);
            for (float x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const float hz = prm->convertFrom0to1 (x), expected = 20.0f * std::pow (1000.0f, x);
                ok &= std::abs (1200.0f * std::log2 (hz / expected)) < 1.0f && std::abs (prm->convertTo0to1 (hz) - x) < 1.0e-4f;
                if (juce::String (id) == "flt_cutoff") rep << juce::String (x * 100.0f, 0) << "% " << juce::String (hz, 0) << " Hz; ";
            }
            ok &= std::abs (prm->getDefaultValue() - 1.0f) < 1.0e-6f;   // default fully open
        }
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Cutoff knobs logarithmic (both filters): " << rep << "\n";
    }

    // Filter key tracking with the sequencer: at Key Track 100% the filter follows each sequenced note, so a C3 step and
    // a C4 step have the same balance of 3rd harmonic to fundamental (as when the notes are played by hand); at 0% the
    // higher note is duller. The Key mod source follows the sequenced note too (Key -> Osc 2 Level).
    {
        auto qp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *qp;
        FakePlayHead fph; fph.playing = false; fph.bpm = 120.0;
        q.setPlayHead (&fph);
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "o1_wave", (float) P::WSaw); setP (q, "o1_level", 0.5f); setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
        setP (q, "flt_cutoff", 400.0f); setP (q, "flt_res", 0.0f);
        setP (q, "ampR", 0.005f);   // no tail from the previous step under the next one (C3's 2nd harmonic is C4's fundamental)
        // balance (dB) of the 3rd harmonic against the fundamental in [t0, t0 + 0.15 s]
        auto balance = [&] (const juce::AudioBuffer<float>& b, double t0, float f0)
        {
            auto mag = [&] (float hz)
            {
                const int a = (int) (t0 * sr), n = (int) (0.15 * sr);
                double re = 0.0, im = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / n), ph = juce::MathConstants<double>::twoPi * hz * i / sr;
                    re += w * b.getSample (0, a + i) * std::cos (ph); im += w * b.getSample (0, a + i) * std::sin (ph);
                }
                return std::hypot (re, im);
            };
            return (float) juce::Decibels::gainToDecibels (mag (3.0f * f0) / mag (f0), -200.0);
        };
        auto seqRun = [&] (float keyPct, const char* pitches = "C ^C")
        {
            setP (q, "flt_key", keyPct);
            setP (q, "sq_on", 1.0f); setP (q, "sq_rate", 0.0f); setP (q, "sq_gate", 0.95f);   // 1/8 steps: 0.25 s at 120 BPM
            q.pattern = SeqPattern::fromText ("t", "xx", pitches);
            auto out = renderNotes (q, { 48 }, 1.0);
            setP (q, "sq_on", 0.0f);
            return std::make_pair (balance (out, 0.55, 130.81f), balance (out, 0.3, 261.63f));   // steps 3 (C3, 0.5 s) and 2 (C4, 0.25 s)
        };
        auto handRun = [&] (float keyPct)
        {
            setP (q, "flt_key", keyPct);
            return std::make_pair (balance (renderNotes (q, { 48 }, 0.6), 0.3, 130.81f), balance (renderNotes (q, { 60 }, 0.6), 0.3, 261.63f));
        };
        const auto seq100 = seqRun (100.0f), seq0 = seqRun (0.0f), hand100 = handRun (100.0f);
        // the extended range: -100% inverts (C4's cutoff two octaves below C3's: the C4 step much darker), +200% doubles
        // (the C4 step brighter); each matches the same notes played by hand
        const auto seqNeg = seqRun (-100.0f), handNeg = handRun (-100.0f), seq200 = seqRun (200.0f), hand200 = handRun (200.0f);
        const float negDiff = seqNeg.second - seqNeg.first, dblDiff = seq200.second - seq200.first;
        const bool rangeOk = negDiff < -15.0f && dblDiff > 5.0f && std::abs (negDiff - (handNeg.second - handNeg.first)) < 1.5f
                             && std::abs (dblDiff - (hand200.second - hand200.first)) < 1.5f;
        const auto slid = seqRun (100.0f, "C~ ^C~");   // every step slides into the next (one voice glides between them)
        fph.playing = true; fph.ppq = 0.0;
        const auto hostPlaying = seqRun (100.0f);
        fph.playing = false;
        setP (q, "flt_slope", 1.0f);
        const auto slope12 = seqRun (100.0f);
        setP (q, "flt_slope", 0.0f);
        std::cout << "     key tracking variants (C4 minus C3, dB): slides " << juce::String (slid.second - slid.first, 1) << ", host playing "
                  << juce::String (hostPlaying.second - hostPlaying.first, 1) << ", 12 dB " << juce::String (slope12.second - slope12.first, 1) << "\n";
        const float seqDiff = seq100.second - seq100.first, handDiff = hand100.second - hand100.first, flatDiff = seq0.second - seq0.first;
        q.setPlayHead (nullptr);
        const bool ok = std::abs (seqDiff) < 1.5f && std::abs (seqDiff - handDiff) < 1.5f && flatDiff < -6.0f && rangeOk;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "Filter key tracking with the sequencer: 3rd harmonic vs fundamental, C4 step minus C3 step: Key Track 100% "
                  << juce::String (seqDiff, 1) << " dB (by hand " << juce::String (handDiff, 1) << " dB), 0% " << juce::String (flatDiff, 1) << " dB, -100% "
                  << juce::String (negDiff, 1) << " dB, +200% " << juce::String (dblDiff, 1) << " dB\n";
    }

    // FX filter: a 24 dB low pass at 500 Hz keeps a 110 Hz sine and takes a 1760 Hz one down by 40 dB or more; the high
    // pass does the opposite; off leaves the sound untouched; Macro 1 -> FX Filter Cutoff closes it from fully open;
    // sweeping the cutoff during a note makes no clicks; a save from before the filter existed gets it after Distortion.
    {
        auto qp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *qp;
        q.setPlayConfigDetails (0, 2, sr, block);
        setP (q, "o1_wave", (float) P::WSine); setP (q, "o1_level", 0.5f); setP (q, "master", 0.0f); setP (q, "ampS", 1.0f);
        auto level = [&] (int note, float hz) { return juce::Decibels::gainToDecibels (measure (renderNotes (q, { note }, 1.6), 0, hz).first, -140.0f); };
        const float dryLo = level (45, 110.0f), dryHi = level (93, 1760.0f);
        setP (q, "fxf_on", 1.0f); setP (q, "fxf_cutoff", 500.0f);
        const float lpLo = level (45, 110.0f) - dryLo, lpHi = level (93, 1760.0f) - dryHi;
        setP (q, "fxf_type", 1.0f);   // high pass
        const float hpLo = level (45, 110.0f) - dryLo, hpHi = level (93, 1760.0f) - dryHi;
        setP (q, "fxf_type", 0.0f); setP (q, "fxf_cutoff", 20000.0f);
        setP (q, "mx1_src", (float) P::SrcMacro1); setP (q, "mx1_dst", (float) P::DstFxfCutoff); setP (q, "mx1_amt", -0.9f); setP (q, "macro1", 1.0f);
        const float macroHi = level (93, 1760.0f) - dryHi;
        setP (q, "mx1_src", 0.0f); setP (q, "macro1", 0.0f);
        setP (q, "fxf_on", 0.0f);
        const float offHi = level (93, 1760.0f) - dryHi;

        // cutoff swept 20 kHz -> 200 Hz -> 20 kHz over 0.6 s on a held sine at 131 Hz, resonance up (a sine: the click
        // detector reads a saw's own edges as clicks)
        setP (q, "fxf_on", 1.0f); setP (q, "fxf_res", 0.5f);
        q.prepareToPlay (sr, block);
        auto sweep = [&] (bool moving)
        {
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> o (2, (int) (sr * 1.2) / block * block), buf (2, block);
            for (int b = 0; b < o.getNumSamples() / block; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
                const double t = (double) b * block / sr, x = moving ? juce::jlimit (0.0, 1.0, 1.0 - std::abs ((t - 0.5) / 0.3)) : 0.0;
                setP (q, "fxf_cutoff", (float) (20000.0 * std::pow (0.01, x)));
                q.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) o.copyFrom (ch, b * block, buf, ch, 0, block);
            }
            return o;
        };
        auto out = sweep (true);
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i));
        const auto clicks = detectClicks (out, 0.005f, 1000), still = detectClicks (sweep (false), 0.005f, 1000);

        // an older save: FX order without "filter"
        auto old = q.saveState();
        old.setProperty ("fxOrder", "comb,ott,dist,delay,reverb,eq", nullptr);
        ElukkaAudioProcessor fresh;
        fresh.loadState (old);
        juce::StringArray names;
        for (auto& f : fresh.fxOrder) names.add (P::fxKeys[f.load()]);
        const bool orderOk = names.joinIntoString (",") == "comb,ott,dist,filter,delay,reverb,eq";

        const bool ok = std::abs (lpLo) < 1.0f && lpHi < -40.0f && hpHi > -1.0f && hpLo < -40.0f && macroHi < -20.0f && std::abs (offHi) < 0.01f
                        && finite && clicks.clicks == 0 && orderOk;
        if (! ok) ++failures;
        std::cout << (ok ? "ok   " : "FAIL ") << "FX filter: low pass 500 Hz: 110 Hz " << juce::String (lpLo, 1) << " dB, 1760 Hz " << juce::String (lpHi, 1)
                  << " dB; high pass: 110 Hz " << juce::String (hpLo, 1) << " dB, 1760 Hz " << juce::String (hpHi, 1) << " dB; Macro 1 closing it: 1760 Hz "
                  << juce::String (macroHi, 1) << " dB; off " << juce::String (offHi, 2) << " dB; cutoff sweep clicks " << clicks.clicks << " (worst " << juce::String (clicks.worst, 4)
                  << "; same note unswept: " << still.clicks << ", worst " << juce::String (still.worst, 4) << ")"
                  << "; old save's FX order -> " << names.joinIntoString (" > ") << "\n";
    }

    // ---- LFO smoothing, S&H retrigger, wet mixes, resynth position restart ----
    {
        // Smoothing: a square LFO jumps 0 -> 100%; with Smooth the per-block change is limited
        auto maxStep = [&] (float smoothMs)
        {
            ModUnit u; ModShape sq; sq.setPoints (ModShape::preset (4));
            ModUnitParams mp; mp.rateHz = 2.0; mp.smoothMs = smoothMs;
            juce::Random r2; u.trigger (mp, r2, 0.0);
            float last = u.process (mp, sq, 128, sr, r2), worst = 0.0f;
            for (int i = 0; i < 2000; ++i) { const float v = u.process (mp, sq, 128, sr, r2); worst = std::max (worst, std::abs (v - last)); last = v; }
            return worst;
        };
        const float s0 = maxStep (0.0f), s3 = maxStep (3.0f), s10 = maxStep (10.0f);
        const bool smoothOk = s0 > 0.99f && s3 < 0.6f && s10 < 0.25f;
        if (! smoothOk) ++failures;
        std::cout << (smoothOk ? "ok   " : "FAIL ") << "LFO Smooth: biggest jump per 2.7 ms block for a square LFO: 0 ms " << juce::String (s0, 2)
                  << ", 3 ms " << juce::String (s3, 2) << ", 10 ms " << juce::String (s10, 2) << "\n";

        // S&H retrigger: each note gives a new value at once (host grid ignored)
        {
            ElukkaAudioProcessor q;
            q.setPlayHead (&ph);
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "sh_retrig", 1.0f); setP (q, "sh_syncRate", (float) P::syncRateIndex (4.0f));   // 1 bar: no grid steps inside the test
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block);
            ph.playing = true; ph.ppq = 0.3;
            int changesAtNotes = 0;
            float last = q.uiSh.load();
            for (int b = 0; b < 40; ++b)
            {
                juce::MidiBuffer midi;
                const bool noteHere = b % 10 == 5;
                if (noteHere) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                q.processBlock (buf, midi);
                ph.ppq += block / sr * ph.bpm / 60.0;
                if (noteHere && q.uiSh.load() != last) ++changesAtNotes;
                last = q.uiSh.load();
            }
            const bool retrigOk = changesAtNotes == 4;
            if (! retrigOk) ++failures;
            std::cout << (retrigOk ? "ok   " : "FAIL ") << "S&H Retrigger: new value on " << changesAtNotes << " of 4 notes\n";
            q.setPlayHead (nullptr);
        }

        // Delay and reverb at 100% mix: no dry signal (first 20 ms of a note are silent before any echo/reverb)
        {
            ElukkaAudioProcessor q;
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "master", 0.0f);
            auto firstMs = [&] (const char* fx)
            {
                setP (q, "dly_on", 0.0f); setP (q, "rev_on", 0.0f);
                if (fx != nullptr) { setP (q, juce::String (fx) == "dly" ? "dly_on" : "rev_on", 1.0f); setP (q, juce::String (fx) == "dly" ? "dly_mix" : "rev_mix", 1.0f); }
                auto b2 = renderNotes (q, { 60 }, 0.5);
                return juce::Decibels::gainToDecibels (b2.getMagnitude (0, 0, (int) (0.02 * sr)), -140.0f);
            };
            const float dry = firstMs (nullptr), dly = firstMs ("dly"), rev = firstMs ("rev");
            const bool wetOk = dly < dry - 60.0f && rev < dry - 20.0f;
            if (! wetOk) ++failures;
            std::cout << (wetOk ? "ok   " : "FAIL ") << "Mix 100% = fully wet: first 20 ms peak dry " << juce::String (dry, 1) << " dB, delay " << juce::String (dly, 1)
                      << " dB, reverb " << juce::String (rev, 1) << " dB\n";
        }

        // Position restart: S&H -> Position; every step restarts playback exactly at the new position
        {
            juce::AudioBuffer<float> longTone (2, (int) (sr * 4));
            for (int i = 0; i < longTone.getNumSamples(); ++i)
            {
                const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr);
                longTone.setSample (0, i, v); longTone.setSample (1, i, v);
            }
            ElukkaAudioProcessor q;
            q.setPlayHead (&ph);
            q.setPlayConfigDetails (0, 2, sr, block);
            setP (q, "o1_wave", (float) P::WResynth); setP (q, "rs_sync", 0.0f); setP (q, "ampS", 1.0f);
            setP (q, "sh_smooth", 0.0f); setP (q, "sh_syncRate", (float) P::syncRateIndex (0.25f));   // 1/16
            setP (q, "mx1_src", (float) P::SrcSH); setP (q, "mx1_dst", (float) P::DstResPos); setP (q, "mx1_amt", 1.0f);
            q.setResynth (ResynthData::fromAudio (longTone, sr, "long"));
            // Fixed S&H values: an unseeded juce::Random is seeded from the clock, and about 2% of seeds draw a
            // value above 0.999 (the restart loops round within the block, so it reads as a non-exact jump) or two
            // consecutive values within 0.02 (a small backwards restart that reads as a non-advancing block).
            q.seedRandom (1);
            q.prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block);
            ph.playing = true; ph.ppq = 0.0; ph.bpm = 120.0;
            int jumps = 0, exact = 0, advancing = 0, steady = 0;
            float lastPos = -1.0f;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                q.processBlock (buf, midi);
                ph.ppq += block / sr * ph.bpm / 60.0;
                const float pos = q.uiRsPos.load();
                if (lastPos >= 0.0f)
                {
                    const bool loopWrap = lastPos > 0.97f && pos < 0.03f;   // started near the end and looped round
                    if (std::abs (pos - lastPos) > 0.02f && ! loopWrap) { ++jumps; if (std::abs (pos - q.uiSh.load()) < 0.02f) ++exact; }
                    else if (! loopWrap) { ++steady; if (pos > lastPos) ++advancing; }
                }
                lastPos = pos;
            }
            ph.bpm = 128.0;
            const bool posOk = jumps >= 10 && exact == jumps && advancing > steady * 0.9;
            if (! posOk) ++failures;
            std::cout << (posOk ? "ok   " : "FAIL ") << "Position restart: " << jumps << " S&H jumps, " << exact << " landed exactly on the new position, "
                      << "playback runs forward between jumps (" << advancing << "/" << steady << " blocks)\n";
            q.setPlayHead (nullptr);
        }
    }

    failures += UnisonTests::run();

    std::cout << "Worst render time: " << juce::String (worstRealtime * 100.0, 1) << "% of real time\n";
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILURES") << "\n";
    proc.setPlayHead (nullptr);
    return failures == 0 ? 0 : 1;
}
