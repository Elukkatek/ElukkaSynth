#include "Resynth.h"

// Audio analysis (fromAudio) lives in ResynthAnalysis.cpp. This file holds the data helpers used by the real-time
// engine, the image front end, state storage, the preview and the tempo/fundamental detectors.

namespace
{
constexpr float imageGain = 0.15f;       // level of a fully bright image row
constexpr float imageDbRange = 72.0f;    // black..white maps to -72..0 dB
}

float ResynthData::meanFreq (int s, double u0, double u1) const
{
    const int F = numFrames;
    if (F <= 0) return 0.0f;
    const double fr = frameRate;
    if (u1 <= u0 + 1.0e-9)
    {
        const int f = juce::jlimit (0, F - 1, (int) std::floor (u0 * fr));
        return freq[(size_t) f * (size_t) numSlots + (size_t) s];
    }
    // integrate the piecewise-constant interval frequency over [u0, u1]
    double sum = 0.0;
    int f = juce::jmax (0, (int) std::floor (u0 * fr));
    while (true)
    {
        const double a = juce::jmax (u0, f / fr), b = juce::jmin (u1, (f + 1) / fr);
        if (b > a) sum += (double) freq[(size_t) juce::jmin (f, F - 1) * (size_t) numSlots + (size_t) s] * (b - a);
        if ((f + 1) / fr >= u1 || f >= F - 1) break;
        ++f;
    }
    return (float) (sum / (u1 - u0));
}

float ResynthData::phaseAt (int s, double u) const
{
    if (phase.empty() || numFrames <= 0) return -1.0f;
    const int a = juce::jlimit (0, numFrames - 1, (int) std::floor (u * frameRate));
    const int b = juce::jmin (a + 1, numFrames - 1);
    const size_t ia = (size_t) a * (size_t) numSlots + (size_t) s, ib = (size_t) b * (size_t) numSlots + (size_t) s;
    float x;
    if (amp[ia] > 0.0f)      x = phase[ia] + (float) (u - a / frameRate) * freq[ia];
    else if (amp[ib] > 0.0f) x = phase[ib] - (float) (b / frameRate - u) * freq[ia];   // freq[a] was filled from frame b
    else return -1.0f;
    return x - std::floor (x);
}

// ---- images: rows are harmonics of middle C (bottom = fundamental), columns are time, brightness is level ----

std::shared_ptr<ResynthData> ResynthData::fromImage (const juce::Image& source, const juce::String& name)
{
    if (! source.isValid()) return {};
    const int width = juce::jlimit (1, 1024, source.getWidth());
    auto img = source.rescaled (width, maxSlots, juce::Graphics::highResamplingQuality).convertedToFormat (juce::Image::RGB);
    std::vector<uint8_t> pixels ((size_t) width * maxSlots);
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < maxSlots; ++y)
            pixels[(size_t) x * maxSlots + (size_t) y] =
                (uint8_t) juce::jlimit (0, 255, (int) (img.getPixelAt (x, maxSlots - 1 - y).getPerceivedBrightness() * 255.0f));
    return fromImagePixels (std::move (pixels), width, name);
}

std::shared_ptr<ResynthData> ResynthData::fromImagePixels (std::vector<uint8_t> pixels, int width, const juce::String& name)
{
    auto d = std::make_shared<ResynthData>();
    d->name = name;
    d->isImage = true;
    d->imageWidth = width;
    d->numFrames = width;
    d->numSlots = maxSlots;
    // An image plays over one bar at 120 BPM; tempo sync stretches that to one bar at the host tempo.
    d->durationSec = 2.0;
    d->frameRate = width / d->durationSec;
    d->detectedBpm = 120.0;

    // No phase information: partials start at the oscillator's own (deterministic or random) phases.
    const size_t total = (size_t) width * maxSlots;
    d->freq.resize (total);
    d->amp.resize (total);
    d->pan.assign (total, 0.0f);
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < maxSlots; ++y)
        {
            const size_t i = (size_t) x * maxSlots + (size_t) y;
            const uint8_t b = pixels[i];
            d->freq[i] = (float) (rootHz * (y + 1));
            d->amp[i] = b == 0 ? 0.0f : imageGain * juce::Decibels::decibelsToGain ((float) b / 255.0f * imageDbRange - imageDbRange);
        }
    d->maxAmp = imageGain;
    d->noteMidi.assign (total, 60.0f);   // every row is a harmonic of middle C: one note
    d->frameNotes.assign ((size_t) width * maxNotesPerFrame, 0.0f);
    for (int x = 0; x < width; ++x) d->frameNotes[(size_t) x * maxNotesPerFrame] = 60.0f;
    d->firstNoteMidi = 60.0f;
    d->imagePixels = std::move (pixels);
    return d;
}

// ---- state ----

juce::ValueTree ResynthData::toTree() const
{
    juce::ValueTree t ("Resynth");
    t.setProperty ("isImage", isImage, nullptr);
    t.setProperty ("name", name, nullptr);

    juce::MemoryBlock block;
    if (isImage)
    {
        juce::MemoryOutputStream mos (block, false);
        {
            juce::GZIPCompressorOutputStream gz (mos, 6);
            gz.write (imagePixels.data(), imagePixels.size());
        }
        t.setProperty ("width", imageWidth, nullptr);
        t.setProperty ("pixels", block.toBase64Encoding(), nullptr);
    }
    else
    {
        // The source audio is stored losslessly (FLAC) and re-analysed when the state is loaded.
        auto* mos = new juce::MemoryOutputStream (block, false);
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (mos, sourceRate, (unsigned int) sourceAudio.getNumChannels(), 24, {}, 0));
        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer (sourceAudio, 0, sourceAudio.getNumSamples());
            writer.reset();
        }
        else
            delete mos;
        t.setProperty ("rate", sourceRate, nullptr);
        t.setProperty ("audio", block.toBase64Encoding(), nullptr);
    }
    return t;
}

std::shared_ptr<ResynthData> ResynthData::fromTree (const juce::ValueTree& t)
{
    if (! t.isValid()) return {};
    const auto name = t["name"].toString();
    juce::MemoryBlock block;

    if ((bool) t["isImage"])
    {
        const int width = t["width"];
        if (width <= 0 || ! block.fromBase64Encoding (t["pixels"].toString())) return {};
        juce::MemoryInputStream mis (block, false);
        juce::GZIPDecompressorInputStream gz (mis);
        juce::MemoryBlock raw;
        gz.readIntoMemoryBlock (raw);
        if (raw.getSize() != (size_t) width * maxSlots) return {};
        std::vector<uint8_t> pixels ((const uint8_t*) raw.getData(), (const uint8_t*) raw.getData() + raw.getSize());
        return fromImagePixels (std::move (pixels), width, name);
    }

    if (! t.hasProperty ("audio") || ! block.fromBase64Encoding (t["audio"].toString())) return {};
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, true), true));
    if (reader == nullptr) return {};
    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
    return fromAudio (buf, reader->sampleRate, name);
}

// ---- preview ----

juce::Image ResynthData::renderPreview (int width, int height, juce::Colour colour) const
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true);
    if (numFrames <= 0) return img;

    // Brightness per pixel = loudest partial landing there, in dB relative to the loudest partial.
    std::vector<float> level ((size_t) width * (size_t) height, 0.0f);
    const float logLo = std::log (30.0f), logHi = std::log (16000.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        const int x0 = juce::jmin (width - 1, f * width / numFrames);
        const int x1 = juce::jmax (x0, juce::jmin (width - 1, (f + 1) * width / numFrames - 1));
        for (int s = 0; s < numSlots; ++s)
        {
            const size_t i = (size_t) f * (size_t) numSlots + (size_t) s;
            if (amp[i] <= 0.0f) continue;
            int y0, y1;
            if (isImage)
            {
                y1 = height - 1 - s * height / numSlots;
                y0 = juce::jmax (0, juce::jmin (y1, height - (s + 1) * height / numSlots));
            }
            else
            {
                const float n = (std::log (juce::jmax (30.0f, freq[i])) - logLo) / (logHi - logLo);
                if (n > 1.0f) continue;
                y0 = y1 = juce::jlimit (0, height - 1, (int) ((1.0f - n) * (float) height));
            }
            const float db = juce::Decibels::gainToDecibels (amp[i] / maxAmp, -72.0f);
            const float v = (db + 72.0f) / 72.0f;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    auto& px = level[(size_t) y * (size_t) width + (size_t) x];
                    px = juce::jmax (px, v);
                }
        }
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const float v = level[(size_t) y * (size_t) width + (size_t) x];
            if (v > 0.0f) img.setPixelAt (x, y, colour.withAlpha (v * v));
        }
    return img;
}

// ---- tempo ----

double ResynthData::detectTempo (const float* mono, int numSamples, double sampleRate)
{
    // Onset envelope: positive change in log energy per 256-sample hop.
    const int hop = 256;
    const int n = numSamples / hop;
    if (n < 16) return 120.0;
    const double envRate = sampleRate / hop;

    std::vector<float> energy ((size_t) n), onset ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        double e = 0.0;
        for (int j = 0; j < hop; ++j)
        {
            float s = mono[(size_t) (i * hop + j)];
            e += s * s;
        }
        energy[(size_t) i] = (float) std::log (1.0e-6 + e / hop);
    }
    for (int i = 1; i < n; ++i)
        onset[(size_t) i] = juce::jmax (0.0f, energy[(size_t) i] - energy[(size_t) i - 1]);
    float mean = 0.0f;
    for (auto v : onset) mean += v;
    mean /= (float) n;
    for (auto& v : onset) v -= mean;

    auto acf = [&] (double lagSamples)
    {
        int lag = (int) std::lround (lagSamples);
        if (lag <= 0 || lag >= n) return 0.0;
        double s = 0.0, norm = 0.0;
        for (int i = 0; i + lag < n; ++i)
        {
            s += onset[(size_t) i] * onset[(size_t) (i + lag)];
            norm += onset[(size_t) i] * onset[(size_t) i];
        }
        return norm > 0.0 ? s / norm : 0.0;
    };
    auto score = [&] (double bpm)
    {
        double beat = 60.0 / bpm * envRate;
        return acf (beat) + 0.5 * acf (beat * 2.0) + 0.25 * acf (beat * 4.0);
    };

    const double duration = numSamples / sampleRate;
    const double lo = 70.0, hi = 180.0;

    // Prefer tempos where the file is a whole number of bars (a trimmed loop).
    double best = 0.0, bestScore = -1.0e9;
    for (int beats : { 4, 8, 16, 32, 64 })
    {
        double bpm = 60.0 * beats / duration;
        if (bpm < lo || bpm > hi) continue;
        double s = score (bpm) + (bpm >= 85.0 && bpm <= 165.0 ? 0.5 : 0.0);
        if (s > bestScore) { bestScore = s; best = bpm; }
    }
    if (best > 0.0) return std::round (best * 100.0) / 100.0;

    // Not a clean loop: search the autocorrelation directly.
    for (double bpm = lo; bpm <= hi; bpm += 0.25)
    {
        double s = score (bpm) + (bpm >= 85.0 && bpm <= 165.0 ? 0.1 : 0.0);
        if (s > bestScore) { bestScore = s; best = bpm; }
    }
    return std::round (best * 4.0) / 4.0;
}
