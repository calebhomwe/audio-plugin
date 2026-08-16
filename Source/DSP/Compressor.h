#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <atomic>

namespace agm {

class Compressor
{
public:
    Compressor() { updateCoefs(); }

    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        updateCoefs();
        envDb = -120.0f;
        rmsSq = 0.0f;
        mixSmooth = mixTarget;
        mkSmooth = makeupDb;
        smoothedGrDb.store(0.0f, std::memory_order_relaxed);
        bypass.prepare(sr);
    }

    void reset()
    {
        envDb = -120.0f;
        rmsSq = 0.0f;
        mixSmooth = mixTarget;
        mkSmooth = makeupDb;
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
        const float invTwoK = k > 0.0f ? 1.0f / (2.0f * k) : 0.0f;
        const float slopeFactor = 1.0f - 1.0f / ratio;
        const float atk = attackCoef;
        const float rel = releaseCoef;
        const float rmsC = rmsCoef;
        const float mkC = mkCoef;
        const float grC = grCoef;
        const float mixC = mixCoef;
        const float mixT = mixTarget;
        const float mkT = makeupDb;

        float env = envDb;
        float rms = rmsSq;
        float mk = mkSmooth;
        float mixS = mixSmooth;
        float grMeter = smoothedGrDb.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            float dry0 = ch0[i];
            float dry1 = ch1 != nullptr ? ch1[i] : 0.0f;
            if (!std::isfinite(dry0)) { dry0 = 0.0f; ch0[i] = 0.0f; }
            if (ch1 != nullptr && !std::isfinite(dry1)) { dry1 = 0.0f; ch1[i] = 0.0f; }

            const float a0 = std::fabs(dry0);
            const float a1 = ch1 != nullptr ? std::fabs(dry1) : 0.0f;
            const float peak = a0 > a1 ? a0 : a1;

            rms += rmsC * (peak * peak - rms);
            if (rms < 1e-24f)
                rms = 0.0f;

            float level = peak;
            const float rmsEquiv = std::sqrt(rms) * 1.41421356f;
            if (rmsEquiv > level)
                level = rmsEquiv;

            float levelDb = gainToDb(level > 1e-8f ? level : 1e-8f);
            if (!(levelDb > -120.0f))
                levelDb = -120.0f;

            env += (levelDb > env ? atk : rel) * (levelDb - env);

            const float over = env - tDb;
            float gr = 0.0f;
            if (over > halfK)
                gr = over * slopeFactor;
            else if (k > 0.0f && over > -halfK)
            {
                const float x = over + halfK;
                gr = slopeFactor * x * x * invTwoK;
            }

            mk += mkC * (mkT - mk);

            float wetGain = dbToGain(mk - gr);
            if (!(wetGain < 1.0f))
                wetGain = 1.0f;
            if (!(wetGain >= 0.0f))
                wetGain = 0.0f;

            grMeter += grC * (gr - grMeter);

            mixS += mixC * (mixT - mixS);
            const float gain = (1.0f - mixS) + wetGain * mixS;
            const float bg = bypass.next();

            ch0[i] = dry0 + (dry0 * gain - dry0) * bg;
            if (ch1 != nullptr)
                ch1[i] = dry1 + (dry1 * gain - dry1) * bg;
        }

        envDb = std::isfinite(env) ? env : -120.0f;
        rmsSq = std::isfinite(rms) ? rms : 0.0f;
        mkSmooth = std::isfinite(mk) ? mk : mkT;
        mixSmooth = std::isfinite(mixS) ? mixS : mixT;
        smoothedGrDb.store(std::isfinite(grMeter) ? grMeter : 0.0f, std::memory_order_relaxed);
    }

    void setEnabled(bool on) { bypass.setEnabled(on); }
    void setThresholdDb(float v) { thresholdDb = juce::jlimit(-60.0f, 0.0f, v); }
    void setRatio(float v) { ratio = juce::jlimit(1.0f, 20.0f, v); }
    void setAttackMs(float v) { attackMs = juce::jlimit(0.1f, 100.0f, v); updateCoefs(); }
    void setReleaseMs(float v) { releaseMs = juce::jlimit(10.0f, 1000.0f, v); updateCoefs(); }
    void setKneeDb(float v) { kneeDb = juce::jlimit(0.0f, 24.0f, v); }
    void setMakeupDb(float v) { makeupDb = juce::jlimit(0.0f, 24.0f, v); }
    void setMix(float v) { mixTarget = juce::jlimit(0.0f, 1.0f, v); }

    float getGainReductionDb() const { return smoothedGrDb.load(std::memory_order_relaxed); }

private:
    void updateCoefs()
    {
        const double fs = sr > 0.0 ? sr : 44100.0;
        attackCoef = (float)std::exp(-1.0 / (attackMs * 0.001 * fs));
        releaseCoef = (float)std::exp(-1.0 / (releaseMs * 0.001 * fs));
        rmsCoef = (float)(1.0 - std::exp(-1.0 / (8.0 * 0.001 * fs)));
        mixCoef = (float)(1.0 - std::exp(-1.0 / (10.0 * 0.001 * fs)));
        mkCoef = (float)(1.0 - std::exp(-1.0 / (15.0 * 0.001 * fs)));
        grCoef = (float)(1.0 - std::exp(-1.0 / (50.0 * 0.001 * fs)));
    }

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
    float rmsCoef = 0.0f;
    float mixCoef = 0.0f;
    float mkCoef = 0.0f;
    float grCoef = 0.0f;

    float envDb = -120.0f;
    float rmsSq = 0.0f;
    float mixSmooth = 1.0f;
    float mkSmooth = 0.0f;
    std::atomic<float> smoothedGrDb{ 0.0f };

    SmoothBypass bypass;
};

} // namespace agm
