#include "Sequencer.h"

juce::ValueTree SeqPattern::toTree() const
{
    juce::ValueTree t ("Pattern");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("length", length, nullptr);
    t.setProperty ("numPitches", numPitches, nullptr);
    t.setProperty ("startStep", startStep, nullptr);
    juce::String s, p;
    for (int i = 0; i < maxSteps; ++i)
    {
        s << (int) steps[(size_t) i];
        auto& e = pitches[(size_t) i];
        p << e.semis << ":" << (int) e.accent << ":" << (int) e.slide << " ";
    }
    t.setProperty ("steps", s, nullptr);
    t.setProperty ("pitches", p.trim(), nullptr);
    return t;
}

void SeqPattern::fromTree (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    name = t["name"].toString();
    length = juce::jlimit (1, maxSteps, (int) t["length"]);
    numPitches = juce::jlimit (1, maxSteps, (int) t["numPitches"]);
    startStep = juce::jlimit (0, maxSteps - 1, (int) t.getProperty ("startStep", 0));
    auto s = t["steps"].toString();
    for (int i = 0; i < maxSteps; ++i)
        steps[(size_t) i] = i < s.length() ? (StepType) juce::jlimit (0, 2, (int) (s[i] - '0')) : StepType::Note;

    auto tokens = juce::StringArray::fromTokens (t["pitches"].toString(), " ", "");
    for (int i = 0; i < maxSteps; ++i)
    {
        PitchEntry e;
        if (i < tokens.size())
        {
            auto f = juce::StringArray::fromTokens (tokens[i], ":", "");
            if (f.size() == 4)   // older saves: note (0..12) : octave (-1..1) : accent : slide
            {
                e.semis = juce::jlimit (0, 12, f[0].getIntValue()) + 12 * juce::jlimit (-1, 1, f[1].getIntValue());
                e.accent = f[2].getIntValue() != 0;
                e.slide = f[3].getIntValue() != 0;
            }
            else if (f.size() == 3)   // semitones : accent : slide
            {
                e.semis = juce::jlimit (-PitchEntry::range, PitchEntry::range, f[0].getIntValue());
                e.accent = f[1].getIntValue() != 0;
                e.slide = f[2].getIntValue() != 0;
            }
        }
        pitches[(size_t) i] = e;
    }
}

SeqPattern SeqPattern::fromText (const juce::String& n, const juce::String& time, const juce::String& pitch)
{
    SeqPattern p;
    p.name = n;
    auto t = time.removeCharacters (" |");
    p.length = juce::jlimit (1, maxSteps, t.length());
    for (int i = 0; i < p.length; ++i)
        p.steps[(size_t) i] = t[i] == '-' ? StepType::Tie : t[i] == '.' ? StepType::Rest : StepType::Note;

    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto tokens = juce::StringArray::fromTokens (pitch, " ", "");
    p.numPitches = juce::jlimit (1, maxSteps, tokens.size());
    for (int i = 0; i < p.numPitches; ++i)
    {
        auto tok = tokens[i];
        PitchEntry e;
        int octave = 0;
        while (tok.startsWithChar ('^')) { octave = 1; tok = tok.substring (1); }
        while (tok.startsWithChar ('v')) { octave = -1; tok = tok.substring (1); }
        if (tok.containsChar ('!')) e.accent = true;
        if (tok.containsChar ('~')) e.slide = true;
        tok = tok.removeCharacters ("!~");
        e.semis = 12 * octave + (tok == "C2" ? 12 : juce::jmax (0, names.indexOf (tok)));
        p.pitches[(size_t) i] = e;
    }
    return p;
}

SeqPattern SeqPattern::generate (const juce::String& n, int seed, int len, const std::vector<int>& scale,
                                 float density, float tieProb, float accentProb, float slideProb, float octaveProb)
{
    juce::Random r (seed);
    SeqPattern p;
    p.name = n;
    p.length = len;
    int notes = 0;
    for (int i = 0; i < len; ++i)
    {
        float x = r.nextFloat();
        StepType s = x < density ? StepType::Note : (x < density + tieProb && i > 0 ? StepType::Tie : StepType::Rest);
        if (i == 0) s = StepType::Note;
        p.steps[(size_t) i] = s;
        if (s == StepType::Note) ++notes;
    }
    p.numPitches = juce::jlimit (1, maxSteps, notes);
    for (int i = 0; i < p.numPitches; ++i)
    {
        PitchEntry e;
        // weight towards the root
        const int note = r.nextFloat() < 0.35f ? 0 : scale[(size_t) r.nextInt ((int) scale.size())];
        float o = r.nextFloat();
        e.semis = note + 12 * (o < octaveProb * 0.6f ? 1 : (o < octaveProb ? -1 : 0));
        e.accent = r.nextFloat() < accentProb;
        e.slide = r.nextFloat() < slideProb;
        p.pitches[(size_t) i] = e;
    }
    return p;
}

const std::vector<SeqPattern>& SeqPattern::factory()
{
    static const std::vector<SeqPattern> list = []
    {
        std::vector<SeqPattern> v;
        // Dreamy, melancholic lines in the natural minor (C D D# F G G# A#) and dorian (A instead of G#) of the held key:
        // dub chord offbeats, rolling octave basses, trance arps and slow tied bass lines. The chord presets stack their
        // oscillators into the chord, so the single-note dub patterns play full chords with them.
        v.push_back (fromText ("Init", "xxxx xxxx xxxx xxxx", "C C C C C C C C C C C C C C C C"));

        // Dub chords
        v.push_back (fromText ("Dub Skank",       "..x. ..x. ..x. ..x.", "C C C C"));
        v.push_back (fromText ("Dub Chord Echo",  "x--. ..x. .... x-..", "C C vA#"));
        v.push_back (fromText ("Dub Chord Drift", "x--- ..x- .x.. ..x-", "C ^C G vA#"));
        v.push_back (fromText ("Offbeat Minor",   "..x. ..x. ..x. ..x.", "C D# C G"));
        v.push_back (fromText ("Echo Stabs",      "x... ..x. .x.. ...x", "C G D# ^C"));
        v.push_back (fromText ("Chord Swell 32",  "x--- ---- ..x- --.. x--- --.. .x-- ..x.", "C C vG# C vA# vA# ^C"));

        // Basses
        v.push_back (fromText ("Deep Roller",     "xxxx xxxx xxxx xxxx", "C C C ^C! C C C G C C C ^C! C D# C G"));
        v.push_back (fromText ("Octave Sway",     "x.x. x.x. x.x. x.x.", "C ^C! C ^C D# ^D#! G ^G"));
        v.push_back (fromText ("Dub Bass Sway",   "x--- ..x- --.. x.x.", "C C~ vA# G~"));
        v.push_back (fromText ("Sub Pulse",       "x... x... x..x ....", "C C C ^C"));
        v.push_back (fromText ("Gallop Minor",    "xx.x xx.x xx.x xx.x", "C C ^C C C D# G G! C C ^C C"));
        v.push_back (fromText ("Slow Tide 32",    "x--- ---- x--- --x- x--- ---- x--- -x--", "C~ D# vA#~ G# F~ G"));
        v.push_back (fromText ("Minor Walk 64",   // i - VI - III - VII, one bar each
                               "x.x. x.x. x.x. x.xx x.x. x.x. x.x. x.xx x.x. x.x. x.x. x.xx x.x. x.x. x.x. x.xx",
                               "C ^C C G C ^C C G C! vG# G# vG# vD# vG# G# vG# vD# vG#! D# ^D# D# A# D# ^D# D# A# D#! "
                               "vA# A# vA# F vA# A# vA# F vA#!"));

        // Arps and melodies
        v.push_back (fromText ("Minor Arp Rise",     "xxxx xxxx xxxx xxxx", "C D# G ^C C D# G ^C C F G# ^C C F G# ^C"));
        v.push_back (fromText ("Minor Arp Fall",     "xxxx xxxx xxxx xxxx", "^C G D# C ^C G D# C ^C G# F C ^C G# F C"));
        v.push_back (fromText ("Trance Lift",        "x.xx x.xx x.xx x.xx", "C G ^C! C G ^C D# G ^D#! D# A# ^D#"));
        v.push_back (fromText ("Sixteenth Dream",    "xxxx xxxx xxxx xxxx", "C C ^C C D# D# ^D# D# G G ^G G A# A# ^A# A#"));
        v.push_back (fromText ("Melancholy Fall",    "xxxx xxxx xxxx xxxx", "^C A# G# G F D# D C ^D ^C A# G# G F D# D"));
        v.push_back (fromText ("Dorian Drift",       "xxxx xxxx xxxx xxxx", "C D# G A ^C A G D# C D A# A G F D C"));
        v.push_back (fromText ("Ninth Shimmer",      "x.xx x.xx x.xx x.xx", "C D# A# ^D ^C A# D# C G A# ^D ^F"));
        v.push_back (fromText ("Broken Minor 3-3-2", "x.xx .xx. x.xx .xx.", "C D# G ^C G D# ^C! G D# C G ^C"));
        v.push_back (fromText ("Suspended Hope",     "x.xx .x.x x.xx .x.x", "C F G ^C G F ^D G F ^C G F"));
        v.push_back (fromText ("Rising Minor",       "xxxx xxxx xxxx xxxx", "C D D# F G G# A# ^C ^D ^D# ^F ^G ^G# ^A# ^G ^D#"));
        v.push_back (fromText ("Pentatonic Rain",    "xxxx xxxx xxxx xxxx", "C D# F G A# ^C A# G F D# C vA# C D# F G"));
        v.push_back (fromText ("Rain Drops",         "x..x .x.. x.x. ..x.", "^C ^G ^D# ^A# ^F ^G"));
        v.push_back (fromText ("Triplet Drift",      "xxxxxx xxxxxx", "C D# G ^C G D# C F G# ^C G# F"));

        // Polymeters (3, 5, 7 and 12 steps drift against the bar)
        v.push_back (fromText ("Poly 3 Drops",   "xx.", "C ^G!"));
        v.push_back (fromText ("Poly 5 Bells",   "x.xx.", "C G ^D#"));
        v.push_back (fromText ("Poly 7 Glass",   "x.x.xx.", "C D# G ^C A#"));
        v.push_back (fromText ("Hypno Minor 12", "x.xx .xx. xx.x", "C ^C G D# ^D# C G A# ^C"));

        // Generated lines, weighted towards the root
        const std::vector<int> minor { 2, 3, 5, 7, 8, 10 }, dorian { 2, 3, 5, 7, 9, 10 }, pentatonic { 3, 5, 7, 10 }, minor7 { 3, 7, 10 };
        v.push_back (generate ("Gen Minor Drift",      303,  16, minor,      0.65f, 0.15f, 0.2f,  0.25f, 0.3f));
        v.push_back (generate ("Gen Dorian Flow",      909,  16, dorian,     0.7f,  0.12f, 0.2f,  0.2f,  0.3f));
        v.push_back (generate ("Gen Pentatonic Rain",  1987, 16, pentatonic, 0.55f, 0.1f,  0.15f, 0.1f,  0.4f));
        v.push_back (generate ("Gen Minor 7 Chimes",   4242, 16, minor7,     0.6f,  0.1f,  0.2f,  0.1f,  0.4f));
        v.push_back (generate ("Gen Sparse Dub",       1234, 16, minor7,     0.35f, 0.3f,  0.3f,  0.2f,  0.2f));
        v.push_back (generate ("Gen Soft Pulse",       777,  16, pentatonic, 0.75f, 0.1f,  0.15f, 0.15f, 0.25f));
        v.push_back (generate ("Gen Rolling Minor 32", 8080, 32, minor,      0.8f,  0.08f, 0.25f, 0.2f,  0.3f));
        v.push_back (generate ("Gen Hypnotic 32",      2600, 32, minor7,     0.7f,  0.1f,  0.2f,  0.25f, 0.35f));
        v.push_back (generate ("Gen Slow Tide 64",     6502, 64, minor,      0.4f,  0.35f, 0.2f,  0.3f,  0.2f));
        v.push_back (generate ("Gen Long Dorian 64",   1408, 64, dorian,     0.7f,  0.12f, 0.2f,  0.2f,  0.3f));
        return v;
    }();
    return list;
}

const std::vector<SeqPattern::Scale>& SeqPattern::scales()
{
    static const std::vector<Scale> list
    {
        { "Western",            "Major",                       { 0, 2, 4, 5, 7, 9, 11 } },
        { "Western",            "Minor",                       { 0, 2, 3, 5, 7, 8, 10 } },
        { "Western",            "Harmonic Minor",              { 0, 2, 3, 5, 7, 8, 11 } },
        { "Western",            "Melodic Minor",               { 0, 2, 3, 5, 7, 9, 11 } },
        { "Modes",              "Dorian",                      { 0, 2, 3, 5, 7, 9, 10 } },
        { "Modes",              "Phrygian",                    { 0, 1, 3, 5, 7, 8, 10 } },
        { "Modes",              "Lydian",                      { 0, 2, 4, 6, 7, 9, 11 } },
        { "Modes",              "Mixolydian",                  { 0, 2, 4, 5, 7, 9, 10 } },
        { "Modes",              "Locrian",                     { 0, 1, 3, 5, 6, 8, 10 } },
        { "Pentatonic & Blues", "Minor Pentatonic",            { 0, 3, 5, 7, 10 } },
        { "Pentatonic & Blues", "Major Pentatonic",            { 0, 2, 4, 7, 9 } },
        { "Pentatonic & Blues", "Blues",                       { 0, 3, 5, 6, 7, 10 } },
        { "World",              "Arabic (Double Harmonic)",    { 0, 1, 4, 5, 7, 8, 11 } },
        { "World",              "Hijaz (Phrygian Dominant)",   { 0, 1, 4, 5, 7, 8, 10 } },
        { "World",              "Hungarian Minor",             { 0, 2, 3, 6, 7, 8, 11 } },
        { "World",              "Persian",                     { 0, 1, 4, 5, 6, 8, 11 } },
        { "World",              "Japanese (In)",               { 0, 1, 5, 7, 8 } },
        { "World",              "Hirajoshi",                   { 0, 2, 3, 7, 8 } },
        { "Symmetric",          "Whole Tone",                  { 0, 2, 4, 6, 8, 10 } },
        { "Symmetric",          "Diminished (Half-Whole)",     { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "Symmetric",          "Chromatic",                   { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return list;
}

int SeqPattern::initIndex()
{
    static const int index = []
    {
        const auto& f = factory();
        for (int i = 0; i < (int) f.size(); ++i)
            if (f[(size_t) i].name == "Init") return i;
        return 0;
    }();
    return index;
}

void SeqEngine::process (const SeqPattern& pat, int numSamples, double sampleRate, double bpm, bool hostPlaying, double ppqStart,
                         bool keyHeld, int rootNote, double stepBeats, float gate, float accentAmount, std::vector<SeqEvent>& out)
{
    if (! keyHeld)
    {
        if (sounding) out.push_back ({ 0, false, currentNote, 0.0f, false, false });
        sounding = false;
        running = false;
        pendingOff = -1;
        lastStep = std::numeric_limits<int64_t>::min();
        displayStep = -1;
        accentValue = 0.0f;
        return;
    }

    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const double stepSamples = stepBeats / beatsPerSample;

    if (! running)
    {
        running = true;
        internalBeat = 0.0;
        originBeat = 0.0;
        lastStep = std::numeric_limits<int64_t>::min();
    }
    if (restartPending)
    {
        // restart from the start step now: the clock counts from here; no slide into it from the previous note
        restartPending = false;
        internalBeat = 0.0;
        originHost = hostPlaying;
        originBeat = hostPlaying ? ppqStart : 0.0;
        lastStep = std::numeric_limits<int64_t>::min();
        currentSlide = false;
    }
    if (originBeat != 0.0 && originHost != hostPlaying) originBeat = 0.0;   // the transport started or stopped: back to its clock

    const double pos0 = (hostPlaying ? ppqStart : internalBeat) - originBeat;

    auto stepAt = (int64_t) std::floor (pos0 / stepBeats + 1.0e-9);
    if (stepAt != lastStep)
        triggerStep (pat, stepAt, 0, rootNote, stepSamples, gate, accentAmount, out);

    for (int64_t k = stepAt + 1;; ++k)
    {
        int offset = (int) std::ceil (((double) k * stepBeats - pos0) / beatsPerSample);
        if (offset >= numSamples) break;
        triggerStep (pat, k, juce::jmax (0, offset), rootNote, stepSamples, gate, accentAmount, out);
    }

    // Gate-off after the last step trigger: fire it here or carry it into the next block.
    if (pendingOff >= 0 && pendingOff < numSamples)
    {
        if (sounding) out.push_back ({ (int) pendingOff, false, currentNote, 0.0f, false, false });
        sounding = false;
        pendingOff = -1;
    }
    else if (pendingOff >= 0)
    {
        pendingOff -= numSamples;
    }

    if (! hostPlaying) internalBeat += numSamples * beatsPerSample;

    std::stable_sort (out.begin(), out.end(), [] (const SeqEvent& a, const SeqEvent& b)
    {
        if (a.offset != b.offset) return a.offset < b.offset;
        return ! a.on && b.on;
    });
}

void SeqEngine::triggerStep (const SeqPattern& pat, int64_t absStep, int offset, int rootNote, double stepSamples,
                             float gate, float accentAmount, std::vector<SeqEvent>& out)
{
    lastStep = absStep;
    const int len = juce::jlimit (1, SeqPattern::maxSteps, pat.length);
    // the pattern is rotated so that playback begins at the start step (pitches move with their steps)
    const int i = (int) ((((absStep + pat.startStep) % len) + len) % len);
    displayStep = i;

    // A pending gate-off scheduled before this step boundary has already fired; drop leftovers.
    if (pendingOff >= 0 && pendingOff <= offset)
    {
        if (sounding) out.push_back ({ (int) pendingOff, false, currentNote, 0.0f, false, false });
        sounding = false;
    }
    pendingOff = -1;

    const auto type = pat.steps[(size_t) i];
    if (type == StepType::Note)
    {
        int noteIndex = 0;
        for (int s = 0; s < i; ++s)
            if (pat.steps[(size_t) s] == StepType::Note) ++noteIndex;
        const auto& e = pat.pitches[(size_t) (noteIndex % juce::jmax (1, pat.numPitches))];
        const int note = juce::jlimit (0, 127, rootNote + e.semis);
        const bool legato = sounding && currentSlide;

        if (sounding && ! legato)
            out.push_back ({ offset, false, currentNote, 0.0f, false, false });

        const float vel = e.accent ? 1.0f : 1.0f - 0.5f * accentAmount;
        out.push_back ({ offset, true, note, vel, e.accent, legato });
        currentNote = note;
        currentSlide = e.slide;
        sounding = true;
        accentValue = e.accent ? accentAmount : 0.0f;
    }
    else if (type == StepType::Rest)
    {
        if (sounding) out.push_back ({ offset, false, currentNote, 0.0f, false, false });
        sounding = false;
        currentSlide = false;
        accentValue = 0.0f;
    }

    if (sounding)
    {
        const auto next = pat.steps[(size_t) ((i + 1) % len)];
        const bool hold = next == StepType::Tie || (currentSlide && next == StepType::Note);
        if (! hold)
            pendingOff = offset + (int64_t) (gate * stepSamples);
    }
}
