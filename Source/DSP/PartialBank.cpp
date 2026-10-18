#include "PartialBank.h"

std::array<float, PartialBank::tableSize + 1> PartialBank::sineTable = []
{
    std::array<float, tableSize + 1> t {};
    for (int i = 0; i <= tableSize; ++i)
        t[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / tableSize);
    return t;
}();

std::array<float, PartialBank::noiseSize> PartialBank::noiseWindow = []
{
    std::array<float, noiseSize> w {};
    for (int i = 0; i < noiseSize; ++i) w[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / noiseSize);
    return w;
}();

std::array<float, PartialBank::noiseSize / 2> PartialBank::log2Bin = []
{
    std::array<float, noiseSize / 2> t {};
    for (int k = 1; k < noiseSize / 2; ++k) t[(size_t) k] = ResynthData::noiseBandsPerOctave * std::log2 ((float) k);
    return t;
}();

std::array<float, 512> PartialBank::unitCos = []
{
    std::array<float, 512> t {};
    for (int i = 0; i < 512; ++i) t[(size_t) i] = (float) std::cos (juce::MathConstants<double>::twoPi * i / 512.0);
    return t;
}();

std::array<float, 512> PartialBank::unitSin = []
{
    std::array<float, 512> t {};
    for (int i = 0; i < 512; ++i) t[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / 512.0);
    return t;
}();

namespace
{
// Four floats processed together (NEON / SSE with clang's vector extensions); plain C++ elsewhere.
#if defined(__clang__) && defined(__has_builtin)
 #if __has_builtin (__builtin_elementwise_min) && __has_builtin (__builtin_elementwise_max)
  #define ELUKKA_VEC4_NATIVE 1
 #endif
#endif
#if ELUKKA_VEC4_NATIVE
using Vec4 = float __attribute__ ((ext_vector_type (4)));
inline Vec4 vmin (Vec4 a, Vec4 b) noexcept { return __builtin_elementwise_min (a, b); }
inline Vec4 vmax (Vec4 a, Vec4 b) noexcept { return __builtin_elementwise_max (a, b); }
inline Vec4 load4 (const float* p) noexcept { Vec4 v; std::memcpy (&v, p, sizeof (v)); return v; }
inline void store4 (float* p, Vec4 v) noexcept { std::memcpy (p, &v, sizeof (v)); }
inline Vec4 vec4 (float a, float b, float c, float d) noexcept { return Vec4 { a, b, c, d }; }
#else
struct Vec4
{
    float v[4];
    Vec4 (float x = 0.0f) noexcept : v { x, x, x, x } {}
    Vec4 (float a, float b, float c, float d) noexcept : v { a, b, c, d } {}
    friend Vec4 operator+ (Vec4 a, Vec4 b) noexcept { for (int i = 0; i < 4; ++i) a.v[i] += b.v[i]; return a; }
    friend Vec4 operator- (Vec4 a, Vec4 b) noexcept { for (int i = 0; i < 4; ++i) a.v[i] -= b.v[i]; return a; }
    friend Vec4 operator* (Vec4 a, Vec4 b) noexcept { for (int i = 0; i < 4; ++i) a.v[i] *= b.v[i]; return a; }
    Vec4& operator+= (Vec4 b) noexcept { return *this = *this + b; }
};
inline Vec4 vmin (Vec4 a, Vec4 b) noexcept { for (int i = 0; i < 4; ++i) a.v[i] = std::min (a.v[i], b.v[i]); return a; }
inline Vec4 vmax (Vec4 a, Vec4 b) noexcept { for (int i = 0; i < 4; ++i) a.v[i] = std::max (a.v[i], b.v[i]); return a; }
inline Vec4 load4 (const float* p) noexcept { return { p[0], p[1], p[2], p[3] }; }
inline void store4 (float* p, Vec4 v) noexcept { std::memcpy (p, v.v, sizeof (v.v)); }
inline Vec4 vec4 (float a, float b, float c, float d) noexcept { return { a, b, c, d }; }
#endif

// sin (2 pi x) for |x| < 2^22: x minus its nearest integer (adding and removing 1.5 * 2^23 rounds it), folded into
// [-1/4, 1/4] cycle (sin (2 pi x) = sin (2 pi (+-1/2 - x))), then an odd polynomial. Max error 1.7e-7 (the
// 4096-point table tick() reads: 3.5e-7).
inline Vec4 sin2pi (Vec4 x) noexcept
{
    const Vec4 magic = 12582912.0f, half = 0.5f;
    x = x - ((x + magic) - magic);                    // [-1/2, 1/2]
    const Vec4 y = vmax (vmin (x, half - x), Vec4 (-0.5f) - x);
    const Vec4 y2 = y * y;
    return y * (Vec4 (6.283185005f) + y2 * (Vec4 (-41.34165573f) + y2 * (Vec4 (81.60100555f) + y2 * (Vec4 (-76.54977417f) + y2 * Vec4 (39.53665161f)))));
}

inline double wrap (double x) noexcept { return x - std::floor (x); }
}

void PartialBank::renderBlock (int n, float* outL, float* outR, float* mono, Scratch& sc) noexcept
{
    if (n <= 0) return;
    jassert (n <= maxBlock);
    const int nPad = (n + 7) & ~7;   // worked in groups of 8 samples

    // Unison gains over the block (shared by every partial). Each partial's sines advance inc per sample before
    // each read, as in tick(); a group's start phase is computed in double precision from the block's start, so
    // rounding never accumulates.
    for (int u = 0; u < numUnison; ++u)
        for (int k = 0; k < nPad; ++k)
        {
            sc.uniL[u][k] = uniL[u] + (float) k * dUniL[u];
            sc.uniR[u][k] = uniR[u] + (float) k * dUniR[u];
        }
    std::fill_n (sc.sumL, nPad, 0.0f);
    std::fill_n (sc.sumR, nPad, 0.0f);
    std::fill_n (sc.sumM, nPad, 0.0f);
    std::fill_n (sc.bass, nPad, 0.0f);
    bool anyBass = false;
    const float po = phaseOffset;
    const Vec4 j0 = vec4 (1.0f, 2.0f, 3.0f, 4.0f), j1 = vec4 (5.0f, 6.0f, 7.0f, 8.0f);   // samples 1..8 after a group's base
    const Vec4 k0 = vec4 (0.0f, 1.0f, 2.0f, 3.0f), k1 = vec4 (4.0f, 5.0f, 6.0f, 7.0f);   // sample index within a group

    for (int a = 0; a < numActive; ++a)
    {
        const int s = active[a];
        auto& sl = slots[s];
        const float r0 = sl.ratio, dr = sl.dRatio;
        const bool wide = ! (std::abs (sl.sph) < 0.002f && sl.dSph == 0.0f);   // as tick() decides (constant over the block)
        const float h0 = 0.5f * sl.sph, dh = 0.5f * sl.dSph;
        const float aL = sl.ampL, aR = sl.ampR, dL = sl.dAmpL, dR = sl.dAmpR;
        float* ph = phases[s];
        float incs[maxUnison];
        for (int u = 0; u < numUnison; ++u) incs[u] = sl.inc * uniMul[u];

        for (int c = 0; c < n; c += 8)
        {
            const Vec4 kc0 = k0 + (float) c, kc1 = k1 + (float) c;
            // Phase modulation offset po * ratio (the ratio glides across the block)
            Vec4 off0 = 0.0f, off1 = 0.0f;
            if (po != 0.0f) { off0 = Vec4 (po) * (Vec4 (r0) + kc0 * dr); off1 = Vec4 (po) * (Vec4 (r0) + kc1 * dr); }
            Vec4 hf0 = 0.0f, hf1 = 0.0f;
            if (wide) { hf0 = Vec4 (h0) + kc0 * dh; hf1 = Vec4 (h0) + kc1 * dh; }
            Vec4 accL0 = 0.0f, accL1 = 0.0f, accR0 = 0.0f, accR1 = 0.0f, accM0 = 0.0f, accM1 = 0.0f;
            for (int u = 0; u < numUnison; ++u)
            {
                const float inc = incs[u];
                const float base = (float) wrap ((double) ph[u] + (double) c * (double) inc);
                const Vec4 x0 = Vec4 (base) + j0 * inc + off0, x1 = Vec4 (base) + j1 * inc + off1;
                const Vec4 gL0 = load4 (sc.uniL[u] + c), gL1 = load4 (sc.uniL[u] + c + 4);
                const Vec4 gR0 = load4 (sc.uniR[u] + c), gR1 = load4 (sc.uniR[u] + c + 4);
                if (! wide)
                {
                    const Vec4 v0 = sin2pi (x0), v1 = sin2pi (x1);
                    accL0 += v0 * gL0; accL1 += v1 * gL1;
                    accR0 += v0 * gR0; accR1 += v1 * gR1;
                    accM0 += v0;       accM1 += v1;
                }
                else
                {
                    // stereo-wide partial: the two sides half a stereo phase either way of the partial's phase
                    const Vec4 vL0 = sin2pi (x0 - hf0), vL1 = sin2pi (x1 - hf1);
                    const Vec4 vR0 = sin2pi (x0 + hf0), vR1 = sin2pi (x1 + hf1);
                    accL0 += vL0 * gL0; accL1 += vL1 * gL1;
                    accR0 += vR0 * gR0; accR1 += vR1 * gR1;
                    accM0 += Vec4 (0.5f) * (vL0 + vR0); accM1 += Vec4 (0.5f) * (vL1 + vR1);
                }
            }
            const Vec4 gl0 = Vec4 (aL) + kc0 * dL, gl1 = Vec4 (aL) + kc1 * dL;
            const Vec4 gr0 = Vec4 (aR) + kc0 * dR, gr1 = Vec4 (aR) + kc1 * dR;
            store4 (sc.sumL + c, load4 (sc.sumL + c) + accL0 * gl0);
            store4 (sc.sumL + c + 4, load4 (sc.sumL + c + 4) + accL1 * gl1);
            store4 (sc.sumR + c, load4 (sc.sumR + c) + accR0 * gr0);
            store4 (sc.sumR + c + 4, load4 (sc.sumR + c + 4) + accR1 * gr1);
            store4 (sc.sumM + c, load4 (sc.sumM + c) + accM0 * (gl0 + gr0));
            store4 (sc.sumM + c + 4, load4 (sc.sumM + c + 4) + accM1 * (gl1 + gr1));
        }
        for (int u = 0; u < numUnison; ++u)
            ph[u] = (float) wrap ((double) ph[u] + (double) n * (double) incs[u]);

        // Bass Mono voice: always advanced so it stays in step with the centre unison voice
        const float aB = sl.ampB, dB = sl.dAmpB;
        if (aB > 0.0f || dB > 0.0f)
        {
            anyBass = true;
            const float inc = sl.inc;
            for (int c = 0; c < n; c += 8)
            {
                const Vec4 kc0 = k0 + (float) c, kc1 = k1 + (float) c;
                const float base = (float) wrap ((double) bassPhase[s] + (double) c * (double) inc);
                Vec4 x0 = Vec4 (base) + j0 * inc, x1 = Vec4 (base) + j1 * inc;
                if (po != 0.0f) { x0 += Vec4 (po) * (Vec4 (r0) + kc0 * dr); x1 += Vec4 (po) * (Vec4 (r0) + kc1 * dr); }
                store4 (sc.bass + c, load4 (sc.bass + c) + sin2pi (x0) * (Vec4 (aB) + kc0 * dB));
                store4 (sc.bass + c + 4, load4 (sc.bass + c + 4) + sin2pi (x1) * (Vec4 (aB) + kc1 * dB));
            }
        }
        bassPhase[s] = (float) wrap ((double) bassPhase[s] + (double) n * (double) sl.inc);

        // end of block: where tick() would have left the ramps
        const float fn = (float) n;
        sl.ampL += fn * sl.dAmpL;
        sl.ampR += fn * sl.dAmpR;
        sl.ampB += fn * sl.dAmpB;
        sl.ratio += fn * sl.dRatio;
        sl.sph += fn * sl.dSph;
    }

    const float monoScale = 0.5f * uniNorm;
    for (int k = 0; k < n; ++k)
    {
        const float lev = level + (float) k * levelInc;
        float l = sc.sumL[k], r = sc.sumR[k];
        if (anyBass)
        {
            l += sc.bass[k] * (bassL + (float) k * dBassL);
            r += sc.bass[k] * (bassR + (float) k * dBassR);
        }
        if (noiseOn)
        {
            if (noisePos == noiseHop) { generateNoiseFrame (0); noisePos = 0; }
            l += noiseBufL[(size_t) noiseRead];
            r += noiseBufR[(size_t) noiseRead];
            noiseBufL[(size_t) noiseRead] = noiseBufR[(size_t) noiseRead] = 0.0f;
            noiseRead = (noiseRead + 1) & (noiseSize - 1);
            ++noisePos;
        }
        outL[k] += l * lev;
        outR[k] += r * lev;
        mono[k] += sc.sumM[k] * monoScale + sc.bass[k];
    }

    const float fn = (float) n;
    for (int u = 0; u < numUnison; ++u)
    {
        uniL[u] += fn * dUniL[u];
        uniR[u] += fn * dUniR[u];
    }
    bassL += fn * dBassL;
    bassR += fn * dBassR;
    level += fn * levelInc;
}

void PartialBank::prepare (double sampleRate)
{
    sr = sampleRate;
    noiseBufL.assign ((size_t) noiseSize, 0.0f);
    noiseBufR.assign ((size_t) noiseSize, 0.0f);
    noiseSpecL.assign ((size_t) noiseSize * 2, 0.0f);
    noiseSpecR.assign ((size_t) noiseSize * 2, 0.0f);
}

void PartialBank::restart()
{
    for (auto& s : slots)
    {
        s.ampL = s.ampR = s.targetL = s.targetR = s.dAmpL = s.dAmpR = 0.0f;
        s.ampB = s.targetB = s.dAmpB = 0.0f;
        s.primed = false;
        s.corrValid = false;
    }
    numActive = 0;
    noiseOn = false;
    std::fill (noiseBufL.begin(), noiseBufL.end(), 0.0f);
    std::fill (noiseBufR.begin(), noiseBufR.end(), 0.0f);
}

void PartialBank::resetPhases (int voices, int phaseMode, float startPhase, float amount, juce::Random& rng)
{
    const int n = juce::jlimit (1, maxUnison, voices);
    for (int s = 0; s < ResynthData::maxSlots; ++s)
    {
        // Partials must not all start in phase (that would make an impulse); Reset uses a fixed
        // golden-ratio sequence so every trigger is identical, other modes a new random base.
        const float base = phaseMode == Unison::PhaseReset ? std::fmod ((float) s * 0.6180339887f, 1.0f) : rng.nextFloat();
        if (s == 0)
            for (int u = 0; u < maxUnison; ++u) uniOffset[u] = Unison::initialPhase (u, n, phaseMode, startPhase, amount, rng);
        for (int u = 0; u < maxUnison; ++u)
        {
            const float x = base + Unison::initialPhase (u, n, phaseMode, startPhase, amount, rng);
            phases[s][u] = x - std::floor (x);
        }
        bassPhase[s] = phases[s][n / 2];   // bass voice starts in step with the centre voice
    }
    for (auto& s : slots)
    {
        s.ampL = s.ampR = s.targetL = s.targetR = 0.0f;
        s.ampB = s.targetB = 0.0f;
        s.primed = false;
        s.corrValid = false;
    }
    numActive = 0;
    snapGains = true;

    noiseOn = false;
    noiseRng = (uint32_t) rng.nextInt() | 1u;
    std::fill (noiseBufL.begin(), noiseBufL.end(), 0.0f);
    std::fill (noiseBufR.begin(), noiseBufR.end(), 0.0f);
}

float PartialBank::beginBlock (const BlockParams& p, int n, float levelStart, float levelEnd)
{
    invN = 1.0f / (float) juce::jmax (1, n);

    // Unison copies of the whole bank, detuned and spread like the other oscillators.
    uniCount = juce::jlimit (1, maxUnison, p.unison);
    // previous block ended on its targets
    std::copy (std::begin (tgtL), std::end (tgtL), std::begin (uniL));
    std::copy (std::begin (tgtR), std::end (tgtR), std::begin (uniR));
    bassL = bassTgtL;
    bassR = bassTgtR;
    std::fill (std::begin (tgtL), std::end (tgtL), 0.0f);
    std::fill (std::begin (tgtR), std::end (tgtR), 0.0f);
    float newMul[maxUnison];
    Unison::layout (uniCount, p.detuneCents, p.detuneMode, p.blend, p.width, newMul, tgtL, tgtR);
    float panL, panR;
    Unison::panGains (p.pan, panL, panR);
    float maxMul = 1.0f;
    for (int u = 0; u < uniCount; ++u)
    {
        uniMul[u] = newMul[u];
        maxMul = juce::jmax (maxMul, uniMul[u]);
        // Partials carry their own stereo position, so a single centred voice must have unity gain per side.
        tgtL[u] *= juce::MathConstants<float>::sqrt2 * panL;
        tgtR[u] *= juce::MathConstants<float>::sqrt2 * panR;
    }
    // the bass voice is a single centred voice: unity gain per side, not moved by pan
    bassTgtL = bassTgtR = 1.0f;
    if (snapGains)
    {
        std::copy (std::begin (tgtL), std::end (tgtL), std::begin (uniL));
        std::copy (std::begin (tgtR), std::end (tgtR), std::begin (uniR));
        bassL = bassTgtL;
        bassR = bassTgtR;
        snapGains = false;
    }
    numUnison = uniCount;
    for (int u = maxUnison - 1; u >= uniCount; --u)
        if (uniL[u] != 0.0f || uniR[u] != 0.0f) { numUnison = u + 1; break; }
    for (int u = 0; u < maxUnison; ++u)
    {
        dUniL[u] = (tgtL[u] - uniL[u]) * invN;
        dUniR[u] = (tgtR[u] - uniR[u]) * invN;
    }
    dBassL = (bassTgtL - bassL) * invN;
    dBassR = (bassTgtR - bassR) * invN;
    uniNorm = 1.0f / (float) uniCount;
    phaseOffset = p.phaseOffset;

    level = levelStart;
    levelInc = (levelEnd - levelStart) * invN;
    numActive = 0;
    return (float) (sr * 0.45) / maxMul;
}

float PartialBank::mangle (float h, const BlockParams& p, int index)
{
    float d = 0.0f;
    if (p.prism > 0.0f) d += Mangle::prism (h, p.prism, p.prismCenter, p.prismWidth);
    if (p.shimmer > 0.0f && p.drift != nullptr) d += Mangle::shimmer (h, p.shimmer, p.drift[index % Mangle::Drift::size]);
    return d;
}

void PartialBank::setSlot (int s, float f, float h, float a, float pan, const BlockParams& p, float nyquist, float startPhase,
                           float stereoPhase)
{
    auto& sl = slots[s];
    // Start this block exactly where the previous one ended.
    sl.ampL = sl.targetL;
    sl.ampR = sl.targetR;
    sl.ratio = sl.targetRatio;
    sl.ampB = sl.targetB;
    sl.sph = sl.targetSph;

    float newL = 0.0f, newR = 0.0f, newB = 0.0f, newInc = sl.inc, newRatio = sl.ratio;
    if (f < nyquist && f > 1.0f && a > 0.0f)
    {
        // Bass Mono: the share below the crossover goes to the centred bass voice (no source pan)
        float toBass = 0.0f, toUnison = 1.0f;
        if (p.bassMono) Unison::bassSplit (f, p.bassFreq, uniCount == 1, toBass, toUnison);
        newL = a * toUnison * (1.0f - pan);
        newR = a * toUnison * (1.0f + pan);
        newB = a * toBass;
        newInc = f / (float) sr;
        newRatio = h;
        // Anti-aliasing: FM depth grows with the partial's ratio, so limit it where the sidebands
        // would pass Nyquist (otherwise high partials fold back as crackle).
        if (p.fmDevPerRatio > 0.0f)
            newRatio = juce::jlimit (0.0f, h, (nyquist - f - p.fmModHz) / p.fmDevPerRatio);
    }

    // A partial that is (re)starting from silence takes its measured phase and stereo phase.
    if (! sl.primed || (sl.ampL <= 0.0f && sl.ampR <= 0.0f && sl.ampB <= 0.0f))
    {
        sl.ratio = newRatio;
        sl.sph = stereoPhase;
        if (startPhase >= 0.0f)
        {
            // tick() advances before reading, so start one step behind the phase the block begins at
            for (int u = 0; u < maxUnison; ++u)
            {
                const float x = startPhase + uniOffset[u] - newInc * uniMul[u];
                phases[s][u] = x - std::floor (x);
            }
            const float xb = startPhase - newInc;
            bassPhase[s] = xb - std::floor (xb);
        }
    }
    sl.primed = true;

    // the increment is the block's mean frequency: integrating it lands exactly on the source's phase
    sl.inc = newInc;
    sl.targetL = newL;
    sl.targetR = newR;
    sl.dAmpL = (newL - sl.ampL) * invN;
    sl.dAmpR = (newR - sl.ampR) * invN;
    sl.targetB = newB;
    sl.dAmpB = (newB - sl.ampB) * invN;
    sl.targetRatio = newRatio;
    sl.dRatio = (newRatio - sl.ratio) * invN;
    // stereo phase glides the short way round
    float dsp = stereoPhase - sl.sph;
    dsp -= std::round (dsp);
    sl.targetSph = sl.sph + dsp;
    sl.dSph = dsp * invN;
    if (std::abs (sl.targetSph) > 0.5f) sl.targetSph -= std::round (sl.targetSph);

    if (newL > 0.0f || newR > 0.0f || newB > 0.0f || sl.ampL > 0.0f || sl.ampR > 0.0f || sl.ampB > 0.0f)
        active[numActive++] = s;
}

void PartialBank::setBlock (const ResynthData& d, const BlockParams& p, const GainFn& gain, int n, float levelStart, float levelEnd)
{
    const float nyquist = beginBlock (p, n, levelStart, levelEnd);

    const int S = juce::jmin (d.numSlots, ResynthData::maxSlots);
    const double maxTime = (double) juce::jmax (0, d.numFrames - 1) / d.frameRate;
    const double u0 = juce::jlimit (0.0, maxTime, p.timeSec);
    const double u1 = juce::jlimit (0.0, maxTime, p.timeSec + p.blockSec);
    // Levels are aimed at the end of the block (each block ramps to them), so changes land on time.
    const double fpos = u1 * d.frameRate;
    const int fa = juce::jlimit (0, juce::jmax (0, d.numFrames - 1), (int) fpos);
    const int fb = juce::jmin (fa + 1, d.numFrames - 1);
    const float ft = (float) (fpos - fa);
    const size_t rowA = (size_t) fa * (size_t) d.numSlots, rowB = (size_t) fb * (size_t) d.numSlots;

    // The key: at Tune 0 it transposes the source (middle C = original pitch); at Tune 1 every note of the source moves
    // to the nearest note of the key's pitch class in its own register, shifted by the key's octave from middle C.
    const float keyMidi = 69.0f + 12.0f * std::log2 (p.f0 / 440.0f);
    const float transposeCents = (1.0f - p.tune) * (keyMidi - 60.0f) * 100.0f;
    const bool tuning = p.tune > 0.0f && ! d.noteMidi.empty();
    const float corrK = 1.0f - std::exp (-(float) n / (0.025f * (float) sr));   // correction glide, 25 ms time constant
    const float invMax = 1.0f / d.maxAmp;

    // Gate: thresholds (amplitude) relative to the loudest partial at this moment; between them a smooth 6 dB knee
    float gateLo = 0.0f, gateHi = 0.0f;
    if (p.gateDb > -80.0f)
    {
        float loudest = 0.0f;
        for (int s = 0; s < S; ++s)
        {
            const float aA = d.amp[rowA + (size_t) s], aB = d.amp[rowB + (size_t) s];
            loudest = std::max (loudest, aA + (aB - aA) * ft);
        }
        gateHi = loudest * juce::Decibels::decibelsToGain (p.gateDb);
        gateLo = loudest * juce::Decibels::decibelsToGain (p.gateDb - 6.0f);
    }

    for (int s = 0; s < S; ++s)
    {
        const float aA = d.amp[rowA + (size_t) s], aB = d.amp[rowB + (size_t) s];
        float a = aA + (aB - aA) * ft;
        float f = 0.0f, h = 0.0f, pan = 0.0f, startPhase = -1.0f, stereoPh = 0.0f;

        if (a > 0.0f)
        {
            const float fSrc = d.meanFreq (s, u0, u1);
            pan = juce::jlimit (-1.0f, 1.0f, d.pan[rowA + (size_t) s] + (d.pan[rowB + (size_t) s] - d.pan[rowA + (size_t) s]) * ft);
            if (! d.stereoPhase.empty()) stereoPh = d.stereoPhase[(aA > 0.0f ? rowA : rowB) + (size_t) s];
            startPhase = d.phaseAt (s, u0);

            auto& sl = slots[s];
            float corr = 0.0f;
            if (tuning)
            {
                const float m = d.noteMidi[(aA > 0.0f ? rowA : rowB) + (size_t) s];
                if (m > 0.0f) corr = 100.0f * (ResynthData::tuneTarget (m, keyMidi) - m);
            }
            // a (re)starting partial, or one whose own pitch just jumped by over a semitone, takes the correction at once
            if (! sl.corrValid || std::abs (fSrc - sl.srcFreq) > 0.0595f * sl.srcFreq) { sl.corr = corr; sl.corrValid = true; }
            else sl.corr += (corr - sl.corr) * corrK;
            sl.srcFreq = fSrc;
            f = fSrc * std::exp2 ((transposeCents + p.tune * sl.corr) / 1200.0f);
            if (p.stretch != 0.0f)
                f *= std::pow (juce::jmax (0.01f, f / p.f0), p.stretch * 0.25f);
            h = f / p.f0;
            if (p.prism > 0.0f || p.shimmer > 0.0f)
            {
                f += p.f0 * mangle (h, p, s);
                h = f / p.f0;
            }

            if (a < gateHi)
            {
                const float x = a <= gateLo ? 0.0f : std::log (a / gateLo) / std::log (gateHi / gateLo);
                a *= x * x * (3.0f - 2.0f * x);
            }

            // Bass region (below the crossover): its own Lift
            const bool bass = f < p.bassFreq;
            const float lift = bass ? p.subLift : p.lift;
            if (lift > 0.0f)
                a = d.maxAmp * std::pow (a * invMax, 1.0f - 0.8f * lift);
            a *= gain (f, h);
        }
        else
        {
            slots[s].corrValid = false;   // a partial that restarts takes its note's correction at once
        }
        setSlot (s, f, h, a, pan, p, nyquist, startPhase, stereoPh);
    }

    float panL, panR;
    Unison::panGains (p.pan, panL, panR);
    setNoiseBlock (d, p, gain, u1, transposeCents, keyMidi, tuning, panL, panR, n);
}

void PartialBank::setNoiseBlock (const ResynthData& d, const BlockParams& p, const GainFn& gain, double timeSec, float transposeCents, float keyMidi, bool tuning,
                                 float panL, float panR, int n)
{
    if (! d.hasNoise() || p.fft == nullptr || noiseBufL.empty())
    {
        noiseOn = false;
        return;
    }
    noiseFft = p.fft;
    const double fpos = juce::jlimit (0.0, (double) juce::jmax (0, d.numFrames - 1), timeSec * d.frameRate);
    const int fa = (int) fpos;
    const int fb = juce::jmin (fa + 1, d.numFrames - 1);
    const float ft = (float) (fpos - fa);
    const size_t rowA = (size_t) fa * numBands, rowB = (size_t) fb * numBands;

    const float noteRatio = std::exp2 (transposeCents / 1200.0f);
    const float binHz = (float) (sr / noiseSize);
    // the frame's bins are shaped by the source bands at bin frequency / note ratio
    bandOffset = ResynthData::noiseBandPos (binHz / noteRatio);
    // density (amplitude per sqrt Hz) -> bin amplitude: variance per bin = P * binHz; Hann frames at 75 % overlap
    // sum to 1.5 x the frame power, and a frame sinusoid of amplitude A has power A^2 / 2
    noiseBinScale = std::sqrt (binHz / 0.75f) * noiseFft->sineScale (noiseOrder);

    // the source's bands at this time
    float srcL[numBands], srcR[numBands], srcC[numBands];
    for (int b = 0; b < numBands; ++b)
    {
        srcL[b] = d.noiseL[rowA + (size_t) b] + (d.noiseL[rowB + (size_t) b] - d.noiseL[rowA + (size_t) b]) * ft;
        srcR[b] = d.noiseR[rowA + (size_t) b] + (d.noiseR[rowB + (size_t) b] - d.noiseR[rowA + (size_t) b]) * ft;
        srcC[b] = d.noiseCoh[rowA + (size_t) b] + (d.noiseCoh[rowB + (size_t) b] - d.noiseCoh[rowA + (size_t) b]) * ft;
    }
    // Tune: each band moves with the correction of its nearest note (power split between the two bands it lands on)
    const float* notes = d.frameNotes.empty() ? nullptr : &d.frameNotes[(size_t) fa * (size_t) ResynthData::maxNotesPerFrame];
    if (tuning && notes != nullptr && notes[0] > 0.0f)
    {
        float pL[numBands + 1] {}, pR[numBands + 1] {}, pC[numBands + 1] {};
        for (int b = 0; b < numBands; ++b)
        {
            const float midiB = 69.0f + 12.0f * std::log2 (ResynthData::noiseBandCentre (b) / 440.0f);
            float best = 1.0e9f, m = 0.0f;
            for (int i = 0; i < ResynthData::maxNotesPerFrame && notes[i] > 0.0f; ++i)
                if (std::abs (notes[i] - midiB) < best) { best = std::abs (notes[i] - midiB); m = notes[i]; }
            const float corr = p.tune * 100.0f * (ResynthData::tuneTarget (m, keyMidi) - m);   // cents
            const float pos = (float) b + corr / (1200.0f / ResynthData::noiseBandsPerOctave);
            const int i0 = (int) std::floor (pos);
            const float t = pos - (float) i0;
            const float eL = srcL[b] * srcL[b], eR = srcR[b] * srcR[b];
            if (i0 >= 0 && i0 < numBands)         { pL[i0] += eL * (1.0f - t); pR[i0] += eR * (1.0f - t); pC[i0] += srcC[b] * (eL + eR) * (1.0f - t); }
            if (i0 + 1 >= 0 && i0 + 1 < numBands) { pL[i0 + 1] += eL * t;      pR[i0 + 1] += eR * t;      pC[i0 + 1] += srcC[b] * (eL + eR) * t; }
        }
        for (int b = 0; b < numBands; ++b)
        {
            srcL[b] = std::sqrt (pL[b]);
            srcR[b] = std::sqrt (pR[b]);
            srcC[b] = pL[b] + pR[b] > 0.0f ? pC[b] / (pL[b] + pR[b]) : 1.0f;
        }
    }

    float energy = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float nL = srcL[b], nR = srcR[b], coh = srcC[b];
        const float fPlayed = ResynthData::noiseBandCentre (b) * noteRatio;
        if (p.bassMono && fPlayed < p.bassFreq)
        {
            // the bass region is one centred signal
            nL = nR = std::sqrt (0.5f * (nL * nL + nR * nR));
            coh = 1.0f;
        }
        const float g = fPlayed < (float) sr * 0.45f ? gain (fPlayed, fPlayed / p.f0) : 0.0f;
        bandL[b] = nL * g * panL;
        bandR[b] = nR * g * panR;
        bandCoh[b] = juce::jlimit (0.0f, 1.0f, coh);
        energy += bandL[b] + bandR[b];
    }
    bandL[numBands] = bandR[numBands] = 0.0f;
    bandCoh[numBands] = bandCoh[numBands - 1];

    if (energy > 0.0f)
    {
        noiseQuietSamples = 0;
        if (! noiseOn)
        {
            // start with the overlapping frames already in place, so the residual is at full level from the
            // first sample (an attack must not fade in)
            noiseOn = true;
            noiseRead = 0;
            noisePos = 0;
            std::fill (noiseBufL.begin(), noiseBufL.end(), 0.0f);
            std::fill (noiseBufR.begin(), noiseBufR.end(), 0.0f);
            for (int k = 3; k >= 0; --k) generateNoiseFrame (-k * noiseHop);
        }
    }
    else if (noiseOn)
    {
        noiseQuietSamples += n;
        if (noiseQuietSamples > noiseSize) noiseOn = false;   // silent for a whole frame: the overlap-add buffer has drained
    }
}

void PartialBank::generateNoiseFrame (int offset)
{
    constexpr int N = noiseSize;
    std::fill (noiseSpecL.begin(), noiseSpecL.end(), 0.0f);
    std::fill (noiseSpecR.begin(), noiseSpecR.end(), 0.0f);
    uint32_t rng = noiseRng;
    for (int k = 1; k < N / 2; ++k)
    {
        const float pos = log2Bin[(size_t) k] + bandOffset;
        if (pos >= (float) numBands) break;
        const int b = pos > 0.0f ? (int) pos : 0;
        const float t = pos > 0.0f ? pos - (float) b : 0.0f;
        const float aL = bandL[b] + (bandL[b + 1] - bandL[b]) * t;
        const float aR = bandR[b] + (bandR[b + 1] - bandR[b]) * t;
        if (aL <= 0.0f && aR <= 0.0f) continue;
        const float c = bandCoh[b] + (bandCoh[b + 1] - bandCoh[b]) * t;
        const float s = std::sqrt (juce::jmax (0.0f, 1.0f - c * c));
        rng = rng * 1664525u + 1013904223u;
        const int i1 = (int) (rng >> 23);
        rng = rng * 1664525u + 1013904223u;
        const int i2 = (int) (rng >> 23);
        const float c1 = unitCos[(size_t) i1], s1 = unitSin[(size_t) i1], c2 = unitCos[(size_t) i2], s2 = unitSin[(size_t) i2];
        noiseSpecL[(size_t) (2 * k)] = aL * c1;
        noiseSpecL[(size_t) (2 * k + 1)] = aL * s1;
        // the right side shares the left's noise by the measured coherence, the rest is independent
        noiseSpecR[(size_t) (2 * k)] = aR * (c * c1 + s * c2);
        noiseSpecR[(size_t) (2 * k + 1)] = aR * (c * s1 + s * s2);
    }
    noiseRng = rng;
    noiseFft->inverse (noiseOrder, noiseSpecL.data());
    noiseFft->inverse (noiseOrder, noiseSpecR.data());
    const float g = noiseBinScale;
    for (int i = juce::jmax (0, -offset); i < N; ++i)
    {
        const size_t idx = (size_t) ((noiseRead + offset + i) & (N - 1));
        const float w = noiseWindow[(size_t) i] * g;
        noiseBufL[idx] += noiseSpecL[(size_t) i] * w;
        noiseBufR[idx] += noiseSpecR[(size_t) i] * w;
    }
}

void PartialBank::setHarmonics (const float* amps, int numHarm, uint64_t which, const BlockParams& p, int n, float levelStart, float levelEnd)
{
    const float nyquist = beginBlock (p, n, levelStart, levelEnd);
    for (int s = 0; s < numHarmonicSlots; ++s)
    {
        const float hn = (float) (s + 2);
        float f = 0.0f, h = 0.0f, a = 0.0f;
        if (s + 2 <= numHarm && (which >> s) & 1u)
        {
            h = p.stretch != 0.0f ? hn * std::pow (hn, p.stretch * 0.25f) : hn;
            h += mangle (h, p, s);
            f = p.f0 * h;
            // the wavetable plays a harmonic of amplitude a at a/sqrt2 per side (centred voice); match it
            a = amps[s + 1] * juce::MathConstants<float>::sqrt2 * 0.5f;
        }
        setSlot (s, f, h, a, 0.0f, p, nyquist);
    }
    noiseOn = false;
}

void PartialBank::alignHarmonics (const float* voicePhases, const float* voiceIncs, float bassVoicePhase, float bassVoiceInc, float stretch, uint64_t which)
{
    // Wavetable harmonic h of a voice at phase x (cycles of the fundamental) is at phase m*x, where m is the
    // harmonic's (stretched, 1/8-quantised) position in the table. The wavetable reads before advancing and
    // tick() advances before reading, so start one sample's step behind.
    for (int s = 0; s < numHarmonicSlots; ++s)
    {
        if (! ((which >> s) & 1u)) continue;
        const float hn = (float) (s + 2);
        const float m = std::abs (stretch) > 1.0e-4f ? std::round (hn * std::pow (hn, stretch * 0.25f) * (float) stretchCycles) / (float) stretchCycles : hn;
        for (int u = 0; u < maxUnison; ++u)
        {
            const double x = (double) m * ((double) voicePhases[u] - (double) voiceIncs[u]);
            phases[s][u] = (float) (x - std::floor (x));
        }
        const double xb = (double) m * ((double) bassVoicePhase - (double) bassVoiceInc);
        bassPhase[s] = (float) (xb - std::floor (xb));
    }
}
