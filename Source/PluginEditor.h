#pragma once
#include "PluginProcessor.h"
#include "UI/Pages.h"

class ElukkaAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ElukkaAudioProcessorEditor (ElukkaAudioProcessor&);
    ~ElukkaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    static constexpr int baseWidth = 1140, baseHeight = 790;

private:
    void timerCallback() override;
    bool hostWindowKept = false;   // see keepHostWindowVisible
    void showPage (int index);
    void updatePresetButton();
    void showPresetMenu();
    void stepPreset (int delta);
    void holdKeyboardFocus();
    void savePresetDialog();
    void deletePresetDialog();
    static juce::Image makeLogo();
    void paintView (juce::Graphics&);

    // Everything is laid out at the base size inside `view`, which is scaled to the window size.
    struct View : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    ElukkaAudioProcessor& proc;
    ElukkaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image logo;
    View view;

    // Clicking the logo shows the credits in a small pop-up.
    struct LogoArea : juce::Component
    {
        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent& e) override { if (onClick && ! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition())) onClick(); }
    };
    LogoArea logoArea;
    void showCredits();

    juce::TextButton tabs[6];
    std::unique_ptr<Page> pages[6];
    int currentPage = 0;

    juce::TextButton presetButton;   // opens the preset menu (categories open on hover)
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, resetBtn { "Reset" }, saveBtn { "Save" }, deleteBtn { "Delete" };
    Knob macro1, macro2, master;

    int lastUiVersion = -1;
    bool wantsKeyboard = false;   // keep the keyboard until the user clicks outside the plugin
    bool presetMenuOpen = false;
    juce::uint32 lastUndoTime = 0;   // ignore key auto-repeat so holding Ctrl+Z does not skip steps
};
