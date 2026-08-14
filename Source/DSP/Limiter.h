#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <vector>
#include <atomic>

namespace agm {

class Limiter
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        (void)blockSize;
        sr = sampleRate;
        delaySamples = juce::roundToInt(0.003 * sampleRate);
        if (delaySamples < 1)
            delaySamples = 1;
        delayLines.assign(2, std::vector<float>(delaySamples, 0.0f));
        writePos.assign(2, 0);
        currentGain = 1.0f;
        grSmoother.store(0.0f, std::memory_order_relaxed);
        updateCoefs();
        bypass.prepare(sampleRate);
        bypass.setEnabled(enabled);
    }

    void reset()
    {
        for (auto& line : delayLines)
            std::fill(line.begin(), line.end(), 0.0f);
        std::fill(writePos.begin(), writePos.end(), 0);
        currentGain = 1.0f;
        grSmoother.store(0.0f, std::memory_order_relaxed);
        bypass.prepare(sr);
        bypass.setEnabled(enabled);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        if (delaySamples < 1)
            return;
        const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(delayLines.size()));
        const int numSamples = buffer.getNumSamples();
        if (numChannels < 1 || numSamples < 1)
            return;

        const float ceilingGain = dbToGain(ceilingDb);

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = juce::jmax(level, std::fabs(buffer.getReadPointer(ch)[i]));

            const float target = juce::jmin(1.0f, ceilingGain / juce::jmax(level, 1e-6f));
            currentGain += (target - currentGain) * ((target < currentGain) ? attackCoef : releaseCoef);

            const float mix = bypass.next();
            const float appliedGain = 1.0f + mix * (currentGain - 1.0f);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                std::vector<float>& line = delayLines[ch];
                int& wp = writePos[ch];
                const float in = buffer.getReadPointer(ch)[i];
                const float delayed = line[wp];
                line[wp] = in;
                if (++wp >= delaySamples)
                    wp = 0;
                buffer.getWritePointer(ch)[i] = delayed * appliedGain;
            }

            const float instGr = -gainToDb(currentGain);
            float g = grSmoother.load(std::memory_order_relaxed);
            g += (instGr - g) * grCoef;
            grSmoother.store(g, std::memory_order_relaxed);
        }
    }

    void setEnabled(bool on)
    {
        enabled = on;
        bypass.setEnabled(on);
    }

    void setCeilingDb(float db) { ceilingDb = juce::jlimit(-20.0f, 0.0f, db); }

    void setAttackMs(float ms)
    {
        attackMs = juce::jlimit(0.01f, 10.0f, ms);
        updateCoefs();
    }

    void setReleaseMs(float ms)
    {
        releaseMs = juce::jlimit(10.0f, 500.0f, ms);
        updateCoefs();
    }

    int getLatencySamples() const { return delaySamples; }

    float getGainReductionDb() const { return grSmoother.load(std::memory_order_relaxed); }

private:
    void updateCoefs()
    {
        const double fs = (sr > 0.0) ? sr : 44100.0;
        attackCoef = static_cast<float>(std::exp(-1.0 / (attackMs * 0.001 * fs)));
        releaseCoef = static_cast<float>(std::exp(-1.0 / (releaseMs * 0.001 * fs)));
        grCoef = static_cast<float>(std::exp(-1.0 / (50.0 * 0.001 * fs)));
    }

    std::vector<std::vector<float>> delayLines;
    std::vector<int> writePos;
    double sr = 44100.0;
    int delaySamples = 0;
    float ceilingDb = -1.0f;
    float attackMs = 1.0f;
    float releaseMs = 100.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
    float grCoef = 0.0f;
    float currentGain = 1.0f;
    std::atomic<float> grSmoother{ 0.0f };
    bool enabled = true;
    SmoothBypass bypass;
};

} // namespace agm
