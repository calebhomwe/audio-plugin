#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>
#include <algorithm>

namespace agm {

// DSP-synthesized drum one-shot engine. No samples required.
//   KICK  : pitch-drop sine + click transient
//   SNARE : tonal body + filtered noise burst
//   CYMBAL: high-passed noise, closed (short) / open (long), subtle pan
// Per-family voice pools with round-robin steering; one-shots always run to
// full length (noteOff is ignored) so a hat trigger never cuts a kick tail.
class DrumEngine
{
public:
    enum class Family : int { Kick, Snare, Cymbal, kCount };

    void prepare(double sampleRateIn, int blockSize)
    {
        sr = sampleRateIn > 1.0 ? sampleRateIn : 44100.0;
        preparedBlock = juce::jmax(blockSize, 1);
        reset();
    }

    void reset()
    {
        std::fill(voices.begin(), voices.end(), Voice {});
        rotate = 0;
        rngState = 0x9E3779B9u;
    }

    void setEnabled(bool on)          { enabled = on; if (!on) allNotesOff(); }
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
            const float p = ((n % 7) - 3) * 0.08f;
            v.panL += juce::jlimit(-0.2f, 0.2f, -p);
            v.panR += juce::jlimit(-0.2f, 0.2f, p);
            break;
        }
        default: break;
        }
    }

    void noteOff(int /*note*/, float /*velocity*/) {}

    void allNotesOff()
    {
        for (auto& v : voices)
            v.active = false;
    }

    // Mix the drum bus into the host buffer BEFORE the FX chain.
    void renderAdd(juce::AudioBuffer<float>& b, int numChannels)
    {
        if (!enabled) return;
        const int num = b.getNumSamples();
        if (num <= 0 || numChannels <= 0) return;

        if (scratch.getNumSamples() < num)
            scratch.setSize(juce::jmax(2, numChannels), num, false, false, true);

        for (auto& v : voices)
        {
            if (!v.active) continue;
            float* const outL = scratch.getWritePointer(0);
            float* const outR = scratch.getWritePointer(1);
            renderVoice(v, num, outL, outR);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* dst = b.getWritePointer(ch);
                const float* src = (ch & 1) ? outR : outL;
                for (int i = 0; i < num; ++i)
                    dst[i] += src[i];
            }
        }
    }

private:
    struct Voice
    {
        Family family = Family::Kick;
        bool active = false;
        bool open = false;
        int note = 0;
        float vel = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        float phase = 0.0f;
        float pos = 0.0f;
        float hpMem = 0.0f;
        float randomOffset = 0.0f;
    };

    static constexpr int kPools = 10 + 6 + 16;
    std::array<Voice, kPools> voices {};
    uint32_t rotate = 0;
    double sr = 44100.0;
    int preparedBlock = 1;
    uint32_t rngState = 0x9E3779B9u;
    bool enabled = true;
    float drumDb = -16.0f;
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
        default: break;
        }
        const int span = end - begin;
        const int base = (int)(rotate++ % (uint32_t)span);
        for (int i = 0; i < span; ++i)
        {
            const int idx = begin + (base + i) % span;
            if (!voices[idx].active) return voices[idx];
        }
        return voices[begin + base];
    }

    float white()
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(rngState >> 8) / 8388608.0f - 1.0f;
    }

    float envExp(float t, float tauMs) { return std::exp(-t / (0.001f * tauMs * (float)sr)); }

    // per-family synthesis into scratch
    void renderVoice(Voice& v, int num, float* outL, float* outR)
    {
        const float g = juce::Decibels::decibelsToGain(drumDb) * 0.5f;
        const float gainL = g * v.panL;
        const float gainR = g * v.panR;

        switch (v.family)
        {
        case Family::Kick:
        {
            const float velPow = std::pow(v.vel, 0.7f);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                const float p = 42.0f + 122.0f * std::exp(-t / (0.030f * (float)sr));
                v.phase += (float)(p / sr);
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                const float body = std::sin(v.phase * 6.283185307179586f) * envExp(t, 240.0f);
                const float click = (t < 12.0f) ? (0.45f * (1.0f - t / 12.0f)) : 0.0f;
                const float pun = 0.35f * v.vel * envExp(t, 8.0f) * std::sin(v.phase * 37.69911184307752f);
                const float out = (body * 0.95f + click + pun) * velPow;
                outL[i] = out * gainL;
                outR[i] = out * gainR;
            }
            break;
        }
        case Family::Snare:
        {
            const float velPow = std::pow(v.vel, 0.8f);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                v.phase += 162.0f / (float)sr;
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                const float body = std::sin(v.phase * 6.283185307179586f) * envExp(t, 120.0f);
                v.hpMem = v.hpMem * 0.6f + white() * 0.4f;
                const float nz = v.hpMem * envExp(t, 95.0f);
                const float out = (body * 0.5f + nz * 0.9f) * velPow;
                outL[i] = out * gainL;
                outR[i] = out * gainR;
            }
            break;
        }
        case Family::Cymbal:
        {
            const float decay = v.open ? 320.0f : 65.0f;
            const float toneInc = (1600.0f + 800.0f * v.randomOffset) / (float)sr;
            const float velPow = std::pow(v.vel, 0.8f);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                const float w = white();
                const float hp = w - v.hpMem;
                v.hpMem = w;
                v.phase += toneInc;
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                const float out = hp * envExp(t, decay)
                    + 0.5f * std::sin(v.phase * 6.283185307179586f) * envExp(t, decay * 0.35f);
                outL[i] = out * gainL * velPow;
                outR[i] = out * gainR * velPow;
            }
            break;
        }
        default: break;
        }

        v.pos += (float)num;
        if (v.active)
        {
            const float tail = (v.family == Family::Kick) ? 240.0f
                              : (v.family == Family::Snare) ? 120.0f
                              : (v.open ? 320.0f : 65.0f);
            if (envExp(v.pos, tail) < 0.01f)
                v.active = false;
        }
    }
};

} // namespace agm