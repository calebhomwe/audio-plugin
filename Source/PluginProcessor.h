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

class MixAgentAudioProcessor : public juce::AudioProcessor
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
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return kPresetCount; }
    int getCurrentProgram() override { return currentProgram.load(std::memory_order_relaxed); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    void getAnalyzerSpectrum(float* outDb, int n) const;
    void getEqCurve(float* outDb, int n) const;
    float getInLevel(int ch) const;
    float getOutLevel(int ch) const;
    float getCompGrDb() const { return compressor.getGainReductionDb(); }
    float getLimGrDb() const { return limiter.getGainReductionDb(); }
    bool getInstrumentActive() const { return instrumentActive.load(std::memory_order_relaxed); }
    double getProcessingSampleRate() const { return sampleRate.load(std::memory_order_relaxed); }
    int getInstrumentProgram() const;
    void setInstrumentProgram(int p);
    bool isFavorite(int program) const;
    void setFavorite(int program, bool fav);
    int getFavoriteCount() const;
    juce::StringArray skipModules;

    // UI-driven triggers (message thread safe; drained on the audio thread)
    void uiNoteOn(int note, float velocity);
    void uiNoteOff(int note);
    void uiAllSoundOff();

private:
    void handleParameter(const juce::String& id, float rawValue);
    void syncModules(bool force = false);
    void enqueueUiNote(int note, float velocity, bool on);
    void dispatchMidi(const juce::MidiMessage& message);
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

    struct ParameterSnapshot
    {
        juce::String id;
        std::atomic<float>* value;
        float lastValue;
    };
    std::vector<ParameterSnapshot> parameterSnapshots;

    float inGainTarget = 1.0f, outGainTarget = 1.0f;
    float gainSmoothingCoef = 0.0f;
    float inGainSmoothed = 1.0f, outGainSmoothed = 1.0f;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> instrumentActive { false };
    static constexpr int kPresetCount = 12;

    std::atomic<float> inLevelL { 0.0f }, inLevelR { 0.0f };
    std::atomic<float> outLevelL { 0.0f }, outLevelR { 0.0f };
    float inPeakL = 0.0f, inPeakR = 0.0f, outPeakL = 0.0f, outPeakR = 0.0f;

    // Single message-thread producer, single audio-thread consumer. Overflow
    // requests a panic rather than leaving a dropped note-off hanging forever.
    struct UiNote { int note = 0; float velocity = 0.0f; bool on = false; };
    static constexpr int kUiNoteCapacity = 128;
    std::array<UiNote, kUiNoteCapacity> uiNotes {};
    juce::AbstractFifo uiNoteFifo { kUiNoteCapacity };
    std::atomic<bool> uiPanic { false };

    static constexpr int kFftSize = 2048;
    static constexpr int kAnaBins = 600;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { (size_t)kFftSize, juce::dsp::WindowingFunction<float>::hann, false, 0.0f };
    std::vector<float> fftIn;
    std::vector<float> fftWork;
    std::vector<int> bucketMap;
    std::vector<float> bucketPeak;
    std::vector<float> anaSmooth;
    std::array<std::atomic<float>, kAnaBins> anaOut {};
    int fftPos = 0;
};
