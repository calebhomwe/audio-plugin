#pragma once
// Shared test scaffolding for the headless console test targets:
//   - an operator new counter so processBlock() can be proven allocation-free
//   - a check() reporter
//   - a Harness that owns a prepared processor and renders probe signals

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <random>
#include <ctime>

using namespace juce;

// ---------------------------------------------------------------------------
// Allocation counter: every operator new that happens while gCountAllocs is set
// is counted, so processBlock() can be proven allocation-free.
// thread_local: only the thread that calls processBlock() is audited, JUCE's
// background threads (timers, message thread) may allocate freely.
inline thread_local bool gCountAllocs = false;
inline std::atomic<long> gAllocCount { 0 };

#if defined(__linux__) || defined(__APPLE__)
 #include <execinfo.h>
 #include <unistd.h>
#endif

inline void* countedAlloc(std::size_t n)
{
    if (gCountAllocs)
    {
        if (gAllocCount.fetch_add(1, std::memory_order_relaxed) == 0)
        {
           #if defined(__linux__) || defined(__APPLE__)
            // First offending allocation: dump the call stack so it can be located.
            void* frames[24];
            const int depth = backtrace(frames, 24);
            const char hdr[] = "  [alloc in processBlock] backtrace:\n";
            (void)!write(2, hdr, sizeof(hdr) - 1);
            backtrace_symbols_fd(frames, depth, 2);
           #endif
        }
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n == 0 ? 1 : n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n == 0 ? 1 : n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ---------------------------------------------------------------------------
inline int gFailures = 0;
inline int gChecks = 0;

inline void check(bool ok, const char* name)
{
    ++gChecks;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!ok)
        gFailures++;
}

inline void check(bool ok, const std::string& name) { check(ok, name.c_str()); }

inline float peakOf(const AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        p = std::max(p, b.getMagnitude(ch, 0, b.getNumSamples()));
    return p;
}

inline float dbOf(float g) { return agm::gainToDb(std::max(g, 1e-9f)); }

// ---------------------------------------------------------------------------
struct Harness
{
    std::unique_ptr<MixAgentAudioProcessor> proc;
    double sr = 44100.0;
    int block = 512;

    explicit Harness(double sampleRate = 44100.0, int blockSize = 512) : proc(std::make_unique<MixAgentAudioProcessor>())
    {
        prepare(sampleRate, blockSize);
    }

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate;
        block = blockSize;
        proc->setRateAndBufferSizeDetails(sr, block);
        proc->prepareToPlay(sr, block);
    }

    AudioProcessorValueTreeState& apvts() { return proc->getAPVTS(); }

    float getRaw(const char* id)
    {
        auto* p = dynamic_cast<RangedAudioParameter*>(apvts().getParameter(id));
        return p != nullptr ? p->getNormalisableRange().convertFrom0to1(p->getValue()) : 0.0f;
    }

    void setRaw(const char* id, float raw)
    {
        if (auto* p = dynamic_cast<RangedAudioParameter*>(apvts().getParameter(id)))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(raw));
    }
    void setOn(const char* id, bool on) { setRaw(id, on ? 1.0f : 0.0f); }

    void disableAllModules()
    {
        setOn("eq_enabled", false); setOn("sat_enabled", false); setOn("comp_enabled", false);
        setOn("img_enabled", false); setOn("dly_enabled", false); setOn("rvb_enabled", false);
        setOn("lim_enabled", false); setRaw("in_gain", 0.0f); setRaw("out_gain", 0.0f);
    }

    void enableAllModulesModerate()
    {
        setOn("eq_enabled", true); setRaw("eq_p1_gain", 3.0f); setOn("eq_hp_enabled", true);
        setOn("sat_enabled", true); setRaw("sat_drive", 0.5f); setRaw("sat_mix", 0.7f);
        setOn("comp_enabled", true); setRaw("comp_thresh", -20.0f); setRaw("comp_ratio", 4.0f);
        setOn("img_enabled", true); setRaw("img_width", 130.0f);
        setOn("dly_enabled", true); setRaw("dly_mix", 0.3f); setRaw("dly_feedback", 0.5f);
        setOn("rvb_enabled", true); setRaw("rvb_mix", 0.3f);
        setOn("lim_enabled", true); setRaw("lim_ceiling", -1.0f);
    }

    // Process `total` samples of silence + optional MIDI at the first block, collecting output.
    void run(int total, std::vector<float>* outL, std::vector<float>* outR, MidiBuffer* firstBlockMidi = nullptr,
             const std::function<float(int)>& inputGen = {}, int blockOverride = 0, int channels = 2)
    {
        const int bs = blockOverride > 0 ? blockOverride : block;
        AudioBuffer<float> buf(channels, bs);
        int done = 0;
        bool first = true;
        while (done < total)
        {
            const int n = std::min(bs, total - done);
            buf.setSize(channels, n, false, false, true);
            buf.clear();
            if (inputGen)
                for (int i = 0; i < n; ++i)
                {
                    const float x = inputGen(done + i);
                    for (int ch = 0; ch < channels; ++ch)
                        buf.setSample(ch, i, x);
                }
            MidiBuffer midi;
            if (first && firstBlockMidi != nullptr)
                midi = *firstBlockMidi;
            first = false;
            proc->processBlock(buf, midi);
            for (int i = 0; i < n; ++i)
            {
                if (outL) outL->push_back(buf.getSample(0, i));
                if (outR) outR->push_back(buf.getSample(channels > 1 ? 1 : 0, i));
            }
            done += n;
        }
    }

    void runSilence(float secs) { run((int)(sr * secs), nullptr, nullptr); }

    void runMidi(const MidiBuffer& mb, float secs = 0.0f)
    {
        MidiBuffer copy(mb);
        run(std::max(block, (int)(sr * secs)), nullptr, nullptr, &copy);
    }

    void note(int n, bool on, float vel = 0.9f, float secsAfter = 0.0f)
    {
        MidiBuffer mb;
        mb.addEvent(on ? MidiMessage::noteOn(1, n, vel) : MidiMessage::noteOff(1, n), 0);
        runMidi(mb, secsAfter);
    }

    void runSine(float freq, float amp, float secs, float settleSecs, float& rmsOut, float& peakOut)
    {
        const int total = (int)(sr * secs);
        const int skip = (int)(sr * settleSecs);
        const double inc = 2.0 * MathConstants<double>::pi * freq / sr;
        std::vector<float> out;
        run(total, &out, nullptr, nullptr, [&](int i) { return amp * (float)std::sin(inc * i); });
        double sumSq = 0.0;
        float peak = 0.0f;
        int measured = 0;
        for (int i = skip; i < total; ++i)
        {
            sumSq += (double)out[(size_t)i] * out[(size_t)i];
            peak = std::max(peak, std::abs(out[(size_t)i]));
            measured++;
        }
        rmsOut = measured > 0 ? (float)std::sqrt(sumSq / measured) : 0.0f;
        peakOut = peak;
    }

    // Impulse -> index of the output peak (integer-sample latency measurement).
    int measureLatency()
    {
        runSilence(0.3f);
        std::vector<float> out;
        run((int)(sr * 0.2), &out, nullptr, nullptr, [](int i) { return i == 0 ? 0.5f : 0.0f; });
        int best = 0;
        for (int i = 1; i < (int)out.size(); ++i)
            if (std::abs(out[(size_t)i]) > std::abs(out[(size_t)best]))
                best = i;
        return best;
    }
};

inline float maxAbsDiff(const std::vector<float>& v, int from, int to)
{
    float m = 0.0f;
    from = std::max(from, 1);
    to = std::min(to, (int)v.size());
    for (int i = from; i < to; ++i)
        m = std::max(m, std::abs(v[(size_t)i] - v[(size_t)i - 1]));
    return m;
}

inline float rmsRange(const std::vector<float>& v, int from, int to)
{
    double s = 0.0;
    from = std::max(from, 0);
    to = std::min(to, (int)v.size());
    for (int i = from; i < to; ++i)
        s += (double)v[(size_t)i] * v[(size_t)i];
    return to > from ? (float)std::sqrt(s / (to - from)) : 0.0f;
}

inline float peakRange(const std::vector<float>& v, int from, int to)
{
    float m = 0.0f;
    from = std::max(from, 0);
    to = std::min(to, (int)v.size());
    for (int i = from; i < to; ++i)
        m = std::max(m, std::abs(v[(size_t)i]));
    return m;
}

inline bool exactlyZero(const std::vector<float>& v, int from, int to)
{
    to = std::min(to, (int)v.size());
    for (int i = std::max(from, 0); i < to; ++i)
        if (v[(size_t)i] != 0.0f)
            return false;
    return true;
}

inline float zeroCrossingFreq(const std::vector<float>& v, int from, int to, double sr)
{
    int crossings = 0;
    int firstIdx = -1, lastIdx = -1;
    for (int i = std::max(from, 1); i < std::min(to, (int)v.size()); ++i)
        if (v[(size_t)i - 1] < 0.0f && v[(size_t)i] >= 0.0f)
        {
            if (firstIdx < 0) firstIdx = i;
            lastIdx = i;
            ++crossings;
        }
    if (crossings < 2) return 0.0f;
    return (float)((crossings - 1) * sr / (double)(lastIdx - firstIdx));
}
