#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Style.h"
#include "UI/Knob.h"
#include "UI/Meter.h"
#include "UI/Spectrum.h"
#include "UI/PadGrid.h"

class MixAgentAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit MixAgentAudioProcessorEditor(MixAgentAudioProcessor&);
    ~MixAgentAudioProcessorEditor() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    /* ---------------------------------------------------------------------
       Measurement seams for the wave-6a visual rubric (Tests/VisualRubric.h).

       This editor paints eight section headers, a logo and a MODE caption
       itself, and it owns four meters that draw their own scales. A rig that
       only walks the component tree sees none of it, so a clipped header or a
       2:1 meter would be invisible to the very check that exists to catch it.
       paint() now draws FROM these accessors, so the measurement cannot drift
       away from the pixels.
       --------------------------------------------------------------------- */
    struct PaintedText
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        juce::Font font { juce::FontOptions {} };
        juce::Colour colour;
        juce::String panel;        /* which section it belongs to */
        /* How the text sits in its rectangle. R6 measures the contrast of the
           GLYPHS, so it has to look where they are: MODE is centred in a 134 px
           box, and measuring the left of that box found no glyphs at all and
           reported 1.00:1. */
        juce::Justification just { juce::Justification::centredLeft };
    };
    struct GraphicSample
    {
        juce::String what;
        juce::Colour fg, bg;       /* fg may carry alpha; it is composited over bg */
        bool surface = false;      /* a panel fill: nothing to read */
        bool structural = false;   /* a border, a bevel, an unfilled groove */
    };
    struct MeterRef
    {
        juce::String what;
        const agm::ui::Meter* meter;
    };
    juce::Array<PaintedText>   paintedTexts()   const;
    juce::Array<GraphicSample> graphicSamples() const;
    juce::Array<MeterRef>      meterRefs()      const;
    /* Which section a given child component sits in, so R2 can be measured per
       panel instead of across the whole window. */
    juce::String panelOf (const juce::Component& c) const;

private:
    agm::ui::Knob* addKnob(const juce::String& id, const juce::String& label,
                           const juce::String& suffix = {}, int decimals = 1,
                           float displayScale = 1.0f);
    juce::ToggleButton* addPower(const juce::String& id);
    juce::ToggleButton* addToggle(const juce::String& id, const juce::String& text);
    void placeKnobs(float centreX, int y, const juce::Array<agm::ui::Knob*>& ks, int w = 50, int h = 72);
    void updateProgramList();
    void onClickFav();
    bool fullyBuilt = false;

    MixAgentAudioProcessor& proc;

    juce::Label logoLabel, subLabel;
    juce::Label inLabel { {}, "IN" }, outLabel { {}, "OUT" };
    juce::ComboBox presetCombo;
    agm::ui::Meter inMeter { agm::ui::Meter::Kind::Level };
    agm::ui::Meter outMeter { agm::ui::Meter::Kind::Level };
    agm::ui::Spectrum spectrum;
    agm::ui::Meter compMeter { agm::ui::Meter::Kind::GainReduction };
    agm::ui::Meter limMeter { agm::ui::Meter::Kind::GainReduction };
    juce::ComboBox satModeCombo;
    agm::ui::PadGrid padGrid { [this](int note) { proc.uiNoteOn(note, 0.9f); },
                               [this](int note) { proc.uiNoteOff(note); },
                               [this] { return proc.getInstrumentActive(); } };
    juce::Label padLabel { {}, "DRUM / 808 - MIDI OR MOUSE" };
    juce::ComboBox instProgramCombo;
    juce::ComboBox instFilterCombo;
    juce::ToggleButton favToggle { "FAV" };
    agm::ui::Knob* instLevel = nullptr;
    agm::ui::Knob* drumLevel = nullptr;
    agm::ui::Knob* inTrim = nullptr;
    agm::ui::Knob* outTrim = nullptr;
    juce::ToggleButton* instPower = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> instProgramAttachment;
    int auditionTimerRemaining = 0;

    juce::OwnedArray<juce::ToggleButton> powerToggles;
    juce::OwnedArray<agm::ui::Knob> knobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satModeAttachment;

    juce::Rectangle<int> topBarRect, eqSection, modulesRow, drumRect;
    juce::Rectangle<int> panels[6];

    juce::ToggleButton* hpToggle = nullptr;
    juce::ToggleButton* lpToggle = nullptr;
    juce::ToggleButton* monoToggle = nullptr;
    agm::ui::Knob* hpKnob = nullptr;
    agm::ui::Knob* lpKnob = nullptr;
    agm::ui::Knob* lsfF = nullptr, * lsfG = nullptr, * hsfF = nullptr, * hsfG = nullptr;
    agm::ui::Knob* p1F = nullptr, * p1G = nullptr, * p1Q = nullptr;
    agm::ui::Knob* p2F = nullptr, * p2G = nullptr, * p2Q = nullptr;
    agm::ui::Knob* p3F = nullptr, * p3G = nullptr, * p3Q = nullptr;
    agm::ui::Knob* satDrive = nullptr, * satMix = nullptr, * satOut = nullptr;
    agm::ui::Knob* compT = nullptr, * compR = nullptr, * compA = nullptr;
    agm::ui::Knob* compRel = nullptr, * compK = nullptr, * compMix = nullptr, * compMake = nullptr;
    agm::ui::Knob* imgW = nullptr, * imgB = nullptr;
    agm::ui::Knob* dlyT = nullptr, * dlyF = nullptr, * dlyM = nullptr, * dlyD = nullptr, * dlyW = nullptr;
    agm::ui::Knob* rvbS = nullptr, * rvbDc = nullptr, * rvbD = nullptr, * rvbW = nullptr, * rvbM = nullptr, * rvbP = nullptr;
    agm::ui::Knob* limC = nullptr, * limA = nullptr, * limR = nullptr;
};
