#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace agm {
namespace ui {

const juce::Colour kBg        = juce::Colour(0xff0c0c10);
const juce::Colour kPanel     = juce::Colour(0xff16161c);
const juce::Colour kPanelHi   = juce::Colour(0xff1e1e26);
const juce::Colour kBorder    = juce::Colour(0xff2a2a34);
const juce::Colour kText      = juce::Colour(0xfff2f2f7);
const juce::Colour kTextDim   = juce::Colour(0xff77778a);
const juce::Colour kAccent    = juce::Colour(0xffff6b1a);
const juce::Colour kAccentHot = juce::Colour(0xffffa640);
const juce::Colour kAccentDim = juce::Colour(0x66ff6b1a);
const juce::Colour kGlow      = juce::Colour(0x33ff6b1a);
const juce::Colour kCyan      = juce::Colour(0xff35c4d8);
const juce::Colour kCyanDim   = juce::Colour(0x6635c4d8);
const juce::Colour kPadLit    = juce::Colour(0xff2f7dff);
const juce::Colour kMeterLo   = juce::Colour(0xff3fae5a);
const juce::Colour kMeterHi   = juce::Colour(0xffe0b32a);
const juce::Colour kMeterClip = juce::Colour(0xffe04a2a);

const int kMargin = 8;

inline juce::ColourGradient verticalFade(juce::Rectangle<float> r,
                                         juce::Colour top, juce::Colour bottom)
{
    return juce::ColourGradient(top, r.getX(), r.getY(),
                                bottom, r.getX(), r.getBottom(), false);
}

inline void panelBevel(juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setGradientFill(verticalFade(r, kPanelHi, kPanel));
    g.fillRoundedRectangle(r, corner);

    g.setColour(kBorder);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.fillRect(r.getX() + corner, r.getY() + 0.5f,
               juce::jmax(0.0f, r.getWidth() - corner * 2.0f), 1.0f);
}

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

            if (on)
            {
                g.setColour(kGlow);
                g.fillRoundedRectangle(r.expanded(2.5f), corner + 2);
            }

            panelBevel(g, r, corner);
            if (on)
            {
                g.setColour(kAccent.withAlpha(0.75f));
                g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
            }

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
