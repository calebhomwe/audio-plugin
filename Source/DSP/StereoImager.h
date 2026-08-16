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
        juce::ignoreUnused(blockSize);
        const double sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        widthSmooth.prepare(sr, smoothingMs);
        balanceSmooth.prepare(sr, smoothingMs);
        monoSmooth.prepare(sr, smoothingMs);
        widthSmooth.snapTo(widthTarget);
        balanceSmooth.snapTo(balanceTarget);
        monoSmooth.snapTo(monoTarget);
        bypass.prepare(sampleRate);
    }

    void reset()
    {
        widthSmooth.snapTo(widthTarget);
        balanceSmooth.snapTo(balanceTarget);
        monoSmooth.snapTo(monoTarget);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numChannels < 2 || numSamples <= 0)
            return;

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = sanitize(left[i]);
            const float dryR = sanitize(right[i]);

            const float width = widthSmooth.next();
            const float balance = juce::jlimit(-1.0f, 1.0f, balanceSmooth.next());
            const float monoAmount = monoSmooth.next();
            const float wetMix = bypass.next();

            const float atten = juce::jmax(0.0f, std::cos(balance * 1.5707963267948966f));
            const float gainL = balance > 0.0f ? atten : 1.0f;
            const float gainR = balance < 0.0f ? atten : 1.0f;

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
        widthTarget = juce::jlimit(0.0f, 2.0f, sanitize(width, 1.0f));
        widthSmooth.setTarget(widthTarget);
    }

    void setBalance(float balance)
    {
        balanceTarget = juce::jlimit(-1.0f, 1.0f, sanitize(balance));
        balanceSmooth.setTarget(balanceTarget);
    }

    void setMono(bool mono)
    {
        monoTarget = mono ? 1.0f : 0.0f;
        monoSmooth.setTarget(monoTarget);
    }

private:
    static constexpr float smoothingMs = 10.0f;

    float widthTarget = 1.0f;
    float balanceTarget = 0.0f;
    float monoTarget = 0.0f;

    OnePole widthSmooth;
    OnePole balanceSmooth;
    OnePole monoSmooth;

    SmoothBypass bypass;
};

} // namespace agm
