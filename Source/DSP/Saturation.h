#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "Biquad.h"
#include <cmath>
#include <algorithm>
#include <array>

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
        osMaxBlock = static_cast<size_t>(maxSamples);
        oversampler.reset();
        // The FIR half-band stages have an integer group delay; the bypass path
        // reproduces exactly that delay so the module's latency never changes.
        latency = (int)std::lround(oversampler.getLatencyInSamples());
        jassert(std::abs(oversampler.getLatencyInSamples() - (float)latency) < 1.0e-3f);
        jassert(latency < kMaxLatency);
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
        for (auto& r : dryRing) r.fill(0.0f);
        ringPos = 0;
        wasFullyOff = bypass.fullyOff();
    }

    int getLatencySamples() const { return latency; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || workBuffer.getNumSamples() <= 0)
            return;

        const bool fullyOff = bypass.fullyOff() || (mix == 0.0f && mixSmooth.settled());

        if (fullyOff)
        {
            if (!wasFullyOff)
            {
                tapeLP[0].reset();
                tapeLP[1].reset();
                exciterHP[0].reset();
                exciterHP[1].reset();
                dcServo[0] = 0.0f;
                dcServo[1] = 0.0f;
                wasFullyOff = true;
            }
            // Bit-exact pass-through, delayed by the oversampler's latency so the
            // host's compensation stays valid whether the module is on or off.
            const int chans = juce::jmin(numChannels, 2);
            if (latency <= 0)
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const int rp = ringPos;
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* p = buffer.getWritePointer(ch) + i;
                    const float in = std::isfinite(*p) ? *p : 0.0f;
                    auto& ring = dryRing[(size_t)ch];
                    *p = ring[(size_t)rp];
                    ring[(size_t)rp] = in;
                }
                ringPos = (rp + 1 < latency) ? rp + 1 : 0;
            }
            return;
        }

        if (wasFullyOff)
        {
            // Re-engaging: the FIR state is empty while the ring holds the last
            // `latency` input samples. Run that history through the oversampler
            // (output discarded) so the switch is seamless instead of a 1 ms gap.
            wasFullyOff = false;
            oversampler.reset();
            primeFromRing(numChannels);
        }

        // Hosts may deliver blocks larger than the size promised in prepare();
        // process in slices no larger than what the oversampler was sized for,
        // so nothing is (re)allocated on the audio thread.
        const int chunk = (int)std::min<size_t>((size_t)numSamples, osMaxBlock);
        for (int base = 0; base < numSamples; base += chunk)
        {
            const int n = std::min(chunk, numSamples - base);
            // keep the dry ring current so a later bypass continues seamlessly
            for (int i = 0; i < n; ++i)
            {
                const int rp = ringPos;
                dryRing[0][(size_t)rp] = buffer.getReadPointer(0)[base + i];
                dryRing[1][(size_t)rp] = buffer.getReadPointer(numChannels > 1 ? 1 : 0)[base + i];
                ringPos = (rp + 1 < latency) ? rp + 1 : 0;
            }
            processChunk(buffer, numChannels, base, n, false);
        }
    }

    void primeFromRing(int numChannels)
    {
        const int slice = (int)osMaxBlock;
        for (int done = 0; done < latency; done += slice)
        {
            const int n = juce::jmin(slice, latency - done);
            for (int i = 0; i < n; ++i)
            {
                // oldest sample first: ring[ringPos] is the oldest of the last `latency`
                const int idx = (ringPos + done + i) % latency;
                workBuffer.getWritePointer(0)[i] = dryRing[0][(size_t)idx];
                workBuffer.getWritePointer(1)[i] = dryRing[1][(size_t)idx];
            }
            juce::dsp::AudioBlock<float> blk(workBuffer);
            auto sub = blk.getSubBlock(0, (size_t)n);
            oversampler.processSamplesUp(sub);
            juce::ignoreUnused(numChannels);
            oversampler.processSamplesDown(sub);   // output discarded (workBuffer is scratch)
        }
    }

    void processChunk(juce::AudioBuffer<float>& buffer, int numChannels, int base, int numSamples, bool shapingOff)
    {
        jassert(numSamples <= workBuffer.getNumSamples());
        const float* srcL = buffer.getReadPointer(0) + base;
        const float* srcR = (numChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0)) + base;
        float* wL = workBuffer.getWritePointer(0);
        float* wR = workBuffer.getWritePointer(1);
        bool poisoned = false;
        for (int i = 0; i < numSamples; ++i)
        {
            // NaN/Inf must never enter the FIR state: scrub on the way in.
            const float l = srcL[i], r = srcR[i];
            const bool ok = std::isfinite(l) && std::isfinite(r);
            poisoned |= !ok;
            wL[i] = std::isfinite(l) ? l : 0.0f;
            wR[i] = std::isfinite(r) ? r : 0.0f;
        }
        if (poisoned)
            oversampler.reset();

        juce::dsp::AudioBlock<float> upBlock(workBuffer);
        auto upSub = upBlock.getSubBlock(0, static_cast<size_t>(numSamples));
        auto osBlock = oversampler.processSamplesUp(upSub);

        const int osNum = static_cast<int>(osBlock.getNumSamples());
        if (osNum <= 0)
            return;

        float* osL = osBlock.getChannelPointer(0);
        float* osR = osBlock.getChannelPointer(1);

        for (int i = 0; i < osNum && !shapingOff; ++i)
        {
            const float pre = driveGainSmooth.next();
            const float post = outGainSmooth.next();
            const float amount = bypass.next() * mixSmooth.next();
            const float modeMix = modeMixSmooth.next();

            const float inL = osL[i];
            const float inR = osR[i];

            if (amount > 0.0f)
            {
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

        juce::dsp::AudioBlock<float> downFull(buffer);
        if (numChannels == 1)
        {
            auto monoBlock = downFull.getSingleChannelBlock(0).getSubBlock(base, numSamples);
            oversampler.processSamplesDown(monoBlock);
        }
        else
        {
            auto stereoBlock = downFull.getSubsetChannelBlock(0, 2).getSubBlock(base, numSamples);
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

    // useIntegerLatency = true: the two FIR stages otherwise sum to a half-sample
    // group delay, which no host can compensate; JUCE pads it to a whole sample.
    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::AudioBuffer<float> workBuffer;
    size_t osMaxBlock = 1;
    static constexpr int kMaxLatency = 256;
    std::array<std::array<float, kMaxLatency>, 2> dryRing {};
    int ringPos = 0;
    int latency = 0;
    bool wasFullyOff = false;
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
