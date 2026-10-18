#pragma once
#include <JuceHeader.h>

// TB-303 style pattern: time and pitch are programmed separately.
// Every "Note" step consumes the next pitch entry; "Tie" holds the previous note; "Rest" is silent.
// A pitch entry's slide glides into the following note without retriggering.
enum class StepType : uint8_t { Note = 0, Tie = 1, Rest = 2 };

struct PitchEntry
{
    static constexpr int range = 36;   // semitones up or down
    int semis = 0;       // semitones from the held key, -range..+range
    bool accent = false;
    bool slide = false;
};

struct SeqPattern
{
    static constexpr int maxSteps = 64;

    juce::String name { "Init" };
    int length = 16;      // time steps, 1..64 (4 bars of 16ths)
    int numPitches = 16;  // pitch entries, 1..64
    int startStep = 0;    // playback starts here and wraps around (0-based, < length)
    std::array<StepType, maxSteps> steps {};
    std::array<PitchEntry, maxSteps> pitches {};

    SeqPattern() { steps.fill (StepType::Note); }

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& t);

    // Compact text form used for factory patterns:
    // time: 'x' note, '-' tie, '.' rest. pitch tokens: note name with optional '^'/'v' prefix
    // (octave) and '!' (accent) / '~' (slide) suffixes, e.g. "C! ^C D#~ vG".
    static SeqPattern fromText (const juce::String& name, const juce::String& time, const juce::String& pitch);
    static SeqPattern generate (const juce::String& name, int seed, int length, const std::vector<int>& scale,
                                float density, float tieProb, float accentProb, float slideProb, float octaveProb);

    static const std::vector<SeqPattern>& factory();
    static int initIndex();   // the plain "Init" pattern: 16 notes on the root
};

struct SeqEvent
{
    int offset;
    bool on;
    int note;
    float velocity;
    bool accent;
    bool legato;
};

class SeqEngine
{
public:
    void reset() { running = false; sounding = false; lastStep = std::numeric_limits<int64_t>::min(); pendingOff = -1; restartPending = false; }
    // Retrigger: the next process() call restarts the pattern from its start step at its first sample, wherever the
    // song position is (used on every new note when Retrigger is on)
    void retrigger() { restartPending = true; }

    void process (const SeqPattern& pat, int numSamples, double sampleRate, double bpm, bool hostPlaying, double ppqStart,
                  bool keyHeld, int rootNote, double stepBeats, float gate, float accentAmount, std::vector<SeqEvent>& out);

    std::atomic<int> displayStep { -1 };
    float accentValue = 0.0f;

private:
    void triggerStep (const SeqPattern& pat, int64_t absStep, int offset, int rootNote, double stepSamples,
                      float gate, float accentAmount, std::vector<SeqEvent>& out);

    bool running = false, sounding = false, currentSlide = false, restartPending = false;
    int currentNote = 60;
    int64_t lastStep = std::numeric_limits<int64_t>::min();
    double internalBeat = 0.0;
    // Steps count from this beat (the retriggering note's song position); valid only for the clock it was taken from
    double originBeat = 0.0;
    bool originHost = false;
    int64_t pendingOff = -1;
};
