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
    Spectrum()
    {
    }

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

        g.setColour(kPanel);
        g.fillRoundedRectangle(bounds, 6.0f);

        const juce::Rectangle<float> plot = bounds.reduced(1.0f);

        drawGrid(g, plot);

        g.saveState();
        g.reduceClipRegion(plot.toNearestInt());

        if (!spec_.empty())
            drawSpectrum(g, plot);

        drawCurve(g, plot, curve_, kText, 1.5f);

        g.restoreState();
    }

    void resized() override
    {
    }

private:
    static float freqToX(float freq, float width)
    {
        return std::log10(freq / 20.0f) / std::log10(1000.0f) * width;
    }

    static float dbToY(float db, float height)
    {
        return juce::jlimit(0.0f, height, (1.0f - (db + 90.0f) / 96.0f) * height);
    }

    void drawGrid(juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        static const float dbLines[] = { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
        static const float freqLines[] = { 100.0f, 1000.0f, 10000.0f };

        g.setColour(kBorder);

        for (float db : dbLines)
        {
            const float y = plot.getY() + dbToY(db, plot.getHeight());
            g.drawHorizontalLine(static_cast<int>(std::lround(y)), plot.getX(), plot.getRight());
        }

        for (float freq : freqLines)
        {
            const float x = plot.getX() + freqToX(freq, plot.getWidth());
            g.drawVerticalLine(static_cast<int>(std::lround(x)), plot.getY(), plot.getBottom());
        }

        g.setColour(kTextDim);
        g.setFont(8.0f);

        for (float db : dbLines)
        {
            const float y = plot.getY() + dbToY(db, plot.getHeight());
            const juce::String label = (db == 0.0f) ? "0" : juce::String(static_cast<int>(db));
            g.drawText(label, static_cast<int>(plot.getRight() - 28.0f),
                       static_cast<int>(y - 7.0f), 26, 14,
                       juce::Justification::right, false);
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

        g.setColour(kAccentDim);
        g.fillPath(filled);

        g.setColour(kAccent);
        g.strokePath(curve, juce::PathStrokeType(1.2f));
    }

    void drawCurve(juce::Graphics& g, const juce::Rectangle<float>& plot,
                   const std::vector<float>& data, juce::Colour colour, float stroke)
    {
        if (!curveSet_ || data.empty())
            return;

        juce::Path path = buildCurvePath(data, plot);
        if (path.isEmpty())
            return;

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(stroke));
    }

    juce::Path buildCurvePath(const std::vector<float>& data,
                              const juce::Rectangle<float>& plot) const
    {
        juce::Path path;

        const int n = static_cast<int>(data.size());
        if (n <= 1)
            return path;

        const float x0 = plot.getX();
        const float y0 = plot.getY();
        const float w = plot.getWidth();
        const float h = plot.getHeight();
        const float step = 1.0f / static_cast<float>(n - 1);

        path.startNewSubPath(x0, y0 + dbToY(data[0], h));
        for (int i = 1; i < n; ++i)
        {
            const float x = x0 + step * static_cast<float>(i) * w;
            const float y = y0 + dbToY(data[static_cast<size_t>(i)], h);
            path.lineTo(x, y);
        }

        return path;
    }

    std::vector<float> spec_;
    std::vector<float> curve_;
    bool curveSet_ = false;
};

} // namespace ui
} // namespace agm
