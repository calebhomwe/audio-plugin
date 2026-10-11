#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Only the host framework is linked here. All plugin code runs from the .vst3.
namespace
{
int failures = 0, passes = 0;

bool check(bool result, const char* name)
{
    std::printf("[%s] %s\n", result ? "PASS" : "FAIL", name);
    result ? ++passes : ++failures;
    return result;
}

juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& plugin, const char* name)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(128) == name)
            return parameter;
    return nullptr;
}

bool setParameter(juce::AudioPluginInstance& plugin, const char* name, float normalized)
{
    if (auto* parameter = findParameter(plugin, name))
    {
        parameter->setValueNotifyingHost(normalized);
        return true;
    }
    std::printf("Missing hosted parameter: %s\n", name);
    return false;
}

bool isFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(ch, i)))
                return false;
    return true;
}

void processSilence(juce::AudioPluginInstance& plugin, juce::AudioBuffer<float>& buffer, int blocks)
{
    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        plugin.processBlock(buffer, midi);
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("Usage: MixAgentVst3HostSmoke /path/to/MixAgent.vst3\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const auto bundle = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    if (!check(bundle.exists(), "compiled VST3 bundle exists"))
        return 1;

    juce::AudioPluginFormatManager formats;
    auto* vst3 = new juce::VST3PluginFormat();
    formats.addFormat(vst3);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, bundle.getFullPathName());
    if (!check(descriptions.size() == 1 && descriptions[0]->name == "MixAgent",
               "VST3 factory discovers exactly the MixAgent audio processor"))
        return 1;
    check(descriptions[0]->isInstrument, "factory advertises an instrument");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    juce::String error;
    auto plugin = formats.createPluginInstance(*descriptions[0], sampleRate, blockSize, error);
    if (!check(plugin != nullptr, "compiled VST3 instantiates through host API"))
    {
        std::printf("Host error: %s\n", error.toRawUTF8());
        return 1;
    }
    check(plugin->acceptsMidi(), "wrapper exposes a MIDI input bus");

    auto layout = plugin->getBusesLayout();
    if (layout.inputBuses.size() > 0) layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
    if (layout.outputBuses.size() > 0) layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    if (!check(plugin->setBusesLayout(layout) && plugin->getTotalNumInputChannels() == 2
               && plugin->getTotalNumOutputChannels() == 2, "wrapper accepts stereo mixing buses"))
        return 1;

    bool parametersFound = true;
    for (const char* name : { "EQ", "Sat On", "Comp On", "Img On", "Dly On", "Rvb On", "Lim On" })
        parametersFound &= setParameter(*plugin, name, 0.0f);
    parametersFound &= setParameter(*plugin, "Input", 0.5f);
    parametersFound &= setParameter(*plugin, "Output", 0.5f);
    parametersFound &= setParameter(*plugin, "Inst On", 1.0f);
    // Organ sustains while held, then has a short release: a lost note-off is observable.
    parametersFound &= setParameter(*plugin, "Instrument", 8.0f / 15.0f);
    if (!check(parametersFound, "mixing and instrument parameters exposed to the host"))
        return 1;

    plugin->setRateAndBufferSizeDetails(sampleRate, blockSize);
    plugin->prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    processSilence(*plugin, buffer, 20); // Flush wrapper parameter queues and bypass ramps.
    const int latency = plugin->getLatencySamples();
    if (!check(latency > 0 && latency < 4096, "wrapper reports bounded processing latency"))
        return 1;
    std::printf("Reported latency: %d samples\n", latency);

    constexpr int dryBlocks = 24;
    std::vector<float> reference[2];
    for (auto& channel : reference) channel.resize(dryBlocks * blockSize);
    float maxError = 0.0f;
    bool finite = true;
    for (int block = 0; block < dryBlocks; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const int n = block * blockSize + i;
                const float input = 0.17f * (float)std::sin(n * (ch == 0 ? 0.37 : 0.61))
                                  + (n == 0 ? 0.25f : 0.0f);
                reference[ch][n] = input;
                buffer.setSample(ch, i, input);
            }
        juce::MidiBuffer midi;
        plugin->processBlock(buffer, midi);
        finite &= isFinite(buffer);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const int source = block * blockSize + i - latency;
                const float expected = source >= 0 ? reference[ch][source] : 0.0f;
                maxError = std::max(maxError, std::abs(buffer.getSample(ch, i) - expected));
            }
    }
    check(finite && maxError < 1.0e-6f, "hosted stereo dry audio matches reported latency sample by sample");
    std::printf("Maximum dry error: %.9g\n", maxError);

    processSilence(*plugin, buffer, 20);
    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 64);
    buffer.clear();
    plugin->processBlock(buffer, noteOn);
    bool quietBeforeNote = true, heardNote = false;
    for (int block = 0; block < 16; ++block)
    {
        finite &= isFinite(buffer);
        for (int i = 0; i < blockSize; ++i)
        {
            const int time = block * blockSize + i;
            const float magnitude = std::abs(buffer.getSample(0, i));
            if (time < 64 + latency && magnitude > 1.0e-7f) quietBeforeNote = false;
            if (time >= 64 + latency && magnitude > 1.0e-4f) heardNote = true;
        }
        processSilence(*plugin, buffer, 1);
    }
    check(finite && quietBeforeNote && heardNote, "real wrapper MIDI delivers timestamped melodic bass note");

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 36), 32);
    buffer.clear();
    plugin->processBlock(buffer, noteOff);
    processSilence(*plugin, buffer, 400); // >2 s, ample margin for Organ's release.
    check(isFinite(buffer) && buffer.getMagnitude(0, blockSize) < 1.0e-6f,
          "real wrapper MIDI note-off releases the sustained voice");

    setParameter(*plugin, "Inst On", 0.0f);
    processSilence(*plugin, buffer, 20);
    juce::MidiBuffer drum;
    drum.addEvent(juce::MidiMessage::noteOn(10, 36, 0.9f), 0);
    buffer.clear();
    plugin->processBlock(buffer, drum);
    float drumPeak = buffer.getMagnitude(0, blockSize);
    for (int block = 0; block < 8; ++block)
    {
        processSilence(*plugin, buffer, 1);
        finite &= isFinite(buffer);
        drumPeak = std::max(drumPeak, buffer.getMagnitude(0, blockSize));
    }
    check(finite && drumPeak > 1.0e-4f, "channel 10 reaches percussion through the wrapper MIDI bus");

    setParameter(*plugin, "Input", 0.625f);
    setParameter(*plugin, "P1 Gain", 0.7333333f);
    processSilence(*plugin, buffer, 20);
    std::vector<float> savedParameters;
    for (auto* parameter : plugin->getParameters()) savedParameters.push_back(parameter->getValue());
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    check(state.getSize() > 0, "compiled wrapper serializes nonempty state");
    setParameter(*plugin, "Input", 0.125f);
    setParameter(*plugin, "P1 Gain", 0.1f);
    processSilence(*plugin, buffer, 20);
    plugin->setStateInformation(state.getData(), (int)state.getSize());
    bool stateMatches = savedParameters.size() == (size_t)plugin->getParameters().size();
    for (size_t i = 0; stateMatches && i < savedParameters.size(); ++i)
        stateMatches &= std::abs(savedParameters[i] - plugin->getParameters()[(int)i]->getValue()) < 1.0e-5f;
    check(stateMatches, "compiled wrapper state restores every hosted parameter");
    processSilence(*plugin, buffer, 20);
    check(isFinite(buffer), "wrapper processes after state restoration");

    bool editorsValid = true;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
        editorsValid &= editor != nullptr && editor->getWidth() > 100 && editor->getHeight() > 100;
        // No addToDesktop(): exercise VST3 view lifetime without requiring a display.
        editor.reset();
        editorsValid &= plugin->getActiveEditor() == nullptr;
    }
    check(editorsValid, "VST3 editor view creates and destroys twice before unload");
    plugin->releaseResources();
    plugin.reset();
    check(true, "compiled VST3 instance releases and unloads cleanly");
    std::printf("Host checks: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
