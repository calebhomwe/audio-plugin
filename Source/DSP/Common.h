#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace agm {

inline float dbToGain(float db) { return juce::Decibels::decibelsToGain(db); }
inline float gainToDb(float g) { return juce::Decibels::gainToDecibels(g); }

inline float sanitize(float x, float fallback = 0.0f) { return std::isfinite(x) ? x : fallback; }

class OnePole
{
public:
    void prepare(double sampleRate, float timeMs)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        setTimeMs(timeMs);
        state = target;
    }

    void setTimeMs(float ms)
    {
        const float t = ms < 0.2f ? 0.2f : ms;
        coef = 1.0f - std::exp(-1.0f / (0.001f * t * (float)sr));
    }

    void setTarget(float v) { target = sanitize(v); }

    void snapTo(float v)
    {
        target = sanitize(v);
        state = target;
    }

    float next()
    {
        state += (target - state) * coef;
        if (std::fabs(target - state) <= 1e-6f * (1.0f + std::fabs(target)))
            state = target;
        return state;
    }

    float current() const { return state; }
    bool settled() const { return state == target; }

private:
    double sr = 44100.0;
    float coef = 0.01f;
    float state = 0.0f;
    float target = 0.0f;
};

class SmoothBypass
{
public:
    void prepare(double sampleRate, float ms = 15.0f)
    {
        const double r = sampleRate > 1.0 ? sampleRate : 44100.0;
        const float t = ms < 0.5f ? 0.5f : (ms > 500.0f ? 500.0f : ms);
        coef = 1.0f - std::exp(-1.0f / (t * 0.001f * (float)r));
        current = target;
    }

    void setEnabled(bool on) { target = on ? 1.0f : 0.0f; }

    float next()
    {
        current += (target - current) * coef;
        if (std::fabs(target - current) < 1e-5f)
            current = target;
        return current;
    }

    bool fullyOff() const { return current < 0.0001f && target == 0.0f; }

private:
    float coef = 0.0015f;
    float current = 1.0f;
    float target = 1.0f;
};

} // namespace agm
