#pragma once
#include <cmath>

namespace agm {

class Biquad
{
public:
    void prepare(double sampleRate) { fs = sampleRate > 1.0 ? sampleRate : 44100.0; }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    float process(float x)
    {
        const float y = (float)(b0 * (double)x + b1 * (double)x1 + b2 * (double)x2
                                - a1 * (double)y1 - a2 * (double)y2) + antiDenormal;
        antiDenormal = -antiDenormal;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void setHighPass(float freq, float q = 0.70710678f)
    {
        double w, c, s;
        trig(freq, w, c, s);
        const double alpha = s / (2.0 * clampQ(q));
        const double a0 = 1.0 + alpha;
        apply((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, -2.0 * c, 1.0 - alpha, a0);
    }

    void setLowPass(float freq, float q = 0.70710678f)
    {
        double w, c, s;
        trig(freq, w, c, s);
        const double alpha = s / (2.0 * clampQ(q));
        const double a0 = 1.0 + alpha;
        apply((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, -2.0 * c, 1.0 - alpha, a0);
    }

    void setLowShelf(float freq, float gainDb, float s = 0.9f) { shelf(freq, gainDb, s, false); }
    void setHighShelf(float freq, float gainDb, float s = 0.9f) { shelf(freq, gainDb, s, true); }

    void setPeaking(float freq, float gainDb, float q)
    {
        double w, c, s;
        trig(freq, w, c, s);
        const double A = std::pow(10.0, clampDb(gainDb) / 40.0);
        const double alpha = s / (2.0 * clampQ(q));
        const double a0 = 1.0 + alpha / A;
        apply(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, -2.0 * c, 1.0 - alpha / A, a0);
    }

    float magnitudeDbAt(float freq) const
    {
        double w, c1, s1;
        trig(freq, w, c1, s1);
        const double c2 = c1 * c1 - s1 * s1;
        const double s2 = 2.0 * s1 * c1;
        const double reN = b0 + b1 * c1 + b2 * c2;
        const double imN = -(b1 * s1 + b2 * s2);
        const double reD = 1.0 + a1 * c1 + a2 * c2;
        const double imD = -(a1 * s1 + a2 * s2);
        const double num = reN * reN + imN * imN;
        const double den = reD * reD + imD * imD;
        if (num < 1e-30)
            return -200.0f;
        if (den < 1e-30)
            return 200.0f;
        const double db = 10.0 * std::log10(num / den);
        return (float)(db < -200.0 ? -200.0 : (db > 200.0 ? 200.0 : db));
    }

private:
    static double clampQ(double q) { return q < 0.05 ? 0.05 : (q > 50.0 ? 50.0 : q); }
    static double clampDb(double db) { return db < -60.0 ? -60.0 : (db > 60.0 ? 60.0 : db); }

    void trig(float freq, double& w, double& c, double& s) const
    {
        const double upper = 0.49 * fs;
        double f = freq;
        if (!(f > 1.0))
            f = 1.0;
        if (f > upper)
            f = upper > 1.0 ? upper : 1.0;
        w = 6.28318530717958647693 * f / fs;
        c = std::cos(w);
        s = std::sin(w);
    }

    void apply(double nb0, double nb1, double nb2, double na1, double na2, double a0)
    {
        const bool ok = std::isfinite(a0) && std::fabs(a0) > 1e-12
                     && std::isfinite(nb0) && std::isfinite(nb1) && std::isfinite(nb2)
                     && std::isfinite(na1) && std::isfinite(na2);
        if (!ok)
        {
            b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0;
            return;
        }
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    void shelf(float freq, float gainDb, float S, bool high)
    {
        double w, c, s;
        trig(freq, w, c, s);
        const double A = std::pow(10.0, clampDb(gainDb) / 40.0);
        const double slope = S < 0.1 ? 0.1 : (S > 1.0 ? 1.0 : (double)S);
        const double alpha = 0.5 * s * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double beta = 2.0 * std::sqrt(A) * alpha;
        if (!high)
        {
            const double a0 = (A + 1.0) + (A - 1.0) * c + beta;
            apply(A * ((A + 1.0) - (A - 1.0) * c + beta),
                  2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                  A * ((A + 1.0) - (A - 1.0) * c - beta),
                  -2.0 * ((A - 1.0) + (A + 1.0) * c),
                  (A + 1.0) + (A - 1.0) * c - beta, a0);
        }
        else
        {
            const double a0 = (A + 1.0) - (A - 1.0) * c + beta;
            apply(A * ((A + 1.0) + (A - 1.0) * c + beta),
                  -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                  A * ((A + 1.0) + (A - 1.0) * c - beta),
                  2.0 * ((A - 1.0) - (A + 1.0) * c),
                  (A + 1.0) - (A - 1.0) * c - beta, a0);
        }
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    float antiDenormal = 1e-17f;
    double fs = 44100.0;
};

} // namespace agm
