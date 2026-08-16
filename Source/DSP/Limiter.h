#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <array>
#include <atomic>

namespace agm {

class Limiter
{
    static constexpr int maxChannels = 2;
    static constexpr int maxDelay = 2048;

public:
    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        delaySamples = juce::jlimit(1, maxDelay, juce::roundToInt(0.003 * sr));
        updateCoefs();
        clearState();
        bypass.prepare(sr);
        bypass.setEnabled(enabled);
    }

    void reset()
    {
        clearState();
        bypass.prepare(sr);
        bypass.setEnabled(enabled);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || delaySamples <= 0)
            return;

        const int chans = juce::jmin(numChannels, maxChannels);
        const float ceiling = juce::jmax(dbToGain(ceilingDb), 1e-4f);
        const float zoneStart = ceiling * 0.95f;
        const float zoneWidth = ceiling - zoneStart;
        const float zoneInv = 1.0f / zoneWidth;
        const float down = downSlope;
        const float up = upSlope;
        const float grC = grCoef;

        float env = currentGain;
        float grMeter = grSmoother.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            float in[maxChannels];
            for (int ch = 0; ch < chans; ++ch)
            {
                float s = buffer.getReadPointer(ch)[i];
                if (!std::isfinite(s))
                {
                    s = 0.0f;
                    buffer.getWritePointer(ch)[i] = 0.0f;
                }
                in[ch] = s;
                const float a = std::fabs(s);
                if (a > level)
                    level = a;
            }

            const float req = level > ceiling ? ceiling / level : 1.0f;
            if (req < env)
            {
                env -= down;
                if (env < req)
                    env = req;
            }
            else
            {
                env += up;
                if (env > 1.0f)
                    env = 1.0f;
            }

            const float mix = bypass.next();

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = delayLines[ch];
                int wp = writePos[ch];
                const float delayed = line[wp];
                line[wp] = in[ch];
                if (++wp >= delaySamples)
                    wp = 0;
                writePos[ch] = wp;

                float out = delayed;
                if (mix > 0.0001f)
                {
                    float lim = delayed * env;
                    const float a = std::fabs(lim);
                    if (a > zoneStart)
                    {
                        const float t = (a - zoneStart) * zoneInv;
                        const float shaped = zoneStart + zoneWidth * std::tanh(t);
                        lim = lim >= 0.0f ? shaped : -shaped;
                    }
                    out = delayed + mix * (lim - delayed);
                }
                buffer.getWritePointer(ch)[i] = out;
            }

            grMeter += grC * (-gainToDb(env) - grMeter);
        }

        currentGain = std::isfinite(env) ? juce::jlimit(0.0f, 1.0f, env) : 1.0f;
        grSmoother.store(std::isfinite(grMeter) ? grMeter : 0.0f, std::memory_order_relaxed);
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
        const double fs = sr > 0.0 ? sr : 44100.0;
        const double n = delaySamples > 0 ? (double)delaySamples : 1.0;
        const double atk = juce::jlimit(0.5, n, attackMs * 0.001 * fs);
        downSlope = (float)(1.0 / atk);
        const double rel = juce::jmax(1.0, releaseMs * 0.001 * fs);
        upSlope = (float)(1.0 / rel);
        grCoef = (float)(1.0 - std::exp(-1.0 / (50.0 * 0.001 * fs)));
    }

    void clearState()
    {
        for (auto& line : delayLines)
            line.fill(0.0f);
        writePos.fill(0);
        currentGain = 1.0f;
        grSmoother.store(0.0f, std::memory_order_relaxed);
    }

    std::array<std::array<float, maxDelay>, maxChannels> delayLines{};
    std::array<int, maxChannels> writePos{ { 0, 0 } };
    double sr = 44100.0;
    int delaySamples = 0;
    float ceilingDb = -1.0f;
    float attackMs = 1.0f;
    float releaseMs = 100.0f;
    float downSlope = 1.0f;
    float upSlope = 0.0001f;
    float grCoef = 0.0f;
    float currentGain = 1.0f;
    std::atomic<float> grSmoother{ 0.0f };
    bool enabled = true;
    SmoothBypass bypass;
};

} // namespace agm
