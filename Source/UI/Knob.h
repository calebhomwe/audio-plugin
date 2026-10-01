#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include <cmath>

namespace agm {
namespace ui {

class Knob : public juce::Slider
{
public:
    // `scale` and `decimals` control the READOUT only, never the value: a 0..1
    // parameter reads as 0..100 % with scale 100, and a frequency reads in whole
    // hertz. Without this, ten 0..1 controls were drawn with zero decimals and
    // could only ever show "0" or "1".
    Knob(const juce::String& label, const juce::String& suffix = {},
         float scale = 1.0f, int decimals = 1)
        : label_(label), suffix_(suffix), scale_(scale), decimals_(decimals)
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

        /* Drawn from drawnTexts(), which is also what the visual rubric measures.
           A knob paints its own caption and its own readout, so a rig that walks
           the component tree sees neither; and drawFitted() condenses a string
           that does not fit instead of clipping it, so "the required width" is
           only meaningful if the measurement knows the condensed font. One
           source for both. */
        for (const auto& t : drawnTexts())
        {
            g.setColour(t.colour);
            g.setFont(t.font);
            g.drawText(t.text, t.bounds, juce::Justification::centred, false);
        }
    }

    /* A string this knob paints for itself, with the rectangle it is drawn in and
       the font it is ACTUALLY drawn with - horizontal scale included, because
       drawFitted() condenses rather than clips. */
    struct DrawnText
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        juce::Font font { juce::FontOptions {} };
        juce::Colour colour;
        bool isReadout = false;
        /* 1.0 when the string fitted; below 1.0 by the factor drawFitted() had to
           condense it. It bottoms out at 0.55, and at the floor the text IS
           clipped. */
        float condensed = 1.0f;
    };

    juce::Array<DrawnText> drawnTexts() const
    {
        juce::Array<DrawnText> out;
        const auto bounds = getLocalBounds().toFloat();
        const float side = juce::jmin(bounds.getWidth(), bounds.getHeight() - 24.0f);
        if (side <= 4.0f)
            return out;
        auto textArea = bounds.withTrimmedTop(side).reduced(1.0f, 0.0f);
        const auto captionArea = textArea.removeFromTop(textArea.getHeight() * 0.45f);
        out.add(fitted(label_.toUpperCase(), captionArea, juce::FontOptions(8.5f), kTextDim, false));
        out.add(fitted(readout(), textArea, juce::FontOptions(10.5f, juce::Font::bold), kText, true));
        return out;
    }

    /* The knob's dial, for R7: the value arc and the track it sits on. */
    juce::Colour trackColour()     const { return kBorder; }
    juce::Colour valueArcColour()  const { return juce::Colour(0xffff6b1a); }
    juce::Colour needleColour()    const { return kText; }
    juce::Colour faceColour()      const { return juce::Colour(0xff16161c); }

    juce::String readout() const
    {
        return juce::String(getValue() * (double)scale_, decimals_) + suffix_;
    }

private:
    /* The condensing drawFitted() used to do inline, returned as data so paint()
       and the rubric cannot disagree about what was drawn. */
    static DrawnText fitted(const juce::String& text, juce::Rectangle<float> area,
                            const juce::FontOptions& options, juce::Colour colour, bool isReadout)
    {
        DrawnText t;
        t.text = text;
        t.bounds = area.getSmallestIntegerContainer();
        t.colour = colour;
        t.isReadout = isReadout;
        juce::Font f(options);
        const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, text));
        if (w > area.getWidth() && w > 0)
        {
            t.condensed = juce::jmax(0.55f, (float)area.getWidth() / (float)w);
            f.setHorizontalScale(t.condensed);
        }
        t.font = f;
        return t;
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    juce::String label_;
    juce::String suffix_;
    float scale_ = 1.0f;
    int decimals_ = 1;
};

} // namespace ui
} // namespace agm
