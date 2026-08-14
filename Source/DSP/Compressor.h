#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <atomic>

namespace agm {

class Compressor
{
public:
    Compressor()
    {
        updateAttackCoef();
        updateReleaseCoef();
        updateMixCoef();
        updateGrCoef();
    }

    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate;
        updateAttackCoef();
        updateReleaseCoef();
        updateMixCoef();
        updateGrCoef();
        mixSmooth = mixTarget;
        bypass.prepare(sampleRate);
    }

    void reset()
    {
        envDb = -120.0f;
        mixSmooth = mixTarget;
        smoothedGrDb.store(0.0f, std::memory_order_relaxed);
        bypass.prepare(sr);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0)
            return;

        float* ch0 = buffer.getWritePointer(0);
        float* ch1 = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        const float tDb = thresholdDb;
        const float k = kneeDb;
        const float halfK = k * 0.5f;
        const float k2 = 2.0f * k;
        const float oneMinusInvRatio = 1.0f - 1.0f / ratio;
        const float mk = makeupDb;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry0 = ch0[i];
            const float dry1 = ch1 ? ch1[i] : 0.0f;

            float maxAbs = std::fabs(dry0);
            if (ch1)
            {
                const float a1 = std::fabs(dry1);
                if (a1 > maxAbs)
                    maxAbs = a1;
            }

            float levelDb = gainToDb(maxAbs);
            if (!(levelDb > -120.0f))
                levelDb = -120.0f;

            envDb += (levelDb > envDb ? attackCoef : releaseCoef) * (levelDb - envDb);

            const float overshoot = envDb - tDb;
            float gr = 0.0f;
            if (overshoot > halfK)
                gr = overshoot * oneMinusInvRatio;
            else if (k > 0.0f && overshoot > -halfK)
            {
                const float x = overshoot + halfK;
                gr = oneMinusInvRatio * (x * x) / k2;
            }

            float wetGain = dbToGain(-gr + mk);
            if (wetGain > 1.0f)
                wetGain = 1.0f;

            const float prevGr = smoothedGrDb.load(std::memory_order_relaxed);
            smoothedGrDb.store(prevGr + grCoef * (gr - prevGr), std::memory_order_relaxed);

            mixSmooth += (mixTarget - mixSmooth) * mixCoef;
            const float gain = (1.0f - mixSmooth) + wetGain * mixSmooth;

            const float bg = bypass.next();

            ch0[i] = dry0 + (dry0 * gain - dry0) * bg;
            if (ch1)
                ch1[i] = dry1 + (dry1 * gain - dry1) * bg;
        }
    }

    void setEnabled(bool on) { bypass.setEnabled(on); }
    void setThresholdDb(float v) { thresholdDb = juce::jlimit(-60.0f, 0.0f, v); }
    void setRatio(float v) { ratio = juce::jlimit(1.0f, 20.0f, v); }
    void setAttackMs(float v) { attackMs = juce::jlimit(0.1f, 100.0f, v); updateAttackCoef(); }
    void setReleaseMs(float v) { releaseMs = juce::jlimit(10.0f, 1000.0f, v); updateReleaseCoef(); }
    void setKneeDb(float v) { kneeDb = juce::jlimit(0.0f, 24.0f, v); }
    void setMakeupDb(float v) { makeupDb = juce::jlimit(0.0f, 24.0f, v); }
    void setMix(float v) { mixTarget = juce::jlimit(0.0f, 1.0f, v); }

    float getGainReductionDb() const { return smoothedGrDb.load(std::memory_order_relaxed); }

private:
    void updateAttackCoef() { attackCoef = std::exp(-1.0f / (attackMs * 0.001f * (float)sr)); }
    void updateReleaseCoef() { releaseCoef = std::exp(-1.0f / (releaseMs * 0.001f * (float)sr)); }
    void updateMixCoef() { mixCoef = 1.0f - std::exp(-1.0f / (10.0f * 0.001f * (float)sr)); }
    void updateGrCoef() { grCoef = 1.0f - std::exp(-1.0f / (50.0f * 0.001f * (float)sr)); }

    double sr = 44100.0;
    float thresholdDb = -12.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
    float mixTarget = 1.0f;

    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
    float mixCoef = 0.0f;
    float grCoef = 0.0f;

    float envDb = -120.0f;
    float mixSmooth = 1.0f;
    std::atomic<float> smoothedGrDb{ 0.0f };

    SmoothBypass bypass;
};

} // namespace agm
