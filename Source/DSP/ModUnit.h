#pragma once
#include "ModShape.h"
#include "../Params.h"

struct ModUnitParams
{
    int mode = P::MLfo;
    double rateHz = 1.0;   // already resolved from the sync rate when synced
    int syncIndex = -1;    // index into P::syncRateNames when synced, -1 = free running (Hz)
    float start = 0.0f;
    bool retrig = true;
    float smoothMs = 3.0f;   // jumps in the output glide over this time (0 = sharp)
};

// LFO / sample & hold / one-shot envelope, evaluated at control rate.
class ModUnit
{
public:
    void trigger (const ModUnitParams& p, juce::Random& rng, double freeRunningPhase)
    {
        phase = p.retrig || p.mode == P::MEnvelope ? (double) p.start : freeRunningPhase;
        shValue = rng.nextFloat();
        snap = true;   // a new note starts from the true value, not a glide from the previous note
    }

    double getPhase() const { return phase; }

    // Phase of a synced LFO locked to the song: bar 1 beat 1 is the start of the shape (plus Start).
    static double songPhase (double ppq, double beats, float start)
    {
        const double x = ppq / juce::jmax (1.0e-6, beats) + (double) start;
        return x - std::floor (x);
    }

    // Returns the current value (0..1, the shape's level; modulation only pushes knobs up
    // for positive matrix amounts), then advances by numSamples.
    // lockedPhase >= 0: the LFO follows the host's song position (synced, Retrig off, host playing).
    float process (const ModUnitParams& p, const ModShape& shape, int numSamples, double sampleRate, juce::Random& rng,
                   double lockedPhase = -1.0)
    {
        if (lockedPhase >= 0.0 && p.mode != P::MEnvelope)
        {
            if (lockedPhase < phase) shValue = rng.nextFloat();   // wrapped round (or the playhead jumped back): new S&H value
            phase = lockedPhase;
        }
        float value = 0.0f;
        switch (p.mode)
        {
            case P::MSampleHold: value = shValue; break;
            case P::MEnvelope:   value = shape.eval ((float) juce::jmin (1.0, phase)); break;
            default:             value = shape.eval ((float) phase); break;
        }

        phase += p.rateHz * (double) numSamples / sampleRate;
        if (p.mode == P::MEnvelope)
        {
            phase = juce::jmin (phase, 1.0);
        }
        else if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            shValue = rng.nextFloat();
        }
        // smoothing (one-pole, evaluated per control block)
        if (snap || p.smoothMs <= 0.0f)
            smoothed = value;
        else
            smoothed += (value - smoothed) * (1.0f - std::exp (-(float) numSamples / (p.smoothMs * 0.001f * (float) sampleRate)));
        snap = false;
        lastValue = smoothed;
        return smoothed;
    }

    float lastValue = 0.0f;

private:
    float smoothed = 0.0f;
    bool snap = true;
    double phase = 0.0;
    float shValue = 0.0f;
};
