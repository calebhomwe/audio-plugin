#include "TestSupport.h"

// ---------------------------------------------------------------------------
static void originalSuite()
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
    check(rms < 0.45f && h.proc->getCompGrDb() > 2.0f, "compressor reduces hot signal + GR metering");

    h.disableAllModules();
    h.setOn("lim_enabled", true);
    h.setRaw("lim_ceiling", -1.0f);
    h.setRaw("lim_attack", 1.0f);
    h.setRaw("lim_release", 120.0f);
    h.runSine(1000.0f, 2.0f, 0.8f, 0.3f, rms, peak);
    check(peak < 1.0f && h.proc->getLimGrDb() > 0.5f, "limiter attenuates");

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

    auto& proc = *h.proc;
    proc.setCurrentProgram(1);
    check(std::abs(h.getRaw("comp_thresh") - (-14.0f)) < 0.5f, "preset loads");

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
        const double inc = 2.0 * MathConstants<double>::pi * 440.0 / 44100.0;
        double phase = 0.0;
        bool fin = true; float pk = 0.0f;
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
                pk = std::max(pk, v);
                if (!std::isfinite(v)) fin = false;
            }
        }
        check(fin && pk < 1.5f, "oversized block + saturation - no OOB/NaN");
    }

    // instrument bank: clean chain, fire notes per program (blocks <= prepared size)
    h.disableAllModules();
    h.setRaw("inst_level", 0.0f);
    h.proc->setInstrumentProgram((int)agm::InstrumentBank::Pluck);
    {
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 0);
        AudioBuffer<float> buf(2, 512);
        buf.clear();
        h.proc->processBlock(buf, mb);
        bool anyNonZero = false; bool fin = true;
        for (int i = 0; i < 512; ++i)
        {
            const float L = std::abs(buf.getSample(0, i));
            const float R = std::abs(buf.getSample(1, i));
            if (L > 1e-5f || R > 1e-5f) anyNonZero = true;
            if (!std::isfinite(L) || !std::isfinite(R)) fin = false;
        }
        check(anyNonZero && fin, "instrument bank produces finite audio");
        mb.clear();
        mb.addEvent(MidiMessage::noteOff(1, 60), 0);
        buf.clear();
        h.proc->processBlock(buf, mb);
    }
    for (int p = 0; p < (int)agm::InstrumentBank::kCount; ++p)
    {
        { MidiBuffer off; off.addEvent(MidiMessage::noteOff(1, 60), 0);
          AudioBuffer<float> b(2, 64); b.clear(); h.proc->processBlock(b, off); }
        h.proc->setInstrumentProgram(p);
        MidiBuffer mb; mb.addEvent(MidiMessage::noteOn(1, 60, 0.8f), 0);
        AudioBuffer<float> buf(2, 512); buf.clear();
        h.proc->processBlock(buf, mb);
        float pk = 0.0f; bool fin = true;
        for (int i = 0; i < 512; ++i)
        {
            const float v = std::abs(buf.getSample(0, i));
            pk = std::max(pk, v);
            if (!std::isfinite(v)) fin = false;
        }
        check(fin && pk < 2.0f, std::string("program ") + agm::InstrumentBank::programName(p) + " finite/bounded");
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
                maxDelta = std::max(maxDelta, std::abs(tailA.getSample(ch, i) - tailB.getSample(ch, i)));
        check(maxDelta < 1.0e-6f, "program switch preserves active voice recipe");
    }
    // favorites persisted in APVTS state, survive save/load roundtrip
    auto& procRef = *h.proc;
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
        check(temp->isFavorite(9) && !temp->isFavorite(2), "favorites survive state roundtrip");
    }
}

// ---------------------------------------------------------------------------
static void latencySuite()
{
    Harness h;
    h.disableAllModules();
    h.setOn("inst_enabled", false);

    // Limiter is a delay line even when bypassed; reported latency must match the impulse.
    const int reportedOff = h.proc->getLatencySamples();
    const int measuredOff = h.measureLatency();
    std::cout << "  latency (sat off): reported " << reportedOff << " measured " << measuredOff << "\n";
    check(reportedOff == measuredOff, "latency reported == measured (saturation bypassed)");

    h.setOn("sat_enabled", true);
    h.setRaw("sat_drive", 0.0f); h.setRaw("sat_mix", 1.0f); h.setRaw("sat_out", 0.0f);
    const int reportedOn = h.proc->getLatencySamples();
    const int measuredOn = h.measureLatency();
    std::cout << "  latency (sat on):  reported " << reportedOn << " measured " << measuredOn << "\n";
    check(reportedOn == measuredOn, "latency reported == measured (saturation engaged)");

    h.setOn("lim_enabled", true);
    const int measuredLim = h.measureLatency();
    check(h.proc->getLatencySamples() == measuredLim, "latency reported == measured (limiter engaged)");

    // 48k / 96k re-prepare updates the reported value.
    h.prepare(96000.0, 256);
    check(h.proc->getLatencySamples() == h.measureLatency(), "latency reported == measured at 96k");
}

// ---------------------------------------------------------------------------
static void realtimeSafetySuite()
{
    Harness h(44100.0, 512);
    h.enableAllModulesModerate();
    h.setRaw("inst_level", -6.0f);
    // warm up: one block with notes so every code path has run once
    {
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 0.8f), 0);
        mb.addEvent(MidiMessage::noteOn(1, 36, 0.8f), 10);
        h.runMidi(mb);
    }

    auto countAllocs = [&](int blockSize, bool withMidi, int reps)
    {
        AudioBuffer<float> buf(2, blockSize);
        gAllocCount.store(0);
        for (int r = 0; r < reps; ++r)
        {
            buf.clear();
            for (int i = 0; i < blockSize; ++i)
                buf.setSample(0, i, 0.3f * std::sin(0.05f * (float)i)), buf.setSample(1, i, 0.3f * std::cos(0.05f * (float)i));
            MidiBuffer mb;
            if (withMidi)
            {
                mb.addEvent(MidiMessage::noteOn(1, 60 + r % 12, 0.9f), 0);
                mb.addEvent(MidiMessage::noteOn(1, 36 + r % 3, 0.9f), std::min(3, blockSize - 1));
                mb.addEvent(MidiMessage::pitchWheel(1, 8192 + 400 * (r % 5)), std::min(5, blockSize - 1));
                mb.addEvent(MidiMessage::controllerEvent(1, 64, (r & 1) ? 127 : 0), std::min(7, blockSize - 1));
                mb.addEvent(MidiMessage::noteOff(1, 60 + (r + 6) % 12), std::min(9, blockSize - 1));
                h.proc->uiNoteOn(48, 0.7f);
                h.proc->uiNoteOff(48);
            }
            gCountAllocs = true;
            h.proc->processBlock(buf, mb);
            gCountAllocs = false;
        }
        return gAllocCount.load();
    };

    check(countAllocs(512, true, 40) == 0, "processBlock allocation-free (512, MIDI + UI notes)");
    check(countAllocs(1, true, 200) == 0, "processBlock allocation-free (block size 1)");
    check(countAllocs(4096, true, 8) == 0, "processBlock allocation-free (4096 > prepared 512)");
    check(countAllocs(2048, false, 8) == 0, "processBlock allocation-free (2048, no MIDI)");

    // UI note FIFO: notes queued from the GUI thread are heard on the audio thread
    {
        Harness g;
        g.disableAllModules();
        g.setOn("inst_enabled", true);
        g.proc->setInstrumentProgram((int)agm::InstrumentBank::Organ);
        g.proc->uiNoteOn(64, 0.9f);
        std::vector<float> out;
        g.run(2048, &out, nullptr);
        check(peakRange(out, 0, 2048) > 0.01f && g.proc->getInstrumentVoiceCount() == 1, "UI note queue -> audio thread note-on");
        g.proc->uiNoteOff(64);
        g.run(512, &out, nullptr);
        g.runSilence(2.0f);
        check(g.proc->getInstrumentVoiceCount() == 0, "UI note queue -> note-off releases voice");
        // overflow must drop, not corrupt
        for (int i = 0; i < 1000; ++i) g.proc->uiNoteOn(60 + (i % 5), 0.5f);
        out.clear();
        g.run(512, &out, nullptr);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && peakRange(out, 0, 512) < 1.5f, "UI note queue overflow is dropped safely");
    }
}

// ---------------------------------------------------------------------------
static void robustnessSuite()
{
    // NaN input must not poison the chain
    {
        Harness h;
        h.enableAllModulesModerate();
        std::vector<float> out;
        h.run(512, &out, nullptr, nullptr, [](int) { return std::numeric_limits<float>::quiet_NaN(); });
        h.run(512, &out, nullptr, nullptr, [](int) { return std::numeric_limits<float>::infinity(); });
        out.clear();
        const double inc = 2.0 * MathConstants<double>::pi * 1000.0 / 44100.0;
        h.run(44100, &out, nullptr, nullptr, [&](int i) { return 0.25f * (float)std::sin(inc * i); });
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        const float tail = rmsRange(out, 30000, 44100);
        std::cout << "  post-NaN recovery rms " << dbOf(tail) << " dB\n";
        check(fin && tail > 0.02f, "NaN/Inf input: output finite and signal recovers");
    }

    // sample-rate / block-size matrix, full chain, notes + drums, including blocks > prepared size
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Harness h(sr, 512);
        h.enableAllModulesModerate();
        bool ok = true;
        float worst = 0.0f;
        for (int bs : { 1, 32, 64, 512, 2048, 4096 })
        {
            MidiBuffer mb;
            mb.addEvent(MidiMessage::noteOn(1, 48, 1.0f), 0);
            mb.addEvent(MidiMessage::noteOn(1, 52, 1.0f), 0);
            mb.addEvent(MidiMessage::noteOn(1, 55, 1.0f), 0);
            mb.addEvent(MidiMessage::noteOn(1, 36, 1.0f), 0);
            mb.addEvent(MidiMessage::noteOn(1, 42, 1.0f), 0);
            std::vector<float> out;
            const double inc = 2.0 * MathConstants<double>::pi * 220.0 / sr;
            h.run((int)(sr * 0.08), &out, nullptr, &mb, [&](int i) { return 0.5f * (float)std::sin(inc * i); }, bs);
            // full-scale square wave + DC
            h.run((int)(sr * 0.04), &out, nullptr, nullptr, [&](int i) { return ((i / 50) & 1) ? 1.0f : -1.0f; }, bs);
            h.run((int)(sr * 0.02), &out, nullptr, nullptr, [](int) { return 0.7f; }, bs);
            for (float v : out)
            {
                if (!std::isfinite(v)) ok = false;
                worst = std::max(worst, std::abs(v));
            }
            MidiBuffer off;
            off.addEvent(MidiMessage::controllerEvent(1, 123, 0), 0);
            h.runMidi(off);
        }
        check(ok && worst < 2.0f, std::string("full chain finite/bounded at ") + std::to_string((int)sr) + " Hz, blocks 1..4096 (peak "
                                       + std::to_string(worst) + ")");
    }

    // mono bus layout
    {
        Harness h;
        h.enableAllModulesModerate();
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 0);
        std::vector<float> out;
        const double inc = 2.0 * MathConstants<double>::pi * 440.0 / 44100.0;
        h.run(8192, &out, nullptr, &mb, [&](int i) { return 0.3f * (float)std::sin(inc * i); }, 0, 1);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && peakRange(out, 0, 8192) > 0.05f && peakRange(out, 0, 8192) < 1.5f, "mono layout: finite, instrument audible");
    }

    // oversized block with saturation, larger than the earlier regression (4096 vs prepared 512)
    {
        Harness h;
        h.disableAllModules();
        h.setOn("sat_enabled", true);
        h.setRaw("sat_drive", 0.8f); h.setRaw("sat_mix", 1.0f);
        std::vector<float> out;
        const double inc = 2.0 * MathConstants<double>::pi * 440.0 / 44100.0;
        h.run(4096 * 4, &out, nullptr, nullptr, [&](int i) { return 0.5f * (float)std::sin(inc * i); }, 4096);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && peakRange(out, 0, (int)out.size()) < 1.5f, "saturation: 4096-sample block on 512-sample prepare");
    }
}

// ---------------------------------------------------------------------------
static void instrumentSuite()
{
    using IB = agm::InstrumentBank;

    // every program: release stage exists, envelope reaches exactly zero and frees the voice
    for (int p = 0; p < (int)IB::kCount; ++p)
    {
        IB bank;
        bank.prepare(44100.0, 512);
        bank.setProgram(p);
        bank.setLevelDb(0.0f);
        bank.noteOn(60, 0.9f);
        AudioBuffer<float> buf(2, 512);
        auto render = [&](float secs, std::vector<float>* out)
        {
            const int total = (int)(44100.0 * secs);
            for (int done = 0; done < total; done += 512)
            {
                buf.clear();
                bank.renderAdd(buf, 2);
                if (out) for (int i = 0; i < 512; ++i) out->push_back(buf.getSample(0, i));
            }
        };
        std::vector<float> held;
        render(0.3f, &held);
        std::vector<float> rel;
        bank.noteOff(60, 0.0f);
        render(0.02f, &rel);
        // note-off must not jump: compare the largest step right after release to the steady-state step
        const float steady = maxAbsDiff(held, (int)held.size() - 4410, (int)held.size());
        const float atRelease = std::max(maxAbsDiff(rel, 1, (int)rel.size()), std::abs(rel[0] - held.back()));
        check(atRelease <= std::max(steady * 1.5f, 0.02f),
              std::string("program ") + IB::programName(p) + " note-off is click-free (step " + std::to_string(atRelease)
                  + " vs steady " + std::to_string(steady) + ")");
        float secs = 0.0f;
        while (bank.isActive() && secs < 20.0f) { render(0.25f, nullptr); secs += 0.25f; }
        buf.clear();
        bank.renderAdd(buf, 2);
        std::cout << "  " << IB::programName(p) << " silent after ~" << secs << " s\n";
        check(!bank.isActive() && peakOf(buf) == 0.0f && secs < 15.0f,
              std::string("program ") + IB::programName(p) + " envelope reaches exact zero and frees voice");
    }

    // voice lifecycle: 24-voice Pad chord released, measure how long voices stay allocated and the CPU cost
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Pad);
        h.setRaw("inst_level", -12.0f);
        MidiBuffer chord;
        for (int i = 0; i < IB::kVoices; ++i)
            chord.addEvent(MidiMessage::noteOn(1, 50 + i, 0.9f), 0);
        h.runMidi(chord, 1.0f);
        MidiBuffer off;
        for (int i = 0; i < IB::kVoices; ++i)
            off.addEvent(MidiMessage::noteOff(1, 50 + i), 0);
        std::vector<float> out;
        const std::clock_t c0 = std::clock();
        double voiceSeconds = 0.0;
        {
            MidiBuffer offCopy(off);
            const int total = 44100 * 20;
            AudioBuffer<float> buf(2, 512);
            for (int done = 0; done < total; done += 512)
            {
                buf.clear();
                MidiBuffer m;
                if (done == 0) m = offCopy;
                h.proc->processBlock(buf, m);
                voiceSeconds += h.proc->getInstrumentVoiceCount() * 512.0 / 44100.0;
                for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(0, i));
            }
        }
        const double cpuMs = 1000.0 * (double)(std::clock() - c0) / CLOCKS_PER_SEC;
        int lastNonZero = 0;
        for (int i = 0; i < (int)out.size(); ++i) if (out[(size_t)i] != 0.0f) lastNonZero = i;
        // click check at the free point: the step at the end of the tail must be tiny
        const float endStep = maxAbsDiff(out, std::max(1, lastNonZero - 2205), lastNonZero + 2);
        std::cout << "  24-voice Pad release: tail ends at " << lastNonZero / 44100.0 << " s, " << voiceSeconds << " voice-seconds rendered, 20 s took "
                  << (int)cpuMs << " ms CPU, end step " << endStep << ", voices left " << h.proc->getInstrumentVoiceCount() << "\n";
        check(h.proc->getInstrumentVoiceCount() == 0 && lastNonZero < 44100 * 6 && endStep < 1e-3f,
              "24-voice Pad chord frees every voice within 6 s of release, no click at the free point");
    }

    // velocity curve monotonic
    {
        IB bank;
        bank.prepare(44100.0, 512);
        bank.setProgram((int)IB::Sub);
        float last = -1.0f;
        bool mono = true;
        for (float vel : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f })
        {
            bank.reset();
            bank.noteOn(48, vel);
            AudioBuffer<float> buf(2, 4096);
            buf.clear();
            bank.renderAdd(buf, 2);
            const float pk = peakOf(buf);
            if (pk <= last) mono = false;
            last = pk;
        }
        check(mono, "velocity curve strictly increasing");
    }

    // retrigger of a sounding note: no pop (Sub = pure sine, so any step is a click)
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Sub);
        h.setRaw("inst_level", 0.0f);
        h.note(60, true, 1.0f, 0.5f);
        std::vector<float> before;
        h.run(4410, &before, nullptr);
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 1.0f), 0);
        std::vector<float> after;
        h.run(2048, &after, nullptr, &mb);
        const float steady = maxAbsDiff(before, 1, (int)before.size());
        const float step = std::max(maxAbsDiff(after, 1, (int)after.size()), std::abs(after[0] - before.back()));
        std::cout << "  retrigger step " << step << " vs steady " << steady << "\n";
        check(step <= steady * 3.0f, "retriggering a held note is click-free");
    }

    // polyphony: 24 voices honoured, 25th steals without a pop, output bounded
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Sub);
        h.setRaw("inst_level", -30.0f);
        MidiBuffer chord;
        for (int i = 0; i < IB::kVoices; ++i)
            chord.addEvent(MidiMessage::noteOn(1, 50 + i, 0.8f), 0);
        h.runMidi(chord, 0.5f);
        check(h.proc->getInstrumentVoiceCount() == IB::kVoices, "24 simultaneous voices sound");
        std::vector<float> before;
        h.run(4410, &before, nullptr);
        MidiBuffer extra;
        extra.addEvent(MidiMessage::noteOn(1, 72, 0.8f), 0);
        std::vector<float> after;
        h.run(2048, &after, nullptr, &extra);
        check(h.proc->getInstrumentVoiceCount() == IB::kVoices, "polyphony limit honoured (25th note steals)");
        const float steady = maxAbsDiff(before, 1, (int)before.size());
        const float step = std::max(maxAbsDiff(after, 1, (int)after.size()), std::abs(after[0] - before.back()));
        std::cout << "  steal step " << step << " vs steady " << steady << "\n";
        check(step <= steady * 2.0f, "voice stealing is click-free");
    }
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Sub);
        h.setRaw("inst_level", 6.0f);
        MidiBuffer chord;
        for (int i = 0; i < IB::kVoices; ++i)
            chord.addEvent(MidiMessage::noteOn(1, 52 + i, 1.0f), 0);
        std::vector<float> out;
        h.run(22050, &out, nullptr, &chord);
        const float pk = peakRange(out, 0, (int)out.size());
        std::cout << "  24-voice bus peak " << pk << " (" << h.proc->getInstrumentVoiceCount() << " voices)\n";
        check(pk <= 1.0f && pk > 0.5f && h.proc->getInstrumentVoiceCount() == IB::kVoices,
              "24 voices at +6 dB: synth bus bounded below 0 dBFS");
        // unit: the bus limiter is identity below the knee and bounded above it
        bool unit = true;
        for (float x = 0.0f; x < 0.85f; x += 0.01f) if (agm::softClipBus(x) != x || agm::softClipBus(-x) != -x) unit = false;
        float last = 0.0f;
        for (float x = 0.85f; x < 200.0f; x *= 1.05f)
        {
            const float y = agm::softClipBus(x);
            if (y < last || y > 1.0f || (x < 2.0f && y <= last) || agm::softClipBus(-x) != -y) unit = false;
            last = y;
        }
        check(unit && agm::softClipBus(std::numeric_limits<float>::infinity()) <= 1.0f, "softClipBus: identity below 0.85, monotonic, bounded to 1.0");
    }
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Sub);
        std::vector<float> out;
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 52, 0.9f), 0);
        h.run(22050, &out, nullptr, &mb);
        const float pk = peakRange(out, 4410, (int)out.size());
        std::cout << "  single-voice default peak " << pk << "\n";
        check(pk > 0.1f && pk < 0.4f, "single voice at default level well below the bus knee");
    }

    // sample-accurate note-on inside the block
    {
        Harness h(44100.0, 512);
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Organ);
        h.setRaw("inst_level", 0.0f);
        h.runSilence(0.2f);
        const int lat = h.proc->getLatencySamples();
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 1.0f), 256);
        std::vector<float> out;
        h.run(512, &out, nullptr, &mb);
        h.run(512, &out, nullptr);
        check(exactlyZero(out, 0, 256 + lat) && peakRange(out, 256 + lat, 256 + lat + 64) > 1e-4f,
              "note-on at sample offset 256 starts exactly there");
        // note-off at offset: voice releases before the block end
        MidiBuffer off;
        off.addEvent(MidiMessage::noteOff(1, 60), 100);
        h.run(512, &out, nullptr, &off);
        h.runSilence(2.0f);
        check(h.proc->getInstrumentVoiceCount() == 0, "note-off at sample offset releases");
    }

    // pitch bend +2 semitones
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Sub);
        h.setRaw("inst_level", 0.0f);
        h.note(69, true, 1.0f, 0.3f);
        std::vector<float> base;
        h.run(44100, &base, nullptr);
        MidiBuffer bend;
        bend.addEvent(MidiMessage::pitchWheel(1, 16383), 0);
        std::vector<float> bent;
        h.run(4410, &bent, nullptr, &bend);
        bent.clear();
        h.run(44100, &bent, nullptr);
        const float f0 = zeroCrossingFreq(base, 4410, 44100, 44100.0);
        const float f1 = zeroCrossingFreq(bent, 0, 44100, 44100.0);
        const float ratio = f1 / std::max(f0, 1e-3f);
        std::cout << "  pitch bend: " << f0 << " Hz -> " << f1 << " Hz (ratio " << ratio << ")\n";
        check(std::abs(f0 - 220.0f) < 2.0f, "Sub program plays A3 an octave down (220 Hz at note 69)");
        check(std::abs(ratio - std::pow(2.0f, 2.0f / 12.0f)) < 0.01f, "pitch bend max = +2 semitones");
        MidiBuffer centre;
        centre.addEvent(MidiMessage::pitchWheel(1, 8192), 0);
        h.runMidi(centre, 0.1f);
        std::vector<float> back;
        h.run(44100, &back, nullptr);
        check(std::abs(zeroCrossingFreq(back, 0, 44100, 44100.0) - f0) < 1.0f, "pitch bend returns to centre");
    }

    // sustain pedal, all-notes-off, all-sound-off, reset
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Organ);
        MidiBuffer ped;
        ped.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
        ped.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 1);
        h.runMidi(ped, 0.1f);
        h.note(60, false, 0.0f, 0.5f);
        check(h.proc->getInstrumentVoiceCount() == 1, "sustain pedal holds a released note");
        MidiBuffer up;
        up.addEvent(MidiMessage::controllerEvent(1, 64, 0), 0);
        h.runMidi(up, 2.0f);
        check(h.proc->getInstrumentVoiceCount() == 0, "pedal release lets the note decay");

        h.note(60, true, 0.9f, 0.1f);
        h.note(64, true, 0.9f, 0.1f);
        MidiBuffer ano;
        ano.addEvent(MidiMessage::controllerEvent(1, 123, 0), 0);
        std::vector<float> tail;
        MidiBuffer anoCopy(ano);
        h.run(512, &tail, nullptr, &anoCopy);
        const float step = maxAbsDiff(tail, 1, 512);
        h.runSilence(1.0f);
        check(h.proc->getInstrumentVoiceCount() == 0 && step < 0.1f, "CC123 all-notes-off releases everything without a bang");

        h.note(60, true, 0.9f, 0.1f);
        MidiBuffer aso;
        aso.addEvent(MidiMessage::controllerEvent(1, 120, 0), 0);
        h.runMidi(aso);
        check(h.proc->getInstrumentVoiceCount() == 0, "CC120 all-sound-off silences immediately");

        h.note(60, true, 0.9f, 0.1f);
        h.note(67, true, 0.9f, 0.1f);
        h.prepare(48000.0, 256);
        std::vector<float> out;
        h.run(4096, &out, nullptr);
        check(h.proc->getInstrumentVoiceCount() == 0 && exactlyZero(out, 0, 4096), "prepareToPlay/reset: all notes off, silent");
    }

    // MIDI program change: applied at its sample offset, parameter follows on the message thread
    {
        auto render = [](int startProgram, bool sendPc, std::vector<float>& out, MixAgentAudioProcessor** keep = nullptr)
        {
            auto h = std::make_unique<Harness>();
            h->disableAllModules();
            h->proc->setInstrumentProgram(startProgram);
            h->runSilence(0.2f);
            MidiBuffer mb;
            if (sendPc) mb.addEvent(MidiMessage::programChange(1, (int)IB::Pluck), 256);
            mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 300);
            h->run(512, &out, nullptr, &mb);
            h->run(2048, &out, nullptr);
            const int prog = h->proc->getInstrumentProgram();
            if (keep) *keep = nullptr;
            return prog;
        };
        std::vector<float> withPc, ctrlNew, ctrlOld;
        const int progAfter = render((int)IB::Pad, true, withPc);
        render((int)IB::Pluck, false, ctrlNew);
        render((int)IB::Pad, false, ctrlOld);
        float dNew = 0.0f, dOld = 0.0f;
        for (size_t i = 0; i < withPc.size(); ++i)
        {
            dNew = std::max(dNew, std::abs(withPc[i] - ctrlNew[i]));
            dOld = std::max(dOld, std::abs(withPc[i] - ctrlOld[i]));
        }
        std::cout << "  MIDI program change: diff vs Pluck control " << dNew << ", vs Pad control " << dOld << "\n";
        check(progAfter == (int)IB::Pluck && dNew < 1e-6f && dOld > 1e-3f,
              "MIDI program change at offset 256 switches the bank before the note at 300");
        Harness h;
        MidiBuffer pc;
        pc.addEvent(MidiMessage::programChange(1, (int)IB::Organ), 0);
        h.runMidi(pc);
        MessageManager::getInstance()->runDispatchLoopUntil(100);
        check((int)h.getRaw("inst_program") == (int)IB::Organ, "MIDI program change is reflected in the inst_program parameter");
        MidiBuffer drumPc;
        drumPc.addEvent(MidiMessage::programChange(10, 3), 0);
        h.runMidi(drumPc);
        check(h.proc->getInstrumentProgram() == (int)IB::Organ, "program change on the drum channel is ignored");
    }

    // program change while a note is held: no crash, old voice keeps its recipe, new note uses the new one
    {
        Harness h;
        h.disableAllModules();
        h.proc->setInstrumentProgram((int)IB::Pad);
        h.note(60, true, 0.9f, 0.3f);
        h.setRaw("inst_program", (float)IB::Pluck);
        h.note(64, true, 0.9f, 0.1f);
        std::vector<float> out;
        h.run(8192, &out, nullptr);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && h.proc->getInstrumentVoiceCount() == 2 && h.proc->getInstrumentProgram() == (int)IB::Pluck,
              "program change mid-note is safe");
    }
}

// ---------------------------------------------------------------------------
static void drumSuite()
{
    Harness h;
    h.disableAllModules();
    h.setOn("inst_enabled", false);
    h.setRaw("drum_level", 0.0f);
    h.runSilence(0.5f); // let the bypass crossfades settle

    // idle -> exactly silent
    {
        std::vector<float> out;
        h.run(4096, &out, nullptr);
        check(exactlyZero(out, 0, 4096) && !h.proc->getDrumsActive(), "drum engine idle is exactly silent");
    }
    // kick: sounds, ignores note-off (tail-to-end), then ends and goes exactly silent
    {
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 36, 1.0f), 0);
        std::vector<float> out;
        h.run(4410, &out, nullptr, &mb);
        const float early = peakRange(out, 0, 4410);
        MidiBuffer off;
        off.addEvent(MidiMessage::noteOff(1, 36), 0);
        std::vector<float> after;
        h.run(4410, &after, nullptr, &off);
        const float cont = peakRange(after, 0, 4410);
        check(early > 0.1f && cont > 0.01f && h.proc->getDrumsActive(), "kick plays and runs to its natural end (note-off ignored)");
        h.runSilence(2.0f);
        std::vector<float> idle;
        h.run(2048, &idle, nullptr);
        check(!h.proc->getDrumsActive() && exactlyZero(idle, 0, 2048), "kick tail ends, engine exactly silent again");
    }
    // note map: closed hat (42) is much brighter than kick (36); snare (38) between
    auto zcr = [&](int note, int channel = 1)
    {
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(channel, note, 1.0f), 0);
        std::vector<float> out;
        h.run(4410, &out, nullptr, &mb);
        h.runSilence(2.5f);
        return zeroCrossingFreq(out, h.proc->getLatencySamples(), 2205, 44100.0);
    };
    const float kickZ = zcr(36), snareZ = zcr(38), hatZ = zcr(42);
    std::cout << "  drum zero-crossing rates: kick " << kickZ << " snare " << snareZ << " hat " << hatZ << "\n";
    check(kickZ < snareZ && snareZ < hatZ && kickZ < 300.0f && hatZ > 3000.0f, "drum note map: kick / snare / hat have the expected spectra");
    // open hat (46) rings longer than closed hat (42)
    auto ringLen = [&](int note)
    {
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, note, 1.0f), 0);
        std::vector<float> out;
        h.run(44100 * 2, &out, nullptr, &mb);
        int last = 0;
        for (int i = 0; i < (int)out.size(); ++i) if (out[(size_t)i] != 0.0f) last = i;
        h.runSilence(0.5f);
        return last;
    };
    const int closedLen = ringLen(42), openLen = ringLen(46);
    std::cout << "  hat lengths: closed " << closedLen << " open " << openLen << " samples\n";
    check(openLen > closedLen * 2, "open hat rings longer than closed hat");
    // every GM note 35..61 on channel 10 renders something; 57 / 60 reachable there
    {
        bool all = true;
        for (int n = 35; n <= 61; ++n)
        {
            MidiBuffer mb;
            mb.addEvent(MidiMessage::noteOn(10, n, 1.0f), 0);
            std::vector<float> out;
            h.run(2048, &out, nullptr, &mb);
            if (peakRange(out, 0, 2048) < 1e-3f) { all = false; std::cout << "  drum note " << n << " silent\n"; }
            MidiBuffer off;
            off.addEvent(MidiMessage::controllerEvent(1, 120, 0), 0);
            h.runMidi(off, 0.2f);
        }
        check(all, "drum map: every note 35..61 on MIDI channel 10 renders");
        const float crashZ = zcr(57, 10);
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(10, 60, 1.0f), 0);
        std::vector<float> out;
        h.run(4410, &out, nullptr, &mb);
        const float bongoZ = zeroCrossingFreq(out, h.proc->getLatencySamples(), 2205, 44100.0);
        h.runSilence(2.5f);
        std::cout << "  crash2 (57) zcr " << crashZ << ", bongo (60, ch10) zcr " << bongoZ << "\n";
        // 57 is a crash: a high-passed inharmonic metal cluster, so its zero
        // crossings sit in the kilohertz. 60 is a hi bongo: a tuned membrane
        // around 310 Hz. Before the per-note drum table, 60 was the bass-drum
        // recipe (a 42 Hz body) and 57 was the snare recipe.
        check(crashZ > 3000.0f && bongoZ > 200.0f && bongoZ < 700.0f,
              "GM 57 renders as metal in the kilohertz, 60 (channel 10) as a tuned bongo");
        // notes above 49 on channel 1 still belong to the instrument
        Harness g;
        g.disableAllModules();
        g.setOn("inst_enabled", true);
        MidiBuffer inst;
        inst.addEvent(MidiMessage::noteOn(1, 57, 0.9f), 0);
        g.runMidi(inst);
        check(g.proc->getInstrumentVoiceCount() == 1 && !g.proc->getDrumsActive(), "note 57 on channel 1 plays the instrument, not a drum");
        // the pad grid (UI notes 48-59) must play the instrument even for 48/49
        g.proc->uiNoteOn(48, 0.9f);
        g.proc->uiNoteOn(49, 0.9f);
        g.runSilence(0.05f);
        check(g.proc->getInstrumentVoiceCount() == 3 && !g.proc->getDrumsActive(), "UI pads 48/49 play the instrument, never the legacy drum range");
    }

    // round-robin: a second kick does not cut the first; pool exhaustion recycles safely
    {
        MidiBuffer two;
        two.addEvent(MidiMessage::noteOn(1, 36, 1.0f), 0);
        two.addEvent(MidiMessage::noteOn(1, 36, 1.0f), 441);
        h.runMidi(two);
        check(h.proc->getDrumVoiceCount() == 2, "round-robin: second kick gets its own voice");
        MidiBuffer many;
        for (int i = 0; i < 14; ++i) many.addEvent(MidiMessage::noteOn(1, 36, 1.0f), i * 20);
        std::vector<float> out;
        h.run(512, &out, nullptr, &many);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && h.proc->getDrumVoiceCount() == 10, "kick pool (10) recycles when exhausted, output finite");
        h.runSilence(2.5f);
    }
}

// ---------------------------------------------------------------------------
static void fxSuite()
{
    // EQ: gains truly in dB, stable at extreme Q, matches its own analytic curve
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("eq_enabled", true);
        float rms = 0.0f, peak = 0.0f;
        const float ref = agm::gainToDb(0.1f / 1.41421356f);

        h.setRaw("eq_p1_freq", 1000.0f); h.setRaw("eq_p1_gain", -15.0f); h.setRaw("eq_p1_q", 10.0f);
        h.runSine(1000.0f, 0.1f, 1.0f, 0.4f, rms, peak);
        std::cout << "  EQ -15 dB Q10 @1k: " << agm::gainToDb(rms) - ref << " dB\n";
        check(std::abs((agm::gainToDb(rms) - ref) + 15.0f) < 1.0f, "EQ -15 dB at Q=10 is -15 dB");
        h.setRaw("eq_p1_gain", 0.0f);

        h.setRaw("eq_p3_freq", 14000.0f); h.setRaw("eq_p3_gain", 15.0f); h.setRaw("eq_p3_q", 10.0f);
        h.runSine(14000.0f, 0.1f, 1.0f, 0.4f, rms, peak);
        std::cout << "  EQ +15 dB Q10 @14k: " << agm::gainToDb(rms) - ref << " dB\n";
        check(std::abs((agm::gainToDb(rms) - ref) - 15.0f) < 1.0f && std::isfinite(peak), "EQ +15 dB at Q=10, 14 kHz @44.1k stable and exact");
        float curve[600];
        h.proc->getEqCurve(curve, 600);
        float best = 0.0f;
        for (float c : curve) best = std::max(best, c);
        check(std::abs(best - 15.0f) < 0.5f, "EQ analytic curve agrees with the boost");
        h.setRaw("eq_p3_gain", 0.0f);

        h.setRaw("eq_lsf_freq", 120.0f); h.setRaw("eq_lsf_gain", 15.0f);
        h.runSine(20.0f, 0.1f, 1.2f, 0.7f, rms, peak);
        std::cout << "  EQ low shelf +15 @120, measured @20 Hz: " << agm::gainToDb(rms) - ref << " dB\n";
        check(std::abs((agm::gainToDb(rms) - ref) - 15.0f) < 2.0f, "low shelf +15 dB reaches +15 dB below the corner");
        h.setRaw("eq_lsf_gain", 0.0f);

        h.setRaw("eq_hsf_freq", 800.0f); h.setRaw("eq_hsf_gain", -15.0f);
        h.runSine(15000.0f, 0.1f, 1.0f, 0.4f, rms, peak);
        std::cout << "  EQ high shelf -15 @800, measured @15 kHz: " << agm::gainToDb(rms) - ref << " dB\n";
        check(std::abs((agm::gainToDb(rms) - ref) + 15.0f) < 2.0f, "high shelf -15 dB reaches -15 dB above the corner");
        h.setRaw("eq_hsf_gain", 0.0f);

        // HP / LP cut: -3 dB at the corner within 1 dB
        h.setOn("eq_hp_enabled", true); h.setRaw("eq_hp_freq", 200.0f);
        h.runSine(200.0f, 0.1f, 1.0f, 0.4f, rms, peak);
        check(std::abs((agm::gainToDb(rms) - ref) + 3.0f) < 1.0f, "high-pass -3 dB at its corner");
        h.setOn("eq_hp_enabled", false);
    }

    // Compressor: ratio, attack, release
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("comp_enabled", true);
        h.setRaw("comp_thresh", -30.0f); h.setRaw("comp_ratio", 4.0f); h.setRaw("comp_knee", 0.0f);
        h.setRaw("comp_attack", 1.0f); h.setRaw("comp_release", 50.0f); h.setRaw("comp_makeup", 0.0f); h.setRaw("comp_mix", 1.0f);
        float rms1, pk1, rms2, pk2;
        h.runSine(1000.0f, 0.5f, 1.0f, 0.5f, rms1, pk1);
        h.runSine(1000.0f, 0.25f, 1.0f, 0.5f, rms2, pk2);
        const float in1 = dbOf(0.5f), in2 = dbOf(0.25f);
        const float out1 = dbOf(pk1), out2 = dbOf(pk2);
        const float slope = (out1 - out2) / (in1 - in2);
        const float expected1 = -30.0f + (in1 + 30.0f) / 4.0f;
        std::cout << "  comp 4:1: in " << in1 << " -> out " << out1 << " (expect " << expected1 << "), slope " << slope << "\n";
        check(std::abs(out1 - expected1) < 0.5f, "compressor 4:1 static curve (-6 dBFS in)");
        check(std::abs(slope - 0.25f) < 0.03f, "compressor slope above threshold = 1/ratio");
        h.setRaw("comp_ratio", 20.0f);
        h.runSine(1000.0f, 0.5f, 1.0f, 0.5f, rms1, pk1);
        h.runSine(1000.0f, 0.25f, 1.0f, 0.5f, rms2, pk2);
        check(std::abs((dbOf(pk1) - dbOf(pk2)) / (in1 - in2) - 0.05f) < 0.03f, "compressor 20:1 slope");
        h.setRaw("comp_ratio", 4.0f);

        // attack: step 0.05 -> 0.8; GR should reach 63 % of final ~attackMs after the step
        h.setRaw("comp_attack", 10.0f); h.setRaw("comp_release", 200.0f);
        const double inc = 2.0 * MathConstants<double>::pi * 2000.0 / 44100.0;
        h.run(22050, nullptr, nullptr, nullptr, [&](int i) { return 0.05f * (float)std::sin(inc * i); });
        std::vector<float> out;
        h.run(22050, &out, nullptr, nullptr, [&](int i) { return 0.8f * (float)std::sin(inc * i); });
        const int lat = h.proc->getLatencySamples();
        auto grAt = [&](int fromMs, int toMs)
        {
            const float pk = peakRange(out, lat + fromMs * 44, lat + toMs * 44);
            return dbOf(0.8f) - dbOf(pk);
        };
        const float grFinal = grAt(400, 500);
        int t63 = -1;
        for (int ms = 1; ms < 200; ++ms)
            if (grAt(ms, ms + 1) >= 0.63f * grFinal) { t63 = ms; break; }
        std::cout << "  comp attack 10 ms: GR final " << grFinal << " dB, 63% at " << t63 << " ms\n";
        check(t63 >= 5 && t63 <= 16, "compressor attack time (10 ms) within tolerance");
        // release: step 0.8 -> 0.05; GR excess decays to 37 % ~releaseMs later
        std::vector<float> rel;
        h.run(44100, &rel, nullptr, nullptr, [&](int i) { return 0.05f * (float)std::sin(inc * i); });
        auto grRel = [&](int fromMs, int toMs)
        {
            const float pk = peakRange(rel, lat + fromMs * 44, lat + toMs * 44);
            return dbOf(0.05f) - dbOf(pk);
        };
        const float grStart = grRel(0, 2), grEnd = grRel(900, 1000);
        int t37 = -1;
        for (int ms = 2; ms < 900; ms += 2)
            if (grRel(ms, ms + 2) - grEnd <= 0.37f * (grStart - grEnd)) { t37 = ms; break; }
        std::cout << "  comp release 200 ms: GR " << grStart << " -> " << grEnd << " dB, 37% at " << t37 << " ms\n";
        check(t37 >= 120 && t37 <= 300, "compressor release time (200 ms) within tolerance");
    }

    // Imager: mono compatible
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("img_enabled", true);
        h.setRaw("img_width", 200.0f);
        std::vector<float> L, R;
        const double inc = 2.0 * MathConstants<double>::pi * 440.0 / 44100.0;
        h.run(22050, &L, &R, nullptr, [&](int i) { return 0.5f * (float)std::sin(inc * i); });
        float d = 0.0f;
        for (size_t i = 4410; i < L.size(); ++i) d = std::max(d, std::abs(L[i] - R[i]));
        check(d < 1e-5f && peakRange(L, 4410, 22050) > 0.4f, "imager width 200%: mono input stays mono, level kept");
        // decorrelated input, width 0 -> mono; mono button -> mono
        auto stereoRun = [&](std::vector<float>& l, std::vector<float>& r)
        {
            AudioBuffer<float> buf(2, 512);
            for (int blk = 0; blk < 40; ++blk)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const int n = blk * 512 + i;
                    buf.setSample(0, i, 0.5f * (float)std::sin(inc * n));
                    buf.setSample(1, i, 0.5f * (float)std::sin(inc * 1.5 * n));
                }
                MidiBuffer m;
                h.proc->processBlock(buf, m);
                for (int i = 0; i < 512; ++i) l.push_back(buf.getSample(0, i)), r.push_back(buf.getSample(1, i));
            }
        };
        h.setRaw("img_width", 0.0f);
        std::vector<float> l0, r0;
        stereoRun(l0, r0);
        d = 0.0f;
        for (size_t i = 8192; i < l0.size(); ++i) d = std::max(d, std::abs(l0[i] - r0[i]));
        check(d < 1e-4f, "imager width 0 collapses stereo to mono");
        h.setRaw("img_width", 150.0f); h.setOn("img_mono", true);
        std::vector<float> l1, r1;
        stereoRun(l1, r1);
        d = 0.0f;
        for (size_t i = 8192; i < l1.size(); ++i) d = std::max(d, std::abs(l1[i] - r1[i]));
        check(d < 1e-4f, "imager mono switch collapses to mono");
    }

    // Delay: bounded feedback decays, time changes are click-free
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("dly_enabled", true);
        h.setRaw("dly_time", 100.0f); h.setRaw("dly_feedback", 0.95f); h.setRaw("dly_mix", 1.0f); h.setRaw("dly_damp", 0.0f);
        h.runSilence(0.2f);
        std::vector<float> out;
        h.run(44100 * 4, &out, nullptr, nullptr, [](int i) { return i == 0 ? 1.0f : 0.0f; });
        const float e0 = rmsRange(out, 0, 44100), e1 = rmsRange(out, 44100, 88200), e3 = rmsRange(out, 132300, 176400);
        std::cout << "  delay fb 0.95 energy: " << dbOf(e0) << " / " << dbOf(e1) << " / " << dbOf(e3) << " dB\n";
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && e1 < e0 && e3 < e1 && e3 > 0.0f, "delay feedback 0.95 decays monotonically, stays finite");

        h.setRaw("dly_feedback", 0.3f); h.setRaw("dly_mix", 0.5f); h.setRaw("dly_time", 380.0f);
        h.runSilence(3.0f);
        const double inc = 2.0 * MathConstants<double>::pi * 200.0 / 44100.0;
        std::vector<float> steady;
        h.run(44100, &steady, nullptr, nullptr, [&](int i) { return 0.5f * (float)std::sin(inc * i); });
        h.setRaw("dly_time", 200.0f);
        std::vector<float> changing;
        h.run(22050, &changing, nullptr, nullptr, [&](int i) { return 0.5f * (float)std::sin(inc * (i + 44100)); });
        const float sd = maxAbsDiff(steady, 22050, 44100), cd = maxAbsDiff(changing, 1, 22050);
        std::cout << "  delay time change: largest step " << cd << " vs steady " << sd << "\n";
        check(cd < sd * 3.0f && cd < 0.15f, "delay time change is click-free");
    }

    // Reverb: decays to silence, stable at max decay
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("rvb_enabled", true);
        h.setRaw("rvb_mix", 1.0f); h.setRaw("rvb_decay", 0.5f); h.setRaw("rvb_size", 0.7f); h.setRaw("rvb_predelay", 0.0f);
        h.runSilence(0.3f);
        std::vector<float> out;
        const double inc = 2.0 * MathConstants<double>::pi * 1000.0 / 44100.0;
        // 200 ms burst, then silence: the wet tail must fall by >60 dB within ~decay seconds
        h.run(44100 * 6, &out, nullptr, nullptr, [&](int i) { return i < 8820 ? 0.5f * (float)std::sin(inc * i) : 0.0f; });
        const float during = rmsRange(out, 4410, 8820), after = rmsRange(out, 8820 + 44100, 8820 + 66150);
        const float late = peakRange(out, 88200, 132300);
        std::cout << "  reverb decay 0.5 s: burst " << dbOf(during) << " dB, +1.0..1.5 s " << dbOf(after) << " dB, 2-3 s peak " << late
                  << ", 5-6 s exactly zero: " << (exactlyZero(out, 220500, 264600) ? "yes" : "no") << "\n";
        check(during > 1e-3f && after < during * 1e-3f && late < 1e-6f && exactlyZero(out, 220500, 264600),
              "reverb tail falls >60 dB after its decay time and reaches exact silence");
        // RT60 per band (isolated reverb, decay 0.5 s, damp 0.5, size 0.7): slope of the
        // tail envelope between 50 ms and 350 ms after a 300 ms sine burst
        auto rt60At = [](float freq, float decay, float damp)
        {
            agm::Reverb r; r.prepare(44100.0, 512); r.setEnabled(true); r.setMix(1.0f); r.setDecaySec(decay);
            r.setSize(0.7f); r.setDamping(damp); r.setPreDelayMs(0.0f); r.setWidth(1.0f);
            const double bandInc = 2.0 * MathConstants<double>::pi * freq / 44100.0;
            AudioBuffer<float> b(2, 512);
            std::vector<float> tail;
            for (int blk = 0; blk < 300; ++blk)   // 3.5 s
            {
                for (int i = 0; i < 512; ++i)
                {
                    const int n = blk * 512 + i;
                    const float x = n >= 22050 && n < 22050 + 13230 ? 0.5f * (float)std::sin(bandInc * n) : 0.0f;
                    b.setSample(0, i, x); b.setSample(1, i, x);
                }
                r.process(b);
                for (int i = 0; i < 512; ++i) tail.push_back(b.getSample(0, i));
            }
            const int t0 = 22050 + 13230;
            const float e1 = rmsRange(tail, t0 + 2205, t0 + 4410);      // 50-100 ms after the burst
            const float e2 = rmsRange(tail, t0 + 13230, t0 + 15435);    // 300-350 ms after
            const float slopeDbPerSec = (dbOf(e1) - dbOf(e2)) / 0.25f;
            return slopeDbPerSec > 0.0f ? 60.0f / slopeDbPerSec : 1e9f;
        };
        const float rtLo = rt60At(200.0f, 0.5f, 0.5f), rtMid = rt60At(1000.0f, 0.5f, 0.5f), rtHi = rt60At(5000.0f, 0.5f, 0.5f);
        const float rtMid0 = rt60At(1000.0f, 0.5f, 0.0f), rtHi0 = rt60At(5000.0f, 0.5f, 0.0f), rtLo2 = rt60At(200.0f, 2.0f, 0.5f);
        std::cout << "  reverb RT60 @ decay 0.5 / damp 0.5: 200 Hz " << rtLo << " s, 1 kHz " << rtMid << " s, 5 kHz " << rtHi
                  << " s; damp 0: 1 kHz " << rtMid0 << " s, 5 kHz " << rtHi0 << " s; decay 2.0 / damp 0.5 @200 Hz " << rtLo2 << " s\n";
        // The Decay knob is the low-frequency RT60; damping shortens the highs (that is its job).
        check(std::abs(rtLo - 0.5f) < 0.075f && std::abs(rtLo2 - 2.0f) < 0.3f, "reverb Decay knob = RT60 at 200 Hz within 15 %");
        check(std::abs(rtMid0 - 0.5f) < 0.075f && std::abs(rtHi0 - 0.5f) < 0.075f, "reverb with Damp 0: RT60 flat within 15 % up to 5 kHz");
        check(rtHi < rtMid && rtMid < rtLo, "reverb Damp 0.5 shortens the highs progressively");
        h.setRaw("rvb_decay", 10.0f); h.setRaw("rvb_size", 1.0f);
        h.runSilence(0.5f);
        std::vector<float> longTail;
        h.run(44100 * 6, &longTail, nullptr, nullptr, [&](int i) { return i < 8820 ? 0.5f * (float)std::sin(inc * i) : 0.0f; });
        bool fin = true;
        for (float v : longTail) if (!std::isfinite(v)) fin = false;
        const float a = rmsRange(longTail, 44100, 88200), b = rmsRange(longTail, 220500, 264600);
        std::cout << "  reverb decay 10 s: 1-2 s " << dbOf(a) << " dB, 5-6 s " << dbOf(b) << " dB\n";
        check(fin && b < a && b > 0.0f, "reverb at max decay/size is stable and still decaying");
    }

    // Limiter: ceiling never exceeded (sample and 4x true-peak), transparent below threshold
    {
        Harness h;
        h.disableAllModules();
        h.setOn("inst_enabled", false);
        h.setOn("lim_enabled", true);
        h.setRaw("lim_ceiling", -1.0f); h.setRaw("lim_attack", 1.0f); h.setRaw("lim_release", 120.0f);
        const float ceiling = agm::dbToGain(-1.0f);
        h.runSilence(0.3f);
        std::vector<float> out;
        const double inc = 2.0 * MathConstants<double>::pi * 1000.0 / 44100.0;
        // bursts: sine at +6 dBFS with silence gaps, plus a square wave and a lone spike
        h.run(44100 * 2, &out, nullptr, nullptr, [&](int i)
        {
            const int seg = (i / 11025) % 4;
            if (seg == 0) return 2.0f * (float)std::sin(inc * i);
            if (seg == 1) return 0.0f;
            if (seg == 2) return ((i / 40) & 1) ? 1.8f : -1.8f;
            return (i % 11025) == 500 ? 4.0f : 0.1f * (float)std::sin(inc * i);
        });
        float pk = 0.0f;
        for (float v : out) pk = std::max(pk, std::abs(v));
        std::cout << "  limiter sample peak " << pk << " (ceiling " << ceiling << ")\n";
        check(pk <= ceiling + 1e-4f, "limiter: sample peaks never exceed the ceiling");
        // true peak: 4x oversample the output
        juce::dsp::Oversampling<float> os(1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        os.initProcessing((size_t)out.size());
        AudioBuffer<float> ob(1, (int)out.size());
        for (size_t i = 0; i < out.size(); ++i) ob.setSample(0, (int)i, out[i]);
        juce::dsp::AudioBlock<float> blk(ob);
        auto up = os.processSamplesUp(blk);
        float tp = 0.0f;
        for (size_t i = 0; i < up.getNumSamples(); ++i) tp = std::max(tp, std::abs(up.getSample(0, (int)i)));
        float tpSine = 0.0f, tpSquare = 0.0f;
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const int seg = ((int)(i / 4) / 11025) % 4;
            const float v = std::abs(up.getSample(0, (int)i));
            if (seg == 0) tpSine = std::max(tpSine, v);
            if (seg == 2) tpSquare = std::max(tpSquare, v);
        }
        std::cout << "  limiter 4x true peak: overall " << dbOf(tp) << " dBTP, sine bursts " << dbOf(tpSine)
                  << " dBTP, square wave " << dbOf(tpSquare) << " dBTP (sample-peak limiter: square-wave overshoot is expected)\n";
        check(tpSine <= ceiling * agm::dbToGain(0.05f), "limiter: 4x true-peak on sine bursts within 0.05 dB of the ceiling");
        check(tpSquare <= agm::dbToGain(-0.9f), "limiter: 4x true-peak on a hard-clipped square wave <= -0.9 dBTP (ceiling -1 dB)");
        check(tp <= agm::dbToGain(-0.9f), "limiter: 4x true-peak of the whole burst/square/spike programme <= -0.9 dBTP");
        // steady sine at +6 dB: clean limiting (rms near ceiling/sqrt2, i.e. gain riding, not clipping)
        float rms = 0.0f, spk = 0.0f;
        h.runSine(1000.0f, 2.0f, 1.0f, 0.5f, rms, spk);
        std::cout << "  limiter steady: rms " << dbOf(rms) << " dB, peak " << dbOf(spk) << " dB\n";
        check(std::abs(dbOf(rms) - (dbOf(ceiling) - 3.01f)) < 0.5f, "limiter on a steady sine rides gain instead of clipping");
        // transparency below threshold
        h.runSine(1000.0f, 0.5f, 1.0f, 0.5f, rms, spk);
        check(std::abs(dbOf(rms) - dbOf(0.5f / 1.41421356f)) < 0.05f && std::abs(spk - 0.5f) < 0.002f,
              "limiter transparent below the ceiling");
        h.setRaw("lim_ceiling", 0.0f);
        h.runSine(1000.0f, 1.0f, 1.0f, 0.5f, rms, spk);
        check(spk <= 1.0f + 1e-4f && std::abs(dbOf(rms) + 3.01f) < 0.2f, "limiter at 0 dB ceiling with 0 dBFS sine: unity, no overs");
    }
}

// ---------------------------------------------------------------------------
static void stateSuite()
{
    // every parameter round-trips exactly
    {
        Harness a;
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        std::vector<std::pair<String, float>> values;
        for (auto* p : a.proc->getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*>(p))
            {
                float v = uni(rng);
                if (dynamic_cast<AudioParameterBool*>(rp) != nullptr) v = v > 0.5f ? 1.0f : 0.0f;
                if (auto* ch = dynamic_cast<AudioParameterChoice*>(rp)) v = ch->convertTo0to1((float)(int)(v * (ch->choices.size() - 1) + 0.5f));
                rp->setValueNotifyingHost(v);
                values.emplace_back(rp->paramID, rp->getValue());
            }
        a.proc->setFavorite(3, true);
        MemoryBlock blob;
        a.proc->getStateInformation(blob);
        Harness b;
        b.proc->setStateInformation(blob.getData(), (int)blob.getSize());
        bool same = true;
        for (auto& kv : values)
        {
            auto* rp = dynamic_cast<RangedAudioParameter*>(b.apvts().getParameter(kv.first));
            if (rp == nullptr || std::abs(rp->getValue() - kv.second) > 1e-6f)
            {
                same = false;
                std::cout << "  mismatch " << kv.first << "\n";
            }
        }
        check(same && (int)values.size() == a.proc->getParameters().size(), "state round-trip: all " + std::to_string(values.size()) + " parameters exact");
        check(b.proc->isFavorite(3) && b.proc->getInstrumentProgram() == a.proc->getInstrumentProgram(),
              "state round-trip: instrument program + favourites");
        a.proc->setCurrentProgram(7);
        MemoryBlock blob2;
        a.proc->getStateInformation(blob2);
        Harness c;
        c.proc->setStateInformation(blob2.getData(), (int)blob2.getSize());
        check(c.proc->getCurrentProgram() == 7 && std::abs(c.getRaw("inst_level") - a.getRaw("inst_level")) < 1e-5f,
              "state round-trip: host preset index restored without re-applying the preset");
        // the loaded processor must render sanely afterwards
        b.enableAllModulesModerate();
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 0);
        std::vector<float> out;
        b.run(8192, &out, nullptr, &mb);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin, "processor renders after state load");
    }

    // garbage / empty / truncated / wrong-tag / older state must not crash and must not half-apply
    {
        Harness h;
        h.setRaw("inst_level", -20.0f);
        h.setRaw("comp_thresh", -33.0f);
        MemoryBlock good;
        h.proc->getStateInformation(good);

        std::vector<uint8_t> garbage(257);
        for (size_t i = 0; i < garbage.size(); ++i) garbage[i] = (uint8_t)(i * 37 + 11);
        h.proc->setStateInformation(garbage.data(), (int)garbage.size());
        h.proc->setStateInformation(nullptr, 0);
        h.proc->setStateInformation(good.getData(), (int)good.getSize() / 2);
        XmlElement wrong("SomeOtherPlugin");
        wrong.setAttribute("x", 1);
        MemoryBlock wrongBlob;
        AudioProcessor::copyXmlToBinary(wrong, wrongBlob);
        h.proc->setStateInformation(wrongBlob.getData(), (int)wrongBlob.getSize());
        check(std::abs(h.getRaw("inst_level") + 20.0f) < 1e-3f && std::abs(h.getRaw("comp_thresh") + 33.0f) < 1e-3f,
              "garbage/empty/truncated/foreign state ignored, current values kept");

        // an "older" state: same root tag, but missing the instrument parameters and favourites
        auto xml = AudioProcessor::getXmlFromBinary(good.getData(), (int)good.getSize());
        for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
        {
            auto* c = xml->getChildElement(i);
            if (c->getStringAttribute("id").startsWith("inst_") || c->hasTagName("favorites"))
                xml->removeChildElement(c, true);
        }
        // add an unknown parameter as a future version might
        auto* extra = xml->createNewChildElement("PARAM");
        extra->setAttribute("id", "future_param");
        extra->setAttribute("value", 0.5);
        MemoryBlock older;
        AudioProcessor::copyXmlToBinary(*xml, older);
        Harness o;
        o.proc->setStateInformation(older.getData(), (int)older.getSize());
        const float lvl = o.getRaw("inst_level");
        check(std::isfinite(lvl) && lvl >= -40.0f && lvl <= 6.0f && std::abs(o.getRaw("comp_thresh") + 33.0f) < 1e-3f
                  && o.proc->getInstrumentProgram() >= 0 && o.proc->getInstrumentProgram() < (int)agm::InstrumentBank::kCount,
              "older state (missing inst_* / favourites, unknown param) loads with sane values");
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, 0.9f), 0);
        std::vector<float> out;
        o.run(4096, &out, nullptr, &mb);
        bool fin = true;
        for (float v : out) if (!std::isfinite(v)) fin = false;
        check(fin && peakRange(out, 0, 4096) > 1e-4f, "processor plays after loading older state");
        // presets: every one loads and renders finite
        bool allOk = true;
        for (int p = 0; p < o.proc->getNumPrograms(); ++p)
        {
            o.proc->setCurrentProgram(p);
            std::vector<float> po;
            MidiBuffer pm;
            pm.addEvent(MidiMessage::noteOn(1, 62, 0.9f), 0);
            o.run(4096, &po, nullptr, &pm);
            for (float v : po) if (!std::isfinite(v)) allOk = false;
            o.note(62, false);
        }
        check(allOk, "all 12 factory presets load and render finite");
    }
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    const auto t0 = Time::getMillisecondCounterHiRes();
    originalSuite();
    latencySuite();
    realtimeSafetySuite();
    robustnessSuite();
    instrumentSuite();
    drumSuite();
    fxSuite();
    stateSuite();
    const auto ms = Time::getMillisecondCounterHiRes() - t0;
    std::cout << (gFailures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << gChecks - gFailures << "/" << gChecks
              << " checks, " << (int)ms << " ms)\n";
    return gFailures == 0 ? 0 : 1;
}
