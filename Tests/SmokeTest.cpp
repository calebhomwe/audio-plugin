#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

using namespace juce;

static int gFailures = 0;

static void check(bool ok, const char* name)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << std::endl;
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
    void runSine(float freq, float amp, float secs, float settleSecs, float& rmsOut, float& peakOut)
    {
        const int total = (int)(44100.0 * secs);
        const int skip = (int)(44100.0 * settleSecs);
        const double inc = 2.0 * juce::MathConstants<double>::pi * freq / 44100.0;
        double phase = 0.0;
        double sumSq = 0.0;
        float peak = 0.0f;
        int measured = 0;
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
                const int absIndex = done + i;
                if (absIndex >= skip)
                {
                    const float v = buf.getSample(0, i);
                    sumSq += (double)v * v;
                    peak = std::max(peak, std::abs(v));
                    measured++;
                }
            }
            done += n;
        }
        rmsOut = measured > 0 ? (float)std::sqrt(sumSq / measured) : 0.0f;
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
        h.runSine(1000.0f, 0.5f, 0.7f, 0.2f, rms, peak);
        check(std::abs(agm::gainToDb(rms) - agm::gainToDb(0.5f / 1.41421356f)) < 0.5f, "bypass transparency");
        check(std::isfinite(rms) && std::isfinite(peak), "all modules off - finite output");

        h.setOn("eq_enabled", true);
        h.setRaw("eq_p1_freq", 1000.0f);
        h.setRaw("eq_p1_gain", 10.0f);
        h.setRaw("eq_p1_q", 1.0f);
        h.runSine(1000.0f, 0.2f, 0.7f, 0.2f, rms, peak);
        check(std::abs(agm::gainToDb(rms) - (agm::gainToDb(0.2f / 1.41421356f) + 10.0f)) < 1.0f, "EQ +10dB at 1kHz");

        h.disableAllModules();
        h.setOn("comp_enabled", true);
        h.setRaw("comp_thresh", -20.0f);
        h.setRaw("comp_ratio", 4.0f);
        h.setRaw("comp_knee", 0.0f);
        h.setRaw("comp_attack", 1.0f);
        h.setRaw("comp_release", 100.0f);
        h.setRaw("comp_makeup", 0.0f);
        h.setRaw("comp_mix", 1.0f);
        h.runSine(1000.0f, 0.9f, 1.0f, 0.3f, rms, peak);
        check(rms < 0.45f && dynamic_cast<MixAgentAudioProcessor&>(*h.proc).getCompGrDb() > 2.0f,
              "compressor reduces hot signal + GR metering");

        h.disableAllModules();
        h.setOn("lim_enabled", true);
        h.setRaw("lim_ceiling", -1.0f);
        h.setRaw("lim_attack", 1.0f);
        h.setRaw("lim_release", 120.0f);
        h.runSine(1000.0f, 2.0f, 0.8f, 0.3f, rms, peak);
        check(peak < 1.0f && dynamic_cast<MixAgentAudioProcessor&>(*h.proc).getLimGrDb() > 0.5f, "limiter attenuates");

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
        h.runSine(1000.0f, 0.8f, 1.5f, 0.6f, rms, peak);
        check(std::isfinite(rms) && std::isfinite(peak) && peak < 10.0f, "extreme settings - no NaN, bounded");

        auto& proc = dynamic_cast<MixAgentAudioProcessor&>(*h.proc);
        proc.setCurrentProgram(1);
        auto* thr = dynamic_cast<RangedAudioParameter*>(h.apvts().getParameter("comp_thresh"));
        check(std::abs(thr->getNormalisableRange().convertFrom0to1(thr->getValue()) - (-14.0f)) < 0.5f, "preset loads");

        MemoryBlock state;
        proc.getStateInformation(state);
        proc.setStateInformation(state.getData(), (int)state.getSize());
        h.runSine(440.0f, 0.4f, 0.5f, 0.2f, rms, peak);
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

        // regression: oversized block (1024 > prepared 512) with saturation on must not crash/OOB
        h.disableAllModules();
        h.setOn("sat_enabled", true);
        h.setRaw("sat_drive", 0.5f); h.setRaw("sat_mix", 1.0f); h.setRaw("sat_out", 0.0f);
        {
            const int big = 1024;
            AudioBuffer<float> bigBuf(2, big);
            const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;
            double phase = 0.0;
            bool finite = true; float peak = 0.0f;
            for (int rep = 0; rep < 3; ++rep)
            {
                for (int i = 0; i < big; ++i)
                {
                    const float x = 0.5f * (float)std::sin(phase);
                    phase += inc;
                    bigBuf.setSample(0, i, x);
                    bigBuf.setSample(1, i, x);
                }
                MidiBuffer empty;
                h.proc->processBlock(bigBuf, empty);
                for (int i = 0; i < big; ++i)
                {
                    const float v = std::abs(bigBuf.getSample(0, i));
                    peak = std::max(peak, v);
                    if (!std::isfinite(v)) finite = false;
                }
            }
            check(finite && peak < 1.5f, "oversized block + saturation - no OOB/NaN");
        }

        // lookahead limiter: transient riding a 0.9 pre-ramp must be caught before emerging
        h.disableAllModules();
        h.setOn("lim_enabled", true);
        h.setRaw("lim_ceiling", -1.0f);
        h.setRaw("lim_attack", 1.0f);
        h.setRaw("lim_release", 120.0f);
        {
            float limPeak = 0.0f;
            bool limFinite = true;
            const int rampLen = 2048;   // > W so the pre-ramp is fully in the line
            const int total = rampLen + 2048;
            for (int done = 0; done < total; done += 512)
            {
                const int n = std::min(512, total - done);
                AudioBuffer<float> buf(2, n);
                for (int i = 0; i < n; ++i)
                {
                    const int idx = done + i;
                    const float x = idx < rampLen ? 0.9f : (idx == rampLen ? 2.0f : 0.0f);
                    buf.setSample(0, i, x);
                    buf.setSample(1, i, x);
                }
                MidiBuffer empty;
                h.proc->processBlock(buf, empty);
                for (int i = 0; i < n; ++i)
                {
                    const float v = std::abs(buf.getSample(0, i));
                    limPeak = std::max(limPeak, v);
                    if (!std::isfinite(v)) limFinite = false;
                }
            }
            std::cout << "oversized-path limiter peak: " << limPeak << std::endl;
            check(limFinite && limPeak <= 1.003f, "limiter catches 0.9 pre-ramp transient (peak <= 1.003)");
        }

        // instrument bank: clean chain, fire notes per program (blocks <= prepared size)
        h.disableAllModules();
        h.setRaw("inst_level", 0.0f);
        dynamic_cast<MixAgentAudioProcessor&>(*h.proc).setInstrumentProgram((int)agm::InstrumentBank::Pluck);
        {
            MidiBuffer mb;
            mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 0);
            AudioBuffer<float> buf(2, 512);
            buf.clear();
            h.proc->processBlock(buf, mb);
            bool anyNonZero = false; bool allFinite = true;
            for (int i = 0; i < 512; ++i)
            {
                const float L = std::abs(buf.getSample(0, i));
                const float R = std::abs(buf.getSample(1, i));
                if (L > 1e-5f || R > 1e-5f) anyNonZero = true;
                if (!std::isfinite(L) || !std::isfinite(R)) allFinite = false;
            }
            check(anyNonZero && allFinite, "instrument bank produces finite audio");
            mb.clear();
            mb.addEvent(MidiMessage::noteOff(1, 60), 0);
            buf.clear();
            h.proc->processBlock(buf, mb);
        }
        for (int p = 0; p < (int)agm::InstrumentBank::kCount; ++p)
        {
            { MidiBuffer off; off.addEvent(MidiMessage::noteOff(1, 60), 0);
              AudioBuffer<float> b(2, 64); b.clear(); h.proc->processBlock(b, off); }
            dynamic_cast<MixAgentAudioProcessor&>(*h.proc).setInstrumentProgram(p);
            MidiBuffer mb; mb.addEvent(MidiMessage::noteOn(1, 60, 0.8f), 0);
            AudioBuffer<float> buf(2, 512); buf.clear();
            h.proc->processBlock(buf, mb);
            float peak = 0.0f; bool finite = true;
            for (int i = 0; i < 512; ++i)
            {
                const float v = std::abs(buf.getSample(0, i));
                peak = std::max(peak, v);
                if (!std::isfinite(v)) finite = false;
            }
            check(finite && peak < 2.0f, (std::string("program ") + agm::InstrumentBank::programName(p) + " finite/bounded").c_str());
        }
        // program switching mid-sustain every 64 samples across the full bank
        {
            dynamic_cast<MixAgentAudioProcessor&>(*h.proc).setInstrumentProgram(0);
            {
                MidiBuffer mb;
                mb.addEvent(MidiMessage::noteOn(1, 64, 0.9f), 0);
                AudioBuffer<float> buf(2, 512);
                buf.clear();
                h.proc->processBlock(buf, mb);
            }
            bool swFinite = true;
            float swPeak = 0.0f;
            int p = 0;
            for (int rep = 0; rep < 16 * 8; ++rep)
            {
                if (rep % 8 == 0)
                {
                    dynamic_cast<MixAgentAudioProcessor&>(*h.proc)
                        .setInstrumentProgram(p % (int)agm::InstrumentBank::kCount);
                    ++p;
                }
                AudioBuffer<float> buf(2, 64);
                buf.clear();
                MidiBuffer empty;
                h.proc->processBlock(buf, empty);
                for (int i = 0; i < 64; ++i)
                {
                    const float v = std::abs(buf.getSample(0, i));
                    swPeak = std::max(swPeak, v);
                    if (!std::isfinite(v)) swFinite = false;
                }
            }
            {
                MidiBuffer mb;
                mb.addEvent(MidiMessage::noteOff(1, 64), 0);
                AudioBuffer<float> buf(2, 64);
                buf.clear();
                h.proc->processBlock(buf, mb);
            }
            check(swFinite && swPeak < 4.0f, "16-program switch every 64 samples mid-sustain - finite/bounded");
        }
        // favorites persisted in APVTS state, survive save/load roundtrip
        auto& procRef = dynamic_cast<MixAgentAudioProcessor&>(*h.proc);
        procRef.setFavorite(2, true);
        procRef.setFavorite(9, true);
        check(procRef.isFavorite(2) && procRef.isFavorite(9) && procRef.getFavoriteCount() == 2, "favorites set");
        procRef.setFavorite(2, false);
        check(!procRef.isFavorite(2) && procRef.isFavorite(9), "favorites toggle off");
        MemoryBlock favState;
        procRef.getStateInformation(favState);
        {
            auto temp = std::make_unique<MixAgentAudioProcessor>();
            temp->setStateInformation(favState.getData(), (int)favState.getSize());
            auto& q = dynamic_cast<MixAgentAudioProcessor&>(*temp);
            check(q.isFavorite(9) && !q.isFavorite(2), "favorites survive state roundtrip");
        }
    }

    std::cout << (gFailures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return gFailures == 0 ? 0 : 1;
}
