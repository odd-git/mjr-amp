#pragma once
// Circuit model of the amp the captures come from: the 1968 200 W lead head (2x ECC83, ECC82 driver,
// 4x KT88 ultra-linear, solid-state doubler). Values from the public service drawing (Unicord 70-02-12,
// July 1970, "1967"):
//   V1  ECC83, plates 100k. Normal side: cathode 820R/250uF, coupling .022. Bright side (channel I):
//       cathode 2.7k/.68uF, coupling .0022, 500pF across its 470k mixing resistor. Volumes 1M (audio taper),
//       mixed into V2a by two 470k.
//   V2a ECC83, plate 100k, cathode 470R (25uF bypass) + 470R unbypassed. Global feedback from the 16R tap
//       through 47k into that cathode; Presence = 5k pot + .68uF from the cathode node to ground.
//   Tone stack driven straight from V2a's plate (no cathode follower): 500pF, Treble 250k, slope 33k,
//       .022/.022, Bass 1M, Middle 25k; loaded by .022 + 1M into V2b.
//   V2b ECC83, plate 100k, cathode 2.7k unbypassed -> .047 -> ECC82 driver -> 4x KT88 UL -> output transformer.
// Blocks: triode stages are Koren-model load-line tables with grid conduction and bias shift; the tone
// stack is an exact nodal (MNA) solution of its network, trapezoidal per sample; the feedback loop runs
// one oversampled sample late. The nonlinear chain runs 4x oversampled.

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace circuit
{
// Koren's triode: plate current (A) from plate-cathode and grid-cathode voltages.
struct Triode
{
    double mu, ex, kg1, kp, kvb;
    double ip(double vpk, double vgk) const
    {
        if (vpk <= 0)
            return 0;
        const double e1 = vpk / kp * std::log1p(std::exp(kp * (1.0 / mu + vgk / std::sqrt(kvb + vpk * vpk))));
        return e1 > 0 ? std::pow(e1, ex) / kg1 : 0.0;
    }
};
inline constexpr Triode ecc83 { 100.0, 1.4, 1060.0, 600.0, 300.0 };

// First-order filter by bilinear transform: H(s) = (b0 + b1 s/w) / (1 + s/w') in shelf/pass forms.
struct OnePole
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    // H(s) = (n0 + n1 s) / (d0 + d1 s)
    void set(double n0, double n1, double d0, double d1, double fs)
    {
        const double k = 2.0 * fs, a0 = d0 + d1 * k;
        b0 = (float)((n0 + n1 * k) / a0);
        b1 = (float)((n0 - n1 * k) / a0);
        a1 = (float)((d0 - d1 * k) / a0);
    }
    void highPass(double hz, double fs) { const double t = 1.0 / (2 * juce::MathConstants<double>::pi * hz); set(0, t, 1, t, fs); }
    void lowPass(double hz, double fs) { const double t = 1.0 / (2 * juce::MathConstants<double>::pi * hz); set(1, 0, 1, t, fs); }
    float operator()(float x)
    {
        const float y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
};

// One common-cathode triode stage: plate swing (V, AC) for a grid voltage (V, AC).
// The DC load line is solved once into a table; bypassed cathode resistance only sets the bias,
// unbypassed resistance degenerates every sample. Grid conduction clamps positive grid swings and
// charges the coupling cap, shifting the bias down: the "bloom" of a pushed stage.
class TriodeStage
{
public:
    void build(const Triode& t, double bPlus, double rPlate, double rkBypassed, double rkUnbypassed, double fs,
               double tauCharge, double tauRecover, double clamp)
    {
        tube = t, b = bPlus, rl = rPlate, rkb = rkBypassed, rku = rkUnbypassed;
        const double iq = solve(0.0, true);
        vkBias = iq * rkb;
        vpq = b - iq * rl;
        gridLimit = (float)(iq * (rkb + rku)); // grid at the cathode's potential: conduction starts
        for (size_t i = 0; i < table.size(); ++i)
        {
            const double vg = lo + (hi - lo) * (double)i / (double)(table.size() - 1);
            table[i] = (float)(b - solve(vg, false) * rl - vpq);
        }
        charge = (float)(1.0 / (tauCharge * fs));
        recover = (float)std::exp(-1.0 / (tauRecover * fs));
        clampK = (float)clamp;
        shift = 0;
    }
    float process(float vin)
    {
        float x = vin + shift;
        if (const float over = x - gridLimit; over > 0)
        {
            shift -= over * charge;
            x = gridLimit + over / (1.0f + over * clampK);
        }
        shift *= recover;
        const float pos = (x - (float)lo) * (float)(table.size() - 1) / (float)(hi - lo);
        if (pos <= 0)
            return table.front();
        if (pos >= (float)(table.size() - 1))
            return table.back();
        const auto i = (size_t)pos;
        const float f = pos - (float)i;
        return table[i] + f * (table[i + 1] - table[i]);
    }
    void reset() { shift = 0; }
    // Small-signal gain around the bias point (for tests and calibration).
    float smallSignalGain() const
    {
        const float d = 0.01f;
        auto at = [&](float v) { const float pos = (v - (float)lo) * (float)(table.size() - 1) / (float)(hi - lo); const auto i = (size_t)pos; return table[i] + (pos - (float)i) * (table[i + 1] - table[i]); };
        return (at(d) - at(-d)) / (2 * d);
    }

private:
    // Plate current for a grid voltage vg (relative to ground). quiescent: whole Rk degenerates (DC).
    double solve(double vg, bool quiescent) const
    {
        double a = 0, c = b / rl;
        for (int it = 0; it < 60; ++it)
        {
            const double ip = 0.5 * (a + c);
            const double vk = quiescent ? ip * (rkb + rku) : vkBias + ip * rku;
            const double g = tube.ip(b - ip * rl - vk, vg - vk) - ip;
            (g > 0 ? a : c) = ip;
        }
        return 0.5 * (a + c);
    }

    static constexpr double lo = -60.0, hi = 6.0;
    Triode tube = ecc83;
    double b = 300, rl = 100e3, rkb = 0, rku = 0, vkBias = 0, vpq = 0;
    std::array<float, 4096> table {};
    float gridLimit = 0, charge = 0, recover = 1, clampK = 1, shift = 0;
};

// The tone stack's network solved exactly: nodal analysis with trapezoidal capacitors.
// Nodes: 0 plate (after the stage's output resistance), 1 treble pot top, 2 treble wiper (output),
// 3 treble pot bottom / bass top, 4 slope node, 5 bass bottom / middle top.
class ToneStack
{
public:
    void setRate(double fs) { g1 = 2 * c1 * fs, g2 = 2 * c2 * fs, g3 = 2 * c3 * fs, dirty = true; }
    void setKnobs(float treble, float bass, float middle) // 0..1
    {
        const double t = juce::jlimit(0.01, 0.99, (double)treble);
        const double l = juce::jlimit(0.0005, 1.0, (std::pow(10.0, 2.0 * bass) - 1.0) / 99.0); // audio taper
        const double m = juce::jlimit(0.002, 1.0, (double)middle);
        if (t == kt && l == kl && m == km && !dirty)
            return;
        kt = t, kl = l, km = m, dirty = false;
        std::array<std::array<double, 6>, 6> y {};
        auto conduct = [&](int p, int q, double g)
        {
            y[(size_t)p][(size_t)p] += g;
            if (q >= 0)
                y[(size_t)q][(size_t)q] += g, y[(size_t)p][(size_t)q] -= g, y[(size_t)q][(size_t)p] -= g;
        };
        conduct(0, -1, 1.0 / rSource);
        conduct(0, 1, g1);
        conduct(1, 2, 1.0 / ((1 - t) * rTreble));
        conduct(2, 3, 1.0 / (t * rTreble));
        conduct(2, -1, 1.0 / rLoad);
        conduct(0, 4, 1.0 / rSlope);
        conduct(4, 3, g2);
        conduct(4, 5, g3);
        conduct(3, 5, 1.0 / (l * rBass));
        conduct(5, -1, 1.0 / (m * rMid));
        inv = invert(y);
    }
    float process(float vin)
    {
        // Capacitor history currents: J = G * v(n-1) + i(n-1), injected p <- q.
        std::array<double, 6> rhs {};
        rhs[0] = vin / rSource;
        auto inject = [&](int p, int q, double j) { rhs[(size_t)p] += j, rhs[(size_t)q] -= j; };
        inject(0, 1, j1);
        inject(4, 3, j2);
        inject(4, 5, j3);
        std::array<double, 6> v {};
        for (size_t r = 0; r < 6; ++r)
            for (size_t c = 0; c < 6; ++c)
                v[r] += inv[r][c] * rhs[c];
        auto update = [](double g, double vpq, double& j) { const double i = g * vpq - j; j = g * vpq + i; };
        update(g1, v[0] - v[1], j1);
        update(g2, v[4] - v[3], j2);
        update(g3, v[4] - v[5], j3);
        return (float)v[2];
    }
    void reset() { j1 = j2 = j3 = 0; }

    // Exact response of the same network at one frequency (complex nodal solve), for tests.
    double magnitudeAt(double hz) const
    {
        using C = std::complex<double>;
        const C s(0, 2 * juce::MathConstants<double>::pi * hz);
        std::array<std::array<C, 6>, 6> y {};
        auto conduct = [&](int p, int q, C g)
        {
            y[(size_t)p][(size_t)p] += g;
            if (q >= 0)
                y[(size_t)q][(size_t)q] += g, y[(size_t)p][(size_t)q] -= g, y[(size_t)q][(size_t)p] -= g;
        };
        conduct(0, -1, 1.0 / rSource);
        conduct(0, 1, s * c1);
        conduct(1, 2, 1.0 / ((1 - kt) * rTreble));
        conduct(2, 3, 1.0 / (kt * rTreble));
        conduct(2, -1, 1.0 / rLoad);
        conduct(0, 4, 1.0 / rSlope);
        conduct(4, 3, s * c2);
        conduct(4, 5, s * c3);
        conduct(3, 5, 1.0 / (kl * rBass));
        conduct(5, -1, 1.0 / (km * rMid));
        const auto i = invert(y);
        return std::abs(i[2][0] / rSource);
    }

private:
    template <typename T>
    static std::array<std::array<T, 6>, 6> invert(std::array<std::array<T, 6>, 6> a)
    {
        std::array<std::array<T, 6>, 6> r {};
        for (size_t i = 0; i < 6; ++i)
            r[i][i] = 1;
        for (size_t c = 0; c < 6; ++c) // Gauss-Jordan with partial pivoting
        {
            size_t p = c;
            for (size_t k = c + 1; k < 6; ++k)
                if (std::abs(a[k][c]) > std::abs(a[p][c]))
                    p = k;
            std::swap(a[c], a[p]), std::swap(r[c], r[p]);
            const T d = a[c][c];
            for (size_t k = 0; k < 6; ++k)
                a[c][k] /= d, r[c][k] /= d;
            for (size_t k = 0; k < 6; ++k)
                if (k != c)
                {
                    const T f = a[k][c];
                    for (size_t q = 0; q < 6; ++q)
                        a[k][q] -= f * a[c][q], r[k][q] -= f * r[c][q];
                }
        }
        return r;
    }

    // V2a's output resistance: 100k plate resistor parallel with the 12AX7's ~62k plate resistance.
    static constexpr double rSource = 38e3, rLoad = 1e6, rSlope = 33e3, rTreble = 250e3, rBass = 1e6, rMid = 25e3;
    static constexpr double c1 = 500e-12, c2 = 22e-9, c3 = 22e-9;
    double g1 = 0, g2 = 0, g3 = 0, j1 = 0, j2 = 0, j3 = 0;
    double kt = -1, kl = -1, km = -1;
    bool dirty = true;
    std::array<std::array<double, 6>, 6> inv {};
};
} // namespace circuit

class CircuitAmp
{
public:
    enum class Jack { one, two, jumped };

    void prepare(double sampleRate, int maxBlock)
    {
        os.initProcessing((size_t)maxBlock);
        os.reset();
        const double fs = sampleRate * (double)os.getOversamplingFactor();
        using circuit::ecc83;
        // ponytail: preamp B+ ~300 V and the bias-shift time constants are estimates, not from the drawing.
        v1Normal.build(ecc83, 300, 100e3, 820, 0, fs, 0.0015, 0.022, 1.5);
        v1Bright.build(ecc83, 300, 100e3, 2.7e3, 0, fs, 0.00015, 0.0022, 1.5); // .68uF: bypassed above ~90 Hz
        v2a.build(ecc83, 300, 100e3, 470, 470, fs, 0.0005, 0.02, 0.5);
        v2b.build(ecc83, 300, 100e3, 0, 2.7e3, fs, 0.0015, 0.022, 1.5);
        // Bright channel: .0022 into the 1M volume (72 Hz), and the 2.7k cathode's bass cut below its bypass.
        brightCoupling.highPass(72, fs);
        const double unbypassed = 1.0 / (1.0 + 101.0 * 2.7e3 / (100e3 + 62e3)); // degenerated / bypassed gain
        const double rk = 2.7e3, ck = 0.68e-6;
        brightCathode.set(unbypassed, rk * ck, 1.0, unbypassed * rk * ck, fs);
        normalCoupling.highPass(7, fs);
        // 500pF across the bright side's 470k into the mixing node (~470k): +6 dB above ~700 Hz.
        brightMix.set(1, 470e3 * 500e-12, 2, 470e3 * 500e-12, fs);
        toneStack.setRate(fs);
        toneStack.reset();
        v2bCoupling.highPass(7, fs);
        driverCoupling.highPass(7, fs);
        otLow1.highPass(35, fs), otLow2.highPass(35, fs), otHigh.lowPass(9000, fs);
        presenceRate = fs;
        lastPresence = -1;
        sagEnv = 0, sagDecay = (float)std::exp(-1.0 / (0.08 * fs));
        speaker = 0;
        for (auto* s : { &v1Normal, &v1Bright, &v2a, &v2b })
            s->reset();
    }

    int getLatency() const { return (int)std::lround(os.getLatencyInSamples()); }

    // Knobs 0..10 as on the panel. Message or audio thread, before process().
    void setControls(float presence, float bass, float middle, float treble, float vol1, float vol2, Jack j)
    {
        toneStack.setKnobs(treble / 10.0f, bass / 10.0f, middle / 10.0f);
        auto taper = [](float k) { return (std::pow(10.0f, k / 5.0f) - 1.0f) / 99.0f; }; // audio taper pots
        gain1 = j == Jack::two ? 0.0f : taper(vol1);   // channel I: bright side of V1
        gain2 = j == Jack::one ? 0.0f : taper(vol2);   // channel II: normal side
        if (std::abs(presence - lastPresence) > 0.01f)
        {
            lastPresence = presence;
            // Feedback node: 470R to ground, shunted by (5k * (1 - presence) + .68uF): less feedback up top.
            const double rp = std::max(10.0, 5e3 * (1.0 - presence / 10.0)), c = 0.68e-6;
            presenceShelf.set(1.0, c * rp, 1.0, c * (rp + 470.0), presenceRate);
        }
    }

    // Audio thread, mono in place. Input: the DI (1.0 = full scale).
    void process(float* x, int n)
    {
        juce::dsp::AudioBlock<float> block(&x, 1, (size_t)n);
        auto up = os.processSamplesUp(block);
        float* s = up.getChannelPointer(0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            s[i] = sample(s[i]);
        os.processSamplesDown(block);
    }

    // Calibration (ponytail: tuned against the captures and by ear; see tests).
    float inputVolts = 1.5f;   // DI full scale -> volts at the input jack
    float outputScale = 0.02f; // speaker volts -> plugin level

private:
    float sample(float di)
    {
        const float vin = di * inputVolts;
        // V1: both triodes see the guitar when jumped; an empty input's grid sits at ground.
        const float b = gain1 > 0 ? brightCoupling(v1Bright.process(brightCathode(vin))) : 0.0f;
        const float nrm = gain2 > 0 ? normalCoupling(v1Normal.process(vin)) : 0.0f;
        const float mix = 0.5f * (brightMix(b * gain1) + nrm * gain2);
        // V2a, with the global feedback arriving at its cathode (one sample late).
        const float fb = beta * presenceShelf(speaker);
        const float tone = toneStack.process(v2a.process(mix - fb));
        const float drive = v2bCoupling(v2b.process(tone));
        // ECC82 driver: low gain, soft limit well above the KT88s' bias.
        const float grid = driverCoupling(70.0f * std::tanh(drive * driverGain / 70.0f));
        // 4x KT88 ultra-linear push-pull: late, fairly hard limit; a stiff solid-state supply sags little.
        const float u = grid / 55.0f;
        const float shaped = u / std::pow(1.0f + std::pow(std::abs(u), 3.5f), 1.0f / 3.5f);
        const float out = (85.0f * (1.0f - 0.06f * sagEnv)) * shaped;
        sagEnv = std::max(std::abs(shaped), sagEnv * sagDecay);
        speaker = otHigh(otLow2(otLow1(out)));
        return speaker * outputScale;
    }

    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    circuit::TriodeStage v1Normal, v1Bright, v2a, v2b;
    circuit::OnePole brightCoupling, brightCathode, normalCoupling, brightMix, v2bCoupling, driverCoupling, otLow1, otLow2, otHigh, presenceShelf;
    circuit::ToneStack toneStack;
    float gain1 = 0, gain2 = 0, lastPresence = -1, speaker = 0, sagEnv = 0, sagDecay = 1;
    double presenceRate = 192000;
    // 16R tap -> 47k -> 470R: the drawing's feedback divider. ponytail: driver gain is an estimate.
    static constexpr float beta = 470.0f / 47470.0f, driverGain = 8.0f;
};
