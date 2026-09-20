#include <JuceHeader.h>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

// Headless editor smoke test: constructs the editor inside a window, drives a
// resize, renders a snapshot, then drives the program ComboBox and the knobs and
// checks that the attached parameters actually move (editor <-> APVTS mapping).
// Needs a display (on Linux: xvfb-run -a ./EditorProbe). Exit code 0 = all OK.
namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

template <typename T>
void collect(juce::Component& root, std::vector<T*>& out)
{
    for (int i = 0; i < root.getNumChildComponents(); ++i)
    {
        auto* c = root.getChildComponent(i);
        if (auto* t = dynamic_cast<T*>(c)) out.push_back(t);
        collect<T>(*c, out);
    }
}

float rawValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    return p != nullptr ? p->getNormalisableRange().convertFrom0to1(p->getValue()) : 0.0f;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        MixAgentAudioProcessor proc;
        proc.prepareToPlay(44100.0, 512);
        auto ed = std::unique_ptr<juce::AudioProcessorEditor>(proc.createEditor());

        juce::DocumentWindow dw("probe", juce::Colours::black,
                                juce::DocumentWindow::allButtons, false);
        dw.setContentOwned(ed.release(), false);
        dw.setUsingNativeTitleBar(false);
        dw.setBounds(0, 0, 1160, 920);
        dw.setVisible(true);

        auto* edPtr = dw.getContentComponent();
        if (edPtr == nullptr)
        {
            std::cout << "NO CONTENT\n";
            return 1;
        }

        edPtr->setBounds(0, 0, 1160, 920);
        edPtr->setBounds(0, 0, 900, 700);
        edPtr->setBounds(0, 0, 1160, 920);
        juce::Image img = edPtr->createComponentSnapshot(juce::Rectangle<int>(0, 0, 1160, 920));
        juce::File out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mixagent_ui_probe.png");
        {
            juce::FileOutputStream fos(out);
            juce::PNGImageFormat pf;
            pf.writeImageToStream(img, fos);
        }
        std::cout << "RENDERED " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
        check(img.getWidth() == 1160 && img.getHeight() == 920, "editor renders a 1160x920 snapshot after resizes");

        auto& apvts = proc.getAPVTS();
        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };

        // ---- program ComboBox -> inst_program
        std::vector<juce::ComboBox*> combos;
        collect<juce::ComboBox>(*edPtr, combos);
        juce::ComboBox* programCombo = nullptr;
        for (auto* c : combos)
            for (int i = 0; i < c->getNumItems(); ++i)
                if (c->getItemText(i) == agm::InstrumentBank::programName((int)agm::InstrumentBank::Organ))
                    programCombo = c;
        check(programCombo != nullptr, "program ComboBox found");
        if (programCombo != nullptr)
        {
            const int organId = (int)agm::InstrumentBank::Organ + 1;
            programCombo->setSelectedId(organId, juce::sendNotificationSync);
            pump();
            check((int)rawValue(apvts, "inst_program") == (int)agm::InstrumentBank::Organ
                      && proc.getInstrumentProgram() == (int)agm::InstrumentBank::Organ,
                  "selecting 'Organ' in the ComboBox sets inst_program and the bank program");
            // and the other way round: parameter -> ComboBox
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("inst_program")))
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)agm::InstrumentBank::Bell));
            pump();
            check(programCombo->getSelectedId() == (int)agm::InstrumentBank::Bell + 1, "inst_program parameter change updates the ComboBox");
        }

        // ---- knobs (Sliders) -> parameters: move every slider to its maximum in turn;
        // each must move exactly one parameter, to that parameter's maximum
        std::vector<juce::Slider*> sliders;
        collect<juce::Slider>(*edPtr, sliders);
        check(!sliders.empty(), "knobs found");
        std::vector<juce::RangedAudioParameter*> params;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.push_back(rp);
        int mapped = 0, broken = 0;
        for (auto* s : sliders)
        {
            // park at the minimum first: several parameters (mix, width) default to their maximum
            s->setValue(s->getMinimum(), juce::sendNotificationSync);
            pump();
            std::vector<float> before;
            for (auto* rp : params) before.push_back(rp->getValue());
            s->setValue(s->getMaximum(), juce::sendNotificationSync);
            pump();
            int changed = 0; bool atMax = false;
            for (size_t i = 0; i < params.size(); ++i)
                if (std::abs(params[i]->getValue() - before[i]) > 1e-6f)
                {
                    ++changed;
                    atMax = std::abs(params[i]->getValue() - 1.0f) < 1e-4f;
                }
            if (changed == 1 && atMax) ++mapped;
            else if (changed != 0) ++broken;
        }
        std::cout << "  sliders: " << sliders.size() << ", mapped 1:1 to a parameter: " << mapped << ", ambiguous: " << broken << "\n";
        check(mapped >= 1 && broken == 0, "every knob that moves a parameter moves exactly that one parameter to its maximum");
        check(mapped == (int)sliders.size(), "every knob on the editor is attached to a parameter");

        // ---- toggles -> bool parameters
        std::vector<juce::ToggleButton*> toggles;
        collect<juce::ToggleButton>(*edPtr, toggles);
        int toggleMapped = 0;
        for (auto* t : toggles)
        {
            std::vector<float> before;
            for (auto* rp : params) before.push_back(rp->getValue());
            t->setToggleState(!t->getToggleState(), juce::sendNotificationSync);
            pump();
            int changed = 0;
            for (size_t i = 0; i < params.size(); ++i)
                if (std::abs(params[i]->getValue() - before[i]) > 1e-6f) ++changed;
            if (changed == 1) ++toggleMapped;
            t->setToggleState(!t->getToggleState(), juce::sendNotificationSync);
            pump();
        }
        std::cout << "  toggles: " << toggles.size() << ", attached to a parameter: " << toggleMapped << " (FAV is state, not a parameter)\n";
        check(toggleMapped >= (int)toggles.size() - 1, "all power/option toggles except FAV drive a parameter");
    }
    std::cout << (failures == 0 ? "EDITOR TEST OK" : "EDITOR TEST FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
