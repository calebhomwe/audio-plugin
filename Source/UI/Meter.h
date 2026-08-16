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
        const juce::int64 now = juce::Time::currentTimeMillis();
        targetL_.store(juce::jlimit(0.0f, 1.0f, l));
        targetR_.store(juce::jlimit(0.0f, 1.0f, r));
        if (l >= 1.0f) clipLTimeMs_.store(now);
        if (r >= 1.0f) clipRTimeMs_.store(now);
        repaint();
    }

    void setGrDb(float db)
    {
        grTarget_.store(juce::jlimit(0.0f, kGrRange, db));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(kBg);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

        const juce::int64 nowMs = juce::Time::currentTimeMillis();
        const float dt = juce::jlimit(0.0f, 0.25f, (float)(nowMs - lastPaintMs_) * 0.001f);
        lastPaintMs_ = nowMs;

        if (kind_ == Kind::Level)
            paintLevel(g, dt, nowMs);
        else
            paintGr(g, dt);
    }

    void resized() override
    {
        if (kind_ != Kind::Level)
            return;

        const auto area = getLocalBounds().reduced(2);
        const int w = juce::jmax(1, (area.getWidth() - 2) / 2);
        barL_ = juce::Rectangle<int>(area.getX(), area.getY(), w, area.getHeight());
        barR_ = juce::Rectangle<int>(area.getX() + w + 2, area.getY(),
                                     juce::jmax(1, area.getWidth() - w - 2), area.getHeight());

        gradientLevel_ = juce::ColourGradient(kMeterLo, 0.0f, (float)area.getBottom(),
                                              kMeterClip, 0.0f, (float)area.getY(), false);
        gradientLevel_.addColour(0.62, kMeterHi);
    }

private:
    static constexpr float kGrRange = 24.0f;

    static float gainToDb(float gain)
    {
        return gain <= 0.0f ? -60.0f : juce::jmax(-60.0f, 20.0f * std::log10(gain));
    }

    static float dbToGain(float db)
    {
        return std::pow(10.0f, db * 0.05f);
    }

    static float levelFraction(float level)
    {
        return juce::jlimit(0.0f, 1.0f, 1.0f + gainToDb(level) / 60.0f);
    }

    static float approach(float current, float target, float dt, float tauMs)
    {
        if (dt <= 0.0f)
            return current;
        return current + (target - current) * (1.0f - std::exp(-dt * 1000.0f / tauMs));
    }

    static void updatePeak(float& peak, juce::int64& holdMs, float level, float dt, juce::int64 nowMs)
    {
        if (level >= peak)
        {
            peak = level;
            holdMs = nowMs;
            return;
        }
        if (nowMs - holdMs < 600)
            return;
        const float db = gainToDb(peak) - 28.0f * dt;
        peak = juce::jmax(level, dbToGain(db));
    }

    void paintLevel(juce::Graphics& g, float dt, juce::int64 nowMs)
    {
        const float tl = targetL_.load();
        const float tr = targetR_.load();
        dispL_ = approach(dispL_, tl, dt, tl > dispL_ ? 15.0f : 90.0f);
        dispR_ = approach(dispR_, tr, dt, tr > dispR_ ? 15.0f : 90.0f);
        updatePeak(peakL_, peakHoldL_, tl, dt, nowMs);
        updatePeak(peakR_, peakHoldR_, tr, dt, nowMs);

        const bool flashL = nowMs - clipLTimeMs_.load() < 1500;
        const bool flashR = nowMs - clipRTimeMs_.load() < 1500;

        drawLevelBar(g, barL_, dispL_, peakL_, flashL);
        drawLevelBar(g, barR_, dispR_, peakR_, flashR);

        if (std::abs(dispL_ - tl) > 0.003f || std::abs(dispR_ - tr) > 0.003f ||
            peakL_ > tl + 0.002f || peakR_ > tr + 0.002f || flashL || flashR)
            repaint();
    }

    void drawLevelBar(juce::Graphics& g, const juce::Rectangle<int>& bar, float level, float peak, bool clipFlash)
    {
        if (bar.getWidth() < 1 || bar.getHeight() < 2)
            return;

        const int fillH = juce::jlimit(0, bar.getHeight(),
                                       juce::roundToInt(levelFraction(level) * (float)bar.getHeight()));
        if (fillH > 0)
        {
            g.setGradientFill(gradientLevel_);
            g.fillRect(juce::Rectangle<float>((float)bar.getX(), (float)(bar.getBottom() - fillH),
                                              (float)bar.getWidth(), (float)fillH));
        }

        const float pf = levelFraction(peak);
        if (pf > 0.02f)
        {
            const float y = juce::jmax((float)bar.getY(),
                                       (float)bar.getBottom() - pf * (float)bar.getHeight() - 1.5f);
            g.setColour(kText);
            g.fillRect(juce::Rectangle<float>((float)bar.getX(), y, (float)bar.getWidth(), 1.5f));
        }

        if (clipFlash)
        {
            g.setColour(kMeterClip);
            g.fillRect(juce::Rectangle<float>((float)bar.getX(), (float)bar.getY(),
                                              (float)bar.getWidth(),
                                              juce::jmin(3.0f, (float)bar.getHeight())));
        }
    }

    void paintGr(juce::Graphics& g, float dt)
    {
        auto area = getLocalBounds().reduced(3);
        const auto textRect = area.removeFromBottom(juce::jlimit(0, 12, area.getHeight() / 5));

        const float target = grTarget_.load();
        grDisp_ = approach(grDisp_, target, dt, target > grDisp_ ? 20.0f : 140.0f);
        if (grDisp_ < 0.05f)
            grDisp_ = 0.0f;

        if (!area.isEmpty())
        {
            g.setColour(kBorder);
            for (int i = 1; i <= 3; ++i)
            {
                const float y = (float)area.getY() + (float)area.getHeight() * ((float)i * 6.0f / kGrRange);
                g.drawHorizontalLine(juce::roundToInt(y), (float)area.getX(), (float)area.getRight());
            }

            const float frac = juce::jlimit(0.0f, 1.0f, grDisp_ / kGrRange);
            const int fillH = juce::roundToInt(frac * (float)area.getHeight());
            if (fillH > 0)
            {
                g.setGradientFill(juce::ColourGradient(kMeterHi, 0.0f, (float)area.getY(),
                                                       kMeterClip, 0.0f, (float)area.getBottom(), false));
                g.fillRect(juce::Rectangle<float>((float)area.getX(), (float)area.getY(),
                                                  (float)area.getWidth(), (float)fillH));
            }
        }

        if (!textRect.isEmpty())
        {
            const bool showValue = grDisp_ >= 0.5f && textRect.getWidth() >= 14;
            g.setColour(showValue ? kText : kTextDim);
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.drawText(showValue ? juce::String(-(int)(grDisp_ + 0.5f)) : juce::String("GR"),
                       textRect, juce::Justification::centred, false);
        }

        if (std::abs(grDisp_ - target) > 0.02f)
            repaint();
    }

    Kind kind_;
    std::atomic<float> targetL_{0.0f};
    std::atomic<float> targetR_{0.0f};
    std::atomic<float> grTarget_{0.0f};
    std::atomic<juce::int64> clipLTimeMs_{0};
    std::atomic<juce::int64> clipRTimeMs_{0};

    float dispL_ = 0.0f, dispR_ = 0.0f, grDisp_ = 0.0f;
    float peakL_ = 0.0f, peakR_ = 0.0f;
    juce::int64 peakHoldL_ = 0, peakHoldR_ = 0;
    juce::int64 lastPaintMs_ = 0;

    juce::Rectangle<int> barL_, barR_;
    juce::ColourGradient gradientLevel_;
};

} // namespace ui
} // namespace agm
