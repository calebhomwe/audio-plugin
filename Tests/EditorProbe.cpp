#include <JuceHeader.h>
#include <cstdlib>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

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
        if (edPtr != nullptr)
        {
            edPtr->setBounds(0, 0, 1160, 920);
            juce::Image img = edPtr->createComponentSnapshot(juce::Rectangle<int>(0, 0, 1160, 920));
            juce::File out = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("mixagent_ui_v2.png");
            if (const char* envPath = std::getenv("AGM_PROBE_PNG"); envPath != nullptr && envPath[0] != '\0')
                out = juce::File(juce::String::fromRawUTF8(envPath));
            {
                juce::FileOutputStream fos(out);
                juce::PNGImageFormat pf;
                pf.writeImageToStream(img, fos);
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
