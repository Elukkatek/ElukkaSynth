#pragma once
#include <JuceHeader.h>

// Resynthesis source analysed into a hybrid model: sinusoidal partials plus a noise residual.
//
// Partials: each frame holds up to `numSlots` partials. A slot keeps the same partial track from frame to frame,
// so an oscillator assigned to a slot plays a continuous sine. Frequencies are stored at the source's original
// pitch; middle C (MIDI 60) plays them unchanged.
//   freq[f]  = the partial's frequency over the interval from frame f to frame f+1 (Hz). For audio it is derived
//              from the measured phase at both frames (phase-vocoder style), so integrating it reproduces the
//              source's phase exactly; the last frame of a track repeats the previous interval.
//   amp[f]   = linear amplitude at frame f (0 = inactive); played with linear interpolation between frames.
//   pan[f]   = (right - left) / (right + left); a partial plays amp (1 - pan) on the left and amp (1 + pan) on the right.
//   phase[f] = phase at the frame centre (cycles, for a sine); audio only. Used when a partial starts.
//   stereoPhase[f] = right-minus-left phase difference (cycles, -0.5..0.5), constant along a track; audio only.
// Images: rows are harmonics of middle C (bottom row = fundamental), columns are time, brightness is level.
//
// Residual (audio only): what the partials cannot represent (breath, attacks, noise, diffuse reverb), as a noise
// spectrogram in sixth-octave bands: per frame and band the left and right amplitude spectral density
// (sqrt of power per Hz) and the left/right coherence (1 = the same noise on both sides, 0 = independent).
struct ResynthData
{
    static constexpr int maxSlots = 256;
    static constexpr double rootHz = 261.6255653;   // MIDI 60

    static constexpr int numNoiseBands = 54;          // 40 Hz .. 20.5 kHz
    static constexpr float noiseBandLoHz = 40.0f;
    static constexpr float noiseBandsPerOctave = 6.0f;
    static float noiseBandCentre (int b) { return noiseBandLoHz * std::exp2 (((float) b + 0.5f) / noiseBandsPerOctave); }
    // continuous band position of a frequency (band b covers [b, b + 1))
    static float noiseBandPos (float hz) { return std::log2 (juce::jmax (1.0f, hz) / noiseBandLoHz) * noiseBandsPerOctave; }

    bool isImage = false;
    int numFrames = 0;
    int numSlots = 0;
    double frameRate = 100.0;    // frames per second of source time
    double durationSec = 0.0;
    double detectedBpm = 120.0;
    float maxAmp = 1.0f;         // loudest partial, used by the Lift control
    // Level of every frame in dB relative to the loudest frame (0 dB), for the Resynth Level modulation source.
    // Audio: RMS of the source around the frame; images: the summed level of the column's partials.
    std::vector<float> frameDb;
    void computeLevels();
    float levelDbAt (double timeSec) const;   // interpolated between frames; -200 dB when empty
    // Notes (audio only; images are one note at middle C). Every partial is tagged with the centre MIDI pitch of
    // the note it belongs to, per frame, so Tune can move each note of the source to the target note (see
    // tuneTarget). Unclaimed partials carry the nearest note in frequency; frames without notes carry the nearest
    // frame in time with notes; 0 = no note anywhere near.
    static constexpr int maxNotesPerFrame = 6;
    // Centre pitch (MIDI) of the source's first note: the earliest note event (the lowest, if several start together);
    // 0 = no notes found. Shown in the analysis report; Tune itself works note by note.
    float firstNoteMidi = 0.0f;
    std::vector<float> noteMidi;     // numFrames * numSlots
    std::vector<float> frameNotes;   // numFrames * maxNotesPerFrame: the notes sounding in the frame (0 = empty)
    int numNoteEvents = 0;
    // Display summary: the most prominent pitch class over all notes, in its usual octave (0 if none stands out),
    // and the share of the note evidence within half a semitone of it (0..1).
    float detectedFundamentalHz = 0.0f;
    float pitchConfidence = 0.0f;
    // Where Tune 100% moves a note of the source with centre pitch noteMidi when key keyMidi (fractional allowed) is
    // played: the nearest note of the key's pitch class, so every note keeps its own register; keys C4..B4 keep the
    // register, every octave the key is above or below that moves the result an octave. A note halfway between two
    // (within 3 cents of it, so the events of one such note all agree) goes up.
    static float tuneTarget (float noteMidi, float keyMidi)
    {
        const float keyOctave = 12.0f * std::floor ((keyMidi - 60.0f) / 12.0f);   // 0 for C4..B4
        const float pitchClass = keyMidi - keyOctave;                              // 60 <= pitchClass < 72
        float x = (noteMidi - pitchClass) / 12.0f;                                 // octaves from the pitch class
        const float frac = x - std::floor (x);
        if (std::abs (frac - 0.5f) < 0.0025f) x = std::floor (x) + 0.5f;
        return pitchClass + 12.0f * std::floor (x + 0.5f) + keyOctave;
    }

    // Fills the note fields from the tracked partials and the mono signal.
    static void analysePitch (ResynthData& d, const float* mono, int len, double sampleRate);
    juce::String name;

    // numFrames * numSlots each. Inactive slots have amp 0 and the frequency of the nearest active
    // frame in the same slot, so interpolating into or out of silence never sweeps the pitch.
    std::vector<float> freq, amp, pan;
    std::vector<float> phase, stereoPhase;   // audio only (empty for images)

    // numFrames * numNoiseBands each; empty when the source has no residual (images)
    std::vector<float> noiseL, noiseR, noiseCoh;
    bool hasNoise() const { return ! noiseL.empty(); }

    // Kept so the source can be stored in the plugin state and re-analysed on load.
    juce::AudioBuffer<float> sourceAudio;
    double sourceRate = 44100.0;
    std::vector<uint8_t> imagePixels;   // brightness, imageWidth * maxSlots, column-major
    int imageWidth = 0;

    static std::shared_ptr<ResynthData> fromAudio (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& name);
    static std::shared_ptr<ResynthData> fromImage (const juce::Image& image, const juce::String& name);

    // Onset-autocorrelation tempo estimate with a strong prior that loops span whole bars.
    static double detectTempo (const float* mono, int numSamples, double sampleRate);

    juce::ValueTree toTree() const;
    static std::shared_ptr<ResynthData> fromTree (const juce::ValueTree& t);

    // Partials drawn on a log-frequency/time plane for display.
    juce::Image renderPreview (int width, int height, juce::Colour colour) const;

    // Offline render of the partials alone at the source pitch/speed (what the residual was measured against).
    void renderPartials (std::vector<float>& outL, std::vector<float>& outR) const;

    // Mean frequency of slot s over the source time span [u0, u1] (seconds), from the interval frequencies.
    float meanFreq (int s, double u0, double u1) const;
    // The same split in two, for every slot of a block: the frames and their weights (seconds) that meanFreq integrates
    // over for [u0, u1], then the mean for a slot. meanFreqSpan returns the count, or 0 when meanFreq must be used
    // (an empty span, or one longer than maxN frames).
    int meanFreqSpan (double u0, double u1, int* frames, double* weights, int maxN) const;
    float meanFreqOver (int s, const int* frames, const double* weights, int count, double u0, double u1) const
    {
        double sum = 0.0;
        for (int i = 0; i < count; ++i) sum += (double) freq[(size_t) frames[i] * (size_t) numSlots + (size_t) s] * weights[i];
        return (float) (sum / (u1 - u0));
    }
    // Phase (cycles) of slot s at source time u, from the nearest frame where the partial exists; -1 if none.
    float phaseAt (int s, double u) const;

private:
    static std::shared_ptr<ResynthData> fromImagePixels (std::vector<uint8_t> pixels, int width, const juce::String& name);
};
