// Reverb topology bench: measures both tanks - the comb bank that ships and the
// feedback delay network offered beside it - with the same probes, the same six
// noise seeds and the same settings, and prints the distributions rather than one
// draw. Wave 4 learnt the hard way that a single noise seed can move this
// repository's reverb flatness figure by 3 dB.
//
// Not a pass/fail test (the assertions that guard the shipped tank live in
// MixAgentCharacterTest); this is the instrument the ship/no-ship decision was
// made with, kept so the numbers can be reproduced:
//
//   ./build/ReverbProbe_artefacts/Release/ReverbProbe
#include <juce_dsp/juce_dsp.h>
#include "../Source/DSP/Common.h"
#include "../Source/DSP/Reverb.h"
#include "../Source/DSP/ReverbFDN.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace juce;

static double dB(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

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

static const double kBands[7] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

// Third-octave band levels of a stretch of signal, relative to their own mean,
// and the worst deviation - the "spread" every reverb pass here has quoted.
static double bandSpread(const std::vector<float>& x, int from, int n, double sr, double* levelsOut = nullptr)
{
    double lvl[7], mean = 0.0;
    for (int i = 0; i < 7; ++i)
    {
        double acc = 0.0;
        for (int k = -3; k <= 3; ++k)
        {
            const double m = magAt(x, from, n, kBands[i] * std::pow(2.0, k / 18.0), sr);
            acc += m * m;
        }
        lvl[i] = dB(std::sqrt(acc / 7.0));
        mean += lvl[i];
    }
    mean /= 7.0;
    double spread = 0.0;
    for (int i = 0; i < 7; ++i)
    {
        if (levelsOut != nullptr) levelsOut[i] = lvl[i] - mean;
        spread = std::max(spread, std::abs(lvl[i] - mean));
    }
    return spread;
}

// RT60 by least squares over the 30 dB below the envelope peak.
static double rt60Of(const std::vector<float>& x, double sr)
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
}

// Band-limited copy of a signal, so RT60 can be measured per octave. Two passes
// of a 2-pole Butterworth band-pass either side of the centre.
static std::vector<float> bandPass(const std::vector<float>& x, double fc, double sr)
{
    dsp::IIR::Filter<float> hp, lp;
    hp.coefficients = dsp::IIR::Coefficients<float>::makeHighPass(sr, std::max(20.0, fc / std::sqrt(2.0)));
    lp.coefficients = dsp::IIR::Coefficients<float>::makeLowPass(sr, std::min(sr * 0.45, fc * std::sqrt(2.0)));
    hp.reset(); lp.reset();
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = lp.processSample(hp.processSample(x[i]));
    // second pass for a steeper skirt
    hp.reset(); lp.reset();
    for (size_t i = 0; i < y.size(); ++i)
        y[i] = lp.processSample(hp.processSample(y[i]));
    return y;
}

struct Stat
{
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double mean() const { double s = 0.0; for (double x : v) s += x; return v.empty() ? 0.0 : s / (double)v.size(); }
    double min() const { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }
    double max() const { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }
};

static std::ostream& operator<<(std::ostream& o, const Stat& s)
{
    o << std::fixed << std::setprecision(3) << s.mean() << " (" << s.min() << ".." << s.max() << ")";
    return o;
}

static const uint32_t kSeeds[6] = { 5150u, 99u, 7u, 20260928u, 424242u, 31337u };

template <typename Rev>
static void measure(const std::string& name)
{
    const double sr = 48000.0;
    std::cout << "\n================ " << name << " ================\n";

    // ---- wet-only band spread, six seeds, stereo path and mono path ----
    Stat stereoSpread, monoSpread, corr, monoLevel, stereoLevel;
    for (uint32_t seed : kSeeds)
    {
        for (int ch = 2; ch >= 1; --ch)
        {
            Rev r;
            r.prepare(sr, 512);
            r.setEnabled(true);
            r.setDamping(0.0f); r.setDecaySec(2.0f); r.setSize(0.7f);
            r.setMix(1.0f); r.setPreDelayMs(0.0f); r.setWidth(1.0f);
            const int n = (int)(sr * 6.0);
            std::vector<float> out((size_t)n);
            AudioBuffer<float> buf(ch, 512);
            uint32_t s = seed;
            for (int done = 0; done < n; done += 512)
            {
                const int k = std::min(512, n - done);
                buf.setSize(ch, k, false, false, true);
                for (int i = 0; i < k; ++i)
                {
                    s = s * 1664525u + 1013904223u;
                    const float v = 0.4f * ((float)(s >> 8) / 8388608.0f - 1.0f);
                    for (int c = 0; c < ch; ++c) buf.setSample(c, i, v);
                }
                r.process(buf);
                for (int i = 0; i < k; ++i) out[(size_t)(done + i)] = buf.getSample(0, i);
            }
            const int from = (int)(sr * 3.0), len = (int)(sr * 2.0);
            double levels[7];
            const double sp = bandSpread(out, from, len, sr, levels);
            const double lvl = dB(rmsOf(out, from, from + len));
            if (ch == 2)
            {
                stereoSpread.add(sp); stereoLevel.add(lvl);
                std::cout << "    seed " << std::setw(9) << seed << " stereo band profile (dB re. mean):";
                for (int i = 0; i < 7; ++i) std::cout << std::setw(7) << std::setprecision(2) << levels[i];
                std::cout << "   spread " << sp << "\n";
            }
            else { monoSpread.add(sp); monoLevel.add(lvl); }
        }

        // ---- L/R correlation of the tail from a mono source ----
        {
            Rev r;
            r.prepare(sr, 512);
            r.setEnabled(true);
            r.setDamping(0.4f); r.setDecaySec(2.0f); r.setSize(0.7f);
            r.setMix(1.0f); r.setWidth(1.0f); r.setPreDelayMs(0.0f);
            AudioBuffer<float> buf(2, 512);
            std::vector<float> l, rr;
            uint32_t s = seed;
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
            corr.add(std::abs(num / std::sqrt(std::max(dl * dr, 1e-30))));
        }
    }
    std::cout << "  band spread, stereo path, 6 seeds   " << stereoSpread << " dB\n";
    std::cout << "  band spread, mono path,   6 seeds   " << monoSpread << " dB\n";
    std::cout << "  |L/R correlation|,        6 seeds   " << corr << "\n";
    std::cout << "  wet level, stereo / mono            " << std::setprecision(2)
              << stereoLevel.mean() << " / " << monoLevel.mean() << " dBFS\n";

    // ---- an impulse response, for RT60 per octave and modal ringing ----
    auto impulse = [&](float decay, float sizeV, float damp, int ch, double secs)
    {
        Rev r;
        r.prepare(sr, 64);
        r.setEnabled(true);
        r.setDamping(damp); r.setDecaySec(decay); r.setSize(sizeV);
        r.setMix(1.0f); r.setWidth(1.0f); r.setPreDelayMs(0.0f);
        AudioBuffer<float> b(ch, 64);
        for (int i = 0; i < 400; ++i) { b.clear(); r.process(b); }
        const int n = (int)(sr * secs);
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

    {
        const auto ir = impulse(2.0f, 0.7f, 0.0f, 2, 8.0);
        std::cout << "  RT60 per octave (decay 2.0 s, damp 0, size 0.7):\n   ";
        double lo = 1e9, hi = 0.0;
        for (double f : kBands)
        {
            const double rt = rt60Of(bandPass(ir, f, sr), sr);
            lo = std::min(lo, rt); hi = std::max(hi, rt);
            std::cout << std::setw(7) << (int)f << "Hz " << std::setprecision(3) << rt << "s";
        }
        std::cout << "\n  RT60 spread across octaves          " << std::setprecision(3) << (hi - lo)
                  << " s = " << std::setprecision(2) << dB(hi / std::max(lo, 1e-6)) << " dB of decay rate\n";
    }

    {
        // Modal ringing: the tallest narrow peak over the band mean, 40-500 Hz,
        // measured on a late window of the impulse response.
        const auto ir = impulse(4.0f, 0.7f, 0.0f, 2, 8.0);
        const int from = (int)(sr * 1.0), n = (int)(sr * 2.0);
        double acc = 0.0, worst = -1e9;
        int count = 0;
        std::vector<double> mags;
        for (double f = 40.0; f <= 500.0; f *= std::pow(2.0, 1.0 / 48.0))
        {
            const double m = dB(magAt(ir, from, n, f, sr));
            mags.push_back(m); acc += m; ++count;
        }
        const double mean = acc / std::max(count, 1);
        for (double m : mags) worst = std::max(worst, m - mean);
        std::cout << "  modal ringing 40-500 Hz             tallest peak +" << std::setprecision(2)
                  << worst << " dB over the band mean\n";
    }

    // ---- RT60 against the decay and size controls ----
    {
        std::cout << "  RT60 vs the Decay knob:            ";
        bool mono = true;
        double prev = 0.0;
        for (float d : { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f })
        {
            const double rt = rt60Of(bandPass(impulse(d, 0.7f, 0.0f, 2, (double)d * 3.0 + 2.0), 200.0, sr), sr);
            std::cout << std::setprecision(2) << d << "s->" << std::setprecision(3) << rt << "s  ";
            if (rt <= prev) mono = false;
            prev = rt;
        }
        std::cout << (mono ? "monotonic" : "NOT MONOTONIC") << "\n";

        // Size scales the line lengths AND the per-line feedback gains, by design,
        // so RT60 is meant to stay put while the room changes size. Invariance is
        // the property to check here, not monotonicity.
        std::cout << "  RT60 vs the Size knob:             ";
        double loRt = 1e9, hiRt = 0.0;
        for (float sz : { 0.1f, 0.4f, 0.7f, 1.0f })
        {
            const double rt = rt60Of(bandPass(impulse(2.0f, sz, 0.0f, 2, 8.0), 200.0, sr), sr);
            std::cout << std::setprecision(2) << sz << "->" << std::setprecision(3) << rt << "s  ";
            loRt = std::min(loRt, rt); hiRt = std::max(hiRt, rt);
        }
        std::cout << "invariant within " << std::setprecision(1) << (100.0 * (hiRt - loRt) / std::max(loRt, 1e-6)) << " %\n";
    }

    // ---- tail reaches exact zero ----
    {
        Rev r;
        r.prepare(sr, 512);
        r.setEnabled(true);
        r.setDamping(0.5f); r.setDecaySec(3.0f); r.setSize(0.7f); r.setMix(1.0f); r.setPreDelayMs(0.0f);
        AudioBuffer<float> b(2, 512);
        uint32_t s = 1u;
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                s = s * 1664525u + 1013904223u;
                const float v = 0.5f * ((float)(s >> 8) / 8388608.0f - 1.0f);
                b.setSample(0, i, v); b.setSample(1, i, v);
            }
            r.process(b);
        }
        int silentAt = -1;
        for (int blk = 0; blk < 4000; ++blk)
        {
            b.clear();
            r.process(b);
            bool zero = true;
            for (int i = 0; i < 512 && zero; ++i)
                if (b.getSample(0, i) != 0.0f || b.getSample(1, i) != 0.0f) zero = false;
            if (zero) { silentAt = blk; break; }
        }
        std::cout << "  tail reaches exactly 0.0            "
                  << (silentAt >= 0 ? ("yes, after " + std::to_string((int)(silentAt * 512 / sr * 1000.0)) + " ms")
                                    : std::string("NO - never silent"))
                  << "\n";
    }

    // ---- sustained full scale, extreme settings: no NaN ----
    {
        Rev r;
        r.prepare(sr, 512);
        r.setEnabled(true);
        r.setDamping(0.0f); r.setDecaySec(10.0f); r.setSize(1.0f); r.setMix(1.0f); r.setPreDelayMs(250.0f);
        AudioBuffer<float> b(2, 512);
        bool finite = true;
        double peak = 0.0;
        for (int blk = 0; blk < (int)(sr * 60.0 / 512.0); ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = ((blk * 512 + i) % 32) < 16 ? 1.0f : -1.0f;
                b.setSample(0, i, v); b.setSample(1, i, -v);
            }
            r.process(b);
            for (int i = 0; i < 512; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float v = b.getSample(c, i);
                    if (!std::isfinite(v)) finite = false;
                    peak = std::max(peak, std::abs((double)v));
                }
        }
        std::cout << "  60 s of full-scale square, decay 10 s, size 1.0: all finite " << (finite ? "yes" : "NO")
                  << ", peak " << std::setprecision(2) << dB(peak) << " dBFS\n";
    }

    // ---- CPU ----
    {
        Rev r;
        r.prepare(sr, 512);
        r.setEnabled(true);
        r.setDamping(0.4f); r.setDecaySec(3.0f); r.setSize(0.7f); r.setMix(0.4f); r.setPreDelayMs(20.0f);
        AudioBuffer<float> b(2, 512);
        uint32_t s = 3u;
        for (int i = 0; i < 512; ++i)
        {
            s = s * 1664525u + 1013904223u;
            const float v = 0.3f * ((float)(s >> 8) / 8388608.0f - 1.0f);
            b.setSample(0, i, v); b.setSample(1, i, v);
        }
        const int blocks = (int)(sr * 20.0 / 512.0);
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < blocks; ++i) r.process(b);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            best = std::min(best, secs);
        }
        const double audioSecs = (double)blocks * 512.0 / sr;
        std::cout << "  CPU at 48 kHz, blocks of 512        " << std::setprecision(2)
                  << (100.0 * best / audioSecs) << " % of one core\n";
    }
}

int main(int argc, char** argv)
{
    const juce::String only = argc > 1 ? juce::String(argv[1]) : juce::String();
    std::cout << "MixAgent reverb topology bench - 48 kHz, six noise seeds\n";
    if (only != "fdn")
        measure<agm::Reverb>("COMB BANK (16 mutually-prime lines)");
    if (only != "comb")
        measure<agm::ReverbFDN>("FDN (16 lines, Hadamard feedback, Walsh taps, input diffusion)");
    std::cout << "\ndone\n";
    return 0;
}
