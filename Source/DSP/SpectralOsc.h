#pragma once
#include <JuceHeader.h>
#include "Unison.h"

// Additive engine.
// Each oscillator's harmonic spectrum (amplitude per harmonic) is computed at control rate,
// turned into a band-limited single-cycle table by an inverse FFT, and played back by the
// unison voices. Inharmonic (stretched) spectra are rendered into a table holding 8 cycles
// of the fundamental, which lets partials sit on 1/8-harmonic positions.

constexpr int maxHarmonics = 512;
constexpr int stretchCycles = 8;
constexpr int maxTableSize = 16384;
constexpr int maxUnison = 16;

class FFTBank
{
public:
    FFTBank();
    // Inverse real FFT of `order`. `data` holds 2*N floats: interleaved complex bins in, N real samples out.
    void inverse (int order, float* data) const { ffts[(size_t) (order - minOrder)]->performRealOnlyInverseTransform (data); }
    // Multiply the imaginary part of a bin by this to get a sine of amplitude 1.
    float sineScale (int order) const { return scales[(size_t) (order - minOrder)]; }

    static constexpr int minOrder = 8, maxOrder = 14;

private:
    std::vector<std::unique_ptr<juce::dsp::FFT>> ffts;
    std::vector<float> scales;
};

struct WaveTable
{
    std::vector<float> data = std::vector<float> ((size_t) maxTableSize + 1, 0.0f);
    int size = 256;           // power of two
    int cycles = 1;           // 1 or stretchCycles
    int cycleShift = 0;       // log2 (size / cycles)
    bool silent = true;

    inline float read (float phaseCycles) const noexcept
    {
        float idx = phaseCycles * (float) (1 << cycleShift);
        float fl = std::floor (idx);
        int i = ((int) fl) & (size - 1);
        float frac = idx - fl;
        return data[(size_t) i] + (data[(size_t) i + 1] - data[(size_t) i]) * frac;
    }
};

// Builds a table from harmonic amplitudes. Returns false if the result is silent.
void buildWaveTable (WaveTable& t, const float* amps, int numHarm, float stretch, float f0, double sampleRate,
                     const FFTBank& fft, float* work);

// Base harmonic spectra for the classic waveforms (signed sine amplitudes).
void baseSpectrum (int wave, float* amps, int numHarm);

class SpectralOsc
{
public:
    void prepare (double sampleRate);

    // Call on note-on: sets each unison voice's starting phase (see Unison::initialPhase).
    void resetPhases (int voices, int phaseMode, float startPhase, float amount, juce::Random& rng);

    // Per control block: set pitch/unison and (optionally) a new spectrum.
    // Frequencies and gains glide to the new values across the block (no steps when detune/width/pan change).
    // pan: -1..1, shifts the whole stereo image (see Unison::panGains).
    void setUnison (int voices, float detuneCents, int detuneMode, float blend, float width, float f0, float pan = 0.0f);

    float getPhase (int k) const { return phases[k]; }      // cycles of the fundamental (for tests)
    const float* getPhases() const { return phases; }
    float getBassPhase() const { return bassPhase; }
    const float* getIncrements() const { return incs; }
    float getBassIncrement() const { return bassInc; }
    float getIncrement (int k) const { return incTarget[k]; }
    // bassFreq > 0 turns on Bass Mono: harmonics below it are played by one centred, undetuned voice
    // instead of the unison voices (with a smooth crossover, see Unison::bassSplit).
    void setSpectrum (const float* amps, int numHarm, float stretch, float f0, const FFTBank& fft, float* work,
                      float bassFreq = 0.0f, bool singleVoice = false);
    void setNoise (bool isNoise) { noise = isNoise; }

    void beginBlock (int numSamples, float levelStart, float levelEnd);

    // One sample. pm = phase modulation in cycles. Adds stereo output to l/r, returns mono for FM.
    inline float tick (float pm, float& l, float& r, juce::Random& rng) noexcept
    {
        float lev = level;
        level += levelInc;

        if (noise)
        {
            float nl = rng.nextFloat() * 2.0f - 1.0f;
            float nr = rng.nextFloat() * 2.0f - 1.0f;
            l += nl * lev * 0.5f * panGL;
            r += nr * lev * 0.5f * panGR;
            panGL += dPanL;
            panGR += dPanR;
            return (nl + nr) * 0.25f;
        }

        const WaveTable& cur = tables[curTable];
        const WaveTable& prev = tables[1 - curTable];
        float fade = xfade;
        xfade = juce::jmax (0.0f, xfade - xfadeInc);

        float mono = 0.0f, bassOut = 0.0f;
        const float wrap = (float) stretchCycles;
        if (bassOn)
        {
            const float ph = bassPhase + pm;
            float s = bassTables[curTable].read (ph);
            if (fade > 0.0f) s += (bassTables[1 - curTable].read (ph) - s) * fade;
            bassPhase += bassInc;
            bassInc += dBassInc;
            if (bassPhase >= wrap) bassPhase -= wrap;
            bassOut = s;
            l += s * bassL * lev;
            r += s * bassR * lev;
            bassL += dBassL;
            bassR += dBassR;
        }
        for (int k = 0; k < numVoices; ++k)
        {
            float ph = phases[k] + pm;
            float s = cur.read (ph);
            if (fade > 0.0f) s += (prev.read (ph) - s) * fade;
            phases[k] += incs[k];
            incs[k] += dIncs[k];
            if (phases[k] >= wrap) phases[k] -= wrap;
            mono += s;
            l += s * gainL[k] * lev;
            r += s * gainR[k] * lev;
            gainL[k] += dgL[k];
            gainR[k] += dgR[k];
        }
        return mono * norm + bassOut;
    }

    bool isSilent() const { return ! noise && tables[curTable].silent && bassTables[curTable].silent; }

private:
    double sr = 44100.0;
    WaveTable tables[2], bassTables[2];   // unison part and Bass Mono part of the spectrum
    int curTable = 0;
    float xfade = 0.0f, xfadeInc = 0.0f;
    bool pendingFade = false;
    bool noise = false;

    std::vector<float> lastAmps = std::vector<float> (2 * maxHarmonics, 0.0f);   // unison amps, then bass amps
    std::vector<float> splitAmps = std::vector<float> (2 * maxHarmonics, 0.0f);
    int lastNumHarm = -1;
    float lastStretch = 0.0f;

    int numVoices = 1;   // voices being rendered (includes voices still fading out)
    float phases[maxUnison] {}, incs[maxUnison] {}, gainL[maxUnison] {}, gainR[maxUnison] {};
    // Unison gains glide to their targets across each block, so changing the voice count or
    // blend/width never makes voices pop in or out.
    float targetL[maxUnison] {}, targetR[maxUnison] {}, dgL[maxUnison] {}, dgR[maxUnison] {};
    float incTarget[maxUnison] {}, dIncs[maxUnison] {};
    bool snapGains = true;
    // Bass Mono voice: centred (pan does not move it), at the played pitch, starts with the centre voice's phase
    bool bassOn = false;
    int centre = 0;
    float panGL = 1.0f, panGR = 1.0f, panTgtL = 1.0f, panTgtR = 1.0f, dPanL = 0.0f, dPanR = 0.0f;   // pan, for the noise wave
    float bassPhase = 0.0f, bassInc = 0.0f, bassIncTarget = 0.0f, dBassInc = 0.0f;
    float bassL = 0.0f, bassR = 0.0f, bassTargetL = 0.0f, bassTargetR = 0.0f, dBassL = 0.0f, dBassR = 0.0f;
    float norm = 1.0f;
    float level = 0.0f, levelInc = 0.0f;
};
