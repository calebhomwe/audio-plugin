#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include <atomic>
#include <cmath>

namespace agm {
namespace ui {

class Meter : public juce::Component
{
public:
    enum class Kind { Level, GainReduction };

    explicit Meter(Kind kind) : kind_(kind) {}

    void setLevel(float l, float r)
    {
        const juce::int64 nowMs = juce::Time::currentTimeMillis();
        levelL_.store(juce::jlimit(0.0f, 1.0f, l));
        levelR_.store(juce::jlimit(0.0f, 1.0f, r));
        if (l >= 1.0f) clipLTimeMs_.store(nowMs);
        if (r >= 1.0f) clipRTimeMs_.store(nowMs);
        lastPeakTimeMs_.store(nowMs);
        repaint();
    }

    void setGrDb(float db)
    {
        grDb_.store(juce::jlimit(0.0f, 24.0f, db));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(kPanelHi);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

        if (kind_ == Kind::Level)
            paintLevel(g);
        else
            paintGr(g);
    }

    void resized() override
    {
        rebuildLevelLayout();
    }

private:
    static float gainToDb(float gain)
    {
        if (gain <= 0.0f)
            return -60.0f;
        return juce::jmax(-60.0f, 20.0f * std::log10(gain));
    }

    static float dbToGain(float db)
    {
        return std::pow(10.0f, db * 0.05f);
    }

    static float levelFraction(float level)
    {
        return juce::jlimit(0.0f, 1.0f, 1.0f + gainToDb(level) / 60.0f);
    }

    static void decayPeak(float& peak, float level, float dt)
    {
        if (level > peak)
            peak = level;
        if (peak <= level)
            return;

        const float peakDb = gainToDb(peak);
        const float levelDb = gainToDb(level);
        const float decayed = peakDb - 1.2f * dt;
        if (decayed <= levelDb + 0.05f)
        {
            peak = level;
            return;
        }
        peak = dbToGain(decayed);
    }

    void rebuildLevelLayout()
    {
        if (kind_ != Kind::Level)
            return;

        const juce::Rectangle<int> area = getLocalBounds().reduced(2);
        const int w = juce::jmax(1, (area.getWidth() - 2) / 2);
        barL_ = juce::Rectangle<int>(area.getX(), area.getY(), w, area.getHeight());
        barR_ = juce::Rectangle<int>(area.getX() + w + 2, area.getY(), w, area.getHeight());

        const float top = static_cast<float>(area.getY());
        const float bottom = static_cast<float>(area.getBottom());
        gradientLevel_ = juce::ColourGradient(kMeterLo, 0.0f, bottom, kMeterClip, 0.0f, top, false);
        gradientLevel_.addColour(0.66, kMeterHi);
    }

    void paintLevel(juce::Graphics& g)
    {
        const float l = levelL_.load();
        const float r = levelR_.load();

        const juce::int64 nowMs = juce::Time::currentTimeMillis();
        const float dt = juce::jlimit(0.0f, 1.0f, static_cast<float>(nowMs - lastPeakTimeMs_.load()) / 1000.0f);
        if (dt > 0.0f)
        {
            decayPeak(peakL_, l, dt);
            decayPeak(peakR_, r, dt);
            lastPeakTimeMs_.store(nowMs);
        }

        const bool flashL = (nowMs - clipLTimeMs_.load()) < 1500;
        const bool flashR = (nowMs - clipRTimeMs_.load()) < 1500;

        drawLevelBar(g, barL_, l, peakL_, flashL);
        drawLevelBar(g, barR_, r, peakR_, flashR);

        if (peakL_ > l || peakR_ > r || flashL || flashR)
            repaint();
    }

    void drawLevelBar(juce::Graphics& g, const juce::Rectangle<int>& bar, float level, float peak, bool clipFlash)
    {
        if (bar.getWidth() < 1 || bar.getHeight() < 1)
            return;

        if (gradientLevel_.getNumColours() >= 2)
        {
            const int fillH = juce::jlimit(0, bar.getHeight(), juce::roundToInt(levelFraction(level) * static_cast<float>(bar.getHeight())));
            if (fillH > 0)
            {
                const juce::Rectangle<float> fill(static_cast<float>(bar.getX()),
                                                  static_cast<float>(bar.getBottom() - fillH),
                                                  static_cast<float>(bar.getWidth()),
                                                  static_cast<float>(fillH));
                g.setGradientFill(gradientLevel_);
                g.fillRect(fill);
            }
        }

        const float pf = levelFraction(peak);
        if (pf > 0.0f)
        {
            const float y = static_cast<float>(bar.getBottom()) - pf * static_cast<float>(bar.getHeight());
            const float lineY = juce::jmax(static_cast<float>(bar.getY()) + 0.75f, y - 0.75f);
            g.setColour(kText);
            g.fillRect(juce::Rectangle<float>(static_cast<float>(bar.getX()), lineY,
                                              static_cast<float>(bar.getWidth()), 1.5f));
        }

        if (clipFlash)
        {
            const float topH = juce::jmin(3.0f, static_cast<float>(bar.getHeight()));
            g.setColour(kMeterClip);
            g.fillRect(juce::Rectangle<float>(static_cast<float>(bar.getX()), static_cast<float>(bar.getY()),
                                              static_cast<float>(bar.getWidth()), topH));
        }
    }

    void paintGr(juce::Graphics& g)
    {
        juce::Rectangle<int> area = getLocalBounds().reduced(2);
        const juce::Rectangle<int> textRect = area.removeFromBottom(juce::jmax(0, juce::jmin(10, area.getHeight() - 2)));

        const float frac = juce::jlimit(0.0f, 1.0f, grDb_.load() / 24.0f);
        const int fillH = juce::jlimit(0, area.getHeight(), juce::roundToInt(frac * static_cast<float>(area.getHeight())));
        if (fillH > 0)
        {
            g.setColour(kAccent);
            g.fillRect(area.getX(), area.getY(), area.getWidth(), fillH);
        }

        if (!textRect.isEmpty())
        {
            g.setColour(kTextDim);
            g.setFont(juce::FontOptions().withHeight(9.0f));
            g.drawText(grLabel_, textRect, juce::Justification::centred, false);
        }
    }

    Kind kind_;
    std::atomic<float> levelL_{0.0f};
    std::atomic<float> levelR_{0.0f};
    std::atomic<float> grDb_{0.0f};

    float peakL_ = 0.0f;
    float peakR_ = 0.0f;

    std::atomic<juce::int64> lastPeakTimeMs_{0};
    std::atomic<juce::int64> clipLTimeMs_{0};
    std::atomic<juce::int64> clipRTimeMs_{0};

    juce::Rectangle<int> barL_;
    juce::Rectangle<int> barR_;
    juce::ColourGradient gradientLevel_;
    juce::String grLabel_{"GR"};
};

} // namespace ui
} // namespace agm
