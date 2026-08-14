#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"

namespace agm::ui {

class Knob : public juce::Slider
{
public:
    Knob(const juce::String& label, const juce::String& suffix = {})
    {
        label_ = label;
        suffix_ = suffix;

        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setDoubleClickReturnValue(true, 0.0);
        setVelocityBasedMode(true);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        g.setColour(kPanel);
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);

        const float side = static_cast<float>(juce::jmax(0, juce::jmin(bounds.getWidth(), bounds.getHeight() - 22)));
        const float radius = side * 0.38f;
        const juce::Point<float> centre(static_cast<float>(bounds.getCentreX()),
                                        static_cast<float>(bounds.getY()) + side * 0.5f);

        const float startAngle = juce::MathConstants<float>::pi * 1.25f;
        const float endAngle = juce::MathConstants<float>::pi * 2.75f;
        const float valueAngle = startAngle + (endAngle - startAngle) * valueToProportionOfLength(getValue());

        const float arcStart = startAngle - juce::MathConstants<float>::halfPi;
        const float trackLength = endAngle - startAngle;
        const float valueLength = valueAngle - startAngle;
        const float thickness = 3.5f;

        juce::Path trackArc;
        trackArc.addCentredArc(centre.getX(), centre.getY(), radius, radius, 0.0f,
                               arcStart, arcStart + trackLength, true);
        g.setColour(kBorder);
        g.strokePath(trackArc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc(centre.getX(), centre.getY(), radius, radius, 0.0f,
                               arcStart, arcStart + valueLength, true);
        g.setColour(kAccent);
        g.strokePath(valueArc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float pointerLength = radius * 0.8f;
        const float px = centre.getX() + std::sin(valueAngle) * pointerLength;
        const float py = centre.getY() - std::cos(valueAngle) * pointerLength;
        g.setColour(kText);
        g.drawLine(juce::Line<float>(centre.getX(), centre.getY(), px, py), 2.0f);

        auto textArea = bounds.withTrimmedTop(juce::roundToInt(side)).reduced(2, 0);
        g.setColour(kTextDim);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(label_, textArea.removeFromTop(textArea.getHeight() / 2), juce::Justification::centred, false);

        g.setColour(kText);
        g.setFont(juce::Font(12.0f));
        g.drawText(getTextFromValue(getValue()) + suffix_, textArea, juce::Justification::centred, false);
    }

private:
    juce::String label_;
    juce::String suffix_;
};

}
