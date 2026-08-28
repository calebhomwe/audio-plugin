#pragma once
#include <JuceHeader.h>
#include "Style.h"

namespace agm { namespace ui {

// Mouse-triggerable drum pad grid. Each pad fires a MIDI note into the
// processor's UI note queue (drained on the audio thread). Pads light up
// while held (mouse down) and blink when the audio thread is active.
class PadGrid : public juce::Component
{
public:
    struct Pad
    {
        const char* label;
        int note;
        juce::Colour colour;
    };

    PadGrid(std::function<void(int)> onDown, std::function<void(int)> onUp,
            std::function<bool()> isAudioActive)
        : fireDown(std::move(onDown)), fireUp(std::move(onUp)), audioActive(std::move(isAudioActive))
    {
        setWantsKeyboardFocus(false);
    }

    void setPads(std::vector<Pad> p)
    {
        pads = std::move(p);
        heldPads.resize(pads.size(), false);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int idx = padAt(e.getPosition());
        if (idx >= 0)
        {
            heldPads[(size_t)idx] = true;
            fireDown(pads[idx].note);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const int idx = padAt(e.getPosition());
        if (idx >= 0)
        {
            heldPads[(size_t)idx] = false;
            fireUp(pads[idx].note);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        for (int i = 0; i < pads.size(); ++i)
            if (heldPads[(size_t)i]) { fireUp(pads[i].note); heldPads[(size_t)i] = false; }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (pads.empty())
        {
            g.setColour(agm::ui::kTextDim);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText("NO PADS", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int cols = pads.size() > 16 ? (int)std::sqrt((double)pads.size()) : (int)pads.size();
        const int rows = (pads.size() + cols - 1) / cols;
        const int gap = 6;
        const int w = (getWidth() - (cols + 1) * gap) / cols;
        const int h = (getHeight() - (rows + 1) * gap) / rows;

        for (int i = 0; i < pads.size(); ++i)
        {
            const int r = i / cols, c = i % cols;
            const juce::Rectangle<int> rct(gap + c * (w + gap), gap + r * (h + gap), w, h);

            const bool held = heldPads[(size_t)i];
            const bool audio = audioActive ? audioActive() : false;
            const bool lit = held || audio;
            const juce::Colour col = pads[i].colour;
            const juce::Rectangle<float> rf = rct.toFloat();
            const float cr = 7.0f;

            static const juce::Colour kBlueTop(0xff2f7dff), kBlueBot(0xff1f4fbf);

            juce::Colour topC, botC;
            if (col == kBlueTop) { topC = kBlueTop; botC = kBlueBot; }
            else                 { topC = col.brighter(0.18f); botC = col.darker(0.25f); }

            if (lit)
            {
                topC = topC.brighter(0.32f);
                botC = botC.darker(0.05f);
                g.setColour(col.withAlpha(0.35f));
                g.fillRoundedRectangle(rf.expanded(4.0f), cr + 3.0f);
            }
            else
            {
                topC = topC.darker(0.52f);
                botC = botC.darker(0.52f);
            }

            const juce::Colour edgeC = botC.darker(0.40f);

            g.setColour(edgeC);
            g.fillRoundedRectangle(rf, cr);
            const juce::Rectangle<float> face = rf.withTrimmedBottom(2.0f);
            g.setGradientFill(agm::ui::verticalFade(face, topC, botC));
            g.fillRoundedRectangle(face, cr);

            g.setColour(lit ? col.brighter(0.60f) : agm::ui::kBorder);
            g.drawRoundedRectangle(rf.reduced(0.5f), cr, 1.0f);

            g.setColour(juce::Colours::white.withAlpha(lit ? 0.26f : 0.13f));
            g.fillRect(rf.getX() + cr, rf.getY() + 1.5f,
                       juce::jmax(0.0f, rf.getWidth() - cr * 2.0f), 1.0f);

            int note = pads[i].note;
            if (note >= 24 && note <= 48)
            {
                const float f = 440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f);
                g.setColour(lit ? juce::Colours::white : agm::ui::kText);
                g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
                g.drawText(pads[i].label,
                           rf.withTrimmedBottom(juce::jmax(0.0f, rf.getHeight() - 20.0f)),
                           juce::Justification::centred);
                if (rct.getHeight() >= 36)
                {
                    g.setColour(agm::ui::kTextDim);
                    g.setFont(juce::Font(juce::FontOptions(9.0f)));
                    g.drawText(f >= 100.0f ? juce::String(f, 0) + " Hz" : juce::String(f, 1) + " Hz",
                               rf.withTop(rf.getBottom() - 16.0f), juce::Justification::centred);
                }
            }
            else
            {
                g.setColour(lit ? juce::Colours::white : agm::ui::kText);
                g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
                g.drawText(pads[i].label, rf, juce::Justification::centred);
            }
        }
    }

private:
    int padAt(juce::Point<int> pos) const
    {
        if (pads.empty() || getWidth() <= 0)
            return -1;
        const int cols = pads.size() > 16 ? (int)std::sqrt((double)pads.size()) : (int)pads.size();
        const int rows = (pads.size() + cols - 1) / cols;
        const int gap = 6;
        const int w = (getWidth() - (cols + 1) * gap) / cols;
        const int h = (getHeight() - (rows + 1) * gap) / rows;
        for (int i = 0; i < pads.size(); ++i)
        {
            const int r = i / cols, c = i % cols;
            const juce::Rectangle<int> rct(gap + c * (w + gap), gap + r * (h + gap), w, h);
            if (rct.contains(pos)) return i;
        }
        return -1;
    }

    std::function<void(int)> fireDown;
    std::function<void(int)> fireUp;
    std::function<bool()> audioActive;
    std::vector<Pad> pads;
    std::vector<bool> heldPads;
};

}} // namespace agm::ui