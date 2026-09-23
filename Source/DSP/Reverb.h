#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace agm {

class Reverb
{
public:
    static constexpr int numCombs = 16;

    Reverb()
    {
        // Eight mutually-prime lengths per stereo half, evenly log-spaced over a
        // 1:1.95 range; the halves interleave so each channel spans the whole range.
        // The old set (1116 1188 1277 1356 1422 1491 1557 1617, the Freeverb
        // numbers) had 21 of its 28 pairs sharing a common factor - seven of the
        // eight were divisible by 3 - and the stereo split left each channel only
        // four lines over a 1.2:1 spread, which is the sparse low-frequency modal
        // density the audit measured as spectral tilt. Count is what fixes it:
        // 8 lines flatten to 4.3-5.1 dB however well conditioned, 16 reach 2.4 dB.
        const int combLens[numCombs] = {
            1117, 1279, 1447, 1601, 1741, 1877, 2029, 2179,     // left half
            1193, 1361, 1523, 1669, 1811, 1949, 2099, 2251 };   // right half
        const int apLens[4] = { 556, 441, 341, 225 };
        for (int i = 0; i < numCombs; ++i) combs[i].baseLen = combLens[i];
        for (int i = 0; i < 4; ++i) allpasses[i].baseLen = apLens[i];
    }

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        bs = blockSize > 0 ? blockSize : 512;

        for (int i = 0; i < numCombs; ++i)
        {
            combs[i].line.maxLen = maxLength(combs[i].baseLen);
            combs[i].line.buf.assign((size_t)combs[i].line.maxLen, 0.0f);
            combs[i].line.idx = 0;
            combs[i].lpState = 0.0f;
        }
        for (int i = 0; i < 4; ++i)
        {
            allpasses[i].line.maxLen = maxLength(allpasses[i].baseLen);
            allpasses[i].line.buf.assign((size_t)allpasses[i].line.maxLen, 0.0f);
            allpasses[i].line.idx = 0;
        }

        const int preMax = (int)std::ceil(0.250 * sr) + 2;
        for (int i = 0; i < 2; ++i)
        {
            predelays[i].maxLen = preMax;
            predelays[i].buf.assign((size_t)preMax, 0.0f);
            predelays[i].wIdx = 0;
        }
        preDelayF = maxPreDelaySamples();

        const float srF = (float)sr;
        const float blockSec = (float)bs / srF;
        mixCoef = 1.0f - std::exp(-1.0f / (0.015f * srF));
        paramCoef = 1.0f - std::exp(-blockSec / 0.020f);
        sizeCoef = 1.0f - std::exp(-blockSec / 0.060f);
        bypass.prepare(sr);
        decayCur = decayTarget;
        dampCur = dampTarget;
        mixCur = mixTarget;
        sizeCur = size;
        widthCur = width;
        updateCoefficients();
    }

    void reset()
    {
        clearTank();
        for (int i = 0; i < 2; ++i)
        {
            std::fill(predelays[i].buf.begin(), predelays[i].buf.end(), 0.0f);
            predelays[i].wIdx = 0;
        }
        decayCur = decayTarget;
        dampCur = dampTarget;
        mixCur = mixTarget;
        sizeCur = size;
        widthCur = width;
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
        int curLen = 8;

        void setLength(int len)
        {
            curLen = len;
            if (idx >= len)
                idx %= len;
        }
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

        float next(float in, float delayF)
        {
            buf[(size_t)wIdx] = in;
            // delayF is a smoothed value and can sit at e.g. 1e-7: wIdx - 1e-7 rounds
            // to wIdx in float, and after the "+ maxLen" wrap it rounds to maxLen
            // itself, one past the buffer (found by AddressSanitizer). Wrap on the
            // integer index instead of trusting the float.
            float rp = (float)wIdx - (delayF > 0.0f ? delayF : 0.0f);
            if (rp < 0.0f)
                rp += (float)maxLen;
            int i0 = (int)rp;
            if (i0 >= maxLen)
                i0 -= maxLen;
            if (i0 < 0)
                i0 = 0;
            const float f = juce::jlimit(0.0f, 1.0f, rp - (float)i0);
            int i1 = i0 + 1;
            if (i1 >= maxLen)
                i1 = 0;
            const float out = buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
            if (++wIdx >= maxLen)
                wIdx = 0;
            return out;
        }
    };

    // The tank's wet level must not depend on how many comb lines the bank has, or
    // every preset's wet/dry balance moves. The lines are mutually prime, so their
    // outputs sum incoherently and the level goes as sqrt(lines): the scale is
    // therefore 1/sqrt(lines summed), referenced to the 8-line bank this replaced
    // (mono summed 8 lines at 0.5, each stereo half summed 4 at 1.0).
    // Measured wet RMS, mix 1.0, 0.3 full-scale noise, 8 lines -> 16 lines:
    //   stereo  -45.15 -> -45.26 dBFS (decay 0.5), -44.31 -> -44.42 (2.0),
    //           -40.30 -> -40.68 (5.0)
    //   mono    -48.20 -> -48.27,       -47.31 -> -47.38,       -43.43 -> -43.60
    // i.e. within 0.11 dB except 0.39 dB at the longest decay. Without the
    // normalisation the wet level fell 3.1 dB across the board.
    static constexpr float invSqrt2 = 0.70710678f;
    static constexpr float monoCombScale = 0.5f * invSqrt2;     // 0.5 * sqrt(8/16)
    static constexpr float stereoCombScale = invSqrt2;          // 1.0 * sqrt(4/8)

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
        const int len = (int)std::lround((double)base * sr / 44100.0 * (double)sizeCur);
        return len < 8 ? 8 : len;
    }

    float maxPreDelaySamples() const
    {
        const float v = (float)std::lround((double)preDelayMs * 0.001 * sr);
        const float lim = (float)(predelays[0].maxLen - 2);
        return v < lim ? v : lim;
    }

    void updateCoefficients()
    {
        decayCur += (decayTarget - decayCur) * paramCoef;
        dampCur += (dampTarget - dampCur) * paramCoef;
        sizeCur += (size - sizeCur) * sizeCoef;
        widthCur += (width - widthCur) * paramCoef;

        const float srF = (float)sr;
        const float fc = 200.0f * std::pow(100.0f, 1.0f - dampCur);
        const float g = 1.0f - std::exp(-6.2831853f * fc / srF);

        for (int i = 0; i < numCombs; ++i)
        {
            Comb& c = combs[i];
            const int len = scaledLength(c.baseLen);
            c.line.setLength(len);
            const float delaySec = (float)len / srF;
            const float fb = std::exp(-6.9078f * delaySec / decayCur);
            c.fb = fb > 0.999f ? 0.999f : fb;
            c.lpCoef = g;
        }

        for (int i = 0; i < 4; ++i)
            allpasses[i].line.setLength(scaledLength(allpasses[i].baseLen));

        preDelayF += (maxPreDelaySamples() - preDelayF) * paramCoef;
    }

    float combProcess(Comb& c, float in)
    {
        DelayLine& dl = c.line;
        const float out = dl.buf[(size_t)dl.idx];
        const float lp = c.lpState + c.lpCoef * (out - c.lpState);
        c.lpState = lp;
        float v = in + lp * c.fb;
        if (!std::isfinite(v))
        {
            clearTank();
            v = 0.0f;
        }
        else if (v > -1.0e-24f && v < 1.0e-24f)
            v = 0.0f;
        dl.buf[(size_t)dl.idx] = v;
        if (++dl.idx >= dl.curLen)
            dl.idx = 0;
        return out;
    }

    float allpassProcess(Allpass& a, float in)
    {
        DelayLine& dl = a.line;
        const float out = dl.buf[(size_t)dl.idx];
        dl.buf[(size_t)dl.idx] = in + 0.5f * out;
        if (++dl.idx >= dl.curLen)
            dl.idx = 0;
        return 0.75f * out - 0.5f * in;
    }

    void clearTank()
    {
        for (auto& c : combs)
        {
            std::fill(c.line.buf.begin(), c.line.buf.end(), 0.0f);
            c.line.idx = 0;
            c.lpState = 0.0f;
        }
        for (auto& a : allpasses)
        {
            std::fill(a.line.buf.begin(), a.line.buf.end(), 0.0f);
            a.line.idx = 0;
        }
    }

    void processMono(float* data, int n)
    {
        const float inGain = 0.015f;
        for (int s = 0; s < n; ++s)
        {
            float dry = data[s];
            if (!std::isfinite(dry))
                dry = 0.0f;
            // Pre-delay sits in FRONT of the tank: it is the gap between the dry
            // sound and the tank's onset, so it must delay what goes in. Behind the
            // tank it re-times the ringing tail as well, which is audible the moment
            // the knob is automated.
            const float in = predelays[0].next(dry, preDelayF) * inGain;
            float acc = 0.0f;
            for (int i = 0; i < numCombs; ++i)
                acc += combProcess(combs[i], in);
            float wet = acc * monoCombScale;
            wet = allpassProcess(allpasses[0], wet);
            wet = allpassProcess(allpasses[1], wet);
            wet = allpassProcess(allpasses[2], wet);
            wet = allpassProcess(allpasses[3], wet);
            const float bg = bypass.next();
            mixCur += (mixTarget - mixCur) * mixCoef;
            const float m = mixCur * bg;
            data[s] = dry + (wet - dry) * m;
        }
    }

    void processStereo(float* L, float* R, int n)
    {
        const float inGain = 0.015f;
        const float w1 = 0.5f * (1.0f + widthCur);
        const float w2 = 0.5f * (1.0f - widthCur);
        for (int s = 0; s < n; ++s)
        {
            float dryL = L[s], dryR = R[s];
            if (!std::isfinite(dryL))
                dryL = 0.0f;
            if (!std::isfinite(dryR))
                dryR = 0.0f;
            const float inL = predelays[0].next(dryL, preDelayF) * inGain;
            const float inR = predelays[1].next(dryR, preDelayF) * inGain;
            float accL = 0.0f;
            float accR = 0.0f;
            for (int i = 0; i < numCombs / 2; ++i)
                accL += combProcess(combs[i], inL);
            for (int i = numCombs / 2; i < numCombs; ++i)
                accR += combProcess(combs[i], inR);
            accL *= stereoCombScale;
            accR *= stereoCombScale;
            float wetL = allpassProcess(allpasses[0], accL);
            wetL = allpassProcess(allpasses[1], wetL);
            float wetR = allpassProcess(allpasses[2], accR);
            wetR = allpassProcess(allpasses[3], wetR);
            const float mixL = w1 * wetL + w2 * wetR;
            const float mixR = w2 * wetL + w1 * wetR;
            const float bg = bypass.next();
            mixCur += (mixTarget - mixCur) * mixCoef;
            const float m = mixCur * bg;
            L[s] = dryL + (mixL - dryL) * m;
            R[s] = dryR + (mixR - dryR) * m;
        }
    }

    Comb combs[numCombs];
    Allpass allpasses[4];
    PreDelay predelays[2];
    SmoothBypass bypass;

    double sr = 44100.0;
    int bs = 512;
    float size = 0.5f;
    float sizeCur = 0.5f;
    float decayTarget = 2.0f;
    float decayCur = 2.0f;
    float dampTarget = 0.5f;
    float dampCur = 0.5f;
    float width = 1.0f;
    float widthCur = 1.0f;
    float mixTarget = 0.5f;
    float mixCur = 0.5f;
    float preDelayMs = 0.0f;
    float preDelayF = 0.0f;
    float mixCoef = 0.0f;
    float paramCoef = 0.0f;
    float sizeCoef = 0.0f;
};

} // namespace agm
