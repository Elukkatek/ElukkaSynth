#pragma once
#include <JuceHeader.h>

// Per-voice stereo filter. 24 and 12 dB slopes use state-variable filters (two cascaded for 24 dB),
// 6 dB uses one-pole filters (band pass = one-pole high pass into one-pole low pass; no resonance).
class VoiceFilter
{
public:
    enum Type { LowPass, HighPass, BandPass };
    enum Slope { Slope24, Slope12, Slope6 };

    void reset()
    {
        for (auto& c : s) for (auto& v : c) v = 0.0f;
        lastCutoff = -1.0f;
    }

    void setup (double sampleRate, int filterType, int filterSlope, float resonance, float driveAmount)
    {
        sr = sampleRate;
        type = filterType;
        slope = filterSlope;
        drive = driveAmount;
        driveGain = 1.0f + 9.0f * drive;
        driveNorm = 1.0f / std::sqrt (driveGain);
        // Resonance raises the Q of the last stage; at 0 the response is Butterworth (flat passband).
        k1 = slope == Slope24 ? 1.0f / 0.541f : 1.0f / (0.707f + resonance * 20.0f);
        k2 = 1.0f / (1.307f + resonance * 20.0f);
    }

    inline void process (float& l, float& r, float cutoffHz) noexcept
    {
        if (cutoffHz != lastCutoff)
        {
            lastCutoff = cutoffHz;
            const float fc = juce::jlimit (10.0f, (float) (sr * 0.45), cutoffHz);
            g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
            onePole = g / (1.0f + g);
        }
        if (drive > 0.0f)
        {
            l = std::tanh (l * driveGain) * driveNorm;
            r = std::tanh (r * driveGain) * driveNorm;
        }
        l = channel (l, 0);
        r = channel (r, 1);
    }

private:
    // Topology-preserving state-variable filter stage (Simper). Returns the selected response.
    inline float svf (float x, float& ic1, float& ic2, float k) const noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (type == LowPass) return v2;
        if (type == HighPass) return x - k * v1 - v2;
        return v1 * k;   // band pass normalised to unity gain at the centre
    }

    inline float pole (float x, float& state, bool highPass) const noexcept
    {
        const float v = (x - state) * onePole;
        const float lp = v + state;
        state = lp + v;
        return highPass ? x - lp : lp;
    }

    inline float channel (float x, int c) noexcept
    {
        auto& st = s[c];
        switch (slope)
        {
            case Slope24: return svf (svf (x, st[0], st[1], k1), st[2], st[3], k2);
            case Slope12: return svf (x, st[0], st[1], k1);
            default:
                if (type == BandPass) return pole (pole (x, st[0], true), st[1], false);
                return pole (x, st[0], type == HighPass);
        }
    }

    double sr = 44100.0;
    int type = LowPass, slope = Slope24;
    float drive = 0.0f, driveGain = 1.0f, driveNorm = 1.0f;
    float k1 = 1.0f, k2 = 1.0f, g = 1.0f, onePole = 0.5f, lastCutoff = -1.0f;
    float s[2][4] {};
};
