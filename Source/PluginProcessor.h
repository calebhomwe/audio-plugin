#pragma once
#include <JuceHeader.h>
#include "DSP/EQ.h"
#include "DSP/Saturation.h"
#include "DSP/Compressor.h"
#include "DSP/StereoImager.h"
#include "DSP/Delay.h"
#include "DSP/Reverb.h"
#include "DSP/Limiter.h"
#include "DSP/DrumEngine.h"
#include "DSP/InstrumentBank.h"

class MixAgentAudioProcessor : public juce::AudioProcessor, public juce::AudioProcessorValueTreeState::Listener
{
public:
    MixAgentAudioProcessor();
    ~MixAgentAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MixAgent"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return kPresetCount; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int getLatencySamples() const { return limiter.getLatencySamples(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    void getAnalyzerSpectrum(float* outDb, int n) const;
    void getEqCurve(float* outDb, int n) const;
    float getInLevel(int ch) const;
    float getOutLevel(int ch) const;
    float getCompGrDb() const { return compressor.getGainReductionDb(); }
    float getLimGrDb() const { return limiter.getGainReductionDb(); }
    bool getDrumActive() const { return drumEngine.isActive(); }
    bool getInstrumentActive() const { return instruments.isActive(); }
    int getInstrumentProgram() const { return instruments.getProgram(); }
    void setInstrumentProgram(int p) { instruments.setProgram(p); }
    juce::StringArray skipModules;

    // UI-driven triggers (message thread safe; drained on the audio thread)
    void uiNoteOn(int note, float velocity);
    void uiNoteOff(int note);

private:
    void handleParameter(const juce::String& id, float rawValue);
    void syncModules();
    void pushAnalyser(const juce::AudioBuffer<float>& buffer);
    void runFft();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    agm::EQ eq;
    agm::Saturation saturator;
    agm::Compressor compressor;
    agm::StereoImager imager;
    agm::Delay delay;
    agm::Reverb reverb;
    agm::Limiter limiter;
    agm::DrumEngine drumEngine;
    agm::InstrumentBank instruments;

    float inGainDb = 0.0f, outGainDb = 0.0f;
    float inGainSmoothed = 1.0f, outGainSmoothed = 1.0f;
    double sampleRate = 44100.0;
    int currentProgram = 0;
    static constexpr int kPresetCount = 6;

    std::atomic<float> inLevelL { 0.0f }, inLevelR { 0.0f };
    std::atomic<float> outLevelL { 0.0f }, outLevelR { 0.0f };
    float inPeakL = 0.0f, inPeakR = 0.0f, outPeakL = 0.0f, outPeakR = 0.0f;

    juce::CriticalSection uiNoteLock;
    juce::MidiBuffer uiNotes;

    static constexpr int kFftSize = 2048;
    static constexpr int kAnaBins = 600;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { (size_t)kFftSize, juce::dsp::WindowingFunction<float>::hann, false, 0.0f };
    std::vector<float> fftIn;
    std::vector<float> fftWork;
    std::vector<int> bucketMap;
    std::vector<float> bucketSum;
    std::vector<int> bucketCount;
    std::vector<float> anaSmooth;
    std::array<std::atomic<float>, kAnaBins> anaOut {};
    int fftPos = 0;
};
