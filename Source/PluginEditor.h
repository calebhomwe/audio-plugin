#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Style.h"
#include "UI/Knob.h"
#include "UI/Meter.h"
#include "UI/Spectrum.h"

class MixAgentAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit MixAgentAudioProcessorEditor(MixAgentAudioProcessor&);
    ~MixAgentAudioProcessorEditor() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    agm::ui::Knob* addKnob(const juce::String& id, const juce::String& label,
                           const juce::String& suffix = {}, int decimals = 1);
    juce::ToggleButton* addPower(const juce::String& id);
    juce::ToggleButton* addToggle(const juce::String& id, const juce::String& text);
    void styleToggle(juce::ToggleButton& t, const juce::String& text);
    void placeKnobs(float centreX, int y, const juce::Array<agm::ui::Knob*>& ks);
    void drawHeader(juce::Graphics& g, juce::Rectangle<int> r, const char* name);

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

    juce::OwnedArray<juce::ToggleButton> powerToggles;
    juce::OwnedArray<agm::ui::Knob> knobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satModeAttachment;

    juce::Rectangle<int> topBarRect, eqSection, modulesRow;
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
