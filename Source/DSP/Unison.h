#pragma once
#include <JuceHeader.h>

// Unison layout shared by the wavetable oscillator and the resynthesis partial bank.
// Everything here is a pure function of the settings, so it can be tested on its own.
namespace Unison
{
constexpr int maxVoices = 16;

enum PhaseMode { PhaseReset, PhaseRandom, PhaseSpread };
enum DetuneMode { DetuneLinear, DetuneSuper };

// Normalised voice position p in [-1, 1]; 0 for a single voice.
inline float position (int k, int n) { return n <= 1 ? 0.0f : 2.0f * (float) k / (float) (n - 1) - 1.0f; }

// Centre voice(s): the middle voice for odd counts, the two middle voices for even counts.
inline bool isCentre (int k, int n) { return n == 1 || (n % 2 == 1 ? k == n / 2 : (k == n / 2 - 1 || k == n / 2)); }

// Detune distribution: maps a voice position to its share of the maximum detune.
// Linear: evenly spaced. Super: inner voices stay close to the played pitch and the gaps widen
// towards the outer voices (|p|^1.5, approximating the published JP-8000 supersaw spacing). Both are
// odd functions, so offsets stay symmetric and the average pitch stays centred.
inline float detuneShape (float p, int mode)
{
    if (mode == DetuneSuper) return (p < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (p), 1.5f);
    return p;
}

inline float detuneCentsFor (int k, int n, float detuneCents, int mode) { return detuneShape (position (k, n), mode) * detuneCents; }

// Frequency multipliers and equal-power stereo gains for n voices.
// pan = width * p. Outer voices are at `blend` level relative to the centre voice(s); the gains are
// normalised so the summed power is constant (= 1/sqrt(N) per voice when blend = 1).
inline void layout (int n, float detuneCents, int detuneMode, float blend, float width,
                    float* freqMul, float* gainL, float* gainR)
{
    const float quarterPi = juce::MathConstants<float>::pi * 0.25f;
    width = juce::jlimit (0.0f, 1.0f, width);
    blend = juce::jlimit (0.0f, 1.0f, blend);
    float g[maxVoices];
    float sumSq = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        g[k] = isCentre (k, n) ? 1.0f : blend;
        sumSq += g[k] * g[k];
        freqMul[k] = std::pow (2.0f, detuneCentsFor (k, n, detuneCents, detuneMode) / 1200.0f);
        const float angle = (width * position (k, n) + 1.0f) * quarterPi;
        gainL[k] = std::cos (angle);
        gainR[k] = std::sin (angle);
    }
    const float norm = sumSq > 0.0f ? 1.0f / std::sqrt (sumSq) : 0.0f;
    for (int k = 0; k < n; ++k)
    {
        gainL[k] *= g[k] * norm;
        gainR[k] *= g[k] * norm;
    }
}

// Oscillator pan (-1..1) as a constant-power balance on the whole stereo image: 1/1 at centre,
// sqrt2/0 hard left, so a centred sound keeps its loudness when panned.
inline void panGains (float pan, float& l, float& r)
{
    const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    l = juce::MathConstants<float>::sqrt2 * std::cos (angle);
    r = juce::MathConstants<float>::sqrt2 * std::sin (angle);
}

// Bass Mono crossover for a partial at f Hz: how much of it goes to the single centred bass voice and how
// much to the unison voices. A smooth half-octave transition (constant power) around bassFreq, so partials
// gliding across the crossover never switch abruptly. With a single unison voice both parts play in phase,
// so the split is linear (amplitudes add up to 1); with several voices it is constant power.
inline void bassSplit (float f, float bassFreq, bool singleVoice, float& bass, float& unison)
{
    const float x = juce::jlimit (-1.0f, 1.0f, std::log2 (juce::jmax (1.0f, f) / bassFreq) * 4.0f);   // -1 at -1/4 oct, +1 at +1/4 oct
    const float w = 0.5f - 0.5f * x;                                                                    // share in the bass voice
    if (singleVoice) { bass = w; unison = 1.0f - w; return; }
    bass = std::sin (w * juce::MathConstants<float>::halfPi);
    unison = std::cos (w * juce::MathConstants<float>::halfPi);
}

// Starting phase (cycles, [0, 1)) of voice k at note-on.
//  Reset:  every voice at `start` (repeatable, no randomness).
//  Random: `start` plus amount * a random offset per voice (amount 1 = fully random).
//  Spread: `start` plus amount * k / n, evenly fanned out and deterministic.
inline float initialPhase (int k, int n, int mode, float start, float amount, juce::Random& rng)
{
    float p = start;
    if (mode == PhaseRandom) p += amount * rng.nextFloat();
    else if (mode == PhaseSpread) p += amount * (float) k / (float) juce::jmax (1, n);
    return p - std::floor (p);
}
} // namespace Unison
