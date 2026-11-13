#pragma once
// Pedals modelled from their schematics, like the circuit amps. Voltages are AC around each pedal's
// reference (Vref = 4.5 V). Digital full scale is `Tuning::inputVolts` volts peak at the input and
// `outputVolts` at the output: the reamp calibration of the overdrive captures (+16 dBu in, +18 dBu out).
//
// Overdrive (the classic mid-hump op-amp overdrive; values from a published schematic of that circuit):
//  - input buffer: 20n into 510k to Vref (16 Hz), 2SC1815 emitter follower, 1uF into 10k (16 Hz);
//  - clipper, non-inverting 4558: 4k7 + 47n from - to Vref (720 Hz), feedback 51k + Drive 500k A
//    paralleled by 51 pF and two antiparallel 1N914 (gain 12..118 in the mids);
//  - tone, second 4558: 1k into node X (10k to Vref, 220n to ground) feeding +; Tone 20k from X to -,
//    1k from out to -, the wiper to ground through 220n + 220 ohm (pivot ~3.3 kHz);
//  - output: Level 100k A (after 1uF + 1k), emitter follower, 100 ohm / 10k.

#include "CircuitAmp.h"

class CircuitOverdrive
{
public:
    struct Tuning
    {
        // Fitted to the nine captures (grid search, MJR_CALIBRATE=1 in the tests); the pots behave more
        // linearly than their log markings, and the Tone knob reaches the highs sooner than a linear sweep.
        // ponytail: the captures' output sits ~6 dB above the metadata's +18 dBu; folded into outputVolts.
        double inputVolts = 6.91, outputVolts = 4.32; // full scale, peak: +16 dBu in
        double diodeIs = 2.52e-9, diodeN = 1.752;      // 1N914
        double rail = 3.5;                             // 4558 swing around Vref on 9 V
        double driveTaper = 0.4, levelTaper = 1;       // 0 = ideal log (10% at half), 1 = linear
        double toneCurve = 0.7;                        // exponent on the Tone knob's travel
    };

    CircuitOverdrive() = default;
    explicit CircuitOverdrive(Tuning t) : tune(t) {}

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
        rFeedback = 51e3 + 500e3 * taper(drive, tune.driveTaper);
        const double a = taper(level, tune.levelTaper);
        levelGain = a * 100e3 / (1e3 + 100e3) / tune.outputVolts;
        if (toneMoved)
        {
            // Nodes: 0 X (+ input, and - through the op-amp), 1 wiper, 2 between 220n and 220 ohm.
            const double t = juce::jlimit(1e-4, 1.0 - 1e-4, std::pow((double)toneK / 10.0, tune.toneCurve));
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
    static double taper(float v, double linear)
    {
        const double log = (std::pow(10.0, v / 5.0) - 1.0) / 99.0;
        return juce::jlimit(1e-4, 1.0 - 1e-4, log + linear * (v / 10.0 - log));
    }
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

// Boost (a clean boost with a bass cut, values from the schematic the user supplied):
//  - Q1 (collector at IN, base to ground through 1M and D8/D9): off at signal level; it clamps
//    negative peaks below about -1.2 V through the source's resistance;
//  - IN -> R3 4k7 -> C1 47n -> Q2 base (R4 1M to VR). Q2 + TL071 act as a nullor: the op-amp holds
//    Q2's emitter at its base. Feedback: R6 220k from the op-amp to the emitter, C4 1u from the
//    emitter to the bus; bus: R7 820 + R8 1k8 + C5 100n + R9 220 to ground, R10 3k9 to X (C6 470n,
//    R12 20k + R11 1k to ground, R13 120 + R14 47 paralleled by C7 47n to Y, R15 2k2 to OUT),
//    R17 4k7 + R16 12k + C8 10n to OUT. Op-amp -> C3 4u7 -> OUT, Level A5k from OUT to ground.
//  - Supply: VC from the charge pump, taken as 18 V; VR at half of it (the drawn divider gives an
//    impossible operating point, see the notes). Swing about 7.5 V either side of VR.
// Small signal at full Level: +22 dB around 1 kHz, +3.7 dB at 100 Hz; Level only attenuates.
class CircuitBoost
{
public:
    struct Tuning
    {
        // ponytail: datasheet values until the captures calibrate them (phase 3).
        // The captures lost their absolute level, so these are chosen, not measured: a guitar DI (peak ~0.5)
        // stays under the rails (0.5 * 2 V * 12.6 = 12.6 V swings only on hard picking), and full Level
        // gives +20 dB digital, the anchor the boost has always had (22 dB circuit gain - 2 dB).
        double inputVolts = 2.0, outputVolts = 2.52;
        double rail = 7.5;         // TL071 swing around VR with VC = 18 V
        double sourceOhms = 10e3;  // the guitar's pickup, resistance only: what Q1 clamps against
    };

    CircuitBoost() = default;
    explicit CircuitBoost(Tuning t) : tune(t) {}

    void prepare(double sampleRate, int maxBlock)
    {
        os.initProcessing((size_t)maxBlock);
        os.reset();
        const double fs = sampleRate * (double)os.getOversamplingFactor();
        enum { a, b, e, o, bus, n1, n2, n3, X, n4, n5, Y, n6, n7, OUT, iop };
        net.begin();
        net.r(a, -1, 4.7e3), net.c(0, a, b, 47e-9, fs), net.r(b, -1, 1e6);
        net.r(o, e, 220e3), net.c(1, e, bus, 1e-6, fs);
        net.r(bus, n1, 820), net.r(n1, n2, 1.8e3), net.c(2, n2, n3, 100e-9, fs), net.r(n3, -1, 220);
        net.r(bus, X, 3.9e3), net.c(3, X, -1, 470e-9, fs), net.r(X, n4, 20e3), net.r(n4, -1, 1e3);
        net.r(X, n5, 120), net.r(n5, Y, 47), net.c(4, Y, X, 47e-9, fs), net.r(Y, OUT, 2.2e3);
        net.r(bus, n6, 4.7e3), net.r(n6, n7, 12e3), net.c(5, n7, OUT, 10e-9, fs);
        net.c(6, o, OUT, 4.7e-6, fs), net.r(OUT, -1, 5e3);
        net.y[o][iop] = -1; // the op-amp's output current
        auto saturated = net.y;
        net.y[iop][e] = 1, net.y[iop][b] = -1; // linear: emitter follows base
        net.end();
        saturated[iop][o] = 1;                // saturated: the output is a voltage source
        invSaturated = circuit::invert(saturated);
        net.reset();
        vIn = 0;
        setKnobs(level);
    }
    int getLatency() const { return (int)std::lround(os.getLatencyInSamples()); }

    // 0..10, as on the pedal: the one knob is Level.
    void setKnobs(float levelK, float = 0, float = 0)
    {
        level = levelK;
        gain = juce::jlimit(1e-4, 1.0 - 1e-4, (std::pow(10.0, levelK / 5.0) - 1.0) / 99.0) / tune.outputVolts;
    }

    void process(float* x, int n)
    {
        juce::dsp::AudioBlock<float> block(&x, 1, (size_t)n);
        auto up = os.processSamplesUp(block);
        float* s = up.getChannelPointer(0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            s[i] = (float)(tick(s[i] * tune.inputVolts) * gain);
        os.processSamplesDown(block);
    }

private:
    static constexpr int nodes = 16, opOut = 3, opCurrent = 15, out = 14;

    double tick(double vs)
    {
        // Q1: its base-collector junction and D8/D9 in series, one junction with n = 2.75 fitted at 1 mA
        // (1.24 V); the input sees it through the source resistance.
        constexpr double is = 2.8e-11, m = 2.752 * 0.02585;
        for (int it = 0; it < 8; ++it)
        {
            const double ex = std::exp(std::min(-vIn / m, 40.0));
            const double f = vIn - vs - tune.sourceOhms * is * (ex - 1), df = 1 + tune.sourceOhms * is / m * ex;
            const double step = juce::jlimit(-0.2, 0.2, f / df);
            vIn -= step;
            if (std::abs(step) < 1e-9)
                break;
        }
        std::array<double, nodes> in {};
        in[0] = vIn / 4.7e3;
        auto rhs = net.withHistory(in);
        auto v = net.apply(net.inv, rhs);
        const double want = v[opOut], got = soft(want);
        if (got != want) // past the knee: the op-amp's output stops following
        {
            rhs[opCurrent] = got;
            v = net.apply(invSaturated, rhs);
        }
        net.update(v);
        return v[out];
    }
    double soft(double x) const
    {
        const double knee = 0.8 * tune.rail, ax = std::abs(x);
        if (ax <= knee)
            return x;
        const double top = tune.rail - knee;
        return std::copysign(knee + top * std::tanh((ax - knee) / top), x);
    }

    Tuning tune;
    float level = 5;
    double gain = 0.1, vIn = 0;
    circuit::Network<nodes, 7> net;
    std::array<std::array<double, nodes>, nodes> invSaturated {};
    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
};
