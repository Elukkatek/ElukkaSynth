#pragma once
#include <JuceHeader.h>

// Warm, dense stereo reverb: input diffusion into a small feedback delay network (FDN).
//
//   in -> low cut, bandwidth low pass -> 3 allpass diffusers per channel -> FDN (N lines, Hadamard feedback;
//   each line: modulated delay, short allpass, damping low pass, very low cut, decay gain) -> orthogonal L/R taps
//   -> mid/side width -> dry/wet crossfade
//
// One engine; the three modes only change internal values (diffusion, damping range, modulation, width), all of
// which glide, so switching modes never clicks. Everything is allocated in prepare().
template <int N>
class FdnReverb
{
public:
    static_assert (N == 4 || N == 8, "Hadamard feedback needs 4 or 8 lines");
    enum Mode { Smooth, Vintage, Lush };

    struct Params
    {
        float mix = 0.22f;     // 0..1 dry/wet
        float decay = 3.0f;    // seconds (T60)
        float size = 0.65f;    // 0..1
        float tone = 0.5f;     // 0 = dark .. 1 = bright
        float mod = 0.25f;     // 0..1
        int mode = Smooth;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rateScale = (float) (sr / 44100.0);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < numDiffusers; ++k)
                diffuser[c][k].prepare ((int) std::lround (diffuserMs[c][k] * 0.001 * sr));
        for (int i = 0; i < N; ++i)
        {
            const int maxLen = (int) (lineMs[i] * 0.001 * sr * maxSizeScale) + (int) (maxModDepth * rateScale) + 8;
            lines[i].assign ((size_t) juce::nextPowerOfTwo (maxLen + 1), 0.0f);
            loopAp[i].prepare ((int) std::lround (loopApMs[i] * 0.001 * sr));
            loopAp2[i].prepare ((int) std::lround (loopAp2Ms[i] * 0.001 * sr));
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& c : diffuser) for (auto& d : c) d.clear();
        for (auto& a : loopAp) a.clear();
        for (auto& a : loopAp2) a.clear();
        writePos = 0;
        std::fill (std::begin (damp), std::end (damp), 0.0f);
        std::fill (std::begin (lowCut), std::end (lowCut), 0.0f);
        inLowCut[0] = inLowCut[1] = inBand[0] = inBand[1] = 0.0f;
        lfo[0] = { 1.0f, 0.0f };
        lfo[1] = { 0.0f, 1.0f };
        primed = false;
    }

    // Adds the reverb to L/R in place (dry/wet crossfade by p.mix).
    void process (float* L, float* R, int n, const Params& p)
    {
        updateTargets (p, n);
        const float invN = 1.0f / (float) juce::jmax (1, n);
        float len[N], dLen[N];
        for (int i = 0; i < N; ++i)
        {
            len[i] = curLen[i];
            dLen[i] = (tgtLen[i] - curLen[i]) * invN;
            curLen[i] = tgtLen[i];
        }
        float mix = curMix;
        const float dMix = (tgtMix - curMix) * invN;
        curMix = tgtMix;

        const float depth = modDepth * 0.5f;
        for (int s = 0; s < n; ++s)
        {
            // input: low cut, bandwidth, diffusion
            float in[2] { L[s], R[s] };
            for (int c = 0; c < 2; ++c)
            {
                inLowCut[c] += inLowCutA * (in[c] - inLowCut[c]);
                float x = in[c] - inLowCut[c];
                inBand[c] += inBandA * (x - inBand[c]);
                x = inBand[c];
                for (int k = 0; k < numDiffusers; ++k) x = diffuser[c][k].process (x, inDiffGain);
                in[c] = x;
            }

            // modulation: two quadrature oscillators give four different phases
            for (int j = 0; j < 2; ++j)
            {
                lfo[j].s += lfoW[j] * lfo[j].c;
                lfo[j].c -= lfoW[j] * lfo[j].s;
            }
            const float m[4] { lfo[0].s, lfo[0].c, lfo[1].s, -lfo[1].c };

            // read the lines (fractional delay, linear interpolation), then allpass, damping, low cut, decay
            float y[N];
            for (int i = 0; i < N; ++i)
            {
                const float d = len[i] + depth * (1.0f + m[i & 3]);
                len[i] += dLen[i];
                const int bufLen = (int) lines[(size_t) i].size();
                float rp = (float) (writePos & (bufLen - 1)) - d;
                if (rp < 0.0f) rp += (float) bufLen;
                const int i0 = (int) rp;
                const float fr = rp - (float) i0;
                const auto& buf = lines[(size_t) i];
                const float a0 = buf[(size_t) (i0 & (bufLen - 1))], a1 = buf[(size_t) ((i0 + 1) & (bufLen - 1))];
                // reading i0+1 is one sample *less* delay; interpolate between the two neighbours
                float v = a0 + (a1 - a0) * fr;
                v = loopAp[i].process (v, loopApGain);
                v = loopAp2[i].process (v, -loopApGain);   // two short allpasses per line build density quickly
                damp[i] += dampA * (v - damp[i]);
                v = damp[i];
                lowCut[i] += lowCutA * (v - lowCut[i]);
                y[i] = (v - lowCut[i]) * gain[i];
            }

            // orthogonal (Hadamard) feedback plus input, written back into the lines
            float fb[N];
            hadamard (y, fb);
            for (int i = 0; i < N; ++i)
            {
                const float inj = (i & 1) ? in[1] : in[0];
                auto& buf = lines[(size_t) i];
                buf[(size_t) (writePos & ((int) buf.size() - 1))] = fb[i] + inj * ((i & 2) ? -injGain : injGain);
            }
            ++writePos;

            // stereo out: two orthogonal combinations of the line outputs, then width
            float wl = 0.0f, wr = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                wl += y[i] * outL[i];
                wr += y[i] * outR[i];
            }
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = (mid + side) * wetGain;
            wr = (mid - side) * wetGain;

            L[s] = L[s] * (1.0f - mix) + wl * mix;
            R[s] = R[s] * (1.0f - mix) + wr * mix;
            mix += dMix;
        }
        writePos &= 0x3fffffff;
        // keep the oscillators on the unit circle
        for (auto& o : lfo)
        {
            const float r = 1.5f - 0.5f * (o.s * o.s + o.c * o.c);
            o.s *= r; o.c *= r;
        }
    }

    // T60 actually applied, for tests
    float getDecay() const { return curDecay; }

private:
    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0;
        void prepare (int len) { buf.assign ((size_t) juce::jmax (1, len), 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline float process (float x, float g) noexcept
        {
            const float d = buf[(size_t) pos];
            const float w = x + g * d;
            buf[(size_t) pos] = w;
            if (++pos >= (int) buf.size()) pos = 0;
            return d - g * w;
        }
    };

    static void hadamard (const float* x, float* out) noexcept
    {
        // fast Walsh-Hadamard transform, scaled to be orthonormal
        float t[N];
        for (int i = 0; i < N; ++i) t[i] = x[i];
        for (int h = 1; h < N; h *= 2)
            for (int i = 0; i < N; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = t[j], b = t[j + h];
                    t[j] = a + b;
                    t[j + h] = a - b;
                }
        const float scale = 1.0f / std::sqrt ((float) N);
        for (int i = 0; i < N; ++i) out[i] = t[i] * scale;
    }

    // Per-mode internal values; the actual values glide towards these.
    struct ModeValues { float inDiff, loopAp, toneLo, toneHi, band, modDepth, rateA, rateB, width, lowCutHz, sizeScale; };
    static ModeValues modeValues (int mode)
    {
        switch (mode)
        {
            case Vintage: return { 0.62f, 0.45f, 600.0f,  7500.0f, 8000.0f,  12.6f, 0.42f, 0.57f, 0.80f, 140.0f, 0.95f };
            case Lush:    return { 0.72f, 0.60f, 1000.0f, 15000.0f, 17000.0f, 30.8f, 0.33f, 0.51f, 1.00f, 90.0f, 1.10f };
            default:      return { 0.66f, 0.52f, 900.0f, 14000.0f, 15000.0f, 15.4f, 0.55f, 0.79f, 0.90f, 110.0f, 1.00f };
        }
    }

    void updateTargets (const Params& p, int n)
    {
        // glide over ~60 ms (per block): parameter and mode changes never jump
        const float k = primed ? 1.0f - std::exp (-(float) n / (0.06f * (float) sr)) : 1.0f;
        const auto mv = modeValues (p.mode);
        auto glide = [k] (float& v, float target) { v += (target - v) * k; };
        glide (mInDiff, mv.inDiff); glide (mLoopAp, mv.loopAp); glide (mToneLo, mv.toneLo); glide (mToneHi, mv.toneHi);
        glide (mBand, mv.band); glide (mModDepth, mv.modDepth); glide (mRateA, mv.rateA); glide (mRateB, mv.rateB);
        glide (mWidth, mv.width); glide (mLowCutHz, mv.lowCutHz); glide (mSizeScale, mv.sizeScale);
        glide (sDecay, juce::jlimit (0.1f, 30.0f, p.decay));
        glide (sTone, juce::jlimit (0.0f, 1.0f, p.tone));
        glide (sMod, juce::jlimit (0.0f, 1.0f, p.mod));
        // size glides slower (~250 ms): changing delay lengths bends pitch while it moves
        const float ks = primed ? 1.0f - std::exp (-(float) n / (0.25f * (float) sr)) : 1.0f;
        sSize += (juce::jlimit (0.0f, 1.0f, p.size) - sSize) * ks;
        tgtMix = juce::jlimit (0.0f, 1.0f, p.mix);
        if (! primed) curMix = tgtMix;

        inDiffGain = mInDiff;
        loopApGain = mLoopAp;
        width = mWidth;
        auto onePole = [this] (float hz) { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (hz, (float) sr * 0.45f) / (float) sr); };
        // Tone: damping cut-off on a log scale between the mode's dark and bright ends
        const float dampHz = mToneLo * std::pow (mToneHi / mToneLo, sTone);
        dampA = onePole (dampHz);
        {
            // one-pole low pass magnitude at the reference frequency
            const float w = juce::MathConstants<float>::twoPi * 750.0f / (float) sr, b = 1.0f - dampA;
            dampAtRef = dampA / std::sqrt (1.0f - 2.0f * b * std::cos (w) + b * b);
        }
        inBandA = onePole (mBand * (0.55f + 0.45f * sTone));
        inLowCutA = onePole (mLowCutHz);
        lowCutA = onePole (55.0f - 30.0f * sTone);   // darker settings also hold back the lowest rumble in the tail
        modDepth = sMod * mModDepth * rateScale;
        for (int j = 0; j < 2; ++j)
            lfoW[j] = juce::MathConstants<float>::twoPi * (j == 0 ? mRateA : mRateB) / (float) sr;

        // Size: 0..1 -> 0.32..1 of the longest delay set (times the mode's scale)
        const float scale = (0.32f + 0.68f * sSize) * mSizeScale;
        curDecay = sDecay;
        for (int i = 0; i < N; ++i)
        {
            tgtLen[i] = (float) (lineMs[i] * 0.001 * sr) * scale;
            if (! primed) curLen[i] = tgtLen[i];
            // g = 10^(-3 * loopTime / T60); the loop is the delay line plus its allpass
            const float loopSamples = tgtLen[i] + modDepth * 0.5f + (float) loopAp[i].buf.size() + (float) loopAp2[i].buf.size();
            // ...and the damping filter's loss in the midrange (where the tail's length is heard) made up, so Decay is
            // the time a 500 Hz..2 kHz tail takes to fall 60 dB. Capped below 1: the loop always loses energy.
            gain[i] = juce::jmin (0.9995f, std::pow (10.0f, -3.0f * loopSamples / (sDecay * (float) sr)) / dampAtRef);
        }
        // the wet level grows with the decay; keep long settings from getting much louder than short ones. Overall level
        // matched to the previous reverb (so existing presets keep their dry/wet balance)
        wetGain = 8.6f / std::sqrt (1.0f + 0.15f * sDecay);
        primed = true;
    }

    static constexpr int numDiffusers = 3;
    static constexpr float maxSizeScale = 1.12f;
    static constexpr float maxModDepth = 32.0f;
    static constexpr double diffuserMs[2][numDiffusers] { { 4.77, 3.59, 9.33 }, { 4.31, 3.17, 8.71 } };
    // line lengths at full size (ms), chosen so no two share simple ratios; long enough for a dense mode pattern
    // without becoming audible echoes
    static constexpr double lineMs[8] { 56.8, 66.7, 77.9, 89.6, 61.5, 71.6, 82.8, 95.0 };
    static constexpr double loopApMs[8] { 7.9, 5.3, 9.7, 6.7, 8.3, 4.9, 10.3, 6.1 };
    static constexpr double loopAp2Ms[8] { 3.1, 4.3, 2.3, 3.7, 2.9, 4.1, 2.6, 3.4 };
    // output taps: two orthogonal sign patterns (mono sum keeps half of the lines, so it never cancels out)
    static constexpr float outL[8] { 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f };
    static constexpr float outR[8] { 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f };
    static constexpr float injGain = 0.5f;

    double sr = 44100.0;
    float rateScale = 1.0f;
    Allpass diffuser[2][numDiffusers];
    Allpass loopAp[N], loopAp2[N];
    std::vector<float> lines[N];
    int writePos = 0;   // lines are powers of two long, so (writePos & (length - 1)) indexes each
    float damp[N] {}, lowCut[N] {}, gain[N] {};
    float inLowCut[2] {}, inBand[2] {};
    struct Osc { float s, c; } lfo[2] { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    float lfoW[2] {};
    float curLen[N] {}, tgtLen[N] {};
    float curMix = 0.0f, tgtMix = 0.0f;
    float inDiffGain = 0.6f, loopApGain = 0.5f, width = 1.0f, dampA = 0.5f, inBandA = 0.9f, inLowCutA = 0.01f, lowCutA = 0.005f;
    float modDepth = 0.0f, wetGain = 0.5f, curDecay = 3.0f, dampAtRef = 1.0f;
    // glided values
    float mInDiff = 0.66f, mLoopAp = 0.52f, mToneLo = 900.0f, mToneHi = 14000.0f, mBand = 15000.0f, mModDepth = 11.0f,
          mRateA = 0.55f, mRateB = 0.79f, mWidth = 0.9f, mLowCutHz = 110.0f, mSizeScale = 1.0f;   // (Smooth)
    float sDecay = 3.0f, sTone = 0.5f, sMod = 0.25f, sSize = 0.65f;
    bool primed = false;
};

using VintageReverb = FdnReverb<4>;
