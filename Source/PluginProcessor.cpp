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
}

void MixAgentAudioProcessor::syncModules()
{
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            handleParameter(rp->paramID, rp->getNormalisableRange().convertFrom0to1(rp->getValue()));
}

void MixAgentAudioProcessor::prepareToPlay(double sr, int blockSize)
{
    sampleRate = sr;
    eq.prepare(sr, blockSize);
    saturator.prepare(sr, blockSize);
    compressor.prepare(sr, blockSize);
    imager.prepare(sr, blockSize);
    delay.prepare(sr, blockSize);
    reverb.prepare(sr, blockSize);
    limiter.prepare(sr, blockSize);

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
    setLatencySamples(limiter.getLatencySamples());
    skipModules.clear();
    skipModules.addTokens(juce::SystemStats::getEnvironmentVariable("AGM_SKIP", ""), " ", "");
}

void MixAgentAudioProcessor::releaseResources() {}

void MixAgentAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    const float gainCoef = 1.0f - std::exp(-(float)numSamples / (0.010f * (float)sampleRate));
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

void MixAgentAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MixAgentAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

namespace
{
const char* const kPresetNames[] = { "Init", "Clean Master", "Vocal Presence", "Drum Bus Punch", "Wide & Spacey", "Warm Tape" };
}

const juce::String MixAgentAudioProcessor::getProgramName(int index)
{
    return kPresetNames[juce::jlimit(0, kPresetCount - 1, index)];
}

void MixAgentAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, kPresetCount - 1, index);
    auto setRaw = [&](const juce::String& id, float raw)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(raw));
    };
    switch (currentProgram)
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
    }
}
