#pragma once
#include "SpectralOsc.h"
#include "ModUnit.h"
#include "Resynth.h"
#include "PartialBank.h"
#include "Filter.h"
#include "Envelope.h"

struct OscParams
{
    bool on = false; int wave = P::WSaw; float level = 0.8f, coarse = 0.0f, fine = 0.0f;
    float phase = 0.0f, rand = 0.0f;   // degrees, percent (raw parameter values); rand = phase Amount
    int phaseMode = 0, uniMode = 0;    // Unison::PhaseMode, Unison::DetuneMode
    int uniVoices = 1; float uniDetune = 20.0f, uniBlend = 75.0f, uniWidth = 100.0f; int fmSrc = 0; float fmAmt = 0.0f;   // detune in cents
    float pan = 0.0f; bool bassMono = false;   // pan in %
    bool toFilter = true;                      // false: the oscillator bypasses the voice filter
};

struct HarmParams
{
    float stretch = 0, oddEven = 0, cutoff = 20000, res = 0, prism = 0, prismCenter = 8, prismWidth = 2, shimmer = 0, shimmerRate = 1;   // prism/shimmer in %
    int tableSrc = 0; float tableAmt = 0;
};

struct MatrixSlot { int src = 0, dst = 0; float amt = 0.0f; };

struct FilterParams
{
    int type = 0, slope = 0;
    float cutoff = 20000.0f, res = 0.0f, env = 0.0f, key = 0.0f, drive = 0.0f;   // key in %
};

// Everything a voice needs for one block, snapshotted by the processor.
struct VoiceContext
{
    double sampleRate = 44100.0;
    OscParams osc[P::numOscs];
    HarmParams harm;
    ModUnitParams mod[P::numMods];
    const ModShape* shapes[P::numMods] {};
    const float* harmGain = nullptr;
    const float* harmDepth = nullptr;
    MatrixSlot matrix[P::numMatrixSlots];
    const juce::NormalisableRange<float>* destRanges = nullptr;
    float modWheel = 0, macro1 = 0, macro2 = 0, accent = 0, bendWheel = 0, bendRange = 2;
    const ResynthData* resynth = nullptr;
    // Resynth values are raw parameter values (tune/lift in %); sync uses hostBpm / source BPM.
    float rsTune = 0.0f, rsGate = -80.0f, rsLift = 0.0f, rsSpeed = 1.0f, rsBpm = 120.0f, rsPos = 0.0f; bool rsLoop = true, rsSync = true;
    double hostBpm = 120.0;
    bool hostPlaying = false;
    double ppq = 0.0;   // song position (beats) at the start of this block, while the host plays
    float rsSubLift = 0.0f, rsBassFreq = 120.0f;   // rsBassFreq is also every oscillator's Bass Mono crossover
    juce::ADSR::Parameters amp;
    FilterParams filter;
    juce::ADSR::Parameters filterEnv;
    float sh = 0.0f;   // current S&H random value
    double freeRunPhase[P::numMods] {};
    const FFTBank* fft = nullptr;
    float* work = nullptr;
    juce::Random* rng = nullptr;
};

// Applies a normalised modulation offset to a parameter value through its range.
inline float applyMod (const juce::NormalisableRange<float>& r, float value, float offset)
{
    if (offset == 0.0f) return value;
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, r.convertTo0to1 (value) + offset));
}

inline void evalMatrix (const VoiceContext& c, const float* src, bool perVoice, float* off)
{
    std::fill (off, off + P::NumDst, 0.0f);
    for (auto& s : c.matrix)
        if (s.src != P::SrcNone && s.dst != P::DstNone && P::dests[s.dst].perVoice == perVoice)
            off[s.dst] += s.amt * src[s.src];
}

class Voice
{
public:
    static constexpr int controlBlock = 128;

    void prepare (double sampleRate);
    void start (int note, float velocity, const VoiceContext& c, float glideSec, bool glideFromCurrent);
    void legatoTo (int note, float velocity, float glideSec);
    void release() { held = false; amp.noteOff(); filterEnv.noteOff(); }
    void kill() { active = false; amp.reset(); filterEnv.reset(); }

    // n <= controlBlock. Adds into L/R.
    void render (float* L, float* R, int n, const VoiceContext& c);

    bool active = false, held = false;
    int note = 60;
    uint64_t age = 0;
    float envValue = 0.0f;
    float modValue[P::numMods] {};
    double rsPlayTime = 0.0;   // resynth source position (seconds) used in the last block, for the UI

private:
    void computeSpectrum (int wave, float f0, const HarmParams& h, float tableSrc, const VoiceContext& c, float* amps, int& numHarm);
    // Gain the harmonic section applies to a partial at harmonic number hn (may be fractional) and frequency f.
    static float partialGain (float hn, float f, const HarmParams& h, float tableMod, const VoiceContext& c);

    double sr = 44100.0;
    Envelope amp, filterEnv;
    VoiceFilter filter;
    ModUnit mods[P::numMods];
    SpectralOsc oscs[P::numOscs];
    PartialBank banks[P::numOscs];
    float levels[P::numOscs] {};
    // Prism/Shimmer: once they move one of an additive oscillator's harmonics 2..64 during a note, that harmonic
    // is played by the oscillator's sine bank (banks[i]) for the rest of the note. Bit h-2 = harmonic h.
    uint64_t harmMoved[P::numOscs] {};
    Mangle::Drift drift;
    std::vector<float> tableAmps = std::vector<float> (maxHarmonics, 0.0f);
    float velocity = 1.0f;
    float pitch = 60.0f, targetPitch = 60.0f, glideTime = 0.0f;
    double rsTime = 0.0;
    float lastRsPos = 0.0f;   // Position (0..1) the current playback was started from
    // Position jumps crossfade: fadeBanks[i] is a copy of banks[i] taken at the jump that keeps playing the old
    // position (fadeTime) while banks[i] restarts at the new one; equal-power over posFadeSamples. A further
    // jump waits until the fade has finished. Allocated in prepare() (voices may live on the stack in tests).
    std::vector<PartialBank> fadeBanks;
    double fadeTime = 0.0;
    int posFadeSamples = 1, posFadeDone = 0;
    bool posFading = false;
    float fmOut[P::numOscs] {};
    // Sine banks rendered a block at a time (when nothing phase-modulates them): their left/right/mono output.
    // Allocated in prepare() (processors live on the stack in tests).
    static_assert (controlBlock <= PartialBank::maxBlock, "renderBlock handles one control block");
    struct BankBuffers
    {
        float out[P::numOscs][3][controlBlock];
        PartialBank::Scratch scratch;
    };
    std::unique_ptr<BankBuffers> bankBuf;
    float lastOff[P::NumDst] {};   // previous block's modulation, for values read at note-on
    std::vector<float> amps = std::vector<float> (maxHarmonics, 0.0f);

    // De-click: when a sounding voice is restarted (retrigger, voice stealing, sequencer) the jump between
    // the old and new waveform is bridged by an offset that fades out over a few milliseconds.
    float lastOutL = 0.0f, lastOutR = 0.0f, declickL = 0.0f, declickR = 0.0f, declickCoef = 0.99f;
    float lastPreL = 0.0f, lastPreR = 0.0f, preDeclickL = 0.0f, preDeclickR = 0.0f;   // same, before the filter
    bool declickPending = false;
    bool filterEngaged = false;   // once the filter runs during a note it keeps running until the note ends
};
