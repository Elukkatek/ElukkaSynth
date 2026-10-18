#include "Pages.h"

//==============================================================================
// ShapeEditor
juce::Point<float> ShapeEditor::toScreen (float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f).withTrimmedLeft (28.0f);
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

juce::Point<float> ShapeEditor::fromScreen (juce::Point<float> p) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f).withTrimmedLeft (28.0f);
    return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()),
             juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()) };
}

int ShapeEditor::hitPoint (juce::Point<float> p) const
{
    for (int i = 0; i < (int) pts.size(); ++i)
        if (toScreen (pts[(size_t) i].x, pts[(size_t) i].y).getDistanceFrom (p) < 7.0f)
            return i;
    return -1;
}

void ShapeEditor::commit()
{
    proc.shapes[index].setPoints (pts);
    pts = proc.shapes[index].getPoints();
    repaint();
}

void ShapeEditor::paint (juce::Graphics& g)
{
    auto& shape = proc.shapes[index];
    if (dragPoint < 0 && dragSegment < 0) pts = shape.getPoints();

    auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    // 16 x 8 grid: stronger lines every quarter in time and every 50% in level
    for (int i = 0; i <= gridX; ++i)
    {
        auto a = toScreen ((float) i / gridX, 0.0f), b = toScreen ((float) i / gridX, 1.0f);
        g.setColour (i % 4 == 0 ? Col::border.brighter (0.6f) : Col::border.brighter (0.15f));
        g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
    }
    for (int j = 0; j <= gridY; ++j)
    {
        auto a = toScreen (0.0f, (float) j / gridY), b = toScreen (1.0f, (float) j / gridY);
        g.setColour (j % 4 == 0 ? Col::border.brighter (0.6f) : Col::border.brighter (0.15f));
        g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
    }
    g.setColour (Col::dim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int pct : { 0, 50, 100 })
    {
        auto p = toScreen (0.0f, (float) pct / 100.0f);
        g.drawText (juce::String (pct) + "%", juce::Rectangle<float> (2.0f, p.y - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
    }

    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        float x = (float) i / 200.0f;
        auto s = toScreen (x, shape.evalPoints (x));
        if (i == 0) p.startNewSubPath (s); else p.lineTo (s);
    }
    juce::Path fill (p);
    fill.lineTo (toScreen (1.0f, 0.0f));
    fill.lineTo (toScreen (0.0f, 0.0f));
    fill.closeSubPath();
    g.setColour (Col::mod.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (Col::mod);
    g.strokePath (p, juce::PathStrokeType (1.8f));

    for (auto& pt : pts)
    {
        auto s = toScreen (pt.x, pt.y);
        g.setColour (Col::bg);
        g.fillEllipse (s.x - 4.5f, s.y - 4.5f, 9.0f, 9.0f);
        g.setColour (Col::text);
        g.drawEllipse (s.x - 4.5f, s.y - 4.5f, 9.0f, 9.0f, 1.5f);
    }
}

void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    pts = proc.shapes[index].getPoints();
    dragPoint = hitPoint (e.position);
    dragSegment = -1;
    if (dragPoint < 0)
    {
        auto pos = fromScreen (e.position);
        for (int i = 0; i + 1 < (int) pts.size(); ++i)
            if (pos.x >= pts[(size_t) i].x && pos.x <= pts[(size_t) i + 1].x)
            {
                dragSegment = i;
                dragStartCurve = pts[(size_t) i].curve;
                dragStartY = e.position.y;
                break;
            }
    }
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPoint >= 0)
    {
        auto pos = snapped (fromScreen (e.position));
        auto& pt = pts[(size_t) dragPoint];
        const int last = (int) pts.size() - 1;
        if (dragPoint == 0) pos.x = 0.0f;
        else if (dragPoint == last) pos.x = 1.0f;
        else pos.x = juce::jlimit (pts[(size_t) dragPoint - 1].x + 0.001f, pts[(size_t) dragPoint + 1].x - 0.001f, pos.x);
        pt.x = pos.x;
        pt.y = pos.y;
        commit();
    }
    else if (dragSegment >= 0)
    {
        // Vertical drag on a segment bends it.
        auto& a = pts[(size_t) dragSegment];
        auto& b = pts[(size_t) dragSegment + 1];
        float dir = b.y >= a.y ? -1.0f : 1.0f;
        a.curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve + dir * (e.position.y - dragStartY) / 80.0f);
        commit();
    }
}

void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    pts = proc.shapes[index].getPoints();
    int hit = hitPoint (e.position);
    if (hit > 0 && hit < (int) pts.size() - 1)
    {
        pts.erase (pts.begin() + hit);
        pts[(size_t) hit - 1].curve = 0.0f;   // the joined segment is a straight line
    }
    else if (hit < 0)
    {
        auto pos = snapped (fromScreen (e.position));
        pts.push_back ({ pos.x, pos.y, 0.0f });
    }
    dragPoint = dragSegment = -1;
    commit();
}

//==============================================================================
// HarmTableEditor
void HarmTableEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::panel);
    g.fillRoundedRectangle (r.reduced (0.5f), 6.0f);
    g.setColour (Col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (bipolar ? Col::mod : Col::accent);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText (name, 10, 4, getWidth() - 20, 16, juce::Justification::centredLeft);

    auto p = plot();
    g.setColour (Col::bg);
    g.fillRect (p);
    const float bw = p.getWidth() / (float) P::numHarmTable;
    const float zeroY = bipolar ? p.getCentreY() : p.getBottom();
    g.setColour (Col::border);
    g.drawHorizontalLine ((int) zeroY, p.getX(), p.getRight());

    for (int i = 0; i < P::numHarmTable; ++i)
    {
        float v = table[(size_t) i];
        float x = p.getX() + (float) i * bw;
        float y = bipolar ? p.getCentreY() - v * p.getHeight() * 0.5f : p.getBottom() - v * p.getHeight();
        auto bar = juce::Rectangle<float>::leftTopRightBottom (x + 1.0f, juce::jmin (y, zeroY), x + bw - 1.0f, juce::jmax (y, zeroY));
        auto c = bipolar ? Col::mod : Col::accent;
        g.setColour (c.withAlpha ((i % 8 == 0) ? 0.95f : 0.7f));
        g.fillRect (bar);
    }
    g.setColour (Col::dim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int h : { 1, 8, 16, 32, 48, 64 })
        g.drawText (juce::String (h), (int) (p.getX() + (h - 1) * bw), (int) p.getBottom() - 12, 20, 12, juce::Justification::left);
}

void HarmTableEditor::edit (const juce::MouseEvent& e)
{
    auto p = plot();
    const float bw = p.getWidth() / (float) P::numHarmTable;
    int idx = juce::jlimit (0, P::numHarmTable - 1, (int) ((e.position.x - p.getX()) / bw));
    float v = bipolar ? juce::jlimit (-1.0f, 1.0f, (p.getCentreY() - e.position.y) / (p.getHeight() * 0.5f))
                      : juce::jlimit (0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight());

    // Fill bars between drag positions so fast strokes leave no gaps.
    if (lastIndex >= 0 && lastIndex != idx)
    {
        int step = idx > lastIndex ? 1 : -1;
        for (int i = lastIndex + step; i != idx; i += step)
        {
            float t = (float) (i - lastIndex) / (float) (idx - lastIndex);
            table[(size_t) i] = lastValue + (v - lastValue) * t;
        }
    }
    table[(size_t) idx] = v;
    lastIndex = idx;
    lastValue = v;
    repaint();
}

void HarmTableEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto p = plot();
    const float bw = p.getWidth() / (float) P::numHarmTable;
    int idx = juce::jlimit (0, P::numHarmTable - 1, (int) ((e.position.x - p.getX()) / bw));
    table[(size_t) idx] = bipolar ? 0.0f : 1.0f;
    repaint();
}

//==============================================================================
// SpectrogramView
void SpectrogramView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto data = proc.getResynth();
    if (data == nullptr)
    {
        g.setColour (Col::dim);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("Drop an audio file or image here, or use the Load buttons", getLocalBounds(), juce::Justification::centred);
        return;
    }
    auto inner = getLocalBounds().reduced (4);
    if (! cached.isValid() || cached.getWidth() != inner.getWidth() || cached.getHeight() != inner.getHeight())
        cached = data->renderPreview (inner.getWidth(), inner.getHeight(), Col::mod);
    g.drawImageAt (cached, inner.getX(), inner.getY());
    g.setColour (Col::dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (data->isImage ? "harmonics (bottom = fundamental)" : "frequency (log)", inner.reduced (6), juce::Justification::topLeft);
    g.drawText ("time", inner.reduced (6), juce::Justification::bottomRight);

    // Playback position of the newest note
    if (playhead >= 0.0f)
    {
        const float x = (float) inner.getX() + juce::jlimit (0.0f, 1.0f, playhead) * (float) inner.getWidth();
        g.setColour (Col::accent.withAlpha (0.25f));
        g.fillRect (x - 3.0f, (float) inner.getY(), 6.0f, (float) inner.getHeight());
        g.setColour (Col::accent);
        g.drawLine (x, (float) inner.getY(), x, (float) inner.getBottom(), 2.0f);
    }
}

//==============================================================================
// SeqGrid
void SeqGrid::setPitch (int idx, int semis)
{
    auto& pat = proc.pattern;
    pat.pitches[(size_t) idx].semis = juce::jlimit (-PitchEntry::range, PitchEntry::range, semis);
    if (idx >= pat.numPitches) { pat.numPitches = idx + 1; proc.bumpUiVersion(); }
    repaint();
}

namespace
{
// Type a step's pitch: semitones from the root (-36..+36); Enter applies, Escape cancels.
class StepPitchEntry : public juce::Component
{
public:
    StepPitchEntry (int value, std::function<void (int)> apply) : onApply (std::move (apply))
    {
        editor.setText ((value > 0 ? "+" : "") + juce::String (value), false);
        editor.setJustification (juce::Justification::centred);
        editor.setInputRestrictions (4, "+-0123456789");
        editor.onReturnKey = [this]
        {
            const auto t = editor.getText().trim();
            if (t.isNotEmpty() && t != "+" && t != "-") onApply (t.getIntValue());
            close();
        };
        editor.onEscapeKey = [this] { close(); };
        addAndMakeVisible (editor);
        hint.setText ("semitones, -36..+36", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Col::dim);
        hint.setFont (juce::Font (juce::FontOptions (10.5f)));
        hint.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (hint);
        setSize (130, 50);
        juce::Component::SafePointer<StepPitchEntry> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.grabKeyboardFocus(); safe->editor.selectAll(); } });
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (3);
        editor.setBounds (r.removeFromTop (26));
        hint.setBounds (r);
    }

private:
    void close() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
    std::function<void (int)> onApply;
    juce::TextEditor editor;
    juce::Label hint;
};
}

void SeqGrid::typePitch (int idx, juce::Rectangle<int> near)
{
    auto* top = getTopLevelComponent();
    juce::Component::SafePointer<SeqGrid> safe (this);
    juce::CallOutBox::launchAsynchronously (std::make_unique<StepPitchEntry> (proc.pattern.pitches[(size_t) idx].semis,
                                                                              [safe, idx] (int v) { if (safe != nullptr) safe->setPitch (idx, v); }),
                                            top->getLocalArea (this, near), top);
}

SeqGrid::Areas SeqGrid::areas() const
{
    Areas a;
    auto r = getLocalBounds().toFloat();
    a.labelW = 64.0f;
    a.time = r.removeFromTop (30.0f);
    r.removeFromTop (8.0f);
    a.slide = r.removeFromBottom (22.0f);
    r.removeFromBottom (4.0f);
    a.accent = r.removeFromBottom (22.0f);
    r.removeFromBottom (8.0f);
    a.roll = r;
    a.colW = (r.getWidth() - a.labelW) / 16.0f;
    return a;
}

void SeqGrid::paint (juce::Graphics& g)
{
    auto a = areas();
    auto& pat = proc.pattern;
    const int playing = proc.seq.displayStep.load();
    const bool seqOn = proc.apvts.getRawParameterValue ("sq_on")->load() > 0.5f;

    // Which pitch entry is playing
    int playingPitch = -1;
    if (playing >= 0 && playing < SeqPattern::maxSteps && pat.steps[(size_t) playing] == StepType::Note)
    {
        int n = 0;
        for (int s = 0; s < playing; ++s) if (pat.steps[(size_t) s] == StepType::Note) ++n;
        playingPitch = n % juce::jmax (1, pat.numPitches);
    }

    auto label = [&] (juce::Rectangle<float> lane, const juce::String& t)
    {
        g.setColour (Col::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (t, lane.withWidth (a.labelW - 6.0f), juce::Justification::centredRight);
    };
    auto col = [&] (juce::Rectangle<float> lane, int j) { return juce::Rectangle<float> (lane.getX() + a.labelW + j * a.colW, lane.getY(), a.colW, lane.getHeight()); };

    // TIME lane
    label (a.time, "TIME");
    for (int j = 0; j < 16; ++j)
    {
        const int step = bar * 16 + j;
        auto c = col (a.time, j).reduced (2.0f);
        const bool active = step < pat.length;
        const auto type = pat.steps[(size_t) step];
        g.setColour (step == playing && seqOn ? Col::accent.withAlpha (0.35f) : (j % 4 == 0 ? Col::panelHi.brighter (0.05f) : Col::panelHi));
        g.fillRoundedRectangle (c, 3.0f);
        if (! active) { g.setColour (Col::bg.withAlpha (0.7f)); g.fillRoundedRectangle (c, 3.0f); }
        if (step == pat.startStep)
        {
            // start marker: a small triangle in the corner of the start step
            juce::Path tri;
            tri.addTriangle (c.getX() + 2.0f, c.getY() + 2.0f, c.getX() + 11.0f, c.getY() + 2.0f, c.getX() + 2.0f, c.getY() + 11.0f);
            g.setColour (Col::warn);
            g.fillPath (tri);
        }
        g.setColour (active ? Col::text : Col::dim.withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        if (type == StepType::Note) g.fillEllipse (c.withSizeKeepingCentre (9.0f, 9.0f));
        else if (type == StepType::Tie) g.fillRect (c.withSizeKeepingCentre (c.getWidth() - 8.0f, 3.0f));
        else g.drawText ("rest", c, juce::Justification::centred);
    }

    // PITCH lane: bars up or down from the root line
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float z = zeroY (a), u = unitY (a);
    const float x0 = a.roll.getX() + a.labelW, x1 = a.roll.getRight();
    g.setColour (Col::bg);
    g.fillRect (juce::Rectangle<float> (x0, a.roll.getY(), x1 - x0, a.roll.getHeight()));
    label (juce::Rectangle<float> (a.roll.getX(), z - 0.5f * PitchEntry::range * u - 10.0f, a.labelW, 20.0f), "PITCH");
    for (int oct = -3; oct <= 3; ++oct)
    {
        const float y = z - (float) (oct * 12) * u;
        g.setColour (oct == 0 ? Col::dim.withAlpha (0.8f) : Col::border);
        g.drawLine (x0, y, x1, y, oct == 0 ? 1.5f : 1.0f);
        g.setColour (Col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        const juce::String t = oct == 0 ? "root" : (oct > 0 ? "+" : "") + juce::String (oct) + " oct";
        g.drawText (t, juce::Rectangle<float> (a.roll.getX(), y - 7.0f, a.labelW - 6.0f, 14.0f), juce::Justification::centredRight);
    }
    for (int j = 0; j <= 16; ++j)
    {
        const float x = x0 + j * a.colW;
        g.setColour (j % 4 == 0 ? Col::border.brighter (0.2f) : Col::border.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, a.roll.getY(), a.roll.getBottom());
    }
    for (int j = 0; j < 16; ++j)
    {
        const int idx = bar * 16 + j;
        const bool active = idx < pat.numPitches;
        const auto& e = pat.pitches[(size_t) idx];
        const float tip = z - (float) e.semis * u;
        const float left = x0 + j * a.colW + 5.0f, w = a.colW - 10.0f;
        auto barRect = e.semis == 0 ? juce::Rectangle<float> (left, z - 2.0f, w, 4.0f)
                                    : juce::Rectangle<float> (left, juce::jmin (z, tip), w, std::abs (tip - z));
        auto c = idx == playingPitch && seqOn ? Col::text : (e.accent ? Col::warn : Col::accent);
        g.setColour (active ? c : Col::dim.withAlpha (0.35f));
        g.fillRoundedRectangle (barRect, 2.0f);
        // value and note name at the tip
        const bool up = e.semis >= 0;
        auto txt = juce::Rectangle<float> (x0 + j * a.colW, up ? barRect.getY() - 26.0f : barRect.getBottom() + 1.0f, a.colW, 25.0f);
        g.setColour (active ? Col::text : Col::dim.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ((e.semis > 0 ? "+" : "") + juce::String (e.semis), txt.removeFromTop (13.0f), juce::Justification::centred);
        g.setColour (active ? Col::dim : Col::dim.withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (names[((e.semis % 12) + 12) % 12], txt, juce::Justification::centredTop);
    }

    // ACCENT / SLIDE lanes
    auto toggles = [&] (juce::Rectangle<float> lane, const juce::String& t, bool PitchEntry::* field, juce::Colour on)
    {
        label (lane, t);
        for (int j = 0; j < 16; ++j)
        {
            const int idx = bar * 16 + j;
            auto c = col (lane, j).reduced (3.0f, 2.0f);
            const bool v = pat.pitches[(size_t) idx].*field;
            g.setColour (v ? on : Col::panelHi);
            g.fillRoundedRectangle (c, 3.0f);
            if (idx >= pat.numPitches) { g.setColour (Col::bg.withAlpha (0.7f)); g.fillRoundedRectangle (c, 3.0f); }
        }
    };
    toggles (a.accent, "ACCENT", &PitchEntry::accent, Col::warn);
    toggles (a.slide, "SLIDE", &PitchEntry::slide, Col::mod);
}

void SeqGrid::mouseDown (const juce::MouseEvent& e)
{
    auto a = areas();
    auto& pat = proc.pattern;
    const int j = (int) ((e.position.x - a.time.getX() - a.labelW) / a.colW);
    dragMode = 0;
    if (j < 0 || j > 15) return;
    const int idx = bar * 16 + j;

    if (a.time.contains (e.position))
    {
        auto& s = pat.steps[(size_t) idx];
        s = s == StepType::Note ? StepType::Tie : (s == StepType::Tie ? StepType::Rest : StepType::Note);
        if (idx >= pat.length) pat.length = idx + 1;
        proc.bumpUiVersion();
    }
    else if (a.accent.contains (e.position))
    {
        dragMode = 2;
        dragSetTo = ! pat.pitches[(size_t) idx].accent;
        pat.pitches[(size_t) idx].accent = dragSetTo;
    }
    else if (a.slide.contains (e.position))
    {
        dragMode = 3;
        dragSetTo = ! pat.pitches[(size_t) idx].slide;
        pat.pitches[(size_t) idx].slide = dragSetTo;
    }
    else if (a.roll.contains (e.position))
    {
        // drag to draw pitches; a click without dragging opens the type-in box (on mouse up)
        dragMode = 1;
        pitchDragged = false;
        downIndex = idx;
    }
    repaint();
}

void SeqGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == 1 && ! pitchDragged && downIndex >= 0)
    {
        auto a = areas();
        const int j = downIndex - bar * 16;
        typePitch (downIndex, juce::Rectangle<float> (a.roll.getX() + a.labelW + j * a.colW, zeroY (a) - 10.0f, a.colW, 20.0f).toNearestInt());
    }
    dragMode = 0;
    downIndex = -1;
}

void SeqGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto a = areas();
    auto& pat = proc.pattern;
    const int j = juce::jlimit (0, 15, (int) ((e.position.x - a.roll.getX() - a.labelW) / a.colW));
    const int idx = bar * 16 + j;
    if (dragMode == 1)
    {
        if (! pitchDragged && e.getDistanceFromDragStart() < 4) return;
        pitchDragged = true;
        setPitch (idx, juce::roundToInt ((zeroY (a) - e.position.y) / unitY (a)));
    }
    else if (dragMode == 2) pat.pitches[(size_t) idx].accent = dragSetTo;
    else if (dragMode == 3) pat.pitches[(size_t) idx].slide = dragSetTo;
    repaint();
}

//==============================================================================
// OscPanel
OscPanel::OscPanel (APVTS& s, int i)
    : Panel ("OSCILLATOR " + juce::String (i + 1)),
      on (s, P::osc (i, "on"), "On"),
      wave (s, P::osc (i, "wave"), "Wave"),
      fmSrc (s, P::osc (i, "fmSrc"), "FM From"),
      level (s, P::osc (i, "level"), "Level"),
      coarse (s, P::osc (i, "coarse"), "Coarse"),
      fine (s, P::osc (i, "fine"), "Fine"),
      rand (s, P::osc (i, "rand"), "Random Phase"),
      voices (s, P::osc (i, "uniVoices"), "Unison"),
      detune (s, P::osc (i, "uniDetune"), "Detune"),
      blend (s, P::osc (i, "uniBlend"), "Blend"),
      width (s, P::osc (i, "uniWidth"), "Width"),
      fmAmt (s, P::osc (i, "fmAmt"), "FM Amt"),
      pan (s, P::osc (i, "pan"), "Pan"),
      bassMono (s, P::osc (i, "bassMono"), "Bass Mono"),
      phaseMode (s, P::osc (i, "phaseMode"), "Phase Mode"),
      uniMode (s, P::osc (i, "uniMode"), "Detune Mode")
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &wave, &fmSrc, &level, &coarse, &fine, &rand, &phaseMode, &uniMode,
                                                              &voices, &detune, &blend, &width, &fmAmt, &pan, &bassMono })
        addAndMakeVisible (c);
    pan.slider.setTooltip ("Moves the oscillator's whole stereo image left or right");
    // a stop at every octave: 18 px of drag, with the other semitones keeping 2 px each (a longer full sweep, 320 px)
    coarse.slider.setDetents (12.0, 4.8);
    coarse.slider.setMouseDragSensitivity (320);
    bassMono.button.setTooltip ("Below Bass Freq (Synth page, Voice panel) play one centred, undetuned voice instead of the "
                                "unison spread: a solid mono low end that stays centred whatever Pan is set to");
    on.button.setTooltip ("Turn this oscillator on or off");
    rand.slider.setTooltip ("Phase Mode Random: how random each voice's start phase is (100% = fully random). "
                            "Spread: how far the voices' phases are fanned out. Reset: not used");
    phaseMode.box.setTooltip ("Reset: every voice starts at the same phase on each note (repeatable). Random: random start phases. "
                              "Spread: phases evenly fanned out by Amount");
    uniMode.box.setTooltip ("How detune is spread over the unison voices. Linear: evenly. "
                            "Super: inner voices close to the pitch, outer voices wide (supersaw-style)");
    detune.slider.setTooltip ("Detune of the outermost unison voices, in cents");
    blend.slider.setTooltip ("Level of the outer unison voices relative to the centre voice");
    width.slider.setTooltip ("Stereo spread of the unison voices");
}

void OscPanel::resized()
{
    auto r = content();
    auto top = r.removeFromTop (40);
    on.setBounds (top.removeFromLeft (58).withTrimmedTop (14));
    top.removeFromLeft (6);
    auto half = (top.getWidth() - 6) / 2;
    wave.setBounds (top.removeFromLeft (half));
    top.removeFromLeft (6);
    fmSrc.setBounds (top);
    r.removeFromTop (8);

    const int kw = r.getWidth() / 5;
    layoutRow (r.removeFromTop (66), { &level, &pan, &coarse, &fine, &rand }, kw, 0);
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (66), { &voices, &detune, &blend, &width, &fmAmt }, kw, 0);
    r.removeFromTop (4);
    auto modes = r.removeFromTop (40);
    const int mw = (modes.getWidth() - 12) / 3;
    phaseMode.setBounds (modes.removeFromLeft (mw));
    modes.removeFromLeft (6);
    uniMode.setBounds (modes.removeFromLeft (mw));
    modes.removeFromLeft (6);
    bassMono.setBounds (modes.withTrimmedTop (14));
}

//==============================================================================
// ModPanel
ModPanel::ModPanel (ElukkaAudioProcessor& p, int i)
    : Panel ("MOD " + juce::String (i + 1) + "  (LFO / ENVELOPE)"),
      proc (p), index (i),
      mode (p.apvts, P::mod (i, "mode"), "Mode"),

      sync (p.apvts, P::mod (i, "sync"), "Sync"),
      retrig (p.apvts, P::mod (i, "retrig"), "Retrig"),
      rate (p.apvts, P::mod (i, "rate"), "Rate"),
      syncRate (p.apvts, P::mod (i, "syncRate"), "Rate"),
      start (p.apvts, P::mod (i, "start"), "Start"),
      smooth (p.apvts, P::mod (i, "smooth"), "Smooth"),
      editor (p, i)
{
    for (auto* c : std::initializer_list<juce::Component*> { &mode, &sync, &retrig, &rate, &start, &smooth, &shapePreset, &snapButton, &editor })
        addAndMakeVisible (c);
    addChildComponent (syncRate);
    syncRate.slider.setTooltip ("Synced rate, from 8 bars to 1/32 (D = dotted, T = triplet). Double-click to type, right-click to modulate");
    retrig.button.setTooltip ("On: the LFO restarts on every note. Off: a synced LFO follows the song position "
                              "(starts on bar 1 and jumps with the playhead)");
    updateRateKnob();
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setTooltip ("Snap points to the grid (16 steps x 8 levels)");
    snapButton.onClick = [this] { editor.snap = snapButton.getToggleState(); };
    shapePreset.addItemList (ModShape::presetNames(), 1);
    shapePreset.setTextWhenNothingSelected ("Load shape...");
    shapePreset.onChange = [this]
    {
        int id = shapePreset.getSelectedId();
        if (id <= 0) return;
        proc.shapes[index].setPoints (ModShape::preset (id - 1));
        shapePreset.setSelectedId (0, juce::dontSendNotification);
        editor.repaint();
    };
    editor.setTooltip ("Drag points to move, drag a line to bend it, double-click to add or remove points");
    mode.box.setTooltip ("LFO: loops the shape. S&H Random: new random value each cycle. Envelope: plays the shape once per note");
}

void ModPanel::resized()
{
    auto r = content();
    auto top = r.removeFromTop (40);
    mode.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (6);
    auto toggles = top.withTrimmedTop (14);
    sync.setBounds (toggles.removeFromLeft (62));
    toggles.removeFromLeft (4);
    retrig.setBounds (toggles.removeFromLeft (70));
    r.removeFromTop (6);

    auto left = r.removeFromLeft (64);
    rate.setBounds (left.removeFromTop (66));
    syncRate.setBounds (rate.getBounds());
    left.removeFromTop (4);
    start.setBounds (left.removeFromTop (66));
    left.removeFromTop (4);
    smooth.setBounds (left.removeFromTop (66));
    r.removeFromLeft (6);
    auto shapeRow = r.removeFromTop (22);
    shapePreset.setBounds (shapeRow.removeFromRight (120));
    shapeRow.removeFromRight (6);
    snapButton.setBounds (shapeRow.removeFromRight (56));
    r.removeFromTop (4);
    editor.setBounds (r);
}

void ModPanel::paintOverChildren (juce::Graphics& g)
{
    // Live output meter next to the title
    auto r = juce::Rectangle<float> ((float) getWidth() - 90.0f, 9.0f, 76.0f, 8.0f);
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 3.0f);
    const float v = juce::jlimit (0.0f, 1.0f, shownValue);   // modulation output is 0..100%
    auto bar = r.withWidth (v * r.getWidth());
    g.setColour (Col::mod);
    g.fillRoundedRectangle (bar, 3.0f);
}

void ModPanel::updateRateKnob()
{
    const bool synced = proc.apvts.getRawParameterValue (P::mod (index, "sync"))->load() > 0.5f;
    if (syncRate.isVisible() != synced)
    {
        syncRate.setVisible (synced);
        rate.setVisible (! synced);
    }
}

void ModPanel::tick()
{
    updateRateKnob();
    float v = proc.uiMod[index].load();
    if (std::abs (v - shownValue) > 0.005f)
    {
        shownValue = v;
        repaint (getWidth() - 95, 5, 90, 16);
    }
}

//==============================================================================
ShPanel::ShPanel (ElukkaAudioProcessor& p)
    : Panel ("S&H RANDOM"), proc (p), rate (p.apvts, "sh_syncRate", "Rate"),
      smooth (p.apvts, "sh_smooth", "Smooth"), retrig (p.apvts, "sh_retrig", "Retrig")
{
    addAndMakeVisible (rate);
    addAndMakeVisible (smooth);
    addAndMakeVisible (retrig);
    retrig.button.setTooltip ("On: every note picks a new value and the steps restart from that note. Off: steps follow the song's beat grid");
    smooth.slider.setTooltip ("Glide time between values (0 = instant steps)");
    rate.slider.setTooltip ("Synced step rate, from 8 bars to 1/32 (D = dotted, T = triplet). Double-click to type, right-click to modulate");
    hint.setText ("Right-click any knob and choose S&H to map it.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, Col::dim);
    hint.setFont (juce::Font (juce::FontOptions (11.0f)));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);
}

void ShPanel::resized()
{
    auto r = content();
    retrig.setBounds (r.removeFromTop (26).withWidth (r.getWidth() / 2));
    r.removeFromTop (6);
    auto row = r.removeFromTop (66);
    rate.setBounds (row.removeFromLeft (row.getWidth() / 2));
    smooth.setBounds (row);
    r.removeFromTop (6);
    hint.setBounds (r.removeFromBottom (44));
    r.removeFromBottom (4);
    plotArea = r;
}

void ShPanel::paintOverChildren (juce::Graphics& g)
{
    auto r = plotArea.toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Col::border);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());   // 50%
    if (history.empty()) return;
    const int maxSteps = 24;
    const float w = r.getWidth() / (float) maxSteps;
    juce::Path p;
    float x = r.getRight() - w * (float) history.size();
    for (size_t i = 0; i < history.size(); ++i)
    {
        const float y = r.getBottom() - 4.0f - history[i] * (r.getHeight() - 8.0f);   // 0% bottom, 100% top
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        p.lineTo (x + w, y);
        x += w;
    }
    g.setColour (Col::mod);
    g.strokePath (p, juce::PathStrokeType (1.8f));
}

void ShPanel::tick()
{
    if (! proc.uiPlaying.load()) return;   // the plot only moves while notes play
    const float v = proc.uiSh.load();
    if (v == lastValue) return;
    lastValue = v;
    history.push_back (v);
    while (history.size() > 24) history.pop_front();
    repaint (plotArea);
}

SynthPage::SynthPage (ElukkaAudioProcessor& p)
    : o1 (p.apvts, 0), o2 (p.apvts, 1), o3 (p.apvts, 2),
      fltType (p.apvts, "flt_type", "Type"), fltSlope (p.apvts, "flt_slope", "Slope"),
      fCutoff (p.apvts, "flt_cutoff", "Cutoff"), fRes (p.apvts, "flt_res", "Resonance"), fEnv (p.apvts, "flt_env", "Env Amt"),
      fKey (p.apvts, "flt_key", "Key Track"), fDrive (p.apvts, "flt_drive", "Drive"),
      fA (p.apvts, "flt_A", "Attack"), fD (p.apvts, "flt_D", "Decay"), fS (p.apvts, "flt_S", "Sustain"), fR (p.apvts, "flt_R", "Release"),
      toFlt1 (p.apvts, P::osc (0, "toFilter"), "Osc 1"), toFlt2 (p.apvts, P::osc (1, "toFilter"), "Osc 2"), toFlt3 (p.apvts, P::osc (2, "toFilter"), "Osc 3"),
      a (p.apvts, "ampA", "Attack"), d (p.apvts, "ampD", "Decay"), s (p.apvts, "ampS", "Sustain"), r (p.apvts, "ampR", "Release"),
      glide (p.apvts, "glide", "Glide"), bend (p.apvts, "bendRange", "Bend"), bassFreq (p.apvts, "rs_bassFreq", "Bass Freq"),
      polyMode (p.apvts, "polyMode", "Voice Mode"),
      mod1 (p, 0), mod2 (p, 1), sh (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &o1, &o2, &o3, &filterPanel, &ampPanel, &mod1, &mod2, &sh })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &fltType, &fltSlope, &fCutoff, &fRes, &fEnv, &fKey, &fDrive,
                                                              &fA, &fD, &fS, &fR, &fEnvLabel, &a, &d, &s, &r, &glide, &bend, &bassFreq, &polyMode,
                                                              &toFlt1, &toFlt2, &toFlt3, &fInputLabel })
        addAndMakeVisible (c);
    fInputLabel.setText ("INPUT", juce::dontSendNotification);
    fInputLabel.setColour (juce::Label::textColourId, Col::accent);
    fInputLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    fInputLabel.setJustificationType (juce::Justification::centredRight);
    for (auto* t : { &toFlt1, &toFlt2, &toFlt3 })
        t->button.setTooltip ("Send this oscillator through the filter (off: it bypasses the filter)");
    fEnvLabel.setText ("FILTER ENV", juce::dontSendNotification);
    fEnvLabel.setColour (juce::Label::textColourId, Col::accent);
    fEnvLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    fEnvLabel.setJustificationType (juce::Justification::centredRight);
    fEnv.slider.setTooltip ("How far the filter envelope moves the cutoff (up or down, up to 8 octaves)");
    fKey.slider.setTooltip ("How much the cutoff follows the played note (100% = tracks the keyboard exactly)");
    bassFreq.slider.setTooltip ("Crossover for the oscillators' Bass Mono switches and the resynth Sub Lift");
}

void SynthPage::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (266);
    const int w = (top.getWidth() - 16) / 3;
    o1.setBounds (top.removeFromLeft (w));
    top.removeFromLeft (8);
    o2.setBounds (top.removeFromLeft (w));
    top.removeFromLeft (8);
    o3.setBounds (top);
    area.removeFromTop (8);

    filterPanel.setBounds (area.removeFromTop (104));
    auto f = filterPanel.contentInParent();
    auto choices = f.removeFromLeft (110);
    fltType.setBounds (choices.withSizeKeepingCentre (110, 40).translated (0, -6));
    f.removeFromLeft (8);
    fltSlope.setBounds (f.removeFromLeft (90).withSizeKeepingCentre (90, 40).translated (0, -6));
    f.removeFromLeft (12);
    layoutRow (f.removeFromLeft (5 * 68), { &fCutoff, &fRes, &fEnv, &fKey, &fDrive }, 66, 2);
    f.removeFromLeft (12);
    fEnvLabel.setBounds (f.removeFromLeft (80).withSizeKeepingCentre (80, 20));
    f.removeFromLeft (8);
    layoutRow (f.removeFromLeft (4 * 66), { &fA, &fD, &fS, &fR }, 64, 2);
    // oscillators routed through the filter, stacked at the right
    auto routing = f.removeFromRight (juce::jmin (80, f.getWidth()));
    f.removeFromRight (8);
    fInputLabel.setBounds (f.removeFromRight (juce::jmin (60, f.getWidth())).withSizeKeepingCentre (juce::jmin (60, f.getWidth()), 20));
    const int rowH = routing.getHeight() / 3;
    for (auto* t : { &toFlt1, &toFlt2, &toFlt3 }) t->setBounds (routing.removeFromTop (rowH));
    area.removeFromTop (8);

    ampPanel.setBounds (area.removeFromLeft (230));
    area.removeFromLeft (8);
    sh.setBounds (area.removeFromRight (160));
    area.removeFromRight (8);
    const int mw = (area.getWidth() - 8) / 2;
    mod1.setBounds (area.removeFromLeft (mw));
    area.removeFromLeft (8);
    mod2.setBounds (area);

    auto c = ampPanel.getBounds().reduced (8).withTrimmedTop (20);
    const int kw = c.getWidth() / 4;
    layoutRow (c.removeFromTop (66), { &a, &d, &s, &r }, kw, 0);
    c.removeFromTop (14);
    polyMode.setBounds (c.removeFromTop (40).reduced (4, 0));
    c.removeFromTop (10);
    layoutRow (c.removeFromTop (66), { &glide, &bend, &bassFreq }, kw, 0);
}

//==============================================================================
HarmonicsPage::HarmonicsPage (ElukkaAudioProcessor& p)
    : proc (p),
      stretch (p.apvts, "hm_stretch", "Stretch"), oddEven (p.apvts, "hm_oddEven", "Odd/Even"),
      cutoff (p.apvts, "hm_cutoff", "Cutoff"), res (p.apvts, "hm_res", "Resonance"),
      prism (p.apvts, "hm_prism", "Prism"), prismCenter (p.apvts, "hm_prismCenter", "Center"),
      prismWidth (p.apvts, "hm_prismWidth", "Width"), shimmer (p.apvts, "hm_shimmer", "Shimmer"),
      shimmerRate (p.apvts, "hm_shimmerRate", "Rate"), tableAmt (p.apvts, "hm_tableAmt", "Depth"),
      tableSrc (p.apvts, "hm_tableSrc", "Depth Source"),
      gain (p, p.harmGain, false, "HARMONIC LEVELS  (harmonics 1-64, draw with the mouse)"),
      depth (p, p.harmDepth, true, "HARMONIC MOD DEPTH  (how much the Depth Source moves each harmonic)")
{
    for (auto* c : std::initializer_list<juce::Component*> { &controls, &stretch, &oddEven, &cutoff, &res, &prism, &prismCenter,
                                                              &prismWidth, &shimmer, &shimmerRate, &tableAmt, &tableSrc, &help, &gain, &depth, &resetGain, &resetDepth })
        addAndMakeVisible (c);
    help.setText ("Stretch pushes partials off the harmonic series for bell-like and atonal tones.\n\n"
                  "Prism knocks the harmonics around Center (Width in octaves) off their places, the fundamental "
                  "stays put. Shimmer lets every harmonic drift in pitch on its own.\n\n"
                  "Draw alternating values in the depth table and pick an LFO as Depth Source "
                  "to make individual harmonics move.", juce::dontSendNotification);
    help.setColour (juce::Label::textColourId, Col::dim);
    help.setFont (juce::Font (juce::FontOptions (11.5f)));
    help.setJustificationType (juce::Justification::topLeft);
    prism.slider.setTooltip ("Pushes the harmonics around Center up or down by uneven amounts (up to half a harmonic): "
                             "metallic, atonal colours while the note keeps its pitch");
    prismCenter.slider.setTooltip ("Which harmonic the Prism band is centred on");
    prismWidth.slider.setTooltip ("How wide the Prism band is, in octaves (harmonics outside it are untouched)");
    shimmer.slider.setTooltip ("Every harmonic drifts in pitch on its own (up to +-80 cents): chorus-like at low amounts, seasick at high");
    shimmerRate.slider.setTooltip ("How fast the Shimmer drift moves");
    resetGain.onClick = [this] { proc.harmGain.fill (1.0f); gain.repaint(); };
    resetDepth.onClick = [this] { proc.harmDepth.fill (0.0f); depth.repaint(); };
}

void HarmonicsPage::resized()
{
    auto area = getLocalBounds();
    controls.setBounds (area.removeFromLeft (290));
    area.removeFromLeft (8);
    auto c = controls.getBounds().reduced (8).withTrimmedTop (20);
    const int kw = c.getWidth() / 3;
    layoutRow (c.removeFromTop (66), { &stretch, &oddEven, &tableAmt }, kw, 0);
    c.removeFromTop (6);
    layoutRow (c.removeFromTop (66), { &cutoff, &res }, kw, 0);
    c.removeFromTop (6);
    layoutRow (c.removeFromTop (66), { &prism, &prismCenter, &prismWidth }, kw, 0);
    c.removeFromTop (6);
    layoutRow (c.removeFromTop (66), { &shimmer, &shimmerRate }, kw, 0);
    c.removeFromTop (6);
    tableSrc.setBounds (c.removeFromTop (40).reduced (4, 0));
    c.removeFromTop (12);
    help.setBounds (c.reduced (4, 0));

    const int h = (area.getHeight() - 8) / 2;
    gain.setBounds (area.removeFromTop (h));
    area.removeFromTop (8);
    depth.setBounds (area);
    resetGain.setBounds (gain.getRight() - 100, gain.getY() + 3, 92, 18);
    resetDepth.setBounds (depth.getRight() - 100, depth.getY() + 3, 92, 18);
}

//==============================================================================
ResynthPage::ResynthPage (ElukkaAudioProcessor& p)
    : proc (p), view (p),
      tune (p.apvts, "rs_tune", "Tune"), gate (p.apvts, "rs_gate", "Gate"), lift (p.apvts, "rs_lift", "Lift"), subLift (p.apvts, "rs_subLift", "Sub Lift"),
      speed (p.apvts, "rs_speed", "Speed"),
      bpm (p.apvts, "rs_bpm", "Source BPM"), pos (p.apvts, "rs_pos", "Position"),
      sync (p.apvts, "rs_sync", "Sync to Host"), loop (p.apvts, "rs_loop", "Loop")
{
    for (auto* c : std::initializer_list<juce::Component*> { &sourcePanel, &playPanel, &loadAudioBtn, &loadImageBtn, &clearBtn, &detectBtn,
                                                              &doubleBtn, &halveBtn, &useOsc1, &info, &help, &view, &tune, &gate, &lift, &subLift, &speed, &bpm, &pos, &sync, &loop })
        addAndMakeVisible (c);

    loadAudioBtn.onClick = [this] { loadAudio(); };
    loadImageBtn.onClick = [this] { loadImage(); };
    clearBtn.onClick = [this] { proc.setResynth (nullptr); refresh(); };
    detectBtn.onClick = [this] { if (auto d = proc.getResynth()) setParam ("rs_bpm", (float) d->detectedBpm); };
    doubleBtn.onClick = [this] { setParam ("rs_bpm", proc.apvts.getRawParameterValue ("rs_bpm")->load() * 2.0f); };
    halveBtn.onClick = [this] { setParam ("rs_bpm", proc.apvts.getRawParameterValue ("rs_bpm")->load() * 0.5f); };
    useOsc1.onClick = [this] { setParam ("o1_on", 1.0f); setParam ("o1_wave", (float) P::WResynth); };

    bpmLabel.setText ("Loop BPM", juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, Col::dim);
    addAndMakeVisible (bpmLabel);
    bpmField.setEditable (true, true, false);
    bpmField.setJustificationType (juce::Justification::centred);
    bpmField.setColour (juce::Label::backgroundColourId, Col::panelHi);
    bpmField.setColour (juce::Label::outlineColourId, Col::border);
    bpmField.setTooltip ("Click and type the loop's tempo");
    bpmField.onTextChange = [this]
    {
        const float v = bpmField.getText().retainCharacters ("0123456789.").getFloatValue();
        if (v > 0.0f) setParam ("rs_bpm", juce::jlimit (40.0f, 240.0f, v));
        tick();
    };
    addAndMakeVisible (bpmField);

    info.setColour (juce::Label::textColourId, Col::text);
    info.setJustificationType (juce::Justification::topLeft);
    help.setColour (juce::Label::textColourId, Col::dim);
    help.setFont (juce::Font (juce::FontOptions (11.5f)));
    help.setJustificationType (juce::Justification::topLeft);
    help.setText ("Set an oscillator's wave to \"Resynth\" to play the source. Middle C (C3 in Ableton, C5 in FL) "
                  "plays it at its original pitch; other keys transpose it.\n\n"
                  "Tune: 0% plays the source as it is, transposed by the key. 100% moves every note found in the "
                  "source to the nearest note with the key's name, each in its own octave (press C: a melody E G B "
                  "becomes C C C with the octaves kept, a chord stays spread); playing an octave above or below "
                  "middle C's octave moves the result an octave too; values in between move part of the way.\n"
                  "Gate: fades out partials more than the set level below the loudest partial at that moment (-80 dB = off). "
                  "It comes before Lift, so Lift does not bring back what Gate removed.\n"
                  "Lift: raises quiet partials and dark image areas above Bass Freq (Synth page); Sub Lift does the same below it. "
                  "Bass Mono (on each oscillator) centres everything below Bass Freq.\n\n"
                  "Audio: tempo is detected on load. With Sync to Host on, the loop is time-stretched to your "
                  "project tempo. If the BPM is detected at half/double speed, use the x2 and /2 buttons.\n\n"
                  "Images: brightness = level, bottom row = fundamental, left to right = time (one bar).",
                  juce::dontSendNotification);
    refresh();
}

void ResynthPage::tick()
{
    view.setPlayhead (proc.uiRsPos.load());
    if (bpmField.isBeingEdited()) return;
    auto text = juce::String (proc.apvts.getRawParameterValue ("rs_bpm")->load(), 2);
    if (bpmField.getText() != text) bpmField.setText (text, juce::dontSendNotification);
}

void ResynthPage::setParam (const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void ResynthPage::refresh()
{
    view.refresh();
    if (auto d = proc.getResynth())
    {
        juce::String s;
        s << d->name << "\n" << (d->isImage ? "Image" : "Audio") << ", " << juce::String (d->durationSec, 2) << " s\n";
        if (! d->isImage)
        {
            s << "Detected tempo: " << juce::String (d->detectedBpm, 2) << " BPM\n";
            if (d->numNoteEvents > 0)
            {
                s << "Notes found: " << d->numNoteEvents;
                if (d->detectedFundamentalHz > 0.0f)
                {
                    // the most common note with cents, e.g. "G#2 +12 c"; octaves numbered so that middle C is C4
                    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const float midi = 69.0f + 12.0f * std::log2 (d->detectedFundamentalHz / 440.0f);
                    const int n = juce::roundToInt (midi), cents = juce::roundToInt ((midi - (float) n) * 100.0f);
                    s << ", mostly " << names[((n % 12) + 12) % 12] << (n / 12 - 1) << " " << (cents >= 0 ? "+" : "") << cents << " c ("
                      << juce::roundToInt (d->pitchConfidence * 100.0f) << "%)";
                }
            }
            else
            {
                s << "No pitched notes found (Tune has no effect)";
            }
        }
        info.setText (s, juce::dontSendNotification);
    }
    else
    {
        info.setText ("Nothing loaded", juce::dontSendNotification);
    }
}

static const char* audioWildcards = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
static const char* imageWildcards = "*.png;*.jpg;*.jpeg;*.gif";

bool ResynthPage::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension (juce::String (audioWildcards).removeCharacters ("*"));
}

bool ResynthPage::isImageFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension (juce::String (imageWildcards).removeCharacters ("*"));
}

bool ResynthPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (juce::File (f)) || isImageFile (juce::File (f))) return true;
    return false;
}

void ResynthPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
    {
        juce::File file (f);
        if (isAudioFile (file)) { loadAudioFile (file); return; }
        if (isImageFile (file)) { loadImageFile (file); return; }
    }
}

void ResynthPage::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver) return;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Col::bg.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Col::accent);
    g.drawRoundedRectangle (r, 8.0f, 2.0f);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("Drop audio or an image to resynthesise", getLocalBounds(), juce::Justification::centred);
}

void ResynthPage::loadAudioFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { info.setText ("Could not read " + file.getFileName(), juce::dontSendNotification); return; }
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    if (auto d = ResynthData::fromAudio (buf, reader->sampleRate, file.getFileName()))
    {
        proc.setResynth (d);
        setParam ("rs_bpm", (float) d->detectedBpm);
        refresh();
    }
    else
        info.setText ("Too short to analyse: " + file.getFileName(), juce::dontSendNotification);
}

void ResynthPage::loadAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Load audio for resynthesis", juce::File(), audioWildcards);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile()) loadAudioFile (file);
    });
}

void ResynthPage::loadImage()
{
    chooser = std::make_unique<juce::FileChooser> ("Load image for resynthesis", juce::File(), imageWildcards);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile()) loadImageFile (file);
    });
}

void ResynthPage::loadImageFile (const juce::File& file)
{
    auto img = juce::ImageFileFormat::loadFrom (file);
    if (auto d = ResynthData::fromImage (img, file.getFileName()))
    {
        proc.setResynth (d);
        setParam ("rs_bpm", 120.0f);
        refresh();
    }
    else
        info.setText ("Could not read " + file.getFileName(), juce::dontSendNotification);
}

void ResynthPage::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (290);
    sourcePanel.setBounds (left);
    auto c = sourcePanel.contentInParent();
    loadAudioBtn.setBounds (c.removeFromTop (28));
    c.removeFromTop (6);
    loadImageBtn.setBounds (c.removeFromTop (28));
    c.removeFromTop (6);
    clearBtn.setBounds (c.removeFromTop (24).removeFromLeft (80));
    c.removeFromTop (10);
    info.setBounds (c.removeFromTop (70));
    useOsc1.setBounds (c.removeFromTop (26));
    c.removeFromTop (10);
    help.setBounds (c);

    area.removeFromLeft (8);
    auto bottom = area.removeFromBottom (130);
    playPanel.setBounds (bottom);
    area.removeFromBottom (8);
    view.setBounds (area);

    auto pc = playPanel.contentInParent();
    layoutRow (pc.removeFromLeft (7 * 68).removeFromTop (70), { &tune, &gate, &lift, &subLift, &speed, &bpm, &pos }, 66, 2);
    pc.removeFromLeft (10);
    auto col1 = pc.removeFromLeft (120);
    sync.setBounds (col1.removeFromTop (26));
    col1.removeFromTop (4);
    loop.setBounds (col1.removeFromTop (26));
    pc.removeFromLeft (10);
    auto col2 = pc.removeFromLeft (150);
    detectBtn.setBounds (col2.removeFromTop (24));
    col2.removeFromTop (4);
    auto halves = col2.removeFromTop (24);
    doubleBtn.setBounds (halves.removeFromLeft (72));
    halves.removeFromLeft (6);
    halveBtn.setBounds (halves);
    col2.removeFromTop (4);
    auto bpmRow = col2.removeFromTop (24);
    bpmLabel.setBounds (bpmRow.removeFromLeft (70));
    bpmField.setBounds (bpmRow);
}

//==============================================================================
SeqPage::SeqPage (ElukkaAudioProcessor& p)
    : proc (p),
      on (p.apvts, "sq_on", "Sequencer On"), retrig (p.apvts, "sq_retrig", "Retrigger"), rate (p.apvts, "sq_rate", "Rate"),
      gate (p.apvts, "sq_gate", "Gate"), slide (p.apvts, "sq_slide", "Slide Time"), accent (p.apvts, "sq_accent", "Accent"),
      grid (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &controls, &on, &retrig, &rate, &gate, &slide, &accent, &patternBox, &prevPattern, &nextPattern, &savePattern,
                                                              &length, &pitchCount, &startStep, &lengthLabel, &pitchLabel, &startLabel, &grid,
                                                              &randomize, &randomScale, &randomRange })
        addAndMakeVisible (c);
    randomScale.addItemList ({ "Chromatic", "Minor", "Phrygian", "Locrian", "Major", "Pentatonic" }, 1);
    randomScale.setSelectedId (2, juce::dontSendNotification);
    randomScale.setTooltip ("Scale the random pitches are taken from (relative to the key you hold)");
    for (int o = 1; o <= 3; ++o) randomRange.addItem (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (o) + " oct", o);
    randomRange.setSelectedId (1, juce::dontSendNotification);
    randomRange.setTooltip ("How far up and down the random pitches may go");
    randomize.setTooltip ("New random pitch for every step (time, accents and slides stay)");
    retrig.button.setTooltip ("Every new note restarts the pattern from its start step. Off: the pattern follows the song position");
    randomize.onClick = [this] { randomizePitch(); };

    for (auto* s : { &length, &pitchCount, &startStep })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 36, 22);
        s->setRange (1, SeqPattern::maxSteps, 1);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    }
    length.onValueChange = [this]
    {
        proc.pattern.length = (int) length.getValue();
        proc.pattern.startStep = juce::jmin (proc.pattern.startStep, proc.pattern.length - 1);
        refresh();
    };
    startStep.onValueChange = [this] { proc.pattern.startStep = (int) startStep.getValue() - 1; grid.repaint(); };
    startStep.setTooltip ("Step the pattern starts from; it plays to the end and wraps around");
    startLabel.setText ("Start Step", juce::dontSendNotification);
    pitchCount.onValueChange = [this] { proc.pattern.numPitches = (int) pitchCount.getValue(); grid.repaint(); };
    lengthLabel.setText ("Steps", juce::dontSendNotification);
    pitchLabel.setText ("Pitches", juce::dontSendNotification);
    for (auto* l : { &lengthLabel, &pitchLabel, &startLabel })
    {
        l->setColour (juce::Label::textColourId, Col::dim);
        l->setFont (juce::Font (juce::FontOptions (11.0f)));
    }

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (bars[i]);
        bars[i].setClickingTogglesState (true);
        bars[i].setRadioGroupId (4242);
        bars[i].onClick = [this, i] { grid.setBar (i); };
    }
    bars[0].setToggleState (true, juce::dontSendNotification);

    patternBox.setTextWhenNothingSelected ("Pattern...");
    prevPattern.onClick = [this] { stepPattern (-1); };
    nextPattern.onClick = [this] { stepPattern (1); };
    prevPattern.setTooltip ("Previous pattern");
    nextPattern.setTooltip ("Next pattern");
    patternBox.onChange = [this]
    {
        int id = patternBox.getSelectedId();
        if (id > 0) proc.presets.loadPattern (id - 1);
    };
    savePattern.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Save Pattern", "Name for this pattern:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.pattern.name);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1) proc.presets.savePattern (w->getTextEditorContents ("name"));
            refresh();
        }), true);
    };

    grid.setTooltip ("Hold a key to play: the pattern is transposed so 'root' = the key you hold. Time row: click to cycle "
                     "note / tie / rest; each note step uses the next pitch. Pitch: drag to draw, click a step to type "
                     "semitones (-36..+36). Slide glides into the next note; accent hits harder.");
    refresh();
}

void SeqPage::randomizePitch()
{
    static const std::vector<std::vector<int>> scales { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 3, 5, 7, 8, 10 },
                                                        { 0, 1, 3, 5, 7, 8, 10 }, { 0, 1, 3, 5, 6, 8, 10 },
                                                        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 3, 5, 7, 10 } };
    const auto& scale = scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, randomScale.getSelectedId() - 1)];
    const int span = 12 * juce::jlimit (1, 3, randomRange.getSelectedId());
    // every note of the scale within the range; the root counts triple and nearer octaves more, so lines stay grounded
    std::vector<std::pair<int, float>> choices;
    float total = 0.0f;
    for (int v = -span; v <= span; ++v)
    {
        const int pc = ((v % 12) + 12) % 12;
        if (std::find (scale.begin(), scale.end(), pc) == scale.end()) continue;
        const float w = (pc == 0 ? 3.0f : 1.0f) / (1.0f + 0.5f * (float) (std::abs (v) / 12));
        choices.push_back ({ v, w });
        total += w;
    }
    auto& rng = juce::Random::getSystemRandom();
    for (auto& e : proc.pattern.pitches)
    {
        float x = rng.nextFloat() * total;
        for (auto& [v, w] : choices)
            if ((x -= w) <= 0.0f) { e.semis = v; break; }
    }
    grid.repaint();
}

void SeqPage::stepPattern (int delta)
{
    auto& list = proc.presets.patterns;
    const int n = (int) list.size();
    if (n == 0) return;
    int current = -1;
    for (int i = 0; i < n && current < 0; ++i)
        if (list[(size_t) i].name == proc.pattern.name) current = i;
    const int next = current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
    proc.presets.loadPattern (next);
    refresh();
}

void SeqPage::refresh()
{
    length.setValue (proc.pattern.length, juce::dontSendNotification);
    pitchCount.setValue (proc.pattern.numPitches, juce::dontSendNotification);
    startStep.setRange (1, juce::jmax (2, proc.pattern.length), 1);
    startStep.setValue (proc.pattern.startStep + 1, juce::dontSendNotification);
    patternBox.clear (juce::dontSendNotification);
    bool userHeading = false;
    for (int i = 0; i < (int) proc.presets.patterns.size(); ++i)
    {
        auto& e = proc.presets.patterns[(size_t) i];
        if (! e.isFactory && ! userHeading) { patternBox.addSeparator(); patternBox.addSectionHeading ("User"); userHeading = true; }
        patternBox.addItem (e.name, i + 1);
    }
    patternBox.setText (proc.pattern.name, juce::dontSendNotification);
    grid.repaint();
}

void SeqPage::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (146);
    controls.setBounds (top);
    auto c = controls.contentInParent();
    auto row = c.removeFromTop (84);
    {
        auto onCol = row.removeFromLeft (120);
        on.setBounds (onCol.removeFromTop (onCol.getHeight() / 2).withSizeKeepingCentre (120, 26));
        retrig.setBounds (onCol.withSizeKeepingCentre (120, 26));
    }
    row.removeFromLeft (8);
    rate.setBounds (row.removeFromLeft (80).withSizeKeepingCentre (80, 40));
    row.removeFromLeft (8);
    layoutRow (row.removeFromLeft (3 * 66), { &gate, &slide, &accent }, 64, 2);
    row.removeFromLeft (14);
    auto patCol = row.removeFromLeft (240);
    auto patRow = patCol.removeFromTop (30).withTrimmedTop (6);
    prevPattern.setBounds (patRow.removeFromLeft (24));
    patRow.removeFromLeft (3);
    nextPattern.setBounds (patRow.removeFromRight (24));
    patRow.removeFromRight (3);
    patternBox.setBounds (patRow);
    patCol.removeFromTop (6);
    savePattern.setBounds (patCol.removeFromTop (24).removeFromLeft (110));
    row.removeFromLeft (14);
    auto lenCol = row.removeFromLeft (110);
    lengthLabel.setBounds (lenCol.removeFromTop (14));
    length.setBounds (lenCol.removeFromTop (24));
    lenCol.removeFromTop (2);
    pitchLabel.setBounds (lenCol.removeFromTop (14));
    pitchCount.setBounds (lenCol.removeFromTop (24));
    row.removeFromLeft (10);
    auto startCol = row.removeFromLeft (110);
    startLabel.setBounds (startCol.removeFromTop (14));
    startStep.setBounds (startCol.removeFromTop (24));
    row.removeFromLeft (14);
    auto rnd = row.removeFromLeft (200).withTrimmedTop (14);
    randomize.setBounds (rnd.removeFromTop (24));
    rnd.removeFromTop (4);
    randomRange.setBounds (rnd.removeFromRight (82).removeFromTop (24));
    rnd.removeFromRight (4);
    randomScale.setBounds (rnd.removeFromTop (24));
    c.removeFromTop (4);
    auto barRow = c.removeFromTop (24);
    for (auto& b : bars)
    {
        b.setBounds (barRow.removeFromLeft (70));
        barRow.removeFromLeft (4);
    }

    area.removeFromTop (8);
    grid.setBounds (area);
}

//==============================================================================
FXParams EqCurve::current() const
{
    auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    FXParams f;
    f.eqLow = v ("eq_low"); f.eqLowF = v ("eq_lowF"); f.eqLowQ = v ("eq_lowQ");
    f.eqMid = v ("eq_mid"); f.eqMidF = v ("eq_midF"); f.eqMidQ = v ("eq_midQ");
    f.eqHigh = v ("eq_high"); f.eqHighF = v ("eq_highF"); f.eqHighQ = v ("eq_highQ");
    return f;
}

void EqCurve::tick()
{
    const auto f = current();
    const std::array<float, 10> now { f.eqLow, f.eqLowF, f.eqLowQ, f.eqMid, f.eqMidF, f.eqMidQ, f.eqHigh, f.eqHighF, f.eqHighQ,
                                      proc.apvts.getRawParameterValue ("eq_on")->load() };
    if (now != last) { last = now; repaint(); }
}

void EqCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    r = r.reduced (2.0f);

    const float range = 18.0f;
    auto xFor = [&] (double hz) { return r.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * r.getWidth(); };
    auto yFor = [&] (float db) { return r.getCentreY() - db / range * (r.getHeight() * 0.5f); };

    g.setColour (Col::border);
    for (double hz : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xFor (hz), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) yFor (0.0f), r.getX(), r.getRight());

    const double sr = 48000.0;
    auto coefs = makeEqCoefficients (sr, current());
    juce::Path p;
    for (int i = 0; i <= (int) r.getWidth(); ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / r.getWidth());
        double mag = 1.0;
        for (auto& c : coefs) mag *= c->getMagnitudeForFrequency (juce::jmin (hz, sr * 0.49), sr);
        const float y = juce::jlimit (r.getY(), r.getBottom(), yFor (juce::Decibels::gainToDecibels ((float) mag, -60.0f)));
        if (i == 0) p.startNewSubPath (r.getX() + (float) i, y); else p.lineTo (r.getX() + (float) i, y);
    }
    const bool enabled = proc.apvts.getRawParameterValue ("eq_on")->load() > 0.5f;
    g.setColour (enabled ? Col::accent : Col::dim);
    g.strokePath (p, juce::PathStrokeType (1.8f));
}

void OttMeter::tick()
{
    bool changed = false;
    for (int b = 0; b < 3; ++b)
    {
        const float v = proc.uiOttDb[b].load();
        if (std::abs (v - shown[b]) > 0.2f) { shown[b] = v; changed = true; }
    }
    if (changed) repaint();
}

void OttMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    const float range = 24.0f;   // dB shown in each direction
    const char* names[] { "Low", "Mid", "High" };
    const float w = r.getWidth() / 3.0f;
    auto plot = r.withTrimmedBottom (14.0f).reduced (0.0f, 4.0f);
    g.setColour (Col::border);
    g.drawHorizontalLine ((int) plot.getCentreY(), r.getX(), r.getRight());
    for (int b = 0; b < 3; ++b)
    {
        auto col = juce::Rectangle<float> (r.getX() + w * (float) b, plot.getY(), w, plot.getHeight()).reduced (w * 0.25f, 0.0f);
        const float v = juce::jlimit (-range, range, shown[b]);
        const float y = plot.getCentreY() - v / range * plot.getHeight() * 0.5f;
        g.setColour (v >= 0.0f ? Col::accent : Col::warn);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (col.getX(), juce::jmin (y, plot.getCentreY()), col.getRight(), juce::jmax (y, plot.getCentreY())));
        g.setColour (Col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (names[b], juce::Rectangle<float> (r.getX() + w * (float) b, r.getBottom() - 14.0f, w, 14.0f), juce::Justification::centred);
    }
}

void FxBox::paint (juce::Graphics& g)
{
    Panel::paint (g);
    // drag grip in the top-right corner
    g.setColour (Col::dim);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col)
            g.fillEllipse ((float) getWidth() - 30.0f + (float) col * 6.0f, 10.0f + (float) row * 6.0f, 3.0f, 3.0f);
}

FxPage::FxPage (ElukkaAudioProcessor& p) : proc (p)
{
    auto& s = p.apvts;
    auto twoColumns = [] (FxBox& box, juce::Component* on, juce::Component* choice, std::vector<juce::Component*> knobs, juce::Component* extra)
    {
        box.layout = [on, choice, knobs, extra] (juce::Rectangle<int> c)
        {
            on->setBounds (c.removeFromTop (26).removeFromLeft (70));
            c.removeFromTop (8);
            if (choice != nullptr) { choice->setBounds (c.removeFromTop (40)); c.removeFromTop (10); }
            const int kw = c.getWidth() / 2;
            juce::Rectangle<int> row;
            for (size_t i = 0; i < knobs.size(); ++i)
            {
                if (i % 2 == 0) { row = c.removeFromTop (70); c.removeFromTop (8); }
                knobs[i]->setBounds (row.removeFromLeft (kw));
            }
            if (extra != nullptr) extra->setBounds (c.removeFromTop (26).removeFromLeft (110));
        };
    };

    {
        auto& b = *(boxes[P::FxComb] = std::make_unique<FxBox> ("COMB FILTER", P::FxComb));
        auto& on = b.add<Toggle> (s, "comb_on", "On");
        twoColumns (b, &on, nullptr, { &b.add<Knob> (s, "comb_freq", "Freq"), &b.add<Knob> (s, "comb_fb", "Feedback"),
                                       &b.add<Knob> (s, "comb_lp", "Low Pass"), &b.add<Knob> (s, "comb_hp", "High Pass"),
                                       &b.add<Knob> (s, "comb_mix", "Mix") }, nullptr);
    }
    {
        auto& b = *(boxes[P::FxDist] = std::make_unique<FxBox> ("DISTORTION", P::FxDist));
        auto& on = b.add<Toggle> (s, "dist_on", "On");
        auto& type = b.add<Choice> (s, "dist_type", "Type");
        twoColumns (b, &on, &type, { &b.add<Knob> (s, "dist_drive", "Drive"), &b.add<Knob> (s, "dist_mix", "Mix") }, nullptr);
    }
    {
        auto& b = *(boxes[P::FxDelay] = std::make_unique<FxBox> ("DELAY", P::FxDelay));
        auto* on = &b.add<Toggle> (s, "dly_on", "On");
        auto* sync = &b.add<Toggle> (s, "dly_sync", "Sync");
        dlyDiv = &b.add<Choice> (s, "dly_div", "Synced Time");
        dlyTime = &b.add<Knob> (s, "dly_time", "Time (ms)");
        std::vector<juce::Component*> knobs { dlyTime, &b.add<Knob> (s, "dly_fb", "Feedback"), &b.add<Knob> (s, "dly_lp", "Low Pass"),
                                              &b.add<Knob> (s, "dly_hp", "High Pass"), &b.add<Knob> (s, "dly_mix", "Mix") };
        auto* pp = &b.add<Toggle> (s, "dly_pp", "Ping-Pong");
        auto* div = dlyDiv;
        b.layout = [on, sync, div, knobs, pp] (juce::Rectangle<int> c)
        {
            auto top = c.removeFromTop (26);
            on->setBounds (top.removeFromLeft (70));
            top.removeFromLeft (6);
            sync->setBounds (top.removeFromLeft (76));
            c.removeFromTop (8);
            div->setBounds (c.removeFromTop (40));
            c.removeFromTop (10);
            const int kw = c.getWidth() / 2;
            juce::Rectangle<int> row;
            for (size_t i = 0; i < knobs.size(); ++i)
            {
                if (i % 2 == 0) { row = c.removeFromTop (70); c.removeFromTop (8); }
                knobs[i]->setBounds (row.removeFromLeft (kw));
            }
            pp->setBounds (c.removeFromTop (26).removeFromLeft (110));
        };
    }
    {
        auto& b = *(boxes[P::FxReverb] = std::make_unique<FxBox> ("REVERB", P::FxReverb));
        auto& on = b.add<Toggle> (s, "rev_on", "On");
        auto& mode = b.add<Choice> (s, "rev_mode", "Mode");
        mode.box.setTooltip ("Smooth: dense and balanced. Vintage: warmer and darker. Lush: wider, more diffuse and chorused");
        twoColumns (b, &on, &mode, { &b.add<Knob> (s, "rev_decay", "Decay"), &b.add<Knob> (s, "rev_size", "Size"),
                                     &b.add<Knob> (s, "rev_tone", "Tone"), &b.add<Knob> (s, "rev_mod", "Mod"),
                                     &b.add<Knob> (s, "rev_mix", "Mix") }, nullptr);
    }
    {
        auto& b = *(boxes[P::FxOtt] = std::make_unique<FxBox> ("OTT", P::FxOtt));
        auto* on = &b.add<Toggle> (s, "ott_on", "On");
        auto* amount = &b.add<Knob> (s, "ott_amount", "Amount");
        auto* speed = &b.add<Knob> (s, "ott_speed", "Speed");
        ottMeter = &b.add<OttMeter> (p);
        auto* meter = ottMeter;
        amount->slider.setTooltip ("How strongly each band is compressed: loud parts pushed down, quiet parts pulled up");
        speed->slider.setTooltip ("Attack/release speed of the compression (higher = faster)");
        b.layout = [on, amount, speed, meter] (juce::Rectangle<int> c)
        {
            on->setBounds (c.removeFromTop (26).removeFromLeft (70));
            c.removeFromTop (8);
            auto row = c.removeFromTop (70);
            const int kw = row.getWidth() / 2;
            amount->setBounds (row.removeFromLeft (kw));
            speed->setBounds (row);
            c.removeFromTop (10);
            meter->setBounds (c.removeFromTop (120));
        };
    }
    {
        auto& b = *(boxes[P::FxFilter] = std::make_unique<FxBox> ("FILTER", P::FxFilter));
        auto* on = &b.add<Toggle> (s, "fxf_on", "On");
        auto* type = &b.add<Choice> (s, "fxf_type", "Type");
        auto* slope = &b.add<Choice> (s, "fxf_slope", "Slope");
        std::vector<juce::Component*> knobs { &b.add<Knob> (s, "fxf_cutoff", "Cutoff"), &b.add<Knob> (s, "fxf_res", "Resonance"),
                                              &b.add<Knob> (s, "fxf_drive", "Drive") };
        b.layout = [on, type, slope, knobs] (juce::Rectangle<int> c)
        {
            on->setBounds (c.removeFromTop (26).removeFromLeft (70));
            c.removeFromTop (8);
            type->setBounds (c.removeFromTop (40));
            c.removeFromTop (6);
            slope->setBounds (c.removeFromTop (40));
            c.removeFromTop (10);
            const int kw = c.getWidth() / 2;
            juce::Rectangle<int> row;
            for (size_t i = 0; i < knobs.size(); ++i)
            {
                if (i % 2 == 0) { row = c.removeFromTop (70); c.removeFromTop (8); }
                knobs[i]->setBounds (row.removeFromLeft (kw));
            }
        };
    }
    {
        auto& b = *(boxes[P::FxEq] = std::make_unique<FxBox> ("EQ", P::FxEq));
        auto& on = b.add<Toggle> (s, "eq_on", "On");
        eqCurve = &b.add<EqCurve> (p);
        // columns: low shelf, bell, high shelf; rows: gain, frequency, resonance (Q)
        Knob* k[3][3] = {
            { &b.add<Knob> (s, "eq_low", "Low"), &b.add<Knob> (s, "eq_lowF", "Freq"), &b.add<Knob> (s, "eq_lowQ", "Q") },
            { &b.add<Knob> (s, "eq_mid", "Mid"), &b.add<Knob> (s, "eq_midF", "Freq"), &b.add<Knob> (s, "eq_midQ", "Q") },
            { &b.add<Knob> (s, "eq_high", "High"), &b.add<Knob> (s, "eq_highF", "Freq"), &b.add<Knob> (s, "eq_highQ", "Q") } };
        auto* curve = eqCurve;
        std::array<std::array<Knob*, 3>, 3> grid;
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) grid[(size_t) c][(size_t) r] = k[c][r];
        auto* onToggle = &on;
        b.layout = [onToggle, curve, grid] (juce::Rectangle<int> c)
        {
            onToggle->setBounds (c.removeFromTop (26).removeFromLeft (70));
            c.removeFromTop (8);
            curve->setBounds (c.removeFromTop (90));
            c.removeFromTop (10);
            const int kw = c.getWidth() / 3;
            for (int row = 0; row < 3; ++row)
            {
                auto rr = c.removeFromTop (66);
                for (int col = 0; col < 3; ++col) grid[(size_t) col][(size_t) row]->setBounds (rr.removeFromLeft (kw));
                c.removeFromTop (6);
            }
        };
    }

    for (auto& b : boxes)
    {
        b->onDrag = [this] (FxBox& box, const juce::MouseEvent& e, int phase) { handleDrag (box, e, phase); };
        addAndMakeVisible (*b);
    }
    hint.setText ("Signal flows left to right. Drag an effect box to change the order.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, Col::dim);
    hint.setFont (juce::Font (juce::FontOptions (11.5f)));
    addAndMakeVisible (hint);
}

void FxPage::tick()
{
    if (eqCurve != nullptr) eqCurve->tick();
    if (ottMeter != nullptr) ottMeter->tick();
    // Only the active delay time control is enabled
    const bool synced = proc.apvts.getRawParameterValue ("dly_sync")->load() > 0.5f;
    if (dlyDiv->isEnabled() != synced)
    {
        dlyDiv->setEnabled (synced);
        dlyDiv->setAlpha (synced ? 1.0f : 0.4f);
        dlyTime->setEnabled (! synced);
        dlyTime->setAlpha (synced ? 0.4f : 1.0f);
    }
}

std::vector<int> FxPage::currentOrder() const
{
    std::vector<int> order;
    for (auto& f : proc.fxOrder) order.push_back (f.load());
    return order;
}

juce::Rectangle<int> FxPage::slotBounds (int slot) const
{
    const int gap = 8;
    const int w = (getWidth() - gap * (P::NumFx - 1)) / P::NumFx;
    return { slot * (w + gap), 0, w, getHeight() - 24 };
}

void FxPage::layoutBoxes (const std::vector<int>& order, const FxBox* skip)
{
    for (int slot = 0; slot < (int) order.size(); ++slot)
        if (boxes[order[(size_t) slot]].get() != skip)
            boxes[order[(size_t) slot]]->setBounds (slotBounds (slot));
}

void FxPage::resized()
{
    layoutBoxes (currentOrder(), nullptr);
    hint.setBounds (getLocalBounds().removeFromBottom (20));
}

void FxPage::handleDrag (FxBox& box, const juce::MouseEvent& e, int phase)
{
    auto pos = e.getEventRelativeTo (this).getPosition();
    const int slotW = slotBounds (1).getX();
    if (phase == 0)
    {
        dragOffsetX = pos.x - box.getX();
        box.toFront (false);
        dragTarget = -1;
        return;
    }

    auto order = currentOrder();
    order.erase (std::find (order.begin(), order.end(), box.fxId));
    const int x = juce::jlimit (0, getWidth() - box.getWidth(), pos.x - dragOffsetX);
    const int target = juce::jlimit (0, P::NumFx - 1, (x + slotW / 2) / juce::jmax (1, slotW));
    order.insert (order.begin() + target, box.fxId);

    if (phase == 1)
    {
        // live preview: the other boxes make room while this one follows the mouse
        box.setTopLeftPosition (x, box.getY());
        layoutBoxes (order, &box);
        dragTarget = target;
        return;
    }

    for (int i = 0; i < P::NumFx; ++i) proc.fxOrder[(size_t) i] = order[(size_t) i];
    layoutBoxes (order, nullptr);
}

//==============================================================================
MatrixPage::MatrixPage (ElukkaAudioProcessor& p)
    : macro1 (p.apvts, "macro1", "Macro 1"), macro2 (p.apvts, "macro2", "Macro 2")
{
    addAndMakeVisible (matrixPanel);
    addAndMakeVisible (macroPanel);
    for (int i = 0; i < P::numMatrixSlots; ++i)
    {
        auto& r = rows[i];
        r.src = std::make_unique<Choice> (p.apvts, P::mx (i, "src"), "");
        r.dst = std::make_unique<Choice> (p.apvts, P::mx (i, "dst"), "");
        r.amount.setSliderStyle (juce::Slider::LinearHorizontal);
        r.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        r.amount.setColour (juce::Slider::trackColourId, Col::mod);
        r.amount.setColour (juce::Slider::thumbColourId, Col::mod);
        r.att = std::make_unique<APVTS::SliderAttachment> (p.apvts, P::mx (i, "amt"), r.amount);
        r.amount.setDoubleClickReturnValue (true, 0.0);
        setupAmountText (r.amount, p.apvts, i);
        setPitchAmountDetents (r.amount);   // stops at octaves while the row's destination is a pitch
        r.amount.detentsActive = [dst = p.apvts.getRawParameterValue (P::mx (i, "dst"))] { return P::isPitchDest ((int) std::lround (dst->load())); };
        auto* amountSlider = &r.amount;
        r.dst->box.onChange = [amountSlider] { amountSlider->updateText(); };
        r.number.setText (juce::String (i + 1), juce::dontSendNotification);
        r.number.setColour (juce::Label::textColourId, Col::dim);
        r.number.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*r.src);
        addAndMakeVisible (*r.dst);
        addAndMakeVisible (r.amount);
        addAndMakeVisible (r.number);
    }
    addAndMakeVisible (macro1);
    addAndMakeVisible (macro2);
    addAndMakeVisible (help);
    help.setText ("Each row routes a source to a destination by an amount (-1 to +1). "
                  "Mod 1/2 are the LFO/envelope units on the Synth page. "
                  "To map a macro, pick Macro 1 or 2 as a source; several rows can share one macro. "
                  "The macro knobs are also in the header and can be automated from your DAW.", juce::dontSendNotification);
    help.setColour (juce::Label::textColourId, Col::dim);
    help.setFont (juce::Font (juce::FontOptions (11.5f)));
    help.setJustificationType (juce::Justification::topLeft);
}

void MatrixPage::resized()
{
    auto area = getLocalBounds();
    macroPanel.setBounds (area.removeFromRight (300));
    area.removeFromRight (8);
    matrixPanel.setBounds (area);

    auto c = matrixPanel.contentInParent().withTrimmedTop (6);
    c.removeFromTop (16);
    const int rowH = juce::jmin (52, c.getHeight() / P::numMatrixSlots);
    for (auto& r : rows)
    {
        auto row = c.removeFromTop (rowH).withSizeKeepingCentre (c.getWidth(), 30);
        r.number.setBounds (row.removeFromLeft (28));
        r.src->setBounds (row.removeFromLeft (160));
        row.removeFromLeft (10);
        r.dst->setBounds (row.removeFromLeft (200));
        row.removeFromLeft (16);
        r.amount.setBounds (row);
    }

    auto m = macroPanel.contentInParent();
    auto knobs = m.removeFromTop (130);
    macro1.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (10, 0));
    macro2.setBounds (knobs.reduced (10, 0));
    m.removeFromTop (16);
    help.setBounds (m.reduced (4, 0));
}
