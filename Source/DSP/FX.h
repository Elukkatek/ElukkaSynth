#pragma once
#include <JuceHeader.h>
#include "../Params.h"
#include "Reverb.h"
#include "Filter.h"

struct FXParams
{
    bool distOn = false; int distType = 0; float distDrive = 0.3f, distMix = 1.0f;
    bool combOn = false; float combFreq = 400.0f, combFb = 0.6f, combMix = 0.5f;
    bool eqOn = false; float eqLow = 0.0f, eqMidF = 1000.0f, eqMid = 0.0f, eqHigh = 0.0f;
    float eqLowF = 150.0f, eqLowQ = 0.71f, eqMidQ = 1.0f, eqHighF = 6000.0f, eqHighQ = 0.71f;
    float combLp = 20000.0f, combHp = 20.0f;
    bool dlySync = true; float dlyMs = 375.0f, dlyLp = 20000.0f, dlyHp = 20.0f;
    bool ottOn = false; float ottAmount = 0.5f, ottSpeed = 0.5f;   // 0..1
    bool dlyOn = false; float dlyBeats = 0.75f, dlyFb = 0.4f, dlyMix = 0.25f; bool dlyPingPong = true;
    bool revOn = false; VintageReverb::Params rev;
    bool fltOn = false; int fltType = 0, fltSlope = 0; float fltCutoff = 20000.0f, fltRes = 0.0f, fltDrive = 0.0f;
};

// Low shelf, bell and high shelf of the EQ as raw biquad coefficients (no allocation, for the audio thread).
inline std::array<std::array<float, 6>, 3> makeEqArrays (double sr, const FXParams& p)
{
    using C = juce::dsp::IIR::ArrayCoefficients<float>;
    const double top = sr * 0.45;
    return { C::makeLowShelf (sr, (float) juce::jmin ((double) p.eqLowF, top), p.eqLowQ, juce::Decibels::decibelsToGain (p.eqLow)),
             C::makePeakFilter (sr, (float) juce::jmin ((double) p.eqMidF, top), p.eqMidQ, juce::Decibels::decibelsToGain (p.eqMid)),
             C::makeHighShelf (sr, (float) juce::jmin ((double) p.eqHighF, top), p.eqHighQ, juce::Decibels::decibelsToGain (p.eqHigh)) };
}

// The same as filter objects. Used by the UI to draw the curve.
inline std::array<juce::dsp::IIR::Coefficients<float>::Ptr, 3> makeEqCoefficients (double sr, const FXParams& p)
{
    using C = juce::dsp::IIR::Coefficients<float>;
    const auto a = makeEqArrays (sr, p);
    return { new C (a[0]), new C (a[1]), new C (a[2]) };
}

// 12 dB/oct Butterworth state-variable low or high pass for an effect's wet signal (stereo).
// Bypassed when fully open (low pass >= 20 kHz, high pass <= 20 Hz).
struct WetFilter
{
    void set (double sr, float hz, bool isHighPass)
    {
        highPass = isHighPass;
        active = highPass ? hz > 20.5f : hz < 19999.0f;
        if (! active) return;
        g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, (float) (sr * 0.45), hz) / (float) sr);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline float process (float x, int c) noexcept
    {
        if (! active) return x;
        const float v3 = x - ic2[c];
        const float v1 = a1 * ic1[c] + a2 * v3;
        const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
        ic1[c] = 2.0f * v1 - ic1[c];
        ic2[c] = 2.0f * v2 - ic2[c];
        return highPass ? x - k * v1 - v2 : v2;
    }
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    bool active = false, highPass = false;
    float g = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    static constexpr float k = 1.41421356f;   // 1/Q, Q = 0.707
    float ic1[2] {}, ic2[2] {};
};

// OTT-style multiband compressor: 3 bands (crossovers 88 Hz and 2.5 kHz), each compressed downward above
// and upward below a target level. Amount blends in the gain changes; Speed sets attack/release.
class OttCompressor
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
        auto setup = [&] (juce::dsp::LinkwitzRileyFilter<float>& f, juce::dsp::LinkwitzRileyFilterType type, float hz)
        {
            f.prepare (spec);
            f.setType (type);
            f.setCutoffFrequency (hz);
            f.reset();
        };
        setup (lp1, juce::dsp::LinkwitzRileyFilterType::lowpass, 88.0f);
        setup (hp1, juce::dsp::LinkwitzRileyFilterType::highpass, 88.0f);
        setup (lp2, juce::dsp::LinkwitzRileyFilterType::lowpass, 2500.0f);
        setup (hp2, juce::dsp::LinkwitzRileyFilterType::highpass, 2500.0f);
        setup (ap2, juce::dsp::LinkwitzRileyFilterType::allpass, 2500.0f);
        for (auto& e : env) e = 0.0f;
        for (auto& m : meterDb) m = 0.0f;
    }

    void process (float* L, float* R, int n, float amount, float speed)
    {
        const float release = 0.6f * std::pow (0.025f, speed);    // 600 ms .. 15 ms
        const float attack = release * 0.05f;                       // 30 ms .. 0.75 ms
        const float att = 1.0f - std::exp (-1.0f / (attack * (float) sr));
        const float rel = 1.0f - std::exp (-1.0f / (release * (float) sr));
        float* ch[2] = { L, R };
        float lastDb[3] {};
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                const float rest = hp1.processSample (c, x);
                band[0][c] = ap2.processSample (c, lp1.processSample (c, x));   // keeps the low band in phase with the others
                band[1][c] = lp2.processSample (c, rest);
                band[2][c] = hp2.processSample (c, rest);
            }
            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float level = juce::jmax (std::abs (band[b][0]), std::abs (band[b][1]));
                env[b] += (level > env[b] ? att : rel) * (level - env[b]);
                const float db = gainDb (juce::Decibels::gainToDecibels (env[b], -120.0f)) * amount;
                const float g = juce::Decibels::decibelsToGain (db);
                outL += band[b][0] * g;
                outR += band[b][1] * g;
                lastDb[b] = db;
            }
            L[i] = outL;
            R[i] = outR;
        }
        for (int b = 0; b < 3; ++b) meterDb[b] = lastDb[b];
    }

    float meterDb[3] {};   // current gain change per band (dB), for the UI

private:
    // Static curve: above the target, 6:1 downward; below it, 3:1 upward (max +30 dB, fading out
    // near silence so noise floors are not pulled up).
    static float gainDb (float levelDb)
    {
        const float target = -24.0f;
        if (levelDb > target) return (target - levelDb) * (1.0f - 1.0f / 6.0f);
        const float up = juce::jmin (30.0f, (target - levelDb) * (1.0f - 1.0f / 3.0f));
        return up * juce::jlimit (0.0f, 1.0f, (levelDb + 80.0f) / 15.0f);
    }

    double sr = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> lp1, hp1, lp2, hp2, ap2;
    float env[3] {};
};

// Effects run in a user-defined order (P::Fx indices).
class FXChain
{
public:
    using Order = std::array<int, P::NumFx>;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        combLen = (int) (sr / 20.0) + 4;
        for (auto& c : comb) { c.assign ((size_t) combLen, 0.0f); }
        combPos = 0;
        dlyLen = (int) (sr * 6.0) + 4;
        for (auto& d : delay) d.assign ((size_t) dlyLen, 0.0f);
        dlyPos = 0;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
        for (auto& f : eq) { f.prepare (spec); f.reset(); }
        reverb.prepare (sr);
        revWasOn = false;
        smoothedDelay = -1.0f;
        eqDirty = true;
        for (auto* f : { &combLp, &combHp, &dlyLp, &dlyHp }) f->reset();
        ott.prepare (sr, juce::jmax (maxBlock, 512));
        combWasOn = false;
        fxFilter.reset();
        fltWasOn = false;
    }

    void process (float* L, float* R, int n, const FXParams& p, double bpm, const Order& order)
    {
        for (int fx : order)
            switch (fx)
            {
                case P::FxComb:
                    if (p.combOn)
                    {
                        // start from a clean buffer so old content is never replayed when switching on
                        if (! combWasOn) { for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f); combLp.reset(); combHp.reset(); combDelay = -1.0f; }
                        combFilter (L, R, n, p);
                    }
                    combWasOn = p.combOn;
                    break;
                case P::FxOtt:    if (p.ottOn) ott.process (L, R, n, p.ottAmount, p.ottSpeed); else for (auto& m : ott.meterDb) m = 0.0f; break;
                case P::FxDist:   if (p.distOn) distortion (L, R, n, p); break;
                case P::FxDelay:  if (p.dlyOn) delayLine (L, R, n, p, bpm); else smoothedDelay = -1.0f; break;
                case P::FxReverb:
                    if (p.revOn)
                    {
                        if (! revWasOn) reverb.reset();   // switching on starts from silence, never an old tail
                        reverb.process (L, R, n, p.rev);
                    }
                    revWasOn = p.revOn;
                    break;
                case P::FxEq:     if (p.eqOn) equalise (L, R, n, p); break;
                case P::FxFilter:
                    if (p.fltOn)
                    {
                        if (! fltWasOn) { fxFilter.reset(); fltCutoff = -1.0f; }   // switching on starts from a clean state
                        filterFx (L, R, n, p);
                    }
                    fltWasOn = p.fltOn;
                    break;
                default: break;
            }
    }

private:
    static float shape (float x, int type)
    {
        switch (type)
        {
            case 1: return juce::jlimit (-1.0f, 1.0f, x);
            case 2:
            {
                // foldback
                float y = x;
                for (int i = 0; i < 8 && std::abs (y) > 1.0f; ++i)
                    y = y > 1.0f ? 2.0f - y : -2.0f - y;
                return juce::jlimit (-1.0f, 1.0f, y);
            }
            case 3: return std::sin (x * juce::MathConstants<float>::halfPi);
            default: return std::tanh (x);
        }
    }

    // The voice filter on the summed signal; the cutoff glides (exponentially) across the block so changes never step
    void filterFx (float* L, float* R, int n, const FXParams& p)
    {
        fxFilter.setup (sr, p.fltType, p.fltSlope, p.fltRes, p.fltDrive);
        const float target = juce::jlimit (20.0f, 20000.0f, p.fltCutoff);
        if (fltCutoff < 0.0f) fltCutoff = target;
        const float step = std::pow (target / fltCutoff, 1.0f / (float) juce::jmax (1, n));
        for (int i = 0; i < n; ++i)
        {
            fltCutoff *= step;
            fxFilter.process (L[i], R[i], fltCutoff);
        }
        fltCutoff = target;
    }

    void distortion (float* L, float* R, int n, const FXParams& p)
    {
        const float drive = 1.0f + p.distDrive * p.distDrive * 40.0f;
        const float makeup = 1.0f / std::sqrt (drive) * 1.5f;
        for (int i = 0; i < n; ++i)
        {
            float wl = shape (L[i] * drive, p.distType) * juce::jmin (1.0f, makeup);
            float wr = shape (R[i] * drive, p.distType) * juce::jmin (1.0f, makeup);
            L[i] += (wl - L[i]) * p.distMix;
            R[i] += (wr - R[i]) * p.distMix;
        }
    }

    void combFilter (float* L, float* R, int n, const FXParams& p)
    {
        combLp.set (sr, p.combLp, false);
        combHp.set (sr, p.combHp, true);
        // glide the delay length across the block so frequency changes never jump
        const float target = juce::jlimit (1.0f, (float) combLen - 2.0f, (float) (sr / p.combFreq));
        if (combDelay < 0.0f) combDelay = target;
        const float dInc = (target - combDelay) / (float) n;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            combDelay += dInc;
            const float d = combDelay;
            float rp = (float) combPos - d;
            if (rp < 0) rp += (float) combLen;
            int i0 = (int) rp;
            float t = rp - (float) i0;
            int i1 = (i0 + 1) % combLen;
            for (int c = 0; c < 2; ++c)
            {
                auto& buf = comb[c];
                float delayed = buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * t;
                float x = ch[c][i];
                float y = x + p.combFb * delayed;
                buf[(size_t) combPos] = juce::jlimit (-4.0f, 4.0f, y);
                const float wet = combHp.process (combLp.process (y * (1.0f - std::abs (p.combFb) * 0.5f), c), c);
                ch[c][i] = x + (wet - x) * p.combMix;
            }
            combPos = (combPos + 1) % combLen;
        }
    }

    void equalise (float* L, float* R, int n, const FXParams& p)
    {
        const float cur[] { p.eqLow, p.eqMidF, p.eqMid, p.eqHigh, p.eqLowF, p.eqLowQ, p.eqMidQ, p.eqHighF, p.eqHighQ };
        if (eqDirty || ! std::equal (std::begin (cur), std::end (cur), std::begin (lastEq)))
        {
            // assigned into the filters' existing coefficient storage: no allocation
            const auto c = makeEqArrays (sr, p);
            for (int b = 0; b < 3; ++b)
            {
                *eq[b * 2].coefficients = c[(size_t) b];
                *eq[b * 2 + 1].coefficients = c[(size_t) b];
            }
            std::copy (std::begin (cur), std::end (cur), std::begin (lastEq));
            eqDirty = false;
        }
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            for (int b = 0; b < 3; ++b)
            {
                l = eq[b * 2].processSample (l);
                r = eq[b * 2 + 1].processSample (r);
            }
            L[i] = l; R[i] = r;
        }
    }

    void delayLine (float* L, float* R, int n, const FXParams& p, double bpm)
    {
        const double seconds = p.dlySync ? p.dlyBeats * 60.0 / bpm : p.dlyMs * 0.001;
        const float target = juce::jlimit (1.0f, (float) dlyLen - 2.0f, (float) (seconds * sr));
        dlyLp.set (sr, p.dlyLp, false);
        dlyHp.set (sr, p.dlyHp, true);
        if (smoothedDelay < 0.0f) smoothedDelay = target;
        for (int i = 0; i < n; ++i)
        {
            smoothedDelay += (target - smoothedDelay) * 0.0005f;
            float rp = (float) dlyPos - smoothedDelay;
            if (rp < 0) rp += (float) dlyLen;
            int i0 = (int) rp;
            float t = rp - (float) i0;
            int i1 = (i0 + 1) % dlyLen;
            // the feedback loop is unfiltered; the low/high pass shape only the wet signal that is heard
            const float rl = delay[0][(size_t) i0] + (delay[0][(size_t) i1] - delay[0][(size_t) i0]) * t;
            const float rr = delay[1][(size_t) i0] + (delay[1][(size_t) i1] - delay[1][(size_t) i0]) * t;
            const float dl = dlyHp.process (dlyLp.process (rl, 0), 0);
            const float dr = dlyHp.process (dlyLp.process (rr, 1), 1);
            float inL = L[i], inR = R[i];
            if (p.dlyPingPong)
            {
                // Ping-pong: the input enters on the left; each right echo repeats the left one before it at the same
                // level and the feedback acts once per round trip (fb squared), so both sides are equally loud and the
                // tail dies away at the same rate as with feedback fb on every bounce (left 1, right 1, left fb^2, ...)
                delay[0][(size_t) dlyPos] = (inL + inR) * 0.5f + rr * p.dlyFb * p.dlyFb;
                delay[1][(size_t) dlyPos] = rl;
            }
            else
            {
                delay[0][(size_t) dlyPos] = inL + rl * p.dlyFb;
                delay[1][(size_t) dlyPos] = inR + rr * p.dlyFb;
            }
            // dry/wet crossfade: 0% = dry only, 100% = echoes only
            L[i] = inL * (1.0f - p.dlyMix) + dl * p.dlyMix;
            R[i] = inR * (1.0f - p.dlyMix) + dr * p.dlyMix;
            dlyPos = (dlyPos + 1) % dlyLen;
        }
    }

    double sr = 44100.0;
    std::vector<float> comb[2];
    int combLen = 0, combPos = 0;
    std::vector<float> delay[2];
    int dlyLen = 0, dlyPos = 0;
    float smoothedDelay = -1.0f;
    juce::dsp::IIR::Filter<float> eq[6];
    float lastEq[9] {};
    bool eqDirty = true;
    WetFilter combLp, combHp, dlyLp, dlyHp;
    bool revWasOn = false;
    bool combWasOn = false;
    VoiceFilter fxFilter;
    float fltCutoff = -1.0f;   // gliding cutoff (Hz); < 0 = jump to the target
    bool fltWasOn = false;
    float combDelay = -1.0f;   // current (smoothed) comb delay in samples

public:
    OttCompressor ott;
    VintageReverb reverb;
};
