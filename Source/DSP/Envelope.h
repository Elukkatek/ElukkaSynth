#pragma once
#include <JuceHeader.h>

// Linear ADSR with the same shape as juce::ADSR, but safe to update every block: the release speed is
// based on the level the note was released at, so a release always plays out over the Release time
// (juce::ADSR recalculates it from the sustain level, which ends the note at once when Sustain is 0).
class Envelope
{
public:
    using Parameters = juce::ADSR::Parameters;

    void setSampleRate (double newSampleRate) { sr = newSampleRate; }

    void setParameters (const Parameters& p)
    {
        params = p;
        attackRate = rate (1.0f, p.attack);
        decayRate = rate (1.0f - p.sustain, p.decay);
        releaseRate = rate (releaseFrom, p.release);
        if ((state == State::attack && attackRate <= 0.0f)
            || (state == State::decay && (decayRate <= 0.0f || value <= p.sustain))
            || (state == State::release && releaseRate <= 0.0f))
            next();
    }

    void noteOn()
    {
        if (attackRate > 0.0f)     state = State::attack;
        else if (decayRate > 0.0f) { value = 1.0f; state = State::decay; }
        else                       { value = params.sustain; state = State::sustain; }
    }

    void noteOff()
    {
        if (state == State::idle) return;
        if (params.release > 0.0f && value > 0.0f)
        {
            releaseFrom = value;
            releaseRate = rate (releaseFrom, params.release);
            state = State::release;
        }
        else
        {
            reset();
        }
    }

    float getNextSample()
    {
        switch (state)
        {
            case State::idle: return 0.0f;
            case State::attack:  value += attackRate; if (value >= 1.0f) { value = 1.0f; next(); } break;
            case State::decay:   value -= decayRate; if (value <= params.sustain) { value = params.sustain; next(); } break;
            case State::sustain: value = params.sustain; break;
            case State::release: value -= releaseRate; if (value <= 0.0f) next(); break;
        }
        return value;
    }

    bool isActive() const { return state != State::idle; }
    void reset() { value = 0.0f; state = State::idle; }

private:
    enum class State { idle, attack, decay, sustain, release };

    float rate (float distance, float seconds) const { return seconds > 0.0f ? (float) (distance / (seconds * sr)) : -1.0f; }

    void next()
    {
        if (state == State::attack)       state = decayRate > 0.0f ? State::decay : State::sustain;
        else if (state == State::decay)   state = State::sustain;
        else if (state == State::release) reset();
    }

    double sr = 44100.0;
    Parameters params;
    State state = State::idle;
    float value = 0.0f, releaseFrom = 0.0f;
    float attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f;
};
