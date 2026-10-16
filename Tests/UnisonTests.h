#pragma once
// Unison tests (spec items 1-10) and listening renders.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

namespace UnisonTests
{
inline float centsOf (float mul) { return 1200.0f * std::log2 (mul); }

// Renders `samples` of a saw through one SpectralOsc with the engine's 128-sample control blocks.
inline juce::AudioBuffer<float> renderOsc (SpectralOsc& osc, const FFTBank& fft, std::vector<float>& work, double sr, float f0,
                                           int voices, float detune, int detuneMode, float blend, float width, int samples)
{
    std::vector<float> amps ((size_t) maxHarmonics);
    const int numHarm = juce::jlimit (1, maxHarmonics, (int) (sr * 0.45 / f0));
    baseSpectrum (P::WSaw, amps.data(), numHarm);
    juce::AudioBuffer<float> out (2, samples);
    out.clear();
    juce::Random rng (1);
    for (int start = 0; start < samples; start += 128)
    {
        const int n = juce::jmin (128, samples - start);
        osc.setSpectrum (amps.data(), numHarm, 0.0f, f0, fft, work.data());
        osc.setUnison (voices, detune, detuneMode, blend, width, f0);
        osc.beginBlock (n, 1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            osc.tick (0.0f, l, r, rng);
            out.setSample (0, start + i, l);
            out.setSample (1, start + i, r);
        }
    }
    return out;
}

inline int run()
{
    int failures = 0;
    auto report = [&] (bool ok, const juce::String& text) { if (! ok) ++failures; std::cout << (ok ? "ok   " : "FAIL ") << "Unison: " << text << "\n"; };
    const double sr = 48000.0;
    FFTBank fft;
    std::vector<float> work ((size_t) maxTableSize * 2 + 16);
    juce::Random rng (7);

    // 1. One voice behaves like the plain oscillator: centred, exact pitch, unaffected by every unison setting
    {
        SpectralOsc a, b;
        a.prepare (sr); b.prepare (sr);
        a.resetPhases (1, Unison::PhaseReset, 0.0f, 0.0f, rng);
        b.resetPhases (1, Unison::PhaseSpread, 0.0f, 1.0f, rng);
        auto x = renderOsc (a, fft, work, sr, 220.0f, 1, 0.0f, Unison::DetuneLinear, 1.0f, 0.0f, 9600);
        auto y = renderOsc (b, fft, work, sr, 220.0f, 1, 80.0f, Unison::DetuneSuper, 0.0f, 1.0f, 9600);
        float maxDiff = 0.0f, lr = 0.0f;
        for (int i = 0; i < x.getNumSamples(); ++i)
        {
            maxDiff = std::max (maxDiff, std::abs (x.getSample (0, i) - y.getSample (0, i)));
            lr = std::max (lr, std::abs (x.getSample (0, i) - x.getSample (1, i)));
        }
        report (maxDiff < 1.0e-6f && lr < 1.0e-6f && std::abs (a.getIncrement (0) - 220.0f / (float) sr) < 1.0e-9f,
                "1 voice ignores detune/blend/width/modes, is centred (L-R " + juce::String (lr, 7) + ") and plays exactly 220 Hz");
    }

    // 2. Seven voices: one centre voice at 0 cents, symmetric offsets, outer voices at +-detune
    {
        float mul[16], gl[16], gr[16];
        bool ok = true;
        juce::String cents;
        for (int mode : { Unison::DetuneLinear, Unison::DetuneSuper })
        {
            Unison::layout (7, 30.0f, mode, 1.0f, 1.0f, mul, gl, gr);
            ok &= centsOf (mul[3]) == 0.0f && std::abs (centsOf (mul[0]) + 30.0f) < 1.0e-3f && std::abs (centsOf (mul[6]) - 30.0f) < 1.0e-3f;
            for (int k = 0; k < 7; ++k) ok &= std::abs (centsOf (mul[k]) + centsOf (mul[6 - k])) < 1.0e-3f;
            cents << (mode == 0 ? "linear [" : " super [");
            for (int k = 0; k < 7; ++k) cents << juce::String (centsOf (mul[k]), 1) << (k < 6 ? " " : "]");
        }
        report (ok, "7 voices, 30 ct: " + cents);
    }

    // 3. Even count: symmetric offsets, no voice at the played pitch
    {
        float mul[16], gl[16], gr[16];
        Unison::layout (6, 30.0f, Unison::DetuneLinear, 1.0f, 1.0f, mul, gl, gr);
        bool ok = true;
        for (int k = 0; k < 6; ++k) { ok &= std::abs (centsOf (mul[k])) > 1.0f; ok &= std::abs (centsOf (mul[k]) + centsOf (mul[5 - k])) < 1.0e-3f; }
        report (ok, "6 voices: inner pair at " + juce::String (centsOf (mul[2]), 1) + " / " + juce::String (centsOf (mul[3]), 1) + " ct, none at 0");
    }

    // 4. Reset mode repeats exactly across triggers (whatever the random generator's state)
    {
        SpectralOsc osc;
        osc.prepare (sr);
        juce::Random r1 (1), r2 (999);
        osc.resetPhases (7, Unison::PhaseReset, 0.25f, 1.0f, r1);
        float first[16];
        for (int k = 0; k < 16; ++k) first[k] = osc.getPhase (k);
        osc.resetPhases (7, Unison::PhaseReset, 0.25f, 1.0f, r2);
        bool ok = true;
        for (int k = 0; k < 16; ++k) ok &= osc.getPhase (k) == first[k] && first[k] == 0.25f;
        report (ok, "Reset: every voice starts at the Phase setting (0.25) on every trigger");
    }

    // 5. Random mode differs between triggers and between voices
    {
        SpectralOsc osc;
        osc.prepare (sr);
        osc.resetPhases (7, Unison::PhaseRandom, 0.0f, 1.0f, rng);
        float first[7];
        for (int k = 0; k < 7; ++k) first[k] = osc.getPhase (k);
        osc.resetPhases (7, Unison::PhaseRandom, 0.0f, 1.0f, rng);
        int sameAcross = 0, sameWithin = 0;
        for (int k = 0; k < 7; ++k)
        {
            if (osc.getPhase (k) == first[k]) ++sameAcross;
            for (int j = k + 1; j < 7; ++j) if (first[j] == first[k]) ++sameWithin;
        }
        // Spread is deterministic and fans out evenly
        osc.resetPhases (4, Unison::PhaseSpread, 0.0f, 1.0f, rng);
        const bool spreadOk = osc.getPhase (0) == 0.0f && std::abs (osc.getPhase (1) - 0.25f) < 1e-6f && std::abs (osc.getPhase (3) - 0.75f) < 1e-6f;
        report (sameAcross == 0 && sameWithin == 0 && spreadOk, "Random: all phases differ between triggers and voices; Spread fans 4 voices to 0/.25/.5/.75");
    }

    // 6. Each voice's phase advances at its own frequency
    {
        SpectralOsc osc;
        osc.prepare (sr);
        osc.resetPhases (5, Unison::PhaseReset, 0.0f, 0.0f, rng);
        const float f0 = 110.0f, det = 40.0f;
        const int samples = 1280;
        renderOsc (osc, fft, work, sr, f0, 5, det, Unison::DetuneLinear, 1.0f, 1.0f, samples);
        float mul[16], gl[16], gr[16];
        Unison::layout (5, det, Unison::DetuneLinear, 1.0f, 1.0f, mul, gl, gr);
        float worst = 0.0f;
        for (int k = 0; k < 5; ++k)
        {
            const double expected = std::fmod ((double) f0 * mul[k] * samples / sr, (double) stretchCycles);
            worst = std::max (worst, (float) std::abs (osc.getPhase (k) - expected));
        }
        report (worst < 1.0e-3f, "phase of each of 5 voices after 1280 samples matches its own frequency (worst error " + juce::String (worst, 6) + " cycles)");
    }

    // 7 & 8. Width 0 is mono; width 1 puts the outer voices hard left/right
    {
        SpectralOsc osc;
        osc.prepare (sr);
        osc.resetPhases (7, Unison::PhaseRandom, 0.0f, 1.0f, rng);
        auto m = renderOsc (osc, fft, work, sr, 110.0f, 7, 30.0f, Unison::DetuneLinear, 1.0f, 0.0f, 4800);
        float lr = 0.0f;
        for (int i = 0; i < m.getNumSamples(); ++i) lr = std::max (lr, std::abs (m.getSample (0, i) - m.getSample (1, i)));
        report (lr < 1.0e-6f, "width 0: left and right identical (max difference " + juce::String (lr, 8) + ")");
        float mul[16], gl[16], gr[16];
        Unison::layout (7, 30.0f, Unison::DetuneLinear, 1.0f, 1.0f, mul, gl, gr);
        report (gr[0] < 1.0e-6f && gl[0] > 0.1f && gl[6] < 1.0e-6f && gr[6] > 0.1f && std::abs (gl[3] - gr[3]) < 1.0e-6f,
                "width 1: first voice only left, last voice only right, centre voice in the middle");
    }

    // 9. Finite and bounded across voice counts and sample rates
    {
        bool ok = true;
        float worstPeak = 0.0f;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int n = 1; n <= 16; ++n)
            {
                SpectralOsc osc;
                osc.prepare (rate);
                osc.resetPhases (n, Unison::PhaseRandom, 0.0f, 1.0f, rng);
                auto b = renderOsc (osc, fft, work, rate, 55.0f, n, 100.0f, Unison::DetuneSuper, 1.0f, 1.0f, 2048);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i) ok &= std::isfinite (b.getSample (ch, i));
                worstPeak = std::max (worstPeak, b.getMagnitude (0, b.getNumSamples()));
            }
        report (ok && worstPeak < 4.0f, "1-16 voices at 44.1/48/96 kHz: all finite, peak " + juce::String (worstPeak, 2));
    }

    // 10. Normalisation keeps loudness steady; Blend sets centre vs outer level and keeps total power
    {
        juce::String levels;
        float lo = 1e9f, hi = -1e9f;
        for (int n : { 1, 3, 5, 7, 16 })
        {
            double sum = 0.0;
            for (int trial = 0; trial < 8; ++trial)
            {
                SpectralOsc osc;
                osc.prepare (sr);
                osc.resetPhases (n, Unison::PhaseRandom, 0.0f, 1.0f, rng);
                auto b = renderOsc (osc, fft, work, sr, 110.0f, n, 25.0f, Unison::DetuneLinear, 1.0f, 1.0f, 48000);
                sum += 0.5 * (b.getRMSLevel (0, 0, b.getNumSamples()) + b.getRMSLevel (1, 0, b.getNumSamples()));
            }
            const float db = juce::Decibels::gainToDecibels ((float) (sum / 8.0));
            lo = std::min (lo, db); hi = std::max (hi, db);
            levels << n << "v " << juce::String (db, 1) << " dB  ";
        }
        float mul[16], gl[16], gr[16];
        bool powerOk = true;
        for (int n : { 1, 2, 7, 16 })
            for (float blend : { 0.0f, 0.5f, 1.0f })
            {
                Unison::layout (n, 25.0f, Unison::DetuneLinear, blend, 0.7f, mul, gl, gr);
                double p = 0.0;
                for (int k = 0; k < n; ++k) p += gl[k] * gl[k] + gr[k] * gr[k];
                powerOk &= std::abs (p - 1.0) < 1.0e-4;
            }
        Unison::layout (7, 25.0f, Unison::DetuneLinear, 0.0f, 0.0f, mul, gl, gr);
        const bool blendZero = gl[0] == 0.0f && gl[6] == 0.0f && gl[3] > 0.7f;
        Unison::layout (7, 25.0f, Unison::DetuneLinear, 0.5f, 0.0f, mul, gl, gr);
        const bool blendHalf = std::abs (gl[0] / gl[3] - 0.5f) < 1.0e-5f;
        report (hi - lo < 1.5f && powerOk && blendZero && blendHalf,
                "loudness with random phases: " + levels + "(spread " + juce::String (hi - lo, 2) + " dB); total power constant for any blend; "
                "blend 0 = centre only, blend 50% = outer voices at half level");
    }
    return failures;
}

// Listening renders: saw, voice counts 1/3/5/7, the combinations from the spec. Each file plays the note twice
// (to compare attacks) then holds it. Prints each file's loudness.
inline void renderListening (const juce::File& dir)
{
    dir.createDirectory();
    const double sr = 48000.0;
    const int block = 512;
    struct Combo { const char* name; float detune; int phaseMode; float amount; float width; int detuneMode; };
    const Combo combos[] {
        { "A_low-detune_reset_width0",       8.0f, Unison::PhaseReset,  0.0f,   0.0f,   Unison::DetuneLinear },
        { "B_moderate_reset_full-width",    25.0f, Unison::PhaseReset,  0.0f,   100.0f, Unison::DetuneLinear },
        { "C_moderate_random_full-width",   25.0f, Unison::PhaseRandom, 100.0f, 100.0f, Unison::DetuneLinear },
        { "D_moderate_spread_full-width",   25.0f, Unison::PhaseSpread, 100.0f, 100.0f, Unison::DetuneLinear },
        { "E_moderate_random_full-width_SUPER", 25.0f, Unison::PhaseRandom, 100.0f, 100.0f, Unison::DetuneSuper },
    };
    auto setP = [] (ElukkaAudioProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    for (auto& c : combos)
        for (int n : { 1, 3, 5, 7 })
        {
            ElukkaAudioProcessor p;
            p.setPlayConfigDetails (0, 2, sr, block);
            setP (p, "master", -6.0f); setP (p, "ampA", 0.003f); setP (p, "ampS", 1.0f); setP (p, "ampR", 0.3f);
            setP (p, "o1_uniVoices", (float) n); setP (p, "o1_uniDetune", c.detune); setP (p, "o1_phaseMode", (float) c.phaseMode);
            setP (p, "o1_rand", c.amount); setP (p, "o1_uniWidth", c.width); setP (p, "o1_uniMode", (float) c.detuneMode);
            setP (p, "o1_uniBlend", 100.0f);
            p.prepareToPlay (sr, block);
            const int total = (int) (sr * 4.0) / block * block;
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            out.clear();
            const int events[][2] { { 0, 1 }, { (int) (sr * 0.8), 0 }, { (int) (sr * 1.0), 1 }, { (int) (sr * 3.4), 0 } };
            for (int b = 0; b < total / block; ++b)
            {
                juce::MidiBuffer midi;
                for (auto& e : events)
                    if (e[0] >= b * block && e[0] < (b + 1) * block)
                        midi.addEvent (e[1] ? juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110) : juce::MidiMessage::noteOff (1, 45), e[0] - b * block);
                p.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * block, buf, ch, 0, block);
            }
            auto f = dir.getChildFile (juce::String (c.name) + "_" + juce::String (n) + "v.wav");
            f.deleteFile();
            juce::WavAudioFormat wav;
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)))
                {
                    os.release();
                    w->writeFromAudioSampleBuffer (out, 0, total);
                }
            const float rms = 0.5f * (out.getRMSLevel (0, (int) sr, (int) sr * 2) + out.getRMSLevel (1, (int) sr, (int) sr * 2));
            std::cout << f.getFileName().paddedRight (' ', 46) << " loudness " << juce::String (juce::Decibels::gainToDecibels (rms), 1) << " dB\n";
        }
}
} // namespace UnisonTests
