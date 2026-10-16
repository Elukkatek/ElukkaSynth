#pragma once
#include <JuceHeader.h>

// Harmonic "mangling" shared by the additive oscillators and the resynthesis partials.
// Both return a frequency deviation in harmonics (multiples of the fundamental). The fundamental itself
// never moves, so the note keeps a recognisable pitch.
namespace Mangle
{
// Highest harmonic that Prism/Shimmer move on the additive oscillators (the rest stay in the wavetable).
constexpr int maxMovedHarmonic = 64;

// Fixed, uneven value in [-1, 1] for each integer, smoothly interpolated in between, so a partial
// gliding between harmonic positions gets a smoothly changing offset.
inline float prismPattern (float h)
{
    auto hash = [] (int k)
    {
        uint32_t x = (uint32_t) k * 2654435761u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return (float) (x & 0xffff) / 32767.5f - 1.0f;
    };
    const float fl = std::floor (h);
    const float t = h - fl;
    const float a = hash ((int) fl), b = hash ((int) fl + 1);
    return a + (b - a) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t));
}

// How strongly harmonic h is inside the Prism band: 1 at `center`, fading to 0 at +-width/2 octaves.
// Harmonics at or below the fundamental are never moved.
inline float prismWindow (float h, float center, float widthOct)
{
    const float d = std::log2 (juce::jmax (1.0f, h) / center) / (0.5f * widthOct);
    if (std::abs (d) >= 1.0f) return 0.0f;
    return (0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * d)) * juce::jlimit (0.0f, 1.0f, h - 1.0f);
}

// Prism: harmonics in the band are pushed up to half a harmonic up or down (amount 0..1).
inline float prism (float h, float amount, float center, float widthOct)
{
    if (amount <= 0.0f) return 0.0f;
    const float w = prismWindow (h, center, widthOct);
    return w > 0.0f ? amount * 0.5f * w * prismPattern (h) : 0.0f;
}

// Shimmer: `drift` in [-1, 1] (a slowly wandering value per partial) detunes the partial by up to
// +-80 cents at amount 1. The fundamental stays put.
inline float shimmer (float h, float amount, float drift)
{
    if (amount <= 0.0f) return 0.0f;
    const float cents = amount * 80.0f * drift * juce::jlimit (0.0f, 1.0f, h - 1.0f);
    return h * (std::exp2 (cents / 1200.0f) - 1.0f);
}

// A smooth random walk per partial: a new random target every 1/rate seconds (each partial at a slightly
// different speed so they never move in step), cosine-interpolated in between.
class Drift
{
public:
    static constexpr int size = 256;

    void reset (juce::Random& rng)
    {
        for (int i = 0; i < size; ++i)
        {
            prev[i] = rng.nextFloat() * 2.0f - 1.0f;
            next[i] = rng.nextFloat() * 2.0f - 1.0f;
            phase[i] = rng.nextFloat();
            speed[i] = 0.7f + 0.6f * rng.nextFloat();
        }
    }

    void advance (float rateHz, int numSamples, double sampleRate, juce::Random& rng)
    {
        const float step = rateHz * (float) numSamples / (float) sampleRate;
        for (int i = 0; i < size; ++i)
        {
            phase[i] += step * speed[i];
            while (phase[i] >= 1.0f)
            {
                phase[i] -= 1.0f;
                prev[i] = next[i];
                next[i] = rng.nextFloat() * 2.0f - 1.0f;
            }
            value[i] = prev[i] + (next[i] - prev[i]) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * phase[i]));
        }
    }

    const float* values() const { return value; }

private:
    float prev[size] {}, next[size] {}, phase[size] {}, speed[size] {}, value[size] {};
};
} // namespace Mangle
