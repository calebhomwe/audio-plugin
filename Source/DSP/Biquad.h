#pragma once
#include <cmath>

namespace agm {

class Biquad
{
public:
    void prepare(double sampleRate) { fs = sampleRate; }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    float process(float x)
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void setHighPass(float freq, float q = 0.70710678f)
    {
        const float w = w0(freq), c = std::cos(w), s = std::sin(w);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f + c) / 2.0f; b1 = -(1.0f + c); b2 = (1.0f + c) / 2.0f;
        a1 = -2.0f * c; a2 = 1.0f - alpha;
        normalize(a0);
    }

    void setLowPass(float freq, float q = 0.70710678f)
    {
        const float w = w0(freq), c = std::cos(w), s = std::sin(w);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - c) / 2.0f; b1 = 1.0f - c; b2 = (1.0f - c) / 2.0f;
        a1 = -2.0f * c; a2 = 1.0f - alpha;
        normalize(a0);
    }

    void setLowShelf(float freq, float gainDb, float s = 0.9f) { shelf(freq, gainDb, s, false); }
    void setHighShelf(float freq, float gainDb, float s = 0.9f) { shelf(freq, gainDb, s, true); }

    void setPeaking(float freq, float gainDb, float q)
    {
        const float w = w0(freq), c = std::cos(w), s = std::sin(w);
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha / A;
        b0 = 1.0f + alpha * A; b1 = -2.0f * c; b2 = 1.0f - alpha * A;
        a1 = -2.0f * c; a2 = 1.0f - alpha / A;
        normalize(a0);
    }

    float magnitudeDbAt(float freq) const
    {
        const float w = w0(freq);
        const float c1 = std::cos(w), c2 = std::cos(2.0f * w);
        const float s1 = std::sin(w), s2 = std::sin(2.0f * w);
        const float reN = b0 + b1 * c1 + b2 * c2, imN = -b1 * s1 - b2 * s2;
        const float reD = 1.0f + a1 * c1 + a2 * c2, imD = -a1 * s1 - a2 * s2;
        const float mag = std::sqrt((reN * reN + imN * imN) / (reD * reD + imD * imD) + 1e-12f);
        return 20.0f * std::log10(mag);
    }

    void dbg() const
    {
        std::cout << "coeffs " << b0 << " " << b1 << " " << b2 << " " << a1 << " " << a2
                  << " state " << x1 << " " << x2 << " " << y1 << " " << y2 << "\n";
    }

private:
    float w0(float f) const { return 2.0f * 3.14159265f * f / (float)fs; }
    void normalize(float a0)
    {
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    void shelf(float freq, float gainDb, float S, bool high)
    {
        const float w = w0(freq), c = std::cos(w), s = std::sin(w);
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float alpha = 0.5f * s * std::sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        const float beta = 2.0f * std::sqrt(A) * alpha;
        float a0;
        if (!high)
        {
            a0 = (A + 1.0f) + (A - 1.0f) * c + beta;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * c + beta);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * c - beta);
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * c);
            a2 = (A + 1.0f) + (A - 1.0f) * c - beta;
        }
        else
        {
            a0 = (A + 1.0f) - (A - 1.0f) * c + beta;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * c + beta);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * c - beta);
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c);
            a2 = (A + 1.0f) - (A - 1.0f) * c - beta;
        }
        normalize(a0);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    double fs = 44100.0;
};

} // namespace agm
