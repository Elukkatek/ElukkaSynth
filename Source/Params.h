#pragma once
#include <JuceHeader.h>

// Parameter IDs, choice lists and the modulation source/destination tables.
namespace P
{
inline const juce::StringArray waveNames   { "Sine", "Triangle", "Saw", "Square", "Pulse", "Noise", "Resynth" };
enum Wave { WSine, WTriangle, WSaw, WSquare, WPulse, WNoise, WResynth };

inline const juce::StringArray polyModes   { "Poly", "Mono", "Legato" };
inline const juce::StringArray modModes    { "LFO", "S&H Random", "Envelope" };
enum ModMode { MLfo, MSampleHold, MEnvelope };

// Synced rates for the LFOs and S&H, slowest to fastest (dotted and triplet values sit between the straight ones),
// so the rate knob and its modulation move smoothly from slow to fast.
inline const juce::StringArray syncRateNames { "8 Bars", "4 Bars", "2 Bars D", "2 Bars", "1 Bar D", "1 Bar", "1/2 D", "1/2", "1/4 D", "1/2 T",
                                               "1/4", "1/8 D", "1/4 T", "1/8", "1/16 D", "1/8 T", "1/16", "1/32 D", "1/16 T", "1/32" };
inline const float syncRateBeats[] { 32.0f, 16.0f, 12.0f, 8.0f, 6.0f, 4.0f, 3.0f, 2.0f, 1.5f, 4.0f / 3.0f,
                                     1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 0.375f, 1.0f / 3.0f, 0.25f, 0.1875f, 1.0f / 6.0f, 0.125f };
constexpr int numSyncRates = 20;
inline int syncRateIndex (float beats)
{
    for (int i = 0; i < numSyncRates; ++i)
        if (std::abs (syncRateBeats[i] - beats) < 1.0e-4f) return i;
    return 10;   // 1/4
}
// Older saves stored the rate as an index into this list (Mod "div" and "sh_rate").
inline const float oldSyncDivBeats[] { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                       2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f, 1.5f, 0.75f };
inline int syncRateFromOldDiv (int old) { return syncRateIndex (oldSyncDivBeats[juce::jlimit (0, 12, old)]); }

inline const juce::StringArray fmSrcNames  { "Off", "Osc 1", "Osc 2", "Osc 3" };
inline const juce::StringArray reverbModes { "Smooth", "Vintage", "Lush" };
// The old Freeverb reverb's room size (0..1) as a decay time: its comb feedback was 0.7 + 0.28 * size over
// ~30 ms combs, so T60 = 3 * 0.03 / -log10 (feedback), damped a little in practice.
inline float reverbDecayForOldSize (float size)
{
    const float g = 0.7f + 0.28f * juce::jlimit (0.0f, 1.0f, size);
    return juce::jlimit (0.2f, 15.0f, 0.8f * 0.09f / -std::log10 (g));
}
inline const juce::StringArray distTypes   { "Soft", "Hard", "Fold", "Sine" };

inline const juce::StringArray seqRates    { "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
inline const float seqRateBeats[]          { 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f };

inline const juce::StringArray dlyDivNames { "1/2", "1/4", "1/4 D", "1/8", "1/8 D", "1/16", "1/4 T", "1/8 T" };
inline const float dlyDivBeats[]           { 2.0f, 1.0f, 1.5f, 0.5f, 0.75f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f };

// Modulation sources
inline const juce::StringArray modSources { "None", "Mod 1", "Mod 2", "Amp Env", "Velocity", "Mod Wheel",
                                            "Macro 1", "Macro 2", "Seq Accent", "Key", "S&H", "Mod 3" };
// Mod 3 was added later: appended so saved matrix settings keep their meaning
enum Src { SrcNone, SrcMod1, SrcMod2, SrcAmpEnv, SrcVel, SrcModWheel, SrcMacro1, SrcMacro2, SrcAccent, SrcKey, SrcSH, SrcMod3, NumSrc };
constexpr int numMods = 3;   // mod units (LFO / S&H / envelope), each with its own shape
inline constexpr int modSrc[numMods] { SrcMod1, SrcMod2, SrcMod3 };

inline const juce::StringArray phaseModes  { "Reset", "Random", "Spread" };   // Unison::PhaseMode
inline const juce::StringArray detuneModes { "Linear", "Super" };              // Unison::DetuneMode

inline const juce::StringArray filterTypes  { "Low Pass", "High Pass", "Band Pass" };
inline const juce::StringArray filterSlopes { "24 dB", "12 dB", "6 dB" };

// FX chain: the order is user-editable (dragging the FX boxes); this is the default.
enum Fx { FxComb, FxDist, FxDelay, FxReverb, FxEq, FxOtt, FxFilter, NumFx };
inline const char* fxKeys[NumFx] { "comb", "dist", "delay", "reverb", "eq", "ott", "filter" };
inline const int defaultFxOrder[NumFx] { FxComb, FxOtt, FxDist, FxFilter, FxDelay, FxReverb, FxEq };

// Source driving the per-harmonic "depth" table
inline const juce::StringArray tableModSources { "Mod 1", "Mod 2", "Amp Env", "Macro 1", "Macro 2", "Mod 3" };
inline const Src tableModSrcMap[] { SrcMod1, SrcMod2, SrcAmpEnv, SrcMacro1, SrcMacro2, SrcMod3 };

// Modulation destinations
struct DestInfo { const char* name; const char* paramID; bool perVoice; };
enum Dst
{
    DstNone,
    DstO1Level, DstO1Pitch, DstO1Detune, DstO1Fm,
    DstO2Level, DstO2Pitch, DstO2Detune, DstO2Fm,
    DstO3Level, DstO3Pitch, DstO3Detune, DstO3Fm,
    DstStretch, DstOddEven, DstCutoff, DstRes, DstPrism, DstPrismCenter, DstTableAmt, DstResPos,
    DstDistDrive, DstDistMix, DstCombFreq, DstCombFb, DstCombMix, DstEqMid, DstDlyFb, DstDlyMix,
    DstRevSize, DstRevMix, DstMaster,
    // Added later; appended so saved matrix settings keep their meaning.
    DstO1Fine, DstO1Phase, DstO1Rand, DstO1Voices, DstO1Blend, DstO1Width,
    DstO2Fine, DstO2Phase, DstO2Rand, DstO2Voices, DstO2Blend, DstO2Width,
    DstO3Fine, DstO3Phase, DstO3Rand, DstO3Voices, DstO3Blend, DstO3Width,
    DstAmpA, DstAmpD, DstAmpS, DstAmpR, DstGlide, DstBend, DstPrismWidth,   // Prism slots were the Phaser's (same indices)
    DstM1Rate, DstM1Start, DstM2Rate, DstM2Start, DstRsTune, DstRsLift, DstRsSpeed, DstRsBpm,
    DstSqGate, DstSqSlide, DstSqAccent, DstEqLow, DstEqMidF, DstEqHigh, DstRevDamp, DstRevWidth,
    DstFltCutoff, DstFltRes, DstFltEnv, DstFltKey, DstFltDrive, DstFltA, DstFltD, DstFltS, DstFltR,
    DstEqLowF, DstEqLowQ, DstEqMidQ, DstEqHighF, DstEqHighQ,
    DstCombLp, DstCombHp, DstDlyTime, DstDlyLp, DstDlyHp, DstRevLp, DstRevHp,
    DstRsSubLift, DstRsBassFreq, DstOttAmount, DstOttSpeed,
    DstM1Smooth, DstM2Smooth, DstShSmooth,
    DstO1Pan, DstO2Pan, DstO3Pan,
    DstM1SyncRate, DstM2SyncRate, DstShRate,
    DstShimmer, DstShimmerRate,
    DstRsGate,
    DstFxfCutoff, DstFxfRes, DstFxfDrive,
    DstM3Rate, DstM3Start, DstM3Smooth, DstM3SyncRate,
    NumDst
};
// each mod unit's own destinations
inline constexpr int modRateDst[numMods]   { DstM1Rate, DstM2Rate, DstM3Rate };
inline constexpr int modStartDst[numMods]  { DstM1Start, DstM2Start, DstM3Start };
inline constexpr int modSmoothDst[numMods] { DstM1Smooth, DstM2Smooth, DstM3Smooth };
inline constexpr int modSyncDst[numMods]   { DstM1SyncRate, DstM2SyncRate, DstM3SyncRate };

inline const DestInfo dests[NumDst] =
{
    { "None", "", false },
    { "Osc 1 Level", "o1_level", true }, { "Osc 1 Pitch", "o1_coarse", true }, { "Osc 1 Detune", "o1_uniDetune", true }, { "Osc 1 FM", "o1_fmAmt", true },
    { "Osc 2 Level", "o2_level", true }, { "Osc 2 Pitch", "o2_coarse", true }, { "Osc 2 Detune", "o2_uniDetune", true }, { "Osc 2 FM", "o2_fmAmt", true },
    { "Osc 3 Level", "o3_level", true }, { "Osc 3 Pitch", "o3_coarse", true }, { "Osc 3 Detune", "o3_uniDetune", true }, { "Osc 3 FM", "o3_fmAmt", true },
    { "Harm Stretch", "hm_stretch", true }, { "Harm Odd/Even", "hm_oddEven", true }, { "Harm Cutoff", "hm_cutoff", true },
    { "Harm Resonance", "hm_res", true }, { "Prism Amount", "hm_prism", true }, { "Prism Center", "hm_prismCenter", true },
    { "Harm Table Depth", "hm_tableAmt", true }, { "Resynth Position", "rs_pos", true },
    { "Dist Drive", "dist_drive", false }, { "Dist Mix", "dist_mix", false }, { "Comb Freq", "comb_freq", false },
    { "Comb Feedback", "comb_fb", false }, { "Comb Mix", "comb_mix", false }, { "EQ Mid Gain", "eq_mid", false },
    { "Delay Feedback", "dly_fb", false }, { "Delay Mix", "dly_mix", false }, { "Reverb Size", "rev_size", false },
    { "Reverb Mix", "rev_mix", false }, { "Master Volume", "master", false },
    { "Osc 1 Fine", "o1_fine", true }, { "Osc 1 Phase", "o1_phase", true }, { "Osc 1 Rand", "o1_rand", true },
    { "Osc 1 Unison", "o1_uniVoices", true }, { "Osc 1 Blend", "o1_uniBlend", true }, { "Osc 1 Width", "o1_uniWidth", true },
    { "Osc 2 Fine", "o2_fine", true }, { "Osc 2 Phase", "o2_phase", true }, { "Osc 2 Rand", "o2_rand", true },
    { "Osc 2 Unison", "o2_uniVoices", true }, { "Osc 2 Blend", "o2_uniBlend", true }, { "Osc 2 Width", "o2_uniWidth", true },
    { "Osc 3 Fine", "o3_fine", true }, { "Osc 3 Phase", "o3_phase", true }, { "Osc 3 Rand", "o3_rand", true },
    { "Osc 3 Unison", "o3_uniVoices", true }, { "Osc 3 Blend", "o3_uniBlend", true }, { "Osc 3 Width", "o3_uniWidth", true },
    { "Amp Attack", "ampA", true }, { "Amp Decay", "ampD", true }, { "Amp Sustain", "ampS", true }, { "Amp Release", "ampR", true },
    { "Glide", "glide", true }, { "Bend Range", "bendRange", true }, { "Prism Width", "hm_prismWidth", true },
    { "Mod 1 Rate", "m1_rate", true }, { "Mod 1 Start", "m1_start", true }, { "Mod 2 Rate", "m2_rate", true }, { "Mod 2 Start", "m2_start", true },
    { "Resynth Tune", "rs_tune", true }, { "Resynth Lift", "rs_lift", true }, { "Resynth Speed", "rs_speed", true },
    { "Resynth BPM", "rs_bpm", true },
    { "Seq Gate", "sq_gate", false }, { "Seq Slide", "sq_slide", false }, { "Seq Accent Amt", "sq_accent", false },
    { "EQ Low", "eq_low", false }, { "EQ Mid Freq", "eq_midF", false }, { "EQ High", "eq_high", false },
    { "Reverb Tone", "rev_tone", false }, { "Reverb Decay", "rev_decay", false },   // were Damping and Width
    { "Filter Cutoff", "flt_cutoff", true }, { "Filter Resonance", "flt_res", true }, { "Filter Env Amount", "flt_env", true },
    { "Filter Key Track", "flt_key", true }, { "Filter Drive", "flt_drive", true }, { "Filter Attack", "flt_A", true },
    { "Filter Decay", "flt_D", true }, { "Filter Sustain", "flt_S", true }, { "Filter Release", "flt_R", true },
    { "EQ Low Freq", "eq_lowF", false }, { "EQ Low Q", "eq_lowQ", false }, { "EQ Mid Q", "eq_midQ", false },
    { "EQ High Freq", "eq_highF", false }, { "EQ High Q", "eq_highQ", false },
    { "Comb Low Pass", "comb_lp", false }, { "Comb High Pass", "comb_hp", false },
    { "Delay Time (ms)", "dly_time", false }, { "Delay Low Pass", "dly_lp", false }, { "Delay High Pass", "dly_hp", false },
    { "Reverb Mod", "rev_mod", false }, { "(unused)", "", false },                  // were Low Pass and High Pass
    { "Resynth Sub Lift", "rs_subLift", true }, { "Bass Freq", "rs_bassFreq", true },
    { "OTT Amount", "ott_amount", false }, { "OTT Speed", "ott_speed", false },
    { "Mod 1 Smooth", "m1_smooth", true }, { "Mod 2 Smooth", "m2_smooth", true }, { "S&H Smooth", "sh_smooth", false },
    { "Osc 1 Pan", "o1_pan", true }, { "Osc 2 Pan", "o2_pan", true }, { "Osc 3 Pan", "o3_pan", true },
    { "Mod 1 Sync Rate", "m1_syncRate", true }, { "Mod 2 Sync Rate", "m2_syncRate", true }, { "S&H Rate", "sh_syncRate", false },
    { "Shimmer Amount", "hm_shimmer", true }, { "Shimmer Rate", "hm_shimmerRate", true },
    { "Resynth Gate", "rs_gate", true },
    { "FX Filter Cutoff", "fxf_cutoff", false }, { "FX Filter Resonance", "fxf_res", false }, { "FX Filter Drive", "fxf_drive", false },
    { "Mod 3 Rate", "m3_rate", true }, { "Mod 3 Start", "m3_start", true }, { "Mod 3 Smooth", "m3_smooth", true },
    { "Mod 3 Sync Rate", "m3_syncRate", true },
};

// Matrix destination for a parameter ID, or DstNone if it cannot be modulated.
inline int destForParam (const juce::String& id)
{
    for (int d = 1; d < NumDst; ++d)
        if (id == dests[d].paramID) return d;
    return DstNone;
}

// Pitch destinations: a modulation amount of 1.0 moves the pitch by this many semitones.
constexpr float pitchModSemitones = 48.0f;
inline bool isPitchDest (int d) { return d == DstO1Pitch || d == DstO2Pitch || d == DstO3Pitch; }

inline juce::StringArray destNames()
{
    juce::StringArray s;
    for (auto& d : dests) s.add (d.name);
    return s;
}

constexpr int numOscs = 3;
constexpr int numMatrixSlots = 8;
constexpr int numHarmTable = 64;
constexpr int maxVoices = 8;

inline juce::String osc (int i, const char* name) { return "o" + juce::String (i + 1) + "_" + name; }
inline juce::String mod (int i, const char* name) { return "m" + juce::String (i + 1) + "_" + name; }
inline juce::String mx  (int i, const char* name) { return "mx" + juce::String (i + 1) + "_" + name; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto addF = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, const String& suffix = {})
    {
        auto toText = [suffix] (float v, int)
        {
            const int decimals = std::abs (v) >= 100.0f ? 0 : (std::abs (v) >= 10.0f ? 1 : 2);
            return String (v, decimals) + (suffix.isNotEmpty() ? " " + suffix : String());
        };
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    };
    auto addI = [&] (const String& id, const String& name, int lo, int hi, int def)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto addC = [&] (const String& id, const String& name, const StringArray& c, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def));
    };
    auto addB = [&] (const String& id, const String& name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };
    // logarithmic: every octave takes the same knob distance (and the same modulation amount)
    auto logarithmic = [] (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
                                         [] (float s, float e, float p) { return s * std::pow (e / s, p); },
                                         [] (float s, float e, float v) { return std::log (juce::jmax (v, s) / s) / std::log (e / s); });
    };

    // Global
    addF ("master", "Master", { -48.0f, 6.0f, 0.01f }, -6.0f, "dB");
    addC ("polyMode", "Voice Mode", polyModes, 0);
    addF ("glide", "Glide", skewed (0.0f, 2.0f, 0.2f), 0.0f, "s");
    addI ("bendRange", "Bend Range", 0, 24, 2);
    addF ("ampA", "Amp Attack",  skewed (0.001f, 10.0f, 0.5f), 0.005f, "s");
    addF ("ampD", "Amp Decay",   skewed (0.001f, 10.0f, 0.8f), 0.3f, "s");
    addF ("ampS", "Amp Sustain", { 0.0f, 1.0f }, 0.8f);
    addF ("ampR", "Amp Release", skewed (0.001f, 15.0f, 1.0f), 0.2f, "s");

    // Oscillators
    for (int i = 0; i < numOscs; ++i)
    {
        auto n = "Osc " + String (i + 1) + " ";
        addB (osc (i, "on"), n + "On", true);   // all on; osc 2 and 3 start silent (level 0)
        addC (osc (i, "wave"), n + "Wave", waveNames, WSine);
        addF (osc (i, "level"), n + "Level", { 0.0f, 1.0f }, i == 0 ? 0.8f : 0.0f);
        addI (osc (i, "coarse"), n + "Coarse", -48, 48, 0);
        addF (osc (i, "fine"), n + "Fine", { -100.0f, 100.0f }, 0.0f, "ct");
        // Every note restarts the unison phases: Phase is the start phase; Amount means "how random"
        // in Random mode and "how far spread" in Spread mode (unused in Reset mode).
        addF (osc (i, "phase"), n + "Phase", { 0.0f, 360.0f, 0.1f }, 0.0f, "deg");
        addC (osc (i, "phaseMode"), n + "Phase Mode", phaseModes, 0);
        addF (osc (i, "rand"), n + "Random Phase", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
        addI (osc (i, "uniVoices"), n + "Unison Voices", 1, 16, 1);
        // maximum detune of the outer voices, in cents
        addF (osc (i, "uniDetune"), n + "Unison Detune", { 0.0f, 100.0f, 0.01f }, 20.0f, "ct");
        addC (osc (i, "uniMode"), n + "Detune Mode", detuneModes, 1);   // Super by default
        addF (osc (i, "uniBlend"), n + "Unison Blend", { 0.0f, 100.0f, 0.1f }, 75.0f, "%");
        addF (osc (i, "uniWidth"), n + "Unison Width", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");
        addC (osc (i, "fmSrc"), n + "FM Source", fmSrcNames, i < numOscs - 1 ? i + 2 : 0);   // default chain: Osc 3 -> Osc 2 -> Osc 1
        addF (osc (i, "fmAmt"), n + "FM Amount", { 0.0f, 1.0f }, 0.0f);
        addF (osc (i, "pan"), n + "Pan", { -100.0f, 100.0f, 0.1f }, 0.0f, "%");
        // below Bass Freq: one centred, undetuned voice instead of the unison spread
        addB (osc (i, "bassMono"), n + "Bass Mono", false);
        addB (osc (i, "toFilter"), n + "To Filter", true);   // off: the oscillator bypasses the filter
    }

    // Voice filter (after the oscillators) with its own envelope
    addC ("flt_type", "Filter Type", filterTypes, 0);
    addC ("flt_slope", "Filter Slope", filterSlopes, 0);
    addF ("flt_cutoff", "Filter Cutoff", logarithmic (20.0f, 20000.0f), 20000.0f, "Hz");
    addF ("flt_res", "Filter Resonance", { 0.0f, 1.0f }, 0.0f);
    addF ("flt_env", "Filter Env Amount", { -1.0f, 1.0f }, 0.0f);
    // cutoff follows the note: 100% = an octave per octave from C4; negative = higher notes get a lower cutoff
    addF ("flt_key", "Filter Key Track", { -200.0f, 200.0f, 0.1f }, 0.0f, "%");
    addF ("flt_drive", "Filter Drive", { 0.0f, 1.0f }, 0.0f);
    addF ("flt_A", "Filter Attack",  skewed (0.001f, 10.0f, 0.5f), 0.005f, "s");
    addF ("flt_D", "Filter Decay",   skewed (0.001f, 10.0f, 0.8f), 0.4f, "s");
    addF ("flt_S", "Filter Sustain", { 0.0f, 1.0f }, 0.5f);
    addF ("flt_R", "Filter Release", skewed (0.001f, 15.0f, 1.0f), 0.3f, "s");

    // Tempo-synced sample & hold random modulator
    addC ("sh_syncRate", "S&H Rate", syncRateNames, syncRateIndex (0.25f));   // 1/16
    addF ("sh_smooth", "S&H Smooth", { 0.0f, 10.0f, 0.01f }, 3.0f, "ms");
    addB ("sh_retrig", "S&H Retrigger", true);   // on: each note picks a new value and restarts the step clock

    // Harmonics (additive spectrum processing)
    addF ("hm_stretch", "Harmonic Stretch", { -1.0f, 1.0f }, 0.0f);
    addF ("hm_oddEven", "Odd/Even", { -1.0f, 1.0f }, 0.0f);
    addF ("hm_cutoff", "Harmonic Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, "Hz");
    addF ("hm_res", "Harmonic Resonance", { 0.0f, 1.0f }, 0.0f);
    // Prism: harmonics in a band around Center (harmonic number, Width in octaves) pushed off the series
    addF ("hm_prism", "Prism Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    addF ("hm_prismCenter", "Prism Center", skewed (2.0f, 64.0f, 8.0f), 8.0f);
    addF ("hm_prismWidth", "Prism Width", { 0.5f, 6.0f, 0.01f }, 2.0f, "oct");
    // Shimmer: every harmonic drifts in pitch on its own
    addF ("hm_shimmer", "Shimmer Amount", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    addF ("hm_shimmerRate", "Shimmer Rate", skewed (0.05f, 10.0f, 1.0f), 1.0f, "Hz");
    addC ("hm_tableSrc", "Harm Table Source", tableModSources, 0);
    addF ("hm_tableAmt", "Harm Table Depth", { -1.0f, 1.0f }, 0.0f);

    // Mod units (LFO / S&H / Envelope)
    for (int i = 0; i < numMods; ++i)
    {
        auto n = "Mod " + String (i + 1) + " ";
        addC (mod (i, "mode"), n + "Mode", modModes, i == 0 ? MEnvelope : MLfo);   // Mod 1 starts as a decay envelope
        addF (mod (i, "rate"), n + "Rate", skewed (0.01f, 40.0f, 2.0f), 1.0f, "Hz");
        addB (mod (i, "sync"), n + "Sync", false);
        addC (mod (i, "syncRate"), n + "Sync Rate", syncRateNames, syncRateIndex (1.0f));   // 1/4
        addF (mod (i, "start"), n + "Start", { 0.0f, 1.0f }, 0.0f);
        addB (mod (i, "retrig"), n + "Retrigger", false);   // off: synced LFOs follow the song position
        addF (mod (i, "smooth"), n + "Smooth", { 0.0f, 10.0f, 0.01f }, 3.0f, "ms");
    }

    // Resynthesis
    addF ("rs_tune", "Resynth Tune", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    // partials more than this far below the loudest partial at the moment are faded out (before Lift); -80 = off
    addF ("rs_gate", "Resynth Gate", { -80.0f, -6.0f, 0.1f }, -80.0f, "dB");
    addF ("rs_lift", "Resynth Lift", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    addF ("rs_subLift", "Resynth Sub Lift", { 0.0f, 100.0f, 0.1f }, 0.0f, "%");
    // ID kept from when it was a resynth-only setting; it is now the crossover for every oscillator's Bass Mono too
    addF ("rs_bassFreq", "Bass Freq", skewed (40.0f, 500.0f, 120.0f), 120.0f, "Hz");
    addF ("rs_speed", "Resynth Speed", skewed (0.0f, 4.0f, 1.0f), 1.0f, "x");
    addB ("rs_sync", "Resynth Tempo Sync", true);
    addF ("rs_bpm", "Resynth Source BPM", { 40.0f, 240.0f, 0.01f }, 120.0f);
    addF ("rs_pos", "Resynth Position", { 0.0f, 1.0f }, 0.0f);
    addB ("rs_loop", "Resynth Loop", true);

    // Sequencer
    addB ("sq_on", "Sequencer On", false);
    addB ("sq_retrig", "Sequencer Retrigger", false);   // on: every new note restarts the pattern from its start step
    addC ("sq_rate", "Sequencer Rate", seqRates, 1);
    addF ("sq_gate", "Sequencer Gate", { 0.05f, 1.0f }, 0.5f);
    addF ("sq_slide", "Slide Time", skewed (0.005f, 0.5f, 0.06f), 0.06f, "s");
    addF ("sq_accent", "Accent Amount", { 0.0f, 1.0f }, 0.6f);

    // FX
    addB ("dist_on", "Dist On", false);
    addC ("dist_type", "Dist Type", distTypes, 0);
    addF ("dist_drive", "Dist Drive", { 0.0f, 1.0f }, 0.3f);
    addF ("dist_mix", "Dist Mix", { 0.0f, 1.0f }, 1.0f);

    addB ("comb_on", "Comb On", false);
    addF ("comb_freq", "Comb Freq", skewed (20.0f, 5000.0f, 400.0f), 40.0f, "Hz");
    addF ("comb_fb", "Comb Feedback", { -0.97f, 0.97f }, 0.6f);
    addF ("comb_mix", "Comb Mix", { 0.0f, 1.0f }, 0.5f);

    addB ("eq_on", "EQ On", false);
    addF ("eq_low", "EQ Low", { -18.0f, 18.0f }, 0.0f, "dB");
    addF ("eq_midF", "EQ Mid Freq", skewed (100.0f, 10000.0f, 1000.0f), 1000.0f, "Hz");
    addF ("eq_mid", "EQ Mid", { -18.0f, 18.0f }, 0.0f, "dB");
    addF ("eq_high", "EQ High", { -18.0f, 18.0f }, 0.0f, "dB");
    addF ("eq_lowF", "EQ Low Freq", skewed (20.0f, 1000.0f, 150.0f), 150.0f, "Hz");
    addF ("eq_lowQ", "EQ Low Q", skewed (0.3f, 4.0f, 0.9f), 0.71f);
    addF ("eq_midQ", "EQ Mid Q", skewed (0.3f, 10.0f, 1.5f), 1.0f);
    addF ("eq_highF", "EQ High Freq", skewed (1000.0f, 20000.0f, 6000.0f), 6000.0f, "Hz");
    addF ("eq_highQ", "EQ High Q", skewed (0.3f, 4.0f, 0.9f), 0.71f);

    // Wet-signal filters for comb, delay (inside the feedback path) and reverb
    addF ("comb_lp", "Comb Low Pass", skewed (200.0f, 20000.0f, 3000.0f), 20000.0f, "Hz");
    addF ("comb_hp", "Comb High Pass", skewed (20.0f, 5000.0f, 300.0f), 20.0f, "Hz");
    addB ("dly_sync", "Delay Sync", true);
    addF ("dly_time", "Delay Time", skewed (1.0f, 2000.0f, 300.0f), 375.0f, "ms");
    addF ("dly_lp", "Delay Low Pass", skewed (200.0f, 20000.0f, 3000.0f), 20000.0f, "Hz");
    addF ("dly_hp", "Delay High Pass", skewed (20.0f, 5000.0f, 300.0f), 20.0f, "Hz");

    addB ("dly_on", "Delay On", false);
    addC ("dly_div", "Delay Time", dlyDivNames, 4);
    addF ("dly_fb", "Delay Feedback", { 0.0f, 0.95f }, 0.4f);
    addF ("dly_mix", "Delay Mix", { 0.0f, 1.0f }, 0.25f);
    addB ("dly_pp", "Delay Ping-Pong", true);

    // FX filter: the voice filter's sound (same types and slopes) on the summed signal, placed in the FX chain
    addB ("fxf_on", "FX Filter On", false);
    addC ("fxf_type", "FX Filter Type", filterTypes, 0);
    addC ("fxf_slope", "FX Filter Slope", filterSlopes, 0);
    addF ("fxf_cutoff", "FX Filter Cutoff", logarithmic (20.0f, 20000.0f), 20000.0f, "Hz");
    addF ("fxf_res", "FX Filter Resonance", { 0.0f, 1.0f }, 0.0f);
    addF ("fxf_drive", "FX Filter Drive", { 0.0f, 1.0f }, 0.0f);

    // OTT-style multiband (3 bands) upward + downward compressor
    addB ("ott_on", "OTT On", false);
    addF ("ott_amount", "OTT Amount", { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
    addF ("ott_speed", "OTT Speed", { 0.0f, 100.0f, 0.1f }, 50.0f, "%");

    addB ("rev_on", "Reverb On", false);
    // FDN reverb (DSP/Reverb.h): six controls, everything else is set internally per mode
    addF ("rev_mix", "Reverb Mix", { 0.0f, 1.0f }, 0.22f);
    addF ("rev_decay", "Reverb Decay", skewed (0.2f, 15.0f, 2.5f), 3.0f, "s");
    addF ("rev_size", "Reverb Size", { 0.0f, 1.0f }, 0.65f);
    addF ("rev_tone", "Reverb Tone", { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
    addF ("rev_mod", "Reverb Mod", { 0.0f, 100.0f, 0.1f }, 25.0f, "%");
    addC ("rev_mode", "Reverb Mode", reverbModes, 0);

    // Macros + matrix
    addF ("macro1", "Macro 1", { 0.0f, 1.0f }, 0.0f);
    addF ("macro2", "Macro 2", { 0.0f, 1.0f }, 0.0f);
    for (int i = 0; i < numMatrixSlots; ++i)
    {
        auto n = "Matrix " + String (i + 1) + " ";
        addC (mx (i, "src"), n + "Source", modSources, 0);
        addC (mx (i, "dst"), n + "Destination", destNames(), 0);
        addF (mx (i, "amt"), n + "Amount", { -1.0f, 1.0f }, 0.0f);
    }
    return l;
}
} // namespace P
