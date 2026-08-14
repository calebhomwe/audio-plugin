#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

using namespace juce;

static int gFailures = 0;

static void check(bool ok, const char* name)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!ok)
        gFailures++;
}

struct Harness
{
    std::unique_ptr<AudioProcessor> proc;
    Harness() : proc(std::make_unique<MixAgentAudioProcessor>())
    {
        proc->setRateAndBufferSizeDetails(44100.0, 512);
        proc->prepareToPlay(44100.0, 512);
    }
    AudioProcessorValueTreeState& apvts()
    {
        return dynamic_cast<MixAgentAudioProcessor&>(*proc).getAPVTS();
    }
    void setRaw(const char* id, float raw)
    {
        if (auto* p = dynamic_cast<RangedAudioParameter*>(apvts().getParameter(id)))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(raw));
    }
    void setOn(const char* id, bool on) { setRaw(id, on ? 1.0f : 0.0f); }
    void disableAllModules()
    {
        setOn("eq_enabled", false); setOn("sat_enabled", false); setOn("comp_enabled", false);
        setOn("img_enabled", false); setOn("dly_enabled", false); setOn("rvb_enabled", false);
        setOn("lim_enabled", false); setRaw("in_gain", 0.0f); setRaw("out_gain", 0.0f);
    }
    void runSine(float freq, float amp, float secs, float& rmsOut, float& peakOut)
    {
        const int total = (int)(44100.0 * secs);
        const double inc = 2.0 * juce::MathConstants<double>::pi * freq / 44100.0;
        double phase = 0.0;
        double sumSq = 0.0;
        float peak = 0.0f;
        int done = 0;
        while (done < total)
        {
            const int n = std::min(512, total - done);
            AudioBuffer<float> buf(2, n);
            buf.clear();
            for (int i = 0; i < n; ++i)
            {
                const float x = amp * (float)std::sin(phase);
                phase += inc;
                buf.setSample(0, i, x);
                buf.setSample(1, i, x);
            }
            MidiBuffer midi;
            proc->processBlock(buf, midi);
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample(0, i);
                sumSq += (double)v * v;
                peak = std::max(peak, std::abs(v));
            }
            done += n;
        }
        rmsOut = (float)std::sqrt(sumSq / total);
        peakOut = peak;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        Harness h;
        float rms = 0.0f, peak = 0.0f;

        h.disableAllModules();
        h.runSine(1000.0f, 0.5f, 0.5f, rms, peak);
        check(std::abs(agm::gainToDb(rms) - agm::gainToDb(0.5f / 1.41421356f)) < 0.5f, "bypass transparency");
        std::cout << "  diag bypass: rms " << agm::gainToDb(rms) << " dB, expected "
                  << agm::gainToDb(0.5f / 1.41421356f) << " dB, peak " << agm::gainToDb(peak) << " dB\n";

        h.runSine(1000.0f, 0.5f, 0.5f, rms, peak);
        check(std::isfinite(rms) && std::isfinite(peak), "all modules off - finite output");

        {
            agm::Biquad b;
            b.prepare(44100.0);
            b.setPeaking(1000.0f, 10.0f, 1.0f);
            std::cout << "  diag biquad mag@1k = " << b.magnitudeDbAt(1000.0f) << " dB, mag@100 = " << b.magnitudeDbAt(100.0f) << " dB\n";
            double sum = 0.0;
            for (int i = 0; i < 4410; ++i)
            {
                const float x = 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0);
                const float y = b.process(x);
                sum += (double)y * y;
            }
            std::cout << "  diag biquad rms = " << agm::gainToDb((float)std::sqrt(sum / 4410.0)) << " dB\n";
        }

        {
            agm::Biquad br;
            br.prepare(44100.0);
            br.setPeaking(200.0f, 0.0f, 1.0f);
            const float coef = 1.0f - std::exp(-1.0f / (0.010f * 44100.0f));
            float gain = 0.0f, lastGain = 0.0f;
            double s3 = 0.0;
            for (int i = 0; i < 4410; ++i)
            {
                gain += (10.0f - gain) * coef;
                if (std::fabs(gain - lastGain) > 1e-3f)
                {
                    br.setPeaking(1000.0f, gain, 1.0f);
                    lastGain = gain;
                }
                const float x = 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0);
                const float y = br.process(x);
                if (i >= 4000)
                    s3 += (double)y * y;
            }
            std::cout << "  diag raw biquad ramp last10ms rms = " << agm::gainToDb((float)std::sqrt(s3 / 410.0)) << " dB\n";
        }

        {
            agm::EQ eq2;
            eq2.prepare(44100.0, 512);
            eq2.setEnabled(true);
            eq2.setPeakFreq(0, 1000.0f);
            eq2.setPeakGainDb(0, 10.0f);
            eq2.setPeakQ(0, 1.0f);
            std::cout << "  diag eq2 standalone response@1k = " << eq2.getResponseDb(1000.0f) << " dB\n";
            AudioBuffer<float> buf(2, 4410);
            buf.clear();
            for (int i = 0; i < 4410; ++i)
            {
                const float x = 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0);
                buf.setSample(0, i, x);
                buf.setSample(1, i, x);
            }
            eq2.process(buf);
            eq2.dbgDump();
            double sum = 0.0;
            for (int i = 0; i < 4410; ++i)
                sum += (double)buf.getSample(0, i) * buf.getSample(0, i);
            std::cout << "  diag eq2 standalone rms = " << agm::gainToDb((float)std::sqrt(sum / 4410.0)) << " dB\n";
            std::cout << "  diag eq2 response AFTER process@1k = " << eq2.getResponseDb(1000.0f) << " dB\n";
            sum = 0.0;
            for (int i = 4410 - 441; i < 4410; ++i)
                sum += (double)buf.getSample(0, i) * buf.getSample(0, i);
            std::cout << "  diag eq2 rms LAST 10ms = " << agm::gainToDb((float)std::sqrt(sum / 441.0)) << " dB\n";
            for (int i = 4000; i < 4008; ++i)
                std::cout << "  diag eq2 s[" << i << "] in=" << 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0)
                          << " out=" << buf.getSample(0, i) << "\n";
            const float argDump = 2.0f * 3.14159265f * 1000.0f * 4000.0f / 44100.0f;
            std::cout << "  diag arg = " << argDump << " sin(arg) = " << std::sin(argDump)
                      << " sin(569.9) = " << std::sin(569.9) << "\n";
        }

        h.disableAllModules();
        h.setOn("eq_enabled", true);
        h.setRaw("eq_p1_freq", 1000.0f);
        h.setRaw("eq_p1_gain", 10.0f);
        h.setRaw("eq_p1_q", 1.0f);
        {
            auto& proc2 = dynamic_cast<MixAgentAudioProcessor&>(*h.proc);
            float c2[600];
            proc2.getEqCurve(c2, 600);
            std::cout << "  diag eqcurve[1kHz] = " << c2[300] << " dB, [100Hz] = " << c2[50] << " dB\n";
        }
        h.runSine(1000.0f, 0.2f, 0.5f, rms, peak);
        check(std::abs(agm::gainToDb(rms) - (agm::gainToDb(0.2f / 1.41421356f) + 10.0f)) < 1.5f, "EQ +10dB at 1kHz");
        std::cout << "  diag eq: rms " << agm::gainToDb(rms) << " dB, expected "
                  << agm::gainToDb(0.2f / 1.41421356f) + 10.0f << " dB\n";

        h.disableAllModules();
        h.runSine(1000.0f, 0.5f, 0.5f, rms, peak);
        std::cout << "  diag comp-disabled: rms " << agm::gainToDb(rms) << " dB, compGR "
                  << dynamic_cast<MixAgentAudioProcessor&>(*h.proc).getCompGrDb()
                  << " dB, compBypass " << dynamic_cast<MixAgentAudioProcessor&>(*h.proc).debugCompBypass() << "\n";

        const char* mods[7] = { "eq_enabled", "sat_enabled", "comp_enabled", "img_enabled", "dly_enabled", "rvb_enabled", "lim_enabled" };
        for (auto* m : mods)
        {
            h.disableAllModules();
            h.setOn(m, true);
            h.runSine(1000.0f, 0.5f, 0.4f, rms, peak);
            std::cout << "  diag only " << m << " on: " << agm::gainToDb(rms) << " dB\n";
        }

        h.disableAllModules();
        h.setOn("comp_enabled", true);
        h.setRaw("comp_thresh", -20.0f);
        h.setRaw("comp_ratio", 4.0f);
        h.setRaw("comp_knee", 0.0f);
        h.setRaw("comp_attack", 1.0f);
        h.setRaw("comp_release", 100.0f);
        h.setRaw("comp_makeup", 0.0f);
        h.setRaw("comp_mix", 1.0f);
        h.runSine(1000.0f, 0.9f, 0.8f, rms, peak);
        check(rms < 0.45f && dynamic_cast<MixAgentAudioProcessor&>(*h.proc).getCompGrDb() > 2.0f,
              "compressor reduces hot signal + GR metering");

        h.disableAllModules();
        h.setOn("lim_enabled", true);
        h.setRaw("lim_ceiling", -1.0f);
        h.setRaw("lim_attack", 1.0f);
        h.setRaw("lim_release", 120.0f);
        h.runSine(1000.0f, 2.0f, 0.5f, rms, peak);
        check(peak <= agm::dbToGain(-0.7f) + 1e-3f, "limiter holds ceiling");

        h.disableAllModules();
        h.setOn("eq_enabled", true); h.setOn("sat_enabled", true); h.setOn("comp_enabled", true);
        h.setOn("img_enabled", true); h.setOn("dly_enabled", true); h.setOn("rvb_enabled", true);
        h.setOn("lim_enabled", true);
        h.setRaw("sat_drive", 1.0f); h.setRaw("sat_mix", 1.0f); h.setRaw("sat_out", 12.0f);
        h.setRaw("comp_thresh", -60.0f); h.setRaw("comp_ratio", 20.0f); h.setRaw("comp_makeup", 24.0f);
        h.setRaw("img_width", 200.0f); h.setRaw("img_balance", 1.0f);
        h.setRaw("dly_time", 20.0f); h.setRaw("dly_feedback", 0.95f); h.setRaw("dly_mix", 1.0f);
        h.setRaw("rvb_size", 1.0f); h.setRaw("rvb_decay", 10.0f); h.setRaw("rvb_mix", 1.0f); h.setRaw("rvb_predelay", 250.0f);
        h.setRaw("in_gain", 24.0f); h.setRaw("out_gain", 24.0f);
        h.runSine(1000.0f, 0.8f, 1.0f, rms, peak);
        check(std::isfinite(rms) && std::isfinite(peak) && peak < 20.0f, "extreme settings - no NaN, bounded");

        auto& proc = dynamic_cast<MixAgentAudioProcessor&>(*h.proc);
        proc.setCurrentProgram(1);
        auto* thr = dynamic_cast<RangedAudioParameter*>(h.apvts().getParameter("comp_thresh"));
        check(std::abs(thr->getNormalisableRange().convertFrom0to1(thr->getValue()) - (-14.0f)) < 0.5f, "preset loads");

        MemoryBlock state;
        proc.getStateInformation(state);
        proc.setStateInformation(state.getData(), (int)state.getSize());
        h.runSine(440.0f, 0.4f, 0.3f, rms, peak);
        check(std::isfinite(rms), "state roundtrip");

        float curve[600];
        float spec[600];
        proc.getEqCurve(curve, 600);
        proc.getAnalyzerSpectrum(spec, 600);
        bool finite = true;
        for (int i = 0; i < 600; ++i)
            if (!std::isfinite(curve[i]) || !std::isfinite(spec[i]))
                finite = false;
        check(finite, "analyzer + EQ curve finite");
    }

    std::cout << (gFailures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return gFailures == 0 ? 0 : 1;
}
