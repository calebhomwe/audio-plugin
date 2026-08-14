#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>

namespace agm
{

class StereoImager
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        jassert(sampleRate > 0.0);
        juce::ignoreUnused(blockSize);

        widthSmooth.prepare(static_cast<float>(sampleRate), smoothingMs);
        balanceSmooth.prepare(static_cast<float>(sampleRate), smoothingMs);
        monoSmooth.prepare(static_cast<float>(sampleRate), smoothingMs);

        widthSmooth.resetTo(widthTarget);
        balanceSmooth.resetTo(balanceTarget);
        monoSmooth.resetTo(monoTarget);

        bypass.prepare(sampleRate);
    }

    void reset()
    {
        widthSmooth.resetTo(widthTarget);
        balanceSmooth.resetTo(balanceTarget);
        monoSmooth.resetTo(monoTarget);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numChannels < 2 || numSamples == 0)
            return;

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = right[i];

            const float width = widthSmooth.next(widthTarget);
            const float balance = balanceSmooth.next(balanceTarget);
            const float monoAmount = monoSmooth.next(monoTarget);
            const float wetMix = bypass.next();

            const float gainL = balance <= 0.0f ? 1.0f : 1.0f - balance;
            const float gainR = balance <= 0.0f ? 1.0f + balance : 1.0f;

            const float mid = 0.5f * (dryL + dryR);
            const float side = 0.5f * (dryL - dryR) * width;

            float outL = (mid + side) * gainL;
            float outR = (mid - side) * gainR;

            outL += monoAmount * (mid - outL);
            outR += monoAmount * (mid - outR);

            left[i] = dryL + wetMix * (outL - dryL);
            right[i] = dryR + wetMix * (outR - dryR);
        }
    }

    void setEnabled(bool on)
    {
        bypass.setEnabled(on);
    }

    void setWidth(float width)
    {
        widthTarget = juce::jlimit(0.0f, 2.0f, width);
    }

    void setBalance(float balance)
    {
        balanceTarget = juce::jlimit(-1.0f, 1.0f, balance);
    }

    void setMono(bool mono)
    {
        monoTarget = mono ? 1.0f : 0.0f;
    }

private:
    struct OnePole
    {
        void prepare(float sampleRate, float timeMs)
        {
            coeff = 1.0f - std::exp(-1.0f / (sampleRate * 0.001f * timeMs));
        }

        void resetTo(float value)
        {
            state = value;
        }

        float next(float target)
        {
            state += coeff * (target - state);
            return state;
        }

        float coeff = 1.0f;
        float state = 0.0f;
    };

    static constexpr float smoothingMs = 10.0f;

    float widthTarget = 1.0f;
    float balanceTarget = 0.0f;
    float monoTarget = 0.0f;

    OnePole widthSmooth;
    OnePole balanceSmooth;
    OnePole monoSmooth;

    SmoothBypass bypass;
};

}
