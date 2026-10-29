#include "PluginProcessor.h"
#include "PluginEditor.h"

#if defined (JucePlugin_Build_VST3) && JucePlugin_Build_VST3
 #include "pluginterfaces/base/funknown.h"
 #include "pluginterfaces/vst/ivsteditcontroller.h"
#endif

ElukkaAudioProcessor::ElukkaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ElukkaSynth", P::createLayout()),
      presets (*this)
{
    resetHarmTables();
    resetFxOrder();
    shapes[0].setPoints (ModShape::preset (5));   // Mod 1 starts as a decay envelope
    pattern = SeqPattern::factory()[(size_t) SeqPattern::initIndex()];
    cacheParams();
    work.assign ((size_t) maxTableSize * 2 + 16, 0.0f);
    presets.refresh();
    presets.load (0);   // start on the Init preset
    presets.clearHistory();
}

ElukkaAudioProcessor::~ElukkaAudioProcessor()
{
   #if defined (JucePlugin_Build_VST3) && JucePlugin_Build_VST3
    if (auto* handler = static_cast<Steinberg::Vst::IComponentHandler2*> (hostComponentHandler2))
        handler->release();
   #endif
}

void ElukkaAudioProcessor::Vst3Extensions::setIComponentHandler (Steinberg::FUnknown* handler)
{
   #if defined (JucePlugin_Build_VST3) && JucePlugin_Build_VST3
    if (auto* old = static_cast<Steinberg::Vst::IComponentHandler2*> (owner.hostComponentHandler2))
    {
        old->release();
        owner.hostComponentHandler2 = nullptr;
    }

    if (handler != nullptr)
    {
        Steinberg::Vst::IComponentHandler2* handler2 = nullptr;
        if (handler->queryInterface (Steinberg::Vst::IComponentHandler2::iid, (void**) &handler2) == Steinberg::kResultOk)
            owner.hostComponentHandler2 = handler2;
    }
   #else
    juce::ignoreUnused (handler);
   #endif
}

bool ElukkaAudioProcessor::beginHostGroupEdit()
{
   #if defined (JucePlugin_Build_VST3) && JucePlugin_Build_VST3
    if (auto* handler = static_cast<Steinberg::Vst::IComponentHandler2*> (hostComponentHandler2))
    {
        const auto result = handler->startGroupEdit();
        return result == Steinberg::kResultOk || result == Steinberg::kResultTrue;
    }
   #endif
    return false;
}

void ElukkaAudioProcessor::endHostGroupEdit()
{
   #if defined (JucePlugin_Build_VST3) && JucePlugin_Build_VST3
    if (auto* handler = static_cast<Steinberg::Vst::IComponentHandler2*> (hostComponentHandler2))
        handler->finishGroupEdit();
   #endif
}

void ElukkaAudioProcessor::resetHarmTables()
{
    harmGain.fill (1.0f);
    harmDepth.fill (0.0f);
}

void ElukkaAudioProcessor::cacheParams()
{
    for (int i = 0; i < P::numOscs; ++i)
    {
        auto& o = oscRaw[i];
        o.on = raw (P::osc (i, "on")); o.wave = raw (P::osc (i, "wave")); o.level = raw (P::osc (i, "level"));
        o.coarse = raw (P::osc (i, "coarse")); o.fine = raw (P::osc (i, "fine")); o.phase = raw (P::osc (i, "phase"));
        o.rand = raw (P::osc (i, "rand")); o.phaseMode = raw (P::osc (i, "phaseMode")); o.uniMode = raw (P::osc (i, "uniMode")); o.uniVoices = raw (P::osc (i, "uniVoices")); o.uniDetune = raw (P::osc (i, "uniDetune"));
        o.uniBlend = raw (P::osc (i, "uniBlend")); o.uniWidth = raw (P::osc (i, "uniWidth")); o.fmSrc = raw (P::osc (i, "fmSrc")); o.fmAmt = raw (P::osc (i, "fmAmt"));
        o.pan = raw (P::osc (i, "pan")); o.bassMono = raw (P::osc (i, "bassMono")); o.toFilter = raw (P::osc (i, "toFilter"));
    }
    for (int i = 0; i < P::numMods; ++i)
    {
        auto& m = modRaw[i];
        m.mode = raw (P::mod (i, "mode")); m.rate = raw (P::mod (i, "rate")); m.sync = raw (P::mod (i, "sync"));
        m.syncRate = raw (P::mod (i, "syncRate")); m.start = raw (P::mod (i, "start")); m.retrig = raw (P::mod (i, "retrig")); m.smooth = raw (P::mod (i, "smooth"));
    }
    for (int i = 0; i < P::numMatrixSlots; ++i)
        mxRaw[i] = { raw (P::mx (i, "src")), raw (P::mx (i, "dst")), raw (P::mx (i, "amt")) };

    pMaster = raw ("master"); pPoly = raw ("polyMode"); pGlide = raw ("glide"); pBend = raw ("bendRange");
    pA = raw ("ampA"); pD = raw ("ampD"); pS = raw ("ampS"); pR = raw ("ampR");
    hmStretch = raw ("hm_stretch"); hmOddEven = raw ("hm_oddEven"); hmCutoff = raw ("hm_cutoff"); hmRes = raw ("hm_res");
    hmPrism = raw ("hm_prism"); hmPrismCenter = raw ("hm_prismCenter"); hmPrismWidth = raw ("hm_prismWidth");
    hmShimmer = raw ("hm_shimmer"); hmShimmerRate = raw ("hm_shimmerRate");
    hmTableSrc = raw ("hm_tableSrc"); hmTableAmt = raw ("hm_tableAmt");
    rsTune = raw ("rs_tune"); rsGate = raw ("rs_gate"); rsLift = raw ("rs_lift"); rsSpeed = raw ("rs_speed"); rsSync = raw ("rs_sync"); rsBpm = raw ("rs_bpm");
    rsPos = raw ("rs_pos"); rsLoop = raw ("rs_loop");
    fxfOn = raw ("fxf_on"); fxfType = raw ("fxf_type"); fxfSlope = raw ("fxf_slope");
    fxfCutoff = raw ("fxf_cutoff"); fxfRes = raw ("fxf_res"); fxfDrive = raw ("fxf_drive");
    sqOn = raw ("sq_on"); sqRetrig = raw ("sq_retrig"); sqRate = raw ("sq_rate"); sqGate = raw ("sq_gate"); sqSlide = raw ("sq_slide"); sqAccent = raw ("sq_accent");
    distOn = raw ("dist_on"); distType = raw ("dist_type"); distDrive = raw ("dist_drive"); distMix = raw ("dist_mix");
    combOn = raw ("comb_on"); combFreq = raw ("comb_freq"); combFb = raw ("comb_fb"); combMix = raw ("comb_mix");
    eqOn = raw ("eq_on"); eqLow = raw ("eq_low"); eqMidF = raw ("eq_midF"); eqMid = raw ("eq_mid"); eqHigh = raw ("eq_high");
    dlyOn = raw ("dly_on"); dlyDiv = raw ("dly_div"); dlyFb = raw ("dly_fb"); dlyMix = raw ("dly_mix"); dlyPp = raw ("dly_pp");
    revOn = raw ("rev_on"); revSize = raw ("rev_size"); revDecay = raw ("rev_decay"); revTone = raw ("rev_tone");
    revMod = raw ("rev_mod"); revMode = raw ("rev_mode"); revMix = raw ("rev_mix");
    macro1 = raw ("macro1"); macro2 = raw ("macro2");
    eqLowF = raw ("eq_lowF"); eqLowQ = raw ("eq_lowQ"); eqMidQ = raw ("eq_midQ"); eqHighF = raw ("eq_highF"); eqHighQ = raw ("eq_highQ");
    combLp = raw ("comb_lp"); combHp = raw ("comb_hp"); dlySync = raw ("dly_sync"); dlyTime = raw ("dly_time");
    dlyLp = raw ("dly_lp"); dlyHp = raw ("dly_hp");
    fltType = raw ("flt_type"); fltSlope = raw ("flt_slope"); fltCutoff = raw ("flt_cutoff"); fltRes = raw ("flt_res");
    fltEnv = raw ("flt_env"); fltKey = raw ("flt_key"); fltDrive = raw ("flt_drive");
    fltA = raw ("flt_A"); fltD = raw ("flt_D"); fltS = raw ("flt_S"); fltR = raw ("flt_R"); shRate = raw ("sh_syncRate");
    ottOn = raw ("ott_on"); ottAmount = raw ("ott_amount"); ottSpeed = raw ("ott_speed");
    shSmooth = raw ("sh_smooth"); shRetrig = raw ("sh_retrig");
    rsSubLift = raw ("rs_subLift"); rsBassFreq = raw ("rs_bassFreq");

    for (int d = 1; d < P::NumDst; ++d)
        destRanges[d] = apvts.getParameterRange (P::dests[d].paramID);
}

bool ElukkaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ElukkaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    fx.prepare (sr, juce::jmax (samplesPerBlock, Voice::controlBlock));
    seq.reset();
    seqEvents.reserve (256);
    heldNotes.reserve (128);
}

void ElukkaAudioProcessor::setResynth (std::shared_ptr<ResynthData> d)
{
    // Keep the previous data alive on the message thread so the audio thread never frees it. Once the audio thread
    // has moved on from an old source (its block copy is released), this holds the last reference and frees it here.
    retiredResynth.push_back (std::atomic_load (&resynth));
    std::atomic_store (&resynth, std::move (d));
    ++resynthVersion;
    retiredResynth.erase (std::remove_if (retiredResynth.begin(), retiredResynth.end(),
                                          [] (const std::shared_ptr<ResynthData>& r) { return r == nullptr || r.use_count() == 1; }),
                          retiredResynth.end());
    bumpUiVersion();
}

static bool on (std::atomic<float>* p) { return p->load() > 0.5f; }
static int  idx (std::atomic<float>* p) { return (int) std::lround (p->load()); }

void ElukkaAudioProcessor::buildContext (double bpm)
{
    ctx.sampleRate = sr;
    for (int i = 0; i < P::numOscs; ++i)
    {
        auto& o = oscRaw[i];
        auto& c = ctx.osc[i];
        c.on = on (o.on); c.wave = idx (o.wave); c.level = o.level->load(); c.coarse = o.coarse->load();
        c.fine = o.fine->load(); c.phase = o.phase->load(); c.rand = o.rand->load();
        c.phaseMode = idx (o.phaseMode); c.uniMode = idx (o.uniMode);
        c.uniVoices = idx (o.uniVoices); c.uniDetune = o.uniDetune->load();
        c.uniBlend = o.uniBlend->load(); c.uniWidth = o.uniWidth->load();
        c.fmSrc = idx (o.fmSrc); c.fmAmt = o.fmAmt->load();
        c.pan = o.pan->load(); c.bassMono = on (o.bassMono); c.toFilter = on (o.toFilter);
    }
    auto& h = ctx.harm;
    h.stretch = hmStretch->load(); h.oddEven = hmOddEven->load(); h.cutoff = hmCutoff->load(); h.res = hmRes->load();
    h.prism = hmPrism->load(); h.prismCenter = hmPrismCenter->load(); h.prismWidth = hmPrismWidth->load();
    h.shimmer = hmShimmer->load(); h.shimmerRate = hmShimmerRate->load();
    h.tableSrc = idx (hmTableSrc); h.tableAmt = hmTableAmt->load();

    for (int i = 0; i < P::numMods; ++i)
    {
        auto& m = modRaw[i];
        auto& c = ctx.mod[i];
        c.mode = idx (m.mode);
        c.syncIndex = on (m.sync) ? juce::jlimit (0, P::numSyncRates - 1, idx (m.syncRate)) : -1;
        c.rateHz = c.syncIndex >= 0 ? bpm / 60.0 / P::syncRateBeats[c.syncIndex] : (double) m.rate->load();
        c.start = m.start->load();
        c.retrig = on (m.retrig);
        c.smoothMs = m.smooth->load();
        ctx.shapes[i] = &shapes[i];
        ctx.freeRunPhase[i] = globalMods[i].getPhase();
    }
    ctx.harmGain = harmGain.data();
    ctx.harmDepth = harmDepth.data();
    for (int i = 0; i < P::numMatrixSlots; ++i)
        ctx.matrix[i] = { idx (mxRaw[i].src), idx (mxRaw[i].dst), mxRaw[i].amt->load() };
    ctx.destRanges = destRanges;
    ctx.modWheel = modWheel;
    ctx.macro1 = macro1->load();
    ctx.macro2 = macro2->load();
    ctx.bendWheel = bendSemis;
    ctx.bendRange = pBend->load();

    // Held from block to block; when it replaces an older copy, the old source is still held by `retiredResynth`,
    // so it is never freed here.
    if (const int v = resynthVersion.load(); v != blockResynthVersion)
    {
        blockResynthVersion = v;
        blockResynth = std::atomic_load (&resynth);
    }
    ctx.resynth = blockResynth.get();
    ctx.rsTune = rsTune->load();
    ctx.rsGate = rsGate->load();
    ctx.rsLift = rsLift->load();
    ctx.rsSpeed = rsSpeed->load();
    ctx.rsBpm = rsBpm->load();
    ctx.rsSync = on (rsSync);
    ctx.hostBpm = bpm;
    ctx.rsSubLift = rsSubLift->load(); ctx.rsBassFreq = rsBassFreq->load();
    ctx.rsPos = rsPos->load();
    ctx.rsLoop = on (rsLoop);

    ctx.filter = { idx (fltType), idx (fltSlope), fltCutoff->load(), fltRes->load(), fltEnv->load(), fltKey->load(), fltDrive->load() };
    ctx.filterEnv.attack = fltA->load(); ctx.filterEnv.decay = fltD->load();
    ctx.filterEnv.sustain = fltS->load(); ctx.filterEnv.release = fltR->load();
    ctx.amp.attack = pA->load(); ctx.amp.decay = pD->load(); ctx.amp.sustain = pS->load(); ctx.amp.release = pR->load();
    ctx.fft = &fft;
    ctx.work = work.data();
    ctx.rng = &rng;

    auto& f = fxBase;
    f.distOn = on (distOn); f.distType = idx (distType); f.distDrive = distDrive->load(); f.distMix = distMix->load();
    f.combOn = on (combOn); f.combFreq = combFreq->load(); f.combFb = combFb->load(); f.combMix = combMix->load();
    f.eqOn = on (eqOn); f.eqLow = eqLow->load(); f.eqMidF = eqMidF->load(); f.eqMid = eqMid->load(); f.eqHigh = eqHigh->load();
    f.dlyOn = on (dlyOn); f.dlyBeats = P::dlyDivBeats[juce::jlimit (0, 7, idx (dlyDiv))]; f.dlyFb = dlyFb->load();
    f.dlyMix = dlyMix->load(); f.dlyPingPong = on (dlyPp);
    f.eqLowF = eqLowF->load(); f.eqLowQ = eqLowQ->load(); f.eqMidQ = eqMidQ->load(); f.eqHighF = eqHighF->load(); f.eqHighQ = eqHighQ->load();
    f.combLp = combLp->load(); f.combHp = combHp->load();
    f.dlySync = on (dlySync); f.dlyMs = dlyTime->load(); f.dlyLp = dlyLp->load(); f.dlyHp = dlyHp->load();
    f.ottOn = on (ottOn); f.ottAmount = ottAmount->load() * 0.01f; f.ottSpeed = ottSpeed->load() * 0.01f;
    f.revOn = on (revOn);
    f.rev.size = revSize->load(); f.rev.decay = revDecay->load(); f.rev.tone = revTone->load() * 0.01f;
    f.rev.mod = revMod->load() * 0.01f; f.rev.mode = idx (revMode); f.rev.mix = revMix->load();
    f.fltOn = on (fxfOn); f.fltType = idx (fxfType); f.fltSlope = idx (fxfSlope);
    f.fltCutoff = fxfCutoff->load(); f.fltRes = fxfRes->load(); f.fltDrive = fxfDrive->load();
}

int ElukkaAudioProcessor::findVoice()
{
    int best = -1;
    for (int i = 0; i < P::maxVoices; ++i)
        if (! voices[i].active) return i;
    // steal the oldest released voice, otherwise the oldest voice
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    for (int i = 0; i < P::maxVoices; ++i)
        if (! voices[i].held && voices[i].age < oldest) { oldest = voices[i].age; best = i; }
    if (best >= 0) return best;
    for (int i = 0; i < P::maxVoices; ++i)
        if (voices[i].age < oldest) { oldest = voices[i].age; best = i; }
    return juce::jmax (0, best);
}

void ElukkaAudioProcessor::voiceOn (int note, float vel, bool legato, float glideSec, bool forceMono)
{
    lastNote = note;
    lastVelocity = vel;
    if (on (shRetrig))
    {
        // new random value right now, and the steps are counted from this note
        shBeat = 0.0;
        shStep = std::numeric_limits<int64_t>::min();
    }
    const int mode = idx (pPoly);
    if (forceMono)
    {
        // Sequencer: one note at a time, but each new note gets its own voice so the previous one's release rings
        // out; a slide glides the current voice into the new note instead.
        if (legato && voices[seqVoice].active)
            voices[seqVoice].legatoTo (note, vel, glideSec);
        else
        {
            if (voices[seqVoice].held) voices[seqVoice].release();
            seqVoice = findVoice();
            voices[seqVoice].start (note, vel, ctx, glideSec, false);
            for (int i = 0; i < P::numMods; ++i) globalMods[i].trigger (ctx.mod[i], rng, globalMods[i].getPhase());
        }
        voices[seqVoice].age = ++ageCounter;
        return;
    }
    if (mode != 0)
    {
        auto& v = voices[0];
        if (legato && v.active)
            v.legatoTo (note, vel, glideSec);
        else
        {
            v.start (note, vel, ctx, glideSec, true);
            for (int i = 0; i < P::numMods; ++i) globalMods[i].trigger (ctx.mod[i], rng, globalMods[i].getPhase());
        }
        v.age = ++ageCounter;
        return;
    }
    auto& v = voices[findVoice()];
    v.start (note, vel, ctx, 0.0f, false);
    v.age = ++ageCounter;
    for (int i = 0; i < P::numMods; ++i) globalMods[i].trigger (ctx.mod[i], rng, globalMods[i].getPhase());
}

void ElukkaAudioProcessor::releaseNote (int note)
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
            v.release();
}

void ElukkaAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    const bool seqOn = on (sqOn);
    const int mode = idx (pPoly);

    if (m.isNoteOn())
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
        heldNotes.push_back (m.getNoteNumber());
        if (seqOn)
        {
            if (on (sqRetrig)) seq.retrigger();
            return;
        }
        const bool legato = mode == 2 && heldNotes.size() > 1;
        voiceOn (m.getNoteNumber(), m.getFloatVelocity(), legato, pGlide->load(), false);
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
        if (seqOn) return;
        if (mode == 0)
            releaseNote (n);
        else if (voices[0].note == n)
        {
            if (! heldNotes.empty())
                voiceOn (heldNotes.back(), lastVelocity, true, pGlide->load(), false);
            else
                voices[0].release();
        }
    }
    else if (m.isController() && m.getControllerNumber() == 1)
    {
        modWheel = (float) m.getControllerValue() / 127.0f;
    }
    else if (m.isPitchWheel())
    {
        bendSemis = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clear();
        for (auto& v : voices) v.release();
    }
}

void ElukkaAudioProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, bool hostPlaying, double ppq)
{
    ctx.hostPlaying = hostPlaying;
    ctx.ppq = ppq;
    float* L = buffer.getWritePointer (0, start);
    float* R = buffer.getWritePointer (1, start);

    // Global modulation (for FX and master destinations)
    float src[P::NumSrc] {};
    for (int i = 0; i < P::numMods; ++i)
    {
        // synced LFOs with Retrig off follow the song position while the host plays
        const auto& m = ctx.mod[i];
        const double locked = m.syncIndex >= 0 && ! m.retrig && hostPlaying
                                  ? ModUnit::songPhase (ppq, P::syncRateBeats[m.syncIndex], m.start) : -1.0;
        src[P::modSrc[i]] = globalMods[i].process (m, shapes[i], num, sr, rng, locked);
        globalModValue[i] = src[P::modSrc[i]];
    }
    float env = 0.0f;
    uint64_t newest = 0;
    for (auto& v : voices)
        if (v.active && v.age >= newest) { newest = v.age; env = v.envValue; }
    src[P::SrcAmpEnv] = env;
    src[P::SrcVel] = lastVelocity;
    src[P::SrcModWheel] = modWheel;
    src[P::SrcMacro1] = ctx.macro1;
    src[P::SrcMacro2] = ctx.macro2;
    src[P::SrcAccent] = seq.accentValue;
    src[P::SrcKey] = ((float) lastNote - 60.0f) / 48.0f;
    ctx.accent = seq.accentValue;

    // S&H: a new random value at every step of the synced rate
    const int shIdx = (int) std::lround (applyMod (destRanges[P::DstShRate], (float) idx (shRate), globalOff[P::DstShRate]));
    const double shDiv = P::syncRateBeats[juce::jlimit (0, P::numSyncRates - 1, shIdx)];
    const auto step = (int64_t) std::floor (shBeat / shDiv + 1.0e-9);
    if (step != shStep)
    {
        shStep = step;
        shValue = rng.nextFloat();   // 0..1
    }
    shBeat += num * bpm / 60.0 / sr;
    // Glide towards the new value over a few milliseconds so modulated parameters don't click.
    // Glide towards the new value so modulated parameters don't click (Smooth knob; 0 = sharp steps)
    const float shMs = applyMod (destRanges[P::DstShSmooth], shSmooth->load(), globalOff[P::DstShSmooth]);
    shSmoothed = shMs <= 0.0f ? shValue : shSmoothed + (shValue - shSmoothed) * (1.0f - std::exp (-(float) num / (shMs * 0.001f * (float) sr)));
    ctx.sh = shSmoothed;
    src[P::SrcSH] = shSmoothed;
    uiSh = shValue;

    for (auto& v : voices)
        if (v.active) v.render (L, R, num, ctx);

    float off[P::NumDst];
    evalMatrix (ctx, src, false, off);
    FXParams f = fxBase;
    f.distDrive = applyMod (destRanges[P::DstDistDrive], f.distDrive, off[P::DstDistDrive]);
    f.distMix = applyMod (destRanges[P::DstDistMix], f.distMix, off[P::DstDistMix]);
    f.combFreq = applyMod (destRanges[P::DstCombFreq], f.combFreq, off[P::DstCombFreq]);
    f.combFb = applyMod (destRanges[P::DstCombFb], f.combFb, off[P::DstCombFb]);
    f.combMix = applyMod (destRanges[P::DstCombMix], f.combMix, off[P::DstCombMix]);
    f.eqMid = applyMod (destRanges[P::DstEqMid], f.eqMid, off[P::DstEqMid]);
    f.eqLow = applyMod (destRanges[P::DstEqLow], f.eqLow, off[P::DstEqLow]);
    f.eqMidF = applyMod (destRanges[P::DstEqMidF], f.eqMidF, off[P::DstEqMidF]);
    f.eqHigh = applyMod (destRanges[P::DstEqHigh], f.eqHigh, off[P::DstEqHigh]);
    f.rev.tone = applyMod (destRanges[P::DstRevDamp], f.rev.tone * 100.0f, off[P::DstRevDamp]) * 0.01f;   // slot: Reverb Tone
    f.rev.decay = applyMod (destRanges[P::DstRevWidth], f.rev.decay, off[P::DstRevWidth]);              // slot: Reverb Decay
    f.eqLowF = applyMod (destRanges[P::DstEqLowF], f.eqLowF, off[P::DstEqLowF]);
    f.eqLowQ = applyMod (destRanges[P::DstEqLowQ], f.eqLowQ, off[P::DstEqLowQ]);
    f.eqMidQ = applyMod (destRanges[P::DstEqMidQ], f.eqMidQ, off[P::DstEqMidQ]);
    f.eqHighF = applyMod (destRanges[P::DstEqHighF], f.eqHighF, off[P::DstEqHighF]);
    f.eqHighQ = applyMod (destRanges[P::DstEqHighQ], f.eqHighQ, off[P::DstEqHighQ]);
    f.combLp = applyMod (destRanges[P::DstCombLp], f.combLp, off[P::DstCombLp]);
    f.combHp = applyMod (destRanges[P::DstCombHp], f.combHp, off[P::DstCombHp]);
    f.dlyMs = applyMod (destRanges[P::DstDlyTime], f.dlyMs, off[P::DstDlyTime]);
    if (off[P::DstDlyDiv] != 0.0f)   // synced time: modulation steps through the note values (the delay glides between them)
        f.dlyBeats = P::dlyDivBeats[juce::jlimit (0, 7, (int) std::lround (applyMod (destRanges[P::DstDlyDiv], (float) idx (dlyDiv), off[P::DstDlyDiv])))];
    f.dlyLp = applyMod (destRanges[P::DstDlyLp], f.dlyLp, off[P::DstDlyLp]);
    f.dlyHp = applyMod (destRanges[P::DstDlyHp], f.dlyHp, off[P::DstDlyHp]);
    f.rev.mod = applyMod (destRanges[P::DstRevLp], f.rev.mod * 100.0f, off[P::DstRevLp]) * 0.01f;      // slot: Reverb Mod
    f.ottAmount = applyMod (destRanges[P::DstOttAmount], f.ottAmount * 100.0f, off[P::DstOttAmount]) * 0.01f;
    f.ottSpeed = applyMod (destRanges[P::DstOttSpeed], f.ottSpeed * 100.0f, off[P::DstOttSpeed]) * 0.01f;
    std::copy (std::begin (off), std::end (off), std::begin (globalOff));
    f.dlyFb = applyMod (destRanges[P::DstDlyFb], f.dlyFb, off[P::DstDlyFb]);
    f.dlyMix = applyMod (destRanges[P::DstDlyMix], f.dlyMix, off[P::DstDlyMix]);
    f.rev.size = applyMod (destRanges[P::DstRevSize], f.rev.size, off[P::DstRevSize]);
    f.rev.mix = applyMod (destRanges[P::DstRevMix], f.rev.mix, off[P::DstRevMix]);
    f.fltCutoff = applyMod (destRanges[P::DstFxfCutoff], f.fltCutoff, off[P::DstFxfCutoff]);
    f.fltRes = applyMod (destRanges[P::DstFxfRes], f.fltRes, off[P::DstFxfRes]);
    f.fltDrive = applyMod (destRanges[P::DstFxfDrive], f.fltDrive, off[P::DstFxfDrive]);
    FXChain::Order order;
    for (int i = 0; i < P::NumFx; ++i) order[(size_t) i] = fxOrder[(size_t) i].load();
    fx.process (L, R, num, f, bpm, order);

    const float master = juce::Decibels::decibelsToGain (applyMod (destRanges[P::DstMaster], pMaster->load(), off[P::DstMaster]), -48.0f);
    // ramp the master gain across the chunk so modulating it never steps
    const float masterStart = lastMasterGain < 0.0f ? master : lastMasterGain;
    const float masterInc = (master - masterStart) / (float) num;
    for (int i = 0; i < num; ++i)
    {
        const float gm = masterStart + masterInc * (float) (i + 1);
        L[i] *= gm;
        R[i] *= gm;
    }
    lastMasterGain = master;
    for (int b = 0; b < 3; ++b) uiOttDb[b] = fx.ott.meterDb[b];

    // UI feedback
    {
        const Voice* newestVoice = nullptr;
        for (auto& v : voices)
            if (v.active && (newestVoice == nullptr || v.age > newestVoice->age)) newestVoice = &v;
        bool usesResynth = false;
        for (auto& o : ctx.osc) usesResynth |= o.on && o.wave == P::WResynth;
        uiRsPos = (newestVoice != nullptr && usesResynth && ctx.resynth != nullptr && ctx.resynth->durationSec > 0.0)
                      ? (float) (newestVoice->rsPlayTime / ctx.resynth->durationSec) : -1.0f;
        uiPlaying = newestVoice != nullptr;
        for (int i = 0; i < P::numMods; ++i) uiMod[i] = newestVoice != nullptr ? newestVoice->modValue[i] : 0.0f;
    }
}

void ElukkaAudioProcessor::renderRange (juce::AudioBuffer<float>& buffer, int start, int num, double bpm, bool hostPlaying, double ppq)
{
    if (num <= 0) return;
    if (hostPlaying && ! on (shRetrig)) shBeat = ppq;   // keep S&H steps locked to the host's beat grid (unless retriggered by notes)

    const bool seqOn = on (sqOn);
    seqEvents.clear();
    if (seqOn)
    {
        const int root = heldNotes.empty() ? 60 : heldNotes.back();
        seq.process (pattern, num, sr, bpm, hostPlaying, ppq, ! heldNotes.empty(), root,
                     P::seqRateBeats[juce::jlimit (0, 4, idx (sqRate))],
                     applyMod (destRanges[P::DstSqGate], sqGate->load(), globalOff[P::DstSqGate]),
                     applyMod (destRanges[P::DstSqAccent], sqAccent->load(), globalOff[P::DstSqAccent]), seqEvents);
    }

    int pos = 0;
    size_t ev = 0;
    while (pos < num)
    {
        while (ev < seqEvents.size() && seqEvents[ev].offset <= pos)
        {
            auto& e = seqEvents[ev++];
            if (e.on) voiceOn (e.note, e.velocity, e.legato, e.legato ? applyMod (destRanges[P::DstSqSlide], sqSlide->load(), globalOff[P::DstSqSlide]) : 0.0f, true);
            else voices[seqVoice].release();
        }
        int end = num;
        if (ev < seqEvents.size()) end = juce::jmin (end, seqEvents[ev].offset);
        end = juce::jmin (end, pos + Voice::controlBlock);
        if (end <= pos) end = pos + 1;
        renderChunk (buffer, start + pos, end - pos, bpm, hostPlaying, ppq + pos * bpm / 60.0 / sr);
        pos = end;
    }
    while (ev < seqEvents.size())
    {
        auto& e = seqEvents[ev++];
        if (e.on) voiceOn (e.note, e.velocity, e.legato, e.legato ? applyMod (destRanges[P::DstSqSlide], sqSlide->load(), globalOff[P::DstSqSlide]) : 0.0f, true);
        else voices[seqVoice].release();
    }
}

void ElukkaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto p = pos->getPpqPosition()) ppq = *p;
            playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }
    uiHostBpm = bpm;

    buildContext (bpm);

    // Switching sequencer or voice mode: silence everything to avoid hanging notes.
    const bool seqOn = on (sqOn);
    const int polyMode = idx (pPoly);
    if (seqOn != lastSeqOn || polyMode != lastPolyMode)
    {
        for (auto& v : voices) v.release();
        seq.reset();
        lastSeqOn = seqOn;
        lastPolyMode = polyMode;
    }

    const double beatsPerSample = bpm / 60.0 / sr;
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        renderRange (buffer, pos, t - pos, bpm, playing, ppq + pos * beatsPerSample);
        pos = juce::jmax (pos, t);
        handleMidi (meta.getMessage());
    }
    renderRange (buffer, pos, numSamples - pos, bpm, playing, ppq + pos * beatsPerSample);
}

juce::ValueTree ElukkaAudioProcessor::saveState (bool includeResynth)
{
    auto state = apvts.copyState();
    state.setProperty ("syncRates", P::numSyncRates, nullptr);   // saves without this used the list before "8 Bars" was added
    state.setProperty ("dlyDivs", 2, nullptr);                    // saves without this used the old delay time order
    for (auto type : { "Shape1", "Shape2", "Shape3", "HarmTables", "Pattern", "Resynth" })
        state.removeChild (state.getChildWithName (type), nullptr);

    for (int i = 0; i < P::numMods; ++i)
        state.appendChild (shapes[i].toTree ("Shape" + juce::String (i + 1)), nullptr);

    juce::ValueTree ht ("HarmTables");
    juce::String g, d;
    for (int i = 0; i < P::numHarmTable; ++i)
    {
        g << juce::String (harmGain[(size_t) i], 4) << " ";
        d << juce::String (harmDepth[(size_t) i], 4) << " ";
    }
    ht.setProperty ("gain", g.trim(), nullptr);
    ht.setProperty ("depth", d.trim(), nullptr);
    state.appendChild (ht, nullptr);

    state.appendChild (pattern.toTree(), nullptr);

    juce::StringArray order;
    for (auto& f : fxOrder) order.add (P::fxKeys[f.load()]);
    state.setProperty ("fxOrder", order.joinIntoString (","), nullptr);
    if (includeResynth)
        if (auto rs = getResynth())
            state.appendChild (rs->toTree(), nullptr);
    return state;
}

void ElukkaAudioProcessor::loadState (const juce::ValueTree& t, bool includeResynth)
{
    if (! t.isValid() || ! t.hasType (apvts.state.getType())) return;

    auto params = t.createCopy();
    for (auto type : { "Shape1", "Shape2", "Shape3", "HarmTables", "Pattern", "Resynth" })
        params.removeChild (params.getChildWithName (type), nullptr);
    // Older saves used a Freeverb-style reverb (Size = room size, Damping, Width, wet low/high pass). Its room size
    // set both the space and the tail length, so it becomes Size plus an equivalent Decay; Damping becomes Tone.
    if (auto oldDamp = params.getChildWithProperty ("id", "rev_damp"); oldDamp.isValid())
    {
        auto setValue = [&params] (const juce::String& id, float v)
        {
            auto child = params.getChildWithProperty ("id", id);
            if (! child.isValid()) { child = juce::ValueTree ("PARAM"); child.setProperty ("id", id, nullptr); params.appendChild (child, nullptr); }
            child.setProperty ("value", v, nullptr);
        };
        const auto oldSize = params.getChildWithProperty ("id", "rev_size");
        const float size = oldSize.isValid() ? (float) oldSize["value"] : 0.6f;
        if (! params.getChildWithProperty ("id", "rev_decay").isValid()) setValue ("rev_decay", P::reverbDecayForOldSize (size));
        if (! params.getChildWithProperty ("id", "rev_tone").isValid()) setValue ("rev_tone", 100.0f * (1.0f - (float) oldDamp["value"]));
        for (auto id : { "rev_damp", "rev_width", "rev_lp", "rev_hp" })
            params.removeChild (params.getChildWithProperty ("id", id), nullptr);
    }

    // Saves from before "8 Bars" was added at the slow end: every synced rate index moves up by one.
    if (! t.hasProperty ("syncRates"))
        for (auto id : { "m1_syncRate", "m2_syncRate", "sh_syncRate" })
            if (auto r = params.getChildWithProperty ("id", id); r.isValid())
                r.setProperty ("value", (float) r["value"] + 1.0f, nullptr);
    // Saves from before the synced delay times were sorted longest to shortest: remap the index.
    if (! t.hasProperty ("dlyDivs"))
        if (auto r = params.getChildWithProperty ("id", "dly_div"); r.isValid())
            r.setProperty ("value", (float) P::dlyDivFromOld ((int) std::lround ((float) r["value"])), nullptr);
    // Saves from before S&H Retrigger existed behaved as if it were off (it is on by default now).
    if (! params.getChildWithProperty ("id", "sh_retrig").isValid())
    {
        juce::ValueTree r ("PARAM");
        r.setProperty ("id", "sh_retrig", nullptr);
        r.setProperty ("value", 0.0f, nullptr);
        params.appendChild (r, nullptr);
    }

    // Older saves stored synced rates as indices into a shorter list (and under other IDs).
    auto migrateRate = [&params] (const juce::String& oldId, const juce::String& newId)
    {
        auto old = params.getChildWithProperty ("id", oldId);
        if (! old.isValid()) return;
        auto now = params.getChildWithProperty ("id", newId);
        if (! now.isValid()) { now = juce::ValueTree ("PARAM"); now.setProperty ("id", newId, nullptr); params.appendChild (now, nullptr); }
        now.setProperty ("value", (float) P::syncRateFromOldDiv ((int) std::lround ((float) old["value"])), nullptr);
        params.removeChild (old, nullptr);
    };
    migrateRate ("m1_div", "m1_syncRate");
    migrateRate ("m2_div", "m2_syncRate");
    migrateRate ("sh_rate", "sh_syncRate");

    // Older saves had one Resynth Bass Mono switch; it now lives on each oscillator.
    if (auto old = params.getChildWithProperty ("id", "rs_bassMono"); old.isValid())
    {
        const bool wasOn = (float) old["value"] > 0.5f;
        params.removeChild (old, nullptr);
        for (int i = 0; i < P::numOscs && wasOn; ++i)
        {
            auto wave = params.getChildWithProperty ("id", P::osc (i, "wave"));
            auto mono = params.getChildWithProperty ("id", P::osc (i, "bassMono"));
            if (wave.isValid() && (int) std::lround ((float) wave["value"]) == P::WResynth)
            {
                if (! mono.isValid()) { mono = juce::ValueTree ("PARAM"); mono.setProperty ("id", P::osc (i, "bassMono"), nullptr); params.appendChild (mono, nullptr); }
                mono.setProperty ("value", 1.0f, nullptr);
            }
        }
    }
    apvts.replaceState (params);

    shapes[0].fromTree (t.getChildWithName ("Shape1"));
    shapes[1].fromTree (t.getChildWithName ("Shape2"));
    // saves from before Mod 3 existed have no Shape3: it starts as the default sine
    if (auto s3 = t.getChildWithName ("Shape3"); s3.isValid()) shapes[2].fromTree (s3);
    else shapes[2].setPoints (ModShape::preset (0));

    resetHarmTables();
    auto ht = t.getChildWithName ("HarmTables");
    if (ht.isValid())
    {
        auto g = juce::StringArray::fromTokens (ht["gain"].toString(), " ", "");
        auto d = juce::StringArray::fromTokens (ht["depth"].toString(), " ", "");
        for (int i = 0; i < P::numHarmTable; ++i)
        {
            if (i < g.size()) harmGain[(size_t) i] = juce::jlimit (0.0f, 1.0f, g[i].getFloatValue());
            if (i < d.size()) harmDepth[(size_t) i] = juce::jlimit (-1.0f, 1.0f, d[i].getFloatValue());
        }
    }

    // FX order: unknown keys are ignored; effects missing from older saves go to their default position
    resetFxOrder();
    auto keys = juce::StringArray::fromTokens (t["fxOrder"].toString(), ",", "");
    std::vector<int> order;
    for (auto& k : keys)
        for (int i = 0; i < P::NumFx; ++i)
            if (k == P::fxKeys[i] && std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
    if (! order.empty())
    {
        for (int pos = 0; pos < P::NumFx; ++pos)
        {
            const int fx = P::defaultFxOrder[pos];
            if (std::find (order.begin(), order.end(), fx) == order.end())
                order.insert (order.begin() + juce::jmin (pos, (int) order.size()), fx);
        }
        for (int i = 0; i < P::NumFx; ++i) fxOrder[(size_t) i] = order[(size_t) i];
    }

    auto pt = t.getChildWithName ("Pattern");
    if (pt.isValid()) pattern.fromTree (pt);

    if (includeResynth)
    {
        // the state's own sample/image, or none (loading a preset or project without one clears the old one)
        auto rs = t.getChildWithName ("Resynth");
        setResynth (rs.isValid() ? ResynthData::fromTree (rs) : nullptr);
    }

    bumpUiVersion();
}

void ElukkaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = saveState();
    state.setProperty ("presetName", presets.currentName, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ElukkaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        loadState (t);
        presets.currentName = t["presetName"].toString();
        if (t.hasProperty ("uiScale")) uiScale = juce::jlimit (0.6f, 2.0f, (float) t["uiScale"]);
        presets.currentIndex = -1;
        for (int i = 0; i < (int) presets.entries.size(); ++i)
            if (presets.entries[(size_t) i].name == presets.currentName)
                presets.currentIndex = i;
        presets.clearHistory();
    }
}

juce::AudioProcessorEditor* ElukkaAudioProcessor::createEditor()
{
    return new ElukkaAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ElukkaAudioProcessor();
}
