#include "SpectralOsc.h"

FFTBank::FFTBank()
{
    std::vector<float> buf ((size_t) (2 << maxOrder), 0.0f);
    for (int order = minOrder; order <= maxOrder; ++order)
    {
        ffts.push_back (std::make_unique<juce::dsp::FFT> (order));

        // Calibrate scaling/sign once: put a unit value in bin 1's imaginary part and
        // measure the resulting sine at its quarter-cycle peak.
        const int n = 1 << order;
        std::fill (buf.begin(), buf.begin() + 2 * n, 0.0f);
        buf[3] = 1.0f;
        ffts.back()->performRealOnlyInverseTransform (buf.data());
        float peak = buf[(size_t) (n / 4)];
        scales.push_back (std::abs (peak) > 1.0e-12f ? 1.0f / peak : 1.0f);
    }
}

void baseSpectrum (int wave, float* a, int numHarm)
{
    const float pi = juce::MathConstants<float>::pi;
    std::fill (a, a + numHarm, 0.0f);
    for (int i = 0; i < numHarm; ++i)
    {
        const int h = i + 1;
        const bool odd = (h & 1) != 0;
        switch (wave)
        {
            case 0: a[i] = h == 1 ? 1.0f : 0.0f; break;                                               // sine
            case 1: a[i] = odd ? (8.0f / (pi * pi)) / (float) (h * h) * (((h - 1) / 2) % 2 ? -1.0f : 1.0f) : 0.0f; break; // triangle
            case 2: a[i] = (2.0f / pi) / (float) h; break;                                            // saw
            case 3: a[i] = odd ? (4.0f / pi) / (float) h : 0.0f; break;                               // square
            case 4: a[i] = (4.0f / (pi * (float) h)) * std::sin (pi * (float) h * 0.25f) * 0.7f; break; // 25% pulse
            default: break;
        }
    }
}

void buildWaveTable (WaveTable& t, const float* amps, int numHarm, float stretch, float f0, double sampleRate,
                     const FFTBank& fft, float* work)
{
    const int cycles = std::abs (stretch) > 1.0e-4f ? stretchCycles : 1;
    const float limit = (float) (sampleRate * 0.43);

    // Gather (bin, amplitude) pairs below Nyquist.
    int bins[maxHarmonics];
    int count = 0, maxBin = 0;
    for (int i = 0; i < numHarm; ++i)
    {
        bins[i] = -1;
        if (amps[i] == 0.0f) continue;
        const float h = (float) (i + 1);
        const float mult = cycles == 1 ? h : h * std::pow (h, stretch * 0.25f);
        if (mult * f0 >= limit) continue;
        const int b = cycles == 1 ? i + 1 : (int) std::lround (mult * (float) cycles);
        if (b < 1 || b >= maxTableSize / 2 - 1) continue;
        bins[i] = b;
        maxBin = juce::jmax (maxBin, b);
        ++count;
    }

    if (count == 0)
    {
        t.silent = true;
        t.size = 256;
        t.cycles = 1;
        t.cycleShift = 8;
        std::fill (t.data.begin(), t.data.begin() + 257, 0.0f);
        return;
    }

    int size = juce::nextPowerOfTwo (4 * (maxBin + 1));
    size = juce::jlimit (1 << FFTBank::minOrder, maxTableSize, size);
    const int order = (int) std::round (std::log2 ((double) size));

    std::fill (work, work + 2 * size, 0.0f);
    const float scale = fft.sineScale (order);
    for (int i = 0; i < numHarm; ++i)
        if (bins[i] > 0)
            work[2 * bins[i] + 1] += amps[i] * scale;

    fft.inverse (order, work);

    std::copy (work, work + size, t.data.begin());
    t.data[(size_t) size] = t.data[0];
    t.size = size;
    t.cycles = cycles;
    t.cycleShift = order - (cycles == 1 ? 0 : 3);
    t.silent = false;
}

void SpectralOsc::prepare (double sampleRate)
{
    sr = sampleRate;
    lastNumHarm = -1;
}

void SpectralOsc::resetPhases (int voices, int phaseMode, float startPhase, float amount, juce::Random& rng)
{
    const int n = juce::jlimit (1, maxUnison, voices);
    for (int k = 0; k < maxUnison; ++k)
        phases[k] = Unison::initialPhase (k, n, phaseMode, startPhase, amount, rng);
    centre = n / 2;
    bassPhase = phases[centre];
    snapGains = true;
}

void SpectralOsc::setUnison (int voices, float detuneCents, int detuneMode, float blend, float width, float f0, float pan)
{
    const int n = juce::jlimit (1, maxUnison, voices);
    // the previous block ended exactly on its targets
    std::copy (std::begin (targetL), std::end (targetL), std::begin (gainL));
    std::copy (std::begin (targetR), std::end (targetR), std::begin (gainR));

    float mul[maxUnison];
    std::fill (std::begin (targetL), std::end (targetL), 0.0f);
    std::fill (std::begin (targetR), std::end (targetR), 0.0f);
    Unison::layout (n, detuneCents, detuneMode, blend, width, mul, targetL, targetR);
    std::copy (std::begin (incTarget), std::end (incTarget), std::begin (incs));   // previous block ended here
    for (int k = 0; k < n; ++k)
        incTarget[k] = (float) (f0 * mul[k] / sr);
    float panL, panR;
    Unison::panGains (pan, panL, panR);
    for (int k = 0; k < n; ++k)
    {
        targetL[k] *= panL;
        targetR[k] *= panR;
    }
    // Bass voice: the level of a single centred voice, so its loudness matches the unison voices it replaces
    bassL = bassTargetL;
    bassR = bassTargetR;
    bassInc = bassIncTarget;
    bassTargetL = bassTargetR = juce::MathConstants<float>::sqrt2 * 0.5f;
    panGL = panTgtL;
    panGR = panTgtR;
    panTgtL = panL;
    panTgtR = panR;
    bassIncTarget = (float) (f0 / sr);
    centre = n / 2;
    if (snapGains)
    {
        std::copy (std::begin (targetL), std::end (targetL), std::begin (gainL));
        std::copy (std::begin (targetR), std::end (targetR), std::begin (gainR));
        std::copy (std::begin (incTarget), std::end (incTarget), std::begin (incs));
        bassL = bassTargetL;
        bassR = bassTargetR;
        bassInc = bassIncTarget;
        panGL = panTgtL;
        panGR = panTgtR;
        snapGains = false;
    }
    // keep rendering voices that are fading out
    numVoices = n;
    for (int k = maxUnison - 1; k >= n; --k)
        if (gainL[k] != 0.0f || gainR[k] != 0.0f) { numVoices = k + 1; break; }
    norm = 1.0f / (float) n;
}

void SpectralOsc::setSpectrum (const float* amps, int numHarm, float stretch, float f0, const FFTBank& fft, float* work,
                               float bassFreq, bool singleVoice)
{
    // Split into the unison part and the Bass Mono part
    float* uni = splitAmps.data();
    float* bass = uni + maxHarmonics;
    const bool stretched = std::abs (stretch) > 1.0e-4f;
    for (int i = 0; i < numHarm; ++i)
    {
        uni[i] = amps[i];
        bass[i] = 0.0f;
        if (bassFreq > 0.0f && amps[i] != 0.0f)
        {
            const float h = (float) (i + 1);
            const float f = f0 * (stretched ? h * std::pow (h, stretch * 0.25f) : h);
            float b, u;
            Unison::bassSplit (f, bassFreq, singleVoice, b, u);
            uni[i] = amps[i] * u;
            bass[i] = amps[i] * b;
        }
    }

    if (numHarm == lastNumHarm && stretch == lastStretch)
    {
        float diff = 0.0f;
        for (int i = 0; i < numHarm; ++i)
            diff = juce::jmax (diff, juce::jmax (std::abs (uni[i] - lastAmps[(size_t) i]),
                                                 std::abs (bass[i] - lastAmps[(size_t) (maxHarmonics + i)])));
        if (diff < 1.0e-4f) return;
    }

    std::copy (uni, uni + numHarm, lastAmps.begin());
    std::copy (bass, bass + numHarm, lastAmps.begin() + maxHarmonics);
    lastNumHarm = numHarm;
    lastStretch = stretch;

    curTable = 1 - curTable;
    buildWaveTable (tables[curTable], uni, numHarm, stretch, f0, sr, fft, work);
    buildWaveTable (bassTables[curTable], bass, numHarm, stretch, f0, sr, fft, work);
    pendingFade = true;
}

void SpectralOsc::renderBlock (int n, const float* pm, float* outL, float* outR, float* mono, juce::Random& rng) noexcept
{
    jassert (n <= maxBlock);
    float lev[maxBlock];
    for (int k = 0; k < n; ++k)
    {
        lev[k] = level;
        level += levelInc;
    }

    if (noise)
    {
        for (int k = 0; k < n; ++k)
        {
            float nl = rng.nextFloat() * 2.0f - 1.0f;
            float nr = rng.nextFloat() * 2.0f - 1.0f;
            outL[k] += nl * lev[k] * 0.5f * panGL;
            outR[k] += nr * lev[k] * 0.5f * panGR;
            panGL += dPanL;
            panGR += dPanR;
            if (mono != nullptr) mono[k] += (nl + nr) * 0.25f;
        }
        return;
    }

    const WaveTable& cur = tables[curTable];
    const WaveTable& prev = tables[1 - curTable];
    float fade[maxBlock];
    for (int k = 0; k < n; ++k)
    {
        fade[k] = xfade;
        xfade = juce::jmax (0.0f, xfade - xfadeInc);
    }
    const float wrap = (float) stretchCycles;
    float bassOut[maxBlock];
    if (bassOn)
    {
        for (int k = 0; k < n; ++k)
        {
            const float ph = bassPhase + (pm != nullptr ? pm[k] : 0.0f);
            float s = bassTables[curTable].read (ph);
            if (fade[k] > 0.0f) s += (bassTables[1 - curTable].read (ph) - s) * fade[k];
            bassPhase += bassInc;
            bassInc += dBassInc;
            if (bassPhase >= wrap) bassPhase -= wrap;
            bassOut[k] = s;
            outL[k] += s * bassL * lev[k];
            outR[k] += s * bassR * lev[k];
            bassL += dBassL;
            bassR += dBassR;
        }
    }
    float monoSum[maxBlock];
    if (mono != nullptr) std::fill_n (monoSum, n, 0.0f);
    for (int v = 0; v < numVoices; ++v)
    {
        float ph = phases[v], inc = incs[v], gL = gainL[v], gR = gainR[v];
        const float dInc = dIncs[v], dL = dgL[v], dR = dgR[v];
        for (int k = 0; k < n; ++k)
        {
            const float p = ph + (pm != nullptr ? pm[k] : 0.0f);
            float s = cur.read (p);
            if (fade[k] > 0.0f) s += (prev.read (p) - s) * fade[k];
            ph += inc;
            inc += dInc;
            if (ph >= wrap) ph -= wrap;
            if (mono != nullptr) monoSum[k] += s;
            outL[k] += s * gL * lev[k];
            outR[k] += s * gR * lev[k];
            gL += dL;
            gR += dR;
        }
        phases[v] = ph; incs[v] = inc; gainL[v] = gL; gainR[v] = gR;
    }
    if (mono != nullptr)
        for (int k = 0; k < n; ++k) mono[k] += monoSum[k] * norm + (bassOn ? bassOut[k] : 0.0f);
}

void SpectralOsc::beginBlock (int numSamples, float levelStart, float levelEnd)
{
    if (pendingFade)
    {
        xfade = 1.0f;
        xfadeInc = 1.0f / (float) juce::jmax (1, numSamples);
        pendingFade = false;
    }
    else
    {
        xfade = 0.0f;
    }
    level = levelStart;
    levelInc = (levelEnd - levelStart) / (float) juce::jmax (1, numSamples);
    const float inv = 1.0f / (float) juce::jmax (1, numSamples);

    const bool bassNow = ! bassTables[curTable].silent || (xfade > 0.0f && ! bassTables[1 - curTable].silent);
    if (bassNow && ! bassOn) bassPhase = phases[centre];   // starts in step with the centre voice
    bassOn = bassNow;
    dBassL = (bassTargetL - bassL) * inv;
    dBassR = (bassTargetR - bassR) * inv;
    dBassInc = (bassIncTarget - bassInc) * inv;
    dPanL = (panTgtL - panGL) * inv;
    dPanR = (panTgtR - panGR) * inv;
    for (int k = 0; k < maxUnison; ++k)
    {
        dgL[k] = (targetL[k] - gainL[k]) * inv;
        dgR[k] = (targetR[k] - gainR[k]) * inv;
        dIncs[k] = (incTarget[k] - incs[k]) * inv;
    }
}
