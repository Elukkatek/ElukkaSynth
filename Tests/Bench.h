#pragma once
// CPU benchmark (developer tool):
//   --bench [outDir] [filter]   renders a fixed set of scenarios (48 kHz, 512-sample blocks, 6 s unless named otherwise) and
//                               reports CPU as % of real time on one core (best of 3 runs), the slowest block
//                               (% of its own duration), heap allocations in the audio callback and the CPU of the first
//                               and last quarter of the render. With outDir, writes each scenario's audio there.
//   --bench-compare <dirA> <dirB>   difference between two sets of benchmark renders, per scenario
// The processor's random generator is seeded the same way every run, so renders of unchanged code are identical.
// Heap allocations made while Bench::countAllocations is set (the audio callback must not allocate).
namespace Bench { inline std::atomic<bool> countAllocations { false }; inline std::atomic<int> allocations { 0 }; }
void* operator new (std::size_t size)
{
    if (Bench::countAllocations.load (std::memory_order_relaxed)) ++Bench::allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace Bench
{
constexpr double sr = 48000.0;
constexpr int block = 512;
constexpr double seconds = 6.0;

inline void setP (ElukkaAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

inline std::shared_ptr<ResynthData> loadSample (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return {};
    juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples);
    rd->read (&b, 0, b.getNumSamples(), 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    return ResynthData::fromAudio (b, rd->sampleRate, f.getFileName());
}

struct Scenario
{
    juce::String name;
    std::vector<int> notes;
    std::function<void (ElukkaAudioProcessor&)> setup;
    int sample = -1;   // index into the loaded samples (-1 = none)
    double secs = seconds;
    int retriggerBlocks = 0;   // > 0: the notes are released and played again every this many blocks
};

inline void saw (ElukkaAudioProcessor& p, int osc = 0, int voices = 1, float level = 0.8f)
{
    setP (p, P::osc (osc, "wave"), (float) P::WSaw);
    setP (p, P::osc (osc, "level"), level);
    setP (p, P::osc (osc, "uniVoices"), (float) voices);
}
inline void resynth (ElukkaAudioProcessor& p, int voices = 1)
{
    setP (p, P::osc (0, "wave"), (float) P::WResynth);
    setP (p, P::osc (0, "uniVoices"), (float) voices);
}
inline void allFx (ElukkaAudioProcessor& p)
{
    for (auto id : { "dist_on", "comb_on", "eq_on", "dly_on", "rev_on", "ott_on" }) setP (p, id, 1.0f);
    setP (p, "eq_mid", 4.0f);
}
inline void filterLfo (ElukkaAudioProcessor& p)
{
    setP (p, "flt_cutoff", 1500.0f);
    setP (p, "flt_res", 0.4f);
    setP (p, P::mx (0, "src"), (float) P::SrcMod2);
    setP (p, P::mx (0, "dst"), (float) P::DstFltCutoff);
    setP (p, P::mx (0, "amt"), 0.3f);
    setP (p, P::mod (1, "rate"), 3.0f);
}

inline std::vector<Scenario> scenarios()
{
    const std::vector<int> one { 60 }, four { 48, 55, 60, 64 }, eight { 36, 43, 48, 52, 55, 60, 64, 67 };
    std::vector<Scenario> v;
    v.push_back ({ "idle", {}, [] (auto&) {} });
    v.push_back ({ "sine-1", one, [] (auto&) {} });
    v.push_back ({ "saw-1", one, [] (auto& p) { saw (p); } });
    v.push_back ({ "saw-8", eight, [] (auto& p) { saw (p); } });
    v.push_back ({ "saw-8-uni16", eight, [] (auto& p) { saw (p, 0, 16); } });
    v.push_back ({ "3osc-8-uni7", eight, [] (auto& p) { saw (p, 0, 7); saw (p, 1, 7, 0.6f); saw (p, 2, 7, 0.5f);
                                                       setP (p, P::osc (1, "wave"), (float) P::WSquare); setP (p, P::osc (2, "coarse"), 12.0f); } });
    v.push_back ({ "saw-8-uni7-prism-shimmer", eight, [] (auto& p) { saw (p, 0, 7); setP (p, "hm_prism", 50.0f); setP (p, "hm_shimmer", 30.0f); } });
    v.push_back ({ "saw-8-uni7-filter-lfo", eight, [] (auto& p) { saw (p, 0, 7); filterLfo (p); } });
    v.push_back ({ "fx-reverb", one, [] (auto& p) { saw (p); setP (p, "rev_on", 1.0f); } });
    v.push_back ({ "fx-delay", one, [] (auto& p) { saw (p); setP (p, "dly_on", 1.0f); } });
    v.push_back ({ "fx-ott", one, [] (auto& p) { saw (p); setP (p, "ott_on", 1.0f); } });
    v.push_back ({ "fx-comb", one, [] (auto& p) { saw (p); setP (p, "comb_on", 1.0f); } });
    v.push_back ({ "fx-dist", one, [] (auto& p) { saw (p); setP (p, "dist_on", 1.0f); } });
    v.push_back ({ "fx-eq", one, [] (auto& p) { saw (p); setP (p, "eq_on", 1.0f); setP (p, "eq_mid", 4.0f); } });
    v.push_back ({ "fx-all", one, [] (auto& p) { saw (p); allFx (p); } });
    v.push_back ({ "rs-1", one, [] (auto& p) { resynth (p); }, 0 });
    v.push_back ({ "rs-4", four, [] (auto& p) { resynth (p); }, 0 });
    v.push_back ({ "rs-4-tune100", four, [] (auto& p) { resynth (p); setP (p, "rs_tune", 100.0f); }, 0 });
    v.push_back ({ "rs-1-uni8", one, [] (auto& p) { resynth (p, 8); }, 0 });
    v.push_back ({ "rs-8-uni8", eight, [] (auto& p) { resynth (p, 8); }, 0 });
    v.push_back ({ "rs-4-pos-sh", four, [] (auto& p) { resynth (p); setP (p, P::mx (0, "src"), (float) P::SrcSH);
                                                       setP (p, P::mx (0, "dst"), (float) P::DstResPos); setP (p, P::mx (0, "amt"), 0.8f); }, 0 });
    v.push_back ({ "rs-4-vocal-tune100", four, [] (auto& p) { resynth (p); setP (p, "rs_tune", 100.0f); }, 1 });
    v.push_back ({ "rs-4-uni4-30s", four, [] (auto& p) { resynth (p, 4); }, 0, 30.0 });
    v.push_back ({ "rs-8-retrigger-125ms", eight, [] (auto& p) { resynth (p, 2); setP (p, "ampR", 0.3f); }, 0, 12.0, 12 });
    v.push_back ({ "saw-8-uni7-retrigger-125ms", eight, [] (auto& p) { saw (p, 0, 7); setP (p, "ampR", 0.3f); }, -1, 12.0, 12 });
    // FM (the default chain Osc 3 -> Osc 2 -> Osc 1), modulation that changes the spectrum every block, filter envelope
    v.push_back ({ "fm-3osc-8", eight, [] (auto& p) { saw (p); setP (p, P::osc (1, "level"), 0.3f); setP (p, P::osc (2, "level"), 0.2f);
                                                     setP (p, P::osc (0, "fmAmt"), 0.3f); setP (p, P::osc (1, "fmAmt"), 0.2f); } });
    v.push_back ({ "rs-4-fm", four, [] (auto& p) { resynth (p); setP (p, P::osc (0, "fmAmt"), 0.3f); }, 0 });
    v.push_back ({ "3osc-8-uni7-vibrato", eight, [] (auto& p) { saw (p, 0, 7); saw (p, 1, 7, 0.6f); saw (p, 2, 7, 0.5f);
                                                               setP (p, P::mx (0, "src"), (float) P::SrcMod1); setP (p, P::mx (0, "dst"), (float) P::DstO1Pitch);
                                                               setP (p, P::mx (0, "amt"), 0.01f); setP (p, P::mod (0, "rate"), 5.0f); } });
    v.push_back ({ "saw-8-uni7-harm-cutoff-lfo", eight, [] (auto& p) { saw (p, 0, 7); setP (p, "hm_cutoff", 3000.0f);
                                                                      setP (p, P::mx (0, "src"), (float) P::SrcMod2); setP (p, P::mx (0, "dst"), (float) P::DstCutoff);
                                                                      setP (p, P::mx (0, "amt"), 0.3f); setP (p, P::mod (1, "rate"), 3.0f); } });
    v.push_back ({ "saw-8-uni7-filter-env", eight, [] (auto& p) { saw (p, 0, 7); setP (p, "flt_cutoff", 800.0f); setP (p, "flt_env", 0.6f); setP (p, "flt_D", 1.0f); } });
    v.push_back ({ "worst-rs8u8+saw7+filter+fx", eight, [] (auto& p) { resynth (p, 8); saw (p, 1, 7, 0.5f); filterLfo (p); allFx (p);
                                                                         setP (p, "rs_tune", 100.0f); }, 0 });
    return v;
}

struct Result { double cpu = 1.0e9, peakBlock = 1.0e9, firstQuarter = 1.0e9, lastQuarter = 1.0e9; int allocations = 0; };

inline Result runOnce (const Scenario& s, const std::vector<std::shared_ptr<ResynthData>>& samples, juce::AudioBuffer<float>* capture)
{
    auto proc = std::make_unique<ElukkaAudioProcessor>();   // heap: the test's main() already uses most of the stack
    auto& p = *proc;
    FakePlayHead ph;
    p.setPlayHead (&ph);
    p.setPlayConfigDetails (0, 2, sr, block);
    p.presets.load (0);
    if (s.sample >= 0) p.setResynth (samples[(size_t) s.sample]);
    s.setup (p);
    p.seedRandom (1);
    p.prepareToPlay (sr, block);

    // ELUKKA_BENCH_SCALE=n renders every scenario n times longer (for profiling with `sample`)
    const double scale = std::getenv ("ELUKKA_BENCH_SCALE") != nullptr ? juce::jmax (1.0, juce::String (std::getenv ("ELUKKA_BENCH_SCALE")).getDoubleValue()) : 1.0;
    const int numBlocks = (int) (s.secs * scale * sr) / block;
    if (capture != nullptr) capture->setSize (2, numBlocks * block);
    juce::AudioBuffer<float> buf (2, block);
    Result r { 0.0, 0.0 };
    double total = 0.0, quarter[2] {};
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0 || (s.retriggerBlocks > 0 && b % s.retriggerBlocks == 0))
            for (int n : s.notes)
            {
                if (b > 0) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 1);
            }
        allocations = 0;
        countAllocations = true;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        p.processBlock (buf, midi);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        countAllocations = false;
        r.allocations += allocations;
        total += ms;
        if (b < numBlocks / 4) quarter[0] += ms;
        if (b >= numBlocks - numBlocks / 4) quarter[1] += ms;
        r.peakBlock = std::max (r.peakBlock, ms / (block / sr * 1000.0));
        ph.ppq += block / sr * ph.bpm / 60.0;
        if (capture != nullptr) for (int ch = 0; ch < 2; ++ch) capture->copyFrom (ch, b * block, buf, ch, 0, block);
    }
    r.cpu = total / (numBlocks * block / sr * 1000.0);
    r.firstQuarter = quarter[0] / ((numBlocks / 4) * block / sr * 1000.0);
    r.lastQuarter = quarter[1] / ((numBlocks / 4) * block / sr * 1000.0);
    return r;
}

inline int run (const juce::File& outDir, const juce::String& filter)
{
    const juce::File folder ("/Users/elkku/Desktop/Vibecode/resynth samples");
    std::vector<std::shared_ptr<ResynthData>> samples;
    for (auto name : { "Cymatics - Aqua - 74 BPM F Min.wav", "Cymatics - Peace Of Mind Vocal Loop - 175 BPM D Maj.wav" })
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        samples.push_back (loadSample (folder.getChildFile (name)));
        if (samples.back() == nullptr) { std::cout << "missing sample " << name << "\n"; return 1; }
        {
            auto& d = *samples.back();
            double sum = 0.0, below60 = 0.0, below70 = 0.0;
            int peak = 0;
            for (int fr = 0; fr < d.numFrames; ++fr)
            {
                int c = 0;
                float loudest = 0.0f;
                for (int sl = 0; sl < d.numSlots; ++sl) loudest = std::max (loudest, d.amp[(size_t) fr * (size_t) d.numSlots + (size_t) sl]);
                for (int sl = 0; sl < d.numSlots; ++sl)
                {
                    const float a = d.amp[(size_t) fr * (size_t) d.numSlots + (size_t) sl];
                    if (a <= 0.0f) continue;
                    ++c;
                    below60 += a < loudest * 1.0e-3f ? 1 : 0;
                    below70 += a < loudest * 3.162e-4f ? 1 : 0;
                }
                sum += c;
                peak = std::max (peak, c);
            }
            std::cout << "partials per frame: mean " << juce::String (sum / std::max (1, d.numFrames), 1) << ", max " << peak
                      << " (" << juce::String (100.0 * below60 / std::max (1.0, sum), 1) << "% more than 60 dB and "
                      << juce::String (100.0 * below70 / std::max (1.0, sum), 1) << "% more than 70 dB below the frame's loudest); ";
        }
        std::cout << "analysed " << name << " in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) << " s\n";
    }
    if (outDir != juce::File()) outDir.createDirectory();

    std::cout << juce::String ("scenario").paddedRight (' ', 30) << "  cpu %   peak block %   allocations   first/last quarter cpu %\n";
    for (auto& s : scenarios())
    {
        if (filter.isNotEmpty() && ! s.name.contains (filter)) continue;
        Result best;
        juce::AudioBuffer<float> audio;
        for (int k = 0; k < 3; ++k)
        {
            auto r = runOnce (s, samples, k == 0 ? &audio : nullptr);
            best.cpu = std::min (best.cpu, r.cpu);
            best.peakBlock = std::min (best.peakBlock, r.peakBlock);
            best.allocations = std::max (best.allocations, r.allocations);
            best.firstQuarter = std::min (best.firstQuarter, r.firstQuarter);
            best.lastQuarter = std::min (best.lastQuarter, r.lastQuarter);
        }
        std::cout << s.name.paddedRight (' ', 30) << juce::String (best.cpu * 100.0, 2).paddedLeft (' ', 7)
                  << juce::String (best.peakBlock * 100.0, 1).paddedLeft (' ', 14) << juce::String (best.allocations).paddedLeft (' ', 14)
                  << "   " << juce::String (best.firstQuarter * 100.0, 2) << " / " << juce::String (best.lastQuarter * 100.0, 2) << "\n";
        if (outDir != juce::File())
        {
            auto f = outDir.getChildFile (s.name + ".wav");
            f.deleteFile();
            juce::WavAudioFormat wav;
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 32, {}, 0)))
                {
                    os.release();
                    w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
                }
        }
    }
    return 0;
}

inline int compare (const juce::File& a, const juce::File& b)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (auto& fa : a.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        auto fb = b.getChildFile (fa.getFileName());
        std::unique_ptr<juce::AudioFormatReader> ra (fm.createReaderFor (fa)), rb (fm.createReaderFor (fb));
        if (ra == nullptr || rb == nullptr) { std::cout << fa.getFileName() << ": missing\n"; continue; }
        const int n = (int) std::min (ra->lengthInSamples, rb->lengthInSamples);
        juce::AudioBuffer<float> x (2, n), y (2, n);
        ra->read (&x, 0, n, 0, true, true);
        rb->read (&y, 0, n, 0, true, true);
        double sig = 0.0, diff = 0.0;
        float peakDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float d = x.getSample (ch, i) - y.getSample (ch, i);
                sig += (double) x.getSample (ch, i) * x.getSample (ch, i);
                diff += (double) d * d;
                peakDiff = std::max (peakDiff, std::abs (d));
            }
        const double rel = sig > 0.0 ? 10.0 * std::log10 (std::max (1.0e-30, diff) / sig) : (diff > 0.0 ? 0.0 : -999.0);
        // the same over consecutive segments (0-0.1 s, then each second): rounding drift grows with time, a bug would not
        juce::String segs;
        for (double t0 = 0.0; t0 < n / sr; t0 = t0 < 0.1 ? 0.1 : t0 + 1.0)
        {
            const int a0 = (int) (t0 * sr), a1 = std::min (n, (int) ((t0 < 0.1 ? 0.1 : t0 + 1.0) * sr));
            double s2 = 0.0, d2 = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = a0; i < a1; ++i)
                {
                    const double d = x.getSample (ch, i) - y.getSample (ch, i);
                    s2 += (double) x.getSample (ch, i) * x.getSample (ch, i);
                    d2 += d * d;
                }
            segs << " " << (s2 > 0.0 ? juce::String (10.0 * std::log10 (std::max (1.0e-30, d2) / s2), 0) : juce::String ("-"));
        }
        std::cout << fa.getFileNameWithoutExtension().paddedRight (' ', 30) << " difference " << juce::String (rel, 1).paddedLeft (' ', 7)
                  << " dB re signal, peak " << juce::String (juce::Decibels::gainToDecibels (peakDiff, -200.0f), 1) << " dBFS  (by segment:" << segs << ")"
                  << "  spectral distance " << juce::String (ResynthEval::spectralDistance (x, y), 2) << " dB\n";
    }
    return 0;
}
} // namespace Bench
