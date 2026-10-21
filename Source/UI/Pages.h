#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"
#include <deque>

// Base for the tab pages. refresh() runs when non-parameter state changes (preset load etc.),
// tick() runs at ~30 Hz for live displays.
class Page : public juce::Component
{
public:
    virtual void refresh() {}
    virtual void tick() {}
};

//==============================================================================
class ShapeEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    ShapeEditor (ElukkaAudioProcessor& p, int unit) : proc (p), index (unit) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragPoint = dragSegment = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int gridX = 16, gridY = 8;
    bool snap = true;   // points snap to the grid while dragging/adding

private:
    juce::Point<float> snapped (juce::Point<float> p) const
    {
        if (! snap) return p;
        return { std::round (p.x * gridX) / gridX, std::round (p.y * gridY) / gridY };
    }
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    int hitPoint (juce::Point<float> p) const;
    void commit();

    ElukkaAudioProcessor& proc;
    int index;
    std::vector<ShapePoint> pts;
    int dragPoint = -1, dragSegment = -1;
    float dragStartCurve = 0.0f, dragStartY = 0.0f;
};

class HarmTableEditor : public juce::Component
{
public:
    HarmTableEditor (ElukkaAudioProcessor& p, std::array<float, P::numHarmTable>& t, bool isBipolar, juce::String title)
        : proc (p), table (t), bipolar (isBipolar), name (std::move (title)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { lastIndex = -1; edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void edit (const juce::MouseEvent&);
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (8.0f).withTrimmedTop (16.0f); }
    ElukkaAudioProcessor& proc;
    std::array<float, P::numHarmTable>& table;
    bool bipolar;
    juce::String name;
    int lastIndex = -1;
    float lastValue = 0.0f;
};

class SpectrogramView : public juce::Component
{
public:
    explicit SpectrogramView (ElukkaAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void refresh() { cached = {}; repaint(); }
    // 0..1 position being played, or < 0 when nothing is playing
    void setPlayhead (float pos) { if (std::abs (pos - playhead) > 0.0005f) { playhead = pos; repaint(); } }

private:
    ElukkaAudioProcessor& proc;
    juce::Image cached;
    float playhead = -1.0f;
};

class SeqGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SeqGrid (ElukkaAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setBar (int b) { bar = b; repaint(); }
    int getBar() const { return bar; }

private:
    struct Areas { juce::Rectangle<float> time, roll, accent, slide; float colW; float labelW; };
    Areas areas() const;
    // Pitch lane: a bar per step from the root line, -36..+36 semitones
    float zeroY (const Areas& a) const { return a.roll.getCentreY(); }
    float unitY (const Areas& a) const { return (a.roll.getHeight() - 28.0f) / (2.0f * PitchEntry::range); }
    void setPitch (int idx, int semis);
    void typePitch (int idx, juce::Rectangle<int> near);

    ElukkaAudioProcessor& proc;
    int bar = 0;
    int dragMode = 0;   // 1 = pitch, 2 = accent, 3 = slide
    bool dragSetTo = false;
    bool pitchDragged = false;   // a click without a drag on the pitch lane opens the type-in box
    int downIndex = -1;
};

//==============================================================================
class OscPanel : public Panel
{
public:
    OscPanel (APVTS& s, int i);
    void resized() override;

private:
    Toggle on;
    Choice wave, fmSrc;
    Knob level, coarse, fine, rand, voices, detune, blend, width, fmAmt, pan;
    Toggle bassMono;
    Choice phaseMode, uniMode;
};

class ModPanel : public Panel
{
public:
    ModPanel (ElukkaAudioProcessor& p, int i);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh() { editor.repaint(); updateRateKnob(); }
    void tick();

private:
    ElukkaAudioProcessor& proc;
    int index;
    Choice mode;
    Toggle sync, retrig;
    Knob rate, syncRate, start, smooth;   // rate (Hz) and syncRate share a spot: Sync picks which one shows
    void updateRateKnob();
    juce::ComboBox shapePreset;
    juce::TextButton snapButton { "Snap" };
    ShapeEditor editor;
    float shownValue = 0.0f;
};

// Tempo-synced sample & hold: rate selector and a plot of the most recent steps.
class ShPanel : public Panel
{
public:
    explicit ShPanel (ElukkaAudioProcessor& p);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void tick();

private:
    ElukkaAudioProcessor& proc;
    Knob rate;
    Knob smooth;
    Toggle retrig;
    juce::Label hint;
    std::deque<float> history;
    float lastValue = 2.0f;
    juce::Rectangle<int> plotArea;
};

class SynthPage : public Page
{
public:
    explicit SynthPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override { for (auto* m : mods) m->refresh(); }
    void tick() override { for (auto* m : mods) m->tick(); sh.tick(); }

private:
    OscPanel o1, o2, o3;
    Panel filterPanel { "FILTER" };
    Choice fltType, fltSlope;
    Knob fCutoff, fRes, fEnv, fKey, fDrive, fA, fD, fS, fR;
    juce::Label fEnvLabel;
    Toggle toFlt1, toFlt2, toFlt3;   // which oscillators go through the filter
    juce::Label fInputLabel;
    Panel ampPanel { "AMP ENVELOPE / VOICE" };
    Knob a, d, s, r, glide, bend, bassFreq;
    Choice polyMode;
    // one wide panel showing Mod 1, 2 or 3, picked with the tabs in its header
    ModPanel mod1, mod2, mod3;
    ModPanel* mods[P::numMods] { &mod1, &mod2, &mod3 };
    juce::TextButton modTabs[P::numMods];
    int modTab = 0;
    void showMod (int i);
    ShPanel sh;
};

class HarmonicsPage : public Page
{
public:
    explicit HarmonicsPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override { gain.repaint(); depth.repaint(); }

private:
    ElukkaAudioProcessor& proc;
    Panel controls { "HARMONIC PROCESSING" };
    Knob stretch, oddEven, cutoff, res, prism, prismCenter, prismWidth, shimmer, shimmerRate, tableAmt;
    Choice tableSrc;
    juce::Label help;
    HarmTableEditor gain, depth;
    juce::TextButton resetGain { "Reset Levels" }, resetDepth { "Reset Depth" };
};

class ResynthPage : public Page, public juce::FileDragAndDropTarget
{
public:
    explicit ResynthPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override;
    void tick() override;
    void paintOverChildren (juce::Graphics&) override;

    // Drag and drop audio files onto the page
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    static bool isAudioFile (const juce::File& f);
    static bool isImageFile (const juce::File& f);
    void loadAudioFile (const juce::File& file);
    void loadImageFile (const juce::File& file);
    void loadAudio();
    void loadImage();
    void setParam (const juce::String& id, float value);

    ElukkaAudioProcessor& proc;
    Panel sourcePanel { "SOURCE" }, playPanel { "PLAYBACK / TIME WARP" };
    juce::TextButton loadAudioBtn { "Load Audio..." }, loadImageBtn { "Load Image..." }, clearBtn { "Clear" };
    juce::TextButton detectBtn { "Use Detected BPM" }, doubleBtn { "BPM x2" }, halveBtn { "BPM / 2" }, useOsc1 { "Use on Osc 1" };
    juce::Label info, help;
    SpectrogramView view;
    Knob tune, gate, lift, subLift, speed, bpm, pos;
    Toggle sync, loop;
    juce::Label bpmLabel, bpmField;   // type the loop's BPM
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
};

class SeqPage : public Page
{
public:
    explicit SeqPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override;
    void tick() override { grid.repaint(); }

private:
    ElukkaAudioProcessor& proc;
    Panel controls { "SEQUENCER" };
    Toggle on, retrig;
    Choice rate;
    Knob gate, slide, accent;
    juce::ComboBox patternBox;
    juce::TextButton prevPattern { "<" }, nextPattern { ">" };
    void stepPattern (int delta);
    juce::TextButton savePattern { "Save Pattern" };
    juce::Slider length, pitchCount, startStep;
    juce::Label lengthLabel, pitchLabel, startLabel;
    juce::TextButton bars[4] { juce::TextButton ("Bar 1"), juce::TextButton ("Bar 2"), juce::TextButton ("Bar 3"), juce::TextButton ("Bar 4") };
    juce::TextButton randomize { "Randomize Pitch" };
    juce::ComboBox randomScale, randomRange;
    void randomizePitch();
    SeqGrid grid;
};

// Frequency response of the 3-band EQ, redrawn whenever an EQ parameter changes.
class EqCurve : public juce::Component
{
public:
    explicit EqCurve (ElukkaAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();

private:
    FXParams current() const;
    ElukkaAudioProcessor& proc;
    std::array<float, 10> last {};
};

// One effect in the chain. Drag its background to move it; the page reorders the chain on release.
// Gain change of the OTT's three bands: up = boosted (upward compression), down = reduced.
class OttMeter : public juce::Component
{
public:
    explicit OttMeter (ElukkaAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();

private:
    ElukkaAudioProcessor& proc;
    float shown[3] {};
};

class FxBox : public Panel
{
public:
    FxBox (const juce::String& title, int fx) : Panel (title), fxId (fx) { setInterceptsMouseClicks (true, true); }
    void paint (juce::Graphics& g) override;
    void resized() override { if (layout) layout (content()); }
    void mouseDown (const juce::MouseEvent& e) override { if (onDrag) onDrag (*this, e, 0); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (*this, e, 1); }
    void mouseUp (const juce::MouseEvent& e) override { if (onDrag) onDrag (*this, e, 2); }

    template <typename T, typename... Args> T& add (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        controls.push_back (std::move (c));
        return ref;
    }

    const int fxId;
    std::function<void (juce::Rectangle<int>)> layout;
    std::function<void (FxBox&, const juce::MouseEvent&, int)> onDrag;   // phase: 0 down, 1 drag, 2 up

private:
    std::vector<std::unique_ptr<juce::Component>> controls;
};

class FxPage : public Page
{
public:
    explicit FxPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override { resized(); }
    void tick() override;

private:
    void handleDrag (FxBox& box, const juce::MouseEvent& e, int phase);
    juce::Rectangle<int> slotBounds (int slot) const;
    std::vector<int> currentOrder() const;
    void layoutBoxes (const std::vector<int>& order, const FxBox* skip);

    ElukkaAudioProcessor& proc;
    std::unique_ptr<FxBox> boxes[P::NumFx];
    EqCurve* eqCurve = nullptr;
    OttMeter* ottMeter = nullptr;
    Choice* dlyDiv = nullptr;
    Knob* dlyTime = nullptr;
    juce::Label hint;
    int dragOffsetX = 0, dragTarget = -1;
};

class MatrixPage : public Page
{
public:
    explicit MatrixPage (ElukkaAudioProcessor& p);
    void resized() override;
    void refresh() override { for (auto& r : rows) r.amount.updateText(); }

private:
    struct Row
    {
        std::unique_ptr<Choice> src, dst;
        DetentSlider amount;
        std::unique_ptr<APVTS::SliderAttachment> att;
        juce::Label number;
    };
    Panel matrixPanel { "MODULATION MATRIX" }, macroPanel { "MACROS" };
    Row rows[P::numMatrixSlots];
    Knob macro1, macro2;
    juce::Label help;
};
