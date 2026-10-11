#include <juce_dsp/juce_dsp.h>
#include "../Source/DSP/Saturation.h"
#include "../Source/DSP/InstrumentBank.h"
#include "../Source/DSP/DrumEngine.h"
#include "../Source/DSP/Limiter.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
thread_local bool measureAllocations = false;
std::atomic<int> allocations { 0 };

void check(bool passed, const char* label)
{
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << label << '\n';
    if (!passed) ++failures;
}

bool finite(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(ch, i))) return false;
    return true;
}
}

// GNU linker wrapping also sees JUCE HeapBlock/AudioBuffer malloc calls,
// which overriding operator new alone would miss. Only processing is measured.
#if AGM_TEST_ALLOCATIONS
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t bytes)
{
    if (measureAllocations) ++allocations;
    return __real_malloc(bytes);
}
extern "C" void* __wrap_calloc(size_t count, size_t bytes)
{
    if (measureAllocations) ++allocations;
    return __real_calloc(count, bytes);
}
extern "C" void* __wrap_realloc(void* memory, size_t bytes)
{
    if (measureAllocations) ++allocations;
    return __real_realloc(memory, bytes);
}
#endif

int main()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const int channels : { 1, 2 })
        {
            agm::Saturation bypassed, zeroMix;
            bypassed.setEnabled(false);
            zeroMix.setEnabled(true);
            bypassed.setMix(1.0f);
            zeroMix.setMix(0.0f);
            bypassed.prepare(rate, 17);
            zeroMix.prepare(rate, 17);
            juce::AudioBuffer<float> a(channels, 1024), b(channels, 1024);
            a.clear(); b.clear();
            for (int ch = 0; ch < channels; ++ch)
            {
                a.setSample(ch, 0, 0.5f);
                b.setSample(ch, 0, 0.5f);
            }
            bypassed.process(a);
            zeroMix.process(b);
            float delta = 0.0f, peak = 0.0f;
            int peakPosition = 0;
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                delta = std::max(delta, std::abs(a.getSample(0, i) - b.getSample(0, i)));
                if (std::abs(a.getSample(0, i)) > peak)
                {
                    peak = std::abs(a.getSample(0, i));
                    peakPosition = i;
                }
            }
            check(delta < 1e-6f && peak > 0.1f, "saturation bypass and zero mix keep the same delayed dry path");
            check(std::abs(peakPosition - bypassed.getLatencySamples()) <= 1,
                  "saturation impulse delay matches reported integer latency");

            bypassed.reset();
            juce::AudioBuffer<float> exact(channels, 4097);
            std::vector<float> original(4097);
            for (int i = 0; i < exact.getNumSamples(); ++i)
            {
                original[(size_t)i] = 0.4f * (float)std::sin(i * 2.413);
                for (int ch = 0; ch < channels; ++ch) exact.setSample(ch, i, original[(size_t)i]);
            }
            bypassed.process(exact);
            bool unchanged = true;
            const int latency = bypassed.getLatencySamples();
            for (int i = 0; i < exact.getNumSamples(); ++i)
                for (int ch = 0; ch < channels; ++ch)
                    unchanged &= exact.getSample(ch, i) == (i < latency ? 0.0f : original[(size_t)(i - latency)]);
            check(unchanged, "saturation bypass preserves the exact delayed input at high frequencies");

            agm::Saturation wetPath;
            wetPath.setMode(2); wetPath.setDrive(0.0f); wetPath.setMix(1.0f);
            wetPath.prepare(rate, 17);
            exact.clear(); exact.setSample(0, 0, 0.001f);
            if (channels == 2) exact.setSample(1, 0, 0.001f);
            wetPath.process(exact);
            peak = 0.0f; peakPosition = 0;
            for (int i = 0; i < exact.getNumSamples(); ++i)
                if (std::abs(exact.getSample(0, i)) > peak)
                {
                    peak = std::abs(exact.getSample(0, i)); peakPosition = i;
                }
            check(peak > 0.0001f && std::abs(peakPosition - wetPath.getLatencySamples()) <= 1,
                  "saturation wet impulse aligns with the exact dry delay");

            agm::Saturation saturation;
            agm::InstrumentBank bank;
            agm::DrumEngine drums;
            saturation.setEnabled(true);
            saturation.prepare(rate, 17);
            bank.prepare(rate, 17);
            drums.prepare(rate, 17);
            bank.noteOn(60, 0.8f);
            drums.noteOn(36, 0.8f);
            juce::AudioBuffer<float> oversized(channels, 4097);
            oversized.clear();
            allocations.store(0);
            measureAllocations = true;
            bank.renderAdd(oversized, channels);
            drums.renderAdd(oversized, channels);
            saturation.process(oversized);
            measureAllocations = false;
#if AGM_TEST_ALLOCATIONS
            check(allocations.load() == 0, "4097-sample instrument, drum and saturation processing allocates no memory");
#endif
            check(finite(oversized), "oversized mono/stereo processing stays finite");
            saturation.reset();
            oversized.clear();
            oversized.setSample(0, 50, std::numeric_limits<float>::quiet_NaN());
            saturation.process(oversized);
            check(finite(oversized), "saturation contains non-finite input");
        }
    }

    agm::InstrumentBank bank;
    bank.prepare(48000.0, 32);
    bank.setProgram(agm::InstrumentBank::Organ);
    bank.noteOn(36, 0.9f);
    juce::AudioBuffer<float> segment(2, 512);
    segment.clear();
    bank.renderAdd(segment, 2, 257, 255);
    check(segment.getMagnitude(0, 257) == 0.0f && segment.getMagnitude(257, 255) > 0.0f,
          "instrument rendering respects sample-range boundaries");
    bank.setEnabled(false);
    bank.noteOn(60, 0.8f);
    bank.setEnabled(true);
    segment.clear();
    bank.renderAdd(segment, 2);
    check(!bank.isActive() && segment.getMagnitude(0, 512) == 0.0f,
          "disabled instrument rejects notes and never revives stale voices");

    // These six named programs used to fall through to one default recipe.
    // Compare their full attacks against the parent timbres and each other.
    std::vector<std::vector<float>> namedPrograms;
    const int parentPrograms[] = { agm::InstrumentBank::Bell, agm::InstrumentBank::Bell,
        agm::InstrumentBank::Pad, agm::InstrumentBank::EPiano,
        agm::InstrumentBank::Keys, agm::InstrumentBank::Lead };
    for (int program = agm::InstrumentBank::DarkBell; program < agm::InstrumentBank::kCount; ++program)
    {
        auto render = [](int selected)
        {
            agm::InstrumentBank instrument;
            instrument.prepare(48000.0, 64);
            instrument.setProgram(selected);
            instrument.noteOn(60, 0.8f);
            juce::AudioBuffer<float> buffer(2, 8192); buffer.clear();
            instrument.renderAdd(buffer, 2);
            return std::vector<float>(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
        };
        const auto sound = render(program);
        const auto parent = render(parentPrograms[program - agm::InstrumentBank::DarkBell]);
        float peak = 0.0f, difference = 0.0f;
        bool finiteSound = true, distinct = true;
        for (size_t i = 0; i < sound.size(); ++i)
        {
            peak = std::max(peak, std::abs(sound[i]));
            difference = std::max(difference, std::abs(sound[i] - parent[i]));
            finiteSound &= std::isfinite(sound[i]);
        }
        for (const auto& previous : namedPrograms)
        {
            float delta = 0.0f;
            for (size_t i = 0; i < sound.size(); ++i)
                delta = std::max(delta, std::abs(sound[i] - previous[i]));
            distinct &= delta > 0.001f;
        }
        namedPrograms.push_back(sound);
        check(finiteSound && peak > 0.005f && peak < 1.0f && difference > 0.001f && distinct,
              (std::string(agm::InstrumentBank::programName(program)) + " has a distinct finite recipe").c_str());
    }

    agm::Limiter limiter;
    limiter.setCeilingDb(-6.0f);
    limiter.prepare(48000.0, 32);
    juce::AudioBuffer<float> transient(2, 2048);
    transient.clear();
    transient.setSample(0, 0, 20.0f);
    transient.setSample(1, 0, -20.0f);
    limiter.process(transient);
    check(finite(transient) && transient.getMagnitude(0, 2048) <= agm::dbToGain(-6.0f) + 1e-6f,
          "limiter contains isolated oversized stereo transients");
    return failures == 0 ? 0 : 1;
}
