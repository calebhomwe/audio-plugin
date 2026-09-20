#include <JuceHeader.h>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

// Headless editor smoke test: constructs the editor inside a window, drives a
// resize and renders a snapshot. Needs a display (on Linux: xvfb-run ./EditorProbe).
// Catches the "resized() before members are built" crash class.
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
            std::cout << "KEEP_CHECK true\n";
        }
        else
        {
            std::cout << "NO CONTENT\n";
            return 1;
        }
    }
    std::cout << "EDITOR TEST OK\n";
    return 0;
}
