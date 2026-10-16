#pragma once
#include <JuceHeader.h>
#include "DSP/Sequencer.h"

class ElukkaAudioProcessor;

struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
    int shape[2] { 5, 0 };            // ModShape preset index per mod unit (Mod 1 defaults to Decay)
    std::vector<float> gain, depth;   // optional harmonic tables
    int pattern = -1;                 // index into SeqPattern::factory(), -1 = Init pattern
    int image = -1;                   // generated resynthesis image (see presetImage), -1 = none
    float masterComp = 0.0f;          // dB added to the measured level trim (keeps loudness when mixes are converted)

    FactoryPreset& set (const juce::String& id, float v) { values.push_back ({ id, v }); return *this; }
};

class PresetManager
{
public:
    explicit PresetManager (ElukkaAudioProcessor& p) : proc (p) {}

    static const juce::StringArray& categories();
    static const std::vector<FactoryPreset>& factory();
    static juce::File userPresetDir();
    static juce::File userPatternDir();

    struct Entry { juce::String name, category; bool isFactory; int index; juce::File file; };
    std::vector<Entry> entries;
    int currentIndex = -1;
    juce::String currentName { "Init" };

    void refresh();
    void load (int entryIndex);
    bool save (const juce::String& name, const juce::String& category);
    bool deletePreset (int entryIndex);   // user presets only: moves the file to the Trash

    // Sequencer pattern presets
    struct PatternEntry { juce::String name; bool isFactory; int index; juce::File file; };
    std::vector<PatternEntry> patterns;
    void refreshPatterns();
    void loadPattern (int index);
    bool savePattern (const juce::String& name);

private:
    void applyFactory (const FactoryPreset& p);
    ElukkaAudioProcessor& proc;
};
