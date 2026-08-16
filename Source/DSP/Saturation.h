#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "Biquad.h"
#include <cmath>

namespace agm {

class Saturation
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        osRate = sr * 4.0;
        const int maxSamples = juce::jmax(blockSize, 1);
        oversampler.initProcessing(static_cast<size_t>(maxSamples));
        oversampler.reset();
        tapeLP[0].prepare(osRate);
        tapeLP[1].prepare(osRate);
        exciterHP[0].prepare(osRate);
        exciterHP[1].prepare(osRate);
        driveGainSmooth.prepare(osRate, 10.0f);
        outGainSmooth.prepare(osRate, 10.0f);
        mixSmooth.prepare(osRate, 10.0f);
        modeMixSmooth.prepare(osRate, 15.0f);
        servoCoef = 1.0f - std::exp(-6.2831853f * 10.0f / (float)osRate);
        workBuffer.setSize(2, maxSamples, false, false, true);
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
        dcServo[0] = 0.0f;
        dcServo[1] = 0.0f;
        driveGainSmooth.snapTo(dbToGain(drive * 24.0f));
        outGainSmooth.snapTo(dbToGain(outputDb));
        mixSmooth.snapTo(mix);
        modeMixSmooth.snapTo(1.0f);
        bypass.prepare(osRate);
    }

    int getLatencySamples() const { return oversampler.getLatencyInSamples(); }

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

        for (int i = 0; i < osNum; ++i)
        {
            const float pre = driveGainSmooth.next();
            const float post = outGainSmooth.next();
            const float amount = bypass.next() * mixSmooth.next();
            const float modeMix = modeMixSmooth.next();
            if (amount > 0.0f)
            {
                const float inL = sanitize(osL[i]);
                const float inR = sanitize(osR[i]);
                float wetL = shapeSample(0, mode, inL * pre);
                float wetR = shapeSample(1, mode, inR * pre);
                if (modeMix < 1.0f)
                {
                    const float dry = 1.0f - modeMix;
                    wetL = wetL * modeMix + shapeSample(0, prevMode, inL * pre) * dry;
                    wetR = wetR * modeMix + shapeSample(1, prevMode, inR * pre) * dry;
                }
                dcServo[0] += servoCoef * (wetL - dcServo[0]);
                dcServo[1] += servoCoef * (wetR - dcServo[1]);
                wetL = (wetL - dcServo[0]) * post;
                wetR = (wetR - dcServo[1]) * post;
                osL[i] = inL + amount * (wetL - inL);
                osR[i] = inR + amount * (wetR - inR);
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
        const int clamped = juce::jlimit(0, 3, m);
        if (clamped == mode)
            return;
        prevMode = mode;
        mode = clamped;
        modeMixSmooth.snapTo(0.0f);
        modeMixSmooth.setTarget(1.0f);
        if (mode == 1)
        {
            tapeLP[0].reset();
            tapeLP[1].reset();
        }
        if (mode == 3)
        {
            exciterHP[0].reset();
            exciterHP[1].reset();
        }
    }

    void setDrive(float d)
    {
        drive = juce::jlimit(0.0f, 1.0f, sanitize(d, 0.5f));
        driveGainSmooth.setTarget(dbToGain(drive * 24.0f));
    }

    void setMix(float m)
    {
        mix = juce::jlimit(0.0f, 1.0f, sanitize(m, 1.0f));
        mixSmooth.setTarget(mix);
    }

    void setOutputDb(float db)
    {
        outputDb = juce::jlimit(-12.0f, 12.0f, sanitize(db));
        outGainSmooth.setTarget(dbToGain(outputDb));
    }

private:
    float shapeSample(int ch, int m, float x)
    {
        switch (m)
        {
            case 1:
            {
                const float lp = tapeLP[ch].process(x);
                return std::tanh(1.35f * lp) / 1.35f;
            }
            case 2:
            {
                const float c = juce::jlimit(-1.0f, 1.0f, x);
                return c - c * c * c / 3.0f;
            }
            case 3:
            {
                const float hp = exciterHP[ch].process(x);
                const float g = 1.5f + 2.5f * drive;
                return x + drive * 0.7f * (std::tanh(g * hp) - hp);
            }
            default:
            {
                const float t = std::tanh(x);
                return (t + 0.2f * t * t) / 1.2f;
            }
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
    OnePole driveGainSmooth;
    OnePole outGainSmooth;
    OnePole mixSmooth;
    OnePole modeMixSmooth;

    double sr = 44100.0;
    double osRate = 176400.0;
    float servoCoef = 0.0003f;
    float dcServo[2] = { 0.0f, 0.0f };
    float drive = 0.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
    int mode = 0;
    int prevMode = 0;
};

} // namespace agm
