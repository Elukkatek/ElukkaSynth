#pragma once
#include "Resynth.h"
#include "SpectralOsc.h"
#include "Mangle.h"

// Plays a ResynthData model: one sine oscillator per partial slot (per unison voice) plus the noise residual.
//
// Partials: each block the oscillator's increment is set to the source's mean frequency over the block (from the
// interval frequencies, which were derived from the measured phases), so integrating it keeps every partial in
// phase with the source. Phases are only ever set when a partial (re)starts from silence, from the measured phase;
// levels ramp linearly across the block. Pitch, Tune, Stretch, Prism, Shimmer and FM act on the mean frequency.
// Tune moves each note of the source (ResynthData::noteMidi) to the nearest note of the key's pitch class
// (ResynthData::tuneTarget): a partial's correction glides over ~10 ms when its note changes, so reassignments never
// step the pitch; the residual bands follow their nearest note.
//
// Residual: the noise spectrogram is played by overlap-adding Hann-windowed frames of random-phase noise shaped
// to the source's band levels (shifted with the played note), with the measured left/right coherence.
class PartialBank
{
public:
    // Per-partial gain from the harmonic processing section: (frequency Hz, harmonic number f/f0) -> gain.
    // A non-owning reference to the caller's callable (std::function would allocate on the audio thread); it is
    // only used during the call it is passed to.
    class GainFn
    {
    public:
        template <typename F>
        GainFn (const F& f) noexcept : obj (&f), call ([] (const void* o, float a, float b) { return (*static_cast<const F*> (o)) (a, b); }) {}
        float operator() (float f, float h) const { return call (obj, f, h); }

    private:
        const void* obj;
        float (*call) (const void*, float, float);
    };

    struct BlockParams
    {
        double timeSec = 0.0;    // source playback position at the start of the block
        double blockSec = 0.0;   // source time the block covers (levels are aimed at its end)
        float f0 = 261.63f;      // played pitch; equals ResynthData::rootHz at middle C
        float tune = 0.0f;       // 0 = the key transposes the source (middle C = original); 1 = every note of the source
                                 // moved to the nearest note of the key's pitch class, in its own register, plus the
                                 // key's octave from middle C (see ResynthData::tuneTarget)
        float lift = 0.0f;       // raises quiet partials
        float stretch = 0.0f;
        int unison = 1;
        float detuneCents = 0.0f, blend = 0.75f, width = 1.0f;
        int detuneMode = Unison::DetuneLinear;
        float phaseOffset = 0.0f;   // cycles of the fundamental, from Phase modulation
        float bassFreq = 120.0f;    // partials below this are the "bass" region
        bool bassMono = false;      // bass partials played by one centred, undetuned voice (smooth crossover)
        float pan = 0.0f;           // -1..1, shifts the whole stereo image
        float subLift = 0.0f;       // Lift for the bass region (lift applies above it)
        float gateDb = -80.0f;      // Gate (before Lift): partials more than this below the loudest partial at the moment
                                    // fade out over a 6 dB knee below it; -80 = off
        float fmDevPerRatio = 0.0f; // FM: peak frequency deviation (Hz) of a partial at harmonic ratio 1
        float fmModHz = 0.0f;       // FM: modulator frequency
        float prism = 0.0f, prismCenter = 8.0f, prismWidth = 2.0f;   // Prism amount 0..1 (see Mangle::prism)
        float shimmer = 0.0f;       // 0..1
        const float* drift = nullptr;   // per-partial random walk for Shimmer (Mangle::Drift)
        const FFTBank* fft = nullptr;   // for the residual's inverse transforms (none = no residual)
    };

    // Harmonics 2..64 of an additive oscillator, used while Prism/Shimmer move them (the wavetable plays the rest).
    static constexpr int numHarmonicSlots = Mangle::maxMovedHarmonic - 1;

    void prepare (double sampleRate);
    // Note-on: each partial gets a base phase shared by its unison copies (fixed per partial in Reset mode,
    // so triggers repeat; random otherwise), plus each copy's offset from Unison::initialPhase.
    void resetPhases (int voices, int phaseMode, float startPhase, float amount, juce::Random& rng);

    // Sets the targets for the next block of n samples.
    void setBlock (const ResynthData& d, const BlockParams& p, const GainFn& gain, int n, float levelStart, float levelEnd);

    // Additive oscillator mode: amps[i] is harmonic i+1's amplitude (as for the wavetable); plays the harmonics
    // 2..64 whose bit (h - 2) is set in `which`.
    void setHarmonics (const float* amps, int numHarm, uint64_t which, const BlockParams& p, int n, float levelStart, float levelEnd);
    // Takes over harmonics from the wavetable without a click: sets each partial's phase to where the
    // wavetable's copy of it is (voicePhases/voiceIncs = the wavetable's unison voice phases and per-sample
    // increments, in cycles of the fundamental).
    void alignHarmonics (const float* voicePhases, const float* voiceIncs, float bassVoicePhase, float bassVoiceInc, float stretch, uint64_t which);

    // Longest block renderBlock() handles (the voice's control block).
    static constexpr int maxBlock = 128;
    // Working memory for renderBlock(); one per voice, allocated outside the audio callback.
    struct Scratch
    {
        float sumL[maxBlock], sumR[maxBlock], sumM[maxBlock], bass[maxBlock];
        float uniL[maxUnison][maxBlock], uniR[maxUnison][maxBlock];
    };
    // The whole block (n <= maxBlock samples) without phase modulation: the same output as n calls of tick (0, l, r)
    // (to float rounding), added to outL/outR, with the values tick() would return added to mono. Each partial's
    // unison sines are computed 4 samples at a time with a polynomial sine; tick() is kept for FM, where the
    // modulator's output is only known sample by sample.
    void renderBlock (int n, float* outL, float* outR, float* mono, Scratch& scratch) noexcept;

    // One sample. pm = phase modulation in cycles of the fundamental. Adds stereo output, returns mono for FM.
    inline float tick (float pm, float& l, float& r) noexcept
    {
        const float lev = level;
        level += levelInc;
        float mono = 0.0f, outL = 0.0f, outR = 0.0f, bassOut = 0.0f;

        for (int a = 0; a < numActive; ++a)
        {
            const int s = active[a];
            auto& sl = slots[s];
            const float aL = sl.ampL, aR = sl.ampR;
            sl.ampL += sl.dAmpL;
            sl.ampR += sl.dAmpR;
            const float inc = sl.inc;
            const float pmOff = (pm + phaseOffset) * sl.ratio;
            sl.ratio += sl.dRatio;
            float* ph = phases[s];
            const float sph = sl.sph;
            sl.sph += sl.dSph;
            if (std::abs (sph) < 0.002f && sl.dSph == 0.0f)
            {
                for (int u = 0; u < numUnison; ++u)
                {
                    float p = ph[u] + inc * uniMul[u];
                    p -= (float) (int) p;
                    ph[u] = p;
                    const float v = sine (p + pmOff);
                    mono += v * (aL + aR);
                    outL += v * aL * uniL[u];
                    outR += v * aR * uniR[u];
                }
            }
            else
            {
                // stereo-wide partial: the two sides half a stereo phase either way of the partial's phase
                const float half = 0.5f * sph;
                for (int u = 0; u < numUnison; ++u)
                {
                    float p = ph[u] + inc * uniMul[u];
                    p -= (float) (int) p;
                    ph[u] = p;
                    const float vL = sine (p + pmOff - half), vR = sine (p + pmOff + half);
                    mono += 0.5f * (vL + vR) * (aL + aR);
                    outL += vL * aL * uniL[u];
                    outR += vR * aR * uniR[u];
                }
            }
            // Bass Mono voice: always advanced so it stays in step with the centre unison voice
            float pb = bassPhase[s] + inc;
            pb -= (float) (int) pb;
            bassPhase[s] = pb;
            const float aB = sl.ampB;
            sl.ampB += sl.dAmpB;
            if (aB > 0.0f || sl.dAmpB > 0.0f)
            {
                const float v = sine (pb + pmOff) * aB;
                bassOut += v;
                outL += v * bassL;
                outR += v * bassR;
            }
        }
        for (int u = 0; u < numUnison; ++u)
        {
            uniL[u] += dUniL[u];
            uniR[u] += dUniR[u];
        }
        bassL += dBassL;
        bassR += dBassR;
        if (noiseOn)
        {
            if (noisePos == noiseHop) { generateNoiseFrame (0); noisePos = 0; }
            outL += noiseBufL[noiseRead];
            outR += noiseBufR[noiseRead];
            noiseBufL[noiseRead] = noiseBufR[noiseRead] = 0.0f;
            noiseRead = (noiseRead + 1) & (noiseSize - 1);
            ++noisePos;
        }
        l += outL * lev;
        r += outR * lev;
        return mono * 0.5f * uniNorm + bassOut;
    }

    bool isSilent() const { return numActive == 0 && ! noiseOn; }

    // Silences every partial and the residual at once (no ramp), so the next setBlock starts them afresh from the
    // measured phases. Used when Position jumps: the caller crossfades from a copy of the bank as it was.
    void restart();

private:
    float beginBlock (const BlockParams& p, int n, float levelStart, float levelEnd);   // returns the Nyquist limit
    // startPhase >= 0: if the partial (re)starts from silence this block, it starts at this phase (cycles)
    void setSlot (int s, float f, float h, float a, float pan, const BlockParams& p, float nyquist, float startPhase = -1.0f,
                  float stereoPhase = 0.0f);
    static float mangle (float h, const BlockParams& p, int index);
    // transposeCents = shift from the key (already scaled by 1 - Tune); keyMidi = the played key for Tune's targets (tuning = false: none)
    void setNoiseBlock (const ResynthData& d, const BlockParams& p, const GainFn& gain, double timeSec, float transposeCents, float keyMidi, bool tuning,
                        float panL, float panR, int n);
    // adds one Hann-windowed noise frame starting `offset` samples from the read position (offset <= 0)
    void generateNoiseFrame (int offset);

    static inline float sine (float cycles) noexcept
    {
        float x = cycles - std::floor (cycles);
        float idx = x * (float) tableSize;
        int i = (int) idx;
        float t = idx - (float) i;
        return sineTable[(size_t) i] + (sineTable[(size_t) i + 1] - sineTable[(size_t) i]) * t;
    }

    static constexpr int tableSize = 4096;
    static std::array<float, tableSize + 1> sineTable;

    struct Slot
    {
        float inc = 0.0f;                        // cycles per sample (constant over a block)
        float ampL = 0.0f, ampR = 0.0f, dAmpL = 0.0f, dAmpR = 0.0f;
        float targetL = 0.0f, targetR = 0.0f;
        float ratio = 1.0f, dRatio = 0.0f, targetRatio = 1.0f;   // f / f0 for phase modulation, glides per block
        float ampB = 0.0f, dAmpB = 0.0f, targetB = 0.0f;   // level in the Bass Mono voice
        float sph = 0.0f, dSph = 0.0f, targetSph = 0.0f;   // stereo phase: right minus left (cycles)
        float corr = 0.0f;                                  // Tune correction (cents at Tune 100 %), smoothed across blocks
        bool corrValid = false;
        bool primed = false;
    };

    double sr = 44100.0;
    Slot slots[ResynthData::maxSlots];

    float phases[ResynthData::maxSlots][maxUnison] {};
    float bassPhase[ResynthData::maxSlots] {};
    float uniOffset[maxUnison] {};   // each unison copy's phase offset from the note-on settings
    int active[ResynthData::maxSlots] {};
    int numActive = 0;
    int numUnison = 1;
    float uniMul[maxUnison] {}, uniL[maxUnison] {}, uniR[maxUnison] {};
    // unison and bass voice gains glide to their targets across each block
    float tgtL[maxUnison] {}, tgtR[maxUnison] {}, dUniL[maxUnison] {}, dUniR[maxUnison] {};
    float bassL = 0.0f, bassR = 0.0f, bassTgtL = 0.0f, bassTgtR = 0.0f, dBassL = 0.0f, dBassR = 0.0f;
    bool snapGains = true;
    float invN = 1.0f;
    int uniCount = 1;
    float uniNorm = 1.0f;
    float level = 0.0f, levelInc = 0.0f;
    float phaseOffset = 0.0f;

    // ---- residual (overlap-add noise) ----
    static constexpr int noiseOrder = 10, noiseSize = 1 << noiseOrder, noiseHop = noiseSize / 4;
    static constexpr int numBands = ResynthData::numNoiseBands;
    static std::array<float, noiseSize> noiseWindow;
    static std::array<float, noiseSize / 2> log2Bin;   // band position of bin k relative to bin 1 (bands per octave * log2 k)
    static std::array<float, 512> unitCos, unitSin;    // random unit vectors by table
    const FFTBank* noiseFft = nullptr;
    bool noiseOn = false;
    int noiseQuietSamples = 0;
    // allocated in prepare() (voices live on the stack in tests, and 24 KB per bank would overflow it)
    std::vector<float> noiseBufL, noiseBufR, noiseSpecL, noiseSpecR;
    int noiseRead = 0, noisePos = noiseHop;
    uint32_t noiseRng = 12345u;
    float bandL[numBands + 1] {}, bandR[numBands + 1] {}, bandCoh[numBands + 1] {};   // targets for the next frame (+1: zero guard)
    float bandOffset = 0.0f;      // band position of bin 1 at the played pitch
    float noiseBinScale = 0.0f;   // density -> bin amplitude
};
