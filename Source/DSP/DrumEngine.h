#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>
#include <algorithm>

namespace agm {

// DSP-synthesised drum one-shots. No samples, so nothing to license.
//
// The three families follow the topologies the classic analogue drum machines
// used, all of which are documented in public engineering literature:
//
//  KICK / TOM / BONGO - a bridged-T style resonator: one sine whose frequency
//      falls exponentially from a strike pitch to a body pitch, with a short
//      broadband click for the beater and a brief sixth-harmonic "punch". The
//      per-note table below sets the body pitch, how far the strike pitch sits
//      above it, and how long the shell rings, so a floor tom is a different
//      drum from a bass drum rather than the same recipe under another name.
//
//  SNARE / CLAP - two tuned shell modes (a snare drum's first two head modes sit
//      roughly a fifth apart) plus a noise burst band-passed between a high-pass
//      that removes the mud and a low-pass that tames the fizz. The clap is the
//      same machinery with the noise gated into four bursts about 9 ms apart
//      followed by the room tail, which is what makes a clap read as many hands
//      rather than one drum.
//
//  HAT / RIDE / CYMBAL - metal is inharmonic, so a single oscillator cannot
//      sound like it. Six square oscillators are tuned to the mode ratios of a
//      free circular plate (1, 2.08, 3.41, 3.89, 5.00, 6.43 - Kirchhoff plate
//      theory), summed with noise and pushed through a two-pole high-pass. The
//      per-note table sets the cluster's fundamental, the high-pass corner and
//      the decay, so a closed hat, an open hat, a ride and a crash really are
//      different instruments.
//
// Every envelope is a recursive multiplier rather than a per-sample exp(), and
// every voice fades to exact zero over 4 ms once it is 60 dB down, so a voice is
// never cut off mid-tail. One-shots ignore note-off.
class DrumEngine
{
public:
    enum class Family : int { Kick, Snare, Cymbal, kCount };

    void prepare(double sampleRateIn, int blockSize)
    {
        sr = sampleRateIn > 1.0 ? sampleRateIn : 44100.0;
        scratch.setSize(2, juce::jmax(blockSize, 1), false, false, true);
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

    int getActiveVoiceCount() const
    {
        int n = 0;
        for (auto& v : voices) if (v.active) ++n;
        return n;
    }

    void noteOn(int note, float velocity)
    {
        const int n = juce::jlimit(0, 127, note);
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const Spec spec = specForNote(n);
        Voice& v = voiceFor(spec.family);
        v = Voice {};
        v.active = true;
        v.family = spec.family;
        v.note = n;
        v.vel = vel;
        startVoice(v, spec);
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
        renderAdd(b, numChannels, 0, b.getNumSamples());
    }

    // Renders [start, start+num). Allocation-free: sliced to the scratch size
    // fixed in prepare(), so oversized host blocks never grow buffers here.
    void renderAdd(juce::AudioBuffer<float>& b, int numChannels, int start, int num)
    {
        if (!enabled) return;
        numChannels = juce::jmin(numChannels, b.getNumChannels());
        if (num <= 0 || numChannels <= 0 || scratch.getNumSamples() <= 0) return;
        const int slice = scratch.getNumSamples();

        for (int pos = 0; pos < num; pos += slice)
        {
            const int n = juce::jmin(slice, num - pos);
            float* const outL = scratch.getWritePointer(0);
            float* const outR = scratch.getWritePointer(1);
            for (auto& v : voices)
            {
                if (!v.active) continue;
                renderVoice(v, n, outL, outR);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* dst = b.getWritePointer(ch) + start + pos;
                    const float* src = (ch & 1) ? outR : outL;
                    for (int i = 0; i < n; ++i)
                        dst[i] += src[i];
                }
            }
        }
    }

private:
    static constexpr int kPartials = 6;

    // Mode ratios of a free circular plate (Kirchhoff): mutually irrational, so
    // the cluster beats instead of forming a chord.
    static constexpr float kPlateRatios[kPartials] = { 1.0f, 2.08f, 3.41f, 3.89f, 5.00f, 6.43f };

    // Everything a note number decides, resolved once at note-on.
    struct Spec
    {
        Family family = Family::Kick;

        // kick / tom / bongo
        float strikeHz = 164.0f;    // pitch at t = 0
        float bodyHz = 42.0f;       // pitch the sweep settles on
        float pitchTauMs = 30.0f;
        float ampTauMs = 240.0f;
        float clickAmt = 0.45f;
        float clickMs = 0.27f;
        float punchAmt = 0.35f;

        // snare / clap
        float mode1Hz = 185.0f;
        float mode2Hz = 330.0f;
        float bodyTauMs = 90.0f;
        float bodyAmt = 0.50f;
        float noiseTauMs = 150.0f;
        float noiseHpHz = 300.0f;
        float noiseLpHz = 9000.0f;
        float noiseAmt = 0.90f;
        int bursts = 1;             // > 1 = clap
        float burstGapMs = 9.0f;

        // hat / ride / cymbal
        float clusterHz = 420.0f;
        float clusterHpHz = 7000.0f;
        float cymTauMs = 55.0f;
        float cymToneAmt = 0.55f;
        float cymNoiseAmt = 0.75f;

        float spread = 0.0f;        // -1 = hard left, +1 = hard right
        // Voice level. The high-pass on the metal cluster throws away most of its
        // energy, so the cymbals need real make-up to sit with the kick; these
        // numbers hold every voice's peak within a few dB of the old build's
        // kick (-4.3 dBFS at velocity 1 with Drum Level at 0 dB).
        float gain = 1.0f;
    };

    struct Voice
    {
        Family family = Family::Kick;
        bool active = false;
        int note = 0;
        float vel = 1.0f;
        float gainL = 1.0f, gainR = 1.0f;

        float pos = 0.0f;           // samples since note-on
        float phase = 0.0f;

        // pitch sweep
        float bodyHz = 42.0f, sweepHz = 0.0f, sweepMul = 1.0f;

        // amplitude envelopes (recursive multipliers)
        float amp = 1.0f, ampMul = 1.0f;
        float body = 1.0f, bodyMul = 1.0f;
        float noise = 1.0f, noiseMul = 1.0f;
        float punch = 1.0f, punchMul = 1.0f;

        // filters
        float hpA = 0.0f, hpB = 0.0f, hpCoef = 0.0f;
        float lp = 0.0f, lpCoef = 1.0f;

        // snare / clap
        float mode1Inc = 0.0f, mode2Inc = 0.0f, mode1Ph = 0.0f, mode2Ph = 0.0f;
        int bursts = 1;
        float burstGapSamples = 0.0f;

        // metal cluster
        std::array<float, kPartials> pPhase {};
        std::array<float, kPartials> pInc {};

        float clickAmt = 0.0f, clickSamples = 1.0f, punchAmt = 0.0f;
        float bodyAmt = 0.0f, noiseAmt = 0.0f, toneAmt = 0.0f, nzAmt = 0.0f;
        float gain = 1.0f;

        // tail: fade to exact zero rather than being cut off
        float fadeStart = 0.0f, fadeStep = 0.0f, fade = 1.0f;
    };

    static constexpr int kPools = 10 + 6 + 16;
    std::array<Voice, kPools> voices {};
    uint32_t rotate = 0;
    double sr = 44100.0;
    uint32_t rngState = 0x9E3779B9u;
    bool enabled = true;
    float drumDb = -16.0f;
    juce::AudioBuffer<float> scratch;

    Family familyForNote(int n) const
    {
        switch (n)
        {
            case 35: case 36:                                       // bass drums
            case 41: case 43: case 45: case 47: case 48: case 50:   // toms
            case 60: case 61:                                       // bongos
                return Family::Kick;
            case 37: case 38: case 39: case 40:                     // side stick, snares, clap
                return Family::Snare;
            default:
                return Family::Cymbal;   // hats 42/44/46, crashes 49/57, rides, percussion
        }
    }

    // The per-note table. Pitches are the body/cluster fundamentals; the GM
    // instrument each note stands for is in the comment.
    Spec specForNote(int n) const
    {
        Spec s;
        s.family = familyForNote(n);

        if (s.family == Family::Kick)
        {
            switch (n)
            {
            case 35:  s.bodyHz = 36.0f;  s.strikeHz = 140.0f; s.pitchTauMs = 38.0f; s.ampTauMs = 300.0f; break;  // acoustic bass drum
            case 36:  s.bodyHz = 42.0f;  s.strikeHz = 164.0f; s.pitchTauMs = 30.0f; s.ampTauMs = 240.0f; break;  // bass drum 1
            case 41:  s.bodyHz = 74.0f;  s.strikeHz = 122.0f; s.pitchTauMs = 16.0f; s.ampTauMs = 420.0f; break;  // low floor tom
            case 43:  s.bodyHz = 88.0f;  s.strikeHz = 145.0f; s.pitchTauMs = 15.0f; s.ampTauMs = 400.0f; break;  // high floor tom
            case 45:  s.bodyHz = 105.0f; s.strikeHz = 172.0f; s.pitchTauMs = 14.0f; s.ampTauMs = 370.0f; break;  // low tom
            case 47:  s.bodyHz = 125.0f; s.strikeHz = 205.0f; s.pitchTauMs = 13.0f; s.ampTauMs = 340.0f; break;  // low-mid tom
            case 48:  s.bodyHz = 150.0f; s.strikeHz = 245.0f; s.pitchTauMs = 12.0f; s.ampTauMs = 310.0f; break;  // hi-mid tom
            case 50:  s.bodyHz = 178.0f; s.strikeHz = 290.0f; s.pitchTauMs = 11.0f; s.ampTauMs = 280.0f; break;  // high tom
            case 60:  s.bodyHz = 310.0f; s.strikeHz = 400.0f; s.pitchTauMs = 5.0f;  s.ampTauMs = 110.0f; break;  // hi bongo
            default:  s.bodyHz = 225.0f; s.strikeHz = 290.0f; s.pitchTauMs = 6.0f;  s.ampTauMs = 140.0f; break;  // low bongo (61)
            }
            // A bass drum is all beater; a tom is mostly shell; a bongo is a slap.
            const bool bass = (n == 35 || n == 36);
            const bool bongo = (n == 60 || n == 61);
            s.clickAmt = bass ? 0.45f : (bongo ? 0.30f : 0.18f);
            s.punchAmt = bass ? 0.35f : (bongo ? 0.10f : 0.16f);
            s.clickMs = bass ? 0.27f : 0.20f;
            s.spread = bongo ? ((n == 60) ? 0.35f : -0.25f)
                             : (n >= 41 && n <= 50 ? (float)(n - 45) * 0.06f : 0.0f);
            return s;
        }

        if (s.family == Family::Snare)
        {
            switch (n)
            {
            case 37:    // side stick: a rim click, almost no snare buzz
                s.mode1Hz = 780.0f; s.mode2Hz = 1650.0f; s.bodyTauMs = 22.0f; s.bodyAmt = 0.85f;
                s.noiseTauMs = 18.0f; s.noiseHpHz = 900.0f; s.noiseLpHz = 11000.0f; s.noiseAmt = 0.45f;
                s.gain = 0.8f;
                break;
            case 39:    // hand clap: four bursts about 9 ms apart, then the room
                s.mode1Hz = 900.0f; s.mode2Hz = 1400.0f; s.bodyTauMs = 12.0f; s.bodyAmt = 0.06f;
                s.noiseTauMs = 210.0f; s.noiseHpHz = 700.0f; s.noiseLpHz = 7000.0f; s.noiseAmt = 1.0f;
                s.bursts = 4; s.burstGapMs = 9.0f; s.gain = 0.75f;
                break;
            case 40:    // electric snare: tighter and higher
                s.mode1Hz = 250.0f; s.mode2Hz = 420.0f; s.bodyTauMs = 60.0f; s.bodyAmt = 0.45f;
                s.noiseTauMs = 95.0f; s.noiseHpHz = 400.0f; s.noiseLpHz = 11000.0f; s.noiseAmt = 0.95f;
                s.gain = 0.80f;
                break;
            default:    // 38 acoustic snare: two head modes roughly a fifth apart
                s.gain = 0.72f;
                break;
            }
            return s;
        }

        switch (n)
        {
        case 42:  s.clusterHz = 420.0f; s.clusterHpHz = 7200.0f; s.cymTauMs = 52.0f;   s.cymToneAmt = 0.55f; s.cymNoiseAmt = 0.70f; s.spread = 0.18f;  s.gain = 5.20f; break;   // closed hat
        case 44:  s.clusterHz = 400.0f; s.clusterHpHz = 6500.0f; s.cymTauMs = 85.0f;   s.cymToneAmt = 0.50f; s.cymNoiseAmt = 0.75f; s.spread = 0.18f;  s.gain = 4.80f; break;   // pedal hat
        case 46:  s.clusterHz = 420.0f; s.clusterHpHz = 6800.0f; s.cymTauMs = 330.0f;  s.cymToneAmt = 0.55f; s.cymNoiseAmt = 0.80f; s.spread = 0.18f;  s.gain = 4.00f; break;   // open hat
        case 49:  s.clusterHz = 300.0f; s.clusterHpHz = 3400.0f; s.cymTauMs = 900.0f;  s.cymToneAmt = 0.60f; s.cymNoiseAmt = 0.95f; s.spread = -0.45f; s.gain = 1.05f; break;   // crash 1
        case 57:  s.clusterHz = 265.0f; s.clusterHpHz = 3100.0f; s.cymTauMs = 1050.0f; s.cymToneAmt = 0.60f; s.cymNoiseAmt = 0.95f; s.spread = 0.45f;  s.gain = 1.00f; break;   // crash 2
        case 51:  s.clusterHz = 500.0f; s.clusterHpHz = 5200.0f; s.cymTauMs = 620.0f;  s.cymToneAmt = 0.85f; s.cymNoiseAmt = 0.40f; s.spread = 0.30f;  s.gain = 3.30f; break;   // ride 1
        case 59:  s.clusterHz = 545.0f; s.clusterHpHz = 5000.0f; s.cymTauMs = 560.0f;  s.cymToneAmt = 0.85f; s.cymNoiseAmt = 0.40f; s.spread = 0.30f;  s.gain = 3.20f; break;   // ride 2
        case 53:  s.clusterHz = 820.0f; s.clusterHpHz = 2600.0f; s.cymTauMs = 480.0f;  s.cymToneAmt = 1.00f; s.cymNoiseAmt = 0.18f; s.spread = 0.30f;  s.gain = 1.60f; break;   // ride bell
        case 52:  s.clusterHz = 240.0f; s.clusterHpHz = 2400.0f; s.cymTauMs = 800.0f;  s.cymToneAmt = 0.70f; s.cymNoiseAmt = 0.85f; s.spread = -0.40f; s.gain = 0.90f; break;  // chinese cymbal
        case 55:  s.clusterHz = 380.0f; s.clusterHpHz = 5000.0f; s.cymTauMs = 300.0f;  s.cymToneAmt = 0.60f; s.cymNoiseAmt = 0.85f; s.spread = -0.30f; s.gain = 2.20f; break;  // splash
        default:  // the rest of the GM percussion: short, bright, lightly panned by note
            s.clusterHz = 520.0f + (float)((n * 37) % 260);
            s.clusterHpHz = 4200.0f;
            s.cymTauMs = 120.0f;
            s.cymToneAmt = 0.65f;
            s.cymNoiseAmt = 0.60f;
            s.spread = (float)(((n * 11) % 9) - 4) * 0.05f;
            s.gain = 2.00f;
            break;
        }
        return s;
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
            if (!voices[(size_t)idx].active) return voices[(size_t)idx];
        }
        return voices[(size_t)(begin + base)];
    }

    float white()
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(rngState >> 8) / 8388608.0f - 1.0f;
    }

    // exp(-1 / tauMs) as a per-sample multiplier
    float decayMul(float tauMs) const
    {
        const float t = tauMs < 0.05f ? 0.05f : tauMs;
        return std::exp(-1.0f / (0.001f * t * (float)sr));
    }

    // one-pole coefficient for a corner at fc
    float poleCoef(float fc) const
    {
        const float nyq = 0.49f * (float)sr;
        const float f = juce::jlimit(5.0f, nyq, fc);
        return 1.0f - std::exp(-6.2831853f * f / (float)sr);
    }

    void startVoice(Voice& v, const Spec& spec)
    {
        const float vel = v.vel;
        v.gain = spec.gain;
        // constant-power spread so panning never changes the perceived level
        const float pan = juce::jlimit(-1.0f, 1.0f, spec.spread);
        v.gainL = std::cos((pan + 1.0f) * 0.7853981634f) * 1.41421356f;
        v.gainR = std::sin((pan + 1.0f) * 0.7853981634f) * 1.41421356f;

        v.bodyHz = spec.bodyHz;
        // a harder strike starts higher: the head is tighter under the beater
        v.sweepHz = (spec.strikeHz - spec.bodyHz) * (0.80f + 0.20f * vel);
        v.sweepMul = decayMul(spec.pitchTauMs);

        v.ampMul = decayMul(spec.ampTauMs);
        v.clickAmt = spec.clickAmt;
        v.clickSamples = juce::jmax(1.0f, spec.clickMs * 0.001f * (float)sr);
        v.punchAmt = spec.punchAmt;
        v.punchMul = decayMul(8.0f);

        // Softer hits are darker and shorter - that is how a struck drum behaves,
        // and it is what makes a velocity ramp read as dynamics rather than volume.
        const float velBright = 0.55f + 0.45f * vel;
        const float velLength = 0.70f + 0.30f * vel;

        v.mode1Inc = spec.mode1Hz / (float)sr;
        v.mode2Inc = spec.mode2Hz / (float)sr;
        v.bodyMul = decayMul(spec.bodyTauMs * velLength);
        v.bodyAmt = spec.bodyAmt;
        v.noiseMul = decayMul(spec.noiseTauMs * velLength);
        v.noiseAmt = spec.noiseAmt;
        v.bursts = spec.bursts;
        v.burstGapSamples = spec.burstGapMs * 0.001f * (float)sr;

        float tailMs = spec.ampTauMs;
        if (v.family == Family::Snare)
        {
            v.hpCoef = poleCoef(spec.noiseHpHz);
            v.lpCoef = poleCoef(spec.noiseLpHz * velBright);
            tailMs = juce::jmax(spec.bodyTauMs, spec.noiseTauMs) * velLength;
        }
        else if (v.family == Family::Cymbal)
        {
            for (int k = 0; k < kPartials; ++k)
            {
                // a few cents of scatter per hit so repeated hats are not clones
                const float jitter = 1.0f + 0.01f * white();
                v.pInc[(size_t)k] = spec.clusterHz * kPlateRatios[k] * jitter / (float)sr;
                v.pPhase[(size_t)k] = 0.5f * (white() + 1.0f);
            }
            v.hpCoef = poleCoef(spec.clusterHpHz * (0.75f + 0.25f * vel));
            v.toneAmt = spec.cymToneAmt * (0.75f + 0.25f * vel);
            v.nzAmt = spec.cymNoiseAmt;
            v.noiseMul = decayMul(spec.cymTauMs * velLength);
            v.bodyMul = decayMul(spec.cymTauMs * 0.35f * velLength);
            tailMs = spec.cymTauMs * velLength;
        }

        // Fade to exact zero over 4 ms starting where the tail is 60 dB down, or
        // at 3.5 s, whichever comes first. A voice is never truncated mid-tail.
        const float minus60 = tailMs * 6.908f * 0.001f * (float)sr;   // ln(1000) time constants
        v.fadeStart = juce::jmin(minus60, 3.5f * (float)sr);
        v.fadeStep = 1.0f / juce::jmax(1.0f, 0.004f * (float)sr);
        v.fade = 1.0f;
    }

    void renderVoice(Voice& v, int num, float* outL, float* outR)
    {
        const float g = juce::Decibels::decibelsToGain(drumDb) * 0.5f * v.gain;
        const float gainL = g * v.gainL;
        const float gainR = g * v.gainR;
        constexpr float twoPi = 6.283185307179586f;

        switch (v.family)
        {
        case Family::Kick:
        {
            const float velPow = std::pow(v.vel, 0.7f);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                const float p = v.bodyHz + v.sweepHz;
                v.sweepHz *= v.sweepMul;
                v.phase += p / (float)sr;
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                const float body = std::sin(v.phase * twoPi) * v.amp;
                const float click = (t < v.clickSamples) ? (v.clickAmt * (1.0f - t / v.clickSamples)) : 0.0f;
                const float pun = v.punchAmt * v.vel * v.punch * std::sin(v.phase * twoPi * 6.0f);
                const float out = (body * 0.95f + click + pun) * velPow * tailGain(v, t);
                v.amp *= v.ampMul;
                v.punch *= v.punchMul;
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
                v.mode1Ph += v.mode1Inc;
                if (v.mode1Ph >= 1.0f) v.mode1Ph -= 1.0f;
                v.mode2Ph += v.mode2Inc;
                if (v.mode2Ph >= 1.0f) v.mode2Ph -= 1.0f;
                const float body = (0.62f * std::sin(v.mode1Ph * twoPi)
                                  + 0.38f * std::sin(v.mode2Ph * twoPi)) * v.body;

                // band-passed noise: one-pole high-pass then one-pole low-pass
                const float w = white();
                v.hpA += v.hpCoef * (w - v.hpA);
                const float hp = w - v.hpA;
                v.lp += v.lpCoef * (hp - v.lp);
                const float nz = v.lp * v.noise * burstGate(v, t);

                const float out = (body * v.bodyAmt + nz * v.noiseAmt * 1.6f) * velPow * tailGain(v, t);
                v.body *= v.bodyMul;
                v.noise *= v.noiseMul;
                outL[i] = out * gainL;
                outR[i] = out * gainR;
            }
            break;
        }
        case Family::Cymbal:
        {
            const float velPow = std::pow(v.vel, 0.8f);
            constexpr float inv = 1.0f / (float)kPartials;
            for (int i = 0; i < num; ++i)
            {
                const float t = v.pos + (float)i;
                float cluster = 0.0f;
                for (int k = 0; k < kPartials; ++k)
                {
                    float& ph = v.pPhase[(size_t)k];
                    ph += v.pInc[(size_t)k];
                    if (ph >= 1.0f) ph -= 1.0f;
                    cluster += (ph < 0.5f) ? 1.0f : -1.0f;
                }
                cluster *= inv;
                const float x = v.toneAmt * cluster * v.body + v.nzAmt * white() * v.noise;

                // two-pole high-pass: metal has no low end
                v.hpA += v.hpCoef * (x - v.hpA);
                const float h1 = x - v.hpA;
                v.hpB += v.hpCoef * (h1 - v.hpB);
                const float h2 = h1 - v.hpB;

                const float out = h2 * velPow * tailGain(v, t);
                v.noise *= v.noiseMul;
                v.body *= v.bodyMul;
                outL[i] = out * gainL;
                outR[i] = out * gainR;
            }
            break;
        }
        default: break;
        }

        v.pos += (float)num;
        if (v.fade <= 0.0f)
            v.active = false;
    }

    // Linear ramp to exact zero once the tail is inaudible.
    float tailGain(Voice& v, float t)
    {
        if (t < v.fadeStart)
            return 1.0f;
        v.fade -= v.fadeStep;
        if (v.fade < 0.0f)
            v.fade = 0.0f;
        return v.fade;
    }

    // 1 for a single hit; for a clap, four decaying slaps then the room tail.
    float burstGate(const Voice& v, float t) const
    {
        if (v.bursts <= 1 || v.burstGapSamples <= 1.0f)
            return 1.0f;
        const float span = (float)v.bursts * v.burstGapSamples;
        if (t >= span)
            return 1.0f;
        const float within = std::fmod(t, v.burstGapSamples) / v.burstGapSamples;
        return std::exp(-within * 5.0f);
    }
};

} // namespace agm
