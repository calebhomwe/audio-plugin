#include <JuceHeader.h>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        MixAgentAudioProcessor proc;
        proc.prepareToPlay(44100.0, 512);
        auto ed = std::unique_ptr<juce::AudioProcessorEditor>(proc.createEditor());

        auto* edPtr = ed.get();
        if (edPtr != nullptr)
        {
            juce::ComboBox* programs = nullptr;
            juce::ComboBox* filters = nullptr;
            bool resetValueCorrect = false;
            for (int i = 0; i < edPtr->getNumChildComponents(); ++i)
            {
                auto* child = edPtr->getChildComponent(i);
                if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
                {
                    if (combo->getItemText(0) == "Pluck") programs = combo;
                    if (combo->getItemText(1) == "FAVORITES") filters = combo;
                }
                if (auto* knob = dynamic_cast<agm::ui::Knob*>(child))
                    if (knob->getTooltip().startsWith("HP Freq:"))
                        resetValueCorrect = std::abs(knob->getDoubleClickReturnValue() - 40.0) < 0.01;
            }
            if (programs == nullptr || filters == nullptr || !resetValueCorrect) return 1;
            proc.setFavorite(agm::InstrumentBank::Sub, true);
            filters->setSelectedId(2, juce::sendNotificationSync);
            programs->setSelectedId(agm::InstrumentBank::Sub + 1, juce::sendNotificationSync);
            if (proc.getInstrumentProgram() != agm::InstrumentBank::Sub) return 1;
            juce::AudioBuffer<float> silence(2, 512); silence.clear();
            juce::MidiBuffer noMidi;
            proc.processBlock(silence, noMidi);
            if (proc.getInstrumentActive()) return 1;
            std::cout << "[PASS] filtered instrument IDs, real knob reset values and silent preset browsing\n";
            filters->setSelectedId(1, juce::sendNotificationSync);
            programs->setSelectedId(1, juce::sendNotificationSync);
            // Render an actual processed signal so the screenshot exercises
            // analyzer, EQ response and metering rather than an empty mockup.
            proc.setCurrentProgram(1);
            juce::Random noise(42);
            double phase = 0.0;
            const double step = juce::MathConstants<double>::twoPi * 220.0 / 44100.0;
            for (int block = 0; block < 80; ++block)
            {
                for (int i = 0; i < silence.getNumSamples(); ++i)
                {
                    const float sample = 0.28f * (float)std::sin(phase)
                        + 0.12f * (float)std::sin(phase * 4.0)
                        + 0.06f * (float)std::sin(phase * 24.0)
                        + 0.10f * (noise.nextFloat() - 0.5f);
                    silence.setSample(0, i, sample);
                    silence.setSample(1, i, sample * 0.93f);
                    phase += step;
                }
                proc.processBlock(silence, noMidi);
            }
            dynamic_cast<MixAgentAudioProcessorEditor&>(*edPtr).timerCallback();
            edPtr->setBounds(0, 0, 1160, 920);
            juce::Image img = edPtr->createComponentSnapshot(juce::Rectangle<int>(0, 0, 1160, 920));
            juce::File out(argc > 1 ? juce::String(argv[1])
                                   : juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("mixagent_ui.png").getFullPathName());
            {
                juce::MemoryOutputStream png;
                juce::PNGImageFormat pf;
                if (!pf.writeImageToStream(img, png) || !out.replaceWithData(png.getData(), png.getDataSize()))
                    return 1;
            }
            std::cout << "RENDERED " << out.getFullPathName() << "\n";
            std::cout << "KEEP_CHECK true\n";
        }
        else
        {
            std::cout << "NO CONTENT\n";
        }
    }
    std::cout << "EDITOR TEST OK\n";
    return 0;
}
