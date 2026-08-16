#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>
#include <algorithm>

namespace agm {

// DSP-synthesized trap instrument engine. No samples required.
//   BASS  : pitch-tracked 808 sub (chromatic, glide, sat drive, release tail)
//   KICK  : pitch-drop sine + click transient
//   SNARE : tonal body + filtered noise burst
//   HAT   : high-passed noise, closed (short) / open (long), subtle pan
//   CLAP  : noise with multi-slap amplitude bumps
// Per-family voice pools with round-robin steering; one-shots always run to
// full length (noteOff only releases the bass), so hats never cut 808 tails.
class DrumEngine
{
public:
    enum class Family : int { Kick, Snare, Cymbal, Bass, kCount };

    void prepare(double sampleRateIn, int blockSize)
    {
        sr = sampleRateIn > 1.0 ? sampleRateIn : 44100.0;
        maxLen = (uint32_t)std::min(blockSize, 512) + 128u; // scratch length
        reset();
    }

    void reset()
    {
        std::fill(voices.begin(), voices.end(), Voice {});
        rotate = 0;
        noiseState = 0.0f;
        subGlide = 0.0f;
    }

    void setBassDrive(float d)        { drive = juce::jlimit(0.1f, 4.0f, d); }
    void setEnabled(bool on)          { enabled = on; if (!on) allNotesOff(); }
    void setBassGlideSec(float s)     { glide = juce::jlimit(0.0f, 1.0f, s); }
    void setBassLevelDb(float db)     { bassDb = db; }
    void setDrumLevelDb(float db)     { drumDb = db; }
    bool isActive() const
    {
        for (auto& v : voices) if (v.active) return true;
        return false;
    }

    void noteOn(int note, float velocity)
    {
        const int n = juce::jlimit(0, 127, note);
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);

        switch (familyForNote(n))
        {
        case Family::Kick:
        {
            auto& v = voiceFor(Family::Kick);
            v = Voice {};
            v.family = Family::Kick; v.active = true; v.vel = vel;
            break;
        }
        case Family::Snare:
        {
            auto& v = voiceFor(Family::Snare);
            v = Voice {};
            v.family = Family::Snare; v.active = true; v.vel = vel;
            break;
        }
        case Family::Cymbal:
        {
            auto& v = voiceFor(Family::Cymbal);
            v = Voice {};
            v.family = Family::Cymbal;
            v.vel = vel;
            v.active = true;
            v.open = (n == 46 || n == 42);
            v.randomOffset = white() * 0.5f * ((n % 5) == 0 ? 1.0f : 0.3f);
            v.panL = 0.92f; v.panR = 0.92f;
            const int pan = (n % 7); {
                const float p = (pan - 3) * 0.08f;
                v.panL += juce::jlimit(-0.2f, 0.2f, -p);
                v.panR += juce::jlimit(-0.2f, 0.2f, p);
            }
            break;
        }
        default: break;
        }
    }

    void noteOff(int note, float /*velocity*/)
    {
        (void)note; // one-shots always run full length
    }

    void allNotesOff()
    {
        for (auto& v : voices)
        {
            if (v.family == Family::Bass) v.releasing = true;
            else v.active = false;
        }
    }

    // Mix the drum/bass bus into the host buffer BEFORE the FX chain.
    void renderAdd(juce::AudioBuffer<float>& b, int numChannels)
    {
        if (!enabled) return;
        const int num = b.getNumSamples();
        if (num <= 0 || numChannels <= 0) return;

        scratch.setSize(juce::jmax(2, numChannels), num, false, false, true);

        for (auto& v : voices)
        {
            if (!v.active) continue;
            renderVoice(v, num);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* dst = b.getWritePointer(ch);
                const float* src = scratch.getReadPointer(ch % 2);
                const float g = (ch % 2 == 0) ? v.panL : v.panR;
                for (int i = 0; i < num; ++i)
                    dst[i] += src[i] * g;
            }
        }
    }

    int bassLow = 24, bassHigh = 48;

private:
    struct Voice
    {
        Family family = Family::Kick;
        bool active = false;
        bool releasing = false;
        bool open = false;
        int note = 0;
        float vel = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        float phase = 0.0f;
        float amp = 0.0f;
        float pos = 0.0f;
        float hpMem = 0.0f;
        float randomOffset = 0.0f;
    };

    static constexpr int kPools = 10 + 6 + 16 + 2;
    std::array<Voice, kPools> voices {};
    uint32_t rotate = 0;
    double sr = 44100.0;
    uint32_t maxLen = 640;
    float drive = 1.0f, glide = 0.1f;
    bool enabled = true;
    float bassDb = 0.0f, drumDb = -16.0f;
    float noiseState = 0.0f;
    float subGlide = 0.0f;
    juce::AudioBuffer<float> scratch;

    Family familyForNote(int n) const
    {
        if (n == 35 || n == 36 || n == 37 || n == 41 || n == 60) return Family::Kick;
        if (n == 38 || n == 39 || n == 40 || n == 49 || n == 57) return Family::Snare;
        return Family::Cymbal; // hats 42/44/46, open hats, percs, everything else
    }

    Voice& voiceFor(Family fam)
    {
        int begin = 0, end = 0;
        switch (fam)
        {
        case Family::Kick:   begin = 0;  end = 10; break;
        case Family::Snare:  begin = 10; end = 16; break;
        case Family::Cymbal: begin = 16; end = 32; break;
        case Family::Bass:   begin = 32; end = 34; break;
        default: break;
        }
        const int base = (int)(rotate++ % (end - begin));
        for (int i = 0; i < (end - begin); ++i)
        {
            const int idx = begin + (base + i) % (end - begin);
            if (!voices[idx].active) return voices[idx];
        }
        return voices[begin + base];
    }

    float white()
    {
        uint32_t lcg = (uint32_t)(noiseState * 2147483647.0f) + 1u;
        lcg = lcg * 1664525u + 1013904223u;
        noiseState = (float)(lcg >> 8) / 8388608.0f - 1.0f;
        return noiseState;
    }

    float noteFreq(int n) const { return 440.0f * std::pow(2.0f, (float)(n - 69) / 12.0f); }
    float envExp(float t, float tauMs) { return std::exp(-t / (0.001f * tauMs * (float)sr)); }

    // per-family synthesis into scratch
    void renderVoice(Voice& v, int num)
    {
        scratch.clear();
        const bool mono = scratch.getNumChannels() < 2;

#define AGM_OUT1(ch,val) if (ch==0 || numCh==2) scratch.setSample(((ch==1) ? 1 : 0), i, val)
        const int numCh = scratch.getNumChannels();

        switch (v.family)
        {
        case Family::Bass:
        {
            const float freq = noteFreq(v.note);
            const float driveK = drive;
            const float glideCoef = glide > 0.0f ? (float)std::exp(-1.0 / (glide * (float)sr)) : 0.0f;
            const float atkStep = (float)(1.0 / (0.008 * sr));
            const float relCoef = (float)std::exp(-1.0 / (0.09 * sr));
            if (v.amp <= 0.0f && v.releasing) { v.active = false; return; }
            for (int i = 0; i < num; ++i)
            {
                subGlide += (freq - subGlide) * (1.0f - glideCoef);
                v.phase += (float)(subGlide / sr);
                float s = (float)std::sin(v.phase * 6.283185307179586);
                s += 0.25f * (float)std::sin(v.phase * 12.566370614359172) * 0.3f;      // sub octave shimmer
                s = std::tanh(s * 0.3f * driveK);                                        // soft sat glue
                if (v.releasing) { v.amp *= relCoef; } else { v.amp = juce::jmin(1.0f, v.amp + atkStep); }
                const float out = (s * 2.2f) * v.amp * v.amp * std::pow(v.vel, 1.2f);
                AGM_OUT1(0, out); AGM_OUT1(1, out);
            }
            break;
        }
        case Family::Kick:
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                const float p = 42.0f + 122.0f * std::exp(-t / (0.030f * (float)sr));
                v.phase += (float)(p / sr);
                const float body = std::sin(v.phase * 6.283185307179586) * envExp(t, 240.0f);
                const float click = (t < 12.0f) ? (0.45f * (1.0f - t / 12.0f)) : 0.0f;
                const float pun = 0.35f * v.vel * envExp(t, 8.0f) * std::sin(v.phase * 37.69911184307752);
                float out = body * 0.95f + click + pun;
                out *= std::pow(v.vel, 0.7f);
                AGM_OUT1(0, out); AGM_OUT1(1, out);
            }
            break;
        case Family::Snare:
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                v.phase += 162.0f / (float)sr;
                const float body = std::sin(v.phase * 6.283185307179586) * envExp(t, 120.0f);
                noiseState = noiseState * 0.6f + white() * 0.4f;
                const float nz = noiseState * envExp(t, 95.0f);
                const float out = body * 0.5f + nz * 0.9f;
                AGM_OUT1(0, out); AGM_OUT1(1, out);
            }
            break;
        case Family::Cymbal:
        {
            const float decay = v.open ? 320.0f : 65.0f;
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                const float hp = white() - v.hpMem;
                v.hpMem = white();
                float out = hp * envExp(t, decay);
                out += 0.5f * (float)std::sin((1600.0f + 800.0f * v.randomOffset) / (float)sr * i * 6.28318f) * envExp(t, decay * 0.35f);
                AGM_OUT1(0, out); AGM_OUT1(1, out);
            }
            break;
        }
        default: break;
        }
#undef AGM_OUT1

        v.pos += (float)num;
        // one-shots die when their tail has decayed; bass continues until release completes
        if (v.family != Family::Bass)
        {
            const float tailDecay = (v.family == Family::Cymbal && v.open) ? 320.0f : 240.0f;
            if (envExp(v.pos, tailDecay) < 0.01f)
                v.active = false;
        }

        // drum gain staging (per voice)
        const float g = v.family == Family::Bass
            ? juce::Decibels::decibelsToGain(bassDb)
            : juce::Decibels::decibelsToGain(drumDb);
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* w = scratch.getWritePointer(ch);
            for (int i = 0; i < num; ++i) w[i] *= g * 0.5f;
        }
    }
};

} // namespace agm