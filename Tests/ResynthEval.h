#pragma once
// Resynthesis accuracy check: renders test sources through the resynth oscillator at their original pitch
// and compares them with the originals (multi-resolution log-spectral distance), writing both as WAV files.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

// The Master setting that plays at the engine's own level, the old Master 0 dB (the output sits 6 dB above the knob):
// tests comparing levels against a source, or using absolute thresholds, set this.
constexpr float unityMasterDb = -6.0206f;

namespace ResynthEval
{
constexpr double sr = 44100.0;

struct Source { juce::String name; juce::AudioBuffer<float> audio; };

inline juce::AudioBuffer<float> mono (int len) { juce::AudioBuffer<float> b (2, len); b.clear(); return b; }

inline std::vector<Source> generatedSources()
{
    std::vector<Source> v;
    juce::Random r (7);
    const float twoPi = juce::MathConstants<float>::twoPi;

    {   // steady sawtooth (checks that the waveform shape, i.e. the partials' phases, survives)
        auto b = mono ((int) (sr * 2.0));
        double ph = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            ph += 110.0 / sr;
            ph -= std::floor (ph);
            b.setSample (0, i, 0.4f * (float) (2.0 * ph - 1.0));
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "saw", std::move (b) });
    }
    {   // pure 55 Hz sine (level and steadiness of a single low partial)
        auto b = mono ((int) (sr * 2.0));
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.3f * std::sin (twoPi * 55.0f * (float) i / (float) sr));
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "sine55", std::move (b) });
    }
    {   // white noise (calibrates the noise layer)
        auto b = mono ((int) (sr * 2.0));
        for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, 0.1f * (r.nextFloat() * 2.0f - 1.0f)); b.setSample (1, i, 0.1f * (r.nextFloat() * 2.0f - 1.0f)); }
        v.push_back ({ "noise", std::move (b) });
    }
    {   // plucked string (Karplus-Strong), three notes
        auto b = mono ((int) (sr * 3.0));
        for (int n = 0; n < 3; ++n)
        {
            const float hz = n == 1 ? 146.83f : 110.0f;
            const int period = (int) std::lround (sr / hz);
            std::vector<float> line ((size_t) period);
            for (auto& x : line) x = r.nextFloat() * 2.0f - 1.0f;
            const int start = (int) (n * sr), len = (int) sr;
            for (int i = 0; i < len; ++i)
            {
                const size_t k = (size_t) (i % period), k1 = (size_t) ((i + 1) % period);
                const float y = line[k];
                line[k] = 0.996f * 0.5f * (line[k] + line[k1]);
                b.setSample (0, start + i, 0.4f * y);
            }
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "pluck", std::move (b) });
    }
    {   // voice-like: saw with vibrato through three formants, plus breath
        auto b = mono ((int) (sr * 3.0));
        double ph = 0.0;
        juce::IIRFilter f1, f2, f3, nf;
        f1.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 700.0, 5.0));
        f2.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 1200.0, 6.0));
        f3.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 2600.0, 8.0));
        nf.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 2000.0));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) sr;
            const float hz = 220.0f * (1.0f + 0.015f * std::sin (twoPi * 5.5f * t)) * (t > 1.5f ? 1.122f : 1.0f);
            ph += hz / sr;
            ph -= std::floor (ph);
            const float saw = (float) (2.0 * ph - 1.0);
            const float env = juce::jmin (1.0f, t * 10.0f) * juce::jmin (1.0f, (3.0f - t) * 5.0f);
            const float y = (f1.processSingleSampleRaw (saw) + 0.6f * f2.processSingleSampleRaw (saw) + 0.4f * f3.processSingleSampleRaw (saw))
                            + 0.05f * nf.processSingleSampleRaw (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, 0.5f * env * y);
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "voice", std::move (b) });
    }
    {   // drum loop at 128 BPM: kick, snare, closed hats
        auto b = mono ((int) (sr * 60.0 / 128.0 * 8));
        juce::IIRFilter hp;
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));
        const int eighth = (int) (sr * 60.0 / 128.0 / 2.0);
        for (int e = 0; e < 16; ++e)
        {
            const int s0 = e * eighth;
            for (int i = 0; i < eighth && s0 + i < b.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) sr;
                float y = 0.0f;
                if (e % 2 == 0)   // kick on the beats
                {
                    const float f = 45.0f + 80.0f * std::exp (-t * 30.0f);
                    y += 0.8f * std::sin (twoPi * (45.0f * t + 80.0f / 30.0f * (1.0f - std::exp (-t * 30.0f)))) * std::exp (-t * 8.0f);
                    juce::ignoreUnused (f);
                }
                if (e % 4 == 2)   // snare on 2 and 4
                    y += (0.4f * (r.nextFloat() * 2.0f - 1.0f) + 0.3f * std::sin (twoPi * 190.0f * t)) * std::exp (-t * 18.0f);
                y += 0.25f * hp.processSingleSampleRaw (r.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 60.0f);   // hat on every 8th
                b.addSample (0, s0 + i, y);
            }
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "drums", std::move (b) });
    }
    {   // detuned saw chord (dense beating partials)
        auto b = mono ((int) (sr * 3.0));
        const float notes[] { 130.81f, 164.81f, 196.0f };
        double ph[6] {};
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float y = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                const double hz = notes[k / 2] * (k % 2 ? 1.0046 : 0.9954);
                ph[k] += hz / sr;
                ph[k] -= std::floor (ph[k]);
                y += (float) (2.0 * ph[k] - 1.0);
            }
            b.setSample (0, i, 0.08f * y);
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "chord", std::move (b) });
    }
    {   // fast bass line with a filter envelope
        auto b = mono ((int) (sr * 3.0));
        const float notes[] { 55.0f, 55.0f, 82.41f, 55.0f, 110.0f, 55.0f, 73.42f, 65.41f };
        const int step = (int) (sr * 60.0 / 128.0 / 4.0);
        double ph = 0.0;
        float lp = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int n = (i / step) % 8;
            const float t = (float) (i % step) / (float) sr;
            ph += notes[n] / sr;
            ph -= std::floor (ph);
            const float cut = 200.0f + 3000.0f * std::exp (-t * 25.0f);
            const float a = 1.0f - std::exp (-twoPi * cut / (float) sr);
            lp += a * ((float) (2.0 * ph - 1.0) - lp);
            b.setSample (0, i, 0.5f * lp * std::exp (-t * 6.0f));
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        v.push_back ({ "bass", std::move (b) });
    }
    return v;
}

inline std::vector<Source> systemSources()
{
    std::vector<Source> v;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (auto name : { "Glass", "Funk", "Basso", "Blow", "Hero", "Purr", "Submarine", "Pop" })
    {
        juce::File f ("/System/Library/Sounds/" + juce::String (name) + ".aiff");
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) continue;
        juce::AudioBuffer<float> raw (2, (int) rd->lengthInSamples);
        rd->read (&raw, 0, raw.getNumSamples(), 0, true, rd->numChannels > 1);
        // resample to the test rate
        const double ratio = rd->sampleRate / sr;
        juce::AudioBuffer<float> b (2, (int) (raw.getNumSamples() / ratio) - 8);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::WindowedSincInterpolator interp;
            interp.process (ratio, raw.getReadPointer (ch), b.getWritePointer (ch), b.getNumSamples());
        }
        if (b.getNumSamples() < 4096) continue;
        v.push_back ({ juce::String (name).toLowerCase(), std::move (b) });
    }
    return v;
}

// Mean absolute difference of band levels (dB, 48 log-spaced bands 40 Hz..16 kHz) over three STFT sizes.
// Levels are floored 80 dB below the original's loudest band so silence compares as equal.
// channel 0/1 = left/right (each compared on its own, so stereo width and level per side count)
inline float spectralDistanceChannel (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                                      std::initializer_list<int> orders, int channel)
{
    const int len = juce::jmin (a.getNumSamples(), b.getNumSamples());
    auto chanOf = [len, channel] (const juce::AudioBuffer<float>& x)
    {
        std::vector<float> m ((size_t) len);
        for (int i = 0; i < len; ++i) m[(size_t) i] = x.getSample (channel, i);
        return m;
    };
    const auto ma = chanOf (a), mb = chanOf (b);
    static constexpr int numBands = 48;   // static: MSVC needs no lambda capture for it
    double total = 0.0;
    int resolutions = 0;
    for (int order : orders)
    {
        const int N = 1 << order, hop = N / 4;
        juce::dsp::FFT fft (order);
        std::vector<float> w ((size_t) N), xa ((size_t) N * 2), xb ((size_t) N * 2);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<int> bandOf ((size_t) N / 2, -1);
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * sr / N;
            if (hz < 40.0 || hz >= 16000.0) continue;
            bandOf[(size_t) k] = juce::jlimit (0, numBands - 1, (int) (std::log (hz / 40.0) / std::log (400.0) * numBands));
        }
        std::vector<std::array<float, numBands>> la, lb;
        for (int s = 0; s + N <= len; s += hop)
        {
            for (int i = 0; i < N; ++i) { xa[(size_t) i] = ma[(size_t) (s + i)] * w[(size_t) i]; xb[(size_t) i] = mb[(size_t) (s + i)] * w[(size_t) i]; }
            std::fill (xa.begin() + N, xa.end(), 0.0f);
            std::fill (xb.begin() + N, xb.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (xa.data());
            fft.performFrequencyOnlyForwardTransform (xb.data());
            std::array<float, numBands> ea {}, eb {};
            for (int k = 1; k < N / 2; ++k)
                if (const int bnd = bandOf[(size_t) k]; bnd >= 0)
                {
                    ea[(size_t) bnd] += xa[(size_t) k] * xa[(size_t) k];
                    eb[(size_t) bnd] += xb[(size_t) k] * xb[(size_t) k];
                }
            la.push_back (ea);
            lb.push_back (eb);
        }
        float peak = 1.0e-20f;
        for (auto& f : la) for (auto e : f) peak = std::max (peak, e);
        const float floorDb = 10.0f * std::log10 (peak) - 80.0f;
        double sum = 0.0;
        int count = 0;
        std::vector<bool> used (numBands, false);
        for (int k = 1; k < N / 2; ++k) if (bandOf[(size_t) k] >= 0) used[(size_t) bandOf[(size_t) k]] = true;
        for (size_t f = 0; f < la.size(); ++f)
            for (int bnd = 0; bnd < numBands; ++bnd)
            {
                if (! used[(size_t) bnd]) continue;
                const float da = std::max (floorDb, 10.0f * std::log10 (la[f][(size_t) bnd] + 1.0e-30f));
                const float db = std::max (floorDb, 10.0f * std::log10 (lb[f][(size_t) bnd] + 1.0e-30f));
                if (da <= floorDb && db <= floorDb) continue;
                sum += std::abs (da - db);
                ++count;
            }
        if (count > 0) { total += sum / count; ++resolutions; }
    }
    return resolutions > 0 ? (float) (total / resolutions) : 0.0f;
}

// "Tonalized noise": noise rebuilt from many short sines sounds bubbly/watery. Per 1024-sample frame and band
// (16 log bands 200 Hz..16 kHz), spectral flatness (geometric / arithmetic mean of the bin powers, 0 = pure tone,
// ~0.56 = white noise) of the original vs the resynth; mean absolute difference, weighted by band energy.
inline float flatnessErrorChannel (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel)
{
    const int len = juce::jmin (a.getNumSamples(), b.getNumSamples());
    const int order = 10, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N), xa ((size_t) N * 2), xb ((size_t) N * 2);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    static constexpr int numBands = 16;   // static: MSVC needs no lambda capture for it
    std::vector<int> bandOf ((size_t) N / 2, -1);
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * sr / N;
        if (hz >= 200.0 && hz < 16000.0) bandOf[(size_t) k] = juce::jlimit (0, numBands - 1, (int) (std::log (hz / 200.0) / std::log (80.0) * numBands));
    }
    double num = 0.0, den = 0.0;
    for (int s = 0; s + N <= len; s += N / 2)
    {
        for (int i = 0; i < N; ++i)
        {
            xa[(size_t) i] = a.getSample (channel, s + i) * w[(size_t) i];
            xb[(size_t) i] = b.getSample (channel, s + i) * w[(size_t) i];
        }
        std::fill (xa.begin() + N, xa.end(), 0.0f);
        std::fill (xb.begin() + N, xb.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (xa.data());
        fft.performFrequencyOnlyForwardTransform (xb.data());
        for (int bnd = 0; bnd < numBands; ++bnd)
        {
            double la = 0, sa = 0, lb = 0, sb = 0;
            int n = 0;
            for (int k = 1; k < N / 2; ++k)
                if (bandOf[(size_t) k] == bnd)
                {
                    const double pa = (double) xa[(size_t) k] * xa[(size_t) k] + 1e-20, pb = (double) xb[(size_t) k] * xb[(size_t) k] + 1e-20;
                    la += std::log (pa); sa += pa; lb += std::log (pb); sb += pb; ++n;
                }
            if (n < 4) continue;
            const double fa = std::exp (la / n) / (sa / n), fb = std::exp (lb / n) / (sb / n);
            num += sa * std::abs (fa - fb);
            den += sa;
        }
    }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

inline float spectralDistance (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                               std::initializer_list<int> orders = { 8, 10, 12 })
{
    return 0.5f * (spectralDistanceChannel (a, b, orders, 0) + spectralDistanceChannel (a, b, orders, 1));
}

inline float flatnessError (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    return 0.5f * (flatnessErrorChannel (a, b, 0) + flatnessErrorChannel (a, b, 1));
}

// Stereo width: per 2048-sample frame and band (12 log bands 100 Hz..16 kHz), the coherence of L and R
// (1 = identical up to level, 0 = unrelated). Mean absolute difference original vs resynth, weighted by energy.
inline float widthError (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int len = juce::jmin (a.getNumSamples(), b.getNumSamples());
    const int order = 11, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    static constexpr int numBands = 12;   // static: MSVC needs no lambda capture for it
    std::vector<int> bandOf ((size_t) N / 2, -1);
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * sr / N;
        if (hz >= 100.0 && hz < 16000.0) bandOf[(size_t) k] = juce::jlimit (0, numBands - 1, (int) (std::log (hz / 100.0) / std::log (160.0) * numBands));
    }
    auto coherence = [&] (const juce::AudioBuffer<float>& x, int s, std::array<double, numBands>& coh, std::array<double, numBands>& energy)
    {
        std::vector<float> l ((size_t) N * 2, 0.0f), r ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i) { l[(size_t) i] = x.getSample (0, s + i) * w[(size_t) i]; r[(size_t) i] = x.getSample (1, s + i) * w[(size_t) i]; }
        fft.performRealOnlyForwardTransform (l.data(), true);
        fft.performRealOnlyForwardTransform (r.data(), true);
        std::array<double, numBands> cr {}, ci {}, el {}, er {};
        for (int k = 1; k < N / 2; ++k)
            if (const int bnd = bandOf[(size_t) k]; bnd >= 0)
            {
                const double lr = l[(size_t) 2 * k], li = l[(size_t) 2 * k + 1], rr = r[(size_t) 2 * k], ri = r[(size_t) 2 * k + 1];
                cr[(size_t) bnd] += lr * rr + li * ri;
                ci[(size_t) bnd] += li * rr - lr * ri;
                el[(size_t) bnd] += lr * lr + li * li;
                er[(size_t) bnd] += rr * rr + ri * ri;
            }
        for (int bnd = 0; bnd < numBands; ++bnd)
        {
            energy[(size_t) bnd] = el[(size_t) bnd] + er[(size_t) bnd];
            coh[(size_t) bnd] = energy[(size_t) bnd] > 1e-12 ? std::hypot (cr[(size_t) bnd], ci[(size_t) bnd]) / std::sqrt (el[(size_t) bnd] * er[(size_t) bnd] + 1e-30) : 1.0;
        }
    };
    double num = 0.0, den = 0.0;
    for (int s = 0; s + N <= len; s += N / 2)
    {
        std::array<double, numBands> ca {}, ea {}, cb {}, eb {};
        coherence (a, s, ca, ea);
        coherence (b, s, cb, eb);
        for (int bnd = 0; bnd < numBands; ++bnd) { num += ea[(size_t) bnd] * std::abs (ca[(size_t) bnd] - cb[(size_t) bnd]); den += ea[(size_t) bnd]; }
    }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

// Waveform similarity (normalised correlation of the mid signals, 0.25 s .. end), 1 = identical waveform
inline float waveCorrelation (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int len = juce::jmin (a.getNumSamples(), b.getNumSamples());
    double ab = 0, aa = 0, bb = 0;
    for (int i = (int) (0.25 * sr); i < len; ++i)
    {
        const double x = a.getSample (0, i) + a.getSample (1, i), y = b.getSample (0, i) + b.getSample (1, i);
        ab += x * y; aa += x * x; bb += y * y;
    }
    return aa > 0 && bb > 0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
}

// Frame-to-frame pitch jitter of the partial tracks: loudness-weighted mean |second difference| in cents.
// Smooth glides and vibrato score low, wobbly ("squiggly") tracks high.
inline float trackWobble (const ResynthData& d, bool quietOnly = false)
{
    const int S = d.numSlots, F = d.numFrames;
    double num = 0.0, den = 0.0;
    for (int f = 1; f + 1 < F; ++f)
    {
        float frameMax = 0.0f;
        for (int s = 0; s < S; ++s) frameMax = std::max (frameMax, d.amp[(size_t) f * S + (size_t) s]);
        for (int s = 0; s < S; ++s)
        {
            const size_t i = (size_t) f * S + (size_t) s;
            if (d.amp[i] <= 0.0f || d.amp[i - (size_t) S] <= 0.0f || d.amp[i + (size_t) S] <= 0.0f) continue;
            if (quietOnly && d.amp[i] > frameMax * 0.0316f) continue;   // quiet partials: more than 30 dB down, counted equally
            const double c0 = std::log2 (d.freq[i - (size_t) S]), c1 = std::log2 (d.freq[i]), c2 = std::log2 (d.freq[i + (size_t) S]);
            const double w = quietOnly ? 1.0 : d.amp[i];
            num += w * std::abs (c2 - 2.0 * c1 + c0) * 1200.0;
            den += w;
        }
    }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

// Frame-to-frame level jitter of the partial tracks: loudness-weighted mean |second difference| in dB.
inline float ampFlutter (const ResynthData& d)
{
    const int S = d.numSlots, F = d.numFrames;
    double num = 0.0, den = 0.0;
    for (int f = 1; f + 1 < F; ++f)
        for (int s = 0; s < S; ++s)
        {
            const size_t i = (size_t) f * S + (size_t) s;
            if (d.amp[i] <= 0.0f || d.amp[i - (size_t) S] <= 0.0f || d.amp[i + (size_t) S] <= 0.0f) continue;
            const double a0 = 20 * std::log10 (d.amp[i - (size_t) S]), a1 = 20 * std::log10 (d.amp[i]), a2 = 20 * std::log10 (d.amp[i + (size_t) S]);
            num += d.amp[i] * std::abs (a2 - 2.0 * a1 + a0);
            den += d.amp[i];
        }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

inline juce::AudioBuffer<float> resynthesize (const juce::AudioBuffer<float>& src, double& renderCpu, double& analysisMs,
                                              float* wobble = nullptr, float* quietWobble = nullptr, float* flutter = nullptr)
{
    const int block = 512;
    auto qp = std::make_unique<ElukkaAudioProcessor>();   // large: keep it off the stack
    auto& q = *qp;
    q.setPlayConfigDetails (0, 2, sr, block);
    auto setP = [&q] (const char* id, float v) { auto* p = q.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    setP ("o1_wave", (float) P::WResynth); setP ("o1_level", 1.0f); setP ("master", unityMasterDb);
    setP ("rs_sync", 0.0f); setP ("rs_loop", 0.0f); setP ("ampA", 0.001f); setP ("ampS", 1.0f); setP ("ampR", 0.01f);
    if (const char* tune = std::getenv ("ELUKKA_EVAL_TUNE")) setP ("rs_tune", (float) std::atof (tune));   // listen to the tuned result (key C4)
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto data = ResynthData::fromAudio (src, sr, "eval");
    // diagnostics: render only the partials or only the residual
    if (std::getenv ("ELUKKA_NO_NOISE") != nullptr) { data->noiseL.clear(); data->noiseR.clear(); data->noiseCoh.clear(); }
    if (std::getenv ("ELUKKA_NO_PARTIALS") != nullptr) std::fill (data->amp.begin(), data->amp.end(), 0.0f);
    if (wobble != nullptr) *wobble = trackWobble (*data);
    if (quietWobble != nullptr) *quietWobble = trackWobble (*data, true);
    if (flutter != nullptr) *flutter = ampFlutter (*data);
    q.setResynth (data);
    analysisMs = juce::Time::getMillisecondCounterHiRes() - t0;
    q.prepareToPlay (sr, block);

    juce::AudioBuffer<float> out (2, src.getNumSamples()), buf (2, block);
    out.clear();
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        q.processBlock (buf, midi);
        const int n = juce::jmin (block, out.getNumSamples() - pos);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    renderCpu = (juce::Time::getMillisecondCounterHiRes() - t1) / (out.getNumSamples() / sr * 1000.0);
    return out;
}

// --resynth-debug: per-harmonic tracking detail on a sawtooth, and how the offline partial model compares with
// the real-time render (same data, so a difference points at the player, not the analysis).
// --resynth-tracks <name> [untilSec]: lists the partial tracks of a generated source (slot, frames, frequency range, peak level)
inline int debugTracks (const juce::String& name, double untilSec)
{
    juce::AudioBuffer<float> src;
    for (auto& s : generatedSources()) if (s.name == name) src = s.audio;
    if (src.getNumSamples() == 0) { std::cout << "unknown source\n"; return 1; }
    auto d = ResynthData::fromAudio (src, sr, name);
    const int S = d->numSlots, F = juce::jmin (d->numFrames, (int) (untilSec * d->frameRate));
    struct Tr { int slot, start, end; float fLo, fHi, peak; };
    std::vector<Tr> tracks;
    for (int s = 0; s < S; ++s)
    {
        int start = -1; float lo = 1e9f, hi = 0.0f, pk = 0.0f;
        for (int f = 0; f <= F; ++f)
        {
            const bool on = f < F && d->amp[(size_t) f * S + (size_t) s] > 0.0f;
            if (on) { const size_t i = (size_t) f * S + (size_t) s; if (start < 0) start = f; lo = std::min (lo, d->freq[i]); hi = std::max (hi, d->freq[i]); pk = std::max (pk, d->amp[i]); }
            else if (start >= 0) { tracks.push_back ({ s, start, f - 1, lo, hi, pk }); start = -1; lo = 1e9f; hi = 0.0f; pk = 0.0f; }
        }
    }
    std::sort (tracks.begin(), tracks.end(), [] (const Tr& a, const Tr& b) { return a.start != b.start ? a.start < b.start : a.fLo < b.fLo; });
    std::cout << tracks.size() << " tracks in the first " << F << " frames (" << juce::String (F / d->frameRate, 2) << " s), frame = " << juce::String (1000.0 / d->frameRate, 1) << " ms\n";
    for (auto& t : tracks)
        if (t.peak > d->maxAmp * 0.01f)
            std::cout << "  f" << juce::String (t.start).paddedRight (' ', 4) << "-" << juce::String (t.end).paddedRight (' ', 4) << " slot " << juce::String (t.slot).paddedRight (' ', 4)
                      << juce::String (t.fLo, 1).paddedLeft (' ', 8) << " .. " << juce::String (t.fHi, 1).paddedRight (' ', 8) << " Hz  peak " << juce::String (juce::Decibels::gainToDecibels (t.peak / d->maxAmp), 1) << " dB\n";
    return 0;
}

inline int debugSaw (const juce::String& name = "saw")
{
    juce::AudioBuffer<float> saw;
    for (auto& s : generatedSources()) if (s.name == name) saw = s.audio;
    for (auto& s : systemSources()) if (s.name == name) saw = s.audio;
    if (saw.getNumSamples() == 0) { std::cout << "unknown source\n"; return 1; }
    auto d = ResynthData::fromAudio (saw, sr, name);
    const int S = d->numSlots;
    const double dt = 1.0 / d->frameRate;
    std::cout << "frames " << d->numFrames << " hop " << (int) std::lround (sr / d->frameRate) << "\n";
    // harmonics of the lowest strong partial at frame 40
    float base = 1.0e9f;
    for (int s = 0; s < S; ++s)
        if (d->amp[(size_t) 40 * S + s] > d->maxAmp * 0.1f) base = std::min (base, d->freq[(size_t) 40 * S + s]);
    for (int h = 1; h <= 6; ++h)
    {
        int slot = -1;
        for (int s = 0; s < S; ++s)
            if (d->amp[(size_t) 40 * S + s] > 0.0f && std::abs (d->freq[(size_t) 40 * S + s] - base * (float) h) < 5.0f) slot = s;
        if (slot < 0) { std::cout << "h" << h << " not found\n"; continue; }
        std::cout << "h" << h << " slot " << slot << ":";
        for (int f : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111 })
        {
            const size_t i = (size_t) f * S + (size_t) slot;
            // phase predicted from the previous frame vs measured
            float pred = f > 0 ? d->phase[i - S] + d->freq[i - S] * (float) dt : d->phase[i];
            pred -= std::floor (pred);
            float err = d->phase[i] - pred; err -= std::round (err);
            std::cout << "  [f" << f << " a=" << juce::String (d->amp[i], 4) << " F=" << juce::String (d->freq[i], 2) << " ph=" << juce::String (d->phase[i], 3)
                      << " err=" << juce::String (err, 3) << "]";
        }
        std::cout << "\n";
    }
    std::vector<float> mL, mR;
    d->renderPartials (mL, mR);
    juce::AudioBuffer<float> model (2, (int) mL.size());
    for (int i = 0; i < model.getNumSamples(); ++i) { model.setSample (0, i, mL[(size_t) i]); model.setSample (1, i, mR[(size_t) i]); }
    double cpu = 0.0, ms = 0.0;
    auto rt = resynthesize (saw, cpu, ms);
    std::cout << "waveform match: offline model " << juce::String (waveCorrelation (saw, model), 3) << ", real-time " << juce::String (waveCorrelation (saw, rt), 3)
              << "; RMS source " << juce::String (saw.getRMSLevel (0, 0, saw.getNumSamples()), 4) << " model " << juce::String (model.getRMSLevel (0, 0, model.getNumSamples()), 4)
              << " real-time " << juce::String (rt.getRMSLevel (0, 0, rt.getNumSamples()), 4) << "\n";
    // implied residual RMS per frame (sqrt of sum over bands of density^2 * bandwidth) vs the source and real-time RMS around the frame
    std::cout << "first samples of the real-time render:";
    for (int i = 0; i < 3000; i += 150) std::cout << " " << juce::String (rt.getSample (0, i), 3);
    std::cout << "\n  max |x| in 128-sample blocks:";
    for (int b = 0; b < 24; ++b) std::cout << " " << juce::String (rt.getMagnitude (0, b * 128, 128), 2);
    std::cout << "\n";
    std::cout << "frame: implied residual RMS / source RMS / real-time RMS (23 ms around the frame centre)\n";
    std::cout << "RMS per 50 ms window from 0.5 s (source / real-time):";
    for (int st = (int) (sr * 0.5); st + (int) (sr * 0.05) <= rt.getNumSamples(); st += (int) (sr * 0.05))
        std::cout << " " << juce::String (saw.getRMSLevel (0, st, (int) (sr * 0.05)), 3) << "/" << juce::String (rt.getRMSLevel (0, st, (int) (sr * 0.05)), 3);
    std::cout << "\n  (model = partials alone, offline)\n";
    for (int f : { 0, 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256 })
    {
        if (f >= d->numFrames) break;
        double var = 0.0;
        for (int b = 0; b < ResynthData::numNoiseBands; ++b)
        {
            const float lo = ResynthData::noiseBandLoHz * std::exp2 ((float) b / ResynthData::noiseBandsPerOctave), hi = lo * std::exp2 (1.0f / ResynthData::noiseBandsPerOctave);
            const float n = d->noiseL[(size_t) f * ResynthData::numNoiseBands + (size_t) b];
            var += (double) n * n * (hi - lo);
        }
        const int c = (int) (f / d->frameRate * sr), half = 512;
        const int start = juce::jmax (0, c - half);
        std::cout << "  f" << f << ": " << juce::String (std::sqrt (var), 4) << " / " << juce::String (saw.getRMSLevel (0, start, 2 * half), 4)
                  << " / " << juce::String (rt.getRMSLevel (0, start, 2 * half), 4) << "   model " << juce::String (model.getRMSLevel (0, start, 2 * half), 4) << "\n";
    }
    return 0;
}

// --resynth-compare <orig.wav> <resynth.wav> [segmentSec]: per segment, the level (dB) of six bands in the original
// and the resynthesis side by side, to see where (in time and frequency) a comparison goes wrong.
inline juce::AudioBuffer<float> loadFile (const juce::File& f, double maxSeconds);
inline int compareFiles (const juce::File& a, const juce::File& b, double segSec)
{
    auto x = loadFile (a, 60.0), y = loadFile (b, 60.0);
    if (x.getNumSamples() == 0 || y.getNumSamples() == 0) { std::cout << "could not read the files\n"; return 1; }
    const int len = juce::jmin (x.getNumSamples(), y.getNumSamples());
    const int order = 11, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N), bx ((size_t) N * 2), by ((size_t) N * 2);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    const float edges[7] { 30.0f, 120.0f, 350.0f, 1000.0f, 3000.0f, 8000.0f, 16000.0f };
    const int seg = (int) (sr * segSec);
    std::cout << "time      band:  30-120    120-350   350-1k    1k-3k     3k-8k     8k-16k   (orig / resynth dB)\n";
    for (int s0 = 0; s0 + N <= len; s0 += seg)
    {
        double ex[6] {}, ey[6] {};
        for (int s = s0; s + N <= juce::jmin (len, s0 + seg); s += N / 2)
        {
            for (int i = 0; i < N; ++i)
            {
                bx[(size_t) i] = 0.5f * (x.getSample (0, s + i) + x.getSample (1, s + i)) * w[(size_t) i];
                by[(size_t) i] = 0.5f * (y.getSample (0, s + i) + y.getSample (1, s + i)) * w[(size_t) i];
            }
            std::fill (bx.begin() + N, bx.end(), 0.0f);
            std::fill (by.begin() + N, by.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (bx.data());
            fft.performFrequencyOnlyForwardTransform (by.data());
            for (int k = 1; k < N / 2; ++k)
            {
                const float hz = (float) k * (float) sr / N;
                for (int bnd = 0; bnd < 6; ++bnd)
                    if (hz >= edges[bnd] && hz < edges[bnd + 1]) { ex[bnd] += bx[(size_t) k] * bx[(size_t) k]; ey[bnd] += by[(size_t) k] * by[(size_t) k]; }
            }
        }
        std::cout << juce::String (s0 / sr, 2).paddedLeft (' ', 6) << " s       ";
        for (int bnd = 0; bnd < 6; ++bnd)
            std::cout << juce::String (10.0 * std::log10 (ex[bnd] + 1e-12), 0).paddedLeft (' ', 4) << "/" << juce::String (10.0 * std::log10 (ey[bnd] + 1e-12), 0).paddedRight (' ', 5);
        std::cout << "\n";
    }
    return 0;
}

inline void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, int bits = 24)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, (unsigned int) bits, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

inline int run (const juce::File& outDir)
{
    outDir.createDirectory();
    auto sources = generatedSources();
    for (auto& s : systemSources()) sources.push_back (std::move (s));
    double sum = 0.0, flatSum = 0.0;
    for (auto& s : sources)
    {
        double cpu = 0.0, ms = 0.0;
        auto out = resynthesize (s.audio, cpu, ms);
        const float d = spectralDistance (s.audio, out);
        sum += d;
        flatSum += flatnessError (s.audio, out);
        writeWav (outDir.getChildFile (s.name + "_original.wav"), s.audio);
        writeWav (outDir.getChildFile (s.name + "_resynth.wav"), out);
        std::cout << juce::String (s.name).paddedRight (' ', 11) << " distance " << juce::String (d, 2).paddedLeft (' ', 6) << " dB"
                  << "   waveform match " << juce::String (waveCorrelation (s.audio, out), 2).paddedLeft (' ', 5)
                  << "   noise->tones " << juce::String (flatnessError (s.audio, out), 3).paddedLeft (' ', 6)
                  << "   render " << juce::String (cpu * 100.0, 1).paddedLeft (' ', 5) << "% of real time"
                  << "   analysis " << (int) ms << " ms for " << juce::String (s.audio.getNumSamples() / sr, 1) << " s\n";
    }
    std::cout << "average distance " << juce::String (sum / (double) sources.size(), 2) << " dB, noise->tones "
              << juce::String (flatSum / (double) sources.size(), 3) << "\n";
    return 0;
}
// Loads an audio file as stereo at the test rate (resampled if needed), at most `maxSeconds` long.
inline juce::AudioBuffer<float> loadFile (const juce::File& f, double maxSeconds)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return {};
    const int n = (int) juce::jmin ((juce::int64) (rd->sampleRate * maxSeconds), rd->lengthInSamples);
    juce::AudioBuffer<float> raw (2, n);
    rd->read (&raw, 0, n, 0, true, rd->numChannels > 1);
    if (std::abs (rd->sampleRate - sr) < 1.0) return raw;
    const double ratio = rd->sampleRate / sr;
    juce::AudioBuffer<float> b (2, (int) (n / ratio) - 8);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, raw.getReadPointer (ch), b.getWritePointer (ch), b.getNumSamples());
    }
    return b;
}

// --resynth-dir <samples> [outDir]: every audio file in a folder (first 20 s). Ambient material counts double in
// the average. "fine" is the distance at 8192-point resolution, where wobbly ("squiggly") partials smear energy
// around each partial.
inline int runFolder (const juce::File& dir, const juce::File& outDir)
{
    const juce::StringArray ambient { "Aqua", "Brain Fog", "Lighter", "Darkness", "Chords Pitch", "Groove Melodic", "bell" };
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    files.sort();
    if (outDir != juce::File()) outDir.createDirectory();
    double wsum = 0, dsum = 0, fsum = 0, tsum = 0, worstCpu = 0, wobSum = 0, qwobSum = 0, flutSum = 0, widSum = 0;
    for (auto& f : files)
    {
        auto src = loadFile (f, 20.0);
        if (src.getNumSamples() < 4096) continue;
        bool amb = false;
        for (auto& a : ambient) amb |= f.getFileName().contains (a);
        double cpu = 0.0, ms = 0.0;
        float wob = 0.0f, qwob = 0.0f, flut = 0.0f;
        auto out = resynthesize (src, cpu, ms, &wob, &qwob, &flut);
        const float d = spectralDistance (src, out), fine = spectralDistance (src, out, { 13 }), fl = flatnessError (src, out), wid = widthError (src, out);
        const double w = amb ? 2.0 : 1.0;
        wsum += w; dsum += w * d; fsum += w * fine; tsum += w * fl; wobSum += w * wob; qwobSum += w * qwob; flutSum += w * flut; widSum += w * wid;
        worstCpu = std::max (worstCpu, cpu);
        std::cout << (amb ? "A " : "  ") << f.getFileNameWithoutExtension().substring (0, 34).paddedRight (' ', 35)
                  << " dist " << juce::String (d, 2).paddedLeft (' ', 5) << "  fine " << juce::String (fine, 2).paddedLeft (' ', 5)
                  << "  noise->tones " << juce::String (fl, 3) << "  wobble " << juce::String (wob, 1) << "c (quiet " << juce::String (qwob, 0) << "c)  flutter " << juce::String (flut, 2) << " dB  width " << juce::String (wid, 3) << "  render " << juce::String (cpu * 100.0, 1) << "%  analysis "
                  << (int) ms << " ms\n";
        if (outDir != juce::File())
            writeWav (outDir.getChildFile (f.getFileNameWithoutExtension() + (std::getenv ("ELUKKA_EVAL_TUNE") ? " (resynth tune " + juce::String (std::getenv ("ELUKKA_EVAL_TUNE")) + ")" : " (resynth)") + ".wav"), out, 16);
    }
    std::cout << "weighted average: dist " << juce::String (dsum / wsum, 3) << "  fine " << juce::String (fsum / wsum, 3)
              << "  noise->tones " << juce::String (tsum / wsum, 4) << "  wobble " << juce::String (wobSum / wsum, 2) << "c quiet " << juce::String (qwobSum / wsum, 1) << "c  flutter " << juce::String (flutSum / wsum, 2) << " dB  width " << juce::String (widSum / wsum, 3) << "  worst render " << juce::String (worstCpu * 100.0, 1) << "%\n";
    return 0;
}
// --resynth-pitch <samples>: what the note analysis reports for every file in a folder (main note, note events)
inline juce::String noteName (float hz)
{
    if (hz <= 0.0f) return "-";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int n = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - (float) n) * 100.0f);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1) + (cents >= 0 ? "+" : "") + juce::String (cents) + "c";
}

// How often a sounding partial's note tag changes from one frame to the next (per second of source). On a partial
// whose own pitch stays put (`steady`) the change moves its Tune correction alone and is heard as a pitch wiggle;
// with a pitch jump of over a semitone it is a new note on a continuing track, which the correction steps with.
inline float tagFlipsPerSec (const ResynthData& d, float* steady = nullptr)
{
    const int S = d.numSlots;
    int flips = 0, steadyFlips = 0;
    for (int f = 1; f < d.numFrames; ++f)
        for (int s = 0; s < S; ++s)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            if (d.amp[i] > 0.0f && d.amp[i - (size_t) S] > 0.0f && std::abs (d.noteMidi[i] - d.noteMidi[i - (size_t) S]) > 0.01f)
            {
                ++flips;
                if (std::abs (d.freq[i] - d.freq[i - (size_t) S]) <= 0.0595f * d.freq[i - (size_t) S]) ++steadyFlips;
            }
        }
    if (steady != nullptr) *steady = d.durationSec > 0.0 ? (float) (steadyFlips / d.durationSec) : 0.0f;
    return d.durationSec > 0.0 ? (float) (flips / d.durationSec) : 0.0f;
}

// How much Tune 100 % (key C4) moves the pitch of sounding partials whose own pitch stays put, in cents per second,
// weighted by level: every change of a partial's note tag moves its correction by the difference of the two notes'
// corrections, and that is heard as a wiggle or swoop on a steady tone. Pads and chords are where this shows.
inline float tuneSwoopPerSec (const ResynthData& d)
{
    const int S = d.numSlots, F = d.numFrames;
    auto corrOf = [] (float m) { return m > 0.0f ? ResynthData::tuneTarget (m, 60.0f) - m : 0.0f; };
    double swoop = 0.0, ampSum = 0.0;
    for (int f = 1; f < F; ++f)
        for (int s = 0; s < S; ++s)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            ampSum += d.amp[i];
            if (d.amp[i] <= 0.0f || d.amp[i - (size_t) S] <= 0.0f || std::abs (d.freq[i] - d.freq[i - (size_t) S]) > 0.0595f * d.freq[i - (size_t) S]) continue;
            swoop += d.amp[i] * 100.0 * std::abs (corrOf (d.noteMidi[i]) - corrOf (d.noteMidi[i - (size_t) S]));
        }
    return ampSum > 0.0 && d.durationSec > 0.0 ? (float) (swoop / (ampSum / F) / d.durationSec) : 0.0f;
}

inline int runPitchFolder (const juce::File& dir)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    files.sort();
    for (auto& f : files)
    {
        auto src = loadFile (f, 20.0);
        if (src.getNumSamples() < 4096) continue;
        auto d = ResynthData::fromAudio (src, 44100.0, f.getFileNameWithoutExtension());
        float steady = 0.0f;
        std::cout << f.getFileNameWithoutExtension().substring (0, 44).paddedRight (' ', 45)
                  << " pitch " << juce::String (d->detectedFundamentalHz, 1).paddedLeft (' ', 7) << " Hz (" << noteName (d->detectedFundamentalHz).paddedRight (' ', 8)
                  << ")  share " << juce::String (d->pitchConfidence, 2) << "  note events " << d->numNoteEvents
                  << "  first note " << (d->firstNoteMidi > 0.0f ? noteName (440.0f * std::exp2 ((d->firstNoteMidi - 69.0f) / 12.0f)) : juce::String ("-")).paddedRight (' ', 8)
                  << "  tag flips/s " << juce::String (tagFlipsPerSec (*d, &steady), 1) << " (steady partial " << juce::String (steady, 1) << ")  Tune swoop " << juce::String (tuneSwoopPerSec (*d), 0) << " c/s\n";
    }
    return 0;
}
// --resynth-notes <bell|chords|melody|overlap> [fromSec] [toSec]: per-frame notes of the tuning test sources
// (ELUKKA_TUNE_PCT, ELUKKA_NO_NOISE, ELUKKA_NO_PARTIALS change the render)
inline int debugNotes (const juce::String& name, double fromSec, double toSec)
{
    auto makeTone = [] (double secs, std::function<float (float t)> fn)
    {
        juce::AudioBuffer<float> b (2, (int) (sr * secs));
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = fn ((float) i / (float) sr); b.setSample (0, i, v); b.setSample (1, i, v); }
        return b;
    };
    auto harmonicNote = [] (float hz, float t, float gainAmp = 0.25f)
    {
        float v = 0.0f;
        for (int h = 1; h <= 8; ++h) v += gainAmp / (float) h * std::sin (juce::MathConstants<float>::twoPi * hz * (float) h * t);
        return v;
    };
    juce::AudioBuffer<float> src;
    if (name == "bell")
    {
        const float ratios[] { 1.0f, 2.756f, 5.404f, 8.933f };
        src = makeTone (2.5, [&] (float t) { float v = 0.0f; for (int i = 0; i < 4; ++i) v += 0.2f / (float) (i + 1) * std::sin (juce::MathConstants<float>::twoPi * 200.0f * ratios[i] * t); return v; });
    }
    else if (name == "chords")
    {
        struct Chord { float lo, hi; };
        const Chord chords[] { { 164.81f, 207.65f }, { 174.61f, 220.0f }, { 146.83f, 246.94f } };
        const double segSec = 0.5;
        src = makeTone (3 * segSec + 0.1, [&] (float t)
        {
            const int seg = juce::jlimit (0, 2, (int) (t / segSec));
            const float local = t - (float) seg * (float) segSec;
            const float env = juce::jmin (1.0f, local * 200.0f) * juce::jmin (1.0f, ((float) segSec - local) * 200.0f);
            return env * (harmonicNote (chords[seg].lo, local, 0.2f) + harmonicNote (chords[seg].hi, local, 0.2f));
        });
    }
    else if (name == "melody")
    {
        const float melody[] { 138.59f, 155.56f, 174.61f, 196.0f, 220.0f, 246.94f };
        const double segSec = 0.4;
        src = makeTone (6 * segSec + 0.1, [&] (float t)
        {
            const int seg = juce::jlimit (0, 5, (int) (t / segSec));
            const float local = t - (float) seg * (float) segSec;
            const float env = juce::jmin (1.0f, local * 200.0f) * juce::jmin (1.0f, ((float) segSec - local) * 200.0f);
            return env * harmonicNote (melody[seg], local);
        });
    }
    else
    {
        src = makeTone (1.6, [&] (float t)
        {
            float v = harmonicNote (220.0f, t, 0.2f);
            if (t >= 0.75f) v += juce::jmin (1.0f, (t - 0.75f) * 200.0f) * harmonicNote (329.63f, t - 0.75f, 0.2f);
            return v;
        });
    }
    auto d = ResynthData::fromAudio (src, sr, name);
    if (std::getenv ("ELUKKA_NO_NOISE") != nullptr) { d->noiseL.clear(); d->noiseR.clear(); d->noiseCoh.clear(); }
    if (std::getenv ("ELUKKA_NO_PARTIALS") != nullptr) std::fill (d->amp.begin(), d->amp.end(), 0.0f);
    std::cout << name << ": " << d->numNoteEvents << " note events, main " << juce::String (d->detectedFundamentalHz, 1) << " Hz (share " << juce::String (d->pitchConfidence, 2) << ")\n";
    for (int f = (int) (fromSec * d->frameRate); f < juce::jmin (d->numFrames, (int) (toSec * d->frameRate)); ++f)
    {
        std::cout << juce::String (f / d->frameRate, 3) << " s:";
        for (int i = 0; i < ResynthData::maxNotesPerFrame; ++i)
        {
            const float m = d->frameNotes[(size_t) f * ResynthData::maxNotesPerFrame + (size_t) i];
            if (m > 0.0f) std::cout << " " << juce::String (440.0f * std::exp2 ((m - 69.0f) / 12.0f), 1);
        }
        // partials and the note they carry
        std::cout << "   |";
        for (int s = 0; s < d->numSlots; ++s)
        {
            const size_t i = (size_t) f * (size_t) d->numSlots + (size_t) s;
            if (d->amp[i] <= 0.0f) continue;
            std::cout << " " << juce::String (d->freq[i], 0) << "(" << juce::String (juce::Decibels::gainToDecibels (d->amp[i], -120.0f), 0) << ")->" << juce::String (d->noteMidi[i], 1);
        }
        std::cout << "\n";
    }
    // the strongest peaks of the Tune 100 % render (key C) over the same span
    {
        const int block = 512;
        auto qp = std::make_unique<ElukkaAudioProcessor>();
        auto& q = *qp;
        q.setPlayConfigDetails (0, 2, sr, block);
        auto setP = [&q] (const char* id, float v) { auto* p = q.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        setP ("o1_wave", (float) P::WResynth); setP ("o1_level", 1.0f); setP ("master", unityMasterDb);
        setP ("rs_sync", 0.0f); setP ("rs_loop", 0.0f); setP ("ampA", 0.001f); setP ("ampS", 1.0f);
        setP ("rs_tune", std::getenv ("ELUKKA_TUNE_PCT") != nullptr ? (float) std::atof (std::getenv ("ELUKKA_TUNE_PCT")) : 100.0f);   // ELUKKA_TUNE_PCT overrides
        q.setResynth (d);
        q.prepareToPlay (sr, block);
        juce::AudioBuffer<float> out (2, src.getNumSamples()), buf (2, block);
        out.clear();
        for (int pos = 0; pos + block <= out.getNumSamples(); pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            q.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
        }
        const int start = (int) (fromSec * sr), len = juce::jmin ((int) ((toSec - fromSec) * sr), out.getNumSamples() - start);
        int order = 10;
        while ((1 << order) < len && order < 16) ++order;
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
        for (int i = 0; i < N && i < len; ++i) x[(size_t) i] = out.getSample (0, start + i) * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (x.data());
        std::vector<std::pair<float, float>> peaks;   // dB, Hz
        for (int k = 2; k < N / 2 - 1; ++k)
            if (x[(size_t) k] > x[(size_t) k - 1] && x[(size_t) k] >= x[(size_t) k + 1])
                peaks.push_back ({ juce::Decibels::gainToDecibels (x[(size_t) k] * 4.0f / (float) len, -120.0f), (float) k * (float) sr / (float) N });
        std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::cout << "Tune 100% at key C, strongest peaks " << juce::String (fromSec, 2) << "-" << juce::String (toSec, 2) << " s:";
        for (size_t i = 0; i < juce::jmin ((size_t) 14, peaks.size()); ++i) std::cout << " " << juce::String (peaks[i].second, 0) << "Hz/" << juce::String (peaks[i].first, 0) << "dB";
        std::cout << "\n";
    }
    return 0;
}
} // namespace ResynthEval
