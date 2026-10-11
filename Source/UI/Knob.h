#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include <cmath>
#include "../ValueParsing.h"

namespace agm {
namespace ui {

class Knob : public juce::Slider
{
public:
    enum class EntryUnit { Number, Frequency, Percent, Decibels, Milliseconds, Seconds, Ratio };

    Knob(const juce::String& label, const juce::String& suffix = {})
        : label_(label), suffix_(suffix)
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setDoubleClickReturnValue(true, 0.0);
        setVelocityBasedMode(false);
        setMouseDragSensitivity(160);
        setWantsKeyboardFocus(true);
        setTitle(label);
    }

    void setEntryUnit(EntryUnit unit) { entryUnit = unit; }
    std::function<void(double)> onValueEntered;

    bool commitValueText(const juce::String& text)
    {
        double value = 0.0;
        juce::String suffix;
        if (!agm::parseFiniteDecimal(text, value, &suffix)) return false;
        const auto unit = suffix.toLowerCase();
        if (unit.isNotEmpty())
        {
            if (entryUnit == EntryUnit::Frequency && unit == "khz") value *= 1000.0;
            else if (entryUnit == EntryUnit::Frequency && unit == "hz") {}
            else if (entryUnit == EntryUnit::Percent && unit == "%")
                value *= getMaximum() <= 1.0 ? 0.01 : 1.0;
            else if (entryUnit == EntryUnit::Decibels && unit == "db") {}
            else if (entryUnit == EntryUnit::Milliseconds && unit == "ms") {}
            else if (entryUnit == EntryUnit::Seconds && unit == "s") {}
            else if (entryUnit == EntryUnit::Ratio && unit == ":1") {}
            else return false;
        }
        if (!std::isfinite(value)) return false;
        value = juce::jlimit(getMinimum(), getMaximum(), value);
        if (onValueEntered) onValueEntered(value);
        else setValue(value, juce::sendNotificationSync);
        return true;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() || (event.y >= getHeight() - 24 && event.getNumberOfClicks() == 1))
        { showValueEntry(); return; }
        setMouseDragSensitivity(event.mods.isShiftDown() ? 1600 : 160);
        juce::Slider::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu() && event.getMouseDownPosition().y < getHeight() - 24)
            juce::Slider::mouseDrag(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::Slider::mouseUp(event);
        setMouseDragSensitivity(160);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey) { showValueEntry(); return true; }
        return juce::Slider::keyPressed(key);
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
                   juce::FontOptions(9.0f));
        g.setColour(kText);
        drawFitted(g, getTextFromValue(getValue()) + suffix_, textArea,
                   juce::FontOptions(10.5f, juce::Font::bold));
    }

private:
    class ValueEntry : public juce::Component
    {
    public:
        explicit ValueEntry(Knob& knob) : owner(&knob)
        {
            setSize(238, 104);
            caption.setText(knob.label_ + "  ·  " + juce::String(knob.getMinimum())
                            + " to " + juce::String(knob.getMaximum()), juce::dontSendNotification);
            caption.setColour(juce::Label::textColourId, kTextDim);
            caption.setFont(juce::Font(juce::FontOptions(11.0f))); addAndMakeVisible(caption);
            input.setText(knob.getTextFromValue(knob.getValue()) + knob.suffix_);
            input.setSelectAllWhenFocused(true); addAndMakeVisible(input);
            apply.setButtonText("APPLY"); addAndMakeVisible(apply);
            apply.onClick = [this] { commit(); };
            input.onReturnKey = [this] { commit(); };
            input.onEscapeKey = [this] { dismiss(); };
        }
        void resized() override
        {
            caption.setBounds(8, 4, 222, 20);
            input.setBounds(12, 30, 214, 26); apply.setBounds(142, 66, 84, 26);
        }
        void visibilityChanged() override { if (isShowing()) input.grabKeyboardFocus(); }
    private:
        void dismiss() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
        void commit()
        {
            if (owner != nullptr && owner->commitValueText(input.getText())) dismiss();
            else { caption.setText("Enter a finite number with matching units", juce::dontSendNotification);
                   caption.setColour(juce::Label::textColourId, kAccentHot); }
        }
        juce::Component::SafePointer<Knob> owner;
        juce::Label caption;
        juce::TextEditor input;
        juce::TextButton apply;
    };

    void showValueEntry()
    {
        juce::CallOutBox::launchAsynchronously(std::make_unique<ValueEntry>(*this), getScreenBounds(), nullptr);
    }

    static void drawFitted(juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<float> area, const juce::FontOptions& options)
    {
        juce::Font f(options);
        g.setFont(f);
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text);
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
    EntryUnit entryUnit = EntryUnit::Number;
};

} // namespace ui
} // namespace agm
