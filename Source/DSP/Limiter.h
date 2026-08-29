#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include <cmath>
#include <array>
#include <atomic>

namespace agm {

// Look-ahead limiter with true pre-peak anticipation.
//
// The block flows through a ring delay line of W = delaySamples slots. For
// every output sample (stream index s - W), the required gain is computed
// from the max |x| over the look-ahead window [s - W, s) — i.e. exactly the
// samples that will reach the output from now until W later. The window max
// is tracked per channel with a monotonic deque, so this is O(1) amortised
// per sample and exact (no histogram approximation).
//
// Ingest and emit are interleaved in one pass: the ring slot is read (it
// still holds the sample emerging now) before the new sample overwrites it,
// so the line stays correct even when numSamples > W.
//
// Gain shape: linear ramp down with >= kMinAttackMs (0.5ms) so the ramp
// always finishes inside the look-ahead window, ramp up with >= 50ms
// release. Because the ramp completes before the peak emerges, hard
// transients pass through the line already attenuated; the tanh soft knee
// remains only as a safety net against stale-peak edge cases.
class Limiter
{
    static constexpr int maxChannels = 2;
    static constexpr int maxDelay = 2048;
    static constexpr double kLookAheadSec = 0.003;
    static constexpr double kMinAttackMs = 0.5;
    static constexpr double kMinReleaseMs = 50.0;

public:
    void prepare(double sampleRate, int blockSize)
    {
        juce::ignoreUnused(blockSize);
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        delaySamples = juce::jlimit(1, maxDelay, juce::roundToInt(kLookAheadSec * sr));
        updateCoefs();
        clearState();
        bypass.prepare(sr);
        bypass.setEnabled(enabled);
    }

    void reset()
    {
        clearState();
        bypass.prepare(sr);
        bypass.setEnabled(enabled);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || delaySamples <= 0)
            return;

        const int chans = juce::jmin(numChannels, maxChannels);
        const int W = delaySamples;
        const float ceiling = juce::jmax(dbToGain(ceilingDb), 1e-4f);
        const float zoneStart = ceiling * 0.95f;
        const float zoneWidth = ceiling - zoneStart;
        const float zoneInv = 1.0f / zoneWidth;
        const float down = downSlope;
        const float up = upSlope;
        const float grC = grCoef;

        float env = currentGain;
        float grMeter = grSmoother.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t sIdx = gIdx + i;      // stream index of the sample entering now
            const int64_t outIdx = sIdx - W;    // stream index of the sample emerging now

            // Exact sliding-window max over [outIdx, sIdx): evict stale heads,
            // read the front. Deques only ever contain indices < sIdx because
            // ingest happens after emit, so this window is fully known.
            float level = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& dIdx = dqIdx[ch];
                auto& dVal = dqVal[ch];
                int& head = dqHead[ch];
                const int tail = dqTail[ch];
                while (head != tail && dIdx[(size_t)head] < outIdx)
                    head = (head + 1) % dqCap;
                const float m = (head != tail) ? dVal[(size_t)head] : 0.0f;
                if (m > level)
                    level = m;
            }

            const float req = level > ceiling ? ceiling / level : 1.0f;
            if (req < env)
            {
                env -= down;
                if (env < req)
                    env = req;
            }
            else
            {
                env += up;
                if (env > 1.0f)
                    env = 1.0f;
            }

            const float mix = bypass.next();

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = delayLines[ch];
                auto& dIdx = dqIdx[ch];
                auto& dVal = dqVal[ch];

                float s = buffer.getReadPointer(ch)[i];
                if (!std::isfinite(s))
                    s = 0.0f;

                const float delayed = line[(size_t)writePos[ch]]; // oldest sample, read before overwrite
                line[(size_t)writePos[ch]] = s;
                if (++writePos[ch] >= W)
                    writePos[ch] = 0;

                // Maintain monotonic (non-increasing) deque of |sample| indices.
                int head = dqHead[ch];
                int tail = dqTail[ch];
                const float a = std::fabs(s);
                while (tail != head)
                {
                    const int last = (tail - 1 + dqCap) % dqCap;
                    if (dVal[(size_t)last] > a)
                        break;
                    tail = last;
                }
                dIdx[(size_t)tail] = sIdx;
                dVal[(size_t)tail] = a;
                dqTail[ch] = (tail + 1) % dqCap;

                float out = delayed;
                if (mix > 0.0001f)
                {
                    float lim = delayed * env;
                    const float la = std::fabs(lim);
                    if (la > zoneStart)
                    {
                        const float t = (la - zoneStart) * zoneInv;
                        const float shaped = zoneStart + zoneWidth * std::tanh(t);
                        lim = lim >= 0.0f ? shaped : -shaped;
                    }
                    out = delayed + mix * (lim - delayed);
                }
                buffer.getWritePointer(ch)[i] = out;
            }

            grMeter += grC * (-gainToDb(env) - grMeter);
        }

        gIdx += numSamples;

        currentGain = std::isfinite(env) ? juce::jlimit(0.0f, 1.0f, env) : 1.0f;
        grSmoother.store(std::isfinite(grMeter) ? grMeter : 0.0f, std::memory_order_relaxed);
    }

    void setEnabled(bool on)
    {
        enabled = on;
        bypass.setEnabled(on);
    }

    void setCeilingDb(float db) { ceilingDb = juce::jlimit(-20.0f, 0.0f, db); }

    void setAttackMs(float ms)
    {
        attackMs = juce::jlimit(0.01f, 10.0f, ms);
        updateCoefs();
    }

    void setReleaseMs(float ms)
    {
        releaseMs = juce::jlimit(10.0f, 500.0f, ms);
        updateCoefs();
    }

    int getLatencySamples() const { return delaySamples; }

    float getGainReductionDb() const { return grSmoother.load(std::memory_order_relaxed); }

private:
    // capacity covers a full block of strictly-rising |sample| runs plus the
    // W-sample output lag; sized as 2*maxDelay*2 so even pathological
    // monotonic ramps across two blocks cannot overflow it.
    static constexpr int dqCap = maxDelay * 4;

    void updateCoefs()
    {
        const double fs = sr > 0.0 ? sr : 44100.0;
        // linear ramp down: clamp attack to >= 0.5ms and make sure the ramp
        // can traverse full gain within the look-ahead window (downSlope >= 1/W).
        const double atk = juce::jmax(kMinAttackMs * 0.001 * fs, (double)delaySamples);
        downSlope = (float)(1.0 / atk);
        // linear ramp up: never faster than 50ms
        const double rel = juce::jmax(kMinReleaseMs * 0.001 * fs, (double)releaseMs * 0.001 * fs);
        upSlope = (float)(1.0 / rel);
        grCoef = (float)(1.0 - std::exp(-1.0 / (50.0 * 0.001 * fs)));
    }

    void clearState()
    {
        for (auto& line : delayLines)
            line.fill(0.0f);
        writePos.fill(0);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            dqHead[ch] = 0;
            dqTail[ch] = 0;
            dqIdx[ch].fill(0);
            dqVal[ch].fill(0.0f);
        }
        gIdx = 0;
        currentGain = 1.0f;
        grSmoother.store(0.0f, std::memory_order_relaxed);
    }

    std::array<std::array<float, maxDelay>, maxChannels> delayLines{};
    std::array<int, maxChannels> writePos{ { 0, 0 } };
    // monotonic (non-increasing) deques of |sample| for the exact sliding window max
    std::array<std::array<int64_t, dqCap>, maxChannels> dqIdx {};
    std::array<std::array<float, dqCap>, maxChannels> dqVal {};
    std::array<int, maxChannels> dqHead{ { 0, 0 } };
    std::array<int, maxChannels> dqTail{ { 0, 0 } };
    int64_t gIdx = 0; // stream index one past the newest ingested sample
    double sr = 44100.0;
    int delaySamples = 0;
    float ceilingDb = -1.0f;
    float attackMs = 0.5f;
    float releaseMs = 100.0f;
    float downSlope = 1.0f;
    float upSlope = 0.0001f;
    float grCoef = 0.0f;
    float currentGain = 1.0f;
    std::atomic<float> grSmoother{ 0.0f };
    bool enabled = true;
    SmoothBypass bypass;
};

} // namespace agm
