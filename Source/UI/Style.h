#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace agm {
namespace ui {

const juce::Colour kBg        = juce::Colour(0xff121217);
const juce::Colour kPanel     = juce::Colour(0xff1d1d25);
const juce::Colour kPanelHi   = juce::Colour(0xff25252f);
const juce::Colour kBorder    = juce::Colour(0xff2c2c38);
const juce::Colour kText      = juce::Colour(0xffe9e9f0);
const juce::Colour kTextDim   = juce::Colour(0xff8a8a99);
const juce::Colour kAccent    = juce::Colour(0xffff6b1a);
const juce::Colour kAccentDim = juce::Colour(0x66ff6b1a);
const juce::Colour kMeterLo   = juce::Colour(0xff3fae5a);
const juce::Colour kMeterHi   = juce::Colour(0xffe0b32a);
const juce::Colour kMeterClip = juce::Colour(0xffe04a2a);

const int kMargin = 8;

class PowerToggle : public juce::ToggleButton
{
public:
    explicit PowerToggle(const juce::String& text = {})
    {
        setButtonText(text);
        setClickingTogglesState(true);
        setTooltip(text);
    }

    void paint(juce::Graphics& g) override
    {
        const bool on = getToggleState();
        const auto r = getLocalBounds().toFloat();

        if (getButtonText().isNotEmpty())
        {
            const float corner = r.getHeight() * 0.5f;
            g.setColour(on ? kAccent.withAlpha(0.13f) : kPanelHi);
            g.fillRoundedRectangle(r, corner);
            g.setColour(on ? kAccent.withAlpha(0.75f) : kBorder);
            g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);

            const float d = 5.0f;
            const juce::Rectangle<float> led(r.getX() + 7.0f, r.getCentreY() - d * 0.5f, d, d);
            if (on)
            {
                g.setColour(kAccent.withAlpha(0.3f));
                g.fillEllipse(led.expanded(2.5f));
            }
            g.setColour(on ? kAccent : kTextDim.withAlpha(0.45f));
            g.fillEllipse(led);

            g.setColour(on ? kText : kTextDim);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText(getButtonText(), r.withTrimmedLeft(16.0f).withTrimmedRight(4.0f),
                       juce::Justification::centredLeft, false);
            return;
        }

        const float d = juce::jmin(r.getWidth(), r.getHeight()) - 3.0f;
        const juce::Rectangle<float> led(r.getCentreX() - d * 0.5f, r.getCentreY() - d * 0.5f, d, d);
        if (on)
        {
            g.setColour(kAccent.withAlpha(0.22f));
            g.fillEllipse(led.expanded(3.0f));
        }
        g.setColour(on ? kAccent : kBorder);
        g.drawEllipse(led, 1.5f);
        g.setColour(on ? kAccent : kPanelHi);
        g.fillEllipse(led.reduced(2.5f));
    }
};

} // namespace ui
} // namespace agm
