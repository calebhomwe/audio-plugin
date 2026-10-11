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

        {
            Harness calibration;
            calibration.disableAllModules();
            float measuredRms = 0.0f, measuredPeak = 0.0f;
            calibration.runSine(44100.0f * 200.0f / 2048.0f, 0.5f, 2.0f, 0.2f, measuredRms, measuredPeak);
            float spectrum[600];
            dynamic_cast<MixAgentAudioProcessor&>(*calibration.proc).getAnalyzerSpectrum(spectrum, 600);
            const float highest = *std::max_element(std::begin(spectrum), std::end(spectrum));
            check(std::abs(highest + 6.0206f) < 0.3f, "analyzer peak is calibrated to dBFS at high frequencies");
        }

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
        // Browsing presets must not rewrite the recipe of notes already sounding.
        {
            agm::InstrumentBank switched, control;
            switched.prepare(44100.0, 256);
            control.prepare(44100.0, 256);
            switched.setProgram((int)agm::InstrumentBank::Pluck);
            control.setProgram((int)agm::InstrumentBank::Pluck);
            switched.noteOn(60, 0.8f);
            control.noteOn(60, 0.8f);

            AudioBuffer<float> warmA(2, 256), warmB(2, 256);
            warmA.clear(); warmB.clear();
            switched.renderAdd(warmA, 2);
            control.renderAdd(warmB, 2);

            switched.setProgram((int)agm::InstrumentBank::RageLead);
            AudioBuffer<float> tailA(2, 256), tailB(2, 256);
            tailA.clear(); tailB.clear();
            switched.renderAdd(tailA, 2);
            control.renderAdd(tailB, 2);

            float maxDelta = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    maxDelta = std::max(maxDelta,
                        std::abs(tailA.getSample(ch, i) - tailB.getSample(ch, i)));
            check(maxDelta < 1.0e-6f, "program switch preserves active voice recipe");
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

        // Loading a preset is deterministic, including Init. Settings from the
        // previous preset must not leak into the next factory sound.
        {
            MixAgentAudioProcessor visited, fresh;
            visited.setCurrentProgram(4);
            visited.setCurrentProgram(1);
            fresh.setCurrentProgram(1);
            bool same = true;
            for (int i = 0; i < fresh.getParameters().size(); ++i)
                same &= std::abs(fresh.getParameters()[i]->getValue()
                                 - visited.getParameters()[i]->getValue()) < 1e-6f;
            check(same, "factory preset is independent of previously loaded preset");
            visited.setCurrentProgram(0);
            bool defaults = true;
            for (auto* parameter : visited.getParameters())
                defaults &= std::abs(parameter->getValue() - parameter->getDefaultValue()) < 1e-6f;
            check(defaults, "Init restores every parameter default");
            fresh.setCurrentProgram(5);
            MemoryBlock saved;
            fresh.getStateInformation(saved);
            visited.setStateInformation(saved.getData(), (int)saved.getSize());
            check(visited.getCurrentProgram() == 5, "state restores factory program identity");
        }

        // Host MIDI timestamps define the onset, including notes on the last
        // frame. Low melodic notes cannot be stolen by the percussion bus.
        {
            Harness timing;
            timing.disableAllModules();
            auto& p = dynamic_cast<MixAgentAudioProcessor&>(*timing.proc);
            p.setInstrumentProgram(agm::InstrumentBank::Organ);
            AudioBuffer<float> buffer(2, 1024);
            buffer.clear();
            MidiBuffer events;
            events.addEvent(MidiMessage::noteOn(1, 36, 0.9f), 768);
            timing.proc->processBlock(buffer, events);
            check(buffer.getMagnitude(0, 768) < 1e-8f && p.getInstrumentActive(),
                  "timestamped low MIDI note has no early audio and uses the melodic bank");
            buffer.clear(); events.clear();
            timing.proc->processBlock(buffer, events);
            check(buffer.getMagnitude(0, 1024) > 0.001f, "scheduled note sounds after the reported chain latency");
            events.addEvent(MidiMessage::allSoundOff(1), 0);
            buffer.clear(); timing.proc->processBlock(buffer, events);
            check(!p.getInstrumentActive(), "host all-sound-off clears instrument voices");
            events.clear(); events.addEvent(MidiMessage::noteOn(10, 36, 0.9f), 512);
            buffer.clear(); timing.proc->processBlock(buffer, events);
            check(!p.getInstrumentActive() && buffer.getMagnitude(512, 512) > 0.001f,
                  "channel 10 percussion renders at its event timestamp");

            auto layout = p.getBusesLayout();
            layout.inputBuses.set(0, AudioChannelSet::disabled());
            check(p.isBusesLayoutSupported(layout), "synth accepts stereo output with no input bus");
            timing.setOn("dly_enabled", true);
            timing.setRaw("dly_time", 2000.0f);
            timing.setRaw("dly_feedback", 0.95f);
            timing.setRaw("dly_mix", 0.5f);
            check(p.getTailLengthSeconds() > 290.0, "host tail estimate includes long feedback-delay decay");
        }

        // A full UI queue may drop note-off events. Overflow must silence the
        // queue instead of leaving notes sounding when the editor is closed.
        {
            Harness overflow;
            overflow.disableAllModules();
            auto& p = dynamic_cast<MixAgentAudioProcessor&>(*overflow.proc);
            for (int i = 0; i < 300; ++i) p.uiNoteOn(60, 0.8f);
            AudioBuffer<float> buffer(2, 512); buffer.clear();
            MidiBuffer empty;
            p.processBlock(buffer, empty);
            check(!p.getInstrumentActive() && buffer.getMagnitude(0, 512) < 1e-8f,
                  "UI note queue overflow fails safely without hanging voices");
        }

        // Gain automation must have the same 10 ms response at every host
        // block size. Previously the block length was applied on every sample.
        {
            auto renderGain = [](int blockSize)
            {
                Harness gain;
                gain.disableAllModules();
                gain.setRaw("in_gain", 12.0f);
                std::vector<float> output;
                output.reserve(4096);
                MidiBuffer empty;
                for (int offset = 0; offset < 4096; offset += blockSize)
                {
                    AudioBuffer<float> buffer(2, blockSize);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < blockSize; ++i) buffer.setSample(ch, i, 0.02f);
                    gain.proc->processBlock(buffer, empty);
                    for (int i = 0; i < blockSize; ++i) output.push_back(buffer.getSample(0, i));
                }
                return output;
            };
            const auto small = renderGain(64), large = renderGain(512);
            float delta = 0.0f;
            for (size_t i = 0; i < small.size(); ++i)
                delta = std::max(delta, std::abs(small[i] - large[i]));
            check(delta < 1e-5f, "gain smoothing is independent of host block size");
        }
    }

    std::cout << (gFailures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return gFailures == 0 ? 0 : 1;
}
