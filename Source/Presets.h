#pragma once
#include <JuceHeader.h>
#include "DSP/Sequencer.h"
#include "Params.h"

class ElukkaAudioProcessor;
struct ResynthData;

struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
    int shape[P::numMods] { 5, 0, 0 };   // ModShape preset index per mod unit (Mod 1 defaults to Decay)
    std::vector<float> gain, depth;   // optional harmonic tables
    int pattern = -1;                 // index into SeqPattern::factory(), -1 = Init pattern
    int image = -1;                   // generated resynthesis image (see presetImage), -1 = none
    float masterComp = 0.0f;          // dB added to the measured level trim (keeps loudness when mixes are converted)

    FactoryPreset& set (const juce::String& id, float v) { values.push_back ({ id, v }); return *this; }
};

class PresetManager : private juce::AudioProcessorParameter::Listener
{
public:
    explicit PresetManager (ElukkaAudioProcessor& p);
    ~PresetManager() override;

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

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearHistory();

    // If the DAW restored many parameters at once (its own undo/redo), show the matching preset name.
    void syncNameWithHost();

private:
    struct Snapshot
    {
        juce::ValueTree state;
        std::shared_ptr<ResynthData> resynth;
        float uiScale = 1.0f;
        int currentIndex = -1;
        juce::String currentName;
        bool isFactory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    class HistoryAction;
    class SnapshotAction;
    class ParamAction;

    // One user edit of one parameter. Knob gestures are recorded as these instead of whole states, so
    // undo only writes back the parameters the user touched (host automation keeps its values).
    struct ParamChange { int index = 0; float before = 0.0f, after = 0.0f; };

    void applyFactory (const FactoryPreset& p);
    bool applyPreset (const Entry& entry);
    bool applyPattern (const PatternEntry& entry);
    Snapshot captureSnapshot();
    bool restoreSnapshot (const Snapshot& snapshot);
    bool applyParamChanges (const std::vector<ParamChange>& changes, bool useBefore);
    juce::RangedAudioParameter* getParam (int index) const;
    static bool snapshotsEqual (const Snapshot&, const Snapshot&);
    static bool statesEqual (const juce::ValueTree&, const juce::ValueTree&);
    static bool parameterStatesEqual (const juce::ValueTree&, const juce::ValueTree&);
    void commitUnrecordedChanges();
    void commitGestureChanges();
    void rememberState (const Snapshot&);
    void applyNameFrom (const Snapshot&);

    // Every parameter gesture (a knob drag, a typed value, a reset) becomes its own history step.
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool gestureIsStarting) override;
    void attachToParameters();
    void detachFromParameters();
    int gestureDepth = 0;
    bool applyingState = false;        // preset load / undo in progress: gestures are not history steps
    int hostChangeBurst = 0;           // parameter changes since the editor last looked

    std::vector<juce::String> paramIds;               // getParameters() order, for state comparisons
    std::unique_ptr<std::atomic<bool>[]> hostMoved;   // parameter changed off the message thread = host/automation
    int numTrackedParams = 0;
    std::vector<std::pair<int, float>> gestureBefore; // denormalised value each parameter had when its gesture started

    std::vector<Snapshot> knownStates; // recent named states, for keeping the name in sync with the DAW's undo

    juce::UndoManager history { 20, 0 };
    Snapshot checkpoint;
    bool hasCheckpoint = false;
    ElukkaAudioProcessor& proc;
};
