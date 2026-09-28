#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace agm {

// ---------------------------------------------------------------------------
// Feedback delay network, offered as an alternative tank to the comb bank.
//
// Three passes have now tried to flatten the comb bank's spectral tilt by
// choosing better comb lengths, and the measurements said the tilt is a property
// of the topology: parallel combs each contribute one harmonic series of modes,
// so the low-frequency modal density is whatever the line count gives you and no
// conditioning moves it much. An FDN is the other standard answer - N delay
// lines recirculated through an orthogonal matrix, so every line feeds every
// other one and the mode set is the whole network's, not a sum of independent
// combs.
//
//   - 16 mutually-prime line lengths over 1:2, exactly the range the comb bank
//     uses, so the two are compared at the same memory and the same mean delay.
//   - The feedback matrix is a 16-point Hadamard scaled by 1/sqrt(16): exactly
//     orthogonal, so the matrix contributes no gain of its own and the decay is
//     set only by the per-line gains. It costs 64 adds per sample via the fast
//     Walsh-Hadamard transform - no multiplies at all.
//   - One damping one-pole per line, the same corner mapping as the comb bank.
//   - Input and output mixing use different Hadamard rows, which are orthogonal,
//     so the two outputs are built from differently-signed sums of the same lines.
//
// Same public API as Reverb, selected at build time by MIXAGENT_REVERB_FDN so
// both tanks can be measured against the same probes. See AUDIT.md for the
// measurements that decided which one ships.
// ---------------------------------------------------------------------------
class ReverbFDN
{
public:
    static constexpr int numLines = 16;

    ReverbFDN()
    {
        // 16 distinct primes, log-spaced over 1117..2251 - the comb bank's range.
        const int lens[numLines] = {
            1117, 1171, 1229, 1283, 1361, 1409, 1481, 1549,
            1621, 1699, 1783, 1867, 1951, 2053, 2143, 2251 };
        for (int i = 0; i < numLines; ++i)
            lines[i].baseLen = lens[i];
    }

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        bs = blockSize > 0 ? blockSize : 512;

        for (int i = 0; i < numLines; ++i)
        {
            lines[i].maxLen = maxLength(lines[i].baseLen);
            lines[i].buf.assign((size_t)lines[i].maxLen, 0.0f);
            lines[i].idx = 0;
            lines[i].curLen = lines[i].maxLen;
            lines[i].lpState = 0.0f;
        }

        const int apLens[4] = { 556, 441, 341, 225 };
        for (int i = 0; i < 4; ++i)
            diffusers[i].prepare((int)std::lround((double)apLens[i] * sr / 44100.0));

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
        if (n == 0 || lines[0].buf.empty()) return;

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
    struct Line
    {
        std::vector<float> buf;
        int baseLen = 0;
        int maxLen = 0;
        int idx = 0;
        int curLen = 8;
        float lpState = 0.0f;
        float fb = 0.0f;
        float lpCoef = 0.0f;
    };

    // Unity-gain Schroeder allpass, H(z) = (z^-m - 0.5)/(1 - 0.5 z^-m). Verified
    // in wave 4 as exactly flat at every frequency, which is why it is reused here.
    struct Allpass
    {
        std::vector<float> buf;
        int len = 0;
        int idx = 0;

        void prepare(int n)
        {
            len = n < 8 ? 8 : n;
            buf.assign((size_t)len, 0.0f);
            idx = 0;
        }

        float next(float in)
        {
            const float out = buf[(size_t)idx];
            buf[(size_t)idx] = in + 0.5f * out;
            if (++idx >= len)
                idx = 0;
            return 0.75f * out - 0.5f * in;
        }
    };

    struct PreDelay
    {
        std::vector<float> buf;
        int maxLen = 0;
        int wIdx = 0;

        float next(float in, float delayF)
        {
            buf[(size_t)wIdx] = in;
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

        for (int i = 0; i < numLines; ++i)
        {
            Line& l = lines[i];
            const int len = scaledLength(l.baseLen);
            l.curLen = len;
            if (l.idx >= len)
                l.idx %= len;
            const float delaySec = (float)len / srF;
            const float fb = std::exp(-6.9078f * delaySec / decayCur);
            l.fb = fb > 0.999f ? 0.999f : fb;
            l.lpCoef = g;
        }

        preDelayF += (maxPreDelaySamples() - preDelayF) * paramCoef;
    }

    // In-place 16-point fast Walsh-Hadamard transform, scaled by 1/sqrt(16) so
    // the matrix is orthonormal: 64 adds, no multiplies beyond the final scale.
    static void hadamard16(float* v)
    {
        for (int step = 1; step < numLines; step <<= 1)
            for (int i = 0; i < numLines; i += step << 1)
                for (int j = i; j < i + step; ++j)
                {
                    const float a = v[j], b = v[j + step];
                    v[j] = a + b;
                    v[j + step] = a - b;
                }
        for (int i = 0; i < numLines; ++i)
            v[i] *= 0.25f;
    }

    // Input and output taps are four different Walsh sign patterns, mutually
    // orthogonal, so each output is a differently-signed sum of the same lines.
    // Five other mixings were measured and every one was worse; the numbers and
    // the patterns are in AUDIT.md. Splitting the lines into halves the way the
    // comb bank does is much worse here - 7.10 dB against 5.05 - because then only
    // half the lines get direct input and their early echoes arrive in phase.
    static float inWeight(int i, int which)
    {
        return which == 0 ? ((i & 1) == 0 ? 1.0f : -1.0f)
                          : ((i & 2) == 0 ? 1.0f : -1.0f);
    }

    static float outWeight(int i, int which)
    {
        return which == 0 ? ((i & 4) == 0 ? 1.0f : -1.0f)
                          : ((i & 8) == 0 ? 1.0f : -1.0f);
    }

    // One sample of the network. `inB` is zero on the mono path, which then uses
    // one input tap and one output tap - structurally one channel of the stereo
    // path. `outA` and `outB` are the two differently-signed sums of the lines.
    void tankStep(float inA, float inB, float& outA, float& outB)
    {
        float v[numLines];
        float a = 0.0f, b = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            Line& l = lines[i];
            const float out = l.buf[(size_t)l.idx];
            const float lp = l.lpState + l.lpCoef * (out - l.lpState);
            l.lpState = lp;
            v[i] = lp * l.fb;
            a += outWeight(i, 0) * out;
            b += outWeight(i, 1) * out;
        }
        hadamard16(v);
        for (int i = 0; i < numLines; ++i)
        {
            Line& l = lines[i];
            float s = v[i] + inA * inWeight(i, 0) + inB * inWeight(i, 1);
            if (!std::isfinite(s))
            {
                clearTank();
                s = 0.0f;
            }
            else if (s > -1.0e-24f && s < 1.0e-24f)
                s = 0.0f;
            l.buf[(size_t)l.idx] = s;
            if (++l.idx >= l.curLen)
                l.idx = 0;
        }
        outA = a * outScale;
        outB = b * outScale;
    }

    void clearTank()
    {
        for (auto& l : lines)
        {
            std::fill(l.buf.begin(), l.buf.end(), 0.0f);
            l.idx = 0;
            l.lpState = 0.0f;
        }
        for (auto& a : diffusers)
        {
            std::fill(a.buf.begin(), a.buf.end(), 0.0f);
            a.idx = 0;
        }
    }

    void processMono(float* data, int n)
    {
        for (int s = 0; s < n; ++s)
        {
            float dry = data[s];
            if (!std::isfinite(dry))
                dry = 0.0f;
            float in = predelays[0].next(dry, preDelayF) * inGain;
            in = diffusers[1].next(diffusers[0].next(in));
            float wA = 0.0f, wB = 0.0f;
            tankStep(in, 0.0f, wA, wB);
            const float wet = wA * monoTrim;
            const float bg = bypass.next();
            mixCur += (mixTarget - mixCur) * mixCoef;
            const float m = mixCur * bg;
            data[s] = dry + (wet - dry) * m;
        }
    }

    void processStereo(float* L, float* R, int n)
    {
        const float w1 = 0.5f * (1.0f + widthCur);
        const float w2 = 0.5f * (1.0f - widthCur);
        for (int s = 0; s < n; ++s)
        {
            float dryL = L[s], dryR = R[s];
            if (!std::isfinite(dryL))
                dryL = 0.0f;
            if (!std::isfinite(dryR))
                dryR = 0.0f;
            float inL = predelays[0].next(dryL, preDelayF) * inGain;
            float inR = predelays[1].next(dryR, preDelayF) * inGain;
            inL = diffusers[1].next(diffusers[0].next(inL));
            inR = diffusers[3].next(diffusers[2].next(inR));
            float wetL = 0.0f, wetR = 0.0f;
            tankStep(inL, inR, wetL, wetR);
            const float mixL = w1 * wetL + w2 * wetR;
            const float mixR = w2 * wetL + w1 * wetR;
            const float bg = bypass.next();
            mixCur += (mixTarget - mixCur) * mixCoef;
            const float m = mixCur * bg;
            L[s] = dryL + (mixL - dryL) * m;
            R[s] = dryR + (mixR - dryR) * m;
        }
    }

    Line lines[numLines];
    Allpass diffusers[4];
    PreDelay predelays[2];
    SmoothBypass bypass;

    // Input trim and output scale are set so the wet level matches the comb bank
    // this is compared against: a preset's wet/dry balance must not move because
    // the tank behind it changed. See AUDIT.md for the measured levels.
    static constexpr float inGain = 0.015f;
    static constexpr float outScale = 0.3625f;   // 0.25, plus the 3.2 dB measured
    static constexpr float monoTrim = 1.0f;

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
