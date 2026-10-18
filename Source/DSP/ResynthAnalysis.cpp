// Audio analysis for ResynthData::fromAudio: builds the hybrid model (sinusoidal partials + noise residual).
// Runs on the message thread when a source is loaded; nothing here touches the audio thread.
//
//  1. Multi-resolution STFT (Hann): long windows resolve the bass, short windows keep the highs sharp in time; each
//     resolution owns a frequency range. Peaks are refined with parabolic interpolation (frequency, level, phase).
//  2. Tracking: peaks continue the previous frame's partials by a cost on frequency and level change
//     (cheapest pairs first); a track may skip one frame and resume. Short tracks are dropped (noise).
//  3. Interval frequencies: between two frames of a track the frequency is set to exactly what turns the measured
//     phase at one frame into the measured phase at the next (phase vocoder), so synthesis stays in phase with the
//     source without resetting oscillator phases.
//  4. Envelope timing: long windows smear levels in time, so each half-octave band's level is re-measured with a
//     short window every frame and the partials are scaled to follow it (cuts for pre-echo and tails, limited
//     boosts for under-read attacks).
//  5. Residual: the partials are rendered offline and subtracted from the source; what is left (breath, attacks,
//     diffuse reverb) is stored as a sixth-octave noise spectrogram with left/right coherence.
//  6. Pitch: every informative frame is searched for its notes (harmonic evidence over the partials plus a YIN
//     estimate of the waveform), notes are linked into events over time and every partial is tagged with its
//     note's pitch, so Tune can correct each note of the source separately (see analysePitch).
#include "Resynth.h"
#include <complex>

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

struct Settings
{
    double hopSec = 0.0058;      // frame spacing (256 samples at 44.1 kHz)
    float keepDb = 80.0f;        // keep peaks within this of the frame's loudest
    int maxPeaks = 200;
    int minTrackFrames = 3;      // shorter tracks are noise picked up as peaks (the residual carries it instead)
    int maxGap = 1;              // frames a track may skip and still resume
    int cooldown = 2;            // silent frames before a slot can start a new track
    float tol = 0.025f;          // frequency tolerance (ratio) for continuing a track
    float levelCost = 0.3f;      // weight of the level change (per 10 dB) in the matching cost
    float smoothLoCents = 0.5f, smoothHiCents = 2.0f;   // interval-frequency smoothing: none below, full above
    int shortOrder = 10;         // window for envelope timing and the residual (1024)
    int envLowOrder = 11;        // longer envelope window for the lowest bands (a 1024 window barely holds one period of a 55 Hz partial)
    float envSplitHz = 160.0f;   // bands below this use envLowOrder
    float cutDb = -60.0f, boostDb = 12.0f, deadDb = 1.0f;   // envelope correction limits
    float residualOverSub = 2.0f;   // residual = source power - this x model power (never partials the model has)
};

struct Peak { float freq, amp, pan, phase, stereoPhase; int res; };

// Magnitude of the Hann window's transform at an offset of x bins from a sinusoid, relative to the peak.
float hannLobe (float x)
{
    const float ax = std::abs (x);
    if (ax < 1.0e-4f) return 1.0f;
    if (std::abs (ax - 1.0f) < 1.0e-4f) return 0.5f;
    const float s = std::sin (juce::MathConstants<float>::pi * ax) / (juce::MathConstants<float>::pi * ax);
    return std::abs (s / (1.0f - ax * ax));
}

// One analysis resolution: FFT, window and the spectra of both channels for the current frame.
struct Stft
{
    int order = 0, size = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, L, R;

    void init (int ord)
    {
        order = ord;
        size = 1 << ord;
        fft = std::make_unique<juce::dsp::FFT> (ord);
        window.resize ((size_t) size);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) size,
                                                                  juce::dsp::WindowingFunction<float>::hann, false);
        L.resize ((size_t) size * 2);
        R.resize ((size_t) size * 2);
    }
    // windows the signals around `centre` (zero outside) and transforms them
    void analyse (const float* l, const float* r, int len, int centre)
    {
        for (int i = 0; i < size; ++i)
        {
            const int idx = centre - size / 2 + i;
            const bool in = idx >= 0 && idx < len;
            L[(size_t) i] = in ? l[idx] * window[(size_t) i] : 0.0f;
            R[(size_t) i] = in ? r[idx] * window[(size_t) i] : 0.0f;
        }
        fft->performRealOnlyForwardTransform (L.data(), true);
        fft->performRealOnlyForwardTransform (R.data(), true);
    }
    float re (const std::vector<float>& x, int k) const { return x[(size_t) (2 * k)]; }
    float im (const std::vector<float>& x, int k) const { return x[(size_t) (2 * k + 1)]; }
    float powerL (int k) const { return re (L, k) * re (L, k) + im (L, k) * im (L, k); }
    float powerR (int k) const { return re (R, k) * re (R, k) + im (R, k) * im (R, k); }
    // RMS magnitude of both channels: stereo-wide material has phase differences that partly cancel in L + R
    float mag (int k) const { return std::sqrt (0.5f * (powerL (k) + powerR (k))); }
};

// Peaks of one resolution between fLo and fHi (Hz), appended to `out`.
void findPeaks (const Stft& s, double sampleRate, float fLo, float fHi, int res, std::vector<Peak>& out)
{
    const int N = s.size;
    const float binHz = (float) (sampleRate / N);
    const int k0 = juce::jmax (2, (int) (fLo / binHz));
    const int k1 = juce::jmin (N / 2 - 3, (int) (fHi / binHz));
    const float ampScale = 4.0f / (float) N;   // Hann: a sinusoid of amplitude A peaks at A N / 4

    float m0 = s.mag (k0 - 1), m1 = s.mag (k0);
    for (int k = k0; k <= k1; ++k)
    {
        const float m2 = s.mag (k + 1);
        if (m1 > m0 && m1 >= m2 && m1 * ampScale > 1.0e-6f)
        {
            // parabolic interpolation on log magnitude for frequency and level
            const float a = std::log (m0 + 1.0e-12f), b = std::log (m1 + 1.0e-12f), c = std::log (m2 + 1.0e-12f);
            const float denom = a - 2.0f * b + c;
            const float p = std::abs (denom) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;
            const float f = ((float) k + p) * binHz;
            const float amp = std::exp (b - 0.25f * (a - c) * p) * ampScale;

            const float aL = std::sqrt (s.powerL (k)), aR = std::sqrt (s.powerR (k));
            // each side's level at the interpolated peak; the partial plays a (1 -+ pan) on the left/right
            const float sideAmp = 0.5f * (aL + aR) * amp / m1;
            // Phase at the window centre: the window is symmetric about N/2, so arg X[k] = phase - pi k.
            // Stored for a sine oscillator (cos = sine a quarter cycle later).
            const float re = s.re (s.L, k) + s.re (s.R, k), im = s.im (s.L, k) + s.im (s.R, k);
            float ph = (std::atan2 (im, re) + juce::MathConstants<float>::pi * (float) (k % 2)) / twoPi + 0.25f;
            ph -= std::floor (ph);
            float sph = 0.0f;
            if (aL > 1.0e-3f * aR && aR > 1.0e-3f * aL)
            {
                sph = (std::atan2 (s.im (s.R, k), s.re (s.R, k)) - std::atan2 (s.im (s.L, k), s.re (s.L, k))) / twoPi;
                sph -= std::round (sph);
            }
            out.push_back ({ f, sideAmp, (aR - aL) / (aR + aL + 1.0e-12f), ph, sph, res });
        }
        m0 = m1;
        m1 = m2;
    }
}

// ---- track post-processing ----

// Interval frequencies from the measured phases (see file comment, step 3). The peak estimate picks the number
// of whole cycles; where the two disagree by more than a quarter cycle per frame (fast glides, noise peaks) the
// peak estimate is kept.
void intervalFrequencies (ResynthData& d, const std::vector<float>& peakFreq, double dt)
{
    const int S = d.numSlots, F = d.numFrames;
    for (int s = 0; s < S; ++s)
    {
        float last = 0.0f;
        for (int f = 0; f < F; ++f)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            if (d.amp[i] <= 0.0f) continue;
            const bool nextOn = f + 1 < F && d.amp[i + (size_t) S] > 0.0f;
            if (nextOn)
            {
                const float fa = peakFreq[i], fb = peakFreq[i + (size_t) S];
                const float expected = 0.5f * (fa + fb) * (float) dt;
                const float frac = d.phase[i + (size_t) S] - d.phase[i];
                const float cycles = frac + std::round (expected - frac);
                const float fpv = cycles / (float) dt;
                d.freq[i] = fpv > 0.0f && std::abs (fpv - 0.5f * (fa + fb)) < 0.25f / (float) dt ? fpv : 0.5f * (fa + fb);
                last = d.freq[i];
            }
            else
            {
                // last frame of the track: repeat the previous interval (or the estimate for a one-frame track)
                const bool prevOn = f > 0 && d.amp[i - (size_t) S] > 0.0f;
                d.freq[i] = prevOn ? last : peakFreq[i];
            }
        }
    }
}

// The phase-derived interval frequencies carry the measurement noise of the two phases they come from, which
// on real material is a few cents of frame-to-frame jitter (the "squiggle"). A 5-point binomial smoother along
// each track (in log frequency) removes that; vibrato and glides span many more frames and pass through.
// Where the measured value is within loCents of the smoothed one it is kept exactly (a clean sinusoid, whose
// integrated phase then stays locked to the source); deviations beyond hiCents are what the smoothing is for. The kernel is unbiased, so even smoothed tracks keep their long-term phase agreement with the source.
void smoothIntervalFrequencies (ResynthData& d, float loCents, float hiCents)
{
    const int S = d.numSlots, F = d.numFrames;
    std::vector<float> tmp ((size_t) F);
    static const float kernel[5] { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };
    for (int s = 0; s < S; ++s)
    {
        for (int f = 0; f < F; ++f) tmp[(size_t) f] = d.freq[(size_t) f * (size_t) S + (size_t) s];
        for (int f = 0; f < F; ++f)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            if (d.amp[i] <= 0.0f || tmp[(size_t) f] <= 0.0f) continue;
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -2; k <= 2; ++k)
            {
                const int g = f + k;
                if (g < 0 || g >= F || d.amp[(size_t) g * (size_t) S + (size_t) s] <= 0.0f || tmp[(size_t) g] <= 0.0f) continue;
                sum += kernel[k + 2] * std::log2 (tmp[(size_t) g]);
                wsum += kernel[k + 2];
            }
            if (wsum <= 0.0f) continue;
            const float exact = std::log2 (tmp[(size_t) f]), smooth = sum / wsum;
            const float cents = std::abs (exact - smooth) * 1200.0f;
            const float w = juce::jlimit (0.0f, 1.0f, (cents - loCents) / (hiCents - loCents));
            d.freq[i] = std::exp2 (exact + (smooth - exact) * w);
        }
    }
}

void removeShortTracks (ResynthData& d, int minFrames)
{
    const int S = d.numSlots, F = d.numFrames;
    for (int s = 0; s < S; ++s)
    {
        int start = -1;
        for (int f = 0; f <= F; ++f)
        {
            const bool on = f < F && d.amp[(size_t) f * (size_t) S + (size_t) s] > 0.0f;
            if (on && start < 0) start = f;
            if (! on && start >= 0)
            {
                if (f - start < minFrames)
                    for (int g = start; g < f; ++g) d.amp[(size_t) g * (size_t) S + (size_t) s] = 0.0f;
                start = -1;
            }
        }
    }
}

// Each track keeps one stereo phase: its loudness-weighted circular mean (frame-to-frame changes in diffuse
// material would otherwise wobble each side's pitch).
void constantStereoPhase (ResynthData& d)
{
    const int S = d.numSlots, F = d.numFrames;
    for (int s = 0; s < S; ++s)
    {
        int start = -1;
        for (int f = 0; f <= F; ++f)
        {
            const bool on = f < F && d.amp[(size_t) f * (size_t) S + (size_t) s] > 0.0f;
            if (on && start < 0) start = f;
            if (! on && start >= 0)
            {
                double c = 0.0, sn = 0.0;
                for (int g = start; g < f; ++g)
                {
                    const size_t i = (size_t) g * (size_t) S + (size_t) s;
                    c += d.amp[i] * std::cos (twoPi * d.stereoPhase[i]);
                    sn += d.amp[i] * std::sin (twoPi * d.stereoPhase[i]);
                }
                const float mean = (float) (std::atan2 (sn, c) / twoPi);
                for (int g = start; g < f; ++g) d.stereoPhase[(size_t) g * (size_t) S + (size_t) s] = mean;
                start = -1;
            }
        }
    }
}

// Fill the frequency/pan of silent frames in each slot from the nearest active frame.
void fillInactive (ResynthData& d)
{
    const int S = d.numSlots;
    for (int s = 0; s < S; ++s)
    {
        int next = -1;
        for (int f = d.numFrames - 1; f >= 0; --f)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            if (d.amp[i] > 0.0f) next = f;
            else if (next >= 0)
            {
                d.freq[i] = d.freq[(size_t) next * (size_t) S + (size_t) s];
                d.pan[i] = d.pan[(size_t) next * (size_t) S + (size_t) s];
            }
        }
        int prev = -1;
        for (int f = 0; f < d.numFrames; ++f)
        {
            const size_t i = (size_t) f * (size_t) S + (size_t) s;
            if (d.amp[i] > 0.0f) prev = f;
            else if (d.freq[i] <= 0.0f && prev >= 0)
            {
                d.freq[i] = d.freq[(size_t) prev * (size_t) S + (size_t) s];
                d.pan[i] = d.pan[(size_t) prev * (size_t) S + (size_t) s];
            }
        }
    }
}

// Envelope timing (file comment, step 4).
void correctEnvelopes (ResynthData& d, const float* L, const float* R, int len, double sampleRate, int hop, const Settings& st)
{
    constexpr int numBands = 18;   // half octaves from 40 Hz
    auto bandPos = [] (float hz) { return std::log2 (juce::jmax (40.0f, hz) / 40.0f) * 2.0f; };
    const int S = d.numSlots, F = d.numFrames;

    // Two measurement windows: the lowest bands need a longer one to hold a few periods of their partials.
    Stft stfts[2];
    stfts[0].init (st.shortOrder);
    stfts[1].init (st.envLowOrder);
    const int splitBand = juce::jlimit (0, numBands, (int) std::ceil (bandPos (st.envSplitHz)));   // bands < splitBand use the long window
    struct Map { int N; float binHz; std::vector<int> bandOfBin; };
    Map maps[2];
    for (int w = 0; w < 2; ++w)
    {
        auto& m = maps[w];
        m.N = stfts[w].size;
        m.binHz = (float) (sampleRate / m.N);
        m.bandOfBin.resize ((size_t) m.N / 2 + 1);
        for (int k = 0; k <= m.N / 2; ++k) m.bandOfBin[(size_t) k] = juce::jlimit (0, numBands - 1, (int) bandPos ((float) k * m.binHz));
    }

    std::vector<float> measured ((size_t) F * numBands, 0.0f), model ((size_t) F * numBands, 0.0f);
    std::vector<std::complex<float>> spec[2] { std::vector<std::complex<float>> ((size_t) maps[0].N / 2 + 1), std::vector<std::complex<float>> ((size_t) maps[1].N / 2 + 1) };
    for (int fr = 0; fr < F; ++fr)
    {
        for (int w = 0; w < 2; ++w)
        {
            auto& m = maps[w];
            stfts[w].analyse (L, R, len, fr * hop);
            for (int k = 1; k < m.N / 2; ++k)
            {
                const int b = m.bandOfBin[(size_t) k];
                if ((b < splitBand) == (w == 1))
                    measured[(size_t) fr * numBands + (size_t) b] += 0.5f * (stfts[w].powerL (k) + stfts[w].powerR (k));
            }
        }

        // The model's spectrum in each window, summed coherently from the partials' measured phases: partials
        // closer than the window resolves beat in the measurement, and they must beat the same way here, or the
        // gain would ripple at the beat rate and modulate every partial in the band.
        const size_t row = (size_t) fr * (size_t) S;
        for (int w = 0; w < 2; ++w) std::fill (spec[w].begin(), spec[w].end(), std::complex<float>());
        for (int s = 0; s < S; ++s)
        {
            const float a = d.amp[row + (size_t) s];
            if (a <= 0.0f) continue;
            const float pan = d.pan[row + (size_t) s];
            const int band = juce::jlimit (0, numBands - 1, (int) bandPos (d.freq[row + (size_t) s]));
            const int wi = band < splitBand ? 1 : 0;
            const auto& m = maps[wi];
            // a Hann-windowed sinusoid of amplitude A peaks at A N / 4; phase stored for a sine at the window centre,
            // the window's symmetry about its centre adds pi k per bin
            const float A = a * std::sqrt (1.0f + pan * pan) * (float) m.N * 0.25f;
            const float pos = d.freq[row + (size_t) s] / m.binHz;
            const int kc = (int) std::lround (pos);
            const float ph = twoPi * (d.phase.empty() ? 0.0f : d.phase[row + (size_t) s] - 0.25f);
            for (int j = -3; j <= 3; ++j)
            {
                const int k = kc + j;
                if (k < 1 || k >= m.N / 2) continue;
                const float x = (float) k - pos;
                // signed Hann kernel: sin(pi x) / (pi x (1 - x^2))
                float kern;
                if (std::abs (x) < 1.0e-4f) kern = 1.0f;
                else if (std::abs (std::abs (x) - 1.0f) < 1.0e-4f) kern = 0.5f;
                else kern = std::sin (juce::MathConstants<float>::pi * x) / (juce::MathConstants<float>::pi * x * (1.0f - x * x));
                const float phk = ph - juce::MathConstants<float>::pi * (float) k;
                spec[wi][(size_t) k] += std::complex<float> (A * kern * std::cos (phk), A * kern * std::sin (phk));
            }
        }
        for (int w = 0; w < 2; ++w)
            for (int k = 1; k < maps[w].N / 2; ++k)
                model[(size_t) fr * numBands + (size_t) maps[w].bandOfBin[(size_t) k]] += std::norm (spec[w][(size_t) k]);
    }

    // per band gain in log units: cuts down to cutDb (smeared pre-echo and tails), boosts up to boostDb (attacks the
    // long windows under-read)
    const float lnCut = st.cutDb / 20.0f * std::log (10.0f), lnBoost = st.boostDb / 20.0f * std::log (10.0f), dead = st.deadDb / 20.0f * std::log (10.0f);
    std::vector<float> logGain ((size_t) F * numBands, 0.0f);
    for (size_t i = 0; i < logGain.size(); ++i)
    {
        const float mo = model[i], me = measured[i];
        if (mo <= 1.0e-14f) continue;
        float g = juce::jlimit (lnCut, lnBoost, 0.5f * std::log (me / mo + 1.0e-20f));
        // partials in one band beat against each other, so the measured level ripples by a few dB even on steady
        // tones; only the part beyond that ripple is applied
        g = g > dead ? g - dead : (g < -dead ? g + dead : 0.0f);
        logGain[i] = g;
    }
    // each partial takes its band's gain (the model spread its energy over the bins the same way the measurement
    // sees it, so band and partial are consistent)
    for (int fr = 0; fr < F; ++fr)
    {
        const size_t row = (size_t) fr * (size_t) S;
        for (int s = 0; s < S; ++s)
        {
            float& a = d.amp[row + (size_t) s];
            if (a <= 0.0f) continue;
            const int b = juce::jlimit (0, numBands - 1, (int) bandPos (d.freq[row + (size_t) s]));
            a *= std::exp (logGain[(size_t) fr * numBands + (size_t) b]);
        }
    }
}

// Renders the partial model at the source pitch and speed, the way the real-time engine plays it
// (phase set from the measurement when a partial starts, interval frequencies integrated, levels interpolated).
void renderModel (const ResynthData& d, int hop, int len, std::vector<float>& outL, std::vector<float>& outR)
{
    const int S = d.numSlots, F = d.numFrames;
    const double sr = d.frameRate * hop;
    outL.assign ((size_t) len, 0.0f);
    outR.assign ((size_t) len, 0.0f);
    std::vector<double> ph ((size_t) S, 0.0);
    std::vector<float> sine (4097);
    for (int i = 0; i <= 4096; ++i) sine[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / 4096.0);
    auto sin01 = [&] (float cycles)
    {
        const float x = (cycles - std::floor (cycles)) * 4096.0f;
        const int i = (int) x;
        return sine[(size_t) i] + (sine[(size_t) i + 1] - sine[(size_t) i]) * (x - (float) i);
    };

    for (int f = 0; f + 1 < F; ++f)
    {
        const size_t rowA = (size_t) f * (size_t) S, rowB = rowA + (size_t) S;
        const int n0 = f * hop, n1 = juce::jmin (len, n0 + hop);
        for (int s = 0; s < S; ++s)
        {
            const float a0 = d.amp[rowA + (size_t) s], a1 = d.amp[rowB + (size_t) s];
            if (a0 <= 0.0f && a1 <= 0.0f) continue;
            const double inc = d.freq[rowA + (size_t) s] / sr;
            // phase at the interval start from the measurement, at the start of each run of active frames
            const bool starting = a0 <= 0.0f ? true : (f == 0 || d.amp[rowA - (size_t) S + (size_t) s] <= 0.0f);
            // (the loop advances before reading, so start one step behind)
            if (starting)
                ph[(size_t) s] = (a0 > 0.0f ? (double) d.phase[rowA + (size_t) s] : (double) d.phase[rowB + (size_t) s] - inc * hop) - inc;
            const float pan = d.pan[(a0 > 0.0f ? rowA : rowB) + (size_t) s];
            const float sph = 0.5f * d.stereoPhase[(a0 > 0.0f ? rowA : rowB) + (size_t) s];
            const float gL = 1.0f - pan, gR = 1.0f + pan;
            double p = ph[(size_t) s];
            for (int n = n0; n < n1; ++n)
            {
                const float a = a0 + (a1 - a0) * (float) (n - n0) / (float) hop;
                p += inc;
                const float pc = (float) (p - std::floor (p));
                outL[(size_t) n] += a * gL * sin01 (pc - sph);
                outR[(size_t) n] += a * gR * sin01 (pc + sph);
            }
            ph[(size_t) s] = p - std::floor (p);
        }
    }
}

// Residual noise spectrogram (file comment, step 5).
void analyseResidual (ResynthData& d, const float* L, const float* R, int len, double sampleRate, int hop, const Settings& st)
{
    constexpr int B = ResynthData::numNoiseBands;
    const int F = d.numFrames;
    std::vector<float> mL, mR;
    renderModel (d, hop, len, mL, mR);
    std::vector<float> rL ((size_t) len), rR ((size_t) len);
    for (int i = 0; i < len; ++i) { rL[(size_t) i] = L[i] - mL[(size_t) i]; rR[(size_t) i] = R[i] - mR[(size_t) i]; }

    Stft src, mod, res;
    src.init (st.shortOrder);
    mod.init (st.shortOrder);
    res.init (st.shortOrder);
    const int N = src.size;
    const float binHz = (float) (sampleRate / N);
    // |X_k|^2 of Hann-windowed noise with one-sided power density P is P * sr * 3N/16 on average
    const float toDensity = 16.0f / (3.0f * (float) sampleRate * (float) N);
    std::vector<int> bandOfBin ((size_t) N / 2 + 1, -1);
    std::array<bool, B> hasBin {};
    for (int k = 1; k < N / 2; ++k)
    {
        const int b = (int) std::floor (ResynthData::noiseBandPos ((float) k * binHz));
        if (b >= 0 && b < B) { bandOfBin[(size_t) k] = b; hasBin[(size_t) b] = true; }
    }

    d.noiseL.assign ((size_t) F * B, 0.0f);
    d.noiseR.assign ((size_t) F * B, 0.0f);
    d.noiseCoh.assign ((size_t) F * B, 1.0f);
    std::array<double, B> cr, ci, eL, eR;
    std::array<int, B> count;
    std::vector<float> qL ((size_t) N / 2), qR ((size_t) N / 2), sortBuf;
    std::vector<bool> owned ((size_t) N / 2);
    const float beta = st.residualOverSub;
    const int S = d.numSlots;
    for (int fr = 0; fr < F; ++fr)
    {
        const int centre = fr * hop;
        src.analyse (L, R, len, centre);
        mod.analyse (mL.data(), mR.data(), len, centre);
        res.analyse (rL.data(), rR.data(), len, centre);
        cr.fill (0.0); ci.fill (0.0); eL.fill (0.0); eR.fill (0.0); count.fill (0);
        // bins inside a partial's main lobe belong to the partial: whatever the subtraction leaves there is the
        // model's own error, not noise, so the noise floor is read from the bins in between
        std::fill (owned.begin(), owned.end(), false);
        for (int s = 0; s < S; ++s)
        {
            const size_t i = (size_t) fr * (size_t) S + (size_t) s;
            if (d.amp[i] <= 0.0f) continue;
            const int kc = (int) std::lround (d.freq[i] / binHz);
            for (int k = juce::jmax (1, kc - 2); k <= juce::jmin (N / 2 - 1, kc + 2); ++k) owned[(size_t) k] = true;
        }
        for (int k = 1; k < N / 2; ++k)
        {
            const int b = bandOfBin[(size_t) k];
            if (b < 0) continue;
            // Residual power per bin: what the source has clearly beyond the model (over-subtracting the model, so a
            // partial the model has slightly too quiet leaves nothing), and never more than the subtraction left
            // (a partial at the right level but a slightly wrong phase contributes nothing either).
            qL[(size_t) k] = juce::jmin (res.powerL (k), juce::jmax (0.0f, src.powerL (k) - beta * mod.powerL (k)));
            qR[(size_t) k] = juce::jmin (res.powerR (k), juce::jmax (0.0f, src.powerR (k) - beta * mod.powerR (k)));
            cr[(size_t) b] += res.re (res.L, k) * res.re (res.R, k) + res.im (res.L, k) * res.im (res.R, k);
            ci[(size_t) b] += res.im (res.L, k) * res.re (res.R, k) - res.re (res.L, k) * res.im (res.R, k);
            eL[(size_t) b] += res.powerL (k);
            eR[(size_t) b] += res.powerR (k);
            if (! owned[(size_t) k]) ++count[(size_t) b];
        }
        // Band level from the bins no partial owns: the median bin power (scaled to be unbiased for noise, whose bin
        // powers are exponentially distributed) rather than the mean, so a few stray bins do not become a band of
        // noise. A band whose bins all belong to partials takes the level of the band below (filled in below).
        auto bandLevel = [&] (const std::vector<float>& q, int b)
        {
            sortBuf.clear();
            for (int k = 1; k < N / 2; ++k) if (bandOfBin[(size_t) k] == b && ! owned[(size_t) k]) sortBuf.push_back (q[(size_t) k]);
            if (sortBuf.size() < 3) { double sum = 0.0; for (auto v : sortBuf) sum += v; return (float) (sum / (double) sortBuf.size()); }
            std::nth_element (sortBuf.begin(), sortBuf.begin() + (long) sortBuf.size() / 2, sortBuf.end());
            return sortBuf[sortBuf.size() / 2] / 0.6931f;
        };
        const size_t row = (size_t) fr * B;
        for (int b = 0; b < B; ++b)
        {
            if (count[(size_t) b] == 0) continue;   // no free bin: stays 0 if partials own the band, filled below if it has no bin at all
            d.noiseL[row + (size_t) b] = std::sqrt (bandLevel (qL, b) * toDensity);
            d.noiseR[row + (size_t) b] = std::sqrt (bandLevel (qR, b) * toDensity);
            d.noiseCoh[row + (size_t) b] = eL[(size_t) b] > 0.0 && eR[(size_t) b] > 0.0
                                               ? (float) juce::jlimit (0.0, 1.0, std::hypot (cr[(size_t) b], ci[(size_t) b]) / std::sqrt (eL[(size_t) b] * eR[(size_t) b])) : 1.0f;
        }
        // the narrow low bands between bin centres take the level of the nearest band that has a bin
        for (int b = 0; b < B; ++b)
        {
            if (hasBin[(size_t) b]) continue;
            int from = -1;
            for (int g = b - 1; g >= 0 && from < 0; --g) if (hasBin[(size_t) g]) from = g;
            for (int g = b + 1; g < B && from < 0; ++g) if (hasBin[(size_t) g]) from = g;
            if (from < 0) continue;
            d.noiseL[row + (size_t) b] = d.noiseL[row + (size_t) from];
            d.noiseR[row + (size_t) b] = d.noiseR[row + (size_t) from];
            d.noiseCoh[row + (size_t) b] = d.noiseCoh[row + (size_t) from];
        }
    }
}
} // namespace

void ResynthData::renderPartials (std::vector<float>& outL, std::vector<float>& outR) const
{
    const int hop = (int) std::lround (sourceRate / frameRate);
    renderModel (*this, hop, sourceAudio.getNumSamples(), outL, outR);
}

// ---- pitch ----------------------------------------------------------------------------------------------------
//
// Every informative frame is searched for up to maxNotesPerFrame notes, one at a time: candidate fundamentals
// (each strong partial and its 2nd..4th subharmonics, plus the YIN estimate of the waveform for the first note) are
// scored by how much partial weight lies on their harmonic series, with a penalty for low harmonics the series
// predicts but the frame lacks (this separates a fundamental from its half) and a bonus when the time-domain
// estimate agrees. The winner is refined from its matched harmonics, its partials are taken out of the pool, and
// the search repeats on what is left. Silent and attack frames are skipped. Notes are then linked across frames
// into events (pitch continuity, one frame of gap allowed; events shorter than three frames are dropped) and each
// event gets one centre pitch (weighted median), so vibrato and bends inside a note are kept when it is corrected.
// Short events on a harmonic of a longer note next to them are that note's harmonics (attacks) and are dropped.
// The earliest event of at least 50 ms (the lowest of a chord) is the source's first note, whose octave Tune's
// target uses.
// Every partial is tagged with the centre pitch of its note; partials no note claimed take the nearest note in
// frequency, frames without notes take the nearest frame in time with notes. The residual uses the per-frame note
// list the same way. A chroma over all notes gives the summary "main note" for the display.
void ResynthData::analysePitch (ResynthData& d, const float* mono, int len, double sampleRate)
{
    d.detectedFundamentalHz = 0.0f;
    d.pitchConfidence = 0.0f;
    d.numNoteEvents = 0;
    d.firstNoteMidi = 0.0f;
    const int F = d.numFrames, S = d.numSlots;
    d.noteMidi.assign ((size_t) F * (size_t) S, 0.0f);
    d.frameNotes.assign ((size_t) F * (size_t) maxNotesPerFrame, 0.0f);
    if (F < 2 || S == 0 || len < 4096) return;
    constexpr float c0 = 16.3516f;   // C-1
    constexpr float loHz = 24.0f, hiHz = 2500.0f;   // fundamental range considered
    auto toMidi = [] (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); };
    auto toHz = [] (float midi) { return 440.0f * std::exp2 ((midi - 69.0f) / 12.0f); };

    // ---- YIN on the signal decimated by 4 (93 ms windows, every 4th frame) ----
    const int dec = 4;
    const double srD = sampleRate / dec;
    const int lenD = len / dec;
    std::vector<float> x ((size_t) lenD);
    {
        constexpr int taps = 48;   // windowed sinc low pass at 85 % of the decimated Nyquist
        float h[taps];
        float sum = 0.0f;
        for (int i = 0; i < taps; ++i)
        {
            const float t = (float) i - (float) (taps - 1) * 0.5f, fc = 0.85f * 0.5f / (float) dec;
            const float sinc = std::abs (t) < 1.0e-6f ? 2.0f * fc : std::sin (twoPi * fc * t) / (juce::MathConstants<float>::pi * t);
            h[i] = sinc * (0.5f - 0.5f * std::cos (twoPi * ((float) i + 0.5f) / (float) taps));
            sum += h[i];
        }
        for (auto& v : h) v /= sum;
        for (int n = 0; n < lenD; ++n)
        {
            float acc = 0.0f;
            for (int i = 0; i < taps; ++i)
            {
                const int idx = n * dec - taps / 2 + i;
                if (idx >= 0 && idx < len) acc += mono[idx] * h[i];
            }
            x[(size_t) n] = acc;
        }
    }
    const int W = 1024, maxLag = 512, yinEvery = 4;
    const int numYin = (F + yinEvery - 1) / yinEvery;
    std::vector<float> yinHz ((size_t) numYin, 0.0f), yinAper ((size_t) numYin, 1.0f), dn ((size_t) maxLag + 1);
    for (int y = 0; y < numYin; ++y)
    {
        const int start = (int) ((double) (y * yinEvery) / d.frameRate * srD) - W / 2;
        if (start < 0 || start + W + maxLag > lenD) continue;
        const float* s = x.data() + start;
        double run = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float acc = 0.0f;
            for (int j = 0; j < W; ++j) { const float e = s[j] - s[j + tau]; acc += e * e; }
            run += acc;
            dn[(size_t) tau] = (float) (acc * tau / (run + 1.0e-12));   // cumulative mean normalised difference
        }
        int bestTau = 0;
        for (int tau = 2; tau < maxLag; ++tau)
            if (dn[(size_t) tau] < 0.15f && dn[(size_t) tau] <= dn[(size_t) tau - 1] && dn[(size_t) tau] <= dn[(size_t) tau + 1]) { bestTau = tau; break; }
        if (bestTau == 0)
        {
            float best = 1.0e9f;
            for (int tau = 2; tau < maxLag; ++tau) if (dn[(size_t) tau] < best) { best = dn[(size_t) tau]; bestTau = tau; }
        }
        if (bestTau < 2) continue;
        const float a = dn[(size_t) bestTau - 1], b = dn[(size_t) bestTau], c = dn[(size_t) bestTau + 1];
        const float den = a - 2.0f * b + c;
        const float p = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
        yinHz[(size_t) y] = (float) (srD / ((float) bestTau + p));
        yinAper[(size_t) y] = juce::jlimit (0.0f, 1.0f, b - 0.25f * (a - c) * p);
    }

    // ---- per-frame notes ----
    std::vector<float> frameE ((size_t) F, 0.0f);
    float eMax = 0.0f;
    for (int f = 0; f < F; ++f)
    {
        const size_t row = (size_t) f * (size_t) S;
        float e = 0.0f;
        for (int s = 0; s < S; ++s) e += d.amp[row + (size_t) s] * d.amp[row + (size_t) s];
        frameE[(size_t) f] = e;
        eMax = juce::jmax (eMax, e);
    }
    if (eMax <= 0.0f) return;

    static constexpr int maxN = 40;   // static: MSVC needs no lambda capture for it
    struct Part { float hz, w; int slot; bool used; };
    struct Note { float hz, w; int event = -1; };   // w = score (matched partial weight after the series penalty)
    struct Match { int n; float wm, m; };
    std::vector<Match> matches;
    std::vector<float> prevNotes;
    std::vector<std::vector<Note>> notes ((size_t) F);
    std::vector<int> owner ((size_t) F * (size_t) S, -1);   // note index within the frame that claimed the partial
    std::vector<Part> parts;
    std::vector<float> cands;
    float present[maxN + 1];
    auto match = [] (float hz, float c, int& n) -> float
    {
        const float r = hz / c;
        n = juce::roundToInt (r);
        if (n < 1 || n > maxN) return 0.0f;
        const float dev = 1200.0f * std::abs (std::log2 (r / (float) n)), tol = 30.0f + 1.5f * (float) n;
        return std::exp (-(dev / tol) * (dev / tol));
    };
    for (int f = 0; f < F; ++f)
    {
        const float e = frameE[(size_t) f];
        if (e < eMax * 1.0e-4f) continue;                              // quiet (-40 dB)
        if (f >= 3 && e > 4.0f * frameE[(size_t) f - 3]) continue;     // attack (rose > 6 dB in 17 ms)
        const size_t row = (size_t) f * (size_t) S;
        float frameMax = 0.0f;
        for (int s = 0; s < S; ++s) frameMax = juce::jmax (frameMax, d.amp[row + (size_t) s]);
        parts.clear();
        for (int s = 0; s < S; ++s)
        {
            const float a = d.amp[row + (size_t) s];
            if (a >= frameMax * 0.01f && d.freq[row + (size_t) s] > 20.0f)
                parts.push_back ({ d.freq[row + (size_t) s], std::sqrt (a), s, false });   // compressed: weak harmonics count too
        }
        if (parts.empty()) continue;
        std::sort (parts.begin(), parts.end(), [] (const Part& a, const Part& b) { return a.w > b.w; });
        if (parts.size() > 64) parts.resize (64);
        float wTotal = 0.0f;
        for (auto& pt : parts) wTotal += pt.w;
        const float yHz = yinHz[(size_t) (f / yinEvery)], yAper = yinAper[(size_t) (f / yinEvery)];
        prevNotes.clear();
        for (int back = 1; back <= 2 && prevNotes.empty() && f - back >= 0; ++back)
            for (auto& n : notes[(size_t) (f - back)]) prevNotes.push_back (n.hz);

        for (int it = 0; it < maxNotesPerFrame; ++it)
        {
            cands.clear();
            int seen = 0;
            for (auto& pt : parts)
            {
                if (pt.used) continue;
                if (++seen > 12) break;
                for (int k = 1; k <= 4; ++k)
                {
                    const float c = pt.hz / (float) k;
                    if (c >= loHz && c <= hiHz) cands.push_back (c);
                }
            }
            if (it == 0 && yAper < 0.5f && yHz >= loHz && yHz <= hiHz) cands.push_back (yHz);
            float bestScore = 0.0f, bestHz = 0.0f;
            for (float c : cands)
            {
                // partials on the candidate's series, lowest harmonic first
                matches.clear();
                for (auto& pt : parts)
                {
                    if (pt.used) continue;
                    int n;
                    const float m = match (pt.hz, c, n);
                    if (m > 0.0f) matches.push_back ({ n, pt.w * m, m });
                }
                std::sort (matches.begin(), matches.end(), [] (const Match& a, const Match& b) { return a.n < b.n; });
                // The series is credited harmonic by harmonic; each predicted harmonic up to the 10th that is not
                // there costs a factor (the fundamental itself may be weak, higher gaps are implausible). The best
                // truncation counts, so a lone coincidental high partial cannot drag a sparse series down, and a
                // common subharmonic of two notes (a chord) loses to either note.
                float matched = 0.0f, pen = 1.0f, score = 0.0f;
                int lastN = 0;
                for (auto& mt : matches)
                {
                    if (mt.m > 0.3f)
                    {
                        for (int n = lastN + 1; n < mt.n && n <= 10; ++n) pen *= n == 1 ? 0.75f : 0.6f;
                        lastN = juce::jmax (lastN, mt.n);
                    }
                    matched += mt.wm;
                    score = juce::jmax (score, matched * pen);
                }
                if (it == 0 && yAper < 0.5f && yHz > 0.0f && std::abs (1200.0f * std::log2 (c / yHz)) < 60.0f) score *= 1.0f + 0.6f * (1.0f - yAper);
                for (float ph : prevNotes)   // continuity: a note that was there a frame ago is likely still there
                    if (std::abs (1200.0f * std::log2 (c / ph)) < 70.0f) { score *= 1.2f; break; }
                if (score > bestScore) { bestScore = score; bestHz = c; }
            }
            if (bestHz <= 0.0f) break;
            // how many partials would this note claim, and refine it from them
            int count = 0;
            float hzSum = 0.0f, wSum = 0.0f;
            for (auto& pt : parts)
            {
                if (pt.used) continue;
                int n;
                const float m = match (pt.hz, bestHz, n);
                if (m > 0.5f) { ++count; const float w = pt.w * m * m * m * m / (float) (n * n); hzSum += w * pt.hz / (float) n; wSum += w; }
            }
            // the first note needs a convincing share of the frame; further notes need two partials (or one
            // that carries much of the frame: a lone inharmonic partial of a bell is not a note of its own) and a
            // minimum share, otherwise what is left is not a note
            const bool accept = it == 0 ? bestScore >= 0.3f * wTotal
                                        : (count >= 2 || bestScore >= 0.4f * wTotal) && bestScore >= 0.1f * wTotal;
            if (! accept || count == 0) break;
            const float hz = wSum > 0.0f ? hzSum / wSum : bestHz;
            const int idx = (int) notes[(size_t) f].size();
            notes[(size_t) f].push_back ({ hz, bestScore });
            for (auto& pt : parts)
            {
                if (pt.used) continue;
                int n;
                if (match (pt.hz, hz, n) > 0.5f) { pt.used = true; owner[row + (size_t) pt.slot] = idx; }
            }
        }
    }

    // ---- note events: link notes across frames by pitch continuity (one frame may be skipped) ----
    struct Event { int firstFrame = -1, lastFrame = -1, extendedAt = -1, count = 0; float lastHz = 0.0f; std::vector<std::pair<float, float>> midiW; float centre = 0.0f; bool live = false; };
    std::vector<Event> events;
    for (int f = 0; f < F; ++f)
        for (auto& n : notes[(size_t) f])
        {
            int best = -1;
            float bestCents = 70.0f;
            for (int e = 0; e < (int) events.size(); ++e)
            {
                auto& ev = events[(size_t) e];
                if (ev.lastFrame < f - 2 || ev.lastFrame >= f || ev.extendedAt == f) continue;
                const float c = std::abs (1200.0f * std::log2 (n.hz / ev.lastHz));
                if (c < bestCents) { bestCents = c; best = e; }
            }
            if (best < 0) { events.push_back ({}); best = (int) events.size() - 1; events.back().firstFrame = f; }
            auto& ev = events[(size_t) best];
            ev.lastFrame = f; ev.extendedAt = f; ev.lastHz = n.hz; ++ev.count;
            ev.midiW.push_back ({ toMidi (n.hz), n.w });
            n.event = best;
        }
    for (auto& ev : events)
    {
        ev.live = ev.count >= 3;
        if (! ev.live) continue;
        std::sort (ev.midiW.begin(), ev.midiW.end());
        double total = 0.0, acc = 0.0;
        for (auto& m : ev.midiW) total += m.second;
        ev.centre = ev.midiW.back().first;
        for (auto& m : ev.midiW) { acc += m.second; if (acc >= 0.5 * total) { ev.centre = m.first; break; } }
        ++d.numNoteEvents;
    }
    // A short event (under 50 ms) on a harmonic (2nd..8th, within 35 c) of a longer note it overlaps or adjoins (within
    // 50 ms) is that note's harmonic read as a note, typically in an attack: dropped, so its frames take the real note.
    const int shortFrames = juce::jmax (3, (int) std::lround (0.05 * d.frameRate));
    for (auto& ev : events)
    {
        if (! ev.live || ev.count >= shortFrames) continue;
        for (auto& o : events)
        {
            if (! o.live || o.count < shortFrames || o.firstFrame > ev.lastFrame + shortFrames || o.lastFrame < ev.firstFrame - shortFrames) continue;
            const float h = std::exp2 ((ev.centre - o.centre) / 12.0f);
            if (h > 1.5f && h < 8.5f && std::abs (1200.0f * std::log2 (h / std::round (h))) < 35.0f)
            {
                ev.live = false;
                --d.numNoteEvents;
                break;
            }
        }
    }
    // the first note: the earliest event lasting at least 50 ms (any event if none does); of a chord (events starting
    // within 30 ms of it), the lowest
    {
        const int minCount = std::any_of (events.begin(), events.end(), [&] (const Event& e) { return e.live && e.count >= shortFrames; }) ? shortFrames : 1;
        auto reliable = [&] (const Event& e) { return e.live && e.count >= minCount; };
        int first = -1;
        for (auto& ev : events) if (reliable (ev) && (first < 0 || ev.firstFrame < first)) first = ev.firstFrame;
        const int chordFrames = (int) (0.03 * d.frameRate);
        for (auto& ev : events)
            if (reliable (ev) && ev.firstFrame <= first + chordFrames && (d.firstNoteMidi <= 0.0f || ev.centre < d.firstNoteMidi))
                d.firstNoteMidi = ev.centre;
    }
    // drop the notes of short events
    for (int f = 0; f < F; ++f)
    {
        auto& fr = notes[(size_t) f];
        for (int i = (int) fr.size() - 1; i >= 0; --i)
            if (! events[(size_t) fr[(size_t) i].event].live)
            {
                const size_t row = (size_t) f * (size_t) S;
                for (int s = 0; s < S; ++s)
                    if (owner[row + (size_t) s] == i) owner[row + (size_t) s] = -1;
                    else if (owner[row + (size_t) s] > i) --owner[row + (size_t) s];
                fr.erase (fr.begin() + i);
            }
    }

    // ---- tag every partial (and every frame) with note pitches ----
    // Frames without notes (silence, attacks) borrow a nearby frame's notes: the next one with notes if it is
    // close (an attack belongs to the note that is starting, not to the one that just ended), else the nearest
    // within half a second.
    std::vector<int> ref ((size_t) F, -1);
    {
        const int maxDist = (int) (0.5 * d.frameRate), lookAhead = (int) (0.1 * d.frameRate);
        int last = -1;
        for (int f = 0; f < F; ++f) { if (! notes[(size_t) f].empty()) last = f; if (last >= 0 && f - last <= maxDist) ref[(size_t) f] = last; }
        int next = -1;
        for (int f = F - 1; f >= 0; --f)
        {
            if (! notes[(size_t) f].empty()) next = f;
            if (next >= 0 && next - f <= maxDist && (ref[(size_t) f] < 0 || next - f <= lookAhead || next - f < f - ref[(size_t) f])) ref[(size_t) f] = next;
        }
    }
    std::vector<uint8_t> claimed ((size_t) F * (size_t) S, 0);   // the partial was on the series of a note found in that frame
    for (int f = 0; f < F; ++f)
    {
        const int rf = ref[(size_t) f];
        if (rf < 0) continue;
        const auto& fr = notes[(size_t) rf];
        for (size_t i = 0; i < juce::jmin ((size_t) maxNotesPerFrame, fr.size()); ++i)
            d.frameNotes[(size_t) f * (size_t) maxNotesPerFrame + i] = events[(size_t) fr[i].event].centre;
        const size_t row = (size_t) f * (size_t) S;
        for (int s = 0; s < S; ++s)
        {
            if (d.amp[row + (size_t) s] <= 0.0f) continue;
            const int own = rf == f ? owner[row + (size_t) s] : -1;
            if (own >= 0) { d.noteMidi[row + (size_t) s] = events[(size_t) fr[(size_t) own].event].centre; claimed[row + (size_t) s] = 1; continue; }
            // unclaimed: the nearest note in frequency
            float bestDist = 1.0e9f, best = 0.0f;
            for (auto& n : fr)
            {
                const float dist = std::abs (std::log2 (d.freq[row + (size_t) s] / n.hz));
                if (dist < bestDist) { bestDist = dist; best = events[(size_t) n.event].centre; }
            }
            d.noteMidi[row + (size_t) s] = best;
        }
    }
    // A partial keeps its note for as long as it sounds; it only changes when another note claims it, and that claim
    // holds for 50 ms. Otherwise the fading partials of a note that has ended would take the next note (their nearest
    // one) and swoop by the difference of the two corrections, and a harmonic two notes share, or an unclaimed partial
    // whose nearest note comes and goes, would flip from frame to frame: every change moves the partial's Tune
    // correction and is heard as a pitch wiggle. Decided with look-ahead, so a new note still takes effect on its
    // first frame.
    {
        const int hold = juce::jmax (2, (int) std::lround (0.05 * d.frameRate));
        auto same = [] (float a, float b) { return std::abs (a - b) < 0.01f; };
        for (int s = 0; s < S; ++s)
        {
            auto at = [&] (int f) { return (size_t) f * (size_t) S + (size_t) s; };
            auto tag = [&] (int f) -> float& { return d.noteMidi[at (f)]; };
            auto sounding = [&] (int f) { return d.amp[at (f)] > 0.0f; };
            for (int f = 0; f < F;)
            {
                if (! sounding (f)) { ++f; continue; }
                int end = f;
                while (end < F && sounding (end)) ++end;
                float cur = tag (f);
                for (int i = f + 1; i < end;)
                {
                    if (same (tag (i), cur)) { ++i; continue; }
                    int j = i;
                    while (j < end && same (tag (j), tag (i))) ++j;
                    bool claim = false;   // a note claimed the partial within the first 50 ms (a note's attack frames borrow tags, unclaimed)
                    for (int k = i; k < juce::jmin (j, i + hold); ++k) claim |= claimed[at (k)] != 0;
                    if (claim && j - i >= hold) cur = tag (i);
                    else for (int k = i; k < j; ++k) tag (k) = cur;
                    i = j;
                }
                f = end;
            }
        }
    }

    // ---- summary for the display: the most prominent pitch class over all notes ----
    struct Rec { float cents, hz; double w; };
    std::vector<Rec> recs;
    double total = 0.0, re = 0.0, im = 0.0;
    for (int f = 0; f < F; ++f)
        for (auto& n : notes[(size_t) f])
        {
            const double w = n.w * std::sqrt (frameE[(size_t) f] / eMax) * (n.hz < 250.0f ? 1.5 : 1.0);
            float cents = std::fmod (1200.0f * std::log2 (n.hz / c0), 1200.0f);   // cents above C
            if (cents < 0.0f) cents += 1200.0f;
            recs.push_back ({ cents, n.hz, w });
            total += w;
            re += w * std::cos (twoPi * cents / 100.0f);   // phase within a semitone -> the source's tuning offset
            im += w * std::sin (twoPi * cents / 100.0f);
        }
    if (total <= 0.0) return;
    const float offset = (float) (std::atan2 (im, re) / twoPi * 100.0);   // cents the source is off A440 (-50..50)
    auto wrap = [] (float c) { c = std::fmod (c + 600.0f, 1200.0f); if (c < 0.0f) c += 1200.0f; return c - 600.0f; };
    double chroma[12] {};
    for (auto& r : recs) chroma[((juce::roundToInt ((r.cents - offset) / 100.0f) % 12) + 12) % 12] += r.w;
    int root = 0;
    for (int i = 1; i < 12; ++i) if (chroma[i] > chroma[root]) root = i;
    double second = 0.0;
    for (int i = 0; i < 12; ++i) if (i != root) second = juce::jmax (second, chroma[i]);
    const float rootCents = (float) root * 100.0f + offset;
    double within = 0.0, sum = 0.0;
    for (auto& r : recs)
    {
        const float delta = wrap (r.cents - rootCents);
        if (std::abs (delta) <= 50.0f) { within += r.w; sum += r.w * delta; }
    }
    const float share = (float) (within / total);
    d.pitchConfidence = share;
    if (within <= 0.0 || share < 0.2f || second > 0.85 * chroma[root]) return;   // no prominent note
    float pcCents = rootCents + (float) (sum / within);
    pcCents = std::fmod (pcCents + 1200.0f, 1200.0f);
    std::vector<std::pair<int, double>> octs;
    for (auto& r : recs)
        if (std::abs (wrap (r.cents - rootCents)) <= 50.0f)
            octs.push_back ({ juce::roundToInt (std::log2 (r.hz / c0) - pcCents / 1200.0f), r.w });
    std::sort (octs.begin(), octs.end());
    double acc = 0.0;
    int oct = octs.back().first;
    for (auto& o : octs) { acc += o.second; if (acc >= 0.5 * within) { oct = o.first; break; } }
    d.detectedFundamentalHz = c0 * std::exp2 ((float) oct + pcCents / 1200.0f);
    juce::ignoreUnused (toHz);
}

std::shared_ptr<ResynthData> ResynthData::fromAudio (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& name)
{
    const Settings st;
    const int len = juce::jmin (buffer.getNumSamples(), (int) (sampleRate * 30.0));
    if (len < 2048 || buffer.getNumChannels() < 1) return {};

    auto d = std::make_shared<ResynthData>();
    d->name = name;
    d->isImage = false;
    d->sourceRate = sampleRate;
    const int channels = juce::jmin (2, buffer.getNumChannels());
    d->sourceAudio.setSize (channels, len);
    for (int ch = 0; ch < channels; ++ch)
        d->sourceAudio.copyFrom (ch, 0, buffer, ch, 0, len);
    const float* L = d->sourceAudio.getReadPointer (0);
    const float* R = d->sourceAudio.getReadPointer (channels > 1 ? 1 : 0);

    // Resolutions and the frequency ranges they own (window lengths double above 60 kHz)
    struct Res { int order; float lo, hi; };
    const int base = sampleRate > 60000.0 ? 1 : 0;
    const std::vector<Res> res { { 13 + base, 20.0f, 300.0f },              // 8192: bass partials a few Hz apart
                                 { 12 + base, 300.0f, 500.0f },             // 4096
                                 { 11 + base, 500.0f, (float) (sampleRate * 0.48) } };   // 2048
    std::vector<Stft> stfts (res.size());
    for (size_t i = 0; i < res.size(); ++i) stfts[i].init (res[i].order);

    const int hop = juce::jmax (64, (int) std::lround (sampleRate * st.hopSec));
    const double dt = hop / sampleRate;
    d->frameRate = sampleRate / hop;
    d->durationSec = (double) len / sampleRate;
    d->numFrames = juce::jmax (1, len / hop);
    d->numSlots = maxSlots;
    const size_t total = (size_t) d->numFrames * (size_t) maxSlots;
    d->freq.assign (total, 0.0f);
    d->amp.assign (total, 0.0f);
    d->pan.assign (total, 0.0f);
    d->phase.assign (total, 0.0f);
    d->stereoPhase.assign (total, 0.0f);
    std::vector<float> peakFreq (total, 0.0f);

    // ---- peaks and tracking ----
    std::vector<Peak> peaks;
    std::vector<int> lastActive ((size_t) maxSlots, -10);
    std::vector<float> trackFreq ((size_t) maxSlots, 0.0f), trackDb ((size_t) maxSlots, 0.0f);
    std::vector<bool> taken ((size_t) maxSlots, false);
    struct Cand { float cost; int slot, peak; };
    std::vector<Cand> cands;
    std::vector<bool> peakDone;

    for (int fr = 0; fr < d->numFrames; ++fr)
    {
        peaks.clear();
        const int centre = fr * hop;
        for (size_t i = 0; i < res.size(); ++i)
        {
            stfts[i].analyse (L, R, len, centre);
            findPeaks (stfts[i], sampleRate, res[i].lo, res[i].hi, (int) i, peaks);
        }

        // keep the strongest peaks within keepDb of the frame's loudest
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
        if (! peaks.empty())
        {
            const float floor = peaks.front().amp * std::pow (10.0f, -st.keepDb / 20.0f);
            size_t keep = 0;
            while (keep < peaks.size() && keep < (size_t) st.maxPeaks && peaks[keep].amp > floor) ++keep;
            peaks.resize (keep);
        }

        // Match peaks to tracks active in the previous frame (or the one before: a track may skip a frame).
        // Cost: frequency distance relative to the tolerance, plus level change; cheapest pairs are assigned first.
        cands.clear();
        for (int j = 0; j < (int) peaks.size(); ++j)
        {
            const auto& pk = peaks[(size_t) j];
            const float db = 20.0f * std::log10 (pk.amp + 1.0e-12f);
            for (int s = 0; s < maxSlots; ++s)
            {
                const int gap = fr - 1 - lastActive[(size_t) s];   // 0 = active last frame, 1 = skipped one
                if (gap < 0 || gap > st.maxGap) continue;
                const float tol = juce::jmax (trackFreq[(size_t) s] * st.tol, 8.0f) * (gap == 0 ? 1.0f : 1.5f);
                const float df = std::abs (pk.freq - trackFreq[(size_t) s]);
                if (df >= tol) continue;
                const float dl = (db - trackDb[(size_t) s]) / 10.0f;
                cands.push_back ({ (df / tol) * (df / tol) + st.levelCost * dl * dl + (gap == 0 ? 0.0f : 0.6f), s, j });
            }
        }
        std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.cost < b.cost; });
        std::fill (taken.begin(), taken.end(), false);
        peakDone.assign (peaks.size(), false);
        const size_t row = (size_t) fr * (size_t) maxSlots;
        auto place = [&] (int s, const Peak& pk)
        {
            taken[(size_t) s] = true;
            const size_t i = row + (size_t) s;
            d->freq[i] = pk.freq;
            d->amp[i] = pk.amp;
            d->pan[i] = pk.pan;
            d->phase[i] = pk.phase;
            d->stereoPhase[i] = pk.stereoPhase;
            peakFreq[i] = pk.freq;
        };
        for (auto& c : cands)
        {
            if (taken[(size_t) c.slot] || peakDone[(size_t) c.peak]) continue;
            const auto& pk = peaks[(size_t) c.peak];
            if (lastActive[(size_t) c.slot] == fr - 2)
            {
                // resumed after a skipped frame: fill it in between, with the phase the two frames imply
                const size_t prev = row - 2 * (size_t) maxSlots + (size_t) c.slot, mid = row - (size_t) maxSlots + (size_t) c.slot;
                const float expected = (d->freq[prev] + pk.freq) * (float) dt;   // cycles over two frames
                const float frac = pk.phase - d->phase[prev];
                const float cycles = frac + std::round (expected - frac);
                d->amp[mid] = 0.5f * (d->amp[prev] + pk.amp);
                d->freq[mid] = peakFreq[mid] = 0.5f * (d->freq[prev] + pk.freq);
                d->pan[mid] = 0.5f * (d->pan[prev] + pk.pan);
                d->stereoPhase[mid] = d->stereoPhase[prev];
                d->phase[mid] = d->phase[prev] + 0.5f * cycles;
                d->phase[mid] -= std::floor (d->phase[mid]);
            }
            place (c.slot, pk);
            peakDone[(size_t) c.peak] = true;
        }
        // births: unmatched peaks take slots that have been silent for at least two frames
        int next = 0;
        for (int j = 0; j < (int) peaks.size(); ++j)
        {
            if (peakDone[(size_t) j]) continue;
            while (next < maxSlots && (taken[(size_t) next] || lastActive[(size_t) next] >= fr - st.cooldown)) ++next;
            if (next >= maxSlots) break;
            place (next, peaks[(size_t) j]);
        }
        for (int s = 0; s < maxSlots; ++s)
            if (taken[(size_t) s])
            {
                lastActive[(size_t) s] = fr;
                trackFreq[(size_t) s] = d->freq[row + (size_t) s];
                trackDb[(size_t) s] = 20.0f * std::log10 (d->amp[row + (size_t) s] + 1.0e-12f);
            }
    }

    removeShortTracks (*d, st.minTrackFrames);
    intervalFrequencies (*d, peakFreq, dt);
    smoothIntervalFrequencies (*d, st.smoothLoCents, st.smoothHiCents);
    correctEnvelopes (*d, L, R, len, sampleRate, hop, st);
    constantStereoPhase (*d);
    fillInactive (*d);
    analyseResidual (*d, L, R, len, sampleRate, hop, st);

    d->maxAmp = 1.0e-9f;
    for (auto a : d->amp) d->maxAmp = juce::jmax (d->maxAmp, a);

    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);
    analysePitch (*d, mono.data(), len, sampleRate);
    d->detectedBpm = detectTempo (mono.data(), len, sampleRate);
    return d;
}
