#pragma once
#include <JuceHeader.h>

// Editable breakpoint shape used by the LFO/envelope units.
// The UI edits `points` on the message thread; the audio thread reads the baked `lut`.
struct ShapePoint { float x, y, curve; };

class ModShape
{
public:
    static constexpr int lutSize = 512;

    ModShape() { setPoints (preset (0)); }

    void setPoints (std::vector<ShapePoint> p)
    {
        std::sort (p.begin(), p.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        if (p.size() < 2) p = preset (0);
        p.front().x = 0.0f;
        p.back().x = 1.0f;
        points = std::move (p);
        bake();
    }

    const std::vector<ShapePoint>& getPoints() const { return points; }

    // x in [0, 1] -> y in [0, 1]
    float eval (float x) const noexcept
    {
        float f = juce::jlimit (0.0f, 1.0f, x) * (float) lutSize;
        int i = juce::jmin ((int) f, lutSize - 1);
        float t = f - (float) i;
        return lut[(size_t) i] + (lut[(size_t) i + 1] - lut[(size_t) i]) * t;
    }

    static float curveT (float t, float curve)
    {
        if (curve >= 0.0f) return std::pow (t, 1.0f + curve * 4.0f);
        return 1.0f - std::pow (1.0f - t, 1.0f - curve * 4.0f);
    }

    float evalPoints (float x) const
    {
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            auto& a = points[i];
            auto& b = points[i + 1];
            if (x <= b.x || i + 2 == points.size())
            {
                float w = b.x - a.x;
                float t = w > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (x - a.x) / w) : 1.0f;
                return a.y + (b.y - a.y) * curveT (t, a.curve);
            }
        }
        return points.back().y;
    }

    static const juce::StringArray& presetNames()
    {
        static juce::StringArray n { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Decay", "Attack-Decay", "Steps", "Pluck" };
        return n;
    }

    static std::vector<ShapePoint> preset (int idx)
    {
        switch (idx)
        {
            case 1:  return { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } };
            case 2:  return { { 0, 1, 0 }, { 1, 0, 0 } };
            case 3:  return { { 0, 0, 0 }, { 1, 1, 0 } };
            case 4:  return { { 0, 1, 0 }, { 0.499f, 1, 0 }, { 0.5f, 0, 0 }, { 0.999f, 0, 0 }, { 1, 1, 0 } };
            case 5:  return { { 0, 1, -0.6f }, { 1, 0, 0 } };
            case 6:  return { { 0, 0, -0.3f }, { 0.15f, 1, -0.5f }, { 1, 0, 0 } };
            case 7:  return { { 0, 0.2f, 0 }, { 0.249f, 0.2f, 0 }, { 0.25f, 0.9f, 0 }, { 0.499f, 0.9f, 0 },
                              { 0.5f, 0.5f, 0 }, { 0.749f, 0.5f, 0 }, { 0.75f, 0.0f, 0 }, { 1, 0.0f, 0 } };
            case 8:  return { { 0, 1, -0.9f }, { 0.35f, 0.1f, -0.3f }, { 1, 0, 0 } };
            default: return { { 0, 0.5f, -0.2f }, { 0.25f, 1, 0.2f }, { 0.5f, 0.5f, -0.2f }, { 0.75f, 0, 0.2f }, { 1, 0.5f, 0 } };
        }
    }

    juce::ValueTree toTree (const juce::Identifier& type) const
    {
        juce::ValueTree t (type);
        for (auto& p : points)
        {
            juce::ValueTree pt ("Pt");
            pt.setProperty ("x", p.x, nullptr);
            pt.setProperty ("y", p.y, nullptr);
            pt.setProperty ("c", p.curve, nullptr);
            t.appendChild (pt, nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        if (! t.isValid()) return;
        std::vector<ShapePoint> p;
        for (auto c : t)
            p.push_back ({ (float) c["x"], (float) c["y"], (float) c["c"] });
        setPoints (p);
    }

private:
    void bake()
    {
        for (int i = 0; i <= lutSize; ++i)
            lut[(size_t) i] = juce::jlimit (0.0f, 1.0f, evalPoints ((float) i / (float) lutSize));
    }

    std::vector<ShapePoint> points;
    std::array<float, lutSize + 1> lut {};
};
