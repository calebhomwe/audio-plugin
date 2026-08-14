#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <vector>

namespace agm {

class Reverb
{
public:
    Reverb()
    {
        const int combLens[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        const int apLens[4] = { 556, 441, 341, 225 };
        for (int i = 0; i < 8; ++i) combs[i].baseLen = combLens[i];
        for (int i = 0; i < 4; ++i) allpasses[i].baseLen = apLens[i];
    }

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        bs = blockSize > 0 ? blockSize : 512;

        for (int i = 0; i < 8; ++i)
        {
            combs[i].line.maxLen = maxLength(combs[i].baseLen);
            combs[i].line.buf.assign(combs[i].line.maxLen, 0.0f);
            combs[i].line.idx = 0;
            combs[i].lpState = 0.0f;
        }
        for (int i = 0; i < 4; ++i)
        {
            allpasses[i].line.maxLen = maxLength(allpasses[i].baseLen);
            allpasses[i].line.buf.assign(allpasses[i].line.maxLen, 0.0f);
            allpasses[i].line.idx = 0;
        }

        const int preMax = (int)std::ceil(0.250 * sr) + 1;
        for (int i = 0; i < 2; ++i)
        {
            predelays[i].maxLen = preMax;
            predelays[i].buf.assign(preMax, 0.0f);
            predelays[i].wIdx = 0;
            predelays[i].delaySamples = 0;
        }

        mixCoef = 1.0f - std::exp(-1.0f / (0.015f * (float)sr));
        bypass.prepare(sr);
        decayCur = decayTarget;
        dampCur = dampTarget;
        mixCur = mixTarget;
        updateCoefficients();
    }

    void reset()
    {
        for (int i = 0; i < 8; ++i)
        {
            for (size_t j = 0; j < combs[i].line.buf.size(); ++j) combs[i].line.buf[j] = 0.0f;
            combs[i].line.idx = 0;
            combs[i].lpState = 0.0f;
        }
        for (int i = 0; i < 4; ++i)
        {
            for (size_t j = 0; j < allpasses[i].line.buf.size(); ++j) allpasses[i].line.buf[j] = 0.0f;
            allpasses[i].line.idx = 0;
        }
        for (int i = 0; i < 2; ++i)
        {
            for (size_t j = 0; j < predelays[i].buf.size(); ++j) predelays[i].buf[j] = 0.0f;
            predelays[i].wIdx = 0;
        }
        decayCur = decayTarget;
        dampCur = dampTarget;
        mixCur = mixTarget;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        if (n == 0 || combs[0].line.buf.empty()) return;

        updateCoefficients();

        const int ch = buffer.getNumChannels();
        if (ch == 1)
            processMono(buffer.getWritePointer(0), n);
        else if (ch >= 2)
            processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }

    void setEnabled(bool on) { bypass.setEnabled(on); }
    void setSize(float v) { size = clamp(v, 0.1f, 1.0f); }
    void setDecaySec(float v) { decayTarget = clamp(v, 0.2f, 10.0f); }
    void setDamping(float v) { dampTarget = clamp(v, 0.0f, 1.0f); }
    void setWidth(float v) { width = clamp(v, 0.0f, 1.0f); }
    void setMix(float v) { mixTarget = clamp(v, 0.0f, 1.0f); }
    void setPreDelayMs(float v) { preDelayMs = clamp(v, 0.0f, 250.0f); }

private:
    struct DelayLine
    {
        std::vector<float> buf;
        int maxLen = 0;
        int idx = 0;
        int curLen = 0;
    };

    struct Comb
    {
        int baseLen = 0;
        DelayLine line;
        float lpState = 0.0f;
        float fb = 0.0f;
        float lpCoef = 0.0f;
    };

    struct Allpass
    {
        int baseLen = 0;
        DelayLine line;
    };

    struct PreDelay
    {
        std::vector<float> buf;
        int maxLen = 0;
        int wIdx = 0;
        int delaySamples = 0;

        float next(float in)
        {
            buf[wIdx] = in;
            int r = wIdx - delaySamples;
            if (r < 0) r += maxLen;
            const float out = buf[r];
            if (++wIdx >= maxLen) wIdx = 0;
            return out;
        }
    };

    static float clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    int maxLength(int base) const
    {
        const int len = (int)std::lround((double)base * sr / 44100.0);
        return len < 8 ? 8 : len;
    }

    int scaledLength(int base) const
    {
        const int len = (int)std::lround((double)base * sr / 44100.0 * size);
        return len < 8 ? 8 : len;
    }

    void updateCoefficients()
    {
        const float blockSec = (float)bs / (float)sr;
        const float k = 1.0f - std::exp(-blockSec / 0.020f);
        decayCur += (decayTarget - decayCur) * k;
        dampCur += (dampTarget - dampCur) * k;

        const float fc = 200.0f * std::pow(100.0f, 1.0f - dampCur);
        const float g = 1.0f - std::exp(-6.2831853f * fc / (float)sr);

        for (int i = 0; i < 8; ++i)
        {
            const int len = scaledLength(combs[i].baseLen);
            combs[i].line.curLen = len;
            const float delaySec = (float)len / (float)sr;
            const float fb = std::exp(-6.9078f * delaySec / decayCur);
            combs[i].fb = fb > 0.999f ? 0.999f : fb;
            combs[i].lpCoef = g;
        }

        for (int i = 0; i < 4; ++i)
            allpasses[i].line.curLen = scaledLength(allpasses[i].baseLen);

        int ps = (int)std::lround((double)preDelayMs * 0.001 * sr);
        if (ps > predelays[0].maxLen - 1) ps = predelays[0].maxLen - 1;
        if (ps < 0) ps = 0;
        predelays[0].delaySamples = ps;
        predelays[1].delaySamples = ps;
    }

    float allpassProcess(Allpass& a, float in)
    {
        const float out = a.line.buf[a.line.idx];
        const float y = out - in;
        a.line.buf[a.line.idx] = in + 0.5f * out;
        if (++a.line.idx >= a.line.curLen) a.line.idx = 0;
        return y;
    }

    void processMono(float* data, int n)
    {
        const float g = 0.015f;
        for (int s = 0; s < n; ++s)
        {
            const float dry = data[s];
            const float in = dry * g;
            float acc = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                Comb& c = combs[i];
                const float out = c.line.buf[c.line.idx];
                const float lp = c.lpState + c.lpCoef * (out - c.lpState);
                c.lpState = lp;
                c.line.buf[c.line.idx] = in + lp * c.fb;
                if (++c.line.idx >= c.line.curLen) c.line.idx = 0;
                acc += out;
            }
            float wet = acc;
            wet = allpassProcess(allpasses[0], wet);
            wet = allpassProcess(allpasses[1], wet);
            const float bg = bypass.next();
            const float wetOut = predelays[0].next(wet * bg);
            mixCur += (mixTarget - mixCur) * mixCoef;
            data[s] = dry + (wetOut - dry) * mixCur;
        }
    }

    void processStereo(float* L, float* R, int n)
    {
        const float g = 0.015f;
        const float w1 = 0.5f * (1.0f + width);
        const float w2 = 0.5f * (1.0f - width);
        for (int s = 0; s < n; ++s)
        {
            const float dryL = L[s], dryR = R[s];
            const float inL = dryL * g;
            const float inR = dryR * g;
            float accL = 0.0f;
            float accR = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                Comb& c = combs[i];
                const float out = c.line.buf[c.line.idx];
                const float lp = c.lpState + c.lpCoef * (out - c.lpState);
                c.lpState = lp;
                c.line.buf[c.line.idx] = inL + lp * c.fb;
                if (++c.line.idx >= c.line.curLen) c.line.idx = 0;
                accL += out;
            }
            for (int i = 4; i < 8; ++i)
            {
                Comb& c = combs[i];
                const float out = c.line.buf[c.line.idx];
                const float lp = c.lpState + c.lpCoef * (out - c.lpState);
                c.lpState = lp;
                c.line.buf[c.line.idx] = inR + lp * c.fb;
                if (++c.line.idx >= c.line.curLen) c.line.idx = 0;
                accR += out;
            }
            float wetL = accL;
            float wetR = accR;
            wetL = allpassProcess(allpasses[0], wetL);
            wetL = allpassProcess(allpasses[1], wetL);
            wetR = allpassProcess(allpasses[2], wetR);
            wetR = allpassProcess(allpasses[3], wetR);
            const float wL = w1 * wetL + w2 * wetR;
            const float wR = w2 * wetL + w1 * wetR;
            const float bg = bypass.next();
            const float wetOutL = predelays[0].next(wL);
            const float wetOutR = predelays[1].next(wR);
            mixCur += (mixTarget - mixCur) * mixCoef;
            const float m = mixCur * bg;
            L[s] = dryL + (wetOutL - dryL) * m;
            R[s] = dryR + (wetOutR - dryR) * m;
        }
    }

    Comb combs[8];
    Allpass allpasses[4];
    PreDelay predelays[2];
    SmoothBypass bypass;

    double sr = 44100.0;
    int bs = 512;
    float size = 0.5f;
    float decayTarget = 2.0f;
    float decayCur = 2.0f;
    float dampTarget = 0.5f;
    float dampCur = 0.5f;
    float width = 1.0f;
    float mixTarget = 0.5f;
    float mixCur = 0.5f;
    float preDelayMs = 0.0f;
    float mixCoef = 0.0f;
};

} // namespace agm
