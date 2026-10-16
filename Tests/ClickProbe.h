#pragma once
// Click detection: finds sample-to-sample discontinuities. For smooth material the second difference
// x[n] - 2x[n-1] + x[n-2] is tiny; a click shows up as a spike far above the signal's local level.
#include <JuceHeader.h>

struct ClickReport { int clicks = 0; float worst = 0.0f; int firstAt = -1; };

inline ClickReport detectClicks (const juce::AudioBuffer<float>& b, float threshold = 0.02f, int skipStart = 0)
{
    ClickReport r;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* x = b.getReadPointer (ch);
        int lastClick = -100000;
        for (int n = juce::jmax (2, skipStart); n < b.getNumSamples(); ++n)
        {
            const float d2 = std::abs (x[n] - 2.0f * x[n - 1] + x[n - 2]);
            r.worst = juce::jmax (r.worst, d2);
            if (d2 > threshold && n - lastClick > 2000)   // count separate events, not every sample of one
            {
                ++r.clicks;
                if (r.firstAt < 0) r.firstAt = n;
                lastClick = n;
            }
        }
    }
    return r;
}
