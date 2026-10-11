#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace
{
int failures = 0, passes = 0;
thread_local bool countAllocations = false;
thread_local size_t allocations = 0;

void check(bool result, const char* name)
{
    std::printf("[%s] %s\n", result ? "PASS" : "FAIL", name);
    result ? ++passes : ++failures;
}

struct Harness
{
    MixAgentAudioProcessor processor;
    void raw(const char* id, float value)
    {
        auto* parameter = processor.getAPVTS().getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    void dry()
    {
        for (const char* id : {"eq_enabled", "sat_enabled", "comp_enabled", "img_enabled",
                              "dly_enabled", "rvb_enabled", "lim_enabled"}) raw(id, 0.0f);
        raw("in_gain", 0.0f); raw("out_gain", 0.0f);
    }
    void prepare(double rate, int hint, int channels = 2)
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.set(0, layout.inputBuses[0]);
        processor.setBusesLayout(layout);
        processor.setRateAndBufferSizeDetails(rate, hint);
        processor.prepareToPlay(rate, hint);
    }
};

bool finite(const juce::AudioBuffer<float>& buffer, float bound = 1.0e6f)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sample = buffer.getSample(ch, i);
            if (!std::isfinite(sample) || std::abs(sample) > bound) return false;
        }
    return true;
}

void silence(MixAgentAudioProcessor& processor, juce::AudioBuffer<float>& buffer, int blocks)
{
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
}

uint32_t randomState = 0x34567890u;
float randomUnit()
{
    randomState = randomState * 1664525u + 1013904223u;
    return (float)(randomState >> 8) / 16777216.0f;
}

void fill(juce::AudioBuffer<float>& buffer, int offset)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, (float)((offset + i * (ch + 1)) % 97 - 48) * 0.003f);
}

void benchmark(const char* label)
{
    std::printf("BENCHMARK,%s,profile,block,p50_us,p95_us,max_us,mean_ns_per_frame,audio_hash\n", label);
    for (const char* profile : {"dry", "all_modules", "organ_24", "rage_24"})
        for (int size : {32, 128, 512, 4097})
        {
            Harness h;
            h.dry();
            if (std::strcmp(profile, "all_modules") == 0)
                for (const char* id : {"eq_enabled", "sat_enabled", "comp_enabled", "img_enabled",
                                      "dly_enabled", "rvb_enabled", "lim_enabled"}) h.raw(id, 1.0f);
            const bool instrument = std::strstr(profile, "_24") != nullptr;
            if (instrument) h.raw("inst_program", std::strcmp(profile, "organ_24") == 0 ? 8.0f : 15.0f);
            h.prepare(48000.0, 64);
            juce::AudioBuffer<float> buffer(2, size);
            juce::MidiBuffer midi;
            if (instrument)
                for (int note = 48; note < 72; ++note) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.5f), 0);
            std::array<double, 128> durations {};
            uint64_t hash = 1469598103934665603ull;
            double total = 0.0;
            for (int block = 0; block < 160; ++block)
            {
                fill(buffer, block * size);
                const auto start = std::chrono::steady_clock::now();
                h.processor.processBlock(buffer, midi);
                const auto stop = std::chrono::steady_clock::now();
                midi.clear();
                if (block < 32) continue;
                const double micros = std::chrono::duration<double, std::micro>(stop - start).count();
                durations[(size_t)block - 32] = micros;
                total += micros;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < size; ++i)
                    {
                        const int32_t quantized = (int32_t)std::llround(buffer.getSample(ch, i) * 1000000.0);
                        hash ^= (uint32_t)quantized;
                        hash *= 1099511628211ull;
                    }
            }
            std::sort(durations.begin(), durations.end());
            std::printf("BENCHMARK,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%016llx\n", label, profile, size,
                        durations[64], durations[121], durations.back(), total * 1000.0 / (128.0 * size),
                        (unsigned long long)hash);
            h.processor.releaseResources();
        }
}

void compressorOracles()
{
    agm::Compressor compressor;
    compressor.setEnabled(true); compressor.setThresholdDb(-20.0f); compressor.setKneeDb(0.0f);
    compressor.setRatio(4.0f); compressor.setAttackMs(10.0f); compressor.setReleaseMs(150.0f);
    compressor.setMix(1.0f); compressor.setMakeupDb(0.0f); compressor.prepare(48000.0, 512);
    juce::AudioBuffer<float> buffer(1, 1); buffer.setSample(0, 0, 0.8f);
    compressor.process(buffer);
    check(buffer.getSample(0, 0) > 0.79f, "10 ms compressor attack preserves the first sample of a level step");
    buffer.setSize(1, 24000); juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.8f, 24000);
    compressor.process(buffer);
    check(buffer.getSample(0, 23999) < 0.25f, "compressor develops sustained gain reduction after the attack");
    buffer.setSize(1, 28800); juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.005f, 28800);
    compressor.process(buffer);
    check(buffer.getSample(0, 0) < 0.0035f && buffer.getSample(0, 4800) < 0.0040f
          && buffer.getSample(0, 28799) > 0.0045f,
          "150 ms compressor release recovers progressively instead of in one sample");
    compressor.setRatio(1.0f); compressor.setMakeupDb(12.0f); compressor.prepare(48000.0, 512);
    buffer.setSize(1, 512); juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.05f, 512);
    compressor.process(buffer);
    check(std::abs(buffer.getSample(0, 511) - 0.05f * juce::Decibels::decibelsToGain(12.0f)) < 1.0e-5f,
          "compressor makeup provides its specified positive gain at ratio 1");
}

void lifecycleAndAutomation()
{
    bool audioFinite = true, stateMatches = true, latencyStable = true;
    size_t callbackAllocations = 0;
    for (int cycle = 0; cycle < 36; ++cycle)
    {
        Harness h;
        const double rate = std::array<double, 3>{44100.0, 48000.0, 96000.0}[(size_t)cycle % 3];
        const int channels = cycle % 2 + 1;
        h.prepare(rate, 17, channels);
        const int initialLatency = h.processor.getLatencySamples();
        for (int block = 0; block < 36; ++block)
        {
            for (auto* parameter : h.processor.getParameters())
            {
                float value = randomUnit();
                if (parameter->isDiscrete() && parameter->getNumSteps() > 1)
                    value = (float)juce::roundToInt(value * (parameter->getNumSteps() - 1))
                          / (float)(parameter->getNumSteps() - 1);
                parameter->setValueNotifyingHost(value);
            }
            juce::AudioBuffer<float> buffer(channels, std::array<int, 6>{1,17,31,128,1024,4097}[(size_t)block % 6]);
            fill(buffer, block * 17);
            if (block % 9 == 0)
            {
                buffer.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
                if (buffer.getNumSamples() > 1) buffer.setSample(0, 1, std::numeric_limits<float>::infinity());
            }
            juce::MidiBuffer midi;
            midi.ensureSize(2048);
            if (block % 9 == 0)
            {
                std::array<uint8_t, 1024> payload {};
                midi.addEvent(juce::MidiMessage::createSysExMessage(payload.data(), (int)payload.size()), 0);
            }
            for (int event = 0; event < 32; ++event)
            {
                const int note = 24 + (event * 7 + block) % 96;
                const int channel = event % 16 + 1;
                midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.7f),
                              event * buffer.getNumSamples() / 32);
                midi.addEvent(juce::MidiMessage::noteOff(channel, note),
                              std::min(buffer.getNumSamples() - 1, event * buffer.getNumSamples() / 32 + 1));
            }
            allocations = 0; countAllocations = true;
            h.processor.processBlock(buffer, midi);
            countAllocations = false; callbackAllocations += allocations;
            audioFinite &= finite(buffer) && std::isfinite(h.processor.getInLevel(0))
                         && std::isfinite(h.processor.getOutLevel(0));
            latencyStable &= h.processor.getLatencySamples() == initialLatency;
            if (block % 12 == 0)
            {
                juce::MemoryBlock state; h.processor.getStateInformation(state);
                const int savedProgram = h.processor.getCurrentProgram();
                std::vector<float> values;
                for (auto* parameter : h.processor.getParameters()) values.push_back(parameter->getValue());
                h.processor.setCurrentProgram((cycle + block) % h.processor.getNumPrograms());
                h.processor.setStateInformation(state.getData(), (int)state.getSize());
                stateMatches &= h.processor.getCurrentProgram() == savedProgram;
                for (int i = 0; i < h.processor.getParameters().size(); ++i)
                    stateMatches &= std::abs(values[(size_t)i] - h.processor.getParameters()[i]->getValue()) < 1.0e-5f;
            }
        }
        h.processor.releaseResources();
        h.prepare(rate, 64, channels);
        h.dry(); h.raw("inst_enabled", 0.0f);
        juce::AudioBuffer<float> restarted(channels, 512);
        silence(h.processor, restarted, 64);
        audioFinite &= finite(restarted);
        h.processor.releaseResources();
    }
    check(audioFinite, "1296 automated callbacks across rates, mono/stereo and oversized blocks remain finite");
    check(stateMatches, "108 randomized full-parameter state recalls restore every value");
    check(latencyStable, "automation never changes latency within a prepared stream");
#if AGM_TEST_ALLOCATIONS
    check(callbackAllocations == 0, "dense MIDI, automation, analyzer and oversized processor callbacks allocate no heap memory");
    std::printf("Observed callback allocations: %zu\n", callbackAllocations);
#endif
}

void polyphonySnapshotsAndInvalidState()
{
    agm::InstrumentBank reference, switched;
    reference.setProgram(agm::InstrumentBank::RageLead); switched.setProgram(agm::InstrumentBank::RageLead);
    reference.prepare(48000.0, 17); switched.prepare(48000.0, 17);
    for (int note = 24; note < 120; ++note) { reference.noteOn(note, 0.8f); switched.noteOn(note, 0.8f); }
    juce::AudioBuffer<float> expected(2, 513), actual(2, 513);
    bool snapshotsMatch = true;
    for (int block = 0; block < 64; ++block)
    {
        expected.clear(); actual.clear(); switched.setProgram(block % agm::InstrumentBank::kCount);
        reference.renderAdd(expected, 2); switched.renderAdd(actual, 2);
        snapshotsMatch &= finite(actual, 32.0f);
        for (int ch = 0; ch < 2; ++ch)
            snapshotsMatch &= std::memcmp(expected.getReadPointer(ch), actual.getReadPointer(ch), 513 * sizeof(float)) == 0;
    }
    check(snapshotsMatch, "96-note voice stealing and 64 program switches preserve all active voice snapshots exactly");
    switched.allNotesOff();
    for (int block = 0; block < 256; ++block) { actual.clear(); switched.renderAdd(actual, 2); }
    check(!switched.isActive() && actual.getMagnitude(0, 513) < 1.0e-6f,
          "all-notes-off releases a saturated 24-voice bank without reviving stolen voices");

    Harness h; h.prepare(48000.0, 64); h.raw("eq_p1_gain", 7.0f); h.raw("inst_program", 13.0f);
    std::vector<float> saved;
    for (auto* parameter : h.processor.getParameters()) saved.push_back(parameter->getValue());
    const std::array<uint8_t, 128> invalid {0x31,0x22,0x13,0xFF,0x44,0x56,0x67,0x78};
    for (int length = 0; length <= (int)invalid.size(); ++length) h.processor.setStateInformation(invalid.data(), length);
    bool unchanged = true;
    for (int i = 0; i < h.processor.getParameters().size(); ++i)
        unchanged &= saved[(size_t)i] == h.processor.getParameters()[i]->getValue();
    check(unchanged, "129 malformed or truncated state blocks leave current parameters untouched");
    h.processor.releaseResources();
}

void midiAndUiRecovery()
{
    Harness h; h.dry(); h.raw("inst_program", 8.0f); h.prepare(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 512); buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(2, 60, 0.8f), 0);
    h.processor.processBlock(buffer, midi);
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear(); h.processor.processBlock(buffer, midi);
    silence(h.processor, buffer, 220);
    check(h.processor.getInstrumentActive() && buffer.getMagnitude(0, 512) > 1.0e-4f,
          "same pitch on different melodic channels releases independently");
    midi.clear(); midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buffer.clear(); h.processor.processBlock(buffer, midi); silence(h.processor, buffer, 8);
    check(h.processor.getInstrumentActive() && buffer.getMagnitude(0, 512) > 1.0e-4f,
          "channel-scoped all-notes-off and all-sound-off preserve other melodic channels");
    midi.clear(); midi.addEvent(juce::MidiMessage::allSoundOff(2), 0);
    buffer.clear(); h.processor.processBlock(buffer, midi); silence(h.processor, buffer, 8);
    check(!h.processor.getInstrumentActive() && buffer.getMagnitude(0, 512) < 1.0e-6f,
          "channel panic removes the final held melodic voice");
    bool recovered = true;
    for (int cycle = 0; cycle < 120; ++cycle)
    {
        for (int i = 0; i < 512; ++i)
        {
            h.processor.uiNoteOn(24 + i % 96, 0.75f);
            if (i % 2) h.processor.uiNoteOff(24 + i % 96);
        }
        silence(h.processor, buffer, 8);
        recovered &= !h.processor.getInstrumentActive() && finite(buffer) && buffer.getMagnitude(0, 512) < 1.0e-6f;
        h.processor.uiNoteOn(60, 0.8f); silence(h.processor, buffer, 8);
        recovered &= h.processor.getInstrumentActive() && buffer.getMagnitude(0, 512) > 1.0e-4f;
        h.processor.uiNoteOff(60); silence(h.processor, buffer, 220);
        recovered &= !h.processor.getInstrumentActive();
    }
    check(recovered, "120 overflowing UI queues panic safely and accept fresh note-on/off afterward");
    h.processor.releaseResources();
}

void concurrentControlTraffic()
{
    Harness h; h.dry(); h.raw("inst_program", 8.0f); h.prepare(48000.0, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    auto* trim = h.processor.getAPVTS().getParameter("in_gain");
    std::atomic<bool> finished { false };
    std::thread producer([&]
    {
        for (int i = 0; i < 50000; ++i)
        {
            const int note = 36 + (i / 2) % 48;
            if ((i & 1) == 0) h.processor.uiNoteOn(note, 0.6f);
            else h.processor.uiNoteOff(note);
            if (i % 127 == 0) trim->setValueNotifyingHost((float)(i % 101) / 100.0f);
        }
        finished.store(true, std::memory_order_release);
    });
    bool audioFinite = true;
    size_t callbackAllocations = 0;
    juce::MidiBuffer midi;
    for (int block = 0; block < 4000; ++block)
    {
        buffer.clear(); allocations = 0; countAllocations = true;
        h.processor.processBlock(buffer, midi);
        countAllocations = false; callbackAllocations += allocations;
        audioFinite &= finite(buffer);
    }
    producer.join();
    silence(h.processor, buffer, 4000);
    check(finished.load(std::memory_order_acquire) && audioFinite && !h.processor.getInstrumentActive(),
          "50000 concurrent UI events and trim automation drain without corruption or stuck voices");
#if AGM_TEST_ALLOCATIONS
    check(callbackAllocations == 0, "concurrent control traffic adds no audio-thread heap allocations");
#endif
    h.processor.releaseResources();
}

void extremeGainRecovery()
{
    Harness h; h.dry(); h.raw("in_gain", 24.0f); h.raw("out_gain", 24.0f);
    h.prepare(48000.0, 17);
    juce::AudioBuffer<float> buffer(2, 4097); juce::MidiBuffer midi;
    juce::FloatVectorOperations::fill(buffer.getWritePointer(0), std::numeric_limits<float>::max(), 4097);
    juce::FloatVectorOperations::fill(buffer.getWritePointer(1), -std::numeric_limits<float>::max(), 4097);
    h.processor.processBlock(buffer, midi);
    bool valid = finite(buffer) && std::isfinite(h.processor.getInLevel(0))
              && std::isfinite(h.processor.getOutLevel(0));
    std::array<float, 600> spectrum {}; h.processor.getAnalyzerSpectrum(spectrum.data(), (int)spectrum.size());
    for (float bin : spectrum) valid &= std::isfinite(bin);
    check(valid, "finite input overflow at maximum trims cannot poison audio, meters or analyzer");
    h.raw("in_gain", 0.0f); h.raw("out_gain", 0.0f); silence(h.processor, buffer, 32);
    fill(buffer, 123); h.processor.processBlock(buffer, midi);
    check(finite(buffer) && buffer.getMagnitude(0, buffer.getNumSamples()) > 0.01f,
          "processing recovers to audible finite output after extreme trim overflow");
}

void malformedStateEnvelope(bool safeBaseline)
{
    Harness h; h.processor.setCurrentProgram(3); h.dry(); h.raw("inst_enabled", 0.0f);
    h.processor.setFavorite(agm::InstrumentBank::Bell, true); h.prepare(48000.0, 64);
    juce::MemoryBlock validState; h.processor.getStateInformation(validState);
    auto validXml = juce::AudioProcessor::getXmlFromBinary(validState.getData(), (int)validState.getSize());
    if (validXml == nullptr) { check(false, "valid serialized XML is available for malformed-envelope testing"); return; }
    const auto original = juce::ValueTree::fromXml(*validXml);
    std::vector<float> sourceValues;
    for (auto* parameter : h.processor.getParameters()) sourceValues.push_back(parameter->getValue());
    std::vector<float> sentinel;
    auto configureSentinel = [&]
    {
        h.processor.setCurrentProgram(11);
        h.processor.setFavorite(agm::InstrumentBank::Bell, false);
        h.processor.setFavorite(agm::InstrumentBank::RageLead, true);
        sentinel.clear();
        for (int i = 0; i < h.processor.getParameters().size(); ++i)
        {
            auto* parameter = h.processor.getParameters()[i];
            parameter->setValueNotifyingHost(sourceValues[(size_t)i] < 0.5f ? 1.0f : 0.0f);
            sentinel.push_back(parameter->getValue());
        }
    };
    auto unchanged = [&]
    {
        bool same = h.processor.getCurrentProgram() == 11;
        for (int i = 0; i < h.processor.getParameters().size(); ++i)
            same &= h.processor.getParameters()[i]->getValue() == sentinel[(size_t)i];
        return same;
    };
    auto load = [&](const juce::ValueTree& state)
    {
        auto xml = state.createXml(); juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary(*xml, bytes);
        h.processor.setStateInformation(bytes.getData(), (int)bytes.getSize());
    };
    bool rejected = true;
    for (const char* text : {"nan", "inf", "-inf", "1e300", "garbage", "3 trailing", "9999",
                            "1e2147483648", "1e-2147483648", "1e99999999999", "1e-99999999999"})
        for (const char* id : {"in_gain", "out_gain", "eq_p1_freq", "comp_attack", "inst_program"})
        {
            if (safeBaseline && (juce::String(text).contains("2147483648")
                                 || juce::String(text).contains("99999999999"))) continue;
            configureSentinel(); auto state = original.createCopy();
            state.getChildWithProperty("id", id).setProperty("value", text, nullptr);
            load(state); rejected &= unchanged();
            if (juce::String(text) == "nan" && (juce::String(id) == "in_gain" || juce::String(id) == "out_gain"))
            {
                juce::AudioBuffer<float> probe(2, 64); silence(h.processor, probe, 1);
            }
            load(original);
        }
    for (const char* text : {"nan", "inf", "garbage", "1e300", "3.5", "-1", "9999"})
    {
        configureSentinel(); auto state = original.createCopy(); state.setProperty("factoryProgram", text, nullptr);
        load(state); rejected &= unchanged(); load(original);
    }
    configureSentinel(); auto duplicate = original.createCopy();
    duplicate.addChild(duplicate.getChildWithProperty("id", "in_gain").createCopy(), -1, nullptr);
    load(duplicate); rejected &= unchanged(); load(original);
    configureSentinel(); auto missingValue = original.createCopy();
    missingValue.getChildWithProperty("id", "in_gain").removeProperty("value", nullptr);
    load(missingValue); rejected &= unchanged(); load(original);
    check(rejected, safeBaseline ? "44 malformed valid XML envelopes leave a different full sentinel patch untouched"
                                : "64 malformed valid XML envelopes leave a different full sentinel patch untouched");
    bool rejectedPrefixes = true;
    for (int size = 0; size < (int)validState.getSize(); ++size)
    {
        configureSentinel(); h.processor.setStateInformation(validState.getData(), size);
        rejectedPrefixes &= unchanged();
    }
    configureSentinel(); auto forged = validState;
    uint32_t forgedLength = juce::ByteOrder::swapIfBigEndian((uint32_t)0x7fffffff);
    std::memcpy(static_cast<uint8_t*>(forged.getData()) + 4, &forgedLength, sizeof(forgedLength));
    h.processor.setStateInformation(forged.getData(), (int)forged.getSize()); rejectedPrefixes &= unchanged();
    configureSentinel(); auto trailing = validState; trailing.append("junk", 4);
    h.processor.setStateInformation(trailing.getData(), (int)trailing.getSize()); rejectedPrefixes &= unchanged();
    check(rejectedPrefixes, "every real serialized prefix, forged XML length and trailing payload rejects atomically");
    load(original);
    juce::AudioBuffer<float> buffer(2, 512); juce::MidiBuffer midi;
    bool recovered = true;
    for (int block = 0; block < 32; ++block)
    { fill(buffer, block); h.processor.processBlock(buffer, midi); recovered &= finite(buffer); }
    check(recovered && buffer.getMagnitude(0, 512) > 0.01f,
          "rejected nonfinite state cannot poison persistent trim smoothing or mute later audio");
    auto legacy = original.createCopy();
    for (const char* id : {"in_gain", "out_gain", "inst_program"}) legacy.removeChild(legacy.getChildWithProperty("id", id), nullptr);
    configureSentinel(); load(legacy);
    bool defaulted = h.processor.getCurrentProgram() == 3 && h.processor.isFavorite(agm::InstrumentBank::Bell)
                  && !h.processor.isFavorite(agm::InstrumentBank::RageLead) && h.processor.getFavoriteCount() == 1;
    for (const char* id : {"in_gain", "out_gain", "inst_program"})
    {
        auto* parameter = h.processor.getAPVTS().getParameter(id);
        defaulted &= parameter->getValue() == parameter->getDefaultValue();
    }
    check(defaulted, "older sessions fill absent trims/instrument selection from declared defaults and retain their program/favorites");
    auto future = original.createCopy(); future.setProperty("futureVersion", 42, nullptr);
    juce::ValueTree futureParameter("PARAM"); futureParameter.setProperty("id", "future_parameter", nullptr);
    futureParameter.setProperty("value", "unrecognized future data", nullptr); future.addChild(futureParameter, -1, nullptr);
    future.removeProperty("factoryProgram", nullptr); configureSentinel(); load(future);
    bool compatible = h.processor.getCurrentProgram() == 0 && h.processor.isFavorite(agm::InstrumentBank::Bell)
                   && !h.processor.isFavorite(agm::InstrumentBank::RageLead) && h.processor.getFavoriteCount() == 1
                   && (int)h.processor.getAPVTS().state.getProperty("futureVersion") == 42
                   && h.processor.getAPVTS().state.getChildWithProperty("id", "future_parameter").isValid();
    for (int i = 0; i < h.processor.getParameters().size(); ++i)
        compatible &= std::abs(sourceValues[(size_t)i] - h.processor.getParameters()[i]->getValue()) < 1.0e-5f;
    check(compatible, "omitted legacy program metadata and unknown future state fields remain compatible");
}

}

#if AGM_TEST_ALLOCATIONS
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t size) { if (countAllocations) ++allocations; return __real_malloc(size); }
extern "C" void* __wrap_calloc(size_t count, size_t size) { if (countAllocations) ++allocations; return __real_calloc(count, size); }
extern "C" void* __wrap_realloc(void* pointer, size_t size) { if (countAllocations) ++allocations; return __real_realloc(pointer, size); }
// libstdc++ allocates inside a shared library, beyond GNU --wrap's reach.
// Interpose C++ allocation too, using the real allocator to count exactly once.
void* operator new(size_t bytes)
{
    if (countAllocations) ++allocations;
    if (void* pointer = __real_malloc(bytes == 0 ? 1 : bytes)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { std::free(pointer); }
void* operator new(size_t bytes, std::align_val_t alignment)
{
    if (countAllocations) ++allocations;
    void* pointer = nullptr;
    if (posix_memalign(&pointer, (size_t)alignment, bytes == 0 ? 1 : bytes) == 0) return pointer;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { std::free(pointer); }
#endif

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::ScopedNoDenormals noDenormals;
    if (argc > 1 && juce::String(argv[1]) == "--benchmark")
    {
        benchmark(argc > 2 ? argv[2] : "current");
        return 0;
    }
#if AGM_TEST_ALLOCATIONS
    allocations = 0; countAllocations = true;
    void* probe = ::operator new(48);
    countAllocations = false; ::operator delete(probe);
    check(allocations == 1, "callback allocation counter detects a C++ heap allocation");
#endif
    compressorOracles(); lifecycleAndAutomation(); polyphonySnapshotsAndInvalidState();
    midiAndUiRecovery(); concurrentControlTraffic(); extremeGainRecovery();
    malformedStateEnvelope(argc > 1 && juce::String(argv[1]) == "--safe-baseline");
    std::printf("Stress checks: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
