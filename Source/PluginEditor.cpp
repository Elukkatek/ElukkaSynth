#include "PluginEditor.h"
#include "BinaryData.h"
#if JUCE_MAC
 #include "UI/HostWindow.h"
#endif

ElukkaAudioProcessorEditor::ElukkaAudioProcessorEditor (ElukkaAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      macro1 (p.apvts, "macro1", "Macro 1"), macro2 (p.apvts, "macro2", "Macro 2"), master (p.apvts, "master", "Master")
{
    setLookAndFeel (&lnf);
    logo = makeLogo();
    view.painter = [this] (juce::Graphics& g) { paintView (g); };
    addAndMakeVisible (view);
    logoArea.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    logoArea.onClick = [this] { showCredits(); };
    view.addAndMakeVisible (logoArea);
    setWantsKeyboardFocus (true);
    // FL Studio takes the native keyboard focus back on every click, so every click inside the plugin
    // has to take it again for Ctrl+Z to reach the plugin instead of the host.
    addMouseListener (this, true);

    const char* names[] { "SYNTH", "HARMONICS", "RESYNTH", "SEQUENCER", "FX", "MATRIX" };
    pages[0] = std::make_unique<SynthPage> (p);
    pages[1] = std::make_unique<HarmonicsPage> (p);
    pages[2] = std::make_unique<ResynthPage> (p);
    pages[3] = std::make_unique<SeqPage> (p);
    pages[4] = std::make_unique<FxPage> (p);
    pages[5] = std::make_unique<MatrixPage> (p);
    for (int i = 0; i < 6; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (1001);
        tabs[i].onClick = [this, i] { showPage (i); };
        view.addAndMakeVisible (tabs[i]);
        view.addChildComponent (*pages[i]);
    }

    presetButton.onClick = [this] { showPresetMenu(); };
    presetButton.setTooltip ("Browse presets: hover a category to open it");
    resetBtn.onClick = [this] { proc.presets.load (0); updatePresetButton(); };   // entry 0 is Init
    resetBtn.setTooltip ("Reset to the Init preset");
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    deleteBtn.onClick = [this] { deletePresetDialog(); };

    for (auto* c : std::initializer_list<juce::Component*> { &presetButton, &prevBtn, &nextBtn, &resetBtn, &saveBtn, &deleteBtn, &macro1, &macro2, &master })
        view.addAndMakeVisible (c);

    updatePresetButton();
    tabs[0].setToggleState (true, juce::dontSendNotification);
    showPage (0);

    // Drag the bottom-right corner to scale the UI between 60% and 200%; the size is saved with the project.
    // (read the saved scale first: setting the limits resizes the window, which overwrites uiScale)
    const float scale = juce::jlimit (0.6f, 2.0f, proc.uiScale);
    setResizable (true, true);
    setResizeLimits ((int) (baseWidth * 0.6), (int) (baseHeight * 0.6), baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));
    startTimerHz (30);
}

ElukkaAudioProcessorEditor::~ElukkaAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ElukkaAudioProcessorEditor::showCredits()
{
    auto text = std::make_unique<juce::Label>();
    text->setText ("Vibecoded by Elukka\nAll the work done by Claude\nA special thanks to Aku Kangas", juce::dontSendNotification);
    text->setJustificationType (juce::Justification::centred);
    text->setColour (juce::Label::textColourId, Col::text);
    text->setFont (juce::Font (juce::FontOptions (13.0f)));
    text->setSize (230, 66);
    juce::CallOutBox::launchAsynchronously (std::move (text), getLocalArea (&logoArea, logoArea.getLocalBounds()), this);
}

juce::Image ElukkaAudioProcessorEditor::makeLogo()
{
    // Turn the black-on-white logo into light strokes on a transparent background, cropped to the ink.
    auto src = juce::ImageCache::getFromMemory (BinaryData::logo_jpg, BinaryData::logo_jpgSize);
    if (! src.isValid()) return {};
    juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    int x0 = src.getWidth(), y0 = src.getHeight(), x1 = 0, y1 = 0;
    for (int y = 0; y < src.getHeight(); ++y)
        for (int x = 0; x < src.getWidth(); ++x)
        {
            float ink = 1.0f - src.getPixelAt (x, y).getPerceivedBrightness();
            float a = juce::jlimit (0.0f, 1.0f, (ink - 0.15f) / 0.6f);
            if (a > 0.0f)
            {
                out.setPixelAt (x, y, Col::text.withAlpha (a));
                if (a > 0.3f) { x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y); x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y); }
            }
        }
    if (x1 <= x0 || y1 <= y0) return out;
    return out.getClippedImage ({ x0, y0, x1 - x0 + 1, y1 - y0 + 1 });
}

void ElukkaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);
}

void ElukkaAudioProcessorEditor::paintView (juce::Graphics& g)
{
    g.fillAll (Col::bg);
    auto header = view.getLocalBounds().removeFromTop (78);
    g.setColour (Col::panel);
    g.fillRect (header);
    g.setColour (Col::border);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) baseWidth);

    if (logo.isValid())
    {
        auto area = juce::Rectangle<float> (14.0f, 6.0f, 150.0f, 66.0f);
        g.drawImage (logo, area, juce::RectanglePlacement::centred);
    }
    g.setColour (Col::dim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("ADDITIVE / RESYNTHESIS", 172, 52, 160, 14, juce::Justification::centredLeft);
}

void ElukkaAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    proc.uiScale = scale;
    view.setBounds (0, 0, baseWidth, baseHeight);
    view.setTransform (juce::AffineTransform::scale (scale));

    auto r = view.getLocalBounds();
    auto header = r.removeFromTop (78);
    header.removeFromLeft (172);

    auto right = header.removeFromRight (210);
    layoutRow (right.reduced (0, 6), { &macro1, &macro2, &master }, 66, 2);

    auto prow = header.removeFromRight (656).reduced (8, 0).withSizeKeepingCentre (640, 34);
    prevBtn.setBounds (prow.removeFromLeft (28));
    prow.removeFromLeft (4);
    nextBtn.setBounds (prow.removeFromLeft (28));
    prow.removeFromLeft (6);
    deleteBtn.setBounds (prow.removeFromRight (60));
    prow.removeFromRight (6);
    saveBtn.setBounds (prow.removeFromRight (56));
    prow.removeFromRight (6);
    resetBtn.setBounds (prow.removeFromRight (60));
    prow.removeFromRight (6);
    presetButton.setBounds (prow);
    logoArea.setBounds (14, 6, 150, 66);   // where paintView draws the logo

    auto tabRow = r.removeFromTop (40).reduced (10, 6).withTrimmedBottom (0);
    for (auto& t : tabs)
    {
        t.setBounds (tabRow.removeFromLeft (118));
        tabRow.removeFromLeft (6);
    }

    auto content = r.reduced (10).withTrimmedTop (-4);
    for (auto& p : pages) p->setBounds (content);
}

void ElukkaAudioProcessorEditor::showPage (int index)
{
    currentPage = index;
    for (int i = 0; i < 6; ++i)
        pages[i]->setVisible (i == index);
    pages[index]->refresh();
}

void ElukkaAudioProcessorEditor::updatePresetButton()
{
    auto& pm = proc.presets;
    juce::String text = pm.currentName;
    if (pm.currentIndex >= 0 && pm.currentIndex < (int) pm.entries.size() && pm.entries[(size_t) pm.currentIndex].category != "Init")
        text = pm.entries[(size_t) pm.currentIndex].category + "  /  " + text;
    presetButton.setButtonText (text + juce::String::fromUTF8 ("  \xe2\x96\xbe"));

    // only presets you saved yourself can be deleted
    const bool isUser = pm.currentIndex >= 0 && pm.currentIndex < (int) pm.entries.size() && ! pm.entries[(size_t) pm.currentIndex].isFactory;
    deleteBtn.setEnabled (isUser);
    deleteBtn.setTooltip (isUser ? "Delete this preset (moves the file to the Trash)" : "Only your own saved presets can be deleted");
}

void ElukkaAudioProcessorEditor::showPresetMenu()
{
    auto& pm = proc.presets;
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> factoryMenus, userMenus;
    for (int i = 0; i < (int) pm.entries.size(); ++i)
    {
        auto& e = pm.entries[(size_t) i];
        const bool current = i == pm.currentIndex;
        if (e.isFactory && e.category == "Init") menu.addItem (i + 1, e.name, true, current);
        else if (e.isFactory) factoryMenus[e.category].addItem (i + 1, e.name, true, current);
        else userMenus[e.category].addItem (i + 1, e.name, true, current);
    }
    menu.addSeparator();
    for (auto& cat : PresetManager::categories())
        if (factoryMenus.count (cat)) menu.addSubMenu (cat, factoryMenus[cat]);
    if (! userMenus.empty())
    {
        menu.addSeparator();
        juce::PopupMenu user;
        for (auto& [cat, sub] : userMenus) user.addSubMenu (cat, sub);
        menu.addSubMenu ("User", user);
    }

    juce::Component::SafePointer<ElukkaAudioProcessorEditor> safe (this);
    // Draw the menu inside the plugin window instead of spawning a native popup window: clicks on a
    // separate popup window would not reach this editor's mouse listener, so keyboard focus would
    // stay with the host after picking a preset (and Ctrl+Z would go to the host).
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&presetButton)
                            .withParentComponent (this)
                            .withMinimumWidth (presetButton.getWidth()),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            safe->proc.presets.load (result - 1);
                            safe->updatePresetButton();
                        });
}

void ElukkaAudioProcessorEditor::stepPreset (int delta)
{
    auto& pm = proc.presets;
    const int n = (int) pm.entries.size();
    if (n == 0) return;
    const int pos = pm.currentIndex < 0 ? (delta > 0 ? 0 : n - 1) : (pm.currentIndex + delta + n) % n;
    pm.load (pos);
    updatePresetButton();
}

void ElukkaAudioProcessorEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Saved to Documents/ElukkaSynth/Presets", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.presets.currentName, "Name:");
    juce::StringArray cats = PresetManager::categories();
    cats.add ("User");
    w->addComboBox ("category", cats, "Category:");
    if (auto* box = w->getComboBoxComponent ("category"))
    {
        int idx = proc.presets.currentIndex >= 0 ? cats.indexOf (proc.presets.entries[(size_t) proc.presets.currentIndex].category) : -1;
        box->setSelectedItemIndex (idx >= 0 ? idx : cats.size() - 1);
    }
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1) return;
        auto name = w->getTextEditorContents ("name");
        auto* box = w->getComboBoxComponent ("category");
        auto cat = box != nullptr ? box->getText() : juce::String ("User");
        if (! proc.presets.save (name, cat))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset", "Could not save the preset.");
        updatePresetButton();
    }), true);
}

void ElukkaAudioProcessorEditor::deletePresetDialog()
{
    auto& pm = proc.presets;
    const int index = pm.currentIndex;
    if (index < 0 || index >= (int) pm.entries.size() || pm.entries[(size_t) index].isFactory) return;
    const auto file = pm.entries[(size_t) index].file;

    auto* w = new juce::AlertWindow ("Delete Preset",
                                     "Delete \"" + pm.entries[(size_t) index].name + "\"?\nThe file is moved to the Trash.",
                                     juce::MessageBoxIconType::WarningIcon);
    // Return and Escape both cancel, so only a click on Delete removes the preset
    w->addButton ("Delete", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey), juce::KeyPress (juce::KeyPress::returnKey));
    juce::Component::SafePointer<ElukkaAudioProcessorEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, index, file] (int result)
    {
        if (safe == nullptr || result != 1) return;
        auto& presets = safe->proc.presets;
        // the list may have changed while the dialog was open: only delete the same file
        if (index >= (int) presets.entries.size() || presets.entries[(size_t) index].file != file || ! presets.deletePreset (index))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Delete Preset", "Could not delete the preset.");
        safe->updatePresetButton();
    }), true);
}

bool ElukkaAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isAltDown()) return false;

    const auto c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    auto& pm = proc.presets;

    if (c == 'Z')
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastUndoTime < 150)   // auto-repeat: one key press = one step
            return true;

        const bool changed = mods.isShiftDown() ? pm.redo() : pm.undo();
        if (changed)
        {
            lastUndoTime = now;
            updatePresetButton();
            return true;
        }
    }
    else if (c == 'Y')
    {
        const bool changed = pm.redo();
        if (changed)
        {
            updatePresetButton();
            return true;
        }
    }

    return false;
}

void ElukkaAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Text fields need the focus themselves.
    auto* c = e.eventComponent;
    if (c != nullptr && (dynamic_cast<juce::TextEditor*> (c) != nullptr
                         || c->findParentComponentOfClass<juce::TextEditor>() != nullptr))
        return;

    grabKeyboardFocus();
}

void ElukkaAudioProcessorEditor::timerCallback()
{
   #if JUCE_MAC
    // the host attaches the editor to its window some time after creating it
    if (! hostWindowKept) hostWindowKept = keepHostWindowVisible (*this);
   #endif
    // If the DAW undid/redid a preset load in its own history, show the matching preset name.
    proc.presets.syncNameWithHost();

    const int v = proc.uiVersion.load();
    if (v != lastUiVersion)
    {
        lastUiVersion = v;
        for (auto& p : pages) p->refresh();
        updatePresetButton();
    }
    pages[currentPage]->tick();
}
