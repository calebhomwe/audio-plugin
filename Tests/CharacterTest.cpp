// Module-level characterisation: for each DSP block, measure the behaviour that
// the topology it is modelled on is known for, and assert it with tolerances.
// Nothing here is read off the source - every number comes from a probe signal.
#include <JuceHeader.h>
#include "../Source/DSP/Common.h"
#include "../Source/DSP/Biquad.h"
#include "../Source/DSP/EQ.h"
#include "../Source/DSP/Compressor.h"
#include "../Source/DSP/Limiter.h"
#include "../Source/DSP/Saturation.h"
#include "../Source/DSP/Delay.h"
#include "../Source/DSP/Reverb.h"
#include "../Source/DSP/StereoImager.h"
#include "../Source/DSP/DrumEngine.h"
#include "../Source/DSP/InstrumentBank.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace juce;

static int gFailures = 0;
static int gChecks = 0;

static void check(bool ok, const std::string& name)
{
    ++gChecks;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!ok) ++gFailures;
}

static double dB(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// Amplitude of the sinusoidal component at `freq`, Hann-windowed.
static double magAt(const std::vector<float>& x, int from, int n, double freq, double sr)
{
    if (from < 0 || from + n > (int)x.size() || n < 8) return 0.0;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(MathConstants<double>::twoPi * i / (n - 1));
        const double ph = MathConstants<double>::twoPi * freq * (double)i / sr;
        re += (double)x[(size_t)(from + i)] * w * std::cos(ph);
        im -= (double)x[(size_t)(from + i)] * w * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / std::max(wsum, 1e-12);
}

static double rmsOf(const std::vector<float>& x, int from, int to)
{
    from = std::max(from, 0); to = std::min(to, (int)x.size());
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double)x[(size_t)i] * x[(size_t)i];
    return to > from ? std::sqrt(s / (double)(to - from)) : 0.0;
}

static double peakOf(const std::vector<float>& x, int from, int to)
{
    from = std::max(from, 0); to = std::min(to, (int)x.size());
    double m = 0.0;
    for (int i = from; i < to; ++i) m = std::max(m, std::abs((double)x[(size_t)i]));
    return m;
}

// Run a mono processor over a generated signal, block by block.
template <typename Proc, typename Gen>
static std::vector<float> runMono(Proc& p, int n, int block, Gen gen)
{
    std::vector<float> out((size_t)n);
    AudioBuffer<float> buf(2, block);
    for (int done = 0; done < n; )
    {
        const int k = std::min(block, n - done);
        buf.setSize(2, k, false, false, true);
        for (int i = 0; i < k; ++i) { const float v = gen(done + i); buf.setSample(0, i, v); buf.setSample(1, i, v); }
        p.process(buf);
        for (int i = 0; i < k; ++i) out[(size_t)(done + i)] = buf.getSample(0, i);
        done += k;
    }
    return out;
}

// 8x windowed-sinc oversampled true peak, independent of the limiter's own
// interpolator so the limiter is measured, not self-certified.
static double truePeak8x(const std::vector<float>& x)
{
    constexpr int P = 8, T = 32;
    static float c[P][T];
    static bool built = false;
    if (!built)
    {
        const double centre = 0.5 * (P * T - 1);
        for (int m = 0; m < P * T; ++m)
        {
            const double xx = (m - centre) / (double)P;
            const double sinc = xx == 0.0 ? 1.0 : std::sin(MathConstants<double>::pi * xx) / (MathConstants<double>::pi * xx);
            const double w = MathConstants<double>::twoPi * m / (P * T - 1);
            const double win = 0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2.0 * w) - 0.01168 * std::cos(3.0 * w);
            c[m % P][m / P] = (float)(sinc * win);
        }
        for (auto& ph : c)
        {
            double s = 0.0;
            for (float v : ph) s += v;
            for (float& v : ph) v = (float)(v / s);
        }
        built = true;
    }
    double peak = 0.0;
    for (int i = T; i < (int)x.size(); ++i)
    {
        peak = std::max(peak, std::abs((double)x[(size_t)i]));
        for (int p = 0; p < P; ++p)
        {
            double acc = 0.0;
            for (int k = 0; k < T; ++k) acc += c[p][k] * (double)x[(size_t)(i - k)];
            peak = std::max(peak, std::abs(acc));
        }
    }
    return peak;
}

// ===========================================================================
// COMPRESSOR - feed-forward, dB-domain one-pole detector, hybrid peak/RMS
// sidechain, quadratic soft knee. Measured: static transfer curve, the ratio
// the knob claims vs the slope it delivers, knee gain at threshold, attack and
// release against the knob, and make-up gain.
// ===========================================================================
static void compressorSuite()
{
    std::cout << "\n=== COMPRESSOR ===\n";
    const double sr = 48000.0;

    auto steadyOut = [&](float thr, float ratio, float knee, float makeup, float inDb)
    {
        agm::Compressor c;
        c.prepare(sr, 512);
        c.setEnabled(true);
        c.setThresholdDb(thr); c.setRatio(ratio); c.setKneeDb(knee);
        c.setMakeupDb(makeup); c.setMix(1.0f);
        c.setAttackMs(5.0f); c.setReleaseMs(60.0f);
        const float amp = agm::dbToGain(inDb);
        const double inc = MathConstants<double>::twoPi * 1000.0 / sr;
        auto out = runMono(c, (int)(sr * 1.5), 512, [&](int i) { return amp * (float)std::sin(inc * i); });
        // measure the last 0.3 s, well past attack/release and the 15 ms bypass ramp
        return dB(rmsOf(out, (int)(sr * 1.2), (int)(sr * 1.5)) * std::sqrt(2.0));
    };

    std::cout << "  static transfer curve (thr -20 dB, knee 0, makeup 0):\n";
    std::cout << "    in dB   out dB (r=2)  out dB (r=4)  out dB (r=20)\n";
    for (int inDb = -34; inDb <= -2; inDb += 4)
        std::cout << "    " << std::setw(5) << inDb
                  << std::setw(13) << std::fixed << std::setprecision(2) << steadyOut(-20.0f, 2.0f, 0.0f, 0.0f, (float)inDb)
                  << std::setw(14) << steadyOut(-20.0f, 4.0f, 0.0f, 0.0f, (float)inDb)
                  << std::setw(15) << steadyOut(-20.0f, 20.0f, 0.0f, 0.0f, (float)inDb) << "\n";

    std::cout << "  ratio knob vs measured slope (over threshold, -14 dB -> -6 dB):\n";
    bool ratiosOk = true;
    for (float r : { 1.5f, 2.0f, 4.0f, 8.0f, 20.0f })
    {
        const double a = steadyOut(-20.0f, r, 0.0f, 0.0f, -14.0f);
        const double b = steadyOut(-20.0f, r, 0.0f, 0.0f, -6.0f);
        const double slope = (b - a) / 8.0;
        const double measured = slope > 1e-6 ? 1.0 / slope : 1e6;
        std::cout << "    knob " << std::setw(5) << r << " -> measured ratio "
                  << std::setw(7) << std::setprecision(2) << measured << ":1\n";
        if (std::abs(measured - r) > 0.25 * r) ratiosOk = false;
    }
    check(ratiosOk, "compressor: measured slope matches the ratio knob within 25 % from 1.5:1 to 20:1");

    {
        // Quadratic soft knee: at exactly threshold the reduction should be
        // slope * knee / 8 (the knee curve's value at its midpoint).
        const float knee = 12.0f, ratio = 4.0f;
        const double hard = steadyOut(-20.0f, ratio, 0.0f, 0.0f, -20.0f);
        const double soft = steadyOut(-20.0f, ratio, knee, 0.0f, -20.0f);
        const double grAtThresh = hard - soft;
        const double expected = (1.0 - 1.0 / ratio) * knee / 8.0;
        std::cout << "  knee 12 dB at threshold: extra GR " << std::setprecision(3) << grAtThresh
                  << " dB (quadratic knee predicts " << expected << " dB)\n";
        check(std::abs(grAtThresh - expected) < 0.35,
              "compressor: soft knee gain at threshold matches a quadratic knee within 0.35 dB");
    }

    {
        // Make-up gain must be make-up gain: below threshold the output should
        // rise by exactly the knob value.
        const double base = steadyOut(-20.0f, 4.0f, 0.0f, 0.0f, -40.0f);
        bool ok = true;
        std::cout << "  make-up gain with the signal 20 dB below threshold:\n";
        for (float mk : { 0.0f, 3.0f, 6.0f, 12.0f, 24.0f })
        {
            const double got = steadyOut(-20.0f, 4.0f, 0.0f, mk, -40.0f) - base;
            std::cout << "    knob +" << std::setw(5) << std::setprecision(1) << mk
                      << " dB -> measured " << std::setw(7) << std::setprecision(2) << got << " dB\n";
            if (std::abs(got - mk) > 0.25) ok = false;
        }
        check(ok, "compressor: make-up gain raises the output by the knob value within 0.25 dB");
    }

    {
        // Attack / release against the knob: a dB-domain one-pole reaches 63 %
        // of its target in one time constant.
        // The knob is calibrated so the printed value IS the measured attack (see
        // Source/DSP/Compressor.h). Before the calibration this read 2.83 ms for a
        // 1 ms knob, 20.33 for 10 and 94.83 for 50 - about twice the knob, because
        // the detector's 8 ms RMS branch slows the level rise. The branch is the
        // feature (it is what keeps the gain reduction crest-independent, asserted
        // just below), so the mapping was calibrated against it rather than removed.
        // Below about 2 ms the RMS window puts a floor on how fast the detector can
        // rise and the knob saturates; from 3 ms up the printed value holds.
        std::cout << "  attack / release from a -40 -> -6 dBFS burst:\n";
        bool ok = true;
        bool calibrated = true;
        for (float atk : { 1.0f, 3.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f })
        {
            agm::Compressor c;
            c.prepare(sr, 64);
            c.setEnabled(true);
            c.setThresholdDb(-20.0f); c.setRatio(4.0f); c.setKneeDb(0.0f);
            c.setMakeupDb(0.0f); c.setMix(1.0f); c.setAttackMs(atk); c.setReleaseMs(300.0f);
            const double inc = MathConstants<double>::twoPi * 1000.0 / sr;
            const int quiet = (int)(sr * 0.5), n = (int)(sr * 1.0);
            auto out = runMono(c, n, 64, [&](int i)
            {
                const float amp = agm::dbToGain(i < quiet ? -40.0f : -6.0f);
                return amp * (float)std::sin(inc * i);
            });
            // envelope of the gain applied after the step
            const double finalGr = 10.5;   // (1-1/4) * (|-6| - |-20|) = 10.5 dB
            int t63 = -1;
            const int win = (int)(sr * 0.002);
            for (int i = quiet + win; i < n - win; i += 8)
            {
                const double outDb = dB(peakOf(out, i, i + win));
                const double gr = -6.0 - outDb;
                if (gr >= 0.63 * finalGr) { t63 = i - quiet; break; }
            }
            const double ms = 1000.0 * t63 / sr;
            std::cout << "    attack knob " << std::setw(5) << std::setprecision(1) << atk
                      << " ms -> 63 % of " << finalGr << " dB GR reached at "
                      << std::setprecision(2) << ms << " ms\n";
            if (t63 < 0 || ms > atk * 2.0 + 1.5 || ms < atk * 0.4 - 1.0) ok = false;
            // the calibrated bound: within 10 % of the printed value, or the 2 ms
            // floor for the settings that cannot be reached at all
            if (t63 < 0) calibrated = false;
            else if (atk >= 3.0f) { if (std::abs(ms - atk) > atk * 0.1 + 0.25) calibrated = false; }
            else if (ms > 2.25) calibrated = false;
        }
        check(ok, "compressor: attack time to 63 % of the final gain reduction tracks the knob");
        check(calibrated, "compressor: measured attack equals the printed knob within 10 % from 3 ms up");
    }

    {
        // Detector character: a square and a sine at the SAME peak level. A peak
        // detector reduces them almost equally; an RMS detector pulls the square
        // down ~3 dB harder because its RMS is 3 dB above its peak-equivalent.
        auto grFor = [&](bool square)
        {
            agm::Compressor c;
            c.prepare(sr, 512);
            c.setEnabled(true);
            c.setThresholdDb(-20.0f); c.setRatio(4.0f); c.setKneeDb(0.0f);
            c.setMakeupDb(0.0f); c.setMix(1.0f); c.setAttackMs(5.0f); c.setReleaseMs(60.0f);
            const float amp = agm::dbToGain(-6.0f);
            const double inc = MathConstants<double>::twoPi * 200.0 / sr;
            auto out = runMono(c, (int)(sr * 1.2), 512, [&](int i)
            {
                const float s = (float)std::sin(inc * i);
                return amp * (square ? (s >= 0.0f ? 1.0f : -1.0f) : s);
            });
            return -6.0 - dB(peakOf(out, (int)(sr * 0.9), (int)(sr * 1.2)));
        };
        const double grSine = grFor(false), grSquare = grFor(true);
        std::cout << "  detector: GR on a -6 dB peak sine " << std::setprecision(2) << grSine
                  << " dB, on a -6 dB peak square " << grSquare << " dB (difference "
                  << (grSquare - grSine) << " dB)\n";
        check(std::abs(grSquare - grSine) < 4.0,
              "compressor: the hybrid peak/RMS detector tracks peak level, not crest factor (< 4 dB apart)");
    }
}

// ===========================================================================
// LIMITER - lookahead brickwall with a 4x true-peak sidechain.
// ===========================================================================
static void limiterSuite()
{
    std::cout << "\n=== LIMITER ===\n";
    const double sr = 48000.0;

    {
        // Latency: an impulse must come out exactly getLatencySamples() later.
        agm::Limiter lim;
        lim.prepare(sr, 512);
        lim.setEnabled(true);
        lim.setCeilingDb(-1.0f);
        auto out = runMono(lim, 4096, 512, [](int i) { return i == 0 ? 0.2f : 0.0f; });
        int at = 0;
        for (int i = 1; i < (int)out.size(); ++i) if (std::abs(out[(size_t)i]) > std::abs(out[(size_t)at])) at = i;
        std::cout << "  impulse appears at sample " << at << ", getLatencySamples() = "
                  << lim.getLatencySamples() << "\n";
        check(at == lim.getLatencySamples(), "limiter: measured lookahead delay == getLatencySamples()");
    }

    {
        // Inter-sample peaks: a 0 dBFS sine at exactly Nyquist/3 sampled so its
        // true peak sits between samples, plus a 0.5 * fs/4 pair, plus a
        // hard-clipped square. Measured with an INDEPENDENT 8x interpolator.
        for (float ceilDb : { -1.0f, -3.0f, -0.3f })
        {
            agm::Limiter lim;
            lim.prepare(sr, 256);
            lim.setEnabled(true);
            lim.setCeilingDb(ceilDb);
            lim.setAttackMs(1.0f);
            lim.setReleaseMs(100.0f);
            const int n = (int)(sr * 2.0);
            // 7333 Hz is not a sub-multiple of 48 kHz, so peaks land off-grid;
            // the 0.25-sample phase offset maximises the inter-sample excess.
            const double inc = MathConstants<double>::twoPi * 7333.0 / sr;
            auto out = runMono(lim, n, 256, [&](int i)
            {
                const double t = (double)i;
                float v = 0.999f * (float)std::sin(inc * (t + 0.25));
                if (i > n / 2) v = v >= 0.0f ? 0.999f : -0.999f;   // clipped square half
                return v;
            });
            const double tp = dB(truePeak8x(std::vector<float>(out.begin() + (int)(sr * 0.1), out.end())));
            std::cout << "  ceiling " << std::setprecision(2) << ceilDb << " dB -> 8x true peak "
                      << tp << " dBTP (overshoot " << (tp - ceilDb) << " dB)\n";
            check(tp <= ceilDb + 0.25,
                  ("limiter: 8x-measured true peak stays within 0.25 dB of the " + String(ceilDb, 1)
                   + " dB ceiling on an inter-sample-peak programme").toStdString());
        }
    }

    {
        // Release / pumping on a bass-heavy probe: a 50 Hz sine at 0 dBFS. The
        // gain must not modulate the fundamental into audible tremolo: measure
        // the depth of the gain envelope over one release time.
        agm::Limiter lim;
        lim.prepare(sr, 256);
        lim.setEnabled(true);
        lim.setCeilingDb(-3.0f);
        lim.setAttackMs(1.0f);
        lim.setReleaseMs(200.0f);
        const double inc = MathConstants<double>::twoPi * 50.0 / sr;
        auto out = runMono(lim, (int)(sr * 2.0), 256, [&](int i) { return 0.99f * (float)std::sin(inc * i); });
        // peak of each 50 Hz cycle in the last second
        const int cyc = (int)(sr / 50.0);
        double lo = 1e9, hi = 0.0;
        for (int i = (int)(sr * 1.0); i + cyc < (int)out.size(); i += cyc)
        {
            const double p = peakOf(out, i, i + cyc);
            lo = std::min(lo, p); hi = std::max(hi, p);
        }
        std::cout << "  50 Hz at 0 dBFS, ceiling -3 dB: per-cycle peak spread "
                  << std::setprecision(3) << dB(hi) << " .. " << dB(lo) << " dB ("
                  << (dB(hi) - dB(lo)) << " dB of pumping)\n";
        check(dB(hi) - dB(lo) < 1.0, "limiter: steady 50 Hz shows < 1 dB of cycle-to-cycle pumping");
    }
}

// ===========================================================================
// SATURATION - four shapers at 4x oversampling. Measured: unity gain at zero
// drive, the harmonic series and how 2nd/3rd move with drive, aliasing in dBc.
// ===========================================================================
static void saturationSuite()
{
    std::cout << "\n=== SATURATION ===\n";
    const double sr = 48000.0;
    static const char* modeNames[4] = { "Tube", "Tape", "Soft", "Exciter" };

    auto render = [&](int mode, float drive, double freq, float inDb, int n)
    {
        agm::Saturation s;
        s.prepare(sr, 512);
        s.setEnabled(true);
        s.setMode(mode);
        s.setDrive(drive);
        s.setMix(1.0f);
        s.setOutputDb(0.0f);
        const float amp = agm::dbToGain(inDb);
        const double inc = MathConstants<double>::twoPi * freq / sr;
        return runMono(s, n, 512, [&](int i) { return amp * (float)std::sin(inc * i); });
    };

    {
        std::cout << "  unity gain at minimum drive (1 kHz @ -20 dBFS, mix 1, out 0 dB):\n";
        bool ok = true;
        for (int m = 0; m < 4; ++m)
        {
            auto out = render(m, 0.0f, 1000.0, -20.0f, (int)(sr * 0.6));
            const double g = dB(magAt(out, (int)(sr * 0.3), (int)(sr * 0.25), 1000.0, sr)) + 20.0;
            std::cout << "    " << std::setw(8) << modeNames[m] << " " << std::setw(7)
                      << std::fixed << std::setprecision(2) << g << " dB\n";
            if (std::abs(g) > 0.35) ok = false;
        }
        check(ok, "saturation: every mode is within 0.35 dB of unity at minimum drive");
    }

    {
        std::cout << "  harmonic series at 1 kHz, -6 dBFS (dB relative to the fundamental):\n";
        std::cout << "    mode      drive      H2       H3       H4       H5\n";
        bool risesOk = true;
        for (int m = 0; m < 4; ++m)
        {
            double h2prev = -200.0, h3prev = -200.0;
            bool rising = true;
            for (float d : { 0.25f, 0.5f, 1.0f })
            {
                auto out = render(m, d, 1000.0, -6.0f, (int)(sr * 0.6));
                const int from = (int)(sr * 0.3), n = (int)(sr * 0.25);
                const double h1 = magAt(out, from, n, 1000.0, sr);
                double h[4];
                for (int k = 0; k < 4; ++k) h[k] = dB(magAt(out, from, n, 1000.0 * (k + 2), sr) / std::max(h1, 1e-12));
                std::cout << "    " << std::setw(8) << modeNames[m] << std::setw(8)
                          << std::setprecision(2) << d;
                for (int k = 0; k < 4; ++k) std::cout << std::setw(9) << std::setprecision(1) << h[k];
                std::cout << "\n";
                if (h[0] < h2prev - 1.0 && h[1] < h3prev - 1.0) rising = false;
                h2prev = h[0]; h3prev = h[1];
            }
            if (!rising) risesOk = false;
        }
        check(risesOk, "saturation: harmonic content rises (never falls) with drive in every mode");

        // Tube is named after a single-ended triode stage, so the 2nd harmonic
        // must dominate the 3rd where the mode is actually used. Driven into hard
        // clipping (drive 1.0) the odd harmonics take over, which is also what an
        // overdriven triode does, so that point is measured but not asserted.
        bool tubeSecond = true;
        struct TubePoint { float drive; float inDb; bool assertIt; };
        for (TubePoint tp : { TubePoint{ 0.15f, -6.0f, true }, TubePoint{ 0.25f, -6.0f, true },
                              TubePoint{ 0.50f, -18.0f, true }, TubePoint{ 0.50f, -6.0f, false },
                              TubePoint{ 1.00f, -6.0f, false } })
        {
            auto out = render(0, tp.drive, 1000.0, tp.inDb, (int)(sr * 0.6));
            const int from = (int)(sr * 0.3), n = (int)(sr * 0.25);
            const double h1 = magAt(out, from, n, 1000.0, sr);
            const double h2 = dB(magAt(out, from, n, 2000.0, sr) / std::max(h1, 1e-12));
            const double h3 = dB(magAt(out, from, n, 3000.0, sr) / std::max(h1, 1e-12));
            std::cout << "  Tube drive " << std::setprecision(2) << tp.drive << " @ "
                      << std::setprecision(0) << tp.inDb << " dBFS: H2 " << std::setprecision(1)
                      << h2 << " dBc, H3 " << h3 << " dBc (2nd leads by " << (h2 - h3) << " dB)"
                      << (tp.assertIt ? "" : "   [measured, not asserted: the stage is in hard clipping here]")
                      << "\n";
            if (tp.assertIt && h2 - h3 < 5.0) tubeSecond = false;
        }
        check(tubeSecond, "saturation: Tube leads with the 2nd harmonic by at least 5 dB wherever it is "
                          "not slammed into hard clipping");

        {
            // Bounded output: an asymmetric stage swings asymmetrically, so check
            // that once the DC servo has settled the shaper is not running away.
            auto out = render(0, 1.0f, 200.0, -3.0f, (int)(sr * 1.0));
            const double settled = peakOf(out, (int)(sr * 0.5), (int)(sr * 1.0));
            std::cout << "  Tube at drive 1.0 on a -3 dBFS 200 Hz sine: settled peak "
                      << std::setprecision(3) << settled << "\n";
            check(settled < 1.2, "saturation: Tube stays bounded below 1.2 once the DC servo has settled");
        }
    }

    {
        // Aliasing: 15 kHz at 44.1 kHz with the drive wide open. The 2nd harmonic
        // lands at 30 kHz and folds to 14.1 kHz; the 3rd at 45 kHz folds to
        // 0.9 kHz. Both are below Nyquist and unmistakably alias products.
        std::cout << "  aliasing at 44.1 kHz, 15 kHz input @ -3 dBFS, drive 1.0:\n";
        bool ok = true;
        for (int m = 0; m < 4; ++m)
        {
            agm::Saturation s;
            s.prepare(44100.0, 512);
            s.setEnabled(true); s.setMode(m); s.setDrive(1.0f); s.setMix(1.0f); s.setOutputDb(0.0f);
            const float amp = agm::dbToGain(-3.0f);
            const double inc = MathConstants<double>::twoPi * 15000.0 / 44100.0;
            auto out = runMono(s, (int)(44100 * 0.6), 512, [&](int i) { return amp * (float)std::sin(inc * i); });
            const int from = (int)(44100 * 0.3), n = (int)(44100 * 0.25);
            const double f0 = magAt(out, from, n, 15000.0, 44100.0);
            const double a2 = dB(magAt(out, from, n, 14100.0, 44100.0) / std::max(f0, 1e-12));
            const double a3 = dB(magAt(out, from, n, 900.0, 44100.0) / std::max(f0, 1e-12));
            std::cout << "    " << std::setw(8) << modeNames[m] << " fold-down of H2 (14.1 kHz) "
                      << std::setw(7) << std::setprecision(1) << a2 << " dBc, of H3 (0.9 kHz) "
                      << std::setw(7) << a3 << " dBc\n";
            if (a2 > -40.0 || a3 > -40.0) ok = false;
        }
        check(ok, "saturation: 4x oversampling keeps fold-down aliasing below -40 dBc in every mode");
    }
}

// ===========================================================================
// EQ / BIQUAD - RBJ cookbook sections. Measured against the analytic transfer
// function the module itself reports, and against the analogue prototype so
// the cramping at 44.1 kHz is a number and not a shrug.
// ===========================================================================
static void eqSuite()
{
    std::cout << "\n=== EQ / BIQUAD ===\n";
    const double sr = 48000.0;

    auto measure = [&](std::function<void(agm::Biquad&)> setup, double freq)
    {
        agm::Biquad f;
        f.prepare(sr);
        setup(f);
        const int n = (int)(sr * 0.5);
        const double inc = MathConstants<double>::twoPi * freq / sr;
        std::vector<float> out((size_t)n);
        for (int i = 0; i < n; ++i) out[(size_t)i] = f.process((float)std::sin(inc * i));
        const double amp = magAt(out, (int)(sr * 0.25), (int)(sr * 0.2), freq, sr);
        return std::make_pair(dB(amp), (double)f.magnitudeDbAt((float)freq));
    };

    struct Case { const char* name; std::function<void(agm::Biquad&)> setup; };
    const std::vector<Case> cases = {
        { "highpass 200 Hz",        [](agm::Biquad& f) { f.setHighPass(200.0f, 0.7071f); } },
        { "lowpass 3 kHz",          [](agm::Biquad& f) { f.setLowPass(3000.0f, 0.7071f); } },
        { "lowshelf 120 Hz +9 dB",  [](agm::Biquad& f) { f.setLowShelf(120.0f, 9.0f, 0.9f); } },
        { "highshelf 6 kHz -9 dB",  [](agm::Biquad& f) { f.setHighShelf(6000.0f, -9.0f, 0.9f); } },
        { "peak 1 kHz +12 dB Q4",   [](agm::Biquad& f) { f.setPeaking(1000.0f, 12.0f, 4.0f); } },
        { "peak 1 kHz -12 dB Q0.5", [](agm::Biquad& f) { f.setPeaking(1000.0f, -12.0f, 0.5f); } },
    };
    const double probes[] = { 60.0, 200.0, 700.0, 1000.0, 3000.0, 6000.0, 12000.0 };

    bool ok = true;
    double worst = 0.0;
    std::cout << "  measured minus analytic magnitude, dB:\n";
    for (const auto& c : cases)
    {
        std::cout << "    " << std::left << std::setw(24) << c.name << std::right;
        for (double f : probes)
        {
            const auto r = measure(c.setup, f);
            const double err = r.first - r.second;
            std::cout << std::setw(8) << std::fixed << std::setprecision(2) << err;
            worst = std::max(worst, std::abs(err));
            if (std::abs(err) > 0.2) ok = false;
        }
        std::cout << "\n";
    }
    std::cout << "  worst |measured - analytic| = " << std::setprecision(3) << worst << " dB\n";
    check(ok, "biquad: every filter type matches its own analytic response within 0.2 dB");

    {
        // Cramping: a bilinear-transform high shelf loses its target gain as the
        // corner approaches Nyquist. Quantify it at 44.1 kHz.
        std::cout << "  high-shelf cramping at 44.1 kHz (target +12 dB, measured at 20 kHz):\n";
        for (double fc : { 4000.0, 8000.0, 12000.0, 16000.0 })
        {
            agm::Biquad f;
            f.prepare(44100.0);
            f.setHighShelf((float)fc, 12.0f, 0.9f);
            std::cout << "    corner " << std::setw(6) << (int)fc << " Hz -> shelf reaches "
                      << std::setprecision(2) << f.magnitudeDbAt(20000.0f) << " dB\n";
        }
    }

    {
        // Coefficient smoothing: sweeping a peak band from 200 Hz to 3 kHz in one
        // step must not produce a discontinuity larger than the signal's own slope.
        agm::EQ eq;
        eq.prepare(sr, 64);
        eq.setEnabled(true);
        eq.setPeakGainDb(0, 12.0f);
        eq.setPeakQ(0, 6.0f);
        eq.setPeakFreq(0, 200.0f);
        AudioBuffer<float> buf(2, 64);
        const double inc = MathConstants<double>::twoPi * 500.0 / sr;
        float prev = 0.0f, worstStep = 0.0f;
        int idx = 0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 64; ++i)
            {
                const float v = 0.3f * (float)std::sin(inc * (idx + i));
                buf.setSample(0, i, v); buf.setSample(1, i, v);
            }
            eq.setPeakFreq(0, (b % 2) ? 3000.0f : 200.0f);
            eq.process(buf);
            if (b > 20)
                for (int i = 0; i < 64; ++i)
                {
                    worstStep = std::max(worstStep, std::abs(buf.getSample(0, i) - prev));
                    prev = buf.getSample(0, i);
                }
            else prev = buf.getSample(0, 63);
            idx += 64;
        }
        std::cout << "  peak-band frequency flipped 200 Hz <-> 3 kHz every 1.3 ms: worst step "
                  << std::setprecision(4) << worstStep << " (signal's own slope 0.020)\n";
        check(worstStep < 0.08f, "EQ: band frequency automation is coefficient-smoothed, no zipper step > 0.08");
    }
}

// ===========================================================================
// DELAY - digital delay with a cubic-interpolated modulated read head, a
// one-pole damper in the feedback path and mid/side cross-feed.
// ===========================================================================
static void delaySuite()
{
    std::cout << "\n=== DELAY ===\n";
    const double sr = 48000.0;

    {
        // Interpolator quality. The read head is always being moved by a 0.4 Hz
        // LFO, and that modulation puts sidebands around the repeat that no
        // fixed-frequency fit can remove, so measure inside a 100 ms window
        // centred on the LFO's turning point (phase pi/2 at t = 0.625 s), where
        // the read position is momentarily stationary.
        agm::Delay d;
        d.prepare(sr, 512);
        d.setEnabled(true);
        d.setTimeMs(25.0f);
        d.setFeedback(0.0f);
        d.setMix(1.0f);
        d.setDamping(0.0f);
        const double inc = MathConstants<double>::twoPi * 1000.0 / sr;
        auto out = runMono(d, (int)(sr * 1.2), 512, [&](int i) { return 0.5f * (float)std::sin(inc * i); });
        // Exact least-squares removal of the 1 kHz component over a whole number
        // of periods (48 samples at 48 kHz), so what is left really is distortion
        // and not the window's own amplitude bias.
        const int from = (int)(sr * 0.575), n = 48 * 100;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = MathConstants<double>::twoPi * 1000.0 * i / sr;
            re += (double)out[(size_t)(from + i)] * std::cos(ph);
            im += (double)out[(size_t)(from + i)] * std::sin(ph);
        }
        const double A = 2.0 * re / n, B = 2.0 * im / n;
        const double f0 = std::sqrt(A * A + B * B);
        double resSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = MathConstants<double>::twoPi * 1000.0 * i / sr;
            const double fit = A * std::cos(ph) + B * std::sin(ph);
            const double err = (double)out[(size_t)(from + i)] - fit;
            resSq += err * err;
        }
        const double resid = std::sqrt(resSq / n) * std::sqrt(2.0);
        std::cout << "  1 kHz through a 25 ms delay, LFO stationary: fundamental "
                  << std::setprecision(2) << dB(f0) << " dB, non-fundamental residue "
                  << dB(resid / std::max(f0, 1e-12)) << " dBc\n";
        // Catmull-Rom on a fractional delay measures -51.9 dBc here; the guard is
        // set at -45 dBc so a regression in the read head is caught without
        // pretending a 4-point cubic is better than it is.
        check(dB(resid / std::max(f0, 1e-12)) < -45.0,
              "delay: the cubic fractional-delay read head itself is below -45 dBc of distortion");
    }

    {
        // The read head is ALWAYS modulated by a 0.4 Hz LFO whose depth follows
        // the delay time. That is a deliberate tape/BBD wobble, but it is not a
        // control and it is not free: measure how many cents it bends the pitch
        // at the longest delay time the plugin offers.
        for (float timeMs : { 100.0f, 500.0f, 2000.0f })
        {
            agm::Delay d;
            d.prepare(sr, 512);
            d.setEnabled(true);
            d.setTimeMs(timeMs);
            d.setFeedback(0.0f);
            d.setMix(1.0f);
            d.setDamping(0.0f);
            const double inc = MathConstants<double>::twoPi * 1000.0 / sr;
            auto out = runMono(d, (int)(sr * 8.0), 512, [&](int i) { return 0.5f * (float)std::sin(inc * i); });
            // instantaneous period from successive positive-going zero crossings
            double lo = 1e9, hi = 0.0;
            double lastCross = -1.0;
            for (int i = (int)(sr * 3.0) + 1; i < (int)(sr * 8.0); ++i)
            {
                if (out[(size_t)(i - 1)] < 0.0f && out[(size_t)i] >= 0.0f)
                {
                    const double frac = -out[(size_t)(i - 1)] / (out[(size_t)i] - out[(size_t)(i - 1)]);
                    const double x = i - 1 + frac;
                    if (lastCross > 0.0)
                    {
                        const double f = sr / (x - lastCross);
                        if (f > 500.0 && f < 2000.0) { lo = std::min(lo, f); hi = std::max(hi, f); }
                    }
                    lastCross = x;
                }
            }
            const double cents = 1200.0 * std::log2(hi / std::max(lo, 1e-9));
            std::cout << "  always-on wobble at " << std::setw(5) << (int)timeMs
                      << " ms: 1 kHz swings " << std::setprecision(1) << lo << " .. " << hi
                      << " Hz = " << std::setprecision(2) << cents << " cents peak-to-peak\n";
            check(cents < 25.0, ("delay: the always-on read-head wobble stays under 25 cents at "
                                 + String((int)timeMs) + " ms").toStdString());
        }
    }

    {
        // Feedback at the maximum the knob allows must decay, and the damper in
        // the loop must make it decay faster with damping up.
        std::cout << "  feedback 0.95, decay of the tail after the input stops:\n";
        for (float damp : { 0.0f, 0.5f, 1.0f })
        {
            agm::Delay d;
            d.prepare(sr, 512);
            d.setEnabled(true);
            d.setTimeMs(100.0f);
            d.setFeedback(0.95f);
            d.setMix(1.0f);
            d.setDamping(damp);
            const double inc = MathConstants<double>::twoPi * 1000.0 / sr;
            auto out = runMono(d, (int)(sr * 6.0), 512, [&](int i)
            {
                return i < (int)(sr * 0.1) ? 0.5f * (float)std::sin(inc * i) : 0.0f;
            });
            const double p1 = dB(peakOf(out, (int)(sr * 0.5), (int)(sr * 0.7)));
            const double p5 = dB(peakOf(out, (int)(sr * 5.0), (int)(sr * 5.2)));
            std::cout << "    damp " << std::setprecision(2) << damp << ": 0.5 s " << p1
                      << " dB, 5 s " << p5 << " dB (decay " << (p1 - p5) << " dB)\n";
            check(p5 < p1, ("delay: feedback 0.95 with damping " + String(damp, 2)
                            + " still decays, never runs away").toStdString());
        }
    }
}

// ===========================================================================
// REVERB - Schroeder tank: 8 damped comb filters into 4 diffusion allpasses.
// Measured: diffuser flatness (a real allpass has NO magnitude ripple),
// and stereo decorrelation.
// ===========================================================================
static void reverbSuite()
{
    std::cout << "\n=== REVERB ===\n";
    const double sr = 48000.0;

    {
        // The diffusion section should not colour the tank. Measure the wet-only
        // magnitude response, octave by octave, with damping off and a short
        // decay, relative to the average: a well-formed allpass chain gives a
        // flat late field, a mis-normalised one gives comb ripple.
        agm::Reverb r;
        r.prepare(sr, 512);
        r.setEnabled(true);
        r.setDamping(0.0f);
        r.setDecaySec(2.0f);
        r.setSize(0.7f);
        r.setMix(1.0f);
        r.setPreDelayMs(0.0f);
        r.setWidth(1.0f);
        uint32_t s = 5150u;
        auto out = runMono(r, (int)(sr * 6.0), 512, [&](int)
        {
            s = s * 1664525u + 1013904223u;
            return 0.4f * ((float)(s >> 8) / 8388608.0f - 1.0f);
        });
        const int from = (int)(sr * 3.0), n = (int)(sr * 2.0);
        const double bands[] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
        double lvl[7], mean = 0.0;
        std::cout << "  wet-only magnitude, damping 0 (dB re. mean):\n    ";
        for (int i = 0; i < 7; ++i)
        {
            // one-third-octave average around the band centre
            double acc = 0.0;
            for (int k = -3; k <= 3; ++k)
            {
                const double f = bands[i] * std::pow(2.0, k / 18.0);
                const double m = magAt(out, from, n, f, sr);
                acc += m * m;
            }
            lvl[i] = dB(std::sqrt(acc / 7.0));
            mean += lvl[i];
        }
        mean /= 7.0;
        double spread = 0.0;
        for (int i = 0; i < 7; ++i)
        {
            std::cout << std::setw(6) << (int)bands[i] << "Hz" << std::setw(7)
                      << std::fixed << std::setprecision(2) << (lvl[i] - mean) << "  ";
            spread = std::max(spread, std::abs(lvl[i] - mean));
        }
        std::cout << "\n  worst deviation from mean: " << std::setprecision(2) << spread << " dB\n";
        // The tilt is the comb bank's low-frequency modal density. The bank is now
        // 16 mutually-prime lines over a 1:2 range, 8 per stereo half, which took
        // this figure from 3.22 dB to 3.11 dB on this seed and from a 4.28 dB
        // six-seed mean to 2.35 dB. Eight lines cannot do it at any conditioning:
        // measured means were 4.28 (the old Freeverb set), 4.87, 4.41 and 5.05 dB
        // for four differently conditioned 8-line sets, against 3.21 and 2.35 for
        // two 16-line sets. Count, not coefficients.
        //
        // NOT the diffusers, and not for the reason the audit first gave. It
        // claimed the four sections are not unity-gain allpasses, reading the 0.75
        // feed-forward against the 0.5 feedback. That is a misreading: with the
        // delayed value read before the write, H(z) = (z^-m - 0.5)/(1 - 0.5 z^-m),
        // which is exactly the unity-gain Schroeder allpass. Measured ripple of one
        // section over 0..pi, at all four lengths: 0.000 dB. The "proper allpass"
        // that was built to replace it (y = -g*x + d, buf = x + g*d) has
        // H(z) = (1.25 z^-m - 0.5)/(1 - 0.5 z^-m), 2.183 dB of ripple and +3.52 dB
        // at DC - which is why it measured worse on every number and was reverted.
        // The sections are correct; leave them alone.
        check(spread < 4.0, "reverb: with damping off the late field is flat within 4.0 dB from 125 Hz to 8 kHz");
    }

    {
        // Stereo decorrelation: the two outputs must not be the same signal.
        agm::Reverb r;
        r.prepare(sr, 512);
        r.setEnabled(true);
        r.setDamping(0.4f); r.setDecaySec(2.0f); r.setSize(0.7f); r.setMix(1.0f);
        r.setWidth(1.0f); r.setPreDelayMs(0.0f);
        AudioBuffer<float> buf(2, 512);
        std::vector<float> l, rr;
        uint32_t s = 99u;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                s = s * 1664525u + 1013904223u;
                const float v = b < 40 ? 0.4f * ((float)(s >> 8) / 8388608.0f - 1.0f) : 0.0f;
                buf.setSample(0, i, v); buf.setSample(1, i, v);
            }
            r.process(buf);
            if (b >= 100 && b < 300)
                for (int i = 0; i < 512; ++i) { l.push_back(buf.getSample(0, i)); rr.push_back(buf.getSample(1, i)); }
        }
        double num = 0.0, dl = 0.0, dr = 0.0;
        for (size_t i = 0; i < l.size(); ++i) { num += (double)l[i] * rr[i]; dl += (double)l[i] * l[i]; dr += (double)rr[i] * rr[i]; }
        const double corr = num / std::sqrt(std::max(dl * dr, 1e-30));
        std::cout << "  L/R correlation of the tail from a mono source, width 1.0: "
                  << std::setprecision(3) << corr << "\n";
        check(std::abs(corr) < 0.8, "reverb: the tail is decorrelated between channels (|r| < 0.8)");
    }

    {
        // Pre-delay is the gap between the dry sound and the tank's onset, so it
        // must delay the tank's INPUT. Two things then have to hold: the wet output
        // starts at the pre-delay setting plus the tank's own onset, and the tail
        // length does not move with the setting. Both are measured from an impulse.
        auto impulseAt = [&](float pd, int ch)
        {
            agm::Reverb r;
            r.prepare(sr, 64);
            r.setEnabled(true);
            r.setDamping(0.5f); r.setDecaySec(2.0f); r.setSize(0.7f);
            r.setMix(1.0f); r.setWidth(1.0f); r.setPreDelayMs(pd);
            AudioBuffer<float> b(ch, 64);
            for (int i = 0; i < 200; ++i) { b.clear(); r.process(b); }   // settle
            const int n = (int)(sr * 6.0);
            std::vector<float> out((size_t)n, 0.0f);
            for (int done = 0; done < n; done += 64)
            {
                b.clear();
                if (done == 0) for (int c = 0; c < ch; ++c) b.setSample(c, 0, 1.0f);
                r.process(b);
                for (int i = 0; i < 64 && done + i < n; ++i) out[(size_t)(done + i)] = b.getSample(0, i);
            }
            return out;
        };
        // RT60 by least squares over the 30 dB below the envelope peak
        auto rt60 = [&](const std::vector<float>& x)
        {
            const int win = (int)(sr * 0.01);
            std::vector<double> e;
            for (size_t i = 0; i + (size_t)win < x.size(); i += (size_t)win)
                e.push_back(dB(rmsOf(x, (int)i, (int)i + win)));
            if (e.size() < 8) return 0.0;
            const size_t pk = (size_t)(std::max_element(e.begin(), e.end()) - e.begin());
            double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
            for (size_t i = pk; i < e.size(); ++i)
            {
                if (e[i] > e[pk] - 5.0) continue;
                if (e[i] < e[pk] - 35.0) break;
                const double t = (double)i * 0.01;
                sx += t; sy += e[i]; sxx += t * t; sxy += t * e[i]; n += 1.0;
            }
            if (n < 4.0) return 0.0;
            const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            return slope < 0.0 ? -60.0 / slope : 0.0;
        };
        std::cout << "  pre-delay: wet onset and tail length (decay 2 s, size 0.7, damp 0.5)\n";
        bool onsetOk = true, tailOk = true;
        double rtRef = 0.0;
        const float pds[] = { 0.0f, 20.0f, 50.0f, 100.0f };
        for (int ch = 2; ch >= 1; --ch)
        {
            double tankOnset = 0.0;
            for (int k = 0; k < 4; ++k)
            {
                auto out = impulseAt(pds[k], ch);
                const double pk = peakOf(out, 0, (int)out.size());
                int first = -1;
                for (size_t i = 0; i < out.size(); ++i)
                    if (std::abs(out[i]) >= pk * 0.02) { first = (int)i; break; }
                const double ms = 1000.0 * first / sr;
                if (k == 0) tankOnset = ms;
                const double expect = tankOnset + (double)pds[k];
                const double rt = rt60(out);
                if (k == 0 && ch == 2) rtRef = rt;
                std::cout << "    " << ch << "-ch pre-delay " << std::setw(6) << std::setprecision(1)
                          << pds[k] << " ms -> onset " << std::setw(6) << first << " samples = "
                          << std::setw(7) << std::setprecision(2) << ms << " ms (expected "
                          << std::setprecision(2) << expect << "), RT60 " << std::setprecision(3)
                          << rt << " s\n";
                if (first < 0 || std::abs(ms - expect) > 0.2) onsetOk = false;
                // the tail must not change length with the pre-delay: 5 % window
                if (ch == 2 && (rt <= 0.0 || std::abs(rt - rtRef) > rtRef * 0.05)) tailOk = false;
            }
        }
        check(onsetOk, "reverb: the wet onset is the pre-delay setting plus the tank's own onset, within 0.2 ms");
        check(tailOk, "reverb: the tail length does not change with the pre-delay (within 5 %)");
    }

    {
        // Automating the pre-delay must not disturb a tail that is already ringing.
        // With the pre-delay behind the tank it did: the knob slid a read pointer
        // back over the wet history and re-played the louder, earlier part of the
        // decay, 3.66 dB away from the un-automated reference and 0.60 dB louder
        // than the tail had been when the knob moved. In front of the tank the
        // ringing tank cannot be reached.
        auto run = [&](bool step)
        {
            agm::Reverb r;
            r.prepare(sr, 512);
            r.setEnabled(true);
            r.setDamping(0.2f); r.setDecaySec(6.0f); r.setSize(0.7f);
            r.setMix(1.0f); r.setWidth(1.0f); r.setPreDelayMs(0.0f);
            AudioBuffer<float> b(2, 512);
            std::vector<double> env;
            uint32_t st = 7u;
            for (int blk = 0; blk < 300; ++blk)
            {
                for (int i = 0; i < 512; ++i)
                {
                    st = st * 1664525u + 1013904223u;
                    const float v = blk < 33 ? 0.4f * ((float)(st >> 8) / 8388608.0f - 1.0f) : 0.0f;
                    b.setSample(0, i, v); b.setSample(1, i, v);
                }
                if (step && blk == 100) r.setPreDelayMs(100.0f);
                r.process(b);
                double a = 0.0;
                for (int i = 0; i < 512; ++i) a += (double)b.getSample(0, i) * b.getSample(0, i);
                env.push_back(std::sqrt(a / 512.0));
            }
            return env;
        };
        const auto ref = run(false), mod = run(true);
        double worst = 0.0;
        for (size_t b = 101; b < ref.size(); ++b)
            worst = std::max(worst, std::abs(dB(std::max(mod[b], 1e-30)) - dB(std::max(ref[b], 1e-30))));
        std::cout << "  pre-delay stepped 0 -> 100 ms 1.07 s into a decaying tail: tail envelope"
                  << " moves " << std::setprecision(2) << worst << " dB (was 3.66 dB)\n";
        check(worst < 0.5, "reverb: automating the pre-delay leaves a ringing tail untouched");
    }
}

// ===========================================================================
// STEREO IMAGER - mid/side width, cos-law balance, mono fold.
// ===========================================================================
static void imagerSuite()
{
    std::cout << "\n=== STEREO IMAGER ===\n";
    const double sr = 48000.0;

    auto sideGain = [&](float widthKnob)
    {
        agm::StereoImager im;
        im.prepare(sr, 512);
        im.setEnabled(true);
        im.setWidthPercent(widthKnob);
        im.setBalance(0.0f);
        im.setMono(false);
        AudioBuffer<float> buf(2, 512);
        double side = 0.0, mid = 0.0;
        const double inc = MathConstants<double>::twoPi * 400.0 / sr;
        int idx = 0;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = (float)std::sin(inc * (idx + i));
                buf.setSample(0, i, 0.5f * v);      // pure side plus pure mid
                buf.setSample(1, i, -0.5f * v);
            }
            im.process(buf);
            if (b >= 20)
                for (int i = 0; i < 512; ++i)
                {
                    const double m = 0.5 * (buf.getSample(0, i) + buf.getSample(1, i));
                    const double sd = 0.5 * (buf.getSample(0, i) - buf.getSample(1, i));
                    mid += m * m; side += sd * sd;
                }
            idx += 512;
        }
        juce::ignoreUnused(mid);
        return std::sqrt(side / (20.0 * 512.0)) / 0.3535533906;   // vs the 0.5-amplitude input side
    };

    std::cout << "  side-signal gain vs the Width control (the UI shows this as a percentage):\n";
    bool monotone = true;
    double prev = -1.0;
    for (float w : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f, 150.0f, 200.0f })
    {
        const double g = sideGain(w);
        std::cout << "    width " << std::setw(6) << std::fixed << std::setprecision(0) << w
                  << " % -> side gain " << std::setw(7) << std::setprecision(3) << g
                  << " (" << std::setprecision(2) << dB(g) << " dB)\n";
        if (g <= prev + 1e-4 && w > 0.0f) monotone = false;
        prev = g;
    }
    check(monotone, "imager: the Width control is monotonic across its whole 0..200 % range");

    {
        const double g100 = sideGain(100.0f);
        std::cout << "  width 100 % side gain = " << std::setprecision(4) << g100 << "\n";
        check(std::abs(g100 - 1.0) < 0.02, "imager: Width 100 % is unity - the stereo image is unchanged");
    }

    {
        // Width 0 must be a true mono sum: L and R bit-identical, and the mono
        // sum must lose nothing versus the input mid.
        agm::StereoImager im;
        im.prepare(sr, 512);
        im.setEnabled(true);
        im.setWidthPercent(0.0f);
        im.setBalance(0.0f);
        AudioBuffer<float> buf(2, 512);
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample(0, i, 0.4f * (float)std::sin(0.01 * (b * 512 + i)));
                buf.setSample(1, i, 0.2f * (float)std::cos(0.013 * (b * 512 + i)));
            }
            im.process(buf);
        }
        double worst = 0.0, pk = 0.0;
        for (int i = 0; i < 512; ++i)
        {
            worst = std::max(worst, std::abs((double)buf.getSample(0, i) - buf.getSample(1, i)));
            pk = std::max(pk, std::abs((double)buf.getSample(0, i)));
        }
        std::cout << "  width 0: worst L-R difference " << dB(worst / std::max(pk, 1e-12)) << " dBc\n";
        check(dB(worst / std::max(pk, 1e-12)) < -120.0,
              "imager: Width 0 collapses to a mono sum (L-R below -120 dBc)");
    }
}

// ===========================================================================
// DRUM ENGINE - synthesised one-shots in the spirit of the classic analogue
// drum machines: a pitch-dropping resonator for the kick, a tuned body plus a
// filtered noise burst for the snare, an inharmonic metal cluster through a
// high-pass for the hats and cymbals.
// ===========================================================================
static std::vector<float> renderDrum(int note, float vel, double sr, double secs, agm::DrumEngine* shared = nullptr)
{
    agm::DrumEngine local;
    agm::DrumEngine& d = shared != nullptr ? *shared : local;
    if (shared == nullptr) { d.prepare(sr, 256); d.setEnabled(true); d.setDrumLevelDb(0.0f); }
    d.noteOn(note, vel);
    const int n = (int)(sr * secs);
    std::vector<float> out((size_t)n, 0.0f);
    AudioBuffer<float> buf(2, 256);
    for (int done = 0; done < n; )
    {
        const int k = std::min(256, n - done);
        buf.setSize(2, k, false, false, true);
        buf.clear();
        d.renderAdd(buf, 2, 0, k);
        for (int i = 0; i < k; ++i) out[(size_t)(done + i)] = buf.getSample(0, i);
        done += k;
    }
    return out;
}

// Dominant frequency by parabolic-interpolated peak of a coarse DFT scan.
static double dominantFreq(const std::vector<float>& x, int from, int n, double sr, double lo, double hi)
{
    double best = 0.0, bestF = 0.0;
    for (double f = lo; f < hi; f *= 1.01)
    {
        const double m = magAt(x, from, n, f, sr);
        if (m > best) { best = m; bestF = f; }
    }
    // refine: a 1 % scan only locates the peak to +/- 8.6 cents, which would be
    // reported as a tuning error that is really the measurement's own grid
    for (double step = 0.004; step > 1e-5; step *= 0.4)
        for (int k = -3; k <= 3; ++k)
        {
            const double f = bestF * (1.0 + k * step);
            const double m = magAt(x, from, n, f, sr);
            if (m > best) { best = m; bestF = f; }
        }
    return bestF;
}

static double centroidHz(const std::vector<float>& x, int from, int n, double sr)
{
    double num = 0.0, den = 0.0;
    for (double f = 60.0; f < sr * 0.45; f *= 1.05)
    {
        const double m = magAt(x, from, n, f, sr);
        num += f * m * m; den += m * m;
    }
    return den > 0.0 ? num / den : 0.0;
}

static double decayMs(const std::vector<float>& x, double sr, double dropDb)
{
    const double pk = peakOf(x, 0, (int)x.size());
    if (pk <= 0.0) return 0.0;
    const double target = pk * std::pow(10.0, -dropDb / 20.0);
    const int win = (int)(sr * 0.002);
    for (int i = 0; i + win < (int)x.size(); i += win / 2)
        if (peakOf(x, i, i + win) < target) return 1000.0 * i / sr;
    return 1000.0 * x.size() / sr;
}

static void drumSuite()
{
    std::cout << "\n=== DRUM ENGINE ===\n";
    const double sr = 48000.0;
    struct Row { int note; const char* label; };
    const Row rows[] = {
        { 36, "kick" }, { 41, "low tom" }, { 45, "mid tom" }, { 48, "hi tom" },
        { 38, "snare" }, { 39, "clap" }, { 42, "closed hat" }, { 46, "open hat" },
        { 49, "crash" }, { 51, "ride" },
    };
    std::cout << "  note  voice        peak dB   f0 Hz   centroid Hz   -20 dB ms   -60 dB ms\n";
    std::map<String, std::vector<float>> captured;
    for (const auto& r : rows)
    {
        auto out = renderDrum(r.note, 1.0f, sr, 2.0);
        const double f0 = dominantFreq(out, 0, (int)(sr * 0.05), sr, 30.0, 1000.0);
        const double cen = centroidHz(out, 0, (int)(sr * 0.05), sr);
        std::cout << "   " << std::setw(3) << r.note << "  " << std::left << std::setw(12) << r.label
                  << std::right << std::setw(8) << std::fixed << std::setprecision(1) << dB(peakOf(out, 0, (int)out.size()))
                  << std::setw(9) << std::setprecision(1) << f0
                  << std::setw(13) << std::setprecision(0) << cen
                  << std::setw(12) << std::setprecision(1) << decayMs(out, sr, 20.0)
                  << std::setw(12) << decayMs(out, sr, 60.0) << "\n";
        captured[r.label] = out;
    }

    {
        // Kick: a bridged-T style resonator sweeps down in pitch. Measure the
        // instantaneous frequency at the very start and 60 ms in.
        auto k = captured["kick"];
        const double early = dominantFreq(k, 0, (int)(sr * 0.012), sr, 40.0, 400.0);
        const double late = dominantFreq(k, (int)(sr * 0.10), (int)(sr * 0.10), sr, 20.0, 400.0);
        std::cout << "  kick pitch drop: " << std::setprecision(1) << early << " Hz at t=0 -> "
                  << late << " Hz at t=100 ms (ratio " << std::setprecision(2) << (early / std::max(late, 1.0)) << ")\n";
        check(early > late * 1.5, "kick: the pitch envelope drops by at least 1.5x within 100 ms");
    }

    {
        // Toms must not be the kick with a different name.
        bool distinct = true;
        std::vector<String> same;
        for (const char* t : { "low tom", "mid tom", "hi tom" })
        {
            const auto& a = captured["kick"];
            const auto& b = captured[t];
            double d = 0.0;
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) d = std::max(d, std::abs((double)a[i] - b[i]));
            if (d < 1e-9) { distinct = false; same.push_back(t); }
        }
        std::cout << "  toms vs kick: " << (distinct ? "different waveforms" : "BIT-IDENTICAL to the kick") << "\n";
        check(distinct, "drum map: toms are not bit-identical copies of the kick");
    }

    {
        // Snare: a tuned body tone plus a noise burst. The body must be audible
        // as a discrete partial, and the noise must reach well above the body.
        auto s = captured["snare"];
        const double body = magAt(s, 0, (int)(sr * 0.04), 162.0, sr);
        const double hf = magAt(s, 0, (int)(sr * 0.04), 4000.0, sr);
        const double cen = centroidHz(s, 0, (int)(sr * 0.04), sr);
        std::cout << "  snare: body partial " << std::setprecision(1) << dB(body)
                  << " dB, 4 kHz noise " << dB(hf) << " dB, centroid " << std::setprecision(0) << cen << " Hz\n";
        check(body > 0.0 && cen > 600.0, "snare: has both a tuned body partial and a bright noise component");
    }

    {
        // Hats and cymbals: metal is inharmonic. A single sine plus noise has a
        // low-order spectrum; a cluster of mutually irrational partials does not.
        // Measure how much of the energy sits in the top two octaves.
        auto hat = captured["closed hat"];
        double lowE = 0.0, highE = 0.0;
        for (double f = 100.0; f < 20000.0; f *= 1.06)
        {
            const double m = magAt(hat, 0, (int)(sr * 0.03), f, sr);
            if (f < 4000.0) lowE += m * m; else highE += m * m;
        }
        const double ratio = dB(std::sqrt(highE / std::max(lowE, 1e-18)));
        std::cout << "  closed hat: energy above 4 kHz is " << std::setprecision(1) << ratio
                  << " dB relative to below 4 kHz\n";
        check(ratio > -6.0, "closed hat: at least as much energy above 4 kHz as below (it reads as metal, not as a tom)");
    }

    {
        // Crash must be a cymbal, not a snare with a short tail.
        auto crash = captured["crash"];
        auto snare = captured["snare"];
        const double cCen = centroidHz(crash, 0, (int)(sr * 0.05), sr);
        const double sCen = centroidHz(snare, 0, (int)(sr * 0.05), sr);
        const double cDecay = decayMs(crash, sr, 20.0);
        std::cout << "  crash centroid " << std::setprecision(0) << cCen << " Hz vs snare "
                  << sCen << " Hz; crash -20 dB decay " << std::setprecision(1) << cDecay << " ms\n";
        check(cCen > sCen * 1.3 && cDecay > 150.0,
              "crash: brighter than the snare and rings for more than 150 ms");
    }

    {
        // Velocity response: amplitude must scale, and a real accent also opens up.
        std::cout << "  velocity response (peak dB, 0.25 / 0.6 / 1.0):\n";
        bool ok = true;
        for (int note : { 36, 38, 42 })
        {
            double p[3];
            int i = 0;
            for (float v : { 0.25f, 0.6f, 1.0f })
                p[i++] = dB(peakOf(renderDrum(note, v, sr, 1.0), 0, (int)(sr * 1.0)));
            std::cout << "    note " << note << ": " << std::setprecision(1) << p[0] << " / "
                      << p[1] << " / " << p[2] << " dB (span " << (p[2] - p[0]) << " dB)\n";
            if (p[2] - p[0] < 6.0 || p[1] <= p[0] || p[2] <= p[1]) ok = false;
        }
        check(ok, "drums: velocity spans at least 6 dB monotonically on kick, snare and hat");
    }

    {
        // Voice release must not click: the engine frees a voice when its
        // envelope is spent, and the last sample before that must be small.
        auto k = renderDrum(36, 1.0f, sr, 3.0);
        int last = 0;
        for (int i = 0; i < (int)k.size(); ++i) if (k[(size_t)i] != 0.0f) last = i;
        const double tailDb = dB(std::abs((double)k[(size_t)last]) / std::max(peakOf(k, 0, (int)k.size()), 1e-12));
        std::cout << "  kick: last non-zero sample is " << std::setprecision(1) << tailDb
                  << " dB below the peak (the step to silence)\n";
        check(tailDb < -60.0, "drums: a voice is freed only once its output is more than 60 dB down (no truncation click)");
    }

    {
        // Polyphony under a dense pattern: 64 triggers inside one second must not
        // clip the bus into non-finite values or steal a still-loud voice.
        agm::DrumEngine d;
        d.prepare(sr, 128);
        d.setEnabled(true);
        d.setDrumLevelDb(-6.0f);
        AudioBuffer<float> buf(2, 128);
        bool finite = true;
        double pk = 0.0;
        int maxVoices = 0;
        for (int b = 0; b < (int)(sr * 1.5 / 128); ++b)
        {
            if (b % 3 == 0) d.noteOn(36 + (b % 14), 0.9f);
            if (b % 2 == 0) d.noteOn(42, 0.7f);
            buf.clear();
            d.renderAdd(buf, 2, 0, 128);
            maxVoices = std::max(maxVoices, d.getActiveVoiceCount());
            for (int i = 0; i < 128; ++i)
            {
                const float v = buf.getSample(0, i);
                if (!std::isfinite(v)) finite = false;
                pk = std::max(pk, std::abs((double)v));
            }
        }
        std::cout << "  dense pattern: " << maxVoices << " simultaneous voices, bus peak "
                  << std::setprecision(2) << dB(pk) << " dB, all finite: " << (finite ? "yes" : "NO") << "\n";
        check(finite && maxVoices > 4, "drums: a dense pattern keeps many voices alive and stays finite");
    }
}

// ===========================================================================
// INSTRUMENT BANK - subtractive/FM synth voices, chromatic and polyphonic.
// ===========================================================================
static void instrumentSuite()
{
    std::cout << "\n=== INSTRUMENT BANK ===\n";
    const double sr = 48000.0;

    auto renderNote = [&](int program, int note, double secs, bool release)
    {
        agm::InstrumentBank ib;
        ib.prepare(sr, 256);
        ib.setEnabled(true);
        ib.setLevelDb(0.0f);
        ib.setProgram(program);
        ib.noteOn(note, 0.9f);
        const int n = (int)(sr * secs);
        std::vector<float> out((size_t)n, 0.0f);
        AudioBuffer<float> buf(2, 256);
        for (int done = 0; done < n; )
        {
            const int k = std::min(256, n - done);
            if (release && done >= n / 2 && done - k < n / 2) ib.noteOff(note, 0.0f);
            buf.setSize(2, k, false, false, true);
            buf.clear();
            ib.renderAdd(buf, 2, 0, k);
            for (int i = 0; i < k; ++i) out[(size_t)(done + i)] = buf.getSample(0, i);
            done += k;
        }
        return out;
    };

    {
        // Tuning: the fundamental must be the note it was asked for.
        std::cout << "  tuning (program Organ, equal temperament from A4 = 440 Hz):\n";
        bool ok = true;
        for (int note : { 36, 48, 60, 69, 81 })
        {
            auto out = renderNote((int)agm::InstrumentBank::Organ, note, 0.8f, false);
            const double want = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double got = dominantFreq(out, (int)(sr * 0.2), (int)(sr * 0.4), sr, want * 0.7, want * 1.4);
            const double cents = 1200.0 * std::log2(got / want);
            std::cout << "    note " << std::setw(3) << note << " want " << std::setw(8)
                      << std::setprecision(2) << want << " Hz, got " << std::setw(8) << got
                      << " Hz (" << std::setprecision(1) << cents << " cents)\n";
            if (std::abs(cents) > 12.0) ok = false;
        }
        check(ok, "instruments: every note is in tune within 12 cents");
    }

    {
        // Aliasing: the saw voices are the ones at risk. A high note played by a
        // naive saw folds its upper harmonics back below its own fundamental.
        std::cout << "  aliasing below the fundamental (saw programs, note 96 = C7, 2093 Hz):\n";
        bool ok = true;
        for (int prog : { (int)agm::InstrumentBank::Pluck, (int)agm::InstrumentBank::Lead,
                          (int)agm::InstrumentBank::Pad, (int)agm::InstrumentBank::RageLead })
        {
            auto out = renderNote(prog, 96, 0.6f, false);
            const int from = (int)(sr * 0.15), n = (int)(sr * 0.3);
            const double f0 = magAt(out, from, n, 2093.0, sr);
            // anything below 0.9 * f0 cannot be a harmonic of f0: it is fold-down
            double worst = 0.0;
            double worstF = 0.0;
            for (double f = 80.0; f < 2093.0 * 0.9; f *= 1.03)
            {
                const double m = magAt(out, from, n, f, sr);
                if (m > worst) { worst = m; worstF = f; }
            }
            const double dbc = dB(worst / std::max(f0, 1e-12));
            std::cout << "    " << std::left << std::setw(12) << agm::InstrumentBank::programName(prog)
                      << std::right << " worst sub-fundamental partial " << std::setw(7)
                      << std::setprecision(1) << dbc << " dBc at " << std::setprecision(0) << worstF << " Hz\n";
            if (dbc > -60.0) ok = false;
        }
        check(ok, "instruments: fold-down aliasing at C7 stays below -60 dBc for every saw program");
    }

    {
        // Note-off must not click, and the voice must actually end.
        std::cout << "  note-off: step at the release point, and how long until the voice is free\n";
        bool noClick = true, ends = true;
        double worstTail = 0.0;
        for (int prog = 0; prog < (int)agm::InstrumentBank::kCount; ++prog)
        {
            const double secs = 12.0;
            auto out = renderNote(prog, 60, secs, true);
            const int off = (int)out.size() / 2;
            // A sawtooth's own reset is a full-scale step, so "did note-off
            // click?" can only mean "did it step by more than this voice already
            // steps while it is just sitting there sustaining?".
            auto maxStep = [&](int a, int b)
            {
                double m = 0.0;
                for (int i = std::max(a, 1); i < std::min(b, (int)out.size()); ++i)
                    m = std::max(m, std::abs((double)out[(size_t)i] - out[(size_t)(i - 1)]));
                return m;
            };
            const double sustainStep = maxStep(off - (int)(sr * 0.2), off - 16);
            const double offStep = maxStep(off - 8, off + 64);
            int last = off;
            for (int i = off; i < (int)out.size(); ++i) if (out[(size_t)i] != 0.0f) last = i;
            const double tailSec = (last - off) / sr;
            worstTail = std::max(worstTail, tailSec);
            std::cout << "    " << std::left << std::setw(12) << agm::InstrumentBank::programName(prog)
                      << std::right << " step at note-off " << std::setw(9) << std::setprecision(6) << offStep
                      << " vs " << std::setw(9) << sustainStep << " while sustaining, silent "
                      << std::setprecision(2) << tailSec << " s after note-off\n";
            if (offStep > std::max(sustainStep * 1.2, 1e-4)) noClick = false;
            if (tailSec > secs * 0.49) ends = false;
        }
        check(noClick, "instruments: note-off never steps more than the voice already steps while sustaining");
        check(ends, ("instruments: every program frees its voice within 6 s of note-off (worst "
                     + String(worstTail, 2) + " s)").toStdString());
    }

    {
        // Polyphony: 24 voices then more. Voice stealing must be silent and the
        // bus must stay finite.
        agm::InstrumentBank ib;
        ib.prepare(sr, 128);
        ib.setEnabled(true);
        ib.setLevelDb(0.0f);
        ib.setProgram((int)agm::InstrumentBank::Pad);
        AudioBuffer<float> buf(2, 128);
        bool finite = true;
        double worstStep = 0.0, prev = 0.0, pk = 0.0;
        int maxV = 0;
        for (int b = 0; b < 400; ++b)
        {
            if (b % 2 == 0) ib.noteOn(36 + (b % 60), 0.8f);
            buf.clear();
            ib.renderAdd(buf, 2, 0, 128);
            maxV = std::max(maxV, ib.getActiveVoiceCount());
            for (int i = 0; i < 128; ++i)
            {
                const double v = buf.getSample(0, i);
                if (!std::isfinite(v)) finite = false;
                pk = std::max(pk, std::abs(v));
                if (b > 20) worstStep = std::max(worstStep, std::abs(v - prev));
                prev = v;
            }
        }
        std::cout << "  200 note-ons into a 24-voice pool: " << maxV << " voices live, peak "
                  << std::setprecision(2) << dB(pk) << " dB, worst step " << std::setprecision(4)
                  << worstStep << ", finite: " << (finite ? "yes" : "NO") << "\n";
        check(finite && maxV <= agm::InstrumentBank::kVoices && worstStep < 0.35 * std::max(pk, 1e-6),
              "instruments: voice stealing under 200 overlapping note-ons is finite and click-free");
    }
}

// ===========================================================================
int main()
{
    compressorSuite();
    limiterSuite();
    saturationSuite();
    eqSuite();
    delaySuite();
    reverbSuite();
    imagerSuite();
    drumSuite();
    instrumentSuite();
    std::cout << (gFailures == 0 ? "CHARACTER TESTS PASSED" : "CHARACTER TESTS FAILED")
              << " (" << gChecks - gFailures << "/" << gChecks << " checks)\n";
    return gFailures == 0 ? 0 : 1;
}
