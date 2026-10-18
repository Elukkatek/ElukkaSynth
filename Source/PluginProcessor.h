#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "DSP/Voice.h"
#include "DSP/Sequencer.h"
#include "DSP/FX.h"
#include "Presets.h"

class ElukkaAudioProcessor : public juce::AudioProcessor
{
public:
    ElukkaAudioProcessor();
    ~ElukkaAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ElukkaSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Full state (parameters + shapes + harmonic tables + pattern + resynth) as a tree.
    juce::ValueTree saveState();
    void loadState (const juce::ValueTree& t);

    // Non-parameter data edited by the UI (message thread)
    ModShape shapes[2];
    std::array<float, P::numHarmTable> harmGain {}, harmDepth {};
    SeqPattern pattern;

    void setResynth (std::shared_ptr<ResynthData> d);
    std::shared_ptr<ResynthData> getResynth() const { return std::atomic_load (&resynth); }

    void resetHarmTables();
    void seedRandom (juce::int64 seed) { rng.setSeed (seed); }   // for repeatable offline renders (tests)
    void bumpUiVersion() { ++uiVersion; }
    std::atomic<int> uiVersion { 0 };
    float uiScale = 1.0f;   // editor size, saved with the project

    // FX processing order (P::Fx values), edited by dragging the FX boxes; saved with presets and projects.
    std::array<std::atomic<int>, P::NumFx> fxOrder {};
    void resetFxOrder() { for (int i = 0; i < P::NumFx; ++i) fxOrder[(size_t) i] = P::defaultFxOrder[i]; }
    std::atomic<float> uiOttDb[3] {};   // OTT gain change per band (dB) for the meters
    std::atomic<float> uiSh { 0.0f };
    std::atomic<bool> uiPlaying { false };   // any voice sounding (the mod displays only move while notes play)
    std::atomic<float> uiRsPos { -1.0f };   // resynth playback position (0..1) of the newest voice, -1 = not playing

    // Live values for the UI
    std::atomic<float> uiMod[2] {};
    std::atomic<float> globalModValue[2] {};   // the free-running mod units that drive FX destinations (for tests)
    std::atomic<double> uiHostBpm { 120.0 };
    SeqEngine seq;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

private:
    struct OscRaw { std::atomic<float> *on, *wave, *level, *coarse, *fine, *phase, *rand, *phaseMode, *uniMode, *uniVoices, *uniDetune, *uniBlend, *uniWidth, *fmSrc, *fmAmt, *pan, *bassMono, *toFilter; };
    struct ModRaw { std::atomic<float> *mode, *rate, *sync, *syncRate, *start, *retrig, *smooth; };
    struct MxRaw  { std::atomic<float> *src, *dst, *amt; };

    std::atomic<float>* raw (const juce::String& id) { return apvts.getRawParameterValue (id); }
    void cacheParams();
    void buildContext (double bpm);
    void handleMidi (const juce::MidiMessage& m);
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, bool hostPlaying, double ppq);
    void renderChunk (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, bool hostPlaying, double ppq);
    void voiceOn (int note, float vel, bool legato, float glideSec, bool forceMono);
    void releaseNote (int note);
    int findVoice();

    OscRaw oscRaw[P::numOscs] {};
    ModRaw modRaw[2] {};
    MxRaw mxRaw[P::numMatrixSlots] {};
    std::atomic<float> *pMaster {}, *pPoly {}, *pGlide {}, *pBend {}, *pA {}, *pD {}, *pS {}, *pR {};
    std::atomic<float> *hmStretch {}, *hmOddEven {}, *hmCutoff {}, *hmRes {}, *hmPrism {}, *hmPrismCenter {}, *hmPrismWidth {}, *hmShimmer {}, *hmShimmerRate {}, *hmTableSrc {}, *hmTableAmt {};
    std::atomic<float> *rsTune {}, *rsGate {}, *rsLift {}, *rsSpeed {}, *rsSync {}, *rsBpm {}, *rsPos {}, *rsLoop {};
    std::atomic<float> *sqOn {}, *sqRate {}, *sqGate {}, *sqSlide {}, *sqAccent {};
    std::atomic<float> *distOn {}, *distType {}, *distDrive {}, *distMix {}, *combOn {}, *combFreq {}, *combFb {}, *combMix {};
    std::atomic<float> *eqOn {}, *eqLow {}, *eqMidF {}, *eqMid {}, *eqHigh {}, *dlyOn {}, *dlyDiv {}, *dlyFb {}, *dlyMix {}, *dlyPp {};
    std::atomic<float>* sqRetrig {};
    std::atomic<float> *fxfOn {}, *fxfType {}, *fxfSlope {}, *fxfCutoff {}, *fxfRes {}, *fxfDrive {};
    std::atomic<float> *revOn {}, *revSize {}, *revDecay {}, *revTone {}, *revMod {}, *revMode {}, *revMix {}, *macro1 {}, *macro2 {};
    std::atomic<float> *eqLowF {}, *eqLowQ {}, *eqMidQ {}, *eqHighF {}, *eqHighQ {};
    std::atomic<float> *combLp {}, *combHp {}, *dlySync {}, *dlyTime {}, *dlyLp {}, *dlyHp {};
    std::atomic<float> *fltType {}, *fltSlope {}, *fltCutoff {}, *fltRes {}, *fltEnv {}, *fltKey {}, *fltDrive {};
    std::atomic<float> *fltA {}, *fltD {}, *fltS {}, *fltR {}, *shRate {};   // shRate = sh_syncRate
    std::atomic<float> *ottOn {}, *ottAmount {}, *ottSpeed {}, *rsSubLift {}, *rsBassFreq {};
    std::atomic<float> *shSmooth {}, *shRetrig {};
    float lastMasterGain = -1.0f;

    juce::NormalisableRange<float> destRanges[P::NumDst];

    double sr = 44100.0;
    Voice voices[P::maxVoices];
    VoiceContext ctx;
    FXParams fxBase;
    FXChain fx;
    FFTBank fft;
    std::vector<float> work;
    juce::Random rng;
    ModUnit globalMods[2];
    std::vector<SeqEvent> seqEvents;

    std::shared_ptr<ResynthData> resynth;
    // Replaced sources, kept (message thread) until nothing else holds them, so they are never freed while the audio
    // thread plays them or by the audio thread itself.
    std::vector<std::shared_ptr<ResynthData>> retiredResynth;
    std::shared_ptr<ResynthData> blockResynth;   // audio thread: the source the current block plays
    // Bumped after each change of `resynth`; the audio thread re-reads the pointer only when it changes (reading a
    // shared_ptr atomically takes a lock).
    std::atomic<int> resynthVersion { 0 };
    int blockResynthVersion = -1;
    std::vector<int> heldNotes;
    uint64_t ageCounter = 0;
    int seqVoice = 0;   // voice playing the sequencer's current note (each new note gets a fresh voice, so releases ring out)
    float modWheel = 0.0f, bendSemis = 0.0f, lastVelocity = 1.0f;
    float globalOff[P::NumDst] {};
    double shBeat = 0.0;            // beat position driving the S&H steps
    int64_t shStep = std::numeric_limits<int64_t>::min();
    float shValue = 0.0f, shSmoothed = 0.0f;   // last global modulation offsets (sequencer values use these)
    int lastNote = 60;
    bool lastSeqOn = false;
    int lastPolyMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElukkaAudioProcessor)
};
