#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace agm {

class Delay
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        const size_t maxSamples = static_cast<size_t>(std::ceil(sr * 2.25)) + 8;
        leftLine.assign(maxSamples, 0.0f);
        rightLine.assign(maxSamples, 0.0f);
        const float srF = static_cast<float>(sr);
        timeCoef = 1.0f - std::exp(-1.0f / (0.020f * srF));
        mixCoef = 1.0f - std::exp(-1.0f / (0.010f * srF));
        fbCoef = 1.0f - std::exp(-1.0f / (0.012f * srF));
        widthCoef = fbCoef;
        lfoInc = juce::MathConstants<float>::twoPi * lfoRateHz / srF;
        dampingCoef = computeDampingCoef(damping);
        targetTimeSamples = timeMsToSamples(timeMs);
        reset();
    }

    void reset()
    {
        if (!leftLine.empty())
        {
            std::fill(leftLine.begin(), leftLine.end(), 0.0f);
            std::fill(rightLine.begin(), rightLine.end(), 0.0f);
        }
        writeIndex = 0;
        smoothTimeSamples = targetTimeSamples;
        smoothMix = targetMix;
        smoothFb = feedbackGain;
        smoothWidth = width;
        lpStateL = 0.0f;
        lpStateR = 0.0f;
        lfoPhase = 0.0f;
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
        const float sizeF = static_cast<float>(size);

        float* leftPtr = buffer.getWritePointer(0);
        float* rightPtr = mono ? nullptr : buffer.getWritePointer(1);

        const float dampCoef = dampingCoef;
        const float oneMinusDamp = 1.0f - dampCoef;
        const float maxDist = sizeF - 4.0f;
        const float modDepth = modulationDepthSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            smoothTimeSamples += (targetTimeSamples - smoothTimeSamples) * timeCoef;
            smoothMix += (targetMix - smoothMix) * mixCoef;
            smoothFb += (feedbackGain - smoothFb) * fbCoef;
            smoothWidth += (width - smoothWidth) * widthCoef;
            lfoPhase += lfoInc;
            if (lfoPhase >= juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;

            const float wetEnv = wetGain.next();
            const float effMix = smoothMix * wetEnv;
            const float sameGain = smoothFb * (1.0f - smoothWidth);
            const float crossGain = smoothFb * smoothWidth;

            float dist = smoothTimeSamples + std::sin(lfoPhase) * modDepth;
            if (dist < 2.0f)
                dist = 2.0f;
            else if (dist > maxDist)
                dist = maxDist;
            float readPos = static_cast<float>(writeIndex) - dist;
            if (readPos < 0.0f)
                readPos += sizeF;

            float inL = leftPtr[i];
            if (!std::isfinite(inL))
                inL = 0.0f;
            const float delayedL = readCubic(leftLine, readPos, size);

            if (mono)
            {
                const float dampedL = delayedL * dampCoef + lpStateL * oneMinusDamp;
                lpStateL = dampedL;
                leftPtr[i] = inL * (1.0f - effMix) + delayedL * effMix;
                leftLine[static_cast<size_t>(writeIndex)] = sanitize(inL + dampedL * smoothFb);
            }
            else
            {
                float inR = rightPtr[i];
                if (!std::isfinite(inR))
                    inR = 0.0f;
                const float delayedR = readCubic(rightLine, readPos, size);
                const float dampedL = delayedL * dampCoef + lpStateL * oneMinusDamp;
                const float dampedR = delayedR * dampCoef + lpStateR * oneMinusDamp;
                lpStateL = dampedL;
                lpStateR = dampedR;
                leftPtr[i] = inL * (1.0f - effMix) + delayedL * effMix;
                rightPtr[i] = inR * (1.0f - effMix) + delayedR * effMix;
                leftLine[static_cast<size_t>(writeIndex)] = sanitize(inL + dampedL * sameGain + dampedR * crossGain);
                rightLine[static_cast<size_t>(writeIndex)] = sanitize(inR + dampedR * sameGain + dampedL * crossGain);
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

    float modulationDepthSamples() const
    {
        const float depthMs = juce::jmin(timeMs * 0.0015f, 1.5f);
        return depthMs * 0.001f * static_cast<float>(sr);
    }

    static float readCubic(const std::vector<float>& line, float readPos, int size)
    {
        const int i0 = static_cast<int>(readPos);
        const float f = readPos - static_cast<float>(i0);
        const int im1 = i0 > 0 ? i0 - 1 : size - 1;
        int i1 = i0 + 1;
        if (i1 >= size)
            i1 = 0;
        int i2 = i0 + 2;
        if (i2 >= size)
            i2 -= size;
        const float ym1 = line[static_cast<size_t>(im1)];
        const float y0 = line[static_cast<size_t>(i0)];
        const float y1 = line[static_cast<size_t>(i1)];
        const float y2 = line[static_cast<size_t>(i2)];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    float sanitize(float v)
    {
        if (!std::isfinite(v))
        {
            std::fill(leftLine.begin(), leftLine.end(), 0.0f);
            std::fill(rightLine.begin(), rightLine.end(), 0.0f);
            lpStateL = 0.0f;
            lpStateR = 0.0f;
            return 0.0f;
        }
        return (v > -1.0e-24f && v < 1.0e-24f) ? 0.0f : v;
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
    float smoothFb = 0.0f;
    float fbCoef = 0.0f;
    float damping = 0.0f;
    float dampingCoef = 0.0f;
    float width = 0.0f;
    float smoothWidth = 0.0f;
    float widthCoef = 0.0f;
    float lpStateL = 0.0f;
    float lpStateR = 0.0f;
    float lfoPhase = 0.0f;
    float lfoInc = 0.0f;
    float lfoRateHz = 0.4f;
    SmoothBypass wetGain;
};

}
