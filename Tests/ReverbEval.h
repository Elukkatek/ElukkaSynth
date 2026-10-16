#pragma once
// Reverb measurements: decay time (T60), echo density, late-tail ringing, stereo, stability, clicks and CPU,
// for the FDN reverb (4 and 8 lines) and the previous juce::Reverb. `--reverb <dir>` also writes listening WAVs.
#include <JuceHeader.h>
#include "../Source/DSP/Reverb.h"
#include "../Source/Params.h"
#include "ClickProbe.h"

namespace ReverbEval
{
constexpr double sr = 44100.0;

// Any stereo reverb as "process this buffer in place, fully wet"
using Engine = std::function<void (float*, float*, int)>;

template <int N>
Engine fdn (typename FdnReverb<N>::Params p)
{
    auto r = std::make_shared<FdnReverb<N>>();
    r->prepare (sr);
    p.mix = 1.0f;
    return [r, p] (float* l, float* rr, int n) { r->process (l, rr, n, p); };
}

inline Engine oldReverb (float roomSize, float damping)
{
    auto r = std::make_shared<juce::Reverb>();
    juce::Reverb::Parameters rp;
    rp.roomSize = roomSize; rp.damping = damping; rp.width = 1.0f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f;
    r->setParameters (rp);
    r->setSampleRate (sr);
    return [r] (float* l, float* rr, int n) { r->processStereo (l, rr, n); };
}

inline juce::AudioBuffer<float> run (Engine e, juce::AudioBuffer<float> in, int block = 256)
{
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        e (in.getWritePointer (0, pos), in.getWritePointer (1, pos), n);
    }
    return in;
}

inline juce::AudioBuffer<float> impulse (double seconds, bool stereo = true)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    b.clear();
    b.setSample (0, 0, 1.0f);
    if (stereo) b.setSample (1, 0, 1.0f);
    return b;
}

// T60 from the energy decay curve (Schroeder backward integration) of the 500 Hz..2 kHz band, fitted from -5 to -35 dB
inline float measureT60 (const juce::AudioBuffer<float>& ir)
{
    const int n = ir.getNumSamples();
    std::vector<float> x ((size_t) n);
    juce::IIRFilter hp, lp;
    hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 500.0));
    lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 2000.0));
    for (int i = 0; i < n; ++i) x[(size_t) i] = lp.processSingleSampleRaw (hp.processSingleSampleRaw (ir.getSample (0, i) + ir.getSample (1, i)));
    std::vector<double> edc ((size_t) n + 1, 0.0);
    for (int i = n - 1; i >= 0; --i) edc[(size_t) i] = edc[(size_t) i + 1] + (double) x[(size_t) i] * x[(size_t) i];
    const double total = edc[0];
    int t5 = -1, t35 = -1;
    for (int i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10 (edc[(size_t) i] / total + 1e-30);
        if (t5 < 0 && db <= -5.0) t5 = i;
        if (t35 < 0 && db <= -35.0) { t35 = i; break; }
    }
    if (t5 < 0 || t35 < 0) return -1.0f;
    return (float) ((t35 - t5) / sr * 60.0 / 30.0);
}

// Normalised echo density (Abel & Huang): 1 = noise-like. Returns the time (ms) it first reaches 0.9.
inline float echoDensityTimeMs (const juce::AudioBuffer<float>& ir)
{
    const int w = (int) (0.02 * sr);
    for (int c = w / 2; c + w / 2 < ir.getNumSamples(); c += (int) (0.002 * sr))
    {
        double sum = 0.0;
        for (int i = c - w / 2; i < c + w / 2; ++i) sum += (double) ir.getSample (0, i) * ir.getSample (0, i);
        const double sd = std::sqrt (sum / w);
        if (sd < 1e-9) continue;
        int out = 0;
        for (int i = c - w / 2; i < c + w / 2; ++i) out += std::abs (ir.getSample (0, i)) > sd ? 1 : 0;
        if ((double) out / w / 0.3173 >= 0.9) return (float) (c / sr * 1000.0);
    }
    return -1.0f;
}

// Ringing: how peaky the late tail's spectrum is (max over 300 Hz..6 kHz of the smoothed-spectrum ratio of each
// bin to the local median, in dB). Dense, smooth tails stay near the statistical value of noise (~10 dB);
// metallic tails have strong modes far above it.
inline float tailRingingDb (const juce::AudioBuffer<float>& ir, double fromSec)
{
    const int order = 14, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N), x ((size_t) N * 2, 0.0f);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (w.data(), (size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    const int s0 = (int) (fromSec * sr);
    std::vector<double> pw ((size_t) N / 2, 0.0);
    int frames = 0;
    for (int s = s0; s + N <= ir.getNumSamples() && frames < 6; s += N / 2, ++frames)
    {
        for (int i = 0; i < N; ++i) x[(size_t) i] = ir.getSample (0, s + i) * w[(size_t) i];
        std::fill (x.begin() + N, x.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (x.data());
        for (int k = 0; k < N / 2; ++k) pw[(size_t) k] += (double) x[(size_t) k] * x[(size_t) k];
    }
    if (frames == 0) return 0.0f;
    const int k0 = (int) (300.0 * N / sr), k1 = (int) (6000.0 * N / sr), half = 40;
    // average peak excess over local medians, as a robust "how tonal is the tail" figure
    double sum = 0.0;
    int count = 0;
    std::vector<double> win;
    for (int k = k0; k < k1; k += 8)
    {
        win.assign (pw.begin() + (k - half), pw.begin() + (k + half));
        std::nth_element (win.begin(), win.begin() + half, win.end());
        const double med = win[(size_t) half] + 1e-30;
        double mx = 0.0;
        for (int j = k - 4; j < k + 4; ++j) mx = std::max (mx, pw[(size_t) j]);
        sum += 10.0 * std::log10 (mx / med);
        ++count;
    }
    return (float) (sum / count);
}

// Stereo: correlation of L and R in the tail (1 = mono, 0 = fully decorrelated), and the mono sum's energy relative to
// the average of L and R energy (1 = no cancellation, 0.5 = uncorrelated, below = cancellation).
inline std::pair<float, float> stereoStats (const juce::AudioBuffer<float>& ir)
{
    double lr = 0, ll = 0, rr = 0, mono = 0;
    for (int i = (int) (0.05 * sr); i < ir.getNumSamples(); ++i)
    {
        const double l = ir.getSample (0, i), r = ir.getSample (1, i);
        lr += l * r; ll += l * l; rr += r * r; mono += 0.25 * (l + r) * (l + r);
    }
    return { (float) (lr / std::sqrt (ll * rr + 1e-30)), (float) (mono / (0.5 * (ll + rr) + 1e-30) * 2.0) };
}

inline double cpuNsPerSample (Engine e)
{
    juce::AudioBuffer<float> b (2, (int) (sr * 20.0));
    juce::Random r (3);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.1f * (r.nextFloat() * 2.0f - 1.0f));
    const auto t0 = juce::Time::getHighResolutionTicks();
    run (e, std::move (b), 128);
    const auto t1 = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds (t1 - t0) * 1e9 / (sr * 20.0);
}

inline void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Test material: plucks, a detuned saw pad, a mono bright lead line
inline juce::AudioBuffer<float> material (int kind, double seconds)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    b.clear();
    juce::Random r (11);
    const float twoPi = juce::MathConstants<float>::twoPi;
    if (kind == 0)   // plucks, 4 notes
    {
        const float notes[] { 261.6f, 329.6f, 392.0f, 523.3f };
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < (int) (0.4 * sr); ++i)
            {
                const float t = (float) i / (float) sr;
                float v = 0.0f;
                for (int h = 1; h <= 8; ++h) v += std::sin (twoPi * notes[k] * h * t) / (float) h * std::exp (-t * (6.0f + 3.0f * h));
                const int at = (int) ((0.3 + k * 0.35) * sr) + i;
                b.addSample (0, at, 0.3f * v);
                b.addSample (1, at, 0.3f * v);
            }
    }
    else if (kind == 1)   // saw pad chord, 2 s, slight stereo detune
    {
        double ph[6] {};
        const double f[3] { 130.81, 155.56, 196.0 };
        for (int i = 0; i < (int) (2.0 * sr); ++i)
        {
            const float env = juce::jmin (1.0f, (float) i / (float) (0.3 * sr)) * juce::jmin (1.0f, (float) ((2.0 * sr) - i) / (float) (0.3 * sr));
            float l = 0.0f, rr = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                ph[k] += f[k / 2] * (k % 2 ? 1.004 : 0.996) / sr;
                ph[k] -= std::floor (ph[k]);
                (k % 2 ? rr : l) += (float) (2.0 * ph[k] - 1.0);
            }
            b.setSample (0, i + (int) (0.2 * sr), 0.07f * env * l);
            b.setSample (1, i + (int) (0.2 * sr), 0.07f * env * rr);
        }
    }
    else   // mono lead line with vibrato
    {
        double ph = 0.0;
        const float notes[] { 440.0f, 523.3f, 587.3f, 659.3f, 587.3f, 523.3f };
        for (int i = 0; i < (int) (1.8 * sr); ++i)
        {
            const float t = (float) i / (float) sr;
            ph += notes[(int) (t / 0.3f) % 6] * (1.0 + 0.004 * std::sin (twoPi * 5.5f * t)) / sr;
            ph -= std::floor (ph);
            const float v = 0.15f * (float) (2.0 * ph - 1.0) * juce::jmin (1.0f, std::fmod (t, 0.3f) * 60.0f);
            b.setSample (0, i + (int) (0.2 * sr), v);
            b.setSample (1, i + (int) (0.2 * sr), v);
        }
    }
    return b;
}

inline int run (const juce::File& outDir)
{
    using R4 = FdnReverb<4>;
    using R8 = FdnReverb<8>;
    auto params = [] (int mode, float decay, float mod = 0.25f) { R4::Params p; p.mode = mode; p.decay = decay; p.mod = mod; return p; };
    auto p8 = [] (const R4::Params& p) { R8::Params q; q.mode = p.mode; q.decay = p.decay; q.mod = p.mod; q.size = p.size; q.tone = p.tone; return q; };

    std::cout << "Decay accuracy (500 Hz - 2 kHz T60, Smooth, size 65%, tone 50%):\n";
    for (float d : { 0.5f, 1.0f, 3.0f, 8.0f, 15.0f })
    {
        auto ir = ReverbEval::run (fdn<4> (params (0, d)), impulse (d * 1.6 + 1.0));
        std::cout << "  setting " << juce::String (d, 1).paddedLeft (' ', 4) << " s -> measured " << juce::String (measureT60 (ir), 2) << " s\n";
    }

    std::cout << "\nCharacter (decay 3 s): echo density 0.9 reached at, late-tail ringing, L/R correlation, mono sum energy\n";
    const char* modeNames[] { "Smooth", "Vintage", "Lush" };
    for (int m = 0; m < 3; ++m)
        for (float mod : { 0.0f, 0.25f })
        {
            auto ir = ReverbEval::run (fdn<4> (params (m, 3.0f, mod)), impulse (4.0, false));
            auto ir8 = ReverbEval::run (fdn<8> (p8 (params (m, 3.0f, mod))), impulse (4.0, false));
            const auto st = stereoStats (ir), st8 = stereoStats (ir8);
            std::cout << "  " << juce::String (modeNames[m]).paddedRight (' ', 8) << " mod " << juce::String ((int) (mod * 100)).paddedLeft (' ', 2)
                      << "%   4 lines: " << juce::String (echoDensityTimeMs (ir), 0).paddedLeft (' ', 4) << " ms, ringing "
                      << juce::String (tailRingingDb (ir, 0.8), 1) << " dB, corr " << juce::String (st.first, 2) << ", mono " << juce::String (st.second, 2)
                      << "   |  8 lines: " << juce::String (echoDensityTimeMs (ir8), 0).paddedLeft (' ', 4) << " ms, ringing "
                      << juce::String (tailRingingDb (ir8, 0.8), 1) << " dB, corr " << juce::String (st8.first, 2) << "\n";
        }
    {
        auto ir = ReverbEval::run (oldReverb (0.8f, 0.5f), impulse (4.0, false));
        const auto st = stereoStats (ir);
        std::cout << "  previous juce::Reverb (size 0.8): " << juce::String (echoDensityTimeMs (ir), 0) << " ms, ringing "
                  << juce::String (tailRingingDb (ir, 0.8), 1) << " dB, corr " << juce::String (st.first, 2) << ", T60 "
                  << juce::String (measureT60 (ir), 2) << " s\n";
        // reference: white noise with an exponential decay (no ringing at all)
        juce::AudioBuffer<float> nz (2, (int) (4.0 * sr));
        juce::Random r (5);
        for (int i = 0; i < nz.getNumSamples(); ++i) { const float e = std::exp (-(float) i / (float) sr * 2.3f); nz.setSample (0, i, e * (r.nextFloat() - 0.5f)); nz.setSample (1, i, e * (r.nextFloat() - 0.5f)); }
        std::cout << "  reference decaying noise: ringing " << juce::String (tailRingingDb (nz, 0.8), 1) << " dB\n";
    }

    std::cout << "\nWet level with noise in (dB, new / previous at the same old preset setting):\n";
    for (float oldSize : { 0.5f, 0.7f, 0.85f, 0.95f })
    {
        auto level = [&] (Engine e)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * 8.0));
            juce::Random r (9);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.1f * (r.nextFloat() * 2.0f - 1.0f));
            auto out = ReverbEval::run (e, b);
            return juce::Decibels::gainToDecibels (out.getRMSLevel (0, (int) (sr * 4.0), (int) (sr * 4.0)) / 0.0577f);
        };
        R4::Params p = params (0, P::reverbDecayForOldSize (oldSize));
        p.size = oldSize;
        std::cout << "  old size " << oldSize << " (decay " << juce::String (p.decay, 2) << " s): new " << juce::String (level (fdn<4> (p)), 1)
                  << " dB, previous " << juce::String (level (oldReverb (oldSize, 0.5f)), 1) << " dB\n";
    }

    std::cout << "\nCPU (ns per stereo sample, reverb alone):  4 lines " << juce::String (cpuNsPerSample (fdn<4> (params (0, 3.0f))), 1)
              << "   8 lines " << juce::String (cpuNsPerSample (fdn<8> (p8 (params (0, 3.0f)))), 1)
              << "   previous juce::Reverb " << juce::String (cpuNsPerSample (oldReverb (0.6f, 0.5f)), 1) << "\n";

    if (outDir != juce::File())
    {
        outDir.createDirectory();
        const char* kinds[] { "plucks", "pad", "lead" };
        for (int k = 0; k < 3; ++k)
        {
            writeWav (outDir.getChildFile (juce::String (kinds[k]) + " dry.wav"), material (k, 6.0));
            for (int m = 0; m < 3; ++m)
            {
                auto p = params (m, k == 1 ? 5.0f : 3.0f);
                p.mix = 0.35f;
                auto r = std::make_shared<R4>();
                r->prepare (sr);
                auto out = material (k, 6.0);
                for (int pos = 0; pos < out.getNumSamples(); pos += 256)
                    r->process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), juce::jmin (256, out.getNumSamples() - pos), p);
                writeWav (outDir.getChildFile (juce::String (kinds[k]) + " " + modeNames[m] + ".wav"), out);
            }
            auto out = material (k, 6.0);
            auto old = oldReverb (0.75f, 0.5f);
            juce::AudioBuffer<float> wet (out);
            ReverbEval::run (old, wet);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) out.setSample (c, i, 0.65f * out.getSample (c, i) + 0.35f * wet.getSample (c, i));
            writeWav (outDir.getChildFile (juce::String (kinds[k]) + " previous reverb.wav"), out);
        }
        std::cout << "Listening files written to " << outDir.getFullPathName() << "\n";
    }
    return 0;
}
} // namespace ReverbEval
