#include "Voice.h"

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    amp.setSampleRate (sr);
    filterEnv.setSampleRate (sr);
    declickCoef = std::exp (-1.0f / (0.004f * (float) sr));
    for (auto& o : oscs) o.prepare (sr);
    for (auto& b : banks) b.prepare (sr);
    fadeBanks.resize ((size_t) P::numOscs);
    for (auto& b : fadeBanks) b.prepare (sr);
    posFadeSamples = juce::jmax (1, (int) (0.015 * sr));
    if (bankBuf == nullptr) bankBuf = std::make_unique<BankBuffers>();
    posFading = false;
    active = false;
}

void Voice::start (int n, float vel, const VoiceContext& c, float glideSec, bool glideFromCurrent)
{
    note = n;
    velocity = vel;
    targetPitch = (float) n;
    glideTime = glideSec;
    if (! (glideFromCurrent && active && glideSec > 0.0f))
        pitch = (float) n;

    amp.setParameters (c.amp);
    amp.noteOn();
    filterEnv.setParameters (c.filterEnv);
    filterEnv.noteOn();
    if (! active) { filter.reset(); filterEngaged = false; lastOutL = lastOutR = lastPreL = lastPreR = preDeclickL = preDeclickR = 0.0f; }
    else declickPending = true;
    // Values read only at note-on use the previous block's modulation.
    auto* ranges = c.destRanges;
    for (int i = 0; i < P::numMods; ++i)
    {
        auto mp = c.mod[i];
        mp.start = applyMod (ranges[P::modStartDst[i]], mp.start, lastOff[P::modStartDst[i]]);
        mods[i].trigger (mp, *c.rng, c.freeRunPhase[i]);
    }
    for (int i = 0; i < P::numOscs; ++i)
    {
        auto& o = c.osc[i];
        const int extra = P::DstO1Fine + i * 6;
        const float phase = applyMod (ranges[extra + 1], o.phase, lastOff[extra + 1]) / 360.0f;
        const float amount = applyMod (ranges[extra + 2], o.rand, lastOff[extra + 2]) * 0.01f;
        const int voices = (int) std::lround (applyMod (ranges[extra + 3], (float) o.uniVoices, lastOff[extra + 3]));
        oscs[i].resetPhases (voices, o.phaseMode, phase, amount, *c.rng);
        banks[i].resetPhases (voices, o.phaseMode, phase, amount, *c.rng);
        if (! active) { levels[i] = 0.0f; harmMoved[i] = 0; }
        if (harmMoved[i] != 0)
            banks[i].alignHarmonics (oscs[i].getPhases(), oscs[i].getIncrements(), oscs[i].getBassPhase(), oscs[i].getBassIncrement(),
                                     c.harm.stretch, harmMoved[i]);
    }
    if (! active) { drift.reset (*c.rng); rsLevel = 0.0f; }
    lastRsPos = juce::jlimit (0.0f, 1.0f, c.rsPos + lastOff[P::DstResPos]);
    rsTime = c.resynth != nullptr ? lastRsPos * c.resynth->durationSec : 0.0;
    posFading = false;
    std::fill (std::begin (fmOut), std::end (fmOut), 0.0f);
    active = true;
    held = true;
}

void Voice::legatoTo (int n, float vel, float glideSec)
{
    note = n;
    velocity = vel;
    targetPitch = (float) n;
    glideTime = glideSec;
    if (glideSec <= 0.0f) pitch = (float) n;
    held = true;
}

float Voice::partialGain (float hn, float f, const HarmParams& h, float tableMod, const VoiceContext& c)
{
    float g = 1.0f;

    // Harmonic tables (interpolated between harmonics for partials that are not exact harmonics)
    if (hn < (float) P::numHarmTable + 0.5f)
    {
        const float x = juce::jlimit (0.0f, (float) (P::numHarmTable - 1), hn - 1.0f);
        const int i0 = (int) x;
        const int i1 = juce::jmin (i0 + 1, P::numHarmTable - 1);
        const float t = x - (float) i0;
        const float gain = c.harmGain[i0] + (c.harmGain[i1] - c.harmGain[i0]) * t;
        const float depth = c.harmDepth[i0] + (c.harmDepth[i1] - c.harmDepth[i0]) * t;
        g *= juce::jmax (0.0f, gain + depth * tableMod);
    }

    // Odd/even balance: 1 on odd harmonics, 0 on even ones, smooth in between
    if (hn >= 1.5f && h.oddEven != 0.0f)
    {
        const float odd = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (hn - 1.0f));
        if (h.oddEven > 0.0f) g *= 1.0f - h.oddEven * odd;
        else                  g *= 1.0f + h.oddEven * (1.0f - odd);
    }

    // Spectral low-pass with a resonant bump around the cutoff
    const float r = f / h.cutoff;
    const float r2 = r * r, r4 = r2 * r2;
    float lp = 1.0f / std::sqrt (1.0f + r4 * r4);
    if (h.res > 0.0f)
    {
        const float d = std::log2 (juce::jmax (1.0e-6f, r));
        lp *= 1.0f + h.res * 7.0f * std::exp (-d * d / (2.0f * 0.13f * 0.13f));
    }
    g *= lp;

    return g;
}

void Voice::computeSpectrum (int wave, float f0, const HarmParams& h, float tableSrc, const VoiceContext& c, float* a, int& numHarm)
{
    numHarm = juce::jlimit (1, maxHarmonics, (int) (sr * 0.45 / f0));
    baseSpectrum (wave, a, numHarm);

    const float tableMod = h.tableAmt * tableSrc;
    for (int i = 0; i < numHarm; ++i)
        if (a[i] != 0.0f)
            a[i] *= partialGain ((float) (i + 1), (float) (i + 1) * f0, h, tableMod, c);
}

void Voice::render (float* L, float* R, int n, const VoiceContext& c)
{
    if (! active) return;
    auto* ranges = c.destRanges;

    // Modulation sources (rate modulation uses the previous block's matrix values)
    for (int i = 0; i < P::numMods; ++i)
    {
        auto mp = c.mod[i];
        // Rate modulation acts on whichever rate knob is showing: Hz when free, the sync rate when synced
        const int rateDst = P::modRateDst[i];
        const int syncDst = P::modSyncDst[i];
        if (mp.syncIndex >= 0)
        {
            if (lastOff[syncDst] != 0.0f)
            {
                const int k = juce::jlimit (0, P::numSyncRates - 1, (int) std::lround (applyMod (ranges[syncDst], (float) mp.syncIndex, lastOff[syncDst])));
                mp.rateHz = c.hostBpm / 60.0 / P::syncRateBeats[k];
            }
        }
        else if (lastOff[rateDst] != 0.0f)
        {
            mp.rateHz = applyMod (ranges[rateDst], (float) mp.rateHz, lastOff[rateDst]);
        }
        // Synced and not retriggered: follow the song position while the host plays
        double locked = -1.0;
        if (mp.syncIndex >= 0 && ! mp.retrig && c.hostPlaying)
            locked = ModUnit::songPhase (c.ppq, c.hostBpm / 60.0 / mp.rateHz, mp.start);
        const int smoothDst = P::modSmoothDst[i];
        if (lastOff[smoothDst] != 0.0f)
            mp.smoothMs = applyMod (ranges[smoothDst], mp.smoothMs, lastOff[smoothDst]);
        modValue[i] = mods[i].process (mp, *c.shapes[i], n, sr, *c.rng, locked);
    }
    // Resynth Level: the source's level at the playhead, Floor dB -> 0 and the loudest frame -> 1, times Gain,
    // then smoothed with separate attack and release times
    {
        float target = 0.0f;
        if (c.resynth != nullptr)
        {
            const double dur = c.resynth->durationSec;
            const double t = c.rsLoop && dur > 0.0 ? std::fmod (std::fmod (rsTime, dur) + dur, dur) : rsTime;
            if (t < dur && c.rslFloor < 0.0f)
                target = juce::jlimit (0.0f, 1.0f, (1.0f - c.resynth->levelDbAt (t) / c.rslFloor) * c.rslGain * 0.01f);
        }
        const float ms = target > rsLevel ? c.rslAttack : c.rslRelease;
        rsLevel = ms <= 0.0f ? target : rsLevel + (target - rsLevel) * (1.0f - std::exp (-(float) n / (ms * 0.001f * (float) sr)));
    }
    float src[P::NumSrc] {};
    for (int i = 0; i < P::numMods; ++i) src[P::modSrc[i]] = modValue[i];
    src[P::SrcRsLevel] = rsLevel;
    src[P::SrcAmpEnv] = envValue;
    src[P::SrcVel] = velocity;
    src[P::SrcModWheel] = c.modWheel;
    src[P::SrcMacro1] = c.macro1;
    src[P::SrcMacro2] = c.macro2;
    src[P::SrcAccent] = c.accent;
    src[P::SrcKey] = ((float) note - 60.0f) / 48.0f;
    src[P::SrcSH] = c.sh;

    float off[P::NumDst];
    evalMatrix (c, src, true, off);
    std::copy (std::begin (off), std::end (off), std::begin (lastOff));

    auto ampParams = c.amp;
    ampParams.attack = applyMod (ranges[P::DstAmpA], ampParams.attack, off[P::DstAmpA]);
    ampParams.decay = applyMod (ranges[P::DstAmpD], ampParams.decay, off[P::DstAmpD]);
    ampParams.sustain = applyMod (ranges[P::DstAmpS], ampParams.sustain, off[P::DstAmpS]);
    ampParams.release = applyMod (ranges[P::DstAmpR], ampParams.release, off[P::DstAmpR]);
    amp.setParameters (ampParams);

    // Filter (bypassed while it is fully open and unmodulated, so it costs nothing and changes nothing)
    const auto& fp = c.filter;
    const float fltCutoff = applyMod (ranges[P::DstFltCutoff], fp.cutoff, off[P::DstFltCutoff]);
    const float fltRes = applyMod (ranges[P::DstFltRes], fp.res, off[P::DstFltRes]);
    const float fltEnv = applyMod (ranges[P::DstFltEnv], fp.env, off[P::DstFltEnv]);
    const float fltKey = applyMod (ranges[P::DstFltKey], fp.key, off[P::DstFltKey]) * 0.01f;
    const float fltDrive = applyMod (ranges[P::DstFltDrive], fp.drive, off[P::DstFltDrive]);
    auto fenvParams = c.filterEnv;
    fenvParams.attack = applyMod (ranges[P::DstFltA], fenvParams.attack, off[P::DstFltA]);
    fenvParams.decay = applyMod (ranges[P::DstFltD], fenvParams.decay, off[P::DstFltD]);
    fenvParams.sustain = applyMod (ranges[P::DstFltS], fenvParams.sustain, off[P::DstFltS]);
    fenvParams.release = applyMod (ranges[P::DstFltR], fenvParams.release, off[P::DstFltR]);
    filterEnv.setParameters (fenvParams);
    // Bypassed only while the filter's own settings are fully open and nothing in the matrix modulates it;
    // once engaged during a note it stays engaged (switching in and out of bypass would click). Key Track's 0 %
    // is not exactly 0 after the parameter's step snapping (-200..200 in 0.1 steps), so it counts as off below 0.05 %.
    bool filterModulated = false;
    for (auto& slot : c.matrix)
        filterModulated |= slot.src != P::SrcNone && slot.dst >= P::DstFltCutoff && slot.dst <= P::DstFltR;
    if (! filterEngaged && (filterModulated || ! (fp.type == VoiceFilter::LowPass && fp.cutoff >= 19999.0f && fp.env == 0.0f
                                                  && std::abs (fp.key) < 0.05f && fp.drive == 0.0f && fp.res == 0.0f)))
    {
        filterEngaged = true;
        filter.reset();
    }
    const bool filterOn = filterEngaged;
    if (filterOn) filter.setup (sr, fp.type, fp.slope, fltRes, fltDrive);

    // Glide
    const float glideSec = glideTime > 0.0f ? applyMod (ranges[P::DstGlide], glideTime, off[P::DstGlide]) : 0.0f;
    if (glideSec > 0.0f && pitch != targetPitch)
    {
        const float k = 1.0f - std::exp (-(float) n / (glideSec * 0.33f * (float) sr));
        pitch += (targetPitch - pitch) * k;
        if (std::abs (targetPitch - pitch) < 0.001f) pitch = targetPitch;
    }
    else
    {
        pitch = targetPitch;
    }

    // Resynth playback position
    double rsT = rsTime, rsBlock = 0.0, fadeT = 0.0;
    bool posJumped = false;
    if (c.resynth != nullptr)
    {
        const double dur = c.resynth->durationSec;
        auto wrap = [&] (double t) { return c.rsLoop && dur > 0.0 ? std::fmod (std::fmod (t, dur) + dur, dur) : t; };
        // Moving Position (knob or modulation) restarts playback from the new position, so e.g. an S&H
        // on Position jumps to a new section on every step instead of shifting a running playhead.
        // The old position fades out under the new one (see fadeBanks).
        const float posNow = juce::jlimit (0.0f, 1.0f, c.rsPos + off[P::DstResPos]);
        if (std::abs (posNow - lastRsPos) > 0.001f && ! posFading)
        {
            fadeTime = rsTime;
            rsTime = posNow * dur;
            lastRsPos = posNow;
            posJumped = posFading = true;
            posFadeDone = 0;
        }
        rsT = wrap (rsTime);
        fadeT = wrap (fadeTime);
        const float bpm = applyMod (ranges[P::DstRsBpm], c.rsBpm, off[P::DstRsBpm]);
        const double speed = applyMod (ranges[P::DstRsSpeed], c.rsSpeed, off[P::DstRsSpeed])
                             * (c.rsSync ? c.hostBpm / juce::jmax (1.0f, bpm) : 1.0);
        rsBlock = speed * n / sr;
        rsTime += rsBlock;
        if (c.rsLoop && dur > 0.0 && rsTime >= dur) rsTime = std::fmod (rsTime, dur);
        fadeTime += rsBlock;
    }
    else
    {
        posFading = false;
    }
    // equal-power crossfade gains at the start and end of this block (in: new position, out: old one)
    float fadeIn0 = 1.0f, fadeIn1 = 1.0f, fadeOut0 = 0.0f, fadeOut1 = 0.0f;
    if (posFading)
    {
        const float halfPi = juce::MathConstants<float>::halfPi;
        const float x0 = (float) posFadeDone / (float) posFadeSamples;
        const float x1 = juce::jmin (1.0f, (float) (posFadeDone + n) / (float) posFadeSamples);
        fadeIn0 = std::sin (halfPi * x0); fadeIn1 = std::sin (halfPi * x1);
        fadeOut0 = std::cos (halfPi * x0); fadeOut1 = std::cos (halfPi * x1);
        posFadeDone += n;
    }
    const bool fadingThisBlock = posFading;
    if (posFading && posFadeDone >= posFadeSamples) posFading = false;   // this block finishes the fade

    HarmParams h = c.harm;
    h.stretch = applyMod (ranges[P::DstStretch], h.stretch, off[P::DstStretch]);
    h.oddEven = applyMod (ranges[P::DstOddEven], h.oddEven, off[P::DstOddEven]);
    h.cutoff = applyMod (ranges[P::DstCutoff], h.cutoff, off[P::DstCutoff]);
    h.res = applyMod (ranges[P::DstRes], h.res, off[P::DstRes]);
    const float prism = applyMod (ranges[P::DstPrism], h.prism, off[P::DstPrism]) * 0.01f;
    const float prismCenter = applyMod (ranges[P::DstPrismCenter], h.prismCenter, off[P::DstPrismCenter]);
    const float prismWidth = applyMod (ranges[P::DstPrismWidth], h.prismWidth, off[P::DstPrismWidth]);
    const float shimmer = applyMod (ranges[P::DstShimmer], h.shimmer, off[P::DstShimmer]) * 0.01f;
    if (shimmer > 0.0f)
    {
        // the additive oscillators read the drift of harmonics 2..64 only; the resynth partials read all of it
        bool anyResynth = false;
        for (auto& o : c.osc) anyResynth |= o.on && o.wave == P::WResynth;
        drift.advance (applyMod (ranges[P::DstShimmerRate], h.shimmerRate, off[P::DstShimmerRate]), n, sr, *c.rng,
                       anyResynth ? Mangle::Drift::size : PartialBank::numHarmonicSlots);
    }
    const bool mangling = prism > 0.0f || shimmer > 0.0f;
    auto setMangle = [&] (PartialBank::BlockParams& bp)
    {
        bp.prism = prism; bp.prismCenter = prismCenter; bp.prismWidth = prismWidth;
        bp.shimmer = shimmer; bp.drift = drift.values();
    };
    const float bendSemis = c.bendWheel * applyMod (ranges[P::DstBend], c.bendRange, off[P::DstBend]);
    const float rsTune = applyMod (ranges[P::DstRsTune], c.rsTune, off[P::DstRsTune]) * 0.01f;
    const float rsLift = applyMod (ranges[P::DstRsLift], c.rsLift, off[P::DstRsLift]) * 0.01f;
    const float rsGate = applyMod (ranges[P::DstRsGate], c.rsGate, off[P::DstRsGate]);
    h.tableAmt = applyMod (ranges[P::DstTableAmt], h.tableAmt, off[P::DstTableAmt]);
    const float tableSrc = src[P::tableModSrcMap[juce::jlimit (0, 4, h.tableSrc)]];

    float fmAmt[P::numOscs] {};
    int fmSrc[P::numOscs] {};
    bool running[P::numOscs] {};
    bool useBank[P::numOscs] {};
    bool useBoth[P::numOscs] {};   // wavetable + sine bank (Prism/Shimmer on an additive oscillator)
    bool toFilter[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i) toFilter[i] = c.osc[i].toFilter;
    float phaseOff[P::numOscs] {};

    // An oscillator used as an FM modulator must run even when its own level is zero.
    bool usedAsFm[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i)
        if (c.osc[i].on && c.osc[i].fmSrc > 0 && (c.osc[i].fmAmt > 0.0f || off[P::DstO1Fm + i * 4] != 0.0f))
            usedAsFm[c.osc[i].fmSrc - 1] = true;

    // Every oscillator's pitch, needed up front for FM anti-aliasing (the modulator may come later in the loop)
    float oscF0[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i)
    {
        auto& o = c.osc[i];
        const float fineHz = applyMod (ranges[P::DstO1Fine + i * 6], o.fine, off[P::DstO1Fine + i * 6]);
        const float pp = pitch + o.coarse + fineHz * 0.01f + off[P::DstO1Pitch + i * 4] * P::pitchModSemitones + bendSemis;
        oscF0[i] = o.wave == P::WNoise ? (float) sr * 0.25f : 440.0f * std::pow (2.0f, (pp - 69.0f) / 12.0f);
    }

    for (int i = 0; i < P::numOscs; ++i)
    {
        auto& o = c.osc[i];
        const int base = P::DstO1Level + i * 4;
        const float target = o.on ? applyMod (ranges[base], o.level, off[base]) : 0.0f;
        if (target <= 0.0f && levels[i] <= 0.0f && ! (o.on && usedAsFm[i])) { fmOut[i] = 0.0f; continue; }
        running[i] = true;

        const int extra = P::DstO1Fine + i * 6;   // fine, phase, rand, voices, blend, width
        const float fine = applyMod (ranges[extra], o.fine, off[extra]);
        float p = pitch + o.coarse + fine * 0.01f + off[base + 1] * P::pitchModSemitones + bendSemis;
        float f0 = juce::jlimit (8.0f, (float) sr * 0.45f, 440.0f * std::pow (2.0f, (p - 69.0f) / 12.0f));
        const float detune = applyMod (ranges[base + 2], o.uniDetune, off[base + 2]);
        const int voices = (int) std::lround (applyMod (ranges[extra + 3], (float) o.uniVoices, off[extra + 3]));
        const float blend = applyMod (ranges[extra + 4], o.uniBlend, off[extra + 4]) * 0.01f;
        const float width = applyMod (ranges[extra + 5], o.uniWidth, off[extra + 5]) * 0.01f;
        // Modulating Phase shifts the waveform while it plays (phase modulation)
        phaseOff[i] = off[extra + 1] != 0.0f ? (applyMod (ranges[extra + 1], o.phase, off[extra + 1]) - o.phase) / 360.0f : 0.0f;
        fmAmt[i] = applyMod (ranges[base + 3], o.fmAmt, off[base + 3]);
        fmSrc[i] = o.fmSrc;
        const float pan = applyMod (ranges[P::DstO1Pan + i], o.pan, off[P::DstO1Pan + i]) * 0.01f;
        const float bassFreq = applyMod (ranges[P::DstRsBassFreq], c.rsBassFreq, off[P::DstRsBassFreq]);

        useBank[i] = o.wave == P::WResynth;
        if (useBank[i])
        {
            // Resynthesis: one sine per analysed partial
            if (c.resynth == nullptr) { running[i] = false; levels[i] = 0.0f; fmOut[i] = 0.0f; continue; }
            PartialBank::BlockParams bp;
            bp.timeSec = rsT;
            bp.blockSec = rsBlock;
            rsPlayTime = rsT;
            bp.f0 = f0;
            bp.tune = rsTune;
            bp.gateDb = rsGate;
            bp.lift = rsLift;
            bp.stretch = h.stretch;
            bp.unison = voices;
            bp.detuneCents = detune;
            bp.detuneMode = o.uniMode;
            bp.blend = blend;
            bp.width = width;
            bp.phaseOffset = phaseOff[i];
            bp.bassFreq = bassFreq;
            bp.subLift = applyMod (ranges[P::DstRsSubLift], c.rsSubLift, off[P::DstRsSubLift]) * 0.01f;
            bp.bassMono = o.bassMono;
            bp.pan = pan;
            bp.fft = c.fft;
            setMangle (bp);
            if (o.fmSrc > 0)
            {
                // phase deviation is fmAmt * 2 cycles at full modulator level
                const float modHz = oscF0[o.fmSrc - 1];
                bp.fmModHz = modHz;
                bp.fmDevPerRatio = applyMod (ranges[base + 3], o.fmAmt, off[base + 3]) * 2.0f * juce::MathConstants<float>::twoPi * modHz;
            }
            const float tableMod = h.tableAmt * tableSrc;
            // At full level and velocity an audio source plays back at its original loudness.
            const float sourceGain = c.resynth->isImage ? 1.0f : 2.5f;
            const auto gainFn = [&] (float f, float hn) { return sourceGain * partialGain (hn, f, h, tableMod, c); };
            if (posJumped)
            {
                fadeBanks[(size_t) i] = banks[i];   // same sizes: copies without allocating
                banks[i].restart();
            }
            banks[i].setBlock (*c.resynth, bp, gainFn, n, levels[i] * fadeIn0, target * fadeIn1);
            if (fadingThisBlock)
            {
                auto fbp = bp;
                fbp.timeSec = fadeT;
                fadeBanks[(size_t) i].setBlock (*c.resynth, fbp, gainFn, n, levels[i] * fadeOut0, target * fadeOut1);
            }
        }
        else
        {
            oscs[i].setNoise (o.wave == P::WNoise);
            if (o.wave != P::WNoise)
            {
                int numHarm = 0;
                computeSpectrum (o.wave, f0, h, tableSrc, c, amps.data(), numHarm);
                if (mangling)
                {
                    // Harmonics that Prism/Shimmer move now: Shimmer moves all of them, Prism only those in its band
                    uint64_t moving = 0;
                    for (int hn = 2; hn <= juce::jmin (numHarm, Mangle::maxMovedHarmonic); ++hn)
                    {
                        const float m = h.stretch != 0.0f ? (float) hn * std::pow ((float) hn, h.stretch * 0.25f) : (float) hn;
                        if (shimmer > 0.0f || (prism > 0.0f && Mangle::prismWindow (m, prismCenter, prismWidth) > 0.0f))
                            moving |= uint64_t (1) << (hn - 2);
                    }
                    // Hand new ones over to the sine bank, starting exactly where the wavetable has them
                    if (const uint64_t fresh = moving & ~harmMoved[i])
                    {
                        banks[i].alignHarmonics (oscs[i].getPhases(), oscs[i].getIncrements(), oscs[i].getBassPhase(), oscs[i].getBassIncrement(),
                                                 h.stretch, fresh);
                        harmMoved[i] |= fresh;
                    }
                }
                const float* tableSpectrum = amps.data();
                if (harmMoved[i] != 0)
                {
                    useBoth[i] = true;
                    std::copy (amps.begin(), amps.begin() + numHarm, tableAmps.begin());
                    for (int hn = 2; hn <= juce::jmin (numHarm, Mangle::maxMovedHarmonic); ++hn)
                        if ((harmMoved[i] >> (hn - 2)) & 1u) tableAmps[(size_t) hn - 1] = 0.0f;
                    tableSpectrum = tableAmps.data();

                    PartialBank::BlockParams bp;
                    bp.f0 = f0;
                    bp.stretch = h.stretch;
                    bp.unison = voices;
                    bp.detuneCents = detune;
                    bp.detuneMode = o.uniMode;
                    bp.blend = blend;
                    bp.width = width;
                    bp.phaseOffset = phaseOff[i];
                    bp.bassFreq = bassFreq;
                    bp.bassMono = o.bassMono;
                    bp.pan = pan;
                    setMangle (bp);
                    if (o.fmSrc > 0)
                    {
                        bp.fmModHz = oscF0[o.fmSrc - 1];
                        bp.fmDevPerRatio = fmAmt[i] * 2.0f * juce::MathConstants<float>::twoPi * bp.fmModHz;
                    }
                    banks[i].setHarmonics (amps.data(), numHarm, harmMoved[i], bp, n, levels[i], target);
                }
                oscs[i].setSpectrum (tableSpectrum, numHarm, h.stretch, f0, *c.fft, c.work, o.bassMono ? bassFreq : 0.0f,
                                     juce::jlimit (1, maxUnison, voices) == 1);
            }
            oscs[i].setUnison (voices, detune, o.uniMode, blend, width, f0, pan);
            oscs[i].beginBlock (n, levels[i], target);
        }
        levels[i] = target;
    }

    // An oscillator's mono output is only needed when another oscillator uses it as its FM source
    bool needMono[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i)
        for (int j = 0; j < P::numOscs; ++j)
            needMono[i] |= running[j] && fmSrc[j] == i + 1 && fmAmt[j] != 0.0f;
    // FM into the oscillator (from a running source, with an amount): its source must be rendered first
    bool fmInput[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i)
        fmInput[i] = running[i] && fmSrc[i] > 0 && fmAmt[i] != 0.0f && running[fmSrc[i] - 1];

    // Every oscillator is rendered a block at a time, each after its FM source: its modulation is the source's mono
    // output one sample late, as the sample-by-sample pass below has it. Oscillators in an FM loop (each other's
    // source) need each other sample by sample; then everything takes that pass, as before.
    int order[P::numOscs], numOrdered = 0, numRunning = 0;
    bool ordered[P::numOscs] {};
    for (int i = 0; i < P::numOscs; ++i) numRunning += running[i] ? 1 : 0;
    for (int pass = 0; pass < P::numOscs; ++pass)
        for (int i = 0; i < P::numOscs; ++i)
            if (running[i] && ! ordered[i] && (! fmInput[i] || ordered[fmSrc[i] - 1]))
            {
                order[numOrdered++] = i;
                ordered[i] = true;
            }

    const float velGain = 0.4f * (0.4f + 0.6f * velocity);
    auto& mix = bankBuf->mix;
    for (auto& m : mix) std::fill_n (m, n, 0.0f);

    if (numOrdered == numRunning)
    {
        float lastOut[P::numOscs];   // every oscillator's last sample of the previous block
        std::copy (std::begin (fmOut), std::end (fmOut), std::begin (lastOut));
        for (int idx = 0; idx < numOrdered; ++idx)
        {
            const int i = order[idx];
            const float* pm = nullptr;   // this oscillator's phase modulation per sample
            if (fmInput[i])
            {
                const int src = fmSrc[i] - 1;
                const float* srcMono = bankBuf->out[src][2];
                auto& p = bankBuf->pm[i];
                p[0] = lastOut[src] * fmAmt[i] * 2.0f;   // the source's last sample of the previous block
                for (int s = 1; s < n; ++s) p[s] = srcMono[s - 1] * fmAmt[i] * 2.0f;
                std::fill (p + n, p + controlBlock, 0.0f);   // PartialBank::renderBlock reads in groups of 8
                pm = p;
            }
            float* outL = mix[toFilter[i] ? 0 : 2];
            float* outR = mix[toFilter[i] ? 1 : 3];
            float* mono = needMono[i] ? bankBuf->out[i][2] : nullptr;
            if (mono != nullptr) std::fill_n (mono, n, 0.0f);
            if (useBank[i])
            {
                banks[i].renderBlock (n, outL, outR, mono, bankBuf->scratch, pm);
                if (fadingThisBlock) fadeBanks[(size_t) i].renderBlock (n, outL, outR, mono, bankBuf->scratch, pm);
            }
            else
            {
                const float* pmTable = nullptr;   // a wavetable's modulation includes the Phase offset
                if (pm != nullptr || phaseOff[i] != 0.0f)
                {
                    for (int s = 0; s < n; ++s) bankBuf->pmTable[s] = (pm != nullptr ? pm[s] : 0.0f) + phaseOff[i];
                    pmTable = bankBuf->pmTable;
                }
                oscs[i].renderBlock (n, pmTable, outL, outR, mono, *c.rng);
                if (useBoth[i])
                {
                    // bank partials run at 1/sqrt2 (their mono output, for FM, is scaled back up)
                    float* bankMono = nullptr;
                    if (mono != nullptr) { bankMono = bankBuf->tmp; std::fill_n (bankMono, n, 0.0f); }
                    banks[i].renderBlock (n, outL, outR, bankMono, bankBuf->scratch, pm);
                    if (mono != nullptr)
                        for (int s = 0; s < n; ++s) mono[s] += juce::MathConstants<float>::sqrt2 * bankMono[s];
                }
            }
            fmOut[i] = mono != nullptr ? mono[n - 1] : 0.0f;
        }
    }
    else
    {
        // Sine banks that nothing phase-modulates (no FM into the oscillator) are rendered for the whole block at once.
        bool blockBank[P::numOscs] {};
        for (int i = 0; i < P::numOscs; ++i)
        {
            if (! running[i] || ! (useBank[i] || useBoth[i]) || (fmSrc[i] > 0 && fmAmt[i] != 0.0f)) continue;
            blockBank[i] = true;
            auto& o = bankBuf->out[i];
            for (auto& b : o) std::fill_n (b, n, 0.0f);
            float* mono = needMono[i] ? o[2] : nullptr;
            banks[i].renderBlock (n, o[0], o[1], mono, bankBuf->scratch);
            if (useBank[i] && fadingThisBlock)
                fadeBanks[(size_t) i].renderBlock (n, o[0], o[1], mono, bankBuf->scratch);
        }

        // Sine banks with FM are rendered for the whole block after the oscillator pass below records their phase
        // modulation, unless another oscillator needs their output sample by sample as its own FM source.
        bool deferBank[P::numOscs] {};
        bool anyDeferred = false;
        for (int i = 0; i < P::numOscs; ++i)
        {
            if (! running[i] || ! useBank[i] || blockBank[i]) continue;
            deferBank[i] = ! needMono[i];
            anyDeferred |= deferBank[i];
        }

        // Oscillator pass: the oscillators' sum per sample (FM needs them sample by sample)
        for (int s = 0; s < n; ++s)
        {
            float l = 0.0f, r = 0.0f, dryL = 0.0f, dryR = 0.0f;   // dry: oscillators that bypass the filter
            float out[P::numOscs] {};
            for (int i = 0; i < P::numOscs; ++i)
            {
                if (! running[i]) continue;
                float& oL = toFilter[i] ? l : dryL;
                float& oR = toFilter[i] ? r : dryR;
                const float pm = fmSrc[i] > 0 ? fmOut[fmSrc[i] - 1] * fmAmt[i] * 2.0f : 0.0f;
                if (deferBank[i]) { bankBuf->pm[i][s] = pm; continue; }
                if (blockBank[i])
                {
                    const auto& o = bankBuf->out[i];
                    oL += o[0][s];
                    oR += o[1][s];
                    out[i] = useBank[i] ? o[2][s] : oscs[i].tick (pm + phaseOff[i], oL, oR, *c.rng) + juce::MathConstants<float>::sqrt2 * o[2][s];
                    continue;
                }
                out[i] = useBank[i] ? banks[i].tick (pm, oL, oR) : oscs[i].tick (pm + phaseOff[i], oL, oR, *c.rng);
                if (useBoth[i]) out[i] += juce::MathConstants<float>::sqrt2 * banks[i].tick (pm, oL, oR);   // bank partials run at 1/sqrt2
                if (useBank[i] && fadingThisBlock) out[i] += fadeBanks[(size_t) i].tick (pm, oL, oR);
            }
            std::copy (std::begin (out), std::end (out), std::begin (fmOut));
            mix[0][s] = l; mix[1][s] = r; mix[2][s] = dryL; mix[3][s] = dryR;
        }
        if (anyDeferred)
        {
            for (int i = 0; i < P::numOscs; ++i)
            {
                if (! deferBank[i]) continue;
                auto& o = bankBuf->out[i];
                for (auto& b : o) std::fill_n (b, n, 0.0f);
                std::fill (bankBuf->pm[i] + n, bankBuf->pm[i] + controlBlock, 0.0f);   // renderBlock reads in groups of 8
                banks[i].renderBlock (n, o[0], o[1], nullptr, bankBuf->scratch, bankBuf->pm[i]);   // deferred: no one needs its mono output
                if (fadingThisBlock)
                    fadeBanks[(size_t) i].renderBlock (n, o[0], o[1], nullptr, bankBuf->scratch, bankBuf->pm[i]);
                const int side = toFilter[i] ? 0 : 2;
                for (int s = 0; s < n; ++s) { mix[side][s] += o[0][s]; mix[side + 1][s] += o[1][s]; }
                fmOut[i] = 0.0f;
            }
        }
    }

    // the filter's key tracking is constant over the block; without an envelope amount so is the whole cutoff
    const float fltKeyTerm = fltKey * (pitch - 60.0f) / 12.0f;
    const float fltStaticCutoff = fltCutoff * std::exp2 (fltKeyTerm);
    for (int s = 0; s < n; ++s)
    {
        const float env = amp.getNextSample();
        float l = mix[0][s], r = mix[1][s];
        const float dryL = mix[2][s], dryR = mix[3][s];
        const float fe = filterEnv.getNextSample();
        if (filterOn)
        {
            // Pre-filter de-click: the restarted oscillators step the filter's input, which the filter would pass
            // as a transient after the post-gain stage below has already measured its (near zero) offset.
            if (declickPending)
            {
                preDeclickL = lastPreL - l;
                preDeclickR = lastPreR - r;
            }
            l += preDeclickL;
            r += preDeclickR;
            preDeclickL *= declickCoef;
            preDeclickR *= declickCoef;
            lastPreL = l;
            lastPreR = r;
            filter.process (l, r, fltEnv == 0.0f ? fltStaticCutoff : fltCutoff * std::exp2 (fltEnv * fe * 8.0f + fltKeyTerm));
        }
        l += dryL;
        r += dryR;
        const float g = env * velGain;
        envValue = env;
        float outL = l * g, outR = r * g;
        if (declickPending)
        {
            declickL = lastOutL - outL;
            declickR = lastOutR - outR;
            declickPending = false;
        }
        outL += declickL;
        outR += declickR;
        declickL *= declickCoef;
        declickR *= declickCoef;
        lastOutL = outL;
        lastOutR = outR;
        L[s] += outL;
        R[s] += outR;
    }

    if (! amp.isActive()) { active = false; envValue = 0.0f; lastOutL = lastOutR = declickL = declickR = 0.0f;
                             lastPreL = lastPreR = preDeclickL = preDeclickR = 0.0f; }
}
