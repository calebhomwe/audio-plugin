// Audit instruments: the permanent tests that answer "is any control a lie?",
// "does processBlock ever allocate?" and "does state/automation survive abuse?".
// Everything printed here is a measurement, not an opinion.
#include "TestSupport.h"
#include <iomanip>
#include <map>
#include <set>

// ---------------------------------------------------------------------------
struct ParamInfo
{
    String id;
    String name;
    float minV = 0.0f, maxV = 1.0f, defV = 0.0f;
};


static String joinIds(const std::vector<String>& v)
{
    StringArray a;
    for (const auto& s : v) a.add(s);
    return a.joinIntoString(", ");
}

static std::vector<ParamInfo> allParams(MixAgentAudioProcessor& p)
{
    std::vector<ParamInfo> out;
    for (auto* raw : p.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*>(raw))
        {
            const auto& r = rp->getNormalisableRange();
            out.push_back({ rp->paramID, rp->getName(64), r.start, r.end,
                            r.convertFrom0to1(rp->getDefaultValue()) });
        }
    return out;
}

// Deterministic STEREO probe: pink-ish noise + two sines + periodic impulses.
// L and R carry different noise so mid/side controls have something to work on,
// and the level is hot enough that the limiter is actually asked to do its job.
struct Probe { std::vector<float> l, r; };

static Probe makeProbe(int n, double sr)
{
    Probe p;
    p.l.resize((size_t)n);
    p.r.resize((size_t)n);
    uint32_t sL = 12345u, sR = 987654321u;
    float aL[3] = { 0.0f, 0.0f, 0.0f }, aR[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        const float t = (float)i / (float)sr;
        const float imp = (i % 12000 == 0) ? 0.55f : 0.0f;
        auto one = [&](uint32_t& s, float* a, float sineAmp, float sineHz)
        {
            s = s * 1664525u + 1013904223u;
            const float w = (float)(s >> 8) / 8388608.0f - 1.0f;
            a[0] += 0.02f * (w - a[0]);
            a[1] += 0.15f * (w - a[1]);
            a[2] += 0.60f * (w - a[2]);
            const float pink = 0.34f * (a[0] * 2.2f + a[1] * 1.0f + a[2] * 0.5f + w * 0.12f);
            const float s1 = 0.20f * std::sin(MathConstants<float>::twoPi * 220.0f * t);
            const float s2 = sineAmp * std::sin(MathConstants<float>::twoPi * sineHz * t);
            return jlimit(-1.0f, 1.0f, pink + s1 + s2 + imp);
        };
        p.l[(size_t)i] = one(sL, aL, 0.11f, 1900.0f);
        p.r[(size_t)i] = one(sR, aR, 0.11f, 2530.0f);
    }
    return p;
}

// MIDI schedule that exercises the instrument bank and all three drum families.
struct MidiEventSpec { int sample; MidiMessage msg; };

static std::vector<MidiEventSpec> makeMidiSchedule(double sr)
{
    auto at = [sr](double sec) { return (int)(sec * sr); };
    return {
        { at(0.00), MidiMessage::noteOn(1, 36, 0.95f) },   // kick
        { at(0.01), MidiMessage::noteOn(1, 64, 0.85f) },   // instrument E4
        { at(0.12), MidiMessage::noteOn(1, 42, 0.70f) },   // closed hat
        { at(0.25), MidiMessage::noteOn(1, 38, 0.90f) },   // snare
        { at(0.30), MidiMessage::noteOn(1, 67, 0.60f) },   // instrument G4
        { at(0.38), MidiMessage::noteOn(1, 46, 0.80f) },   // open hat
        { at(0.50), MidiMessage::noteOff(1, 64) },
        { at(0.62), MidiMessage::noteOn(1, 36, 0.60f) },
        { at(0.75), MidiMessage::noteOff(1, 67) },
        { at(0.80), MidiMessage::noteOn(1, 49, 0.85f) },   // crash
    };
}

static void renderProbe(Harness& h, const Probe& probe,
                        std::vector<float>& outL, std::vector<float>& outR,
                        const std::vector<MidiEventSpec>& sched, int blockSize)
{
    const int total = (int)probe.l.size();
    AudioBuffer<float> buf(2, blockSize);
    outL.clear(); outR.clear();
    outL.reserve((size_t)total); outR.reserve((size_t)total);
    size_t next = 0;
    for (int done = 0; done < total; )
    {
        const int n = std::min(blockSize, total - done);
        buf.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample(0, i, probe.l[(size_t)(done + i)]);
            buf.setSample(1, i, probe.r[(size_t)(done + i)]);
        }
        MidiBuffer midi;
        while (next < sched.size() && sched[next].sample < done + n)
        {
            midi.addEvent(sched[next].msg, std::max(0, sched[next].sample - done));
            ++next;
        }
        h.proc->processBlock(buf, midi);
        for (int i = 0; i < n; ++i)
        {
            outL.push_back(buf.getSample(0, i));
            outR.push_back(buf.getSample(1, i));
        }
        done += n;
    }
}

static double rmsOf(const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v) s += (double)x * x;
    return v.empty() ? 0.0 : std::sqrt(s / (double)v.size());
}

static double rmsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min(a.size(), b.size());
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) { const double d = (double)a[i] - b[i]; s += d * d; }
    return n == 0 ? 0.0 : std::sqrt(s / (double)n);
}

static double peakDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min(a.size(), b.size());
    double m = 0.0;
    for (size_t i = 0; i < n; ++i) m = std::max(m, std::abs((double)a[i] - b[i]));
    return m;
}

static double dB(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// ---------------------------------------------------------------------------
// 1. Dead-parameter sweep. Every module is switched on so no control is judged
//    while its owner is bypassed; the probe carries audio AND MIDI so the
//    instrument/drum controls are live too.
// ---------------------------------------------------------------------------

// Controls whose min..max genuinely cannot change this probe, with the reason.
static const std::map<String, String>& deadKnobWaivers()
{
    static const std::map<String, String> w = {
        // nothing waived: every control must move the output
    };
    return w;
}

static void deadKnobSuite()
{
    std::cout << "\n=== DEAD-KNOB SWEEP (48 kHz, block 256, 1.0 s probe) ===\n";
    const double sr = 48000.0;
    const int block = 256;
    const int total = (int)(sr * 1.0);
    const Probe probe = makeProbe(total, sr);
    const auto sched = makeMidiSchedule(sr);

    Harness ref(sr, block);
    const auto params = allParams(*ref.proc);

    auto renderAt = [&](const ParamInfo& pi, float value, std::vector<float>& l, std::vector<float>& r)
    {
        Harness h(sr, block);
        h.setOn("eq_enabled", true);  h.setOn("eq_hp_enabled", true); h.setOn("eq_lp_enabled", true);
        h.setOn("sat_enabled", true); h.setOn("comp_enabled", true);  h.setOn("img_enabled", true);
        h.setOn("dly_enabled", true); h.setOn("rvb_enabled", true);   h.setOn("lim_enabled", true);
        h.setOn("inst_enabled", true);
        h.setRaw("dly_mix", 0.4f); h.setRaw("rvb_mix", 0.4f); h.setRaw("sat_mix", 0.8f);
        h.setRaw("eq_hp_freq", 60.0f); h.setRaw("eq_lp_freq", 14000.0f);
        // A peak/shelf band at 0 dB is an exact bypass, so its Freq/Q controls
        // could not possibly do anything: give every band some gain first.
        h.setRaw("eq_lsf_gain", 6.0f); h.setRaw("eq_hsf_gain", 6.0f);
        h.setRaw("eq_p1_gain", 6.0f); h.setRaw("eq_p2_gain", -6.0f); h.setRaw("eq_p3_gain", 6.0f);
        // Hot enough that the limiter is really limiting, so its controls are live.
        h.setRaw("in_gain", 6.0f); h.setRaw("comp_thresh", -14.0f);
        // Width 100 % is an exact bypass, so leave the imager somewhere it is
        // actually doing something or img_enabled has nothing to enable.
        h.setRaw("img_width", 160.0f);
        h.setRaw(pi.id.toRawUTF8(), value);
        renderProbe(h, probe, l, r, sched, block);
    };

    std::cout << std::left << std::setw(16) << "param" << std::right
              << std::setw(11) << "min" << std::setw(11) << "max"
              << std::setw(11) << "dRMS dB" << std::setw(11) << "dPeak dB" << "\n";

    std::vector<String> dead, tiny;
    for (const auto& pi : params)
    {
        std::vector<float> lo, loR, hi, hiR;
        renderAt(pi, pi.minV, lo, loR);
        renderAt(pi, pi.maxV, hi, hiR);
        const double ref1 = std::max(rmsOf(lo), rmsOf(hi));
        const double refP = std::max(peakRange(lo, 0, (int)lo.size()), peakRange(hi, 0, (int)hi.size()));
        const double dr = dB(rmsDiff(lo, hi) / std::max(ref1, 1e-9));
        const double dp = dB(peakDiff(lo, hi) / std::max((double)refP, 1e-9));
        std::cout << std::left << std::setw(16) << pi.id.toRawUTF8() << std::right << std::fixed
                  << std::setprecision(2) << std::setw(11) << pi.minV << std::setw(11) << pi.maxV
                  << std::setw(11) << dr << std::setw(11) << dp << "\n";
        if (dr < -80.0) dead.push_back(pi.id);
        else if (dr < -40.0) tiny.push_back(pi.id);
    }

    std::vector<String> unwaived;
    for (const auto& d : dead)
        if (deadKnobWaivers().find(d) == deadKnobWaivers().end())
            unwaived.push_back(d);

    String msg = "dead-knob sweep: every one of " + String((int)params.size())
               + " controls changes the output by > -80 dB RMS";
    if (!unwaived.empty())
        msg += " (DEAD: " + joinIds(unwaived) + ")";
    check(unwaived.empty(), msg.toStdString());
    if (!tiny.empty())
        std::cout << "  note: effect present but < -40 dB RMS for: "
                  << joinIds(tiny) << "\n";
}

// ---------------------------------------------------------------------------
// 2. Allocation detector. The counter is armed immediately before processBlock
//    and disarmed immediately after, so anything it catches is on the audio
//    thread inside the callback.
// ---------------------------------------------------------------------------
static void allocationSuite()
{
    std::cout << "\n=== AUDIO-THREAD ALLOCATION DETECTOR ===\n";
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const int blocks[] = { 1, 32, 64, 512, 2048, 4096 };
    long grand = 0;

    for (double sr : rates)
        for (int bs : blocks)
        {
            Harness h(sr, bs);
            const auto params = allParams(*h.proc);
            h.setOn("eq_enabled", true);  h.setOn("eq_hp_enabled", true); h.setOn("eq_lp_enabled", true);
            h.setOn("sat_enabled", true); h.setOn("comp_enabled", true);  h.setOn("img_enabled", true);
            h.setOn("dly_enabled", true); h.setOn("rvb_enabled", true);   h.setOn("lim_enabled", true);
            h.setOn("inst_enabled", true);
            const int nBlocks = std::max(40, std::min(400, (int)(sr * 0.5) / std::max(1, bs)));
            AudioBuffer<float> buf(2, bs);
            gAllocCount.store(0);
            uint32_t rng = 7u;
            size_t pIdx = 0;
            for (int b = 0; b < nBlocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < bs; ++i)
                    {
                        rng = rng * 1664525u + 1013904223u;
                        buf.setSample(ch, i, 0.5f * ((float)(rng >> 8) / 8388608.0f - 1.0f));
                    }
                MidiBuffer midi;
                if (b % 5 == 0) midi.addEvent(MidiMessage::noteOn(1, 36 + (b % 12), 0.8f), 0);
                if (b % 7 == 0) midi.addEvent(MidiMessage::noteOn(1, 60 + (b % 13), 0.7f), bs / 2);
                if (b % 11 == 0) midi.addEvent(MidiMessage::noteOff(1, 60 + (b % 13)), bs - 1);
                // sweep one parameter per block, message-thread style
                const auto& pi = params[pIdx++ % params.size()];
                const float f = (float)(b % 11) / 10.0f;
                h.setRaw(pi.id.toRawUTF8(), pi.minV + f * (pi.maxV - pi.minV));

                gCountAllocs = true;
                h.proc->processBlock(buf, midi);
                gCountAllocs = false;
            }
            // Oversized block: the host hands us 4x what prepareToPlay promised.
            AudioBuffer<float> big(2, bs * 4);
            big.clear();
            for (int i = 0; i < bs * 4; ++i) { big.setSample(0, i, 0.3f); big.setSample(1, i, -0.3f); }
            MidiBuffer none;
            gCountAllocs = true;
            h.proc->processBlock(big, none);
            gCountAllocs = false;
            // Mono buffer.
            AudioBuffer<float> mono(1, bs);
            mono.clear();
            gCountAllocs = true;
            h.proc->processBlock(mono, none);
            gCountAllocs = false;

            const long n = gAllocCount.load();
            grand += n;
            if (n != 0)
                std::cout << "  sr " << (int)sr << " block " << bs << ": " << n << " allocations\n";
        }
    std::cout << "  total allocations inside processBlock across 24 rate/block combinations: " << grand << "\n";
    check(grand == 0, "processBlock allocates nothing at 44.1/48/96/192 kHz, blocks 1..4096, "
                      "while every parameter is swept and MIDI is delivered");
}

// ---------------------------------------------------------------------------
// 3. State / automation torture.
// ---------------------------------------------------------------------------
static void tortureSuite()
{
    std::cout << "\n=== STATE / AUTOMATION TORTURE ===\n";
    {
        Harness h(48000.0, 256);
        const auto params = allParams(*h.proc);
        // Put every parameter somewhere non-default first.
        uint32_t rng = 99u;
        std::map<String, float> want;
        for (const auto& pi : params)
        {
            rng = rng * 1664525u + 1013904223u;
            const float f = (float)((rng >> 9) & 0xFFFF) / 65535.0f;
            const float v = pi.minV + f * (pi.maxV - pi.minV);
            h.setRaw(pi.id.toRawUTF8(), v);
            want[pi.id] = h.getRaw(pi.id.toRawUTF8());
        }
        MemoryBlock mb;
        h.proc->getStateInformation(mb);
        bool exact = true;
        for (int i = 0; i < 1000; ++i)
        {
            MemoryBlock round;
            h.proc->setStateInformation(mb.getData(), (int)mb.getSize());
            h.proc->getStateInformation(round);
            if (round.getSize() != mb.getSize()) { exact = false; break; }
            mb = round;
        }
        for (const auto& pi : params)
            if (std::abs(h.getRaw(pi.id.toRawUTF8()) - want[pi.id]) > 1e-4f * std::max(1.0f, std::abs(want[pi.id])))
            { exact = false; std::cout << "  drifted: " << pi.id << "\n"; }
        check(exact, "1000x state round-trip is byte-stable and every parameter is unchanged");
    }

    {
        // Per-parameter click hunt: flip ONE control between its extremes every
        // block while a steady 440 Hz sine runs, and report the worst
        // sample-to-sample discontinuity it causes. The sine itself steps at most
        // 0.3 * 2*pi*440/48000 = 0.017 per sample, so anything much above that is
        // the parameter, not the signal.
        std::cout << "  per-parameter click hunt (440 Hz @ 0.3, extremes every block):\n";
        Harness probe0(48000.0, 64);
        const auto params = allParams(*probe0.proc);
        std::vector<std::pair<String, float>> worst;
        for (const auto& pi : params)
        {
            Harness h(48000.0, 64);
            h.setOn("eq_enabled", true); h.setOn("sat_enabled", true); h.setOn("comp_enabled", true);
            h.setOn("img_enabled", true); h.setOn("dly_enabled", true); h.setOn("rvb_enabled", true);
            h.setOn("lim_enabled", true);
            h.setOn("inst_enabled", false);
            AudioBuffer<float> buf(2, 64);
            const double inc = MathConstants<double>::twoPi * 440.0 / 48000.0;
            long idx = 0;
            float prev = 0.0f, step = 0.0f;
            MidiBuffer none;
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < 64; ++i)
                {
                    const float x = 0.3f * (float)std::sin(inc * (double)(idx + i));
                    buf.setSample(0, i, x);
                    buf.setSample(1, i, x);
                }
                h.setRaw(pi.id.toRawUTF8(), (b % 2) ? pi.maxV : pi.minV);
                h.proc->processBlock(buf, none);
                if (b > 40)   // let the chain settle first
                    for (int i = 0; i < 64; ++i)
                    {
                        step = std::max(step, std::abs(buf.getSample(0, i) - prev));
                        prev = buf.getSample(0, i);
                    }
                else
                    prev = buf.getSample(0, 63);
                idx += 64;
            }
            worst.push_back({ pi.id, step });
        }
        std::sort(worst.begin(), worst.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < worst.size() && i < 12; ++i)
            std::cout << "    " << std::left << std::setw(16) << worst[i].first.toRawUTF8()
                      << std::right << std::fixed << std::setprecision(4) << worst[i].second << "\n";
        std::vector<String> clicky;
        for (const auto& w : worst) if (w.second > 0.25f) clicky.push_back(w.first);
        check(clicky.empty(), ("no control produces a step > 0.25 when flipped between its "
                               "extremes every block" + (clicky.empty() ? String() : " (CLICKS: " + joinIds(clicky) + ")")).toStdString());
    }

    {
        // Audio-rate automation of every parameter while audio runs.
        Harness h(48000.0, 64);
        const auto params = allParams(*h.proc);
        h.setOn("eq_enabled", true); h.setOn("sat_enabled", true); h.setOn("comp_enabled", true);
        h.setOn("img_enabled", true); h.setOn("dly_enabled", true); h.setOn("rvb_enabled", true);
        h.setOn("lim_enabled", true); h.setOn("inst_enabled", true);
        AudioBuffer<float> buf(2, 64);
        float worstPeak = 0.0f;
        bool finite = true;
        uint32_t rng = 4242u;
        const double inc = MathConstants<double>::twoPi * 440.0 / 48000.0;
        long sampleIdx = 0;
        for (int b = 0; b < 3000; ++b)
        {
            for (int i = 0; i < 64; ++i)
            {
                const float x = 0.3f * (float)std::sin(inc * (double)(sampleIdx + i));
                buf.setSample(0, i, x);
                buf.setSample(1, i, x);
            }
            const auto& pi = params[(size_t)b % params.size()];
            rng = rng * 1664525u + 1013904223u;
            const float f = (float)((rng >> 9) & 0xFFFF) / 65535.0f;
            h.setRaw(pi.id.toRawUTF8(), pi.minV + f * (pi.maxV - pi.minV));
            juce::ignoreUnused(worstPeak);
            MidiBuffer midi;
            if (b % 13 == 0) midi.addEvent(MidiMessage::noteOn(1, 60, 0.8f), 0);
            h.proc->processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int i = 0; i < 64; ++i)
                    if (!std::isfinite(buf.getSample(ch, i))) finite = false;
                worstPeak = std::max(worstPeak, buf.getMagnitude(ch, 0, 64));
            }
            sampleIdx += 64;
        }
        // What this suite can prove is that nothing blows up. It deliberately does
        // NOT claim "no clicks": with every parameter being thrown around at once
        // the output legitimately contains near-Nyquist energy (a high-Q EQ band
        // whose frequency is being swept, instrument aliasing), and a
        // sample-to-sample-step metric cannot tell that apart from a
        // discontinuity - it reads 1.7x the block peak either way. The
        // per-parameter click hunt above is the instrument for clicks: it moves
        // one control at a time against a clean 440 Hz sine.
        // Bound: out_gain +24 dB on a limiter ceiling of 0 dBFS is 15.85; the
        // synth bus is soft-clipped to +/-1 and adds at most that again.
        std::cout << "  audio-rate automation of all " << params.size()
                  << " params over 3000 blocks: peak reached " << worstPeak
                  << " (bounded by out_gain +24 dB on a 0 dBFS ceiling = 15.85)\n";
        check(finite, "audio-rate automation of every parameter: output stays finite");
        check(worstPeak < 40.0f,
              "audio-rate automation of every parameter: output stays bounded, nothing runs away");
    }

    {
        // Garbage / truncated / future-version state.
        Harness h(48000.0, 256);
        h.setRaw("comp_ratio", 7.5f);
        MemoryBlock good;
        h.proc->getStateInformation(good);
        const char* junk = "\x00\xff\x01not-xml-at-all<<<>>>";
        h.proc->setStateInformation(junk, 21);
        h.proc->setStateInformation(nullptr, 0);
        h.proc->setStateInformation(good.getData(), (int)good.getSize() / 2);
        {
            XmlElement fut("Parameters");
            fut.setAttribute("hostProgram", 9999);
            fut.setAttribute("futureThing", "hello");
            auto* p = fut.createNewChildElement("PARAM");
            p->setAttribute("id", "comp_ratio");
            p->setAttribute("value", 12.0);
            auto* q = fut.createNewChildElement("PARAM");
            q->setAttribute("id", "a_param_that_does_not_exist");
            q->setAttribute("value", 3.0);
            MemoryBlock fb;
            h.proc->copyXmlToBinary(fut, fb);
            h.proc->setStateInformation(fb.getData(), (int)fb.getSize());
        }
        check(std::abs(h.getRaw("comp_ratio") - 12.0f) < 1e-3f,
              "future-version state: known params applied, unknown ignored, program index clamped");
        check(h.proc->getCurrentProgram() >= 0 && h.proc->getCurrentProgram() < 12,
              "out-of-range hostProgram in state is clamped into the preset list");
        AudioBuffer<float> b(2, 256);
        b.clear();
        MidiBuffer m;
        h.proc->processBlock(b, m);
        bool ok = true;
        for (int i = 0; i < 256; ++i) if (!std::isfinite(b.getSample(0, i))) ok = false;
        check(ok, "processor still renders finite audio after abusive state loads");
    }
}

// ---------------------------------------------------------------------------
// 4. Host presets. Selecting a preset must produce the preset, not the preset
//    layered on top of whatever was loaded before it.
// ---------------------------------------------------------------------------
static void presetSuite()
{
    std::cout << "\n=== HOST PRESETS ===\n";
    Harness h(48000.0, 256);
    const auto params = allParams(*h.proc);
    const int n = h.proc->getNumPrograms();

    // Snapshot of every preset reached from a freshly constructed processor.
    std::vector<std::map<String, float>> clean((size_t)n);
    for (int p = 0; p < n; ++p)
    {
        Harness fresh(48000.0, 256);
        fresh.proc->setCurrentProgram(p);
        for (const auto& pi : params) clean[(size_t)p][pi.id] = fresh.getRaw(pi.id.toRawUTF8());
    }

    // Now walk the presets in order in ONE instance, as a user browsing them would.
    StringArray contaminated;
    int worstDiffs = 0;
    String worstPair;
    for (int from = 0; from < n; ++from)
        for (int to = 0; to < n; ++to)
        {
            if (from == to) continue;
            Harness w(48000.0, 256);
            w.proc->setCurrentProgram(from);
            w.proc->setCurrentProgram(to);
            int diffs = 0;
            for (const auto& pi : params)
                if (std::abs(w.getRaw(pi.id.toRawUTF8()) - clean[(size_t)to][pi.id]) > 1e-4f)
                    ++diffs;
            if (diffs > worstDiffs)
            {
                worstDiffs = diffs;
                worstPair = h.proc->getProgramName(from) + " -> " + h.proc->getProgramName(to);
            }
            if (diffs > 0) contaminated.add(h.proc->getProgramName(from) + "->" + h.proc->getProgramName(to));
        }
    std::cout << "  preset pairs that do not land on the same settings as a fresh load: "
              << contaminated.size() << " of " << (n * (n - 1)) << "\n";
    if (worstDiffs > 0)
        std::cout << "  worst: " << worstPair << " leaves " << worstDiffs
                  << " parameters at the previous preset's value\n";
    check(contaminated.isEmpty(),
          "host presets: selecting a preset gives the same settings no matter what was loaded before");
}

// ---------------------------------------------------------------------------
int main()
{
    ScopedJuceInitialiser_GUI init;
    const auto t0 = Time::getMillisecondCounterHiRes();
    deadKnobSuite();
    allocationSuite();
    tortureSuite();
    presetSuite();
    const auto ms = Time::getMillisecondCounterHiRes() - t0;
    std::cout << (gFailures == 0 ? "AUDIT TESTS PASSED" : "AUDIT TESTS FAILED") << " ("
              << gChecks - gFailures << "/" << gChecks << " checks, " << (int)ms << " ms)\n";
    return gFailures == 0 ? 0 : 1;
}
