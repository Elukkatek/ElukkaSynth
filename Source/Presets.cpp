#include "Presets.h"
#include "PluginProcessor.h"

PresetManager::PresetManager (ElukkaAudioProcessor& p) : proc (p)
{
    attachToParameters();
}

PresetManager::~PresetManager()
{
    detachFromParameters();
}

void PresetManager::attachToParameters()
{
    const auto& params = proc.getParameters();
    numTrackedParams = params.size();
    hostMoved = std::make_unique<std::atomic<bool>[]> ((size_t) numTrackedParams);
    paramIds.assign ((size_t) numTrackedParams, juce::String());

    for (int i = 0; i < numTrackedParams; ++i)
    {
        hostMoved[(size_t) i].store (false);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
            paramIds[(size_t) i] = ranged->getParameterID();
        params[i]->addListener (this);
    }
}

void PresetManager::detachFromParameters()
{
    for (auto* param : proc.getParameters())
        param->removeListener (this);

    hostMoved.reset();
    paramIds.clear();
    numTrackedParams = 0;
}

juce::RangedAudioParameter* PresetManager::getParam (int index) const
{
    const auto& params = proc.getParameters();
    return index >= 0 && index < params.size()
        ? dynamic_cast<juce::RangedAudioParameter*> (params[index])
        : nullptr;
}

void PresetManager::parameterValueChanged (int parameterIndex, float)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        ++hostChangeBurst;
        return;
    }

    // The host moved a parameter (automation playback, the DAW's own undo, a project load). That is not
    // an edit the user can undo, so remember it: commitUnrecordedChanges() folds it into the checkpoint.
    if (hostMoved != nullptr && parameterIndex >= 0 && parameterIndex < numTrackedParams)
        hostMoved[(size_t) parameterIndex].store (true);
}

void PresetManager::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (applyingState)
        return;

    if (gestureIsStarting)
    {
        // Seal anything edited before this gesture (mod shapes, pattern grid, ...) as its own step.
        if (gestureDepth++ == 0)
        {
            commitUnrecordedChanges();
            gestureBefore.clear();
        }

        // The value the parameter had before the gesture: what undo puts back when the gesture ends.
        if (auto* param = getParam (parameterIndex))
        {
            const bool alreadyTracked = std::any_of (gestureBefore.begin(), gestureBefore.end(),
                                                     [parameterIndex] (const auto& e) { return e.first == parameterIndex; });
            if (! alreadyTracked)
                gestureBefore.push_back ({ parameterIndex, param->convertFrom0to1 (param->getValue()) });
        }
    }
    else if (gestureDepth > 0 && --gestureDepth == 0)
    {
        // Only the parameters this gesture touched are a step (not everything automation moved meanwhile).
        commitGestureChanges();
    }
}

void PresetManager::rememberState (const Snapshot& snapshot)
{
    for (auto& known : knownStates)
        if (known.state.isEquivalentTo (snapshot.state) && known.currentName == snapshot.currentName)
            return;

    auto copy = snapshot;
    copy.resynth.reset();   // only the state and name are needed; do not keep old images alive
    knownStates.push_back (std::move (copy));
    if (knownStates.size() > 32)
        knownStates.erase (knownStates.begin());
}

void PresetManager::applyNameFrom (const Snapshot& snapshot)
{
    currentName = snapshot.currentName;
    currentIndex = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& entry = entries[(size_t) i];
        if (entry.isFactory == snapshot.isFactory && entry.index == snapshot.factoryIndex && entry.file == snapshot.file)
        {
            currentIndex = i;
            break;
        }
    }
}

bool PresetManager::parameterStatesEqual (const juce::ValueTree& a, const juce::ValueTree& b)
{
    int compared = 0;
    for (int i = 0; i < a.getNumChildren(); ++i)
    {
        const auto child = a.getChild (i);
        const auto id = child["id"].toString();
        if (id.isEmpty()) continue;

        const auto other = b.getChildWithProperty ("id", id);
        if (! other.isValid()) return false;

        const auto x = (double) child["value"], y = (double) other["value"];
        if (std::abs (x - y) > 1.0e-5 * juce::jmax (1.0, std::abs (x)))
            return false;
        ++compared;
    }
    return compared > 0;
}

void PresetManager::syncNameWithHost()
{
    const int burst = hostChangeBurst;
    hostChangeBurst = 0;

    // A knob move changes one or two values; a DAW undo of a preset load changes many at once.
    if (burst < 16 || applyingState) return;

    const auto state = proc.saveState (false);
    for (auto it = knownStates.rbegin(); it != knownStates.rend(); ++it)
    {
        if (! parameterStatesEqual (it->state, state)) continue;

        if (it->currentName != currentName)
        {
            applyNameFrom (*it);
            checkpoint = captureSnapshot();
            proc.bumpUiVersion();
        }
        return;
    }
}

const juce::StringArray& PresetManager::categories()
{
    static juce::StringArray c { "Pads", "Basses", "Arpeggios", "Keys", "Atmospheres", "Sequences" };
    return c;
}

juce::File PresetManager::userPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ElukkaSynth/Presets");
}

juce::File PresetManager::userPatternDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ElukkaSynth/Patterns");
}

//==============================================================================
// Factory preset building blocks
namespace
{
using FP = FactoryPreset;
enum { Sine, Tri, Saw, Sqr, Pulse, Noise };
enum { L_LFO, L_SH, L_ENV };
enum { ShSine, ShTri, ShSawDown, ShSawUp, ShSquare, ShDecay, ShAD, ShSteps, ShPluck };
enum { D4Bars, D2Bars, D1Bar, D2, D4, D8, D16, D32, D4T, D8T, D16T, D4D, D8D };      // mod sync divisions
enum { Dly2, Dly4D, Dly4, Dly8D, Dly4T, Dly8, Dly8T, Dly16 };                           // delay divisions (P::dlyDivNames)
enum { R8, R16, R32, R8T, R16T };                                                       // sequencer rates

FP make (const juce::String& name, const juce::String& cat)
{
    FP p;
    p.name = name;
    p.category = cat;
    // the factory presets were voiced with other defaults (saw waves, no FM source, only osc 1 on, levels 0.8);
    // keep those
    for (int i = 1; i <= 3; ++i)
    {
        const auto o = "o" + juce::String (i) + "_";
        p.set (o + "wave", (float) P::WSaw).set (o + "fmSrc", 0.0f).set (o + "on", i == 1 ? 1.0f : 0.0f).set (o + "level", 0.8f);
    }
    return p;
}

void fm (FP& p, int target, int source, float amount)
{
    auto t = "o" + juce::String (target + 1) + "_";
    p.set (t + "fmSrc", (float) (source + 1)).set (t + "fmAmt", amount);
}

void amp (FP& p, float a, float d, float s, float r)
{
    p.set ("ampA", a).set ("ampD", d).set ("ampS", s).set ("ampR", r);
}

void route (FP& p, int slot, int src, int dst, float amt)
{
    auto s = "mx" + juce::String (slot + 1) + "_";
    p.set (s + "src", (float) src).set (s + "dst", (float) dst).set (s + "amt", amt);
}

void harm (FP& p, float cutoff, float res = 0.0f)
{
    p.set ("hm_cutoff", cutoff).set ("hm_res", res);
}

void prism (FP& p, float amount, float center, float widthOct)   // amount in %
{
    p.set ("hm_prism", amount).set ("hm_prismCenter", center).set ("hm_prismWidth", widthOct);
}

void shimmer (FP& p, float amount, float rateHz)   // amount in %
{
    p.set ("hm_shimmer", amount).set ("hm_shimmerRate", rateHz);
}

void dist (FP& p, int type, float drive, float mix = 1.0f)
{
    p.set ("dist_on", 1).set ("dist_type", (float) type).set ("dist_drive", drive).set ("dist_mix", mix);
}

std::vector<float> table (std::initializer_list<std::pair<int, float>> peaks, float floor)
{
    std::vector<float> t ((size_t) P::numHarmTable, floor);
    for (auto& pk : peaks)
        if (pk.first >= 1 && pk.first <= P::numHarmTable)
            t[(size_t) pk.first - 1] = pk.second;
    return t;
}

std::vector<float> vowel (int f1, int f2, float floor = 0.15f)
{
    std::vector<float> t ((size_t) P::numHarmTable, floor);
    for (int h = 1; h <= P::numHarmTable; ++h)
    {
        auto bump = [h] (int c, float w) { float d = (float) (h - c) / w; return std::exp (-d * d); };
        t[(size_t) h - 1] = juce::jlimit (0.0f, 1.0f, floor + bump (f1, 1.5f) + 0.8f * bump (f2, 2.5f));
    }
    return t;
}

std::vector<float> alternating (float a, float b)
{
    std::vector<float> t ((size_t) P::numHarmTable);
    for (int i = 0; i < P::numHarmTable; ++i) t[(size_t) i] = (i % 2 == 0) ? a : b;
    return t;
}

//==============================================================================
//==============================================================================
// Generated images for resynthesis presets. Rows are harmonics (bottom = fundamental),
// columns are time over one bar, brightness is level (1.0 = 0 dB, 0.0 = silent, 72 dB range).
float brightnessFor (float amplitude) { return amplitude <= 0.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (amplitude) / 72.0f); }

juce::Image presetImage (int index)
{
    const int W = 256, H = 256;
    juce::Image img (juce::Image::RGB, W, H, true);
    juce::Random rng (1000 + index);
    for (int x = 0; x < W; ++x)
    {
        const float t = (float) x / (float) W;   // 0..1 over the bar
        for (int row = 0; row < H; ++row)
        {
            const float h = (float) (row + 1);     // harmonic number
            const float natural = 1.0f / h;       // saw-like roll-off
            float a = 0.0f;
            switch (index)
            {
                case 0:   // rising lines: bright diagonal bands climbing through the harmonics
                {
                    const float pos = std::fmod (h / 48.0f - t * 2.0f + 4.0f, 1.0f);
                    a = natural * 0.1f + std::exp (-std::pow ((pos - 0.5f) * 14.0f, 2.0f)) * 0.5f / std::sqrt (h);
                    break;
                }
                case 1:   // harmonic stairs: one strong harmonic per 16th note stepping up the series
                {
                    static const int steps[] { 1, 2, 3, 4, 5, 6, 8, 10, 12, 10, 8, 6, 5, 4, 3, 2 };
                    const int step = steps[juce::jmin (15, (int) (t * 16.0f))];
                    a = natural * 0.15f + (row + 1 == step ? 0.9f : 0.0f) + (row + 1 == step * 2 ? 0.3f : 0.0f);
                    break;
                }
                case 2:   // soft gradient: smooth roll-off breathing slowly
                    a = natural * (0.6f + 0.4f * std::sin (juce::MathConstants<float>::twoPi * (t + h * 0.03f)));
                    break;
                case 3:   // sparkle: steady low harmonics plus random glints up high
                    a = h <= 4.0f ? natural : (rng.nextFloat() < 0.012f ? 0.4f / std::sqrt (h) : 0.0f);
                    break;
                case 4:   // pulse columns: full spectrum hits on every 16th, decaying within the step
                {
                    const float within = std::fmod (t * 16.0f, 1.0f);
                    a = natural * std::exp (-within * 6.0f * (1.0f + h * 0.05f));
                    break;
                }
                case 5:   // vowel morph: two formants gliding over the bar
                {
                    const float f1 = 3.0f + 3.0f * std::sin (juce::MathConstants<float>::twoPi * t);
                    const float f2 = 10.0f + 5.0f * std::cos (juce::MathConstants<float>::twoPi * t);
                    a = natural * 0.2f + std::exp (-std::pow ((h - f1) / 1.5f, 2.0f)) * 0.7f + std::exp (-std::pow ((h - f2) / 2.5f, 2.0f)) * 0.4f;
                    break;
                }
                case 6:   // odd glow: odd harmonics pulsing in level
                    a = ((row + 1) % 2 == 1) ? natural * (0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * (t * 4.0f + h * 0.02f))) : 0.0f;
                    break;
                case 8:   // gate grid: harmonic bands switching on and off in a 16th-note pattern, like a trance gate per band
                {
                    static const char* gates[] { "x.xx.x.xx.x.x.xx", "..x...x...x..x.x", "x...x.x.x...x.x.", ".x.x..x.xx..x..x" };
                    const int step = juce::jmin (15, (int) (t * 16.0f));
                    const int band = row < 3 ? -1 : juce::jmin (3, (int) std::log2 ((float) row / 3.0f + 1.0f));
                    const float within = std::fmod (t * 16.0f, 1.0f);
                    const bool on = band < 0 || gates[band][step] == 'x';
                    a = on ? natural * (band < 0 ? 1.0f : std::exp (-within * 3.0f)) : 0.0f;
                    break;
                }
                case 9:   // falling sparks: bright partials sliding down through the spectrum, a new one every 8th
                {
                    a = h <= 2.0f ? natural : 0.0f;
                    for (int k = 0; k < 8; ++k)
                    {
                        const float age = std::fmod (t * 8.0f - (float) k + 8.0f, 8.0f) / 8.0f;   // 0..1 since this spark started
                        const float top = 20.0f + 37.0f * (float) ((k * 5) % 8) / 8.0f;
                        const float pos = top * (1.0f - 0.85f * age);
                        a += std::exp (-std::pow ((h - pos) * 0.8f, 2.0f)) * 0.5f * (1.0f - age) / std::sqrt (pos);
                    }
                    break;
                }
                default:  // ripple: a moving comb across the spectrum
                    a = natural * (0.55f + 0.45f * std::sin (h * 0.6f - t * juce::MathConstants<float>::twoPi * 3.0f));
                    break;
            }
            const auto b = (juce::uint8) juce::jlimit (0, 255, (int) (brightnessFor (a) * 255.0f));
            img.setPixelAt (x, H - 1 - row, juce::Colour (b, b, b));
        }
    }
    return img;
}

void stretch (FP& p, float s) { p.set ("hm_stretch", s); }
void oddEven (FP& p, float v) { p.set ("hm_oddEven", v); }

// Depth table driven by: 0 Mod 1, 1 Mod 2, 2 Amp Env, 3 Macro 1, 4 Macro 2
void depthSrc (FP& p, int src, float amt) { p.set ("hm_tableSrc", (float) src).set ("hm_tableAmt", amt); }

void resynth (FP& p, int osc, int image, float level = 0.8f, float speed = 1.0f, float lift = 0.0f, float tune = 0.0f)
{
    auto id = [osc] (const char* n) { return "o" + juce::String (osc + 1) + "_" + n; };
    p.set (id ("on"), 1).set (id ("wave"), (float) P::WResynth).set (id ("level"), level);
    p.set ("rs_speed", speed).set ("rs_lift", lift).set ("rs_tune", tune).set ("rs_sync", 1).set ("rs_loop", 1).set ("rs_bpm", 120.0f);
    p.image = image;
}

// Gain tables (harmonics 1..64)
std::vector<float> octaves (float floor = 0.0f)
{
    return table ({ { 1, 1.0f }, { 2, 0.9f }, { 4, 0.8f }, { 8, 0.7f }, { 16, 0.6f }, { 32, 0.5f }, { 64, 0.4f } }, floor);
}
std::vector<float> octavesFifths (float floor = 0.05f)
{
    return table ({ { 1, 1.0f }, { 2, 0.9f }, { 3, 0.8f }, { 4, 0.8f }, { 6, 0.7f }, { 8, 0.6f }, { 12, 0.5f }, { 16, 0.5f },
                    { 24, 0.4f }, { 32, 0.35f }, { 48, 0.3f }, { 64, 0.25f } }, floor);
}
std::vector<float> drawbars (float b8, float b4, float b2, float b1)   // 8' = fundamental
{
    return table ({ { 1, b8 }, { 2, b4 }, { 3, (b8 + b4) * 0.4f }, { 4, b2 }, { 6, b2 * 0.6f }, { 8, b1 }, { 10, b1 * 0.4f }, { 12, b1 * 0.3f }, { 16, b1 * 0.3f } }, 0.0f);
}
std::vector<float> ramp (float from, float to, int startHarm = 1)
{
    std::vector<float> t ((size_t) P::numHarmTable);
    for (int h = 1; h <= P::numHarmTable; ++h)
        t[(size_t) h - 1] = h < startHarm ? from : from + (to - from) * (float) (h - startHarm) / (float) (P::numHarmTable - startHarm);
    return t;
}
std::vector<float> bands (int lo, int hi, float inside, float outside)
{
    std::vector<float> t ((size_t) P::numHarmTable, outside);
    for (int h = lo; h <= hi && h <= P::numHarmTable; ++h) t[(size_t) h - 1] = inside;
    return t;
}
std::vector<float> bell()   // a few strong partials, used with Stretch
{
    return table ({ { 1, 1.0f }, { 2, 0.6f }, { 3, 0.7f }, { 5, 0.5f }, { 7, 0.45f }, { 9, 0.4f }, { 12, 0.3f }, { 15, 0.25f } }, 0.0f);
}

//==============================================================================
// Building blocks for the presets (dreamy techno, trance and dub techno, voiced around 120-140 BPM)
// Macro 1 = "Bloom" (opens the tone: filter, prism, shimmer), Macro 2 = "Space" (reverb and delay): matrix slots 7 and 8.
int pat (const char* name)
{
    const auto& f = SeqPattern::factory();
    for (int i = 0; i < (int) f.size(); ++i)
        if (f[(size_t) i].name == name) return i;
    jassertfalse;
    return SeqPattern::initIndex();
}

void wave (FP& p, int i, int w, float level, int coarse = 0, float fine = 0.0f)
{
    auto id = [i] (const char* n) { return "o" + juce::String (i + 1) + "_" + n; };
    p.set (id ("on"), 1).set (id ("wave"), (float) w).set (id ("level"), level).set (id ("coarse"), (float) coarse).set (id ("fine"), fine);
}

void unison (FP& p, int i, int voices, float cents, float width = 100.0f, float blend = 75.0f, bool super = true)
{
    auto id = [i] (const char* n) { return "o" + juce::String (i + 1) + "_" + n; };
    p.set (id ("uniVoices"), (float) voices).set (id ("uniDetune"), cents).set (id ("uniWidth"), width).set (id ("uniBlend"), blend)
     .set (id ("uniMode"), super ? (float) Unison::DetuneSuper : (float) Unison::DetuneLinear)
     .set (id ("phaseMode"), (float) Unison::PhaseRandom).set (id ("rand"), 100.0f);
}

void panOsc (FP& p, int i, float pan) { p.set ("o" + juce::String (i + 1) + "_pan", pan); }
void bassMono (FP& p, int i, float freq = 120.0f) { p.set ("o" + juce::String (i + 1) + "_bassMono", 1.0f).set ("rs_bassFreq", freq); }

// A chord across the oscillators: osc 1 plays the root, osc 2 and 3 the intervals (semitones), panned apart.
void stack (FP& p, int w, float level, int second, int third, float spread = 30.0f)
{
    wave (p, 0, w, level); wave (p, 1, w, level * 0.8f, second); wave (p, 2, w, level * 0.8f, third);
    panOsc (p, 1, -spread); panOsc (p, 2, spread);
}

void monoVoice (FP& p, bool legato = false, float glide = 0.0f) { p.set ("polyMode", legato ? 2.0f : 1.0f).set ("glide", glide); }

void filter (FP& p, int type, float cutoff, float res, float envAmt = 0.0f, float a = 0.002f, float d = 0.3f, float s = 0.0f, float r = 0.2f,
             float key = 0.0f, float drive = 0.0f, int slope = 0)
{
    p.set ("flt_type", (float) type).set ("flt_slope", (float) slope).set ("flt_cutoff", cutoff).set ("flt_res", res).set ("flt_env", envAmt)
     .set ("flt_A", a).set ("flt_D", d).set ("flt_S", s).set ("flt_R", r).set ("flt_key", key).set ("flt_drive", drive);
}

void envUnit (FP& p, int unit, float seconds, int shape)
{
    auto u = "m" + juce::String (unit + 1) + "_";
    p.set (u + "mode", (float) L_ENV).set (u + "rate", 1.0f / seconds).set (u + "retrig", 1.0f);
    p.shape[unit] = shape;
}

void lfoBeats (FP& p, int unit, float beats, int shape, bool retrig = false)   // beats must be one of P::syncRateBeats (16 = 4 bars)
{
    auto u = "m" + juce::String (unit + 1) + "_";
    p.set (u + "mode", (float) L_LFO).set (u + "sync", 1.0f).set (u + "syncRate", (float) P::syncRateIndex (beats)).set (u + "retrig", retrig ? 1.0f : 0.0f);
    p.shape[unit] = shape;
}

void lfoHz (FP& p, int unit, float hz, int shape, bool retrig = false)
{
    auto u = "m" + juce::String (unit + 1) + "_";
    p.set (u + "mode", (float) L_LFO).set (u + "rate", hz).set (u + "retrig", retrig ? 1.0f : 0.0f);
    p.shape[unit] = shape;
}

void shUnit (FP& p, int unit, float beats)
{
    auto u = "m" + juce::String (unit + 1) + "_";
    p.set (u + "mode", (float) L_SH).set (u + "sync", 1.0f).set (u + "syncRate", (float) P::syncRateIndex (beats)).set (u + "retrig", 0.0f);
}

// Reverb: decay in seconds, mix 0..1, size 0..1, tone 0..100 (dark..bright); Lush mode unless given
void verb (FP& p, float decay, float mix, float size = 0.7f, float tone = 55.0f, int mode = 2, float mod = 30.0f)
{
    p.set ("rev_on", 1).set ("rev_decay", decay).set ("rev_mix", mix).set ("rev_size", size).set ("rev_tone", tone)
     .set ("rev_mode", (float) mode).set ("rev_mod", mod);
}

void echo (FP& p, int div, float fb, float mix, bool pingPong = true, float lp = 5000.0f, float hp = 200.0f)
{
    p.set ("dly_on", 1).set ("dly_sync", 1).set ("dly_div", (float) div).set ("dly_fb", fb).set ("dly_mix", mix)
     .set ("dly_pp", pingPong ? 1.0f : 0.0f).set ("dly_lp", lp).set ("dly_hp", hp);
}

// Dub delay: dotted 8th ping-pong, dark and long
void dubEcho (FP& p, float fb = 0.55f, float mix = 0.3f, int div = Dly8D) { echo (p, div, fb, mix, true, 2500.0f, 300.0f); }

void ott (FP& p, float amount, float speed = 50.0f) { p.set ("ott_on", 1).set ("ott_amount", amount).set ("ott_speed", speed); }

void seqOn (FP& p, const char* pattern, int rate = R16, float gate = 0.5f, float slide = 0.06f, float accent = 0.6f)
{
    p.set ("sq_on", 1).set ("sq_rate", (float) rate).set ("sq_gate", gate).set ("sq_slide", slide).set ("sq_accent", accent);
    p.set ("polyMode", 1);
    p.pattern = pat (pattern);
}

void bloomMacro (FP& p, int dst, float amt) { route (p, 6, P::SrcMacro1, dst, amt); }
void spaceMacro (FP& p, int dst, float amt) { route (p, 7, P::SrcMacro2, dst, amt); }

//==============================================================================
void pads (std::vector<FP>& v)
{
    const juce::String c = "Pads";
    { auto p = make ("Morning Haze", c); wave (p, 0, Saw, 0.8f); unison (p, 0, 7, 18.0f); bassMono (p, 0);
      harm (p, 2500.0f); lfoBeats (p, 1, 8.0f, ShSine); route (p, 0, P::SrcMod2, P::DstCutoff, 0.15f); shimmer (p, 8.0f, 0.2f);
      amp (p, 1.5f, 1.0f, 0.9f, 4.0f); verb (p, 6.0f, 0.4f, 0.8f); echo (p, Dly4D, 0.35f, 0.15f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Dub Horizon", c); stack (p, Saw, 0.5f, 3, 10); unison (p, 0, 3, 10.0f);
      filter (p, 0, 900.0f, 0.2f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f); lfoBeats (p, 1, 16.0f, ShTri); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.12f);
      amp (p, 1.0f, 1.0f, 1.0f, 3.5f); dubEcho (p, 0.55f, 0.3f); verb (p, 5.0f, 0.3f, 0.75f, 45.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Glass Chorale", c); wave (p, 0, Saw, 0.75f); unison (p, 0, 5, 14.0f); p.gain = octavesFifths (0.08f);
      prism (p, 12.0f, 12.0f, 2.5f); shimmer (p, 10.0f, 0.3f); harm (p, 6000.0f);
      amp (p, 2.0f, 1.0f, 1.0f, 5.0f); verb (p, 8.0f, 0.45f, 0.85f, 65.0f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstRevMix, 0.35f); v.push_back (p); }
    { auto p = make ("Velvet Strings", c); wave (p, 0, Saw, 0.8f); unison (p, 0, 7, 24.0f); harm (p, 4000.0f);
      filter (p, 0, 1800.0f, 0.1f, 0.2f, 1.0f, 2.0f, 0.6f, 2.0f); lfoBeats (p, 1, 6.0f, ShTri); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.08f);
      amp (p, 1.8f, 1.0f, 0.9f, 4.0f); verb (p, 5.0f, 0.35f, 0.75f); echo (p, Dly4D, 0.3f, 0.12f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Submerged", c); wave (p, 0, Saw, 0.8f, -12); unison (p, 0, 5, 20.0f); wave (p, 1, Sine, 0.5f, -12); bassMono (p, 0);
      filter (p, 0, 500.0f, 0.25f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f); lfoBeats (p, 1, 16.0f, ShSine); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.15f);
      prism (p, 20.0f, 4.0f, 1.5f); amp (p, 2.5f, 1.0f, 1.0f, 5.0f); verb (p, 7.0f, 0.35f, 0.8f, 35.0f);
      bloomMacro (p, P::DstFltCutoff, 0.35f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Choir Of Rain", c); wave (p, 0, Saw, 0.8f); unison (p, 0, 5, 14.0f); p.gain = vowel (3, 9);
      p.depth = ramp (-0.5f, 0.5f, 3); depthSrc (p, 1, 0.7f); lfoBeats (p, 1, 12.0f, ShTri); shimmer (p, 6.0f, 0.25f);
      amp (p, 1.5f, 1.0f, 0.9f, 4.0f); verb (p, 7.0f, 0.45f, 0.8f); echo (p, Dly4D, 0.3f, 0.1f);
      bloomMacro (p, P::DstTableAmt, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Tape Bloom", c); wave (p, 0, Tri, 0.7f); unison (p, 0, 3, 8.0f); wave (p, 1, Saw, 0.3f, 12); unison (p, 1, 3, 10.0f);
      shimmer (p, 14.0f, 0.4f); harm (p, 3000.0f); lfoHz (p, 1, 0.08f, ShSine); route (p, 0, P::SrcMod2, P::DstO2Level, 0.2f);
      amp (p, 1.2f, 1.0f, 1.0f, 3.0f); verb (p, 4.0f, 0.3f, 0.7f, 40.0f, 1); echo (p, Dly8D, 0.4f, 0.15f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Frozen Lake", c); wave (p, 0, Saw, 0.7f); p.gain = bell(); stretch (p, 0.06f); unison (p, 0, 3, 6.0f);
      prism (p, 15.0f, 14.0f, 2.0f); lfoBeats (p, 1, 16.0f, ShSine); route (p, 0, P::SrcMod2, P::DstStretch, 0.03f);
      amp (p, 2.0f, 2.0f, 0.8f, 6.0f); verb (p, 10.0f, 0.5f, 0.9f, 60.0f); echo (p, Dly4D, 0.45f, 0.15f);
      bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstRevMix, 0.35f); v.push_back (p); }
    { auto p = make ("Amber Drift", c); wave (p, 0, Pulse, 0.7f); unison (p, 0, 5, 16.0f); oddEven (p, -0.3f);
      lfoBeats (p, 1, 8.0f, ShSine); route (p, 0, P::SrcMod2, P::DstOddEven, 0.4f); prism (p, 12.0f, 20.0f, 2.0f); harm (p, 4500.0f);
      amp (p, 1.0f, 1.0f, 0.9f, 3.0f); verb (p, 6.0f, 0.35f, 0.75f); echo (p, Dly8D, 0.4f, 0.15f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Dub Cathedral", c); stack (p, Saw, 0.5f, 7, 12); unison (p, 0, 3, 12.0f);
      filter (p, 0, 1200.0f, 0.2f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f); shimmer (p, 8.0f, 0.2f);
      amp (p, 1.5f, 1.0f, 1.0f, 5.0f); verb (p, 9.0f, 0.5f, 0.95f, 50.0f); dubEcho (p, 0.5f, 0.25f, Dly4D);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Sunset Sub Pad", c); wave (p, 0, Saw, 0.6f); unison (p, 0, 3, 10.0f); wave (p, 1, Sine, 0.6f, -12); bassMono (p, 0);
      prism (p, 20.0f, 3.0f, 1.2f); lfoBeats (p, 1, 16.0f, ShSine); route (p, 0, P::SrcMod2, P::DstPrism, 0.3f); harm (p, 1800.0f, 0.2f);
      amp (p, 2.0f, 1.0f, 1.0f, 3.5f); verb (p, 4.0f, 0.25f, 0.7f, 40.0f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Northern Lights", c); wave (p, 0, Saw, 0.75f); unison (p, 0, 3, 8.0f); p.gain = octavesFifths (0.05f);
      p.depth = alternating (-0.8f, 0.5f); depthSrc (p, 1, 1.0f); shUnit (p, 1, 2.0f); p.set ("m2_smooth", 10.0f);
      prism (p, 12.0f, 24.0f, 2.0f); shimmer (p, 6.0f, 0.3f); amp (p, 1.0f, 1.0f, 0.9f, 4.0f); verb (p, 7.0f, 0.4f, 0.8f); echo (p, Dly8D, 0.45f, 0.2f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
}

//==============================================================================
void basses (std::vector<FP>& v)
{
    const juce::String c = "Basses";
    { auto p = make ("Deep Dub Sub", c); monoVoice (p); wave (p, 0, Sine, 0.9f); wave (p, 1, Tri, 0.35f);
      filter (p, 0, 350.0f, 0.2f, 0.3f, 0.002f, 0.4f, 0.2f, 0.2f); amp (p, 0.003f, 0.4f, 0.8f, 0.25f); verb (p, 2.0f, 0.1f, 0.6f, 30.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Rolling Minor Bass", c); monoVoice (p); wave (p, 0, Saw, 0.8f); wave (p, 1, Sine, 0.5f, -12);
      filter (p, 0, 300.0f, 0.3f, 0.45f, 0.002f, 0.18f, 0.1f, 0.08f, 30.0f); dist (p, 0, 0.15f, 0.4f); echo (p, Dly8D, 0.3f, 0.08f);
      amp (p, 0.002f, 0.25f, 0.7f, 0.08f); bloomMacro (p, P::DstFltCutoff, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Reese Dreams", c); monoVoice (p, true, 0.05f); wave (p, 0, Saw, 0.7f); wave (p, 1, Saw, 0.7f, 0, 12.0f);
      bassMono (p, 0, 150.0f); bassMono (p, 1, 150.0f); unison (p, 0, 2, 10.0f); filter (p, 0, 700.0f, 0.15f, 0.0f, 0.002f, 0.3f, 1.0f, 0.1f);
      lfoBeats (p, 1, 8.0f, ShTri); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.1f); amp (p, 0.005f, 0.4f, 0.9f, 0.15f); verb (p, 3.0f, 0.15f, 0.6f, 35.0f);
      bloomMacro (p, P::DstFltCutoff, 0.35f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Soft Rubber", c); monoVoice (p); wave (p, 0, Sine, 0.85f); wave (p, 1, Sine, 0.0f, 12); fm (p, 0, 1, 0.08f);
      envUnit (p, 0, 0.15f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Fm, 0.3f); wave (p, 2, Tri, 0.4f);
      amp (p, 0.002f, 0.3f, 0.6f, 0.08f); verb (p, 2.0f, 0.1f, 0.6f, 30.0f);
      bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Dub Stab Bass", c); monoVoice (p); wave (p, 0, Sqr, 0.8f); filter (p, 0, 350.0f, 0.4f, 0.5f, 0.002f, 0.2f, 0.0f, 0.12f);
      dubEcho (p, 0.5f, 0.25f); amp (p, 0.002f, 0.35f, 0.0f, 0.25f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.4f); v.push_back (p); }
    { auto p = make ("Sub Thump", c); monoVoice (p); wave (p, 0, Tri, 0.8f); wave (p, 1, Sine, 0.6f, -12);
      envUnit (p, 0, 0.1f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Pitch, 0.06f); amp (p, 0.002f, 0.3f, 0.5f, 0.08f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstO2Level, 0.3f); v.push_back (p); }
    { auto p = make ("Glide Sine", c); monoVoice (p, true, 0.08f); wave (p, 0, Sine, 0.9f); wave (p, 1, Tri, 0.25f, 12);
      filter (p, 0, 900.0f, 0.1f, 0.0f, 0.002f, 0.3f, 1.0f, 0.2f); shimmer (p, 4.0f, 0.5f); amp (p, 0.005f, 0.5f, 0.9f, 0.2f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstO2Level, 0.4f); v.push_back (p); }
    { auto p = make ("Driftwood", c); monoVoice (p); wave (p, 0, Saw, 0.8f); bassMono (p, 0); harm (p, 800.0f, 0.3f);
      envUnit (p, 0, 0.25f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.3f); prism (p, 12.0f, 6.0f, 1.5f);
      lfoBeats (p, 1, 16.0f, ShSine); route (p, 1, P::SrcMod2, P::DstPrismCenter, 0.15f); amp (p, 0.002f, 0.3f, 0.8f, 0.1f); echo (p, Dly8D, 0.3f, 0.1f);
      bloomMacro (p, P::DstCutoff, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Hollow Sub Organ", c); monoVoice (p); wave (p, 0, Saw, 0.7f); p.gain = drawbars (1.0f, 0.6f, 0.3f, 0.0f);
      filter (p, 0, 600.0f, 0.2f, 0.0f, 0.002f, 0.3f, 1.0f, 0.15f); amp (p, 0.005f, 0.3f, 1.0f, 0.15f); verb (p, 2.5f, 0.12f, 0.6f, 35.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Velvet 303", c); monoVoice (p, true, 0.06f); wave (p, 0, Saw, 0.85f); harm (p, 420.0f, 0.55f);
      envUnit (p, 0, 0.3f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.35f); route (p, 1, P::SrcVel, P::DstCutoff, 0.15f);
      dist (p, 0, 0.2f, 0.4f); echo (p, Dly8D, 0.35f, 0.12f); amp (p, 0.002f, 0.3f, 0.85f, 0.05f);
      bloomMacro (p, P::DstCutoff, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Warm FM Bass", c); monoVoice (p); wave (p, 0, Sine, 0.85f); wave (p, 1, Sine, 0.0f); fm (p, 0, 1, 0.1f);
      lfoBeats (p, 1, 8.0f, ShSine); route (p, 0, P::SrcMod2, P::DstO1Fm, 0.15f); filter (p, 0, 1200.0f, 0.1f, 0.0f, 0.002f, 0.3f, 1.0f, 0.15f);
      amp (p, 0.003f, 0.4f, 0.8f, 0.15f); verb (p, 2.0f, 0.1f, 0.6f, 30.0f);
      bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Ocean Floor", c); monoVoice (p); wave (p, 0, Saw, 0.6f, -12); unison (p, 0, 3, 12.0f); bassMono (p, 0, 160.0f); wave (p, 1, Sine, 0.6f, -12);
      filter (p, 0, 400.0f, 0.25f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f); lfoBeats (p, 1, 16.0f, ShTri); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.12f);
      amp (p, 0.01f, 0.5f, 0.9f, 0.3f); verb (p, 4.0f, 0.2f, 0.7f, 30.0f); echo (p, Dly4D, 0.3f, 0.1f, true, 1500.0f, 300.0f);
      bloomMacro (p, P::DstFltCutoff, 0.35f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
}

//==============================================================================
// Arpeggios: sequencer arps in the minor key of the held note
void arps (std::vector<FP>& v)
{
    const juce::String c = "Arpeggios";
    { auto p = make ("Crystal Rise", c); seqOn (p, "Minor Arp Rise", R16, 0.45f); wave (p, 0, Saw, 0.8f); unison (p, 0, 3, 8.0f); harm (p, 2500.0f, 0.2f);
      envUnit (p, 0, 0.25f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.3f); prism (p, 10.0f, 12.0f, 2.0f);
      amp (p, 0.002f, 0.3f, 0.2f, 0.2f); echo (p, Dly8D, 0.45f, 0.22f); verb (p, 4.0f, 0.25f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Trance Lift", c); seqOn (p, "Trance Lift", R16, 0.45f); wave (p, 0, Saw, 0.75f); unison (p, 0, 5, 16.0f);
      filter (p, 0, 1500.0f, 0.25f, 0.3f, 0.002f, 0.18f, 0.2f, 0.12f); lfoBeats (p, 1, 16.0f, ShSawUp); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.2f);
      ott (p, 25.0f); amp (p, 0.002f, 0.25f, 0.4f, 0.15f); echo (p, Dly8D, 0.45f, 0.22f); verb (p, 3.0f, 0.2f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Music Box", c); seqOn (p, "Poly 5 Bells", R16, 0.35f); wave (p, 0, Sine, 0.8f); wave (p, 1, Sine, 0.0f, 24); fm (p, 0, 1, 0.1f);
      envUnit (p, 0, 0.3f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Fm, 0.3f); stretch (p, 0.03f);
      amp (p, 0.001f, 0.5f, 0.0f, 0.5f); echo (p, Dly8D, 0.5f, 0.25f); verb (p, 6.0f, 0.35f, 0.8f);
      bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Glass Steps", c); seqOn (p, "Poly 7 Glass", R16, 0.4f); wave (p, 0, Tri, 0.9f); p.gain = bell(); stretch (p, 0.05f);
      prism (p, 15.0f, 12.0f, 2.0f); ott (p, 25.0f); amp (p, 0.001f, 0.35f, 0.0f, 0.3f); echo (p, Dly4D, 0.5f, 0.25f); verb (p, 5.0f, 0.3f, 0.8f);
      bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Falling Leaves", c); seqOn (p, "Melancholy Fall", R16, 0.5f); wave (p, 0, Saw, 0.8f); harm (p, 1800.0f, 0.25f);
      envUnit (p, 0, 0.2f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.3f); shimmer (p, 5.0f, 0.5f);
      amp (p, 0.002f, 0.3f, 0.3f, 0.2f); echo (p, Dly8D, 0.4f, 0.2f); verb (p, 5.0f, 0.3f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Dorian Current", c); seqOn (p, "Dorian Drift", R16, 0.5f); wave (p, 0, Sqr, 0.7f); wave (p, 1, Saw, 0.3f, -12);
      filter (p, 0, 1200.0f, 0.2f, 0.3f, 0.002f, 0.2f, 0.2f, 0.15f); amp (p, 0.002f, 0.25f, 0.4f, 0.15f); echo (p, Dly8D, 0.4f, 0.18f); verb (p, 3.0f, 0.2f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Hypno Glass", c); seqOn (p, "Hypno Minor 12", R16, 0.45f); wave (p, 0, Sine, 0.7f); wave (p, 1, Tri, 0.4f, 12);
      shimmer (p, 12.0f, 1.5f); harm (p, 3000.0f); amp (p, 0.001f, 0.3f, 0.0f, 0.25f); echo (p, Dly8D, 0.5f, 0.25f); verb (p, 5.0f, 0.3f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Ninth Shimmer", c); seqOn (p, "Ninth Shimmer", R16, 0.4f); wave (p, 0, Saw, 0.7f); unison (p, 0, 3, 10.0f); p.gain = octavesFifths (0.05f);
      prism (p, 12.0f, 16.0f, 2.0f); amp (p, 0.002f, 0.3f, 0.3f, 0.25f); echo (p, Dly4D, 0.45f, 0.2f); verb (p, 6.0f, 0.35f, 0.8f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Triplet Drift", c); seqOn (p, "Triplet Drift", R16T, 0.4f); wave (p, 0, Sine, 0.8f); wave (p, 1, Sine, 0.0f, 19); fm (p, 0, 1, 0.1f);
      envUnit (p, 0, 0.25f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Fm, 0.25f); amp (p, 0.001f, 0.3f, 0.0f, 0.25f); echo (p, Dly8T, 0.45f, 0.2f); verb (p, 5.0f, 0.3f);
      bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Suspended Hope", c); seqOn (p, "Suspended Hope", R16, 0.5f); wave (p, 0, Saw, 0.75f); p.gain = bands (1, 3, 1.0f, 0.15f);
      p.depth = ramp (0.0f, 1.0f, 4); depthSrc (p, 1, 1.0f); lfoBeats (p, 1, 6.0f, ShTri); prism (p, 15.0f, 12.0f, 2.5f);
      amp (p, 0.002f, 0.3f, 0.5f, 0.15f); echo (p, Dly8D, 0.4f, 0.2f); verb (p, 4.0f, 0.25f);
      bloomMacro (p, P::DstTableAmt, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Broken Minor", c); seqOn (p, "Broken Minor 3-3-2", R16, 0.45f); wave (p, 0, Saw, 0.8f); unison (p, 0, 2, 8.0f); harm (p, 1600.0f, 0.3f);
      envUnit (p, 0, 0.2f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.3f); route (p, 1, P::SrcAccent, P::DstPrism, 0.3f); prism (p, 10.0f, 8.0f, 2.0f);
      amp (p, 0.002f, 0.25f, 0.4f, 0.12f); echo (p, Dly8D, 0.35f, 0.18f); verb (p, 3.0f, 0.2f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Rain Drops", c); seqOn (p, "Rain Drops", R16, 0.3f); wave (p, 0, Tri, 1.0f); stretch (p, 0.15f); prism (p, 25.0f, 5.0f, 2.0f);
      lfoBeats (p, 1, 16.0f, ShTri); route (p, 0, P::SrcMod2, P::DstPrismCenter, 0.3f); ott (p, 25.0f);
      amp (p, 0.001f, 0.2f, 0.0f, 0.15f); echo (p, Dly8D, 0.5f, 0.3f); verb (p, 6.0f, 0.4f, 0.85f);
      bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
}

//==============================================================================
// Keys: dub chords (stacked and single-voice), electric pianos, bells and plucks for playing by hand
void keys (std::vector<FP>& v)
{
    const juce::String c = "Keys";
    { auto p = make ("Dub Chord Stack", c); stack (p, Saw, 0.55f, 3, 10); unison (p, 0, 3, 10.0f);
      filter (p, 0, 700.0f, 0.3f, 0.4f, 0.002f, 0.25f, 0.0f, 0.3f, 50.0f); amp (p, 0.002f, 0.35f, 0.0f, 0.4f);
      dubEcho (p, 0.55f, 0.3f, Dly4D); verb (p, 4.0f, 0.25f, 0.7f, 45.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyFb, 0.3f); v.push_back (p); }
    { auto p = make ("Dub Chord Solo", c); wave (p, 0, Saw, 0.7f); unison (p, 0, 3, 12.0f);
      filter (p, 0, 800.0f, 0.3f, 0.4f, 0.002f, 0.3f, 0.0f, 0.35f, 40.0f); amp (p, 0.002f, 0.4f, 0.0f, 0.4f);
      dubEcho (p, 0.55f, 0.3f, Dly4D); verb (p, 4.0f, 0.25f, 0.7f, 45.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyFb, 0.3f); v.push_back (p); }
    { auto p = make ("Minor Nine Stack", c); stack (p, Saw, 0.5f, 3, 14); unison (p, 0, 3, 10.0f);
      filter (p, 0, 1200.0f, 0.2f, 0.3f, 0.002f, 0.5f, 0.2f, 0.5f); shimmer (p, 5.0f, 0.3f); amp (p, 0.003f, 0.6f, 0.2f, 0.5f);
      echo (p, Dly8D, 0.4f, 0.2f); verb (p, 5.0f, 0.3f, 0.75f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Foggy Rhodes", c); wave (p, 0, Sine, 0.8f); wave (p, 1, Sine, 0.0f, 24); fm (p, 0, 1, 0.07f); wave (p, 2, Tri, 0.3f, 12);
      envUnit (p, 0, 0.6f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Fm, 0.2f); route (p, 1, P::SrcVel, P::DstO1Fm, 0.1f);
      shimmer (p, 6.0f, 0.4f); amp (p, 0.002f, 1.5f, 0.3f, 0.6f); verb (p, 4.0f, 0.3f, 0.7f); echo (p, Dly8D, 0.3f, 0.12f);
      bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Glass Piano", c); wave (p, 0, Tri, 0.8f); wave (p, 1, Saw, 0.2f, 12); prism (p, 15.0f, 6.0f, 2.5f);
      envUnit (p, 0, 0.5f, ShDecay); route (p, 0, P::SrcMod1, P::DstPrism, 0.25f); harm (p, 3500.0f);
      amp (p, 0.002f, 1.5f, 0.2f, 0.6f); verb (p, 5.0f, 0.3f, 0.75f);
      bloomMacro (p, P::DstPrismCenter, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Music Box Keys", c); wave (p, 0, Saw, 0.7f); p.gain = bell(); stretch (p, 0.25f); unison (p, 0, 2, 4.0f);
      amp (p, 0.001f, 2.5f, 0.0f, 2.0f); echo (p, Dly4D, 0.45f, 0.2f); verb (p, 8.0f, 0.35f, 0.85f);
      bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Soft Organ", c); wave (p, 0, Saw, 0.75f); p.gain = drawbars (1.0f, 0.7f, 0.5f, 0.3f); shimmer (p, 4.0f, 0.5f); harm (p, 4000.0f);
      amp (p, 0.005f, 0.1f, 1.0f, 0.2f); verb (p, 3.0f, 0.2f, 0.7f); route (p, 0, P::SrcModWheel, P::DstShimmer, 0.3f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Dusty Pluck", c); wave (p, 0, Saw, 0.8f); harm (p, 1200.0f, 0.3f); filter (p, 0, 4000.0f, 0.1f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f);
      envUnit (p, 0, 0.3f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.35f); route (p, 1, P::SrcVel, P::DstCutoff, 0.15f);
      amp (p, 0.001f, 0.4f, 0.0f, 0.35f); echo (p, Dly8D, 0.4f, 0.18f); verb (p, 4.0f, 0.25f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Ocean Stab", c); stack (p, Sqr, 0.5f, 7, 12); filter (p, 0, 900.0f, 0.35f, 0.45f, 0.001f, 0.2f, 0.0f, 0.25f);
      amp (p, 0.001f, 0.3f, 0.0f, 0.3f); echo (p, Dly8D, 0.5f, 0.25f, true, 3000.0f, 300.0f); verb (p, 3.0f, 0.2f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f); v.push_back (p); }
    { auto p = make ("Velvet Keys", c); wave (p, 0, Saw, 0.6f); unison (p, 0, 5, 14.0f); wave (p, 1, Sine, 0.3f, -12);
      filter (p, 0, 1500.0f, 0.15f, 0.2f, 0.005f, 0.5f, 0.3f, 0.6f); amp (p, 0.005f, 0.8f, 0.5f, 0.8f); verb (p, 5.0f, 0.3f, 0.75f); echo (p, Dly4D, 0.35f, 0.15f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Formant Keys", c); wave (p, 0, Saw, 0.8f); p.gain = vowel (2, 8); p.depth = ramp (-0.6f, 0.6f, 2);
      depthSrc (p, 2, 0.7f); amp (p, 0.003f, 0.8f, 0.4f, 0.4f); prism (p, 8.0f, 12.0f, 2.0f); verb (p, 4.0f, 0.2f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstTableAmt, 0.4f); v.push_back (p); }
    { auto p = make ("Sine Fifths", c); wave (p, 0, Sine, 0.6f); wave (p, 1, Sine, 0.45f, 7); wave (p, 2, Tri, 0.3f, 12); shimmer (p, 8.0f, 0.6f);
      amp (p, 0.01f, 1.0f, 0.5f, 0.7f); echo (p, Dly4D, 0.45f, 0.2f); verb (p, 6.0f, 0.35f, 0.8f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
}

//==============================================================================
void atmospheres (std::vector<FP>& v)
{
    const juce::String c = "Atmospheres";
    { auto p = make ("Rain On Glass", c); resynth (p, 0, 3, 0.8f, 0.5f, 50.0f); shimmer (p, 12.0f, 0.3f);
      amp (p, 1.0f, 1.0f, 1.0f, 4.0f); echo (p, Dly4D, 0.55f, 0.3f); verb (p, 8.0f, 0.45f, 0.85f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstDlyFb, 0.3f); v.push_back (p); }
    { auto p = make ("Soft Gradient", c); resynth (p, 0, 2, 0.8f, 0.5f, 30.0f); prism (p, 15.0f, 10.0f, 3.0f);
      lfoBeats (p, 1, 16.0f, ShSine); route (p, 0, P::SrcMod2, P::DstPrismCenter, 0.3f); amp (p, 1.5f, 1.0f, 1.0f, 4.0f); verb (p, 9.0f, 0.5f, 0.9f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Sea Wash", c); wave (p, 0, Noise, 0.9f); wave (p, 1, Saw, 0.3f, -12); unison (p, 1, 3, 10.0f); bassMono (p, 1);
      filter (p, 2, 600.0f, 0.3f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f, 0.0f, 0.0f, 1); lfoBeats (p, 1, 16.0f, ShSine); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.3f);
      lfoBeats (p, 0, 8.0f, ShTri); route (p, 1, P::SrcMod1, P::DstFltCutoff, 0.1f);
      amp (p, 2.0f, 1.0f, 1.0f, 4.0f); verb (p, 8.0f, 0.4f, 0.85f); echo (p, Dly4D, 0.4f, 0.2f, true, 2000.0f, 200.0f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Shimmer Fog", c); wave (p, 0, Saw, 0.6f); unison (p, 0, 2, 10.0f); wave (p, 1, Noise, 0.06f); shimmer (p, 30.0f, 0.15f);
      p.gain = octaves (0.1f); harm (p, 5000.0f); amp (p, 2.5f, 1.0f, 1.0f, 5.0f); verb (p, 10.0f, 0.5f, 0.9f, 60.0f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstRevMix, 0.3f); v.push_back (p); }
    { auto p = make ("Falling Stars", c); resynth (p, 0, 9, 0.8f, 0.5f, 40.0f); stretch (p, 0.08f); prism (p, 15.0f, 16.0f, 3.0f);
      amp (p, 0.5f, 1.0f, 1.0f, 3.0f); echo (p, Dly4D, 0.5f, 0.2f); verb (p, 8.0f, 0.45f, 0.85f);
      bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Vowel Mist", c); resynth (p, 0, 5, 0.8f, 0.25f, 40.0f); shimmer (p, 8.0f, 0.3f);
      amp (p, 1.5f, 1.0f, 1.0f, 4.0f); echo (p, Dly8D, 0.45f, 0.2f); verb (p, 7.0f, 0.4f, 0.8f);
      bloomMacro (p, P::DstRsLift, 0.4f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Deep Sea Drone", c); wave (p, 0, Saw, 0.5f, -24); wave (p, 1, Saw, 0.5f, -12, 7.0f); wave (p, 2, Sine, 0.4f, -24);
      bassMono (p, 0); bassMono (p, 1); prism (p, 25.0f, 8.0f, 3.0f); lfoHz (p, 1, 0.05f, ShTri); route (p, 0, P::SrcMod2, P::DstPrismCenter, 0.35f);
      harm (p, 2500.0f, 0.2f); amp (p, 3.0f, 1.0f, 1.0f, 5.0f); verb (p, 10.0f, 0.5f, 0.9f, 35.0f);
      bloomMacro (p, P::DstCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Dub Siren Tail", c); wave (p, 0, Sine, 0.7f); wave (p, 1, Sine, 0.4f, 12);
      lfoBeats (p, 1, 4.0f, ShSine); route (p, 0, P::SrcMod2, P::DstO1Pitch, 0.02f); route (p, 1, P::SrcMod2, P::DstO2Pitch, 0.02f);
      amp (p, 0.3f, 1.0f, 1.0f, 3.0f); dubEcho (p, 0.65f, 0.4f); verb (p, 6.0f, 0.35f, 0.8f);
      bloomMacro (p, P::DstM2Rate, 0.3f); spaceMacro (p, P::DstDlyFb, 0.3f); v.push_back (p); }
    { auto p = make ("Rising Lines", c); resynth (p, 0, 0, 0.8f, 1.0f, 30.0f); shimmer (p, 10.0f, 0.3f);
      amp (p, 1.0f, 1.0f, 1.0f, 3.0f); echo (p, Dly4D, 0.5f, 0.25f); verb (p, 8.0f, 0.45f, 0.85f);
      bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Winter Wind", c); wave (p, 0, Noise, 1.0f); filter (p, 2, 1500.0f, 0.5f, 0.0f, 0.002f, 0.3f, 1.0f, 0.3f);
      lfoHz (p, 1, 0.12f, ShSine); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.4f); shUnit (p, 0, 1.0f); p.set ("m1_smooth", 10.0f);
      route (p, 1, P::SrcMod1, P::DstFltCutoff, 0.1f); amp (p, 3.0f, 1.0f, 1.0f, 5.0f); verb (p, 9.0f, 0.45f, 0.85f); echo (p, Dly4D, 0.4f, 0.15f);
      bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Ripple Field", c); resynth (p, 0, 7, 0.8f, 0.5f, 30.0f); prism (p, 20.0f, 12.0f, 3.0f);
      amp (p, 1.5f, 1.0f, 1.0f, 4.0f); verb (p, 8.0f, 0.45f, 0.85f); echo (p, Dly8D, 0.4f, 0.2f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstRevMix, 0.4f); v.push_back (p); }
    { auto p = make ("Tidal Spectrum", c); wave (p, 0, Saw, 0.7f); unison (p, 0, 5, 18.0f); prism (p, 30.0f, 12.0f, 4.0f);
      lfoBeats (p, 0, 16.0f, ShSine); route (p, 0, P::SrcMod1, P::DstPrismWidth, 0.3f); lfoBeats (p, 1, 12.0f, ShTri);
      route (p, 1, P::SrcMod2, P::DstPrismCenter, 0.3f); harm (p, 4000.0f); shimmer (p, 6.0f, 0.2f); amp (p, 2.5f, 1.0f, 1.0f, 5.0f); verb (p, 10.0f, 0.5f, 0.9f);
      bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstRevMix, 0.35f); v.push_back (p); }
}

//==============================================================================
// Sequences: one sound style per entry, combined with different patterns.
enum Style { DubChord, DubBass, TranceSuper, GlassPluck, MusicBox, VelvetAcid, SubRoll, PadStab, ShimmerKeys, ImageDrift,
             VoxHush, DeepPulse };

void sequenceSound (FP& p, Style s, int variant)
{
    const float vf = (float) (variant % 5) / 4.0f;   // 0..1 variation
    switch (s)
    {
        case DubChord:   // minor seventh chord stacked across the oscillators, filtered, with a long dub delay
            stack (p, Saw, 0.5f, 3, 10); unison (p, 0, 2, 10.0f);
            filter (p, 0, 650.0f + 300.0f * vf, 0.3f, 0.4f, 0.002f, 0.25f, 0.0f, 0.3f);
            dubEcho (p, 0.55f, 0.3f, variant % 2 ? Dly4D : Dly8D); verb (p, 4.0f, 0.25f, 0.7f, 45.0f); amp (p, 0.002f, 0.3f, 0.0f, 0.35f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstDlyFb, 0.3f);
            break;

        case DubBass:   // round sub with a soft filter envelope
            wave (p, 0, Sine, 0.9f); wave (p, 1, Tri, 0.3f);
            filter (p, 0, 300.0f + 100.0f * vf, 0.25f, 0.3f, 0.002f, 0.3f, 0.2f, 0.15f);
            if (variant % 2) dist (p, 0, 0.1f, 0.3f);
            amp (p, 0.002f, 0.35f, 0.8f, 0.15f); verb (p, 2.0f, 0.08f, 0.6f, 30.0f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;

        case TranceSuper:   // supersaw opening over four bars
            wave (p, 0, Saw, 0.75f); unison (p, 0, 5, 16.0f);
            filter (p, 0, 1200.0f + 600.0f * vf, 0.25f, 0.3f, 0.002f, 0.18f, 0.2f, 0.12f);
            lfoBeats (p, 1, 16.0f, ShSawUp); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.2f); ott (p, 25.0f);
            echo (p, Dly8D, 0.45f, 0.22f); verb (p, 3.0f, 0.2f); amp (p, 0.002f, 0.25f, 0.4f, 0.15f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;

        case GlassPluck:   // short glassy bell partials
            wave (p, 0, Tri, 1.0f); p.gain = bell(); stretch (p, 0.04f + 0.04f * vf); prism (p, 12.0f, 10.0f, 2.0f); ott (p, 25.0f);
            amp (p, 0.001f, 0.35f, 0.0f, 0.3f); echo (p, Dly8D, 0.5f, 0.25f); verb (p, 5.0f, 0.3f, 0.8f);
            bloomMacro (p, P::DstStretch, 0.1f); spaceMacro (p, P::DstDlyMix, 0.3f);
            break;

        case MusicBox:   // FM music box
            wave (p, 0, Sine, 0.8f); wave (p, 1, Sine, 0.0f, variant % 2 ? 19 : 24); fm (p, 0, 1, 0.1f);
            envUnit (p, 0, 0.3f, ShDecay); route (p, 0, P::SrcMod1, P::DstO1Fm, 0.3f);
            amp (p, 0.001f, 0.5f, 0.0f, 0.5f); echo (p, Dly8D, 0.5f, 0.25f); verb (p, 6.0f, 0.35f, 0.8f);
            bloomMacro (p, P::DstO1Fm, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;

        case VelvetAcid:   // soft acid line, low resonance, gentle drive
            wave (p, 0, variant % 2 ? Sqr : Saw, 0.85f); harm (p, 380.0f + 200.0f * vf, 0.55f);
            envUnit (p, 0, 0.25f, ShDecay); route (p, 0, P::SrcMod1, P::DstCutoff, 0.35f); route (p, 1, P::SrcAccent, P::DstCutoff, 0.2f);
            dist (p, 0, 0.2f, 0.4f); echo (p, Dly8D, 0.35f, 0.12f); amp (p, 0.002f, 0.3f, 0.85f, 0.05f);
            bloomMacro (p, P::DstCutoff, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f);
            break;

        case SubRoll:   // rolling sub with a slow low prism wobble
            wave (p, 0, Sine, 0.8f); wave (p, 1, Saw, 0.35f); bassMono (p, 1); prism (p, 15.0f, 4.0f, 1.5f);
            filter (p, 0, 450.0f, 0.25f, 0.3f, 0.002f, 0.15f, 0.2f, 0.1f);
            lfoBeats (p, 1, 8.0f, ShSine); route (p, 0, P::SrcMod2, P::DstPrismCenter, 0.2f);
            verb (p, 2.5f, 0.1f, 0.6f, 30.0f); amp (p, 0.002f, 0.3f, 0.7f, 0.05f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;

        case PadStab:   // soft detuned pad played by the sequencer
            wave (p, 0, Saw, 0.6f); unison (p, 0, 5, 14.0f); wave (p, 1, Sine, 0.3f, -12);
            filter (p, 0, 1500.0f, 0.15f, 0.25f, 0.005f, 0.4f, 0.3f, 0.5f);
            amp (p, 0.005f, 0.6f, 0.5f, 0.6f); verb (p, 5.0f, 0.3f, 0.75f); echo (p, Dly4D, 0.35f, 0.15f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;

        case ShimmerKeys:   // sine and triangle an octave apart, drifting partials
            wave (p, 0, Sine, 0.7f); wave (p, 1, Tri, 0.4f, 12); shimmer (p, 12.0f, 1.0f + 0.5f * vf); harm (p, 3000.0f);
            amp (p, 0.001f, 0.3f, 0.0f, 0.25f); echo (p, Dly8D, 0.5f, 0.25f); verb (p, 5.0f, 0.3f);
            bloomMacro (p, P::DstShimmer, 0.4f); spaceMacro (p, P::DstDlyMix, 0.3f);
            break;

        case ImageDrift:   // resynth of a soft image plus a sub
            resynth (p, 0, variant % 2 ? 2 : 3, 0.8f, 1.0f, 30.0f, 100.0f);
            wave (p, 1, Sine, 0.4f, -12); prism (p, 12.0f, 10.0f, 2.0f);
            amp (p, 0.002f, 0.3f, 0.7f, 0.1f); echo (p, Dly8D, 0.4f, 0.2f); verb (p, 4.0f, 0.25f);
            bloomMacro (p, P::DstPrism, 0.4f); spaceMacro (p, P::DstRsLift, 0.5f);
            break;

        case VoxHush:   // hushed vowels, picked at random every beat
            wave (p, 0, Saw, 0.8f); p.gain = vowel (2 + variant % 3, 7 + variant % 4);
            p.depth = ramp (-0.6f, 0.6f, 2); depthSrc (p, 1, 0.8f); shUnit (p, 1, 1.0f); p.set ("m2_smooth", 8.0f);
            filter (p, 0, 3000.0f, 0.1f, 0.0f, 0.002f, 0.3f, 1.0f, 0.2f); amp (p, 0.002f, 0.3f, 0.6f, 0.1f);
            echo (p, Dly8D, 0.35f, 0.15f); verb (p, 3.0f, 0.2f);
            bloomMacro (p, P::DstTableAmt, 0.3f); spaceMacro (p, P::DstDlyMix, 0.3f);
            break;

        case DeepPulse:   // wide, soft pulse bass with a slow filter sweep
            wave (p, 0, Saw, 0.8f); unison (p, 0, 3, 12.0f); bassMono (p, 0, 150.0f);
            filter (p, 0, 500.0f + 400.0f * vf, 0.3f, 0.35f, 0.002f, 0.12f, 0.1f, 0.06f, 30.0f);
            lfoBeats (p, 1, 16.0f, ShTri); route (p, 0, P::SrcMod2, P::DstFltCutoff, 0.15f);
            dist (p, 0, 0.12f, 0.3f); amp (p, 0.002f, 0.25f, 0.6f, 0.05f); verb (p, 2.0f, 0.08f, 0.6f, 30.0f);
            bloomMacro (p, P::DstFltCutoff, 0.3f); spaceMacro (p, P::DstRevMix, 0.3f);
            break;
    }
}

void sequences (std::vector<FP>& v)
{
    struct S { const char* name; const char* pattern; Style style; int rate; float gate; };
    const S list[] =
    {
        { "Skank Chords",        "Dub Skank",            DubChord,     R16,  0.5f  },
        { "Dub Bass Sway",       "Dub Bass Sway",        DubBass,      R16,  0.8f  },
        { "Lift Supersaw",       "Trance Lift",          TranceSuper,  R16,  0.45f },
        { "Glass Poly Five",     "Poly 5 Bells",         GlassPluck,   R16,  0.4f  },
        { "Music Box Fall",      "Melancholy Fall",      MusicBox,     R16,  0.4f  },
        { "Velvet Roller",       "Deep Roller",          VelvetAcid,   R16,  0.55f },
        { "Sub Octave Sway",     "Octave Sway",          SubRoll,      R16,  0.5f  },
        { "Pad Stab Echoes",     "Echo Stabs",           PadStab,      R16,  0.4f  },
        { "Shimmer Hypno",       "Hypno Minor 12",       ShimmerKeys,  R16,  0.45f },
        { "Image Drift",         "Init",                 ImageDrift,   R16,  0.5f  },
        { "Hushed Voices",       "Gen Minor Drift",      VoxHush,      R16,  0.5f  },
        { "Deep Pulse",          "Sub Pulse",            DeepPulse,    R16,  0.6f  },
        { "Chord Echo Dub",      "Dub Chord Echo",       DubChord,     R16,  0.6f  },
        { "Sub Pulse Dub",       "Sub Pulse",            DubBass,      R16,  0.7f  },
        { "Sixteenth Dream",     "Sixteenth Dream",      TranceSuper,  R16,  0.45f },
        { "Glass Poly Seven",    "Poly 7 Glass",         GlassPluck,   R16,  0.4f  },
        { "Music Box Rise",      "Minor Arp Rise",       MusicBox,     R16,  0.4f  },
        { "Velvet Dorian",       "Dorian Drift",         VelvetAcid,   R16,  0.5f  },
        { "Minor Walk Sub",      "Minor Walk 64",        SubRoll,      R16,  0.5f  },
        { "Suspended Stabs",     "Suspended Hope",       PadStab,      R16,  0.45f },
        { "Shimmer Ninths",      "Ninth Shimmer",        ShimmerKeys,  R16,  0.4f  },
        { "Gradient Drift",      "Gen Soft Pulse",       ImageDrift,   R16,  0.5f  },
        { "Dorian Voices",       "Gen Dorian Flow",      VoxHush,      R16,  0.5f  },
        { "Gallop Pulse",        "Gallop Minor",         DeepPulse,    R16,  0.5f  },
        { "Chord Drift Dub",     "Dub Chord Drift",      DubChord,     R16,  0.55f },
        { "Sparse Dub Bass",     "Gen Sparse Dub",       DubBass,      R16,  0.7f  },
        { "Rising Supersaw",     "Rising Minor",         TranceSuper,  R16,  0.5f  },
        { "Rain Glass",          "Rain Drops",           GlassPluck,   R16,  0.3f  },
        { "Triplet Music Box",   "Triplet Drift",        MusicBox,     R16T, 0.4f  },
        { "Velvet Pentatonic",   "Pentatonic Rain",      VelvetAcid,   R16,  0.5f  },
        { "Slow Tide Sub",       "Slow Tide 32",         SubRoll,      R16,  0.8f  },
        { "Chord Swell Stabs",   "Chord Swell 32",       PadStab,      R16,  0.6f  },
        { "Shimmer Chimes",      "Gen Minor 7 Chimes",   ShimmerKeys,  R16,  0.4f  },
        { "Broken Image",        "Broken Minor 3-3-2",   ImageDrift,   R16,  0.5f  },
        { "Long Dorian Voices",  "Gen Long Dorian 64",   VoxHush,      R16,  0.5f  },
        { "Rolling Pulse 32",    "Gen Rolling Minor 32", DeepPulse,    R16,  0.5f  },
        { "Offbeat Dub Chords",  "Offbeat Minor",        DubChord,     R16,  0.5f  },
        { "Tide Bass 64",        "Gen Slow Tide 64",     DubBass,      R16,  0.8f  },
        { "Hypnotic Supersaw",   "Gen Hypnotic 32",      TranceSuper,  R16,  0.45f },
        { "Poly Three Drops",    "Poly 3 Drops",         GlassPluck,   R16,  0.3f  },
    };

    int variant = 0;
    for (auto& s : list)
    {
        auto p = make (s.name, "Sequences");
        seqOn (p, s.pattern, s.rate, s.gate);
        sequenceSound (p, s.style, variant++);
        v.push_back (p);
    }
}

// Per-preset master level, measured offline so presets play at a similar loudness.
const std::map<juce::String, float>& levelTrims()
{
    static const std::map<juce::String, float> m
    {
        { "Init", -3.7f },
        { "Morning Haze", -10.0f },
        { "Dub Horizon", -8.9f },
        { "Glass Chorale", -12.5f },
        { "Velvet Strings", -10.1f },
        { "Submerged", -12.1f },
        { "Choir Of Rain", -4.7f },
        { "Tape Bloom", -9.0f },
        { "Frozen Lake", -10.3f },
        { "Amber Drift", -9.0f },
        { "Dub Cathedral", -13.0f },
        { "Sunset Sub Pad", -10.7f },
        { "Northern Lights", -6.2f },
        { "Deep Dub Sub", -2.9f },
        { "Rolling Minor Bass", -0.1f },
        { "Reese Dreams", -0.8f },
        { "Soft Rubber", -0.9f },
        { "Dub Stab Bass", 5.4f },
        { "Sub Thump", 3.3f },
        { "Glide Sine", -1.3f },
        { "Driftwood", 4.1f },
        { "Hollow Sub Organ", 1.6f },
        { "Velvet 303", -1.1f },
        { "Warm FM Bass", -0.2f },
        { "Ocean Floor", -0.4f },
        { "Crystal Rise", 0.1f },
        { "Trance Lift", 4.0f },
        { "Music Box", -4.8f },
        { "Glass Steps", 1.6f },
        { "Falling Leaves", -2.4f },
        { "Dorian Current", -2.6f },
        { "Hypno Glass", -0.8f },
        { "Ninth Shimmer", 0.6f },
        { "Triplet Drift", -6.6f },
        { "Suspended Hope", 3.7f },
        { "Broken Minor", 5.0f },
        { "Rain Drops", 2.3f },
        { "Dub Chord Stack", 1.8f },
        { "Dub Chord Solo", 3.6f },
        { "Minor Nine Stack", -1.6f },
        { "Foggy Rhodes", -7.4f },
        { "Glass Piano", -4.1f },
        { "Music Box Keys", -4.2f },
        { "Soft Organ", -2.4f },
        { "Dusty Pluck", 0.6f },
        { "Ocean Stab", -1.0f },
        { "Velvet Keys", -4.4f },
        { "Formant Keys", 5.4f },
        { "Sine Fifths", -5.8f },
        { "Rain On Glass", -2.3f },
        { "Soft Gradient", -3.6f },
        { "Sea Wash", 6.0f },
        { "Shimmer Fog", -12.5f },
        { "Falling Stars", -1.0f },
        { "Vowel Mist", -5.2f },
        { "Deep Sea Drone", -13.1f },
        { "Dub Siren Tail", -9.1f },
        { "Rising Lines", 3.8f },
        { "Winter Wind", 6.0f },
        { "Ripple Field", 0.7f },
        { "Tidal Spectrum", -14.3f },
        { "Skank Chords", 6.0f },
        { "Dub Bass Sway", -2.1f },
        { "Lift Supersaw", 2.6f },
        { "Glass Poly Five", 1.5f },
        { "Music Box Fall", -8.1f },
        { "Velvet Roller", -4.6f },
        { "Sub Octave Sway", 2.0f },
        { "Pad Stab Echoes", 1.2f },
        { "Shimmer Hypno", -0.8f },
        { "Image Drift", 6.0f },
        { "Hushed Voices", 6.0f },
        { "Deep Pulse", 1.6f },
        { "Chord Echo Dub", 6.0f },
        { "Sub Pulse Dub", 1.5f },
        { "Sixteenth Dream", 3.5f },
        { "Glass Poly Seven", 0.3f },
        { "Music Box Rise", -7.9f },
        { "Velvet Dorian", -4.1f },
        { "Minor Walk Sub", 1.2f },
        { "Suspended Stabs", -1.7f },
        { "Shimmer Ninths", -1.6f },
        { "Gradient Drift", 4.4f },
        { "Dorian Voices", 6.0f },
        { "Gallop Pulse", 1.2f },
        { "Chord Drift Dub", 4.5f },
        { "Sparse Dub Bass", -1.8f },
        { "Rising Supersaw", 1.7f },
        { "Rain Glass", 2.1f },
        { "Triplet Music Box", -10.2f },
        { "Velvet Pentatonic", -4.0f },
        { "Slow Tide Sub", -1.1f },
        { "Chord Swell Stabs", 3.4f },
        { "Shimmer Chimes", 0.6f },
        { "Broken Image", 4.7f },
        { "Long Dorian Voices", 6.0f },
        { "Rolling Pulse 32", -2.5f },
        { "Offbeat Dub Chords", 5.7f },
        { "Tide Bass 64", -1.9f },
        { "Hypnotic Supersaw", 3.7f },
        { "Poly Three Drops", 0.5f },
    };
    return m;
}

} // namespace

const std::vector<FactoryPreset>& PresetManager::factory()
{
    static const std::vector<FactoryPreset> list = []
    {
        std::vector<FactoryPreset> v;
        // Init: a single plain sine (the parameter defaults), loaded when the plugin opens.
        { auto p = make ("Init", "Init"); wave (p, 0, Sine, 0.8f);
          for (int i = 1; i <= 3; ++i) p.set ("o" + juce::String (i) + "_wave", (float) P::WSine);
          p.set ("o1_fmSrc", 2.0f).set ("o2_fmSrc", 3.0f);
          p.set ("o2_on", 1.0f).set ("o3_on", 1.0f).set ("o2_level", 0.0f).set ("o3_level", 0.0f); v.push_back (p); }
        pads (v);
        basses (v);
        arps (v);
        keys (v);
        atmospheres (v);
        sequences (v);
        for (auto& p : v)
        {
            auto it = levelTrims().find (p.name);
            if (it != levelTrims().end()) p.set ("master", juce::jmin (6.0f, it->second + p.masterComp));
        }
        return v;
    }();
    return list;
}

//==============================================================================
void PresetManager::refresh()
{
    entries.clear();
    const auto& f = factory();
    for (int i = 0; i < (int) f.size(); ++i)
        entries.push_back ({ f[(size_t) i].name, f[(size_t) i].category, true, i, {} });

    auto dir = userPresetDir();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, true, "*.elukka");
        files.sort();
        for (auto& file : files)
        {
            auto cat = file.getParentDirectory() == dir ? juce::String ("User") : file.getParentDirectory().getFileName();
            entries.push_back ({ file.getFileNameWithoutExtension(), cat, false, -1, file });
        }
    }
    refreshPatterns();
}

void PresetManager::applyFactory (const FactoryPreset& fp)
{
    for (auto* param : proc.getParameters())
        param->setValueNotifyingHost (param->getDefaultValue());

    for (auto& [id, value] : fp.values)
        if (auto* param = proc.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));

    for (int i = 0; i < P::numMods; ++i) proc.shapes[i].setPoints (ModShape::preset (fp.shape[i]));
    proc.resetHarmTables();
    proc.resetFxOrder();
    for (size_t i = 0; i < fp.gain.size() && i < proc.harmGain.size(); ++i) proc.harmGain[i] = fp.gain[i];
    for (size_t i = 0; i < fp.depth.size() && i < proc.harmDepth.size(); ++i) proc.harmDepth[i] = fp.depth[i];

    const auto& pats = SeqPattern::factory();
    proc.pattern = pats[(size_t) (fp.pattern < 0 ? SeqPattern::initIndex() : juce::jlimit (0, (int) pats.size() - 1, fp.pattern))];

    // a preset brings its own generated image or clears the resynth source
    proc.setResynth (fp.image >= 0 ? ResynthData::fromImage (presetImage (fp.image), fp.name + " (image)") : nullptr);
}

class PresetManager::HistoryAction final : public juce::UndoableAction
{
public:
    HistoryAction (PresetManager& managerIn, Snapshot beforeIn, std::function<bool()> firstApplyIn)
        : manager (managerIn), before (std::move (beforeIn)), firstApply (std::move (firstApplyIn)) {}

    bool perform() override
    {
        if (! hasAfter)
        {
            if (! firstApply()) return false;
            after = manager.captureSnapshot();
            hasAfter = true;
            return true;
        }

        return manager.restoreSnapshot (after);
    }

    bool undo() override
    {
        return manager.restoreSnapshot (before);
    }

    int getSizeInUnits() override { return 1; }

private:
    PresetManager& manager;
    Snapshot before, after;
    std::function<bool()> firstApply;
    bool hasAfter = false;
};

class PresetManager::SnapshotAction final : public juce::UndoableAction
{
public:
    SnapshotAction (PresetManager& managerIn, Snapshot beforeIn, Snapshot afterIn)
        : manager (managerIn), before (std::move (beforeIn)), after (std::move (afterIn)) {}

    bool perform() override
    {
        // The state is already at `after` when the action is recorded, so only a redo has to apply it.
        if (! firstPerform)
        {
            firstPerform = true;
            return true;
        }
        return manager.restoreSnapshot (after);
    }

    bool undo() override { return manager.restoreSnapshot (before); }
    int getSizeInUnits() override { return 1; }

private:
    PresetManager& manager;
    Snapshot before, after;
    bool firstPerform = false;
};

class PresetManager::ParamAction final : public juce::UndoableAction
{
public:
    ParamAction (PresetManager& managerIn, std::vector<ParamChange> changesIn)
        : manager (managerIn), changes (std::move (changesIn)) {}

    bool perform() override
    {
        // The values are already at `after` when the action is recorded, so only a redo has to apply them.
        if (! firstPerform)
        {
            firstPerform = true;
            return true;
        }
        return manager.applyParamChanges (changes, false);
    }

    bool undo() override { return manager.applyParamChanges (changes, true); }
    int getSizeInUnits() override { return 1; }

private:
    PresetManager& manager;
    std::vector<ParamChange> changes;
    bool firstPerform = false;
};

namespace
{
// A VST3 group edit: the DAW records every parameter change made inside it as one undo step.
struct HostEditGroup
{
    explicit HostEditGroup (ElukkaAudioProcessor& p) : proc (p), active (p.beginHostGroupEdit()) {}
    ~HostEditGroup() { if (active) proc.endHostGroupEdit(); }

    ElukkaAudioProcessor& proc;
    const bool active;
};
}

bool PresetManager::applyPreset (const Entry& entry)
{
    HostEditGroup group (proc);

    applyingState = true;
    if (group.active)
        for (auto* param : proc.getParameters())
            param->beginChangeGesture();
    const juce::ScopeGuard finish { [this, gestures = group.active]
    {
        if (gestures)
            for (auto* param : proc.getParameters())
                param->endChangeGesture();
        applyingState = false;
    } };

    if (entry.isFactory)
    {
        const auto& presets = factory();
        if (entry.index < 0 || entry.index >= (int) presets.size()) return false;
        applyFactory (presets[(size_t) entry.index]);
    }
    else
    {
        auto xml = juce::XmlDocument::parse (entry.file);
        if (xml == nullptr) return false;

        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid() || ! state.hasType (proc.apvts.state.getType())) return false;

        // Parameters missing from older files fall back to defaults.
        for (auto* param : proc.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
        proc.loadState (state);
    }

    currentIndex = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].isFactory == entry.isFactory && entries[(size_t) i].index == entry.index && entries[(size_t) i].file == entry.file)
        {
            currentIndex = i;
            break;
        }
    currentName = entry.name;
    proc.bumpUiVersion();
    return true;
}

bool PresetManager::applyPattern (const PatternEntry& entry)
{
    if (entry.isFactory)
    {
        const auto& factoryPatterns = SeqPattern::factory();
        if (entry.index < 0 || entry.index >= (int) factoryPatterns.size()) return false;
        proc.pattern = factoryPatterns[(size_t) entry.index];
    }
    else
    {
        auto xml = juce::XmlDocument::parse (entry.file);
        if (xml == nullptr) return false;

        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid() || ! state.hasType ("Pattern")) return false;
        proc.pattern.fromTree (state);
    }

    proc.bumpUiVersion();
    return true;
}

PresetManager::Snapshot PresetManager::captureSnapshot()
{
    Snapshot snapshot;
    snapshot.state = proc.saveState (false);
    snapshot.resynth = proc.getResynth();
    snapshot.uiScale = proc.uiScale;
    snapshot.currentIndex = currentIndex;
    snapshot.currentName = currentName;
    if (currentIndex >= 0 && currentIndex < (int) entries.size())
    {
        const auto& entry = entries[(size_t) currentIndex];
        snapshot.isFactory = entry.isFactory;
        snapshot.factoryIndex = entry.index;
        snapshot.file = entry.file;
    }
    return snapshot;
}

bool PresetManager::restoreSnapshot (const Snapshot& snapshot)
{
    HostEditGroup group (proc);

    applyingState = true;
    if (group.active)
        for (auto* param : proc.getParameters())
            param->beginChangeGesture();
    const juce::ScopeGuard finish { [this, gestures = group.active]
    {
        if (gestures)
            for (auto* param : proc.getParameters())
                param->endChangeGesture();
        applyingState = false;
    } };

    if (proc.getResynth() != snapshot.resynth)
        proc.setResynth (snapshot.resynth);
    proc.loadState (snapshot.state, false);
    proc.uiScale = snapshot.uiScale;

    applyNameFrom (snapshot);
    return true;
}

bool PresetManager::applyParamChanges (const std::vector<ParamChange>& changes, bool useBefore)
{
    if (changes.empty()) return true;

    HostEditGroup group (proc);

    applyingState = true;
    if (group.active)
        for (auto& change : changes)
            if (auto* param = getParam (change.index))
                param->beginChangeGesture();

    for (auto& change : changes)
        if (auto* param = getParam (change.index))
            param->setValueNotifyingHost (param->convertTo0to1 (useBefore ? change.before : change.after));

    if (group.active)
        for (auto& change : changes)
            if (auto* param = getParam (change.index))
                param->endChangeGesture();

    applyingState = false;
    proc.bumpUiVersion();
    return true;
}

bool PresetManager::snapshotsEqual (const Snapshot& a, const Snapshot& b)
{
    return statesEqual (a.state, b.state)
        && a.resynth == b.resynth
        && a.uiScale == b.uiScale
        && a.currentName == b.currentName
        && a.isFactory == b.isFactory
        && a.factoryIndex == b.factoryIndex
        && a.file == b.file;
}

bool PresetManager::statesEqual (const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (a.getType() != b.getType()
        || a.getNumChildren() != b.getNumChildren()
        || a.getNumProperties() != b.getNumProperties())
        return false;

    // Some edits live on the root, not in a parameter child (the FX order is one): they are steps too.
    for (int i = 0; i < a.getNumProperties(); ++i)
    {
        const auto name = a.getPropertyName (i);
        if (! b.hasProperty (name) || a[name] != b[name]) return false;
    }

    for (int i = 0; i < a.getNumChildren(); ++i)
    {
        const auto child = a.getChild (i);
        const auto id = child["id"].toString();

        if (id.isEmpty())
        {
            if (! child.isEquivalentTo (b.getChild (i))) return false;
            continue;
        }

        const auto other = b.getChildWithProperty ("id", id);
        if (! other.isValid()) return false;

        // Host parameter echoes can round the value slightly; that is not a new edit.
        const auto x = (double) child["value"], y = (double) other["value"];
        if (std::abs (x - y) > 1.0e-5 * juce::jmax (1.0, std::abs (x)))
            return false;
    }

    return true;
}

void PresetManager::commitUnrecordedChanges()
{
    const auto current = captureSnapshot();
    if (! hasCheckpoint)
    {
        checkpoint = current;
        hasCheckpoint = true;
        rememberState (checkpoint);
        return;
    }

    // Values the host moved (automation playback, the DAW's own undo, a project load) are not user edits.
    // Compare against a copy where those parameters still have the checkpoint's value, so they never
    // become a history step; undoing an edit must not freeze such a parameter at an old position.
    std::vector<int> moved;
    for (int i = 0; i < numTrackedParams; ++i)
        if (hostMoved != nullptr && hostMoved[(size_t) i].exchange (false))
            moved.push_back (i);

    auto comparable = current;
    if (! moved.empty())
    {
        comparable.state = current.state.createCopy();
        for (int index : moved)
        {
            const auto& id = paramIds[(size_t) index];
            if (id.isEmpty()) continue;

            const auto source = checkpoint.state.getChildWithProperty ("id", id);
            auto target = comparable.state.getChildWithProperty ("id", id);
            if (source.isValid() && target.isValid())
                target.setProperty ("value", source["value"], nullptr);
        }
    }

    if (snapshotsEqual (checkpoint, comparable))
    {
        checkpoint = current;   // nothing the user did: keep the host's values as the new baseline
        return;
    }

    history.beginNewTransaction();
    if (history.perform (new SnapshotAction (*this, checkpoint, current)))
    {
        checkpoint = current;
        rememberState (checkpoint);
    }
}

void PresetManager::commitGestureChanges()
{
    std::vector<ParamChange> changes;

    for (auto& [index, before] : gestureBefore)
        if (auto* param = getParam (index))
        {
            const float after = param->convertFrom0to1 (param->getValue());
            if (std::abs (after - before) > 1.0e-5f * juce::jmax (1.0f, std::abs (before)))
                changes.push_back ({ index, before, after });
        }

    gestureBefore.clear();
    if (changes.empty()) return;

    history.beginNewTransaction();
    if (history.perform (new ParamAction (*this, std::move (changes))))
    {
        checkpoint = captureSnapshot();
        rememberState (checkpoint);
    }
}

bool PresetManager::undo()
{
    // Ctrl+Z pressed while a knob is still held: close that edit first so it is not recorded twice.
    if (gestureDepth > 0)
    {
        gestureDepth = 0;
        commitGestureChanges();
    }

    commitUnrecordedChanges();
    const bool changed = history.undo();
    if (changed)
    {
        checkpoint = captureSnapshot();
        rememberState (checkpoint);
    }
    return changed;
}

bool PresetManager::redo()
{
    if (gestureDepth > 0)
    {
        gestureDepth = 0;
        commitGestureChanges();
    }

    commitUnrecordedChanges();
    const bool changed = history.redo();
    if (changed)
    {
        checkpoint = captureSnapshot();
        rememberState (checkpoint);
    }
    return changed;
}

bool PresetManager::canUndo() const
{
    return history.canUndo();
}

bool PresetManager::canRedo() const
{
    return history.canRedo();
}

void PresetManager::clearHistory()
{
    history.clearUndoHistory();
    checkpoint = captureSnapshot();
    hasCheckpoint = true;
    gestureDepth = 0;
    gestureBefore.clear();
    hostChangeBurst = 0;
    for (int i = 0; i < numTrackedParams; ++i)
        if (hostMoved != nullptr)
            hostMoved[(size_t) i].store (false);
    knownStates.clear();
    rememberState (checkpoint);
}

void PresetManager::load (int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= (int) entries.size()) return;
    commitUnrecordedChanges();
    const auto entry = entries[(size_t) entryIndex];
    history.beginNewTransaction();
    if (history.perform (new HistoryAction (*this, captureSnapshot(), [this, entry] { return applyPreset (entry); })))
    {
        checkpoint = captureSnapshot();
        rememberState (checkpoint);
    }
}

bool PresetManager::save (const juce::String& name, const juce::String& category)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto dir = userPresetDir().getChildFile (juce::File::createLegalFileName (category.isEmpty() ? juce::String ("User") : category));
    dir.createDirectory();
    auto file = dir.getChildFile (safe + ".elukka");

    auto state = proc.saveState();
    state.setProperty ("presetName", name, nullptr);
    state.setProperty ("category", category, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (file)) return false;

    refresh();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].isFactory && entries[(size_t) i].file == file)
            currentIndex = i;
    currentName = name;
    rememberState (captureSnapshot());
    return true;
}

bool PresetManager::deletePreset (int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= (int) entries.size() || entries[(size_t) entryIndex].isFactory) return false;
    if (! entries[(size_t) entryIndex].file.moveToTrash()) return false;

    // the sound stays loaded, but it is no longer a saved preset
    const bool wasCurrent = entryIndex == currentIndex;
    const auto current = currentIndex >= 0 && ! wasCurrent ? entries[(size_t) currentIndex] : Entry {};
    refresh();
    currentIndex = -1;
    if (! wasCurrent && current.name.isNotEmpty())
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].isFactory == current.isFactory && entries[(size_t) i].index == current.index && entries[(size_t) i].file == current.file)
                currentIndex = i;
    return true;
}

void PresetManager::refreshPatterns()
{
    patterns.clear();
    // Init first, then the other factory patterns
    const auto& f = SeqPattern::factory();
    patterns.push_back ({ f[(size_t) SeqPattern::initIndex()].name, true, SeqPattern::initIndex(), {} });
    for (int i = 0; i < (int) f.size(); ++i)
        if (i != SeqPattern::initIndex())
            patterns.push_back ({ f[(size_t) i].name, true, i, {} });

    auto dir = userPatternDir();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.elukkaseq");
        files.sort();
        for (auto& file : files)
            patterns.push_back ({ file.getFileNameWithoutExtension(), false, -1, file });
    }
}

void PresetManager::loadPattern (int index)
{
    if (index < 0 || index >= (int) patterns.size()) return;
    commitUnrecordedChanges();
    const auto entry = patterns[(size_t) index];
    history.beginNewTransaction();
    if (history.perform (new HistoryAction (*this, captureSnapshot(), [this, entry] { return applyPattern (entry); })))
    {
        checkpoint = captureSnapshot();
        rememberState (checkpoint);
    }
}

bool PresetManager::savePattern (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto dir = userPatternDir();
    dir.createDirectory();
    proc.pattern.name = name;
    auto xml = proc.pattern.toTree().createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (safe + ".elukkaseq"))) return false;
    refreshPatterns();
    return true;
}
