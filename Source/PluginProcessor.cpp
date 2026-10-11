#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ValueParsing.h"
#include <cstring>

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
            parameterSnapshots.push_back({ rp->paramID, apvts.getRawParameterValue(rp->paramID), 0.0f });
    syncModules(true);
    for (auto& bin : anaOut)
        bin.store(-90.0f, std::memory_order_relaxed);
}

MixAgentAudioProcessor::~MixAgentAudioProcessor() = default;

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

namespace
{
void applyEqParameter(agm::EQ& eq, const juce::String& id, float rawValue)
{
    if (id == "eq_enabled") { eq.setEnabled(rawValue > 0.5f); }
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
}
}

void MixAgentAudioProcessor::handleParameter(const juce::String& id, float rawValue)
{
    if (id.startsWith("eq_")) { applyEqParameter(eq, id, rawValue); }
    else if (id == "in_gain") { inGainTarget = agm::dbToGain(rawValue); }
    else if (id == "out_gain") { outGainTarget = agm::dbToGain(rawValue); }
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
    else if (id == "img_width") { imager.setWidth(rawValue); }
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

void MixAgentAudioProcessor::syncModules(bool force)
{
    // APVTS values are atomic. All mutable DSP state belongs to the audio thread;
    // parameter callbacks from the host/message thread must never touch it.
    for (auto& parameter : parameterSnapshots)
    {
        const float value = parameter.value->load(std::memory_order_relaxed);
        if (force || value != parameter.lastValue)
        {
            handleParameter(parameter.id, value);
            parameter.lastValue = value;
        }
    }
}

void MixAgentAudioProcessor::prepareToPlay(double sr, int blockSize)
{
    sr = std::isfinite(sr) && sr > 1.0 ? sr : 44100.0;
    sampleRate.store(sr, std::memory_order_relaxed);
    syncModules(true);
    eq.prepare(sr, blockSize);
    saturator.prepare(sr, blockSize);
    compressor.prepare(sr, blockSize);
    imager.prepare(sr, blockSize);
    delay.prepare(sr, blockSize);
    reverb.prepare(sr, blockSize);
    limiter.prepare(sr, blockSize);
    drumEngine.prepare(sr, blockSize);
    instruments.prepare(sr, blockSize);

    fftIn.assign(kFftSize, 0.0f);
    fftWork.assign(kFftSize * 2, 0.0f);
    bucketMap.resize(kFftSize / 2);
    bucketPeak.assign(kAnaBins, 0.0f);
    anaSmooth.assign(kAnaBins, -90.0f);
    for (auto& bin : anaOut)
        bin.store(-90.0f, std::memory_order_relaxed);
    inPeakL = inPeakR = outPeakL = outPeakR = 0.0f;
    inLevelL.store(0.0f); inLevelR.store(0.0f);
    outLevelL.store(0.0f); outLevelR.store(0.0f);
    instrumentActive.store(false, std::memory_order_relaxed);
    for (int k = 1; k < kFftSize / 2; ++k)
    {
        const float f = (float)k * (float)sr / (float)kFftSize;
        int b = (int)((float)kAnaBins * std::log(f / 20.0f) / std::log(1000.0f));
        b = juce::jlimit(0, kAnaBins - 1, b);
        bucketMap[k] = b;
    }
    fftPos = 0;
    inGainSmoothed = inGainTarget;
    outGainSmoothed = outGainTarget;
    gainSmoothingCoef = 1.0f - std::exp(-1.0f / (0.010f * (float)sr));
    setLatencySamples(limiter.getLatencySamples() + saturator.getLatencySamples());
    skipModules.clear();
    skipModules.addTokens(juce::SystemStats::getEnvironmentVariable("AGM_SKIP", ""), " ", "");
}

void MixAgentAudioProcessor::releaseResources()
{
    instruments.reset();
    drumEngine.reset();
    eq.reset(); saturator.reset(); compressor.reset(); imager.reset();
    delay.reset(); reverb.reset(); limiter.reset();
    instrumentActive.store(false, std::memory_order_relaxed);
}

void MixAgentAudioProcessor::enqueueUiNote(int note, float velocity, bool on)
{
    int start1, size1, start2, size2;
    uiNoteFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 == 0)
    {
        uiPanic.store(true, std::memory_order_release);
        return;
    }
    uiNotes[size1 != 0 ? start1 : start2] = {
        juce::jlimit(0, 127, note), juce::jlimit(0.0f, 1.0f, agm::sanitize(velocity)), on };
    uiNoteFifo.finishedWrite(1);
}

void MixAgentAudioProcessor::uiNoteOn(int note, float velocity) { enqueueUiNote(note, velocity, true); }
void MixAgentAudioProcessor::uiNoteOff(int note) { enqueueUiNote(note, 0.0f, false); }
void MixAgentAudioProcessor::uiAllSoundOff() { uiPanic.store(true, std::memory_order_release); }

int MixAgentAudioProcessor::getInstrumentProgram() const
{
    return juce::roundToInt(apvts.getRawParameterValue("inst_program")->load(std::memory_order_relaxed));
}

double MixAgentAudioProcessor::getTailLengthSeconds() const
{
    auto value = [this](const char* id)
    {
        return (double)apvts.getRawParameterValue(id)->load(std::memory_order_relaxed);
    };
    // A sounding Pad voice keeps its own release recipe after preset browsing;
    // allow its full release, then account for the serial delay/reverb tails.
    double tail = 12.0;
    if (value("dly_enabled") > 0.5 && value("dly_mix") > 0.0)
    {
        const double feedback = value("dly_feedback");
        const double repeats = feedback > 0.0 ? 1.0 + std::ceil(std::log(0.0005) / std::log(feedback)) : 1.0;
        tail += value("dly_time") * 0.001 * repeats;
    }
    if (value("rvb_enabled") > 0.5 && value("rvb_mix") > 0.0)
        tail += value("rvb_predelay") * 0.001 + value("rvb_decay") * 1.2;
    return tail + getLatencySamples() / sampleRate.load(std::memory_order_relaxed);
}

void MixAgentAudioProcessor::setInstrumentProgram(int program)
{
    auto* parameter = apvts.getParameter("inst_program");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(
        static_cast<float>(juce::jlimit(0, (int)agm::InstrumentBank::kCount - 1, program))));
}

void MixAgentAudioProcessor::dispatchMidi(const juce::MidiMessage& message)
{
    // General MIDI percussion uses channel 10. Bass and the low preview keys
    // must remain pitched instruments, regardless of their note number.
    const bool drum = message.getChannel() == 10;
    if (message.isAllSoundOff())
    {
        if (drum) drumEngine.reset(); else instruments.allSoundOff(message.getChannel());
    }
    else if (message.isAllNotesOff())
    {
        if (drum) drumEngine.allNotesOff(); else instruments.allNotesOff(message.getChannel());
    }
    else if (message.isNoteOn())
    {
        if (drum) drumEngine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
        else instruments.noteOn(message.getNoteNumber(), message.getFloatVelocity(), message.getChannel());
    }
    else if (message.isNoteOff())
    {
        if (drum) drumEngine.noteOff(message.getNoteNumber(), 0.0f);
        else instruments.noteOff(message.getNoteNumber(), 0.0f, message.getChannel());
    }
}

void MixAgentAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    syncModules();
    for (int ch = getTotalNumInputChannels(); ch < numCh; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Drain UI events without locks or MidiBuffer allocations in processBlock.
    const bool panic = uiPanic.exchange(false, std::memory_order_acquire);
    int start1, size1, start2, size2;
    uiNoteFifo.prepareToRead(kUiNoteCapacity, start1, size1, start2, size2);
    auto drain = [&](int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& event = uiNotes[start + i];
            if (panic) continue;
            if (event.on) instruments.noteOn(event.note, event.velocity);
            else instruments.noteOff(event.note, 0.0f);
        }
    };
    drain(start1, size1); drain(start2, size2);
    uiNoteFifo.finishedRead(size1 + size2);
    if (panic)
    {
        instruments.reset();
        drumEngine.reset();
    }

    // Render only up to each MIDI timestamp, then apply the event. Notes at the
    // end of a block cannot sound early, and note-off timing is equally exact.
    int cursor = 0;
    auto renderTo = [&](int end)
    {
        const int count = end - cursor;
        instruments.renderAdd(buffer, numCh, cursor, count);
        drumEngine.renderAdd(buffer, numCh, cursor, count);
        cursor = end;
    };
    for (const auto metadata : midiMessages)
    {
        // Ignore SysEx before constructing a MidiMessage: long messages own
        // heap storage, while the note/controller messages we support fit inline.
        if (metadata.numBytes > 3) continue;
        const int position = juce::jlimit(cursor, numSamples, metadata.samplePosition);
        renderTo(position);
        dispatchMidi(metadata.getMessage());
    }
    renderTo(numSamples);
    instrumentActive.store(instruments.isActive(), std::memory_order_relaxed);

    const float rate = (float)sampleRate.load(std::memory_order_relaxed);

    float g = inGainSmoothed;
    float inL = 0.0f, inR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        g += (inGainTarget - g) * gainSmoothingCoef;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& s = buffer.getWritePointer(ch)[i];
            s = agm::sanitize(agm::sanitize(s) * g);
            if (ch == 0) inL = std::max(inL, std::abs(s));
            else inR = std::max(inR, std::abs(s));
        }
    }
    inGainSmoothed = g;
    if (numCh == 1) { inR = inL; }

    pushAnalyser(buffer);

    if (!skipModules.contains("eq")) eq.process(buffer);
    if (!skipModules.contains("sat")) saturator.process(buffer);
    if (!skipModules.contains("comp")) compressor.process(buffer);
    if (!skipModules.contains("img")) imager.process(buffer);
    if (!skipModules.contains("dly")) delay.process(buffer);
    if (!skipModules.contains("rvb")) reverb.process(buffer);
    if (!skipModules.contains("lim")) limiter.process(buffer);

    float outL = 0.0f, outR = 0.0f;
    g = outGainSmoothed;
    for (int i = 0; i < numSamples; ++i)
    {
        g += (outGainTarget - g) * gainSmoothingCoef;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& s = buffer.getWritePointer(ch)[i];
            s = agm::sanitize(s * g);
            if (ch == 0) outL = std::max(outL, std::abs(s));
            else outR = std::max(outR, std::abs(s));
        }
    }
    outGainSmoothed = g;
    if (numCh == 1) { outR = outL; }

    const float meterCoef = std::exp(-(float)numSamples / (0.3f * rate));
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
    std::fill(bucketPeak.begin(), bucketPeak.end(), 0.0f);
    for (int k = 1; k < kFftSize / 2; ++k)
    {
        const float re = fftWork[2 * k], im = fftWork[2 * k + 1];
        const float mag = std::sqrt(re * re + im * im);
        const int b = bucketMap[k];
        bucketPeak[b] = std::max(bucketPeak[b], mag);
    }
    for (int b = 0; b < kAnaBins; ++b)
    {
        // Hann coherent gain is 0.5: scale a bin-centred full-scale sine to
        // 0 dBFS instead of reporting an arbitrary FFT-size-dependent level.
        const float magnitude = 4.0f * bucketPeak[b] / (float)kFftSize;
        const float db = juce::jlimit(-90.0f, 12.0f, 20.0f * std::log10(magnitude + 1e-9f));
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
    // The editor reads a private filter snapshot, never the live audio-thread
    // coefficients. Display the requested response while DSP smoothing catches up.
    agm::EQ displayEq;
    for (const auto& parameter : parameterSnapshots)
        if (parameter.id.startsWith("eq_"))
            applyEqParameter(displayEq, parameter.id, parameter.value->load(std::memory_order_relaxed));
    displayEq.prepare(sampleRate.load(std::memory_order_relaxed), 1);
    for (int i = 0; i < n; ++i)
    {
        const float f = 20.0f * std::pow(1000.0f, (float)i / (float)std::max(1, n - 1));
        outDb[i] = displayEq.getResponseDb(f);
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
    if (layouts.getMainInputChannels() != 0
        && layouts.getMainInputChannels() != layouts.getMainOutputChannels())
        return false;
    return layouts.getMainOutputChannels() == 1 || layouts.getMainOutputChannels() == 2;
}

void MixAgentAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("factoryProgram", getCurrentProgram(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MixAgentAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 9) return;
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint32_t xmlLength = juce::ByteOrder::littleEndianInt(bytes + 4);
    if (xmlLength == 0 || xmlLength != (uint32_t)sizeInBytes - 9 || bytes[sizeInBytes - 1] != 0
        || std::memchr(bytes + 8, 0, xmlLength) != nullptr) return;
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            auto finiteNumber = [](const juce::var& input, double& output)
            {
                return agm::parseFiniteDecimal(input.toString(), output);
            };
            // Validate the complete state before replacing any live parameters.
            // NaN trim targets poison smoothing history even after automation
            // returns to a finite value. Unknown fields remain forward-compatible.
            juce::StringArray seenParameters;
            for (const auto& child : state)
            {
                const auto id = child.getProperty("id").toString();
                if (auto* parameter = apvts.getParameter(id))
                {
                    double value = 0.0;
                    if (!child.hasType("PARAM") || seenParameters.contains(id) || !child.hasProperty("value")
                        || !finiteNumber(child.getProperty("value"), value)) return;
                    const auto range = parameter->getNormalisableRange();
                    const double tolerance = 1.0e-6 * (1.0 + std::max(std::abs(range.start), std::abs(range.end)));
                    if (value < range.start - tolerance || value > range.end + tolerance) return;
                    seenParameters.add(id);
                }
            }
            double program = 0.0;
            if (state.hasProperty("factoryProgram")
                && (!finiteNumber(state.getProperty("factoryProgram"), program)
                    || program != std::trunc(program) || program < 0 || program >= kPresetCount)) return;
            // Older sessions predate some controls. Complete them from declared
            // defaults so recall never inherits trims or instruments from the
            // patch that happened to be open before loading the session.
            for (const auto& parameter : parameterSnapshots)
                if (!seenParameters.contains(parameter.id))
                {
                    auto* ranged = apvts.getParameter(parameter.id);
                    juce::ValueTree missing("PARAM");
                    missing.setProperty("id", parameter.id, nullptr);
                    missing.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
                    state.addChild(missing, -1, nullptr);
                }
            currentProgram.store((int)program, std::memory_order_relaxed);
            apvts.replaceState(state);
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
    const int selectedProgram = juce::jlimit(0, kPresetCount - 1, index);
    currentProgram.store(selectedProgram, std::memory_order_relaxed);
    // Every factory preset starts from the same baseline. Otherwise loading
    // Clean Master after Wide & Spacey accidentally retains the delay/reverb.
    for (auto* parameter : getParameters())
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
    auto setRaw = [&](const juce::String& id, float raw)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(raw));
    };
    switch (selectedProgram)
    {
    case 0: break;
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

