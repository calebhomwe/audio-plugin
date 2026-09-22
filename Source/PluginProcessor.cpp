#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixAgentAudioProcessor();
}

MixAgentAudioProcessor::MixAgentAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            apvts.addParameterListener(rp->paramID, this);
    syncModules();
}

MixAgentAudioProcessor::~MixAgentAudioProcessor()
{
    cancelPendingUpdate();
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            apvts.removeParameterListener(rp->paramID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MixAgentAudioProcessor::createParameterLayout()
{
    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>("in_gain", "Input", -24.0f, 24.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("out_gain", "Output", -24.0f, 24.0f, 0.0f));

    layout.add(std::make_unique<AudioParameterBool>("eq_enabled", "EQ", true));
    layout.add(std::make_unique<AudioParameterBool>("eq_hp_enabled", "HP On", false));
    layout.add(std::make_unique<AudioParameterFloat>("eq_hp_freq", "HP Freq", 20.0f, 1000.0f, 40.0f));
    layout.add(std::make_unique<AudioParameterBool>("eq_lp_enabled", "LP On", false));
    layout.add(std::make_unique<AudioParameterFloat>("eq_lp_freq", "LP Freq", 500.0f, 20000.0f, 18000.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_lsf_freq", "LS Freq", 40.0f, 1200.0f, 120.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_lsf_gain", "LS Gain", -15.0f, 15.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_hsf_freq", "HS Freq", 800.0f, 20000.0f, 8000.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_hsf_gain", "HS Gain", -15.0f, 15.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p1_freq", "P1 Freq", 60.0f, 3000.0f, 200.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p1_gain", "P1 Gain", -15.0f, 15.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p1_q", "P1 Q", 0.2f, 10.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p2_freq", "P2 Freq", 150.0f, 8000.0f, 800.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p2_gain", "P2 Gain", -15.0f, 15.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p2_q", "P2 Q", 0.2f, 10.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p3_freq", "P3 Freq", 500.0f, 14000.0f, 3200.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p3_gain", "P3 Gain", -15.0f, 15.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("eq_p3_q", "P3 Q", 0.2f, 10.0f, 1.0f));

    layout.add(std::make_unique<AudioParameterBool>("sat_enabled", "Sat On", false));
    layout.add(std::make_unique<AudioParameterChoice>("sat_mode", "Sat Mode",
                                                      juce::StringArray("Tube", "Tape", "Soft", "Exciter"), 0));
    layout.add(std::make_unique<AudioParameterFloat>("sat_drive", "Sat Drive", 0.0f, 1.0f, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>("sat_mix", "Sat Mix", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("sat_out", "Sat Out", -12.0f, 12.0f, 0.0f));

    layout.add(std::make_unique<AudioParameterBool>("comp_enabled", "Comp On", true));
    layout.add(std::make_unique<AudioParameterFloat>("comp_thresh", "Comp Thresh", -60.0f, 0.0f, -18.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_ratio", "Comp Ratio", 1.0f, 20.0f, 3.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_attack", "Comp Attack", 0.1f, 100.0f, 10.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_release", "Comp Release", 10.0f, 1000.0f, 150.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_knee", "Comp Knee", 0.0f, 24.0f, 6.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_makeup", "Comp Makeup", 0.0f, 24.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("comp_mix", "Comp Mix", 0.0f, 1.0f, 1.0f));

    layout.add(std::make_unique<AudioParameterBool>("img_enabled", "Img On", false));
    layout.add(std::make_unique<AudioParameterFloat>("img_width", "Img Width", 0.0f, 200.0f, 100.0f));
    layout.add(std::make_unique<AudioParameterFloat>("img_balance", "Img Balance", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterBool>("img_mono", "Img Mono", false));

    layout.add(std::make_unique<AudioParameterBool>("dly_enabled", "Dly On", false));
    layout.add(std::make_unique<AudioParameterFloat>("dly_time", "Dly Time", 20.0f, 2000.0f, 380.0f));
    layout.add(std::make_unique<AudioParameterFloat>("dly_feedback", "Dly Feedback", 0.0f, 0.95f, 0.45f));
    layout.add(std::make_unique<AudioParameterFloat>("dly_mix", "Dly Mix", 0.0f, 1.0f, 0.25f));
    layout.add(std::make_unique<AudioParameterFloat>("dly_damp", "Dly Damp", 0.0f, 1.0f, 0.4f));
    layout.add(std::make_unique<AudioParameterFloat>("dly_width", "Dly Width", 0.0f, 1.0f, 1.0f));

    layout.add(std::make_unique<AudioParameterBool>("rvb_enabled", "Rvb On", false));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_size", "Rvb Size", 0.1f, 1.0f, 0.7f));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_decay", "Rvb Decay", 0.2f, 10.0f, 2.5f));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_damp", "Rvb Damp", 0.0f, 1.0f, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_width", "Rvb Width", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_mix", "Rvb Mix", 0.0f, 1.0f, 0.25f));
    layout.add(std::make_unique<AudioParameterFloat>("rvb_predelay", "Rvb PreDelay", 0.0f, 250.0f, 10.0f));

    layout.add(std::make_unique<AudioParameterBool>("lim_enabled", "Lim On", true));
    layout.add(std::make_unique<AudioParameterFloat>("lim_ceiling", "Lim Ceiling", -20.0f, 0.0f, -1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("lim_attack", "Lim Attack", 0.01f, 10.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>("lim_release", "Lim Release", 10.0f, 500.0f, 120.0f));

    layout.add(std::make_unique<AudioParameterFloat>("drum_level", "Drum Level", -40.0f, 6.0f, -14.0f));

    juce::StringArray instNames;
    for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
        instNames.add(agm::InstrumentBank::programName(i));
    layout.add(std::make_unique<AudioParameterBool>("inst_enabled", "Inst On", true));
    layout.add(std::make_unique<AudioParameterChoice>("inst_program", "Instrument", instNames, 0));
    layout.add(std::make_unique<AudioParameterFloat>("inst_level", "Inst Level", -40.0f, 6.0f, -6.0f));
    return layout;
}

void MixAgentAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    handleParameter(parameterID, newValue);
}

void MixAgentAudioProcessor::handleParameter(const juce::String& id, float rawValue)
{
    if (id == "in_gain") { inGainDb = rawValue; }
    else if (id == "out_gain") { outGainDb = rawValue; }
    else if (id == "eq_enabled") { eq.setEnabled(rawValue > 0.5f); }
    else if (id == "eq_hp_enabled") { eq.setHpEnabled(rawValue > 0.5f); }
    else if (id == "eq_hp_freq") { eq.setHpFreq(rawValue); }
    else if (id == "eq_lp_enabled") { eq.setLpEnabled(rawValue > 0.5f); }
    else if (id == "eq_lp_freq") { eq.setLpFreq(rawValue); }
    else if (id == "eq_lsf_freq") { eq.setLowShelfFreq(rawValue); }
    else if (id == "eq_lsf_gain") { eq.setLowShelfGainDb(rawValue); }
    else if (id == "eq_hsf_freq") { eq.setHighShelfFreq(rawValue); }
    else if (id == "eq_hsf_gain") { eq.setHighShelfGainDb(rawValue); }
    else if (id == "eq_p1_freq") { eq.setPeakFreq(0, rawValue); }
    else if (id == "eq_p1_gain") { eq.setPeakGainDb(0, rawValue); }
    else if (id == "eq_p1_q") { eq.setPeakQ(0, rawValue); }
    else if (id == "eq_p2_freq") { eq.setPeakFreq(1, rawValue); }
    else if (id == "eq_p2_gain") { eq.setPeakGainDb(1, rawValue); }
    else if (id == "eq_p2_q") { eq.setPeakQ(1, rawValue); }
    else if (id == "eq_p3_freq") { eq.setPeakFreq(2, rawValue); }
    else if (id == "eq_p3_gain") { eq.setPeakGainDb(2, rawValue); }
    else if (id == "eq_p3_q") { eq.setPeakQ(2, rawValue); }
    else if (id == "sat_enabled") { saturator.setEnabled(rawValue > 0.5f); }
    else if (id == "sat_mode") { saturator.setMode((int)(rawValue + 0.5f)); }
    else if (id == "sat_drive") { saturator.setDrive(rawValue); }
    else if (id == "sat_mix") { saturator.setMix(rawValue); }
    else if (id == "sat_out") { saturator.setOutputDb(rawValue); }
    else if (id == "comp_enabled") { compressor.setEnabled(rawValue > 0.5f); }
    else if (id == "comp_thresh") { compressor.setThresholdDb(rawValue); }
    else if (id == "comp_ratio") { compressor.setRatio(rawValue); }
    else if (id == "comp_attack") { compressor.setAttackMs(rawValue); }
    else if (id == "comp_release") { compressor.setReleaseMs(rawValue); }
    else if (id == "comp_knee") { compressor.setKneeDb(rawValue); }
    else if (id == "comp_makeup") { compressor.setMakeupDb(rawValue); }
    else if (id == "comp_mix") { compressor.setMix(rawValue); }
    else if (id == "img_enabled") { imager.setEnabled(rawValue > 0.5f); }
    else if (id == "img_width") { imager.setWidthPercent(rawValue); }
    else if (id == "img_balance") { imager.setBalance(rawValue); }
    else if (id == "img_mono") { imager.setMono(rawValue > 0.5f); }
    else if (id == "dly_enabled") { delay.setEnabled(rawValue > 0.5f); }
    else if (id == "dly_time") { delay.setTimeMs(rawValue); }
    else if (id == "dly_feedback") { delay.setFeedback(rawValue); }
    else if (id == "dly_mix") { delay.setMix(rawValue); }
    else if (id == "dly_damp") { delay.setDamping(rawValue); }
    else if (id == "dly_width") { delay.setWidth(rawValue); }
    else if (id == "rvb_enabled") { reverb.setEnabled(rawValue > 0.5f); }
    else if (id == "rvb_size") { reverb.setSize(rawValue); }
    else if (id == "rvb_decay") { reverb.setDecaySec(rawValue); }
    else if (id == "rvb_damp") { reverb.setDamping(rawValue); }
    else if (id == "rvb_width") { reverb.setWidth(rawValue); }
    else if (id == "rvb_mix") { reverb.setMix(rawValue); }
    else if (id == "rvb_predelay") { reverb.setPreDelayMs(rawValue); }
    else if (id == "lim_enabled") { limiter.setEnabled(rawValue > 0.5f); }
    else if (id == "lim_ceiling") { limiter.setCeilingDb(rawValue); }
    else if (id == "lim_attack") { limiter.setAttackMs(rawValue); }
    else if (id == "lim_release") { limiter.setReleaseMs(rawValue); }
    else if (id == "drum_level") { drumEngine.setDrumLevelDb(rawValue); }
    else if (id == "inst_enabled") { instruments.setEnabled(rawValue > 0.5f); }
    else if (id == "inst_program") { instruments.setProgram((int)(rawValue + 0.5f)); }
    else if (id == "inst_level") { instruments.setLevelDb(rawValue); }
}

void MixAgentAudioProcessor::syncModules()
{
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            handleParameter(rp->paramID, rp->getNormalisableRange().convertFrom0to1(rp->getValue()));
}

void MixAgentAudioProcessor::prepareToPlay(double sr, int maxBlockSize)
{
    sampleRate = sr;
    eq.prepare(sr, maxBlockSize);
    saturator.prepare(sr, maxBlockSize);
    compressor.prepare(sr, maxBlockSize);
    imager.prepare(sr, maxBlockSize);
    delay.prepare(sr, maxBlockSize);
    reverb.prepare(sr, maxBlockSize);
    limiter.prepare(sr, maxBlockSize);
    drumEngine.prepare(sr, maxBlockSize);
    instruments.prepare(sr, maxBlockSize);

    fftIn.assign(kFftSize, 0.0f);
    fftWork.assign(kFftSize * 2, 0.0f);
    bucketMap.resize(kFftSize / 2);
    bucketSum.assign(kAnaBins, 0.0f);
    bucketCount.assign(kAnaBins, 0);
    anaSmooth.assign(kAnaBins, -90.0f);
    for (int k = 1; k < kFftSize / 2; ++k)
    {
        const float f = (float)k * (float)sr / (float)kFftSize;
        int b = (int)((float)kAnaBins * std::log(f / 20.0f) / std::log(1000.0f));
        b = juce::jlimit(0, kAnaBins - 1, b);
        bucketMap[k] = b;
    }
    fftPos = 0;
    inGainSmoothed = agm::dbToGain(inGainDb);
    outGainSmoothed = agm::dbToGain(outGainDb);
    setLatencySamples(limiter.getLatencySamples() + saturator.getLatencySamples());
    skipModules.clear();
    skipModules.addTokens(juce::SystemStats::getEnvironmentVariable("AGM_SKIP", ""), " ", "");
    runEq = !skipModules.contains("eq");
    runSat = !skipModules.contains("sat");
    runComp = !skipModules.contains("comp");
    runImg = !skipModules.contains("img");
    runDly = !skipModules.contains("dly");
    runRvb = !skipModules.contains("rvb");
    runLim = !skipModules.contains("lim");

    synthSlice = juce::jmax(1, maxBlockSize);
    synthBus.setSize(2, synthSlice, false, false, true);
    uiNoteFifo.reset();
}

void MixAgentAudioProcessor::releaseResources() {}

void MixAgentAudioProcessor::uiNoteOn(int note, float velocity)
{
    int start1, size1, start2, size2;
    uiNoteFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        uiNoteSlots[(size_t)start1] = { note, velocity, true };
        uiNoteFifo.finishedWrite(1);
    }
}

void MixAgentAudioProcessor::uiNoteOff(int note)
{
    int start1, size1, start2, size2;
    uiNoteFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        uiNoteSlots[(size_t)start1] = { note, 0.0f, false };
        uiNoteFifo.finishedWrite(1);
    }
}

void MixAgentAudioProcessor::drainUiNotes()
{
    const int ready = uiNoteFifo.getNumReady();
    if (ready <= 0)
        return;
    int start1, size1, start2, size2;
    uiNoteFifo.prepareToRead(ready, start1, size1, start2, size2);
    auto apply = [this](int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            // The pad grid is the instrument's preview keyboard (C3-B3 = 48-59):
            // it must never hit the legacy 35-49 drum range.
            const auto& e = uiNoteSlots[(size_t)(start + i)];
            if (e.on) instruments.noteOn(e.note, e.velocity);
            else instruments.noteOff(e.note, 0.0f);
        }
    };
    apply(start1, size1);
    apply(start2, size2);
    uiNoteFifo.finishedRead(size1 + size2);
}

// Drums: everything on MIDI channel 10 (GM convention, makes the whole GM map
// reachable) plus, for compatibility with earlier sessions, notes 35-49 on any
// channel. UI pads never come through here (they always play the instrument).
void MixAgentAudioProcessor::handleMidiEvent(const juce::MidiMessage& msg)
{
    const bool drumChannel = msg.getChannel() == 10;
    auto isDrumNote = [drumChannel](int n) { return drumChannel || (n >= 35 && n <= 49); };
    if (msg.isNoteOn())
    {
        const int note = msg.getNoteNumber();
        if (isDrumNote(note)) drumEngine.noteOn(note, msg.getFloatVelocity());
        else instruments.noteOn(note, msg.getFloatVelocity());
    }
    else if (msg.isNoteOff())
    {
        const int note = msg.getNoteNumber();
        if (isDrumNote(note)) drumEngine.noteOff(note, 0.0f);
        else instruments.noteOff(note, 0.0f);
    }
    else if (msg.isProgramChange())
    {
        // Applied to the bank right here (sample-accurate); the inst_program
        // parameter is brought in line on the message thread.
        const int pc = msg.getProgramChangeNumber();
        if (pc >= 0 && pc < (int)agm::InstrumentBank::kCount && !drumChannel)
        {
            instruments.setProgram(pc);
            midiProgramChange.store(pc, std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }
    else if (msg.isPitchWheel())
    {
        const float norm = (float)(msg.getPitchWheelValue() - 8192) / 8192.0f;
        instruments.setPitchBend(norm * kPitchBendRangeSemitones);
    }
    else if (msg.isSustainPedalOn())
    {
        instruments.setSustain(true);
    }
    else if (msg.isSustainPedalOff())
    {
        instruments.setSustain(false);
    }
    else if (msg.isAllNotesOff())
    {
        instruments.setSustain(false);
        instruments.allNotesOff();
    }
    else if (msg.isAllSoundOff())
    {
        instruments.setSustain(false);
        instruments.allSoundOff();
        drumEngine.allNotesOff();
    }
}

// Instruments + drums are summed on their own bus, soft-limited so a 24-voice
// pile-up cannot hard-clip into the FX chain, then added to the host signal.
void MixAgentAudioProcessor::renderSynthBus(juce::AudioBuffer<float>& buffer, int numCh, int start, int num)
{
    if (num <= 0 || synthBus.getNumSamples() <= 0)
        return;
    const int busCh = juce::jmin(2, numCh);
    for (int pos = 0; pos < num; pos += synthSlice)
    {
        const int n = juce::jmin(synthSlice, num - pos);
        synthBus.clear(0, n);
        instruments.renderAdd(synthBus, busCh, 0, n);
        drumEngine.renderAdd(synthBus, busCh, 0, n);
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* dst = buffer.getWritePointer(ch) + start + pos;
            const float* src = synthBus.getReadPointer(juce::jmin(ch, busCh - 1));
            for (int i = 0; i < n; ++i)
                dst[i] += agm::softClipBus(src[i]);
        }
    }
}

void MixAgentAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    // Instruments/drums render first (the instrument bus feeds the FX chain),
    // with MIDI applied at each event's sample offset inside the block.
    drainUiNotes();
    int rendered = 0;
    for (const auto metadata : midiMessages)
    {
        const int at = juce::jlimit(0, numSamples, metadata.samplePosition);
        if (at > rendered)
        {
            renderSynthBus(buffer, numCh, rendered, at - rendered);
            rendered = at;
        }
        handleMidiEvent(metadata.getMessage());
    }
    renderSynthBus(buffer, numCh, rendered, numSamples - rendered);

    // Per-SAMPLE step for a 10 ms ramp. This used to divide by the block length
    // instead of by one sample and was then applied once per sample, so the ramp
    // ran roughly blockSize times too fast and gain automation stepped.
    const float gainCoef = 1.0f - std::exp(-1.0f / (0.010f * (float)sampleRate));
    const float inTarget = agm::dbToGain(inGainDb);
    const float outTarget = agm::dbToGain(outGainDb);

    float g = inGainSmoothed;
    float inL = 0.0f, inR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        g += (inTarget - g) * gainCoef;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& s = buffer.getWritePointer(ch)[i];
            s *= g;
            if (ch == 0) inL = std::max(inL, std::abs(s));
            else inR = std::max(inR, std::abs(s));
        }
    }
    inGainSmoothed = g;
    if (numCh == 1) { inR = inL; }

    pushAnalyser(buffer);

    if (runEq) eq.process(buffer);
    if (runSat) saturator.process(buffer);
    if (runComp) compressor.process(buffer);
    if (runImg) imager.process(buffer);
    if (runDly) delay.process(buffer);
    if (runRvb) reverb.process(buffer);
    if (runLim) limiter.process(buffer);

    float outL = 0.0f, outR = 0.0f;
    g = outGainSmoothed;
    for (int i = 0; i < numSamples; ++i)
    {
        g += (outTarget - g) * gainCoef;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& s = buffer.getWritePointer(ch)[i];
            s *= g;
            if (ch == 0) outL = std::max(outL, std::abs(s));
            else outR = std::max(outR, std::abs(s));
        }
    }
    outGainSmoothed = g;
    if (numCh == 1) { outR = outL; }

    const float meterCoef = std::exp(-(float)numSamples / (0.3f * (float)sampleRate));
    inPeakL = std::max(inL, inPeakL * meterCoef);
    inPeakR = std::max(inR, inPeakR * meterCoef);
    outPeakL = std::max(outL, outPeakL * meterCoef);
    outPeakR = std::max(outR, outPeakR * meterCoef);
    inLevelL.store(inPeakL);
    inLevelR.store(inPeakR);
    outLevelL.store(outPeakL);
    outLevelR.store(outPeakR);
}

void MixAgentAudioProcessor::pushAnalyser(const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            m += buffer.getReadPointer(ch)[i];
        m /= (float)numCh;
        fftIn[fftPos++] = m;
        if (fftPos == kFftSize)
        {
            fftPos = 0;
            runFft();
        }
    }
}

void MixAgentAudioProcessor::runFft()
{
    std::copy(fftIn.begin(), fftIn.end(), fftWork.begin());
    window.multiplyWithWindowingTable(fftWork.data(), kFftSize);
    fft.performRealOnlyForwardTransform(fftWork.data());
    std::fill(bucketSum.begin(), bucketSum.end(), 0.0f);
    std::fill(bucketCount.begin(), bucketCount.end(), 0);
    for (int k = 1; k < kFftSize / 2; ++k)
    {
        const float re = fftWork[2 * k], im = fftWork[2 * k + 1];
        const float mag = std::sqrt(re * re + im * im);
        const int b = bucketMap[k];
        bucketSum[b] += mag;
        bucketCount[b]++;
    }
    for (int b = 0; b < kAnaBins; ++b)
    {
        const float db = 20.0f * std::log10(bucketSum[b] / (float)std::max(1, bucketCount[b]) + 1e-9f);
        anaSmooth[b] = anaSmooth[b] * 0.8f + db * 0.2f;
        anaOut[b].store(anaSmooth[b]);
    }
}

void MixAgentAudioProcessor::getAnalyzerSpectrum(float* outDb, int n) const
{
    for (int i = 0; i < n; ++i)
        outDb[i] = anaOut[i % kAnaBins].load();
}

void MixAgentAudioProcessor::getEqCurve(float* outDb, int n) const
{
    for (int i = 0; i < n; ++i)
    {
        const float f = 20.0f * std::pow(1000.0f, (float)i / (float)std::max(1, n - 1));
        outDb[i] = eq.getResponseDb(f);
    }
}

float MixAgentAudioProcessor::getInLevel(int ch) const { return ch == 0 ? inLevelL.load() : inLevelR.load(); }
float MixAgentAudioProcessor::getOutLevel(int ch) const { return ch == 0 ? outLevelL.load() : outLevelR.load(); }

juce::AudioProcessorEditor* MixAgentAudioProcessor::createEditor()
{
    return new MixAgentAudioProcessorEditor(*this);
}

bool MixAgentAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannels() != layouts.getMainOutputChannels())
        return false;
    return layouts.getMainOutputChannels() == 1 || layouts.getMainOutputChannels() == 2;
}

void MixAgentAudioProcessor::handleAsyncUpdate()
{
    const int pc = midiProgramChange.exchange(-1, std::memory_order_relaxed);
    if (pc < 0)
        return;
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("inst_program")))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)pc));
}

void MixAgentAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute("hostProgram", currentProgram);
        copyXmlToBinary(*xml, destData);
    }
}

void MixAgentAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            // Older states carry no hostProgram attribute: keep preset 0 as before.
            currentProgram = juce::jlimit(0, kPresetCount - 1, xml->getIntAttribute("hostProgram", 0));
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
        }
}

namespace
{
const char* const kPresetNames[] = { "Init", "Clean Master", "Vocal Presence", "Drum Bus Punch", "Wide & Spacey", "Warm Tape",
    "Trap: Drill Bell", "Trap: Rage Lead", "Trap: Jersey Keys", "Trap: Plugg Pad", "Trap: BoomBap EP", "Trap: Sub Glue" };
}

const juce::String MixAgentAudioProcessor::getProgramName(int index)
{
    return kPresetNames[juce::jlimit(0, kPresetCount - 1, index)];
}

namespace
{
juce::ValueTree getFavorites(juce::ValueTree& state)
{
    juce::ValueTree fav = state.getChildWithName("favorites");
    if (fav.isValid())
        return fav;
    juce::ValueTree created("favorites");
    state.addChild(created, -1, nullptr);
    return created;
}
}

bool MixAgentAudioProcessor::isFavorite(int program) const
{
    const juce::ValueTree fav = apvts.state.getChildWithName("favorites");
    if (!fav.isValid())
        return false;
    for (int i = 0; i < fav.getNumChildren(); ++i)
        if (fav.getChild(i).getProperty("p").toString() == juce::String(program))
            return true;
    return false;
}

void MixAgentAudioProcessor::setFavorite(int program, bool fav)
{
    juce::ValueTree favorites = getFavorites(apvts.state);
    auto remove = [&]
    {
        for (int i = favorites.getNumChildren() - 1; i >= 0; --i)
            if (favorites.getChild(i).getProperty("p").toString() == juce::String(program))
                favorites.removeChild(i, nullptr);
    };

    if (fav)
    {
        if (isFavorite(program))
            return;
        juce::ValueTree entry("fav");
        entry.setProperty("p", juce::String(program), nullptr);
        favorites.addChild(entry, -1, nullptr);
    }
    else
    {
        remove();
    }
}

int MixAgentAudioProcessor::getFavoriteCount() const
{
    const juce::ValueTree fav = apvts.state.getChildWithName("favorites");
    return fav.isValid() ? fav.getNumChildren() : 0;
}

void MixAgentAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, kPresetCount - 1, index);
    auto setRaw = [&](const juce::String& id, float raw)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(raw));
    };

    // Start from the factory defaults every time. Each preset below only writes
    // the parameters it cares about, so without this a preset was really
    // "whatever was loaded before, plus these few changes" - and preset 0 (Init)
    // was a no-op that reset nothing at all.
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            rp->setValueNotifyingHost(rp->getDefaultValue());

    switch (currentProgram)
    {
    case 0: break;   // Init = the factory defaults restored above
    case 1:
        setRaw("eq_enabled", 1.0f); setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 30.0f);
        setRaw("eq_hsf_freq", 10000.0f); setRaw("eq_hsf_gain", 1.5f);
        setRaw("comp_thresh", -14.0f); setRaw("comp_ratio", 2.0f);
        setRaw("comp_attack", 15.0f); setRaw("comp_release", 200.0f); setRaw("comp_knee", 4.0f);
        setRaw("lim_ceiling", -1.0f);
        break;
    case 2:
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 80.0f);
        setRaw("eq_lsf_freq", 200.0f); setRaw("eq_lsf_gain", -1.0f);
        setRaw("eq_p2_freq", 3200.0f); setRaw("eq_p2_gain", 2.5f); setRaw("eq_p2_q", 1.2f);
        setRaw("eq_hsf_freq", 8000.0f); setRaw("eq_hsf_gain", 1.5f);
        setRaw("comp_thresh", -20.0f); setRaw("comp_ratio", 3.0f);
        setRaw("comp_attack", 5.0f); setRaw("comp_release", 120.0f); setRaw("comp_makeup", 2.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 0.0f); setRaw("sat_drive", 0.3f); setRaw("sat_mix", 0.4f);
        setRaw("dly_enabled", 1.0f); setRaw("dly_time", 320.0f); setRaw("dly_feedback", 0.4f);
        setRaw("dly_mix", 0.18f); setRaw("dly_damp", 0.5f);
        setRaw("rvb_enabled", 1.0f); setRaw("rvb_mix", 0.15f); setRaw("rvb_decay", 2.0f);
        break;
    case 3:
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 25.0f);
        setRaw("eq_p1_freq", 100.0f); setRaw("eq_p1_gain", 3.0f); setRaw("eq_p1_q", 1.0f);
        setRaw("eq_p2_freq", 1800.0f); setRaw("eq_p2_gain", 2.0f); setRaw("eq_p2_q", 1.5f);
        setRaw("eq_hsf_freq", 8000.0f); setRaw("eq_hsf_gain", 1.0f);
        setRaw("eq_lp_enabled", 1.0f); setRaw("eq_lp_freq", 18000.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 2.0f); setRaw("sat_drive", 0.35f); setRaw("sat_mix", 0.5f);
        setRaw("comp_thresh", -16.0f); setRaw("comp_ratio", 4.0f);
        setRaw("comp_attack", 25.0f); setRaw("comp_release", 100.0f); setRaw("comp_knee", 6.0f); setRaw("comp_makeup", 1.5f);
        break;
    case 4:
        setRaw("img_enabled", 1.0f); setRaw("img_width", 140.0f);
        setRaw("dly_enabled", 1.0f); setRaw("dly_time", 380.0f); setRaw("dly_feedback", 0.5f);
        setRaw("dly_mix", 0.3f); setRaw("dly_damp", 0.35f); setRaw("dly_width", 1.0f);
        setRaw("rvb_enabled", 1.0f); setRaw("rvb_size", 0.85f); setRaw("rvb_decay", 4.0f);
        setRaw("rvb_mix", 0.35f); setRaw("rvb_predelay", 40.0f);
        setRaw("eq_hsf_freq", 8000.0f); setRaw("eq_hsf_gain", 1.0f);
        break;
    case 5:
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 1.0f); setRaw("sat_drive", 0.5f);
        setRaw("sat_mix", 1.0f); setRaw("sat_out", -2.0f);
        setRaw("eq_lp_enabled", 1.0f); setRaw("eq_lp_freq", 14000.0f);
        setRaw("eq_lsf_freq", 150.0f); setRaw("eq_lsf_gain", 1.5f);
        setRaw("comp_thresh", -18.0f); setRaw("comp_ratio", 2.0f);
        setRaw("comp_attack", 20.0f); setRaw("comp_release", 300.0f); setRaw("comp_makeup", 2.0f);
        setRaw("lim_ceiling", -0.7f);
        break;
    case 6: // Drill Bell — cold, dark, detached
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 1.0f); setRaw("inst_level", -8.0f);
        setRaw("eq_lp_enabled", 1.0f); setRaw("eq_lp_freq", 9000.0f);
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 35.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 2.0f); setRaw("sat_drive", 0.25f); setRaw("sat_mix", 0.35f);
        setRaw("comp_thresh", -24.0f); setRaw("comp_ratio", 2.5f); setRaw("comp_makeup", 1.5f);
        setRaw("lim_ceiling", -1.0f);
        break;
    case 7: // Rage Lead — bright, wide, driving
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 3.0f); setRaw("inst_level", -9.0f);
        setRaw("eq_hsf_freq", 9000.0f); setRaw("eq_hsf_gain", 2.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 0.0f); setRaw("sat_drive", 0.6f); setRaw("sat_mix", 0.6f);
        setRaw("comp_thresh", -16.0f); setRaw("comp_ratio", 4.0f); setRaw("comp_makeup", 2.0f);
        setRaw("img_enabled", 1.0f); setRaw("img_width", 135.0f);
        setRaw("lim_ceiling", -0.7f);
        break;
    case 8: // Jersey Keys — warm, forward, tight
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 2.0f); setRaw("inst_level", -8.0f);
        setRaw("eq_lsf_freq", 180.0f); setRaw("eq_lsf_gain", 2.0f);
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 45.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 2.0f); setRaw("sat_drive", 0.3f); setRaw("sat_mix", 0.45f);
        setRaw("comp_thresh", -20.0f); setRaw("comp_ratio", 3.0f); setRaw("comp_makeup", 2.0f);
        setRaw("lim_ceiling", -1.0f);
        break;
    case 9: // Plugg Pad — lush, wide, ambient
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 4.0f); setRaw("inst_level", -10.0f);
        setRaw("eq_lp_enabled", 1.0f); setRaw("eq_lp_freq", 12000.0f);
        setRaw("rvb_enabled", 1.0f); setRaw("rvb_size", 0.85f); setRaw("rvb_decay", 5.0f);
        setRaw("rvb_mix", 0.45f); setRaw("rvb_predelay", 35.0f);
        setRaw("dly_enabled", 1.0f); setRaw("dly_time", 420.0f); setRaw("dly_feedback", 0.45f);
        setRaw("dly_mix", 0.3f); setRaw("dly_width", 1.0f);
        setRaw("img_enabled", 1.0f); setRaw("img_width", 150.0f);
        setRaw("lim_ceiling", -1.5f);
        break;
    case 10: // BoomBap EP — dusty, boxy, mid-focused
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 7.0f); setRaw("inst_level", -8.0f);
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 120.0f);
        setRaw("eq_lp_enabled", 1.0f); setRaw("eq_lp_freq", 8000.0f);
        setRaw("eq_p1_freq", 3500.0f); setRaw("eq_p1_gain", -2.0f); setRaw("eq_p1_q", 1.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 1.0f); setRaw("sat_drive", 0.45f); setRaw("sat_mix", 0.7f);
        setRaw("comp_thresh", -22.0f); setRaw("comp_ratio", 2.5f); setRaw("comp_makeup", 1.5f);
        setRaw("lim_ceiling", -1.0f);
        break;
    case 11: // Sub Glue — deep, controlled low-end
        setRaw("inst_enabled", 1.0f); setRaw("inst_program", 9.0f); setRaw("inst_level", -6.0f);
        setRaw("eq_hp_enabled", 1.0f); setRaw("eq_hp_freq", 28.0f);
        setRaw("eq_p1_freq", 60.0f); setRaw("eq_p1_gain", 3.0f); setRaw("eq_p1_q", 1.0f);
        setRaw("sat_enabled", 1.0f); setRaw("sat_mode", 0.0f); setRaw("sat_drive", 0.5f); setRaw("sat_mix", 0.5f);
        setRaw("comp_thresh", -18.0f); setRaw("comp_ratio", 3.5f); setRaw("comp_makeup", 2.5f);
        setRaw("lim_ceiling", -0.7f);
        break;
    }
}

