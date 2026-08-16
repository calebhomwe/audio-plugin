#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>
#include <vector>

namespace agm {

// Multi-timbral instrument library (Nexus-style), fully DSP-synthesized so
// every factory sound is owned and license-clean. A "program" is a recipe
// describing oscillators / FM / filter / ADSR; voices are polyphonic and
// chromatic over MIDI. Programs span many sound types — NOT 808-centric.
//
//   Pluck   Karplus-ish decaying saw through a closing filter
//   Bell    inharmonic FM, fast exponential decay
//   Keys    detuned saws, soft attack, mild chorus
//   Lead    unison saw stack, bright, sustain
//   Pad     slow-attack detuned saws, long release
//   Brass   saw + filter envelope, buzzy
//   Strings sawtooth ensemble, medium attack, slight vibrato
//   EP      FM tine, fast decay to sustain
//   Organ   additive drawbar sines, full sustain
//   Sub     sine bass (generic sub, not "808")
class InstrumentBank
{
public:
    enum Program : int {
        Pluck = 0, Bell, Keys, Lead, Pad, Brass, Strings, EPiano, Organ, Sub, kCount
    };

    static const char* programName(int p)
    {
        static const char* names[] = {
            "Pluck", "Bell", "Keys", "Lead", "Pad", "Brass", "Strings",
            "E.Piano", "Organ", "Sub"
        };
        return names[juce::jlimit(0, (int)kCount - 1, p)];
    }

    static const char* programCategory(int p)
    {
        switch (juce::jlimit(0, (int)kCount - 1, p))
        {
        case Pluck:   case Bell:    case Keys:    case Lead:
        case Pad:     case Brass:   case Strings: case EPiano:
        case Organ:                 return "Keys/Lead";
        case Sub:                   return "Bass";
        default:                    return "Misc";
        }
    }

    void prepare(double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = Voice {};
        randState = 0x1234u;
        setProgram(currentProgram);
    }

    void setProgram(int p)
    {
        currentProgram = juce::jlimit(0, (int)kCount - 1, p);
        recipe = makeRecipe((Program)currentProgram);
    }

    int getProgram() const { return currentProgram; }

    void setLevelDb(float db) { levelDb = db; }
    void setEnabled(bool on) { enabled = on; if (!on) allNotesOff(); }

    void noteOn(int note, float velocity)
    {
        const int n = juce::jlimit(0, 127, note);
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        Voice& v = voiceFor();
        v = Voice {};
        v.active = true;
        v.note = n;
        v.vel = vel;
        v.freq = noteToFreq(n);
        v.oscPhase = {};
        v.envStage = EnvStage::Attack;
        v.envLevel = 0.0f;
        v.envRate = recipe.attackRate;
        v.filterState = 0.0f;
        // initialise oscillator detune seeds for unison
        for (int i = 0; i < recipe.nOsc; ++i)
            v.detune[i] = (float)((rand01() - 0.5) * 2.0) * recipe.detune;
        v.modPhase = 0.0f;
        v.chorusPhase = (float)(rand01() * 6.283185);
    }

    void noteOff(int note, float /*velocity*/)
    {
        const int n = juce::jlimit(0, 127, note);
        for (auto& v : voices)
            if (v.active && v.note == n && v.envStage != EnvStage::Release && v.envStage != EnvStage::Off)
            {
                v.envStage = EnvStage::Release;
                v.envRate = recipe.releaseRate;
            }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
        {
            v.envStage = EnvStage::Release;
            v.envRate = recipe.releaseRate * 4.0f;
        }
    }

    bool isActive() const
    {
        for (auto& v : voices) if (v.active && v.envStage != EnvStage::Off) return true;
        return false;
    }

    void renderAdd(juce::AudioBuffer<float>& b, int numChannels)
    {
        if (!enabled) return;
        const int num = b.getNumSamples();
        if (num <= 0) return;
        scratch.setSize(juce::jmax(2, numChannels), num, false, false, true);
        const float outGain = juce::Decibels::decibelsToGain(levelDb) * 0.5f;

        for (auto& v : voices)
        {
            if (!v.active || v.envStage == EnvStage::Off) continue;
            scratch.clear();
            renderVoice(v, num);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* dst = b.getWritePointer(ch);
                const float* src = scratch.getReadPointer(ch % 2);
                for (int i = 0; i < num; ++i) dst[i] += src[i] * outGain;
            }
        }
    }

private:
    enum class EnvStage { Off, Attack, Decay, Sustain, Release };

    struct Recipe
    {
        int nOsc = 1;
        float detune = 0.0f;       // cents spread per osc
        bool saw = true;
        bool square = false;
        float attackRate = 0.0f;  // 1/sr-scaled rate
        float decayRate = 0.0f;
        float sustainLevel = 1.0f;
        float releaseRate = 0.0f;
        float filterBase = 0.0f;   // 0..1 of nyquist
        float filterEnv = 0.0f;     // amount the env opens/closes the filter
        float filterDecay = 0.0f;   // env-to-filter decay rate
        bool fm = false;
        float modRatio = 1.0f;
        float modAmount = 0.0f;
        float octaveShift = 0.0f;
        float gain = 1.0f;
    };

    struct Voice
    {
        bool active = false;
        int note = 60;
        float vel = 1.0f;
        float freq = 440.0f;
        std::array<float, 4> oscPhase = {};
        float modPhase = 0.0f;
        std::array<float, 4> detune = {};
        float envLevel = 0.0f;
        float envRate = 0.0f;
        EnvStage envStage = EnvStage::Off;
        float filterState = 0.0f;
        float filterEnvLevel = 1.0f;
        float chorusPhase = 0.0f;
    };

    static constexpr int kVoices = 24;
    std::array<Voice, kVoices> voices {};
    uint32_t voiceAge = 0;
    Recipe recipe {};
    int currentProgram = (int)Pluck;
    double sr = 44100.0;
    float levelDb = 0.0f;
    bool enabled = true;
    juce::AudioBuffer<float> scratch;
    uint32_t randState = 0x1234u;

    float rand01()
    {
        randState = randState * 1664525u + 1013904223u;
        return (float)((randState >> 9) & 0xFFFF) / 65535.0f;
    }

    float noteToFreq(int n) const
    {
        return (float)(440.0 * std::pow(2.0, (double)(n - 69) / 12.0));
    }

    Voice& voiceFor()
    {
        // steal the oldest voice in release/off, else the lowest-env voice
        Voice* best = nullptr;
        uint32_t bestAge = 0;
        float bestEnv = 2.0f;
        for (auto& v : voices)
        {
            if (!v.active || v.envStage == EnvStage::Release || v.envStage == EnvStage::Off)
            {
                if (v.envLevel <= bestEnv) { bestEnv = v.envLevel; best = &v; }
            }
        }
        if (best) return *best;
        for (auto& v : voices)
            if (v.envLevel <= bestEnv) { bestEnv = v.envLevel; best = &v; }
        return *best;
    }

    static float sawWave(float ph)
    {
        float x = std::fmod(ph, 1.0f);
        if (x < 0.0f) x += 1.0f;
        return 2.0f * x - 1.0f;
    }

    static float squareWave(float ph)
    {
        float x = std::fmod(ph, 1.0f);
        if (x < 0.0f) x += 1.0f;
        return x < 0.5f ? 1.0f : -1.0f;
    }

    static float ratePerSecToCoef(float ratePerSec, double sampleRate)
    {
        return (ratePerSec <= 0.0f) ? 1.0f
            : (float)(1.0 - std::exp(-1.0 / (ratePerSec * sampleRate)));
    }

    Recipe makeRecipe(Program p)
    {
        Recipe r;
        const double s = sr;
        switch (p)
        {
        case Pluck:
            r.nOsc = 1; r.saw = true; r.detune = 0.0f;
            r.attackRate = 1.0f / 0.001f;          // 1ms
            r.decayRate = 1.0f / 0.45f;             // 450ms decay
            r.sustainLevel = 0.0f;
            r.releaseRate = 1.0f / 0.20f;
            r.filterBase = 0.08f; r.filterEnv = 0.9f; r.filterDecay = 1.0f / 0.30f;
            r.gain = 0.8f;
            break;
        case Bell:
            r.nOsc = 1; r.saw = false; r.fm = true;
            r.modRatio = 3.5f; r.modAmount = 4.0f;
            r.attackRate = 1.0f / 0.002f;
            r.decayRate = 1.0f / 1.6f;
            r.sustainLevel = 0.0f;
            r.releaseRate = 1.0f / 0.8f;
            r.gain = 0.7f;
            break;
        case Keys:
            r.nOsc = 2; r.saw = true; r.detune = 6.0f;   // cents
            r.attackRate = 1.0f / 0.004f;
            r.decayRate = 1.0f / 1.2f;
            r.sustainLevel = 0.7f;
            r.releaseRate = 1.0f / 0.35f;
            r.filterBase = 0.5f; r.filterEnv = 0.2f; r.filterDecay = 1.0f / 0.4f;
            r.gain = 0.55f;
            break;
        case Lead:
            r.nOsc = 3; r.saw = true; r.detune = 10.0f;
            r.attackRate = 1.0f / 0.003f;
            r.decayRate = 1.0f / 0.3f;
            r.sustainLevel = 0.9f;
            r.releaseRate = 1.0f / 0.25f;
            r.filterBase = 0.7f; r.filterEnv = 0.1f; r.filterDecay = 1.0f / 0.2f;
            r.gain = 0.5f;
            break;
        case Pad:
            r.nOsc = 4; r.saw = true; r.detune = 14.0f;
            r.attackRate = 1.0f / 0.6f;
            r.decayRate = 1.0f / 1.0f;
            r.sustainLevel = 1.0f;
            r.releaseRate = 1.0f / 1.5f;
            r.filterBase = 0.35f; r.filterEnv = 0.2f; r.filterDecay = 1.0f / 0.6f;
            r.gain = 0.4f;
            break;
        case Brass:
            r.nOsc = 2; r.saw = true; r.detune = 4.0f;
            r.attackRate = 1.0f / 0.05f;
            r.decayRate = 1.0f / 0.3f;
            r.sustainLevel = 0.85f;
            r.releaseRate = 1.0f / 0.25f;
            r.filterBase = 0.4f; r.filterEnv = 0.5f; r.filterDecay = 1.0f / 0.25f;
            r.gain = 0.5f;
            break;
        case Strings:
            r.nOsc = 3; r.saw = true; r.detune = 8.0f;
            r.attackRate = 1.0f / 0.12f;
            r.decayRate = 1.0f / 0.5f;
            r.sustainLevel = 0.9f;
            r.releaseRate = 1.0f / 0.4f;
            r.filterBase = 0.55f; r.filterEnv = 0.1f; r.filterDecay = 1.0f / 0.3f;
            r.gain = 0.45f;
            break;
        case EPiano:
            r.nOsc = 1; r.saw = false; r.fm = true;
            r.modRatio = 14.0f; r.modAmount = 3.0f;
            r.attackRate = 1.0f / 0.001f;
            r.decayRate = 1.0f / 1.8f;
            r.sustainLevel = 0.25f;
            r.releaseRate = 1.0f / 0.5f;
            r.filterBase = 0.6f; r.filterEnv = 0.0f;
            r.gain = 0.7f;
            break;
        case Organ:
            r.nOsc = 4; r.saw = false; r.detune = 0.0f;
            r.attackRate = 1.0f / 0.01f;
            r.decayRate = 1.0f / 0.2f;
            r.sustainLevel = 1.0f;
            r.releaseRate = 1.0f / 0.15f;
            r.filterBase = 1.0f;
            r.gain = 0.45f;
            break;
        case Sub:
            r.nOsc = 1; r.saw = false; r.fm = false;
            r.attackRate = 1.0f / 0.01f;
            r.decayRate = 1.0f / 0.4f;
            r.sustainLevel = 0.95f;
            r.releaseRate = 1.0f / 0.3f;
            r.filterBase = 0.05f;
            r.gain = 0.9f;
            r.octaveShift = -1.0f;
            break;
        }
        (void)s;
        return r;
    }

    void renderVoice(Voice& v, int num)
    {
        const Recipe& r = recipe;
        const float fundamental = v.freq * std::pow(2.0f, r.octaveShift);
        const float phaseInc = fundamental / (float)sr;
        const float modInc = phaseInc * r.modRatio;
        const float aCoef = ratePerSecToCoef(r.attackRate, sr);
        const float dCoef = ratePerSecToCoef(r.decayRate, sr);
        const float relCoef = ratePerSecToCoef(r.releaseRate, sr);
        const float fEnvCoef = ratePerSecToCoef(r.filterDecay, sr);
        const float nyq = (float)sr * 0.5f;
        const float filterMax = std::min(1.0f, r.filterBase);

        for (int i = 0; i < num; ++i)
        {
            // ADSR
            switch (v.envStage)
            {
            case EnvStage::Attack:
                v.envLevel += aCoef * (1.0f - v.envLevel);
                if (v.envLevel > 0.999f) { v.envLevel = 1.0f; v.envStage = EnvStage::Decay; }
                break;
            case EnvStage::Decay:
                v.envLevel += dCoef * (r.sustainLevel - v.envLevel);
                if (v.envLevel <= r.sustainLevel + 0.001f) { v.envLevel = r.sustainLevel; v.envStage = EnvStage::Sustain; }
                break;
            case EnvStage::Sustain:
                v.envLevel = r.sustainLevel;
                break;
            case EnvStage::Release:
                v.envLevel += relCoef * (0.0f - v.envLevel);
                if (v.envLevel < 0.0005f) { v.envLevel = 0.0f; v.envStage = EnvStage::Off; v.active = false; }
                break;
            default: break;
            }

            // filter envelope (closes from 1 toward 0)
            if (r.filterEnv > 0.0f)
                v.filterEnvLevel += fEnvCoef * (0.0f - v.filterEnvLevel);
            const float cutoff = juce::jlimit(40.0f, nyq * 0.95f,
                nyq * (filterMax + r.filterEnv * v.filterEnvLevel * (1.0f - filterMax)));
            const float fc = std::tan(3.14159265f * cutoff / (float)sr);
            const float a1 = fc / (1.0f + fc);

            // oscillators
            float sample = 0.0f;
            if (r.fm)
            {
                v.modPhase += modInc;
                const float mod = std::sin(v.modPhase * 6.283185) * r.modAmount;
                v.oscPhase[0] += phaseInc * (1.0f + mod * 0.05f);
                sample = std::sin(v.oscPhase[0] * 6.283185 + mod);
            }
            else if (r.saw)
            {
                for (int o = 0; o < r.nOsc; ++o)
                {
                    const float det = std::pow(2.0f, v.detune[o] / 12.0f);
                    v.oscPhase[o] += phaseInc * det;
                    sample += sawWave(v.oscPhase[o]) / (float)r.nOsc;
                }
            }
            else
            {
                if (currentProgram == Organ)
                {
                    v.oscPhase[0] += phaseInc;
                    sample = 0.5f * std::sin(v.oscPhase[0] * 6.283185)
                           + 0.25f * std::sin(v.oscPhase[0] * 2.0f * 6.283185)
                           + 0.15f * std::sin(v.oscPhase[0] * 3.0f * 6.283185)
                           + 0.10f * std::sin(v.oscPhase[0] * 4.0f * 6.283185);
                }
                else
                {
                    v.oscPhase[0] += phaseInc;
                    sample = std::sin(v.oscPhase[0] * 6.283185);
                }
            }

            // one-pole lowpass for character (plucks/keys/brass)
            if (filterMax < 0.95f)
            {
                v.filterState += a1 * (sample - v.filterState);
                sample = v.filterState;
            }

            const float out = sample * v.envLevel * v.vel * r.gain;
            scratch.setSample(0, i, out);
            scratch.setSample(1, i, out);
        }
    }
};

} // namespace agm