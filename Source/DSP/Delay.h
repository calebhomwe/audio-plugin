#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <vector>

namespace agm {

class Delay
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate;
        const size_t maxSamples = static_cast<size_t>(std::ceil(sr * 2.2)) + 1;
        leftLine.assign(maxSamples, 0.0f);
        rightLine.assign(maxSamples, 0.0f);
        timeCoef = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(sr)));
        mixCoef = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sr)));
        dampingCoef = computeDampingCoef(damping);
        targetTimeSamples = timeMsToSamples(timeMs);
        reset();
    }

    void reset()
    {
        if (!leftLine.empty())
        {
            leftLine.assign(leftLine.size(), 0.0f);
            rightLine.assign(rightLine.size(), 0.0f);
        }
        writeIndex = 0;
        smoothTimeSamples = targetTimeSamples;
        smoothMix = targetMix;
        lpStateL = 0.0f;
        lpStateR = 0.0f;
        wetGain.prepare(sr, 15.0f);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        if (leftLine.empty())
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numChannels < 1 || numSamples < 1)
            return;

        const bool mono = numChannels < 2;
        const int size = static_cast<int>(leftLine.size());
        const float sizeFloat = static_cast<float>(size);

        float* leftPtr = buffer.getWritePointer(0);
        float* rightPtr = mono ? nullptr : buffer.getWritePointer(1);

        const float sameGain = feedbackGain * (1.0f - width);
        const float crossGain = feedbackGain * width;
        const float dampCoef = dampingCoef;
        const float oneMinusDamp = 1.0f - dampCoef;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothTimeSamples += (targetTimeSamples - smoothTimeSamples) * timeCoef;
            smoothMix += (targetMix - smoothMix) * mixCoef;
            const float wetEnv = wetGain.next();
            const float effMix = smoothMix * wetEnv;

            float readPos = static_cast<float>(writeIndex) - smoothTimeSamples;
            while (readPos < 0.0f)
                readPos += sizeFloat;
            const int i0 = static_cast<int>(readPos);
            const float frac = readPos - static_cast<float>(i0);
            int i1 = i0 + 1;
            if (i1 >= size)
                i1 = 0;

            const float inL = leftPtr[i];
            const float delayedL = leftLine[static_cast<size_t>(i0)] * (1.0f - frac) + leftLine[static_cast<size_t>(i1)] * frac;

            if (mono)
            {
                const float dampedL = delayedL * dampCoef + lpStateL * oneMinusDamp;
                lpStateL = dampedL;
                leftPtr[i] = inL * (1.0f - effMix) + delayedL * effMix;
                leftLine[static_cast<size_t>(writeIndex)] = inL + dampedL * feedbackGain;
            }
            else
            {
                const float inR = rightPtr[i];
                const float delayedR = rightLine[static_cast<size_t>(i0)] * (1.0f - frac) + rightLine[static_cast<size_t>(i1)] * frac;
                const float dampedL = delayedL * dampCoef + lpStateL * oneMinusDamp;
                const float dampedR = delayedR * dampCoef + lpStateR * oneMinusDamp;
                lpStateL = dampedL;
                lpStateR = dampedR;
                leftPtr[i] = inL * (1.0f - effMix) + delayedL * effMix;
                rightPtr[i] = inR * (1.0f - effMix) + delayedR * effMix;
                leftLine[static_cast<size_t>(writeIndex)] = inL + dampedL * sameGain + dampedR * crossGain;
                rightLine[static_cast<size_t>(writeIndex)] = inR + dampedR * sameGain + dampedL * crossGain;
            }

            ++writeIndex;
            if (writeIndex >= size)
                writeIndex = 0;
        }
    }

    void setEnabled(bool on) { wetGain.setEnabled(on); }

    void setTimeMs(float ms)
    {
        timeMs = juce::jlimit(20.0f, 2000.0f, ms);
        targetTimeSamples = timeMsToSamples(timeMs);
    }

    void setFeedback(float fb) { feedbackGain = juce::jlimit(0.0f, 0.95f, fb); }

    void setMix(float m) { targetMix = juce::jlimit(0.0f, 1.0f, m); }

    void setDamping(float d)
    {
        damping = juce::jlimit(0.0f, 1.0f, d);
        dampingCoef = computeDampingCoef(damping);
    }

    void setWidth(float w) { width = juce::jlimit(0.0f, 1.0f, w); }

private:
    float timeMsToSamples(float ms) const { return ms * 0.001f * static_cast<float>(sr); }

    float computeDampingCoef(float d) const
    {
        const float fc = 20000.0f - d * 19800.0f;
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / static_cast<float>(sr));
    }

    double sr = 44100.0;
    std::vector<float> leftLine;
    std::vector<float> rightLine;
    int writeIndex = 0;
    float timeMs = 500.0f;
    float targetTimeSamples = 22050.0f;
    float smoothTimeSamples = 22050.0f;
    float timeCoef = 0.0f;
    float targetMix = 0.5f;
    float smoothMix = 0.5f;
    float mixCoef = 0.0f;
    float feedbackGain = 0.0f;
    float damping = 0.0f;
    float dampingCoef = 0.0f;
    float width = 0.0f;
    float lpStateL = 0.0f;
    float lpStateR = 0.0f;
    SmoothBypass wetGain;
};

}
