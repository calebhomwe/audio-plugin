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

        const juce::Colour accentTop(hot ? 0xffffc07a : 0xffffa640);
        const juce::Colour accentBottom(hot ? 0xffff8b3d : 0xffff6b1a);
        const juce::Colour glowTint(0x33ff6b1a);
        const juce::Colour faceLight(hot ? 0xff36363f : 0xff33333d);
        const juce::Colour faceDark(hot ? 0xff1c1c22 : 0xff16161c);

        const float sinV = std::sin(valueAngle);
        const float cosV = std::cos(valueAngle);

        const float discR = juce::jmax(4.0f, radius - thickness * 0.5f - 1.5f);
        {
            juce::ColourGradient face(faceLight,
                                      centre.getX() - discR * 0.45f, centre.getY() - discR * 0.45f,
                                      faceDark,
                                      centre.getX() + discR * 0.35f, centre.getY() + discR * 0.35f,
                                      true);
            g.setFillType(juce::FillType(face));
            g.fillEllipse(centre.getX() - discR, centre.getY() - discR, discR * 2.0f, discR * 2.0f);
            g.setColour(kBorder.brighter(hot ? 0.12f : 0.0f));
            g.drawEllipse(centre.getX() - discR, centre.getY() - discR, discR * 2.0f, discR * 2.0f, 1.0f);
        }

        g.setColour(kTextDim.withAlpha(0.30f));
        for (int i = 0; i < 5; ++i)
        {
            const float a = arcOffset + (float)i / 4.0f * (endAngle - startAngle);
            const float sn = std::sin(a), cs = std::cos(a);
            g.drawLine(centre.getX() + cs * (discR - 4.0f), centre.getY() - sn * (discR - 4.0f),
                       centre.getX() + cs * (discR - 1.5f), centre.getY() - sn * (discR - 1.5f), 1.0f);
        }

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

            const juce::PathStrokeType glowStroke(thickness + 2.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded);
            g.setColour(glowTint);
            g.strokePath(value, glowStroke);

            {
                juce::ColourGradient fill(accentTop,
                                          centre.getX(), centre.getY() - radius,
                                          accentBottom,
                                          centre.getX(), centre.getY() + radius,
                                          false);
                g.setFillType(juce::FillType(fill));
                g.strokePath(value, stroke);
            }
        }

        {
            juce::Path needle;
            needle.startNewSubPath(centre.getX() + sinV * discR * 0.18f, centre.getY() - cosV * discR * 0.18f);
            needle.lineTo(centre.getX() + sinV * discR * 0.88f, centre.getY() - cosV * discR * 0.88f);
            juce::Path stroked;
            juce::PathStrokeType(2.0f, juce::PathStrokeType::beveled,
                                 juce::PathStrokeType::rounded).createStrokedPath(stroked, needle);
            g.setColour(kText);
            g.fillPath(stroked);
        }

        auto textArea = bounds.withTrimmedTop(side).reduced(1.0f, 0.0f);
        g.setColour(kTextDim);
        drawFitted(g, label_.toUpperCase(),
                   textArea.removeFromTop(textArea.getHeight() * 0.45f),
                   juce::FontOptions(8.5f));
        g.setColour(kText);
        drawFitted(g, getTextFromValue(getValue()) + suffix_, textArea,
                   juce::FontOptions(10.5f, juce::Font::bold));
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
