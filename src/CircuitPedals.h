#pragma once
// Pedals modelled from their schematics, like the circuit amps. Voltages are AC around each pedal's
// reference (Vref = 4.5 V); one digital unit at the input is `Tuning::inputVolts` volts.
//
// Overdrive (TS9-style, values from the TS808/TS9 schematic as published in the BeanScream clone):
//  - input buffer: 20n into 510k to Vref (16 Hz), 2SC1815 emitter follower, 1uF into 10k (16 Hz);
//  - clipper, non-inverting 4558: 4k7 + 47n from - to Vref (720 Hz), feedback 51k + Drive 500k A
//    paralleled by 51 pF and two antiparallel 1N914 (gain 12..118 in the mids);
//  - tone, second 4558: 1k into node X (10k to Vref, 220n to ground) feeding +; Tone 20k from X to -,
//    1k from out to -, the wiper to ground through 220n + 220 ohm (pivot ~3.3 kHz);
//  - output: Level 100k A (after 1uF + 1k), emitter follower, 100 ohm / 10k (TS9 values).

#include "CircuitAmp.h"

class CircuitTS9
{
public:
    struct Tuning
    {
        // ponytail: datasheet values until the captures calibrate them (phase 3).
        double inputVolts = 1.0;            // volts per digital unit at the input
        double diodeIs = 2.52e-9, diodeN = 1.752; // 1N914
        double rail = 3.5;                  // 4558 swing around Vref on 9 V
    };

    CircuitTS9() = default;
    explicit CircuitTS9(Tuning t) : tune(t) {}

    void prepare(double sampleRate, int maxBlock)
    {
        os.initProcessing((size_t)maxBlock);
        os.reset();
        fs = sampleRate * (double)os.getOversamplingFactor();
        inHp1.highPass(15.6, fs), inHp2.highPass(15.9, fs);
        ground.set(0, 47e-9, 1, 4.7e3 * 47e-9, fs); // current into 4k7 + 47n per volt at the - input
        gCap = 2 * 51e-12 * fs;
        vf = icap = 0;
        knobs.fill(-1);
        tone.reset();
        setKnobs(5, 5, 5);
    }
    int getLatency() const { return (int)std::lround(os.getLatencyInSamples()); }

    // 0..10, as on the pedal.
    void setKnobs(float drive, float toneK, float level)
    {
        const std::array<float, 3> k { drive, toneK, level };
        if (k == knobs)
            return;
        const bool toneMoved = k[1] != knobs[1];
        knobs = k;
        rFeedback = 51e3 + 500e3 * audio(drive);
        const double a = audio(level);
        levelGain = a * 100e3 / (1e3 + 100e3) / tune.inputVolts;
        if (toneMoved)
        {
            // Nodes: 0 X (+ input, and - through the op-amp), 1 wiper, 2 between 220n and 220 ohm.
            const double t = juce::jlimit(1e-4, 1.0 - 1e-4, (double)toneK / 10.0);
            ra = t * 20e3, rb = (1 - t) * 20e3; // X to wiper, wiper to the - input (treble up: wiper to -)
            tone.begin();
            tone.r(0, -1, 1e3), tone.r(0, -1, 10e3), tone.c(0, 0, -1, 220e-9, fs);
            tone.r(0, 1, ra);
            tone.y[1][1] += 1 / rb, tone.y[1][0] -= 1 / rb; // the - input sits at X, fed by the op-amp
            tone.c(1, 1, 2, 220e-9, fs), tone.r(2, -1, 220);
            tone.end();
        }
    }

    void process(float* x, int n)
    {
        juce::dsp::AudioBlock<float> block(&x, 1, (size_t)n);
        auto up = os.processSamplesUp(block);
        float* s = up.getChannelPointer(0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            s[i] = (float)(tick(s[i] * tune.inputVolts) * levelGain);
        os.processSamplesDown(block);
    }

private:
    static double audio(float v) { return juce::jlimit(1e-4, 1.0 - 1e-4, (std::pow(10.0, v / 5.0) - 1.0) / 99.0); }
    double clip(double v) const { return tune.rail * std::tanh(v / tune.rail); }

    double tick(double vin)
    {
        const double v = inHp2(inHp1((float)vin)); // both buffers: unity, coupling high-passes
        // Clipper: the feedback carries the - input's current i; solve i = v/Rf + C dv/dt + 2 Is sinh(v/nVt).
        const double i = ground((float)v), nvt = tune.diodeN * 0.02585, is2 = 2 * tune.diodeIs;
        const double hist = gCap * vf + icap;
        double u = vf;
        for (int it = 0; it < 8; ++it)
        {
            const double e = std::exp(u / nvt), sh = 0.5 * (e - 1 / e), ch = 0.5 * (e + 1 / e);
            const double f = u / rFeedback + gCap * u - hist + is2 * sh - i;
            const double step = juce::jlimit(-0.1, 0.1, f / (1 / rFeedback + gCap + is2 / nvt * ch));
            u -= step;
            if (std::abs(step) < 1e-9)
                break;
        }
        icap = gCap * (u - vf) - icap, vf = u;
        const double clipped = clip(v + u);
        // Tone: the op-amp holds - at X; its output adds 1k times the current it pushes into the pot.
        const auto node = tone.solve({ clipped / 1e3, 0, 0 });
        return clip(node[0] + 1e3 * (node[0] - node[1]) / rb);
    }

    Tuning tune;
    double fs = 192000, rFeedback = 300e3, levelGain = 0.5, ra = 10e3, rb = 10e3, gCap = 0, vf = 0, icap = 0;
    circuit::OnePole inHp1, inHp2, ground;
    circuit::Network<3, 2> tone;
    std::array<float, 3> knobs {};
    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
};
