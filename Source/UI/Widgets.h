#pragma once
#include <JuceHeader.h>
#include "../Params.h"

namespace Col
{
inline const juce::Colour bg       { 0xff111215 };
inline const juce::Colour panel    { 0xff1a1c21 };
inline const juce::Colour panelHi  { 0xff22252c };
inline const juce::Colour border   { 0xff2c3038 };
inline const juce::Colour text     { 0xffdde0e7 };
inline const juce::Colour dim      { 0xff858b97 };
inline const juce::Colour accent   { 0xffb8f03c };   // acid green
inline const juce::Colour mod      { 0xff40d3f0 };   // cyan, used for modulation
inline const juce::Colour warn     { 0xfff0a040 };
}

class ElukkaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ElukkaLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Col::bg);
        setColour (juce::Label::textColourId, Col::text);
        setColour (juce::ComboBox::backgroundColourId, Col::panelHi);
        setColour (juce::ComboBox::outlineColourId, Col::border);
        setColour (juce::ComboBox::textColourId, Col::text);
        setColour (juce::ComboBox::arrowColourId, Col::dim);
        setColour (juce::PopupMenu::backgroundColourId, Col::panelHi);
        setColour (juce::PopupMenu::textColourId, Col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Col::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, Col::text);
        setColour (juce::TextButton::buttonColourId, Col::panelHi);
        setColour (juce::TextButton::buttonOnColourId, Col::accent.withAlpha (0.22f));
        setColour (juce::TextButton::textColourOffId, Col::dim);
        setColour (juce::TextButton::textColourOnId, Col::accent);
        setColour (juce::Slider::textBoxTextColourId, Col::text);
        setColour (juce::Slider::textBoxOutlineColourId, Col::border);
        setColour (juce::Slider::textBoxBackgroundColourId, Col::panelHi);
        setColour (juce::BubbleComponent::backgroundColourId, Col::panelHi);
        setColour (juce::BubbleComponent::outlineColourId, Col::border);
        setColour (juce::TooltipWindow::textColourId, Col::text);
        setColour (juce::AlertWindow::backgroundColourId, Col::panel);
        setColour (juce::AlertWindow::textColourId, Col::text);
        setColour (juce::AlertWindow::outlineColourId, Col::border);
        setColour (juce::TextEditor::backgroundColourId, Col::panelHi);
        setColour (juce::TextEditor::textColourId, Col::text);
        setColour (juce::TextEditor::outlineColourId, Col::border);
        setColour (juce::TextEditor::focusedOutlineColourId, Col::accent);
        setColour (juce::Slider::trackColourId, Col::accent);
        setColour (juce::Slider::backgroundColourId, Col::panelHi);
        setColour (juce::Slider::thumbColourId, Col::accent);
    }

    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        auto size = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (size, size);
        auto c = b.getCentre();
        auto r = size * 0.5f;
        const float angle = start + pos * (end - start);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;

        g.setColour (Col::panelHi);
        g.fillEllipse (b.reduced (r * 0.22f));

        juce::Path track;
        track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
        g.setColour (Col::border);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (s.isEnabled() ? Col::accent : Col::dim);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path ptr;
        auto len = r * 0.55f;
        ptr.addRoundedRectangle (-1.5f, -len, 3.0f, len * 0.6f, 1.5f);
        g.setColour (Col::text);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? Col::accent.withAlpha (0.18f) : Col::panelHi.brighter (hover ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? Col::accent : Col::border);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        auto led = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.33f);
        g.setColour (on ? Col::accent : Col::dim.withAlpha (0.4f));
        g.fillEllipse (led);
        g.setColour (on ? Col::text : Col::dim);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedRight (4), juce::Justification::centredLeft, 1);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto base = on ? Col::accent.withAlpha (0.18f) : Col::panelHi;
        if (hover) base = base.brighter (0.08f);
        if (down) base = base.brighter (0.15f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? Col::accent.withAlpha (0.7f) : Col::border);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (Col::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (true) ? Col::accent.withAlpha (0.6f) : Col::border);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (Col::dim);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};

//==============================================================================
using APVTS = juce::AudioProcessorValueTreeState;

// Matrix amount text: semitones for pitch destinations (1.0 = 48 st), percent otherwise.
// Reads the slot's destination each time, so it follows destination changes.
inline void setupAmountText (juce::Slider& slider, APVTS& state, int slot)
{
    auto* dst = state.getRawParameterValue (P::mx (slot, "dst"));
    slider.textFromValueFunction = [dst] (double v)
    {
        const juce::String sign = v > 0.0 ? "+" : "";
        if (P::isPitchDest ((int) std::lround (dst->load())))
            return sign + juce::String (v * P::pitchModSemitones, 1) + " st";
        return sign + juce::String (v * 100.0, 0) + "%";
    };
    slider.valueFromTextFunction = [dst] (const juce::String& text)
    {
        const double x = text.retainCharacters ("-+0123456789.").getDoubleValue();
        return P::isPitchDest ((int) std::lround (dst->load())) ? x / P::pitchModSemitones : x / 100.0;
    };
    slider.updateText();
}

// A slider with optional detents: while dragging, every multiple of `step` (e.g. 12 = every octave) holds for an extra
// `width` of drag (in value units), so it is easy to land on; the values in between are slightly compressed and all
// stay reachable. Typed values, the mouse wheel and automation are not affected.
class DetentSlider : public juce::Slider
{
public:
    void setDetents (double step, double width) { detentStep = step; detentWidth = width; }
    // optional: detents only while this returns true (checked when a drag starts)
    std::function<bool()> detentsActive;

    // The value a drag position (in value units) lands on, and the drag position a value sits at (its centre).
    static double detentValue (double pos, double step, double width)
    {
        const double base = step * std::floor (pos / step), u = pos - base, half = 0.5 * width;
        if (u <= half) return base;
        if (u >= step - half) return base + step;
        return base + (u - half) * step / (step - width);
    }
    static double detentPosition (double value, double step, double width)
    {
        const double base = step * std::floor (value / step), u = value - base;
        return u == 0.0 ? value : base + 0.5 * width + u * (step - width) / step;
    }

private:
    void startedDragging() override
    {
        dragStartValue = getValue();
        useDetents = detentStep > 0.0 && (detentsActive == nullptr || detentsActive());
    }
    double snapValue (double attempted, DragMode mode) override
    {
        if (! useDetents || mode == notDragging) return attempted;
        // the slider moves `attempted` linearly with the mouse from the value at mouse-down; walk the same distance from
        // that value's place on the detent scale, so the drag never jumps when it starts
        const double v = detentValue (detentPosition (dragStartValue, detentStep, detentWidth) + (attempted - dragStartValue), detentStep, detentWidth);
        return juce::jlimit (getMinimum(), getMaximum(), v);
    }

    double detentStep = 0.0, detentWidth = 0.0, dragStartValue = 0.0;
    bool useDetents = false;
};

// Modulation amounts on pitch destinations (1 = P::pitchModSemitones): a stop at every octave, the same share of an
// octave as the Coarse knobs' stops.
inline void setPitchAmountDetents (DetentSlider& s)
{
    const double octave = 12.0 / P::pitchModSemitones;
    s.setDetents (octave, octave * 0.4);
}

// Right-click window for a knob: type an exact value, and map the knob to a macro or LFO
// (each mapping is a row in the modulation matrix).
class KnobOptions : public juce::Component
{
public:
    KnobOptions (APVTS& s, const juce::String& id)
        : state (s), param (s.getParameter (id)), dest (P::destForParam (id))
    {
        title.setText (param->getName (64), juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, Col::accent);
        addAndMakeVisible (title);

        valueLabel.setText ("Value", juce::dontSendNotification);
        valueLabel.setColour (juce::Label::textColourId, Col::dim);
        addAndMakeVisible (valueLabel);
        valueBox.setText (param->getCurrentValueAsText(), false);
        valueBox.setJustification (juce::Justification::centredLeft);
        valueBox.onReturnKey = [this] { applyValue(); closeWindow(); };
        valueBox.onFocusLost = [this] { applyValue(); };
        addAndMakeVisible (valueBox);
        resetButton.setButtonText ("Reset to default");
        resetButton.onClick = [this]
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
            valueBox.setText (param->getCurrentValueAsText(), false);
            closeWindow();
        };
        addAndMakeVisible (resetButton);
        hint.setText ("type a value, then press Enter", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Col::dim);
        hint.setFont (juce::Font (juce::FontOptions (10.5f)));
        addAndMakeVisible (hint);

        if (dest != P::DstNone)
        {
            mapLabel.setText ("Modulate with", juce::dontSendNotification);
            mapLabel.setColour (juce::Label::textColourId, Col::dim);
            addAndMakeVisible (mapLabel);
            const std::pair<const char*, int> sources[] { { "Macro 1", P::SrcMacro1 }, { "Macro 2", P::SrcMacro2 },
                                                          { "Mod 1", P::SrcMod1 }, { "Mod 2", P::SrcMod2 }, { "S&H", P::SrcSH },
                                                          { "Amp Env", P::SrcAmpEnv } };
            for (int i = 0; i < numMapButtons; ++i)
            {
                mapButtons[i].setButtonText (sources[i].first);
                const int src = sources[i].second;
                mapButtons[i].onClick = [this, src] { addMapping (src); };
                addAndMakeVisible (mapButtons[i]);
            }
            status.setColour (juce::Label::textColourId, Col::warn);
            status.setFont (juce::Font (juce::FontOptions (11.0f)));
            addAndMakeVisible (status);
        }
        buildRows();

        juce::Component::SafePointer<KnobOptions> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) { safe->valueBox.grabKeyboardFocus(); safe->valueBox.selectAll(); }
        });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        auto vr = r.removeFromTop (24);
        valueLabel.setBounds (vr.removeFromLeft (44));
        valueBox.setBounds (vr);
        hint.setBounds (r.removeFromTop (16).withTrimmedLeft (44));
        r.removeFromTop (4);
        resetButton.setBounds (r.removeFromTop (24).withTrimmedLeft (44).removeFromLeft (130));
        if (dest == P::DstNone) return;
        r.removeFromTop (8);
        mapLabel.setBounds (r.removeFromTop (18));
        auto br = r.removeFromTop (24);
        const int bw = (br.getWidth() - 4 * (numMapButtons - 1)) / numMapButtons;
        for (auto& b : mapButtons) { b.setBounds (br.removeFromLeft (bw)); br.removeFromLeft (4); }
        r.removeFromTop (4);
        status.setBounds (r.removeFromTop (status.getText().isEmpty() ? 0 : 18));
        for (auto& row : rows)
        {
            auto rr = r.removeFromTop (26);
            row->name.setBounds (rr.removeFromLeft (80));
            row->remove.setBounds (rr.removeFromRight (60));
            rr.removeFromRight (4);
            row->amount.setBounds (rr);
            r.removeFromTop (2);
        }
    }

private:
    struct Row
    {
        juce::Label name;
        DetentSlider amount;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
        juce::TextButton remove { "Remove" };
    };

    float rawValue (const juce::String& id) const { return state.getRawParameterValue (id)->load(); }

    void setRaw (const juce::String& id, float value)
    {
        auto* p = state.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }

    void applyValue()
    {
        const auto text = valueBox.getText().trim();
        if (text.isNotEmpty() && text != param->getCurrentValueAsText())
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getValueForText (text));
            param->endChangeGesture();
        }
        valueBox.setText (param->getCurrentValueAsText(), false);
    }

    void closeWindow()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    void addMapping (int src)
    {
        for (int i = 0; i < P::numMatrixSlots; ++i)
            if (rawValue (P::mx (i, "src")) < 0.5f && rawValue (P::mx (i, "dst")) < 0.5f)
            {
                setRaw (P::mx (i, "src"), (float) src);
                setRaw (P::mx (i, "dst"), (float) dest);
                // Sources run 0..1 and push the knob up, so the amount starts at what is left above the knob: at the
                // source's peak the knob just reaches its end. (Pitch destinations add semitones instead and keep 1.)
                setRaw (P::mx (i, "amt"), P::isPitchDest (dest) ? 1.0f : 1.0f - param->getValue());
                status.setText ({}, juce::dontSendNotification);
                buildRows();
                return;
            }
        status.setText ("All 8 matrix rows are in use. Free one in the Matrix tab.", juce::dontSendNotification);
        buildRows();
    }

    void removeMapping (int slot)
    {
        setRaw (P::mx (slot, "src"), 0.0f);
        setRaw (P::mx (slot, "dst"), 0.0f);
        setRaw (P::mx (slot, "amt"), 0.0f);
        // Removing a mapping closes the window (asynchronously: the clicked button belongs to this window)
        juce::Component::SafePointer<KnobOptions> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeWindow(); });
    }

    void buildRows()
    {
        rows.clear();
        if (dest != P::DstNone)
            for (int i = 0; i < P::numMatrixSlots; ++i)
            {
                const int src = (int) std::lround (rawValue (P::mx (i, "src")));
                if (src == P::SrcNone || (int) std::lround (rawValue (P::mx (i, "dst"))) != dest) continue;
                auto row = std::make_unique<Row>();
                row->name.setText (P::modSources[src], juce::dontSendNotification);
                row->amount.setSliderStyle (juce::Slider::LinearHorizontal);
                row->amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
                row->amount.setColour (juce::Slider::trackColourId, Col::mod);
                row->amount.setColour (juce::Slider::thumbColourId, Col::mod);
                row->attachment = std::make_unique<APVTS::SliderAttachment> (state, P::mx (i, "amt"), row->amount);
                setupAmountText (row->amount, state, i);
                row->amount.setDoubleClickReturnValue (true, 1.0);
                if (P::isPitchDest (dest)) setPitchAmountDetents (row->amount);
                row->remove.onClick = [this, i] { removeMapping (i); };
                addAndMakeVisible (row->name);
                addAndMakeVisible (row->amount);
                addAndMakeVisible (row->remove);
                rows.push_back (std::move (row));
            }
        const int h = dest == P::DstNone ? 112 : 178 + (status.getText().isEmpty() ? 0 : 18) + (int) rows.size() * 28;
        setSize (360, h);
        resized();
    }

    APVTS& state;
    juce::RangedAudioParameter* param;
    int dest;
    juce::Label title, valueLabel, hint, mapLabel, status;
    juce::TextEditor valueBox;
    juce::TextButton resetButton;
    static constexpr int numMapButtons = 6;
    juce::TextButton mapButtons[numMapButtons];
    std::vector<std::unique_ptr<Row>> rows;
};

// Small box for typing a knob's value: Enter applies it and closes, Escape closes without changing it.
class ValueEntry : public juce::Component
{
public:
    explicit ValueEntry (juce::RangedAudioParameter& p) : param (p)
    {
        editor.setText (param.getCurrentValueAsText(), false);
        editor.setJustification (juce::Justification::centred);
        editor.onReturnKey = [this] { apply(); close(); };
        editor.onEscapeKey = [this] { close(); };
        addAndMakeVisible (editor);
        setSize (120, 30);
        juce::Component::SafePointer<ValueEntry> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) { safe->editor.grabKeyboardFocus(); safe->editor.selectAll(); }
        });
    }

    void resized() override { editor.setBounds (getLocalBounds().reduced (3)); }

    void apply()
    {
        const auto text = editor.getText().trim();
        if (text.isEmpty()) return;
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.getValueForText (text));
        param.endChangeGesture();
    }

    juce::TextEditor editor;

private:
    void close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    juce::RangedAudioParameter& param;
};

// Slider that reports right-clicks (options window) and double-clicks (type a value) instead of
// dragging or resetting. Alt/Option-click still resets to the default value.
class KnobSlider : public DetentSlider
{
public:
    std::function<void()> onRightClick, onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }

};

// Small "modulated" badge on a knob. Dragging it (up/right = more) scales the knob's modulation amounts.
class ModBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onDragStart, onDragEnd;
    std::function<void (float, bool)> onDrag;   // total change in modulation amount since the drag started; true = fine (Shift)
    float amountPerPixel = 0.01f;               // 200 px of travel covers the whole -100%..+100% range

    ModBadge()
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip ("Modulated: drag up or down to change the modulation amount");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Col::bg);
        g.fillEllipse (r);
        g.setColour (Col::mod.withAlpha (isMouseOverOrDragging() ? 1.0f : 0.85f));
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        // a small wave
        juce::Path wave;
        const auto c = r.getCentre();
        const float w = r.getWidth() * 0.32f, h = r.getHeight() * 0.18f;
        for (int i = 0; i <= 12; ++i)
        {
            const float t = (float) i / 12.0f;
            const juce::Point<float> p (c.x - w + 2.0f * w * t, c.y - h * std::sin (t * juce::MathConstants<float>::twoPi));
            if (i == 0) wave.startNewSubPath (p); else wave.lineTo (p);
        }
        g.strokePath (wave, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onDragStart) onDragStart(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        // up/right = more; Shift for fine control (5x finer)
        const float px = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
        const bool fine = e.mods.isShiftDown();
        if (onDrag) onDrag (px * amountPerPixel * (fine ? 0.2f : 1.0f), fine);
    }
    void mouseUp (const juce::MouseEvent&) override { if (onDragEnd) onDragEnd(); }
};

class Knob : public juce::Component,
             private juce::Timer
{
public:
    Knob (APVTS& s, const juce::String& id, const juce::String& name)
        : state (s), paramID (id), labelText (name), dest (P::destForParam (id))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, nullptr, 800);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.onRightClick = [this] { showOptions(); };
        slider.onDoubleClick = [this] { showValueEntry(); };
        slider.setTooltip ("Double-click to type a value. Right-click to map to a macro/LFO. Alt-click to reset");
        addAndMakeVisible (slider);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Col::dim);
        label.setFont (juce::Font (juce::FontOptions (11.0f)));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
        attachment = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
        if (auto* p = s.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

        if (dest != P::DstNone)
        {
            addChildComponent (badge);
            badge.onDragStart = [this] { beginAmountDrag(); };
            badge.onDrag = [this] (float delta, bool fine) { dragAmount (delta, fine); };
            if (P::isPitchDest (dest)) badge.amountPerPixel = (float) badgePitchPerPixel;
            badge.onDragEnd = [this] { endAmountDrag(); };
            refreshModulation();
            startTimerHz (15);   // mappings change from the right-click window and the Matrix page
        }
    }

    ~Knob() override { stopTimer(); }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromBottom (14));
        slider.setBounds (r);
        // badge at the knob's upper right, just outside its ring
        const auto k = knobCircle();
        const int size = 12;
        const int bx = juce::jmin (getWidth() - size, (int) (k.getRight() - 2.0f));
        const int by = juce::jmax (0, (int) (k.getY() - 3.0f));
        badge.setBounds (bx, by, size, size);
    }

    // Modulation ring: from the top of the knob, clockwise for positive amounts and anticlockwise for negative
    // ones; a full circle is 100%.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (std::abs (modTotal) < 0.001f) return;
        const auto k = knobCircle();
        const auto c = k.getCentre();
        const float radius = k.getWidth() * 0.5f + 1.5f;
        const float amount = juce::jlimit (-1.0f, 1.0f, modTotal) * juce::MathConstants<float>::twoPi;
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::jmin (0.0f, amount), juce::jmax (0.0f, amount), true);
        g.setColour (Col::mod);
        g.strokePath (ring, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void showOptions()
    {
        // Shown inside the plugin window so it stays on top and can take keyboard focus in hosts.
        auto* top = getTopLevelComponent();
        juce::CallOutBox::launchAsynchronously (std::make_unique<KnobOptions> (state, paramID),
                                                top->getLocalArea (&slider, slider.getLocalBounds()), top);
    }

    void showValueEntry()
    {
        if (auto* p = state.getParameter (paramID))
        {
            auto* top = getTopLevelComponent();
            juce::CallOutBox::launchAsynchronously (std::make_unique<ValueEntry> (*p), top->getLocalArea (&slider, slider.getLocalBounds()), top);
        }
    }

    KnobSlider slider;
    juce::Label label;

private:
    // The knob's circle inside the slider (same geometry as ElukkaLookAndFeel::drawRotarySlider)
    juce::Rectangle<float> knobCircle() const
    {
        auto b = slider.getBounds().toFloat().reduced (3.0f);
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        return b.withSizeKeepingCentre (size, size);
    }

    float raw (const juce::String& id) const { return state.getRawParameterValue (id)->load(); }

    // matrix slots that modulate this knob
    std::vector<int> mappedSlots() const
    {
        std::vector<int> v;
        for (int i = 0; i < P::numMatrixSlots; ++i)
            if ((int) std::lround (raw (P::mx (i, "src"))) != P::SrcNone && (int) std::lround (raw (P::mx (i, "dst"))) == dest)
                v.push_back (i);
        return v;
    }

    void refreshModulation()
    {
        float total = 0.0f;
        const auto slots = mappedSlots();
        for (int i : slots) total += raw (P::mx (i, "amt"));
        const bool mapped = ! slots.empty();
        if (mapped != badge.isVisible()) badge.setVisible (mapped);
        if (std::abs (total - modTotal) > 1.0e-4f)
        {
            modTotal = total;
            repaint();
        }
    }

    void timerCallback() override { if (dragSlots.empty()) refreshModulation(); }

    juce::String amountText (float v) const
    {
        const juce::String sign = v > 0.0f ? "+" : "";
        if (P::isPitchDest (dest)) return sign + juce::String (v * P::pitchModSemitones, 1) + " st";
        return sign + juce::String (v * 100.0f, 0) + "%";
    }

    void beginAmountDrag()
    {
        dragSlots = mappedSlots();
        dragStart.clear();
        for (int i : dragSlots)
        {
            dragStart.push_back (raw (P::mx (i, "amt")));
            state.getParameter (P::mx (i, "amt"))->beginChangeGesture();
        }
    }

    // Scales all of the knob's modulation amounts so their total moves by delta (keeping their balance); if they
    // cancel out (total near zero) the change is shared equally instead.
    void dragAmount (float delta, bool fine)
    {
        if (dragSlots.empty()) return;
        float startTotal = 0.0f;
        for (float a : dragStart) startTotal += a;
        float moved = startTotal + delta;
        // pitch destinations: the total stops at every octave (unless Shift is held for fine control)
        if (P::isPitchDest (dest) && ! fine)
            moved = (float) DetentSlider::detentValue (DetentSlider::detentPosition (startTotal, badgeOctave, badgePitchStop) + delta,
                                                       badgeOctave, badgePitchStop);
        const float target = juce::jlimit (-1.0f * (float) dragSlots.size(), 1.0f * (float) dragSlots.size(), moved);
        float total = 0.0f;
        for (size_t k = 0; k < dragSlots.size(); ++k)
        {
            const float a = std::abs (startTotal) > 0.02f ? dragStart[k] * target / startTotal
                                                          : dragStart[k] + (target - startTotal) / (float) dragSlots.size();
            auto* p = state.getParameter (P::mx (dragSlots[k], "amt"));
            const float clamped = juce::jlimit (-1.0f, 1.0f, a);
            p->setValueNotifyingHost (p->convertTo0to1 (clamped));
            total += clamped;
        }
        modTotal = total;
        label.setText (amountText (total), juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, Col::mod);
        repaint();
    }

    void endAmountDrag()
    {
        for (int i : dragSlots) state.getParameter (P::mx (i, "amt"))->endChangeGesture();
        dragSlots.clear();
        label.setText (labelText, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, Col::dim);
        refreshModulation();
    }

    APVTS& state;
    juce::String paramID, labelText;
    int dest;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    ModBadge badge;
    float modTotal = 0.0f;
    std::vector<int> dragSlots;
    // Badge drag on pitch destinations: each octave of the total amount (12 / 48 = 0.25) holds for 40 px and the other
    // semitones take 2 px each (62 px per octave): amount per pixel = 0.25 / 62, the stop = 38 px of it.
    static constexpr double badgeOctave = 12.0 / P::pitchModSemitones, badgePitchPerPixel = badgeOctave / 62.0,
                            badgePitchStop = 38.0 * badgePitchPerPixel;
    std::vector<float> dragStart;
};

class Choice : public juce::Component
{
public:
    Choice (APVTS& s, const juce::String& id, const juce::String& name)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            box.addItemList (p->choices, 1);
        addAndMakeVisible (box);
        label.setText (name, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, Col::dim);
        label.setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (label);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (label.getText().isNotEmpty()) label.setBounds (r.removeFromTop (14));
        box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (24, r.getHeight())));
    }

    juce::ComboBox box;
    juce::Label label;

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

class Toggle : public juce::Component
{
public:
    Toggle (APVTS& s, const juce::String& id, const juce::String& name)
    {
        button.setButtonText (name);
        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (s, id, button);
    }
    void resized() override { button.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (24, getHeight()))); }
    juce::ToggleButton button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Rounded section background with a title.
class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& t) : title (t) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Col::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Col::border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (Col::accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (title, 10, 6, getWidth() - 20, 16, juce::Justification::centredLeft);
    }
    // Area below the title
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8).withTrimmedTop (20); }
    // Same area in the parent's coordinates, for laying out sibling components on top of the panel
    juce::Rectangle<int> contentInParent() const { return content() + getPosition(); }
    juce::String title;
};

// Lays out components left to right with fixed width.
inline void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int itemWidth, int gap = 4)
{
    for (auto* c : items)
    {
        if (c != nullptr) c->setBounds (area.removeFromLeft (itemWidth));
        area.removeFromLeft (gap);
    }
}
