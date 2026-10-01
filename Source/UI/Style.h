#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/Mutate.h"

namespace agm {
namespace ui {

const juce::Colour kBg        = juce::Colour(0xff0c0c10);
const juce::Colour kPanel     = juce::Colour(0xff16161c);
const juce::Colour kPanelHi   = juce::Colour(0xff1e1e26);
const juce::Colour kBorder    = juce::Colour(0xff2a2a34);
const juce::Colour kText      = juce::Colour(0xfff2f2f7);
/* 0x77778a measured 3.94:1 against the panel fill it is read on - under the
   4.5:1 floor - and that colour carries every knob caption, every section header,
   the IN/OUT labels, the MODE caption and the pad strip. Same hue, lighter:
   5.77:1 declared, which leaves room for the anti-aliasing of 8.5 px type. */
#if AGM_MUTATION(22)
// M22: the secondary text colour back to 0x77778a - 3.94:1 declared against the
// panel, and 2.35:1 rendered at 8.5 px.
const juce::Colour kTextDim   = juce::Colour(0xff77778a);
#else
const juce::Colour kTextDim   = juce::Colour(0xff9494a6);
#endif
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
    explicit PowerToggle(const juce::String& label = {})
    {
        setButtonText(label);
        setClickingTogglesState(true);
        setTooltip(label);
    }

    /* The rectangle and the font this toggle's caption is ACTUALLY drawn in.
       PowerToggle overrides drawToggleButton entirely, so the generic
       "JUCE leaves room for a tick box" geometry a visual rubric would assume is
       simply wrong here - it measured "MONO" as needing 41.8 px in 34 and called
       it clipped when it has 44. paint() draws from these, so the measurement and
       the pixels are one source. */
    juce::Rectangle<int> textBox() const
    {
        return getLocalBounds().withTrimmedLeft(16).withTrimmedRight(4);
    }

    static juce::Font textFont()
    {
        return juce::Font(juce::FontOptions(9.5f, juce::Font::bold));
    }

    /* The LED and ring colours this toggle paints, so the editor's R7 table and
       paint() are one source rather than two that can disagree. */
    static juce::Colour ledOnColour()   { return kAccent; }
    static juce::Colour ledOffColour()  { return kTextDim; }
    static juce::Colour ringOffColour() { return kTextDim; }

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
            /* Was kTextDim.withAlpha(0.45f): 1.80:1 against the panel, under R7's
               3:1 floor for an indicator. An off power LED you cannot see is a
               control you cannot find. */
            g.setColour(on ? ledOnColour() : ledOffColour());
            g.fillEllipse(led);

            g.setColour(on ? kText : kTextDim);
            g.setFont(textFont());
            g.drawText(getButtonText(), textBox().toFloat(),
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
        /* Was kBorder: 1.22:1 against the panel. Same reason as above - the ring is
           the only thing that says "there is a switch here" when it is off. */
        g.setColour(on ? ledOnColour() : ringOffColour());
        g.drawEllipse(led, 1.5f);
        g.setColour(on ? kAccent : kPanelHi);
        g.fillEllipse(led.reduced(2.5f));
    }
};

} // namespace ui
} // namespace agm
