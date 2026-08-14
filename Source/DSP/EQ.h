#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "Biquad.h"
#include <cmath>

namespace agm {

class EQ
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        fs = sampleRate;
        smoothCoef = 1.0f - std::exp(-1.0f / (0.010f * (float)sampleRate));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 7; ++i)
                bands[ch][i].prepare(sampleRate);
        hpMix.prepare(sampleRate);
        lpMix.prepare(sampleRate);
        masterMix.prepare(sampleRate);
        for (int i = 0; i < 7; ++i)
        {
            curFreq[i] = tgtFreq[i];
            curGain[i] = tgtGain[i];
            curQ[i] = tgtQ[i];
            lastFreq[i] = 0.0f;
            lastGain[i] = 0.0f;
            lastQ[i] = 0.0f;
            updateBand(i);
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 7; ++i)
                bands[ch][i].reset();
        hpMix.prepare(fs);
        lpMix.prepare(fs);
        masterMix.prepare(fs);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 1)
        {
            float* ch = buffer.getWritePointer(0);
            for (int s = 0; s < numSamples; ++s)
                ch[s] = processSample(ch[s], 0);
        }
        else if (numChannels == 2)
        {
            float* l = buffer.getWritePointer(0);
            float* r = buffer.getWritePointer(1);
            for (int s = 0; s < numSamples; ++s)
            {
                l[s] = processSample(l[s], 0);
                r[s] = processSample(r[s], 1);
            }
        }
    }

    void setEnabled(bool on)
    {
        enabled = on;
        masterMix.setEnabled(on);
    }

    void setHpEnabled(bool on)
    {
        hpOn = on;
        hpMix.setEnabled(on);
    }

    void setHpFreq(float hz) { tgtFreq[0] = clampFreq(hz); }

    void setLpEnabled(bool on)
    {
        lpOn = on;
        lpMix.setEnabled(on);
    }

    void setLpFreq(float hz) { tgtFreq[6] = clampFreq(hz); }

    void setLowShelfFreq(float hz) { tgtFreq[1] = clampFreq(hz); }
    void setLowShelfGainDb(float db) { tgtGain[1] = clampGain(db); }

    void setHighShelfFreq(float hz) { tgtFreq[5] = clampFreq(hz); }
    void setHighShelfGainDb(float db) { tgtGain[5] = clampGain(db); }

    void setPeakFreq(int i, float hz) { tgtFreq[2 + i] = clampFreq(hz); }
    void setPeakGainDb(int i, float db) { tgtGain[2 + i] = clampGain(db); }
    void setPeakQ(int i, float q) { tgtQ[2 + i] = clampQ(q); }

    float getResponseDb(float freq) const
    {
        if (!enabled)
            return 0.0f;
        float sum = 0.0f;
        if (hpOn)
            sum += bands[0][0].magnitudeDbAt(freq);
        sum += bands[0][1].magnitudeDbAt(freq);
        sum += bands[0][2].magnitudeDbAt(freq);
        sum += bands[0][3].magnitudeDbAt(freq);
        sum += bands[0][4].magnitudeDbAt(freq);
        sum += bands[0][5].magnitudeDbAt(freq);
        if (lpOn)
            sum += bands[0][6].magnitudeDbAt(freq);
        return sum;
    }

    void dbgDump()
    {
        std::cout << "curGain: " << curGain[0] << " " << curGain[1] << " " << curGain[2] << " "
                  << curGain[3] << " " << curGain[4] << " " << curGain[5] << " " << curGain[6] << "\n";
        std::cout << "curFreq: " << curFreq[0] << " " << curFreq[1] << " " << curFreq[2] << " "
                  << curFreq[3] << " " << curFreq[4] << " " << curFreq[5] << " " << curFreq[6] << "\n";
        for (int i = 0; i < 7; ++i) { std::cout << "band " << i << ": "; bands[0][i].dbg(); }
    }

private:
    static float clampFreq(float f) { return f < 20.0f ? 20.0f : (f > 20000.0f ? 20000.0f : f); }
    static float clampGain(float g) { return g < -15.0f ? -15.0f : (g > 15.0f ? 15.0f : g); }
    static float clampQ(float q) { return q < 0.2f ? 0.2f : (q > 10.0f ? 10.0f : q); }

    void updateBand(int i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            switch (i)
            {
            case 0:
                bands[ch][0].setHighPass(curFreq[0], 0.70710678f);
                break;
            case 1:
                bands[ch][1].setLowShelf(curFreq[1], curGain[1], 0.9f);
                break;
            case 2:
            case 3:
            case 4:
                bands[ch][i].setPeaking(curFreq[i], curGain[i], curQ[i]);
                break;
            case 5:
                bands[ch][5].setHighShelf(curFreq[5], curGain[5], 0.9f);
                break;
            case 6:
                bands[ch][6].setLowPass(curFreq[6], 0.70710678f);
                break;
            }
        }
        lastFreq[i] = curFreq[i];
        lastGain[i] = curGain[i];
        lastQ[i] = curQ[i];
    }

    void smoothParams()
    {
        for (int i = 0; i < 7; ++i)
        {
            curFreq[i] += (tgtFreq[i] - curFreq[i]) * smoothCoef;
            curGain[i] += (tgtGain[i] - curGain[i]) * smoothCoef;
            curQ[i] += (tgtQ[i] - curQ[i]) * smoothCoef;
            const bool freqChanged = std::fabs(curFreq[i] - lastFreq[i]) > 1e-4f * lastFreq[i];
            const bool gainChanged = std::fabs(curGain[i] - lastGain[i]) > 1e-3f;
            const bool qChanged = std::fabs(curQ[i] - lastQ[i]) > 1e-3f;
            if (freqChanged || gainChanged || qChanged)
                updateBand(i);
        }
    }

    float processSample(float x, int ch)
    {
        smoothParams();
        float y = bands[ch][0].process(x);
        const float mHp = hpMix.next();
        y = y * mHp + x * (1.0f - mHp);
        y = bands[ch][1].process(y);
        y = bands[ch][2].process(y);
        y = bands[ch][3].process(y);
        y = bands[ch][4].process(y);
        y = bands[ch][5].process(y);
        const float lpIn = y;
        y = bands[ch][6].process(y);
        const float mLp = lpMix.next();
        y = y * mLp + lpIn * (1.0f - mLp);
        const float mMaster = masterMix.next();
        return y * mMaster + x * (1.0f - mMaster);
    }

    Biquad bands[2][7];
    SmoothBypass hpMix;
    SmoothBypass lpMix;
    SmoothBypass masterMix;

    float tgtFreq[7] = {40.0f, 120.0f, 200.0f, 800.0f, 3200.0f, 8000.0f, 18000.0f};
    float tgtGain[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float tgtQ[7] = {0.70710678f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.70710678f};
    float curFreq[7] = {40.0f, 120.0f, 200.0f, 800.0f, 3200.0f, 8000.0f, 18000.0f};
    float curGain[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float curQ[7] = {0.70710678f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.70710678f};
    float lastFreq[7] = {0.0f};
    float lastGain[7] = {0.0f};
    float lastQ[7] = {0.0f};

    bool hpOn = false;
    bool lpOn = false;
    bool enabled = true;

    float smoothCoef = 0.001f;
    double fs = 44100.0;
};

} // namespace agm
