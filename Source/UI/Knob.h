#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include <cmath>

namespace agm {
namespace ui {

class Knob : public juce::Slider
{
public:
    Knob(const juce::String& label, const juce::String& suffix = {})
        : label_(label), suffix_(suffix)
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setDoubleClickReturnValue(true, 0.0);
        setVelocityBasedMode(true);
        setMouseDragSensitivity(160);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float side = juce::jmin(bounds.getWidth(), bounds.getHeight() - 24.0f);
        if (side <= 4.0f)
            return;

        const juce::Point<float> centre(bounds.getCentreX(), bounds.getY() + side * 0.5f);
        const float radius = side * 0.38f;
        const bool hot = isMouseOverOrDragging();

        const float pi = juce::MathConstants<float>::pi;
        const float startAngle = pi * 1.25f;
        const float endAngle = pi * 2.75f;
        const float prop = (float)valueToProportionOfLength(getValue());
        const float valueAngle = startAngle + (endAngle - startAngle) * prop;
        const float arcOffset = startAngle - juce::MathConstants<float>::halfPi;
        const float thickness = juce::jmax(2.5f, side * 0.075f);
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc(centre.getX(), centre.getY(), radius, radius, 0.0f,
                            arcOffset, arcOffset + (endAngle - startAngle), true);
        g.setColour(kBorder);
        g.strokePath(track, stroke);

        if (prop > 0.002f)
        {
            juce::Path value;
            value.addCentredArc(centre.getX(), centre.getY(), radius, radius, 0.0f,
                                arcOffset, arcOffset + (valueAngle - startAngle), true);
            g.setColour(hot ? kAccent.brighter(0.3f) : kAccent);
            g.strokePath(value, stroke);
        }

        const float capR = radius * 0.62f;
        g.setColour(hot ? kPanelHi.brighter(0.2f) : kPanelHi);
        g.fillEllipse(centre.getX() - capR, centre.getY() - capR, capR * 2.0f, capR * 2.0f);
        g.setColour(kBorder);
        g.drawEllipse(centre.getX() - capR, centre.getY() - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        const float sinA = std::sin(valueAngle);
        const float cosA = std::cos(valueAngle);
        g.setColour(hot ? kText : kTextDim);
        g.drawLine(centre.getX() + sinA * capR * 0.3f, centre.getY() - cosA * capR * 0.3f,
                   centre.getX() + sinA * capR * 0.9f, centre.getY() - cosA * capR * 0.9f,
                   juce::jmax(1.5f, side * 0.04f));

        auto textArea = bounds.withTrimmedTop(side).reduced(1.0f, 0.0f);
        g.setColour(kTextDim);
        drawFitted(g, label_, textArea.removeFromTop(textArea.getHeight() * 0.45f),
                   juce::FontOptions(9.5f, juce::Font::bold));
        g.setColour(kText);
        drawFitted(g, getTextFromValue(getValue()) + suffix_, textArea, juce::FontOptions(11.0f));
    }

private:
    static void drawFitted(juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<float> area, const juce::FontOptions& options)
    {
        juce::Font f(options);
        g.setFont(f);
        const int w = g.getCurrentFont().getStringWidth(text);
        if (w > area.getWidth() && w > 0)
        {
            f.setHorizontalScale(juce::jmax(0.55f, (float)area.getWidth() / (float)w));
            g.setFont(f);
        }
        g.drawText(text, area, juce::Justification::centred, false);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    juce::String label_;
    juce::String suffix_;
};

} // namespace ui
} // namespace agm
