#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "Biquad.h"
#include <cmath>
#include <memory>

namespace agm {

class Saturation
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate;
        osRate = sampleRate * 4.0;
        oversampler.initProcessing(static_cast<size_t>(juce::jmax(blockSize, 1)));
        oversampler.reset();
        tapeLP[0].prepare(osRate);
        tapeLP[1].prepare(osRate);
        exciterHP[0].prepare(osRate);
        exciterHP[1].prepare(osRate);
        mixCoef = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(osRate)));
        workBuffer.setSize(2, juce::jmax(blockSize, 1), false, false, true);
        updateFilters();
        reset();
    }

    void reset()
    {
        oversampler.reset();
        tapeLP[0].reset();
        tapeLP[1].reset();
        exciterHP[0].reset();
        exciterHP[1].reset();
        smoothMix = mix;
        bypass.prepare(osRate);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0)
            return;

        if (workBuffer.getNumSamples() < numSamples)
            workBuffer.setSize(2, numSamples, false, false, true);

        juce::FloatVectorOperations::copy(workBuffer.getWritePointer(0), buffer.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::copy(workBuffer.getWritePointer(1),
                                          numChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0),
                                          numSamples);

        juce::dsp::AudioBlock<float> upBlock(workBuffer);
        auto upSub = upBlock.getSubBlock(0, static_cast<size_t>(numSamples));
        auto osBlock = oversampler.processSamplesUp(upSub);

        const int osNum = static_cast<int>(osBlock.getNumSamples());
        if (osNum <= 0)
            return;

        float* osL = osBlock.getChannelPointer(0);
        float* osR = osBlock.getChannelPointer(1);

        const float pre = dbToGain(drive * 24.0f);
        const float post = dbToGain(outputDb);

        for (int i = 0; i < osNum; ++i)
        {
            smoothMix += (mix - smoothMix) * mixCoef;
            const float amount = bypass.next() * smoothMix;
            if (amount > 0.0f)
            {
                const float inL = osL[i];
                osL[i] = inL + amount * (shapeSample(0, inL * pre) * post - inL);
                const float inR = osR[i];
                osR[i] = inR + amount * (shapeSample(1, inR * pre) * post - inR);
            }
        }

        juce::dsp::AudioBlock<float> downBlock(buffer);
        if (numChannels == 1)
        {
            auto monoBlock = downBlock.getSingleChannelBlock(0);
            oversampler.processSamplesDown(monoBlock);
        }
        else
        {
            auto stereoBlock = downBlock.getSubsetChannelBlock(0, 2);
            oversampler.processSamplesDown(stereoBlock);
        }
    }

    void setEnabled(bool on) { bypass.setEnabled(on); }

    void setMode(int m)
    {
        mode = juce::jlimit(0, 3, m);
        updateFilters();
        tapeLP[0].reset();
        tapeLP[1].reset();
        exciterHP[0].reset();
        exciterHP[1].reset();
    }

    void setDrive(float d) { drive = juce::jlimit(0.0f, 1.0f, d); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setOutputDb(float db) { outputDb = juce::jlimit(-12.0f, 12.0f, db); }

private:
    float shapeSample(int ch, float x)
    {
        switch (mode)
        {
            case 1:
            {
                const float lp = tapeLP[ch].process(x);
                return std::tanh(1.35f * lp) / 1.35f;
            }
            case 2:
            {
                const float c = x - x * x * x / 3.0f;
                return juce::jlimit(-1.25f, 1.25f, c);
            }
            case 3:
            {
                const float hp = exciterHP[ch].process(x);
                return x + (std::tanh(hp) - hp) * drive * 2.0f;
            }
            default:
                return (std::tanh(x) + 0.12f * std::tanh(0.5f * x)) / 1.06f;
        }
    }

    void updateFilters()
    {
        tapeLP[0].setLowPass(12000.0f, 0.7071f);
        tapeLP[1].setLowPass(12000.0f, 0.7071f);
        exciterHP[0].setHighPass(2500.0f, 0.7071f);
        exciterHP[1].setHighPass(2500.0f, 0.7071f);
    }

    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true };
    juce::AudioBuffer<float> workBuffer;
    Biquad tapeLP[2];
    Biquad exciterHP[2];
    SmoothBypass bypass;

    double sr = 44100.0;
    double osRate = 176400.0;
    float mixCoef = 0.0f;
    float drive = 0.0f;
    float mix = 1.0f;
    float smoothMix = 1.0f;
    float outputDb = 0.0f;
    int mode = 0;
};

}
