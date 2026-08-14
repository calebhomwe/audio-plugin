#pragma once
#include <juce_dsp/juce_dsp.h>

namespace agm {

inline float dbToGain(float db) { return juce::Decibels::decibelsToGain(db); }
inline float gainToDb(float g) { return juce::Decibels::gainToDecibels(g); }

class SmoothBypass
{
public:
    void prepare(double sampleRate, float ms = 15.0f)
    {
        sr = sampleRate;
        timeMs = ms;
        current = target;
    }
    void setEnabled(bool on) { target = on ? 1.0f : 0.0f; }
    float next()
    {
        const float coef = 1.0f - std::exp(-1.0f / (timeMs * 0.001f * (float)sr));
        current += (target - current) * coef;
        return current;
    }
    bool fullyOff() const { return current < 0.0001f && target == 0.0f; }

private:
    float current = 1.0f;
    float target = 1.0f;
    float timeMs = 15.0f;
    double sr = 44100.0;
};

} // namespace agm
