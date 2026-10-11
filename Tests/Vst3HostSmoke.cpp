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
            if (!std::isfinite(buffer.getSample(ch, i)) || std::abs(buffer.getSample(ch, i)) > 1.0e8f)
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

bool isMidiControllerTransport(const juce::AudioProcessorParameter& parameter)
{
    // JUCE adds 2080 non-automatable MIDI CC transport fields to its VST3
    // controller. They carry performance messages, live outside APVTS, and are
    // not serialized by the component's state or controller's setComponentState.
    return !parameter.isAutomatable() && parameter.getName(128).startsWith("MIDI CC ");
}

bool matchesSavedState(juce::AudioPluginInstance& plugin, const std::vector<float>& saved, int program)
{
    bool matches = saved.size() == (size_t)plugin.getParameters().size();
    if (plugin.getCurrentProgram() != program)
    {
        std::printf("State program mismatch: expected=%d actual=%d\n", program, plugin.getCurrentProgram());
        matches = false;
    }
    int mismatches = 0, persistentParameters = 0;
    for (size_t i = 0; i < saved.size() && i < (size_t)plugin.getParameters().size(); ++i)
    {
        auto* parameter = plugin.getParameters()[(int)i];
        if (isMidiControllerTransport(*parameter)) continue;
        ++persistentParameters;
        const float actual = parameter->getValue();
        if (!(std::abs(saved[i] - actual) < 1.0e-5f))
        {
            if (mismatches < 4)
            {
                const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*>(parameter);
                std::printf("State parameter mismatch: %s id=%s expected=%.9g actual=%.9g automatable=%d\n",
                            parameter->getName(128).toRawUTF8(), hosted != nullptr ? hosted->getParameterID().toRawUTF8() : "unknown",
                            saved[i], actual, parameter->isAutomatable() ? 1 : 0);
            }
            ++mismatches;
            matches = false;
        }
    }
    if (mismatches != 0) std::printf("State mismatches: %d of %zu exposed parameters\n", mismatches, saved.size());
    // 58 MixAgent controls plus JUCE's serialized bypass and factory program.
    return matches && persistentParameters == 60;
}

bool perturbEveryParameter(juce::AudioPluginInstance& plugin, const std::vector<float>& saved,
                          juce::AudioBuffer<float>& buffer)
{
    const int originalProgram = plugin.getCurrentProgram();
    if (plugin.getNumPrograms() > 1)
    {
        plugin.setCurrentProgram((plugin.getCurrentProgram() + 1) % plugin.getNumPrograms());
        processSilence(plugin, buffer, 16);
    }
    int expectedChanges = 0;
    for (int i = 0; i < plugin.getParameters().size(); ++i)
    {
        auto* parameter = plugin.getParameters()[i];
        if (parameter->getName(128) == "Program" || isMidiControllerTransport(*parameter)) continue;
        parameter->setValueNotifyingHost(saved[(size_t)i] < 0.5f ? 1.0f : 0.0f);
        ++expectedChanges;
    }
    processSilence(plugin, buffer, 16);
    int changes = 0;
    for (int i = 0; i < plugin.getParameters().size(); ++i)
        if (plugin.getParameters()[i]->getName(128) != "Program"
            && !isMidiControllerTransport(*plugin.getParameters()[i])
            && std::abs(saved[(size_t)i] - plugin.getParameters()[i]->getValue()) > 0.1f) ++changes;
    return expectedChanges == 59 && changes == expectedChanges
        && plugin.getCurrentProgram() != originalProgram;
}

void stressHostedLifecycle(juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description)
{
    // VST3 setupProcessing declares a hard maximum, unlike an AudioProcessor's
    // estimated block-size hint. JUCE's wrapper allocates its mapping scratch
    // from this maximum, so every submitted block must fit the declared bound.
    constexpr int maximumBlockSize = 4097;
    bool created = true, finiteAudio = true, recalled = true, mutated = true, stableLatency = true, editorsValid = true;
    int instances = 0, callbacks = 0, stateRecalls = 0, editorCycles = 0;
    for (int cycle = 0; cycle < 24; ++cycle)
    {
        const double rate = cycle % 3 == 0 ? 44100.0 : cycle % 3 == 1 ? 48000.0 : 96000.0;
        juce::String error;
        auto instance = formats.createPluginInstance(description, rate, maximumBlockSize, error);
        if (instance == nullptr) { std::printf("Lifecycle error: %s\n", error.toRawUTF8()); created = false; break; }
        auto layout = instance->getBusesLayout();
        if (layout.inputBuses.size() > 0) layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
        if (layout.outputBuses.size() > 0) layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
        created &= instance->setBusesLayout(layout);
        instance->setRateAndBufferSizeDetails(rate, maximumBlockSize);
        instance->prepareToPlay(rate, maximumBlockSize);
        ++instances;
        const int initialLatency = instance->getLatencySamples();
        juce::Random random(0x765400 + cycle);
        for (int block = 0; block < 20; ++block)
        {
            for (auto* parameter : instance->getParameters())
            {
                // Program selection replaces other parameters, so exercise it separately.
                if (parameter->getName(128) == "Program" || isMidiControllerTransport(*parameter)) continue;
                float value = random.nextFloat();
                if (parameter->isDiscrete() && parameter->getNumSteps() > 1)
                    value = (float)juce::roundToInt(value * (parameter->getNumSteps() - 1))
                          / (float)(parameter->getNumSteps() - 1);
                parameter->setValueNotifyingHost(value);
            }
            const int size = block % 4 == 0 ? 1 : block % 4 == 1 ? 17 : block % 4 == 2 ? 513 : 4097;
            juce::AudioBuffer<float> buffer(2, size);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < size; ++i) buffer.setSample(ch, i, (random.nextFloat() - 0.5f) * 0.2f);
            juce::MidiBuffer midi;
            for (int event = 0; event < 64; ++event)
            {
                const int position = event * size / 64;
                const int channel = event % 16 + 1, note = 24 + (event * 5 + cycle) % 96;
                midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.75f), position);
                midi.addEvent(juce::MidiMessage::noteOff(channel, note), std::min(size - 1, position + 1));
            }
            instance->processBlock(buffer, midi);
            ++callbacks;
            finiteAudio &= isFinite(buffer);
            stableLatency &= instance->getLatencySamples() == initialLatency;
        }
        // Save deliberately distinct values, mutate, and verify real VST3 state restoration.
        setParameter(*instance, "Input", 0.625f); setParameter(*instance, "P1 Gain", 0.7333333f);
        // The host caches requested normalized automation; continuous JUCE
        // floats can snap to a 0.01 physical interval inside the processor.
        // Commit legal values through the public host text API before taking
        // the strict snapshot, while earlier callbacks still stress arbitrary
        // normalized automation. No recall is used to construct this fixture.
        for (auto* parameter : instance->getParameters())
            if (parameter->getName(128) != "Program" && !isMidiControllerTransport(*parameter))
                parameter->setValueNotifyingHost(parameter->getValueForText(parameter->getText(parameter->getValue(), 128)));
        juce::AudioBuffer<float> settling(2, 256); processSilence(*instance, settling, 16);
        juce::MemoryBlock state; instance->getStateInformation(state);
        const int savedProgram = instance->getCurrentProgram();
        std::vector<float> saved;
        for (auto* parameter : instance->getParameters()) saved.push_back(parameter->getValue());
        mutated &= perturbEveryParameter(*instance, saved, settling);
        instance->setStateInformation(state.getData(), (int)state.getSize());
        ++stateRecalls;
        recalled &= state.getSize() > 0 && matchesSavedState(*instance, saved, savedProgram);
        if (cycle % 4 == 0)
        {
            ++editorCycles;
            std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
            editorsValid &= editor != nullptr && editor->getWidth() >= 100 && editor->getHeight() >= 100;
            editor.reset(); editorsValid &= instance->getActiveEditor() == nullptr;
        }
        instance->releaseResources();
    }
    check(created && instances == 24, "24 fresh actual VST3 instances negotiate and prepare their buses");
    check(finiteAudio && callbacks == 480, "480 actual wrapper callbacks survive dense MIDI, automation and 4097-sample blocks");
    check(stableLatency && callbacks == 480, "actual wrapper latency stays fixed throughout parameter automation");
    check(mutated && stateRecalls == 24, "all 59 persistent non-program parameters change before each state load");
    check(recalled && stateRecalls == 24, "24 actual wrapper state recalls restore all 60 persistent fields and the factory program");
    check(editorsValid && editorCycles == 6, "fresh actual wrapper editor lifetimes close before instance destruction");
}
}

int main(int argc, char** argv)
{
    // CI guards capture this process through a pipe. Preserve completed checks
    // when a wrapper or host crashes, rather than losing buffered diagnostics.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    int persistentParameters = 0;
    for (auto* parameter : plugin->getParameters())
        if (!isMidiControllerTransport(*parameter)) ++persistentParameters;
    check(persistentParameters == 60 && plugin->getNumPrograms() == 12,
          "wrapper exposes all 58 MixAgent controls, serialized bypass and 12 factory programs");

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
    check(latency == 205, "actual wrapper preserves the 205-sample latency at 48 kHz");

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

    juce::MidiBuffer layered;
    layered.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    layered.addEvent(juce::MidiMessage::noteOn(2, 60, 0.8f), 0);
    buffer.clear(); plugin->processBlock(buffer, layered);
    layered.clear(); layered.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear(); plugin->processBlock(buffer, layered); processSilence(*plugin, buffer, 400);
    check(isFinite(buffer) && buffer.getMagnitude(0, blockSize) > 1.0e-4f,
          "actual MIDI bus owns equal-pitch melodic notes separately by channel");
    layered.clear(); layered.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    buffer.clear(); plugin->processBlock(buffer, layered); processSilence(*plugin, buffer, 8);
    check(buffer.getMagnitude(0, blockSize) > 1.0e-4f,
          "actual MIDI channel panic preserves a different channel's held voice");
    layered.clear(); layered.addEvent(juce::MidiMessage::allSoundOff(2), 0);
    buffer.clear(); plugin->processBlock(buffer, layered); processSilence(*plugin, buffer, 8);
    check(buffer.getMagnitude(0, blockSize) < 1.0e-6f,
          "actual MIDI channel panic clears its remaining held voice");

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
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    const int savedProgram = plugin->getCurrentProgram();
    std::vector<float> savedParameters;
    for (auto* parameter : plugin->getParameters()) savedParameters.push_back(parameter->getValue());
    check(state.getSize() > 0, "compiled wrapper serializes nonempty state");
    check(perturbEveryParameter(*plugin, savedParameters, buffer),
          "all 59 persistent non-program parameters and the program change before primary state restoration");
    plugin->setStateInformation(state.getData(), (int)state.getSize());
    const bool stateMatches = matchesSavedState(*plugin, savedParameters, savedProgram);
    check(stateMatches, "compiled wrapper state restores all 60 persistent fields and the factory program");
    processSilence(*plugin, buffer, 20);
    check(isFinite(buffer), "wrapper processes after state restoration");

    setParameter(*plugin, "EQ", 1.0f); setParameter(*plugin, "Img Mono", 0.0f);
    processSilence(*plugin, buffer, 2);
    juce::MemoryBlock boolState; plugin->getStateInformation(boolState);
    auto* eq = findParameter(*plugin, "EQ");
    auto* mono = findParameter(*plugin, "Img Mono");
    setParameter(*plugin, "EQ", 0.75f); setParameter(*plugin, "Img Mono", 0.25f);
    processSilence(*plugin, buffer, 2);
    check(eq != nullptr && mono != nullptr && eq->getValue() == 0.75f && mono->getValue() == 0.25f,
          "actual wrapper retains same-side fractional boolean automation before recall");
    plugin->setStateInformation(boolState.getData(), (int)boolState.getSize());
    check(eq != nullptr && mono != nullptr && eq->getValue() == 1.0f && mono->getValue() == 0.0f,
          "actual wrapper state restores exact boolean endpoints without text canonicalization");

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
    // Keep the original instance alive while cycling fresh instances: the same
    // module remains loaded, as it does when a DAW opens several track instances.
    stressHostedLifecycle(formats, *descriptions[0]);
    plugin->releaseResources();
    plugin.reset();
    check(true, "compiled VST3 instance survives explicit release and destruction");
    std::printf("Host checks: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
