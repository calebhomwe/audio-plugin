#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include <vector>
#include <cmath>

namespace agm {
namespace ui {

class Spectrum : public juce::Component
{
public:
    Spectrum() = default;

    void setSpectrum(const float* magsDb, int n)
    {
        if (magsDb != nullptr && n > 1)
            spec_.assign(magsDb, magsDb + n);
        else
            spec_.clear();
    }

    void setEqCurve(const float* magsDb, int n)
    {
        if (magsDb != nullptr && n > 1)
        {
            curve_.assign(magsDb, magsDb + n);
            curveSet_ = true;
        }
        else
        {
            curve_.clear();
            curveSet_ = false;
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(kBg);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        const auto plot = bounds.reduced(6.0f, 4.0f).withTrimmedBottom(11.0f);
        if (plot.isEmpty())
            return;

        drawGrid(g, plot);

        g.saveState();
        g.reduceClipRegion(plot.toNearestInt());
        if (!spec_.empty())
            drawSpectrum(g, plot);
        if (curveSet_ && !curve_.empty())
            drawCurve(g, plot);
        g.restoreState();

        drawLabels(g, plot);
    }

    void resized() override {}

private:
    static float freqToX(float freq, float width)
    {
        return std::log10(freq / 20.0f) / 3.0f * width;
    }

    static float dbToY(float db, float height)
    {
        return juce::jlimit(0.0f, height, (1.0f - (db + 90.0f) / 96.0f) * height);
    }

    void drawGrid(juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        static const float dbLines[] = { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
        static const float majorFreqs[] = { 100.0f, 1000.0f, 10000.0f };
        static const float minorFreqs[] = { 30.0f, 300.0f, 3000.0f };

        g.setColour(kBorder.withAlpha(0.4f));
        for (float f : minorFreqs)
        {
            const float x = plot.getX() + freqToX(f, plot.getWidth());
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        g.setColour(kBorder);
        for (float db : dbLines)
        {
            const float y = plot.getY() + dbToY(db, plot.getHeight());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }
        for (float f : majorFreqs)
        {
            const float x = plot.getX() + freqToX(f, plot.getWidth());
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }
    }

    void drawLabels(juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        static const float dbLines[] = { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
        static const struct { float freq; const char* text; } freqLabels[] = {
            { 20.0f, "20" }, { 100.0f, "100" }, { 1000.0f, "1k" },
            { 10000.0f, "10k" }, { 20000.0f, "20k" } };

        g.setColour(kTextDim.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(8.0f)));

        for (float db : dbLines)
        {
            const float y = plot.getY() + dbToY(db, plot.getHeight());
            g.drawText(db == 0.0f ? juce::String("0") : juce::String((int)db),
                       juce::Rectangle<float>(plot.getRight() - 26.0f, y - 6.0f, 22.0f, 12.0f),
                       juce::Justification::right, false);
        }

        for (auto& fl : freqLabels)
        {
            const float x = plot.getX() + freqToX(fl.freq, plot.getWidth());
            const float w = 30.0f;
            float lx = x - w * 0.5f;
            auto just = juce::Justification::centred;
            if (fl.freq <= 20.0f)
            {
                lx = x;
                just = juce::Justification::left;
            }
            else if (fl.freq >= 20000.0f)
            {
                lx = x - w;
                just = juce::Justification::right;
            }
            g.drawText(juce::String(fl.text),
                       juce::Rectangle<float>(lx, plot.getBottom() + 1.0f, w, 9.0f), just, false);
        }
    }

    void drawSpectrum(juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        juce::Path curve = buildCurvePath(spec_, plot);
        if (curve.isEmpty())
            return;

        juce::Path filled = curve;
        filled.lineTo(plot.getRight(), plot.getBottom());
        filled.lineTo(plot.getX(), plot.getBottom());
        filled.closeSubPath();

        g.setGradientFill(juce::ColourGradient(kAccent.withAlpha(0.38f), 0.0f, plot.getY(),
                                               kAccent.withAlpha(0.03f), 0.0f, plot.getBottom(), false));
        g.fillPath(filled);

        g.setColour(kAccent);
        g.strokePath(curve, juce::PathStrokeType(1.4f));
    }

    void drawCurve(juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        juce::Path path = buildCurvePath(curve_, plot);
        if (path.isEmpty())
            return;

        g.setColour(kText.withAlpha(0.22f));
        g.strokePath(path, juce::PathStrokeType(3.5f));
        g.setColour(kText);
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }

    juce::Path buildCurvePath(const std::vector<float>& data, const juce::Rectangle<float>& plot) const
    {
        juce::Path path;
        const int n = (int)data.size();
        if (n <= 1)
            return path;

        const float x0 = plot.getX();
        const float y0 = plot.getY();
        const float w = plot.getWidth();
        const float h = plot.getHeight();
        const float step = w / (float)(n - 1);

        path.startNewSubPath(x0, y0 + dbToY(data[0], h));
        for (int i = 1; i < n; ++i)
            path.lineTo(x0 + step * (float)i, y0 + dbToY(data[(size_t)i], h));

        return path;
    }

    std::vector<float> spec_;
    std::vector<float> curve_;
    bool curveSet_ = false;
};

} // namespace ui
} // namespace agm
