#include <JuceHeader.h>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"
#include <cstdio>

namespace
{
int failures = 0, passes = 0;
void check(bool result, const char* text)
{
    std::printf("[%s] %s\n", result ? "PASS" : "FAIL", text);
    result ? ++passes : ++failures;
}

juce::MouseEvent event(juce::Component& target, juce::Point<float> position, int modifiers,
                       juce::Point<float> down = {20.0f, 20.0f})
{
    return {juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(modifiers),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), down,
            juce::Time::getCurrentTime(), 1, position != down};
}

void silent(MixAgentAudioProcessor& processor, int blocks)
{
    juce::AudioBuffer<float> buffer(2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i) { buffer.clear(); processor.processBlock(buffer, midi); }
}

bool layoutFits(juce::AudioProcessorEditor& editor)
{
    juce::Array<agm::ui::Knob*> knobs;
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent(i);
        if (child->isVisible() && !editor.getLocalBounds().contains(child->getBounds()))
        { std::printf("Outside editor: %s\n", child->getComponentID().toRawUTF8()); return false; }
        if (auto* knob = dynamic_cast<agm::ui::Knob*>(child))
        {
            if (knob->getWidth() < 34 || knob->getHeight() < 60) return false;
            for (auto* other : knobs)
                if (other->getBounds().intersects(knob->getBounds()))
                { std::printf("Overlapping knobs: %s / %s\n", other->getComponentID().toRawUTF8(), knob->getComponentID().toRawUTF8()); return false; }
            knobs.add(knob);
        }
    }
    return knobs.size() >= 40;
}

bool render(juce::AudioProcessorEditor& editor, const juce::File& file)
{
    auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
    juce::MemoryOutputStream stream; juce::PNGImageFormat format;
    const bool result = format.writeImageToStream(image, stream) && file.replaceWithData(stream.getData(), stream.getDataSize());
    if (result) std::printf("RENDERED %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), editor.getWidth(), editor.getHeight());
    return result;
}

struct GestureListener : juce::AudioProcessorParameter::Listener
{
    int starts = 0, ends = 0, changes = 0;
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool starting) override { starting ? ++starts : ++ends; }
};
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    MixAgentAudioProcessor processor;
    processor.setRateAndBufferSizeDetails(44100.0, 512); processor.prepareToPlay(44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr) { check(false, "editor creates"); return 1; }
    auto& concrete = dynamic_cast<MixAgentAudioProcessorEditor&>(*editor);
    check(editor->getWidth() == 1160 && editor->getHeight() == 760, "compact default editor fits a 760-pixel workspace");
    juce::ComboBox* programs = nullptr; juce::ComboBox* filters = nullptr;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto* combo = dynamic_cast<juce::ComboBox*>(editor->getChildComponent(i)))
        {
            if (combo->getItemText(0) == "Pluck") programs = combo;
            if (combo->getItemText(1) == "FAVORITES") filters = combo;
        }
    if (programs == nullptr || filters == nullptr) { check(false, "instrument selectors exist"); return 1; }
    bool stablePrograms = true;
    for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i) processor.setFavorite(i, true);
    filters->setSelectedId(2, juce::sendNotificationSync);
    for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
    { programs->setSelectedId(i + 1, juce::sendNotificationSync); stablePrograms &= processor.getInstrumentProgram() == i; }
    silent(processor, 8);
    check(stablePrograms && !processor.getInstrumentActive(), "all filtered instrument IDs stay stable and browsing never injects notes");
    filters->setSelectedId(1, juce::sendNotificationSync);
    programs->setSelectedId(agm::InstrumentBank::Organ + 1, juce::sendNotificationSync);

    auto knob = [&](const char* id) { return dynamic_cast<agm::ui::Knob*>(editor->findChildWithID(id)); };
    auto* hp = knob("eq_hp_freq"); auto* lp = knob("eq_lp_freq"); auto* input = knob("in_gain");
    auto* ratio = knob("comp_ratio"); auto* mix = knob("comp_mix");
    if (hp == nullptr || lp == nullptr || input == nullptr || ratio == nullptr || mix == nullptr) return 1;
    check(std::abs(hp->getDoubleClickReturnValue() - 40.0) < 0.01, "double-click reset uses the parameter's real 40 Hz default");
    bool units = lp->commitValueText("12.5 kHz") && std::abs(lp->getValue() - 12500.0) < 0.01
              && input->commitValueText("-7.5 dB") && std::abs(input->getValue() + 7.5) < 0.001
              && ratio->commitValueText("4:1") && std::abs(ratio->getValue() - 4.0) < 0.001
              && mix->commitValueText("37%") && std::abs(mix->getValue() - 0.37) < 0.001;
    check(units, "typed values accept frequency, gain, ratio and percentage units correctly");
    const double saved = lp->getValue();
    check(!lp->commitValueText("nan") && !lp->commitValueText("hello") && !lp->commitValueText("12 dB")
          && !lp->commitValueText("1e2147483648") && !lp->commitValueText("1e-2147483648")
          && !lp->commitValueText("1e99999999999")
          && lp->getValue() == saved, "invalid and mismatched numeric entries leave the control unchanged");
    GestureListener gesture; auto* parameter = processor.getAPVTS().getParameter("in_gain"); parameter->addListener(&gesture);
    input->commitValueText("3 dB"); parameter->removeListener(&gesture);
    check(gesture.starts == 1 && gesture.ends == 1 && gesture.changes >= 1
          && std::abs(processor.getAPVTS().getRawParameterValue("in_gain")->load() - 3.0f) < 0.001f,
          "typed changes update the real parameter inside one complete host automation gesture");
    hp->setValue(300.0, juce::sendNotificationSync);
    hp->mouseDown(event(*hp, {20,20}, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    bool fine = hp->getMouseDragSensitivity() == 1600;
    hp->mouseDrag(event(*hp, {20,4}, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    const double fineDelta = std::abs(hp->getValue() - 300.0);
    hp->mouseUp(event(*hp, {20,20}, 0));
    hp->setValue(300.0, juce::sendNotificationSync);
    hp->mouseDown(event(*hp, {20,20}, juce::ModifierKeys::leftButtonModifier));
    fine &= hp->getMouseDragSensitivity() == 160;
    hp->mouseDrag(event(*hp, {20,4}, juce::ModifierKeys::leftButtonModifier));
    const double ordinaryDelta = std::abs(hp->getValue() - 300.0);
    hp->mouseUp(event(*hp, {20,20}, 0));
    check(fine && fineDelta > 0.0 && ordinaryDelta > fineDelta * 5.0,
          "real Shift-drag movement is finer and ordinary drag restores normal sensitivity");

    auto* keys = dynamic_cast<agm::ui::PadGrid*>(editor->findChildWithID("preview-keys"));
    auto* octave = dynamic_cast<juce::TextButton*>(editor->findChildWithID("octave-up"));
    auto* audition = dynamic_cast<juce::TextButton*>(editor->findChildWithID("audition"));
    auto* stop = dynamic_cast<juce::TextButton*>(editor->findChildWithID("stop-notes"));
    if (keys == nullptr || octave == nullptr || audition == nullptr || stop == nullptr) return 1;
    keys->mouseDown(event(*keys, {20,20}, juce::ModifierKeys::leftButtonModifier)); silent(processor, 8);
    const bool played = processor.getInstrumentActive();
    keys->mouseDrag(event(*keys, {110,20}, juce::ModifierKeys::leftButtonModifier));
    octave->onClick(); silent(processor, 220);
    check(played && !processor.getInstrumentActive(), "dragging across keys and changing octave releases every held preview note");
    audition->onClick(); audition->onClick(); silent(processor, 8);
    const bool previewed = processor.getInstrumentActive();
    for (int i = 0; i < 6; ++i) concrete.timerCallback();
    silent(processor, 220);
    check(previewed && !processor.getInstrumentActive(), "repeated auditions end through the bounded preview timer");
    juce::AudioBuffer<float> midiBuffer(2, 512); midiBuffer.clear(); juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(2, 60, 0.8f), 0); processor.processBlock(midiBuffer, midi);
    audition->onClick(); stop->onClick(); silent(processor, 16);
    check(!processor.getInstrumentActive(), "Stop Notes clears preview and incoming MIDI voices through the audio queue");

    bool responsive = true;
    for (int i = 0; i < 120; ++i)
    { editor->setSize(1080 + (i * 37) % 521, 720 + (i * 19) % 281); responsive &= layoutFits(*editor); }
    check(responsive, "120 editor resizes keep every control visible and all knobs separate");
    processor.setCurrentProgram(1);
    juce::Random noise(42); double phase = 0.0;
    const double step = juce::MathConstants<double>::twoPi * 220.0 / 44100.0;
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < midiBuffer.getNumSamples(); ++i)
        {
            const float sample = 0.28f * (float)std::sin(phase) + 0.12f * (float)std::sin(phase * 4.0)
                               + 0.06f * (float)std::sin(phase * 24.0) + 0.10f * (noise.nextFloat() - 0.5f);
            midiBuffer.setSample(0, i, sample); midiBuffer.setSample(1, i, sample * 0.93f); phase += step;
        }
        midi.clear(); processor.processBlock(midiBuffer, midi);
    }
    concrete.timerCallback();
    juce::File output(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("mixagent-ui.png").getFullPathName());
    editor->setSize(1160, 760); check(render(*editor, output), "fresh native default editor screenshot writes successfully");
    editor->setSize(1080, 720); check(render(*editor, output.getSiblingFile(output.getFileNameWithoutExtension() + "-compact.png")), "fresh native minimum editor screenshot writes successfully");
    editor->setSize(1600, 1000); check(render(*editor, output.getSiblingFile(output.getFileNameWithoutExtension() + "-large.png")), "fresh native maximum editor screenshot writes successfully");
    editor.reset();
    bool reopened = true;
    for (int i = 0; i < 16; ++i)
    { editor.reset(processor.createEditor()); reopened &= editor != nullptr && layoutFits(*editor); editor.reset(); }
    check(reopened, "16 editor open/close cycles retain working controls and valid layouts");
    processor.releaseResources();
    std::printf("Editor checks: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
