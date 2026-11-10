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

// Gauss-Jordan inverse with partial pivoting, for the small nodal matrices.
template <typename T, size_t N>
std::array<std::array<T, N>, N> invert(std::array<std::array<T, N>, N> a)
{
    std::array<std::array<T, N>, N> r {};
    for (size_t i = 0; i < N; ++i)
        r[i][i] = 1;
    for (size_t c = 0; c < N; ++c)
    {
        size_t p = c;
        for (size_t k = c + 1; k < N; ++k)
            if (std::abs(a[k][c]) > std::abs(a[p][c]))
                p = k;
        std::swap(a[c], a[p]), std::swap(r[c], r[p]);
        const T d = a[c][c];
        for (size_t k = 0; k < N; ++k)
            a[c][k] /= d, r[c][k] /= d;
        for (size_t k = 0; k < N; ++k)
            if (k != c)
            {
                const T f = a[k][c];
                for (size_t q = 0; q < N; ++q)
                    a[k][q] -= f * a[c][q], r[k][q] -= f * r[c][q];
            }
    }
    return r;
}

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
        // Loop poles (low end): .047 into the bootstrapped driver grid ~0.7 Hz, the OT primary inductance ~6 Hz,
        // and the dominant one, .047 into the 68k bias feeds of the KT88s (~50 Hz) - feedback flattens it.
        v2bCoupling.highPass(0.7, fs);
        driverCoupling.highPass(50, fs);
        otLow.highPass(6, fs), otHigh.lowPass(9000, fs);
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
    float outputScale = 0.002f; // speaker volts -> plugin level

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
        speaker = otHigh(otLow(out));
        return speaker * outputScale;
    }

    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    circuit::TriodeStage v1Normal, v1Bright, v2a, v2b;
    circuit::OnePole brightCoupling, brightCathode, normalCoupling, brightMix, v2bCoupling, driverCoupling, otLow, otHigh, presenceShelf;
    circuit::ToneStack toneStack;
    float gain1 = 0, gain2 = 0, lastPresence = -1, speaker = 0, sagEnv = 0, sagDecay = 1;
    double presenceRate = 192000;
    // 16R tap -> 47k -> 470R: the drawing's feedback divider. Driver: cathodyne, gain ~1 per side.
    static constexpr float beta = 470.0f / 47470.0f, driverGain = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------
// Amps B and C: the 1967 drawing (Unicord 70-02-12) followed as drawn, block by block.
//   V1: both triodes, grids straight from the jacks (68k), plates 100k. Top jacks (I): cathode 2.7k/.68uF,
//       plate -> .0022 -> Volume I (1M) -> wiper -> 470k || 500pF -> V2a's grid. Bottom jacks (II): cathode
//       820/250uF, plate -> .022 -> Volume II (1M) -> wiper -> 470k -> V2a's grid. (The drawing puts a junction
//       dot where the .0022's wire crosses the mixing bus: taken as a drafting error, since it would leave
//       channel I without a volume control.)
//   V2a: plate 100k; cathode 470 (25uF) + 470 to node N. N: global feedback from the 16R tap (47k), and
//       Presence (5k pot, wiper -> .68uF).
//   Tone stack from V2a's plate: 500pF, Treble 250k, slope 33k, .022 to the bass top, Bass 1M rheostat,
//       Middle 25k pot to ground with the second .022 on its wiper. Treble wiper -> .022 -> V2b.
//   V2b: cathodyne, plate 100k, cathode 2.7k + 100k, 1M grid leak bootstrapped to the 100k's top.
//   V3 (ECC82): the two phases into a pair with a common 1.5k cathode, grids 270k (drawn "270": an error),
//       plates 47k -> .047 -> 68k bias feeds -> 5.6k -> 4x KT88 ultra-linear, 250R screen stoppers.
//   Supply: solid-state doubler, 2x375uF + 200uF. Voltages from the 200 W service sheet (Sept '70): preamp
//       plates 170-175 V, cathodyne cathode 68 V, driver cathode 14 V, KT88 plates 590 V, bias -82 V.
// Amp C: the same circuit with the 1968 drawing's hand-written lead mods: feedback 100k, bias -70 V
// (hotter), and a hotter input.
namespace circuit
{
inline constexpr Triode ecc82 { 21.5, 1.3, 1180.0, 84.0, 300.0 };
// ponytail: KT88 as Koren's triode-connected fit driven by the screen (ultra-linear) and a knee on the
// plate; check: idle ~17 mA at -82 V. A pentode fit would be the upgrade.
inline constexpr Triode kt88 { 8.8, 1.35, 730.0, 32.0, 16.0 };

// Small linear network solved by nodal analysis, capacitors trapezoidal. Resistors may be re-stamped
// when a knob moves; capacitor states survive.
template <size_t N, size_t NC>
struct Network
{
    std::array<std::array<double, N>, N> y {}, inv {};
    std::array<int, NC> cp {}, cq {};
    std::array<double, NC> cg {}, cj {};
    void begin() { y = {}; }
    void r(int p, int q, double ohms) { stamp(p, q, 1.0 / ohms); }
    void c(size_t i, int p, int q, double farads, double fs) { cp[i] = p, cq[i] = q, cg[i] = 2 * farads * fs, stamp(p, q, cg[i]); }
    void end() { inv = invert(y); }
    void reset() { cj = {}; }
    // Current injections in, node voltages out.
    std::array<double, N> solve(const std::array<double, N>& in)
    {
        auto rhs = in;
        for (size_t i = 0; i < NC; ++i)
        {
            rhs[(size_t)cp[i]] += cj[i];
            if (cq[i] >= 0)
                rhs[(size_t)cq[i]] -= cj[i];
        }
        std::array<double, N> v {};
        for (size_t a = 0; a < N; ++a)
            for (size_t b = 0; b < N; ++b)
                v[a] += inv[a][b] * rhs[b];
        for (size_t i = 0; i < NC; ++i)
        {
            const double vpq = v[(size_t)cp[i]] - (cq[i] >= 0 ? v[(size_t)cq[i]] : 0.0);
            const double cur = cg[i] * vpq - cj[i];
            cj[i] = cg[i] * vpq + cur;
        }
        return v;
    }

private:
    void stamp(int p, int q, double g)
    {
        y[(size_t)p][(size_t)p] += g;
        if (q >= 0)
            y[(size_t)q][(size_t)q] += g, y[(size_t)p][(size_t)q] -= g, y[(size_t)q][(size_t)p] -= g;
    }
};

template <typename F>
double bisect(double lo, double hi, F f) // f(lo) > 0 > f(hi)
{
    for (int i = 0; i < 44; ++i)
    {
        const double m = 0.5 * (lo + hi);
        (f(m) > 0 ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

// Common-cathode stage with its cathode bypass cap as state (so the bypass is exact, not a shelf):
// plate current tabulated against grid minus cap voltage; the unbypassed part degenerates in the table.
class Stage
{
public:
    void build(const Triode& t, double bPlus, double rPlate, double rk, double ck, double rku, double fs,
               double tauCharge = 0, double tauRecover = 0)
    {
        rl = rPlate, rkb = rk, b = bPlus;
        iq = bisect(0, b / rl, [&](double i) { return t.ip(b - i * (rl + rk + rku), -i * (rk + rku)) - i; });
        vcq = iq * rk;
        for (size_t n = 0; n < table.size(); ++n)
        {
            const double u = lo + (hi - lo) * (double)n / (double)(table.size() - 1);
            table[n] = (float)bisect(0, b / rl, [&](double i) { return t.ip(b - i * (rl + rku) - vcq, u - i * rku) - i; });
        }
        dtc = rk > 0 ? 1.0 / (fs * ck) : 0;
        charge = tauCharge > 0 ? (float)(1.0 / (tauCharge * fs)) : 0.0f;
        recover = tauRecover > 0 ? (float)std::exp(-1.0 / (tauRecover * fs)) : 1.0f;
        reset();
    }
    void reset() { vc = vcq, shift = 0, current = (float)iq; }
    double plateVolts() const { return b - current * rl; }
    double cathodeVolts() const { return vc; } // the bypassed part (what a meter reads with no signal)
    double outputResistance(const Triode& t) const { return 1.0 / (1.0 / rl + 1.0 / platePlateResistance(t)); }
    // Grid voltage (AC, grid DC at ground) -> plate swing around the quiescent plate voltage.
    float process(float vg)
    {
        float u = vg + shift - (float)vc;
        if (u > 0) // grid conduction: clamps, and charges the coupling cap upstream (if any)
        {
            shift -= u * charge;
            u = u / (1.0f + u);
        }
        shift *= recover;
        current = lookup(u);
        if (rkb > 0)
            vc += dtc * (current - vc / rkb);
        return (float)(iq - current) * (float)rl;
    }

private:
    double platePlateResistance(const Triode& t) const
    {
        const double vp = b - iq * rl, vg = -vcq, d = 1.0;
        return 2 * d / std::max(1e-9, t.ip(vp + d, vg) - t.ip(vp - d, vg));
    }
    float lookup(float u) const
    {
        const float pos = (u - (float)lo) * (float)(table.size() - 1) / (float)(hi - lo);
        if (pos <= 0)
            return table.front();
        if (pos >= (float)(table.size() - 1))
            return table.back();
        const auto i = (size_t)pos;
        return table[i] + (pos - (float)i) * (table[i + 1] - table[i]);
    }
    static constexpr double lo = -80.0, hi = 4.0;
    std::array<float, 4096> table {};
    double rl = 1, rkb = 0, b = 0, iq = 0, vcq = 0, vc = 0, dtc = 0;
    float charge = 0, recover = 1, shift = 0, current = 0;
};

// Cathodyne: plate and cathode loads carry the same current, so one table gives both phases.
class Cathodyne
{
public:
    void build(const Triode& t, double bPlus, double rPlate, double rBias, double rLoad, double fs)
    {
        ra = rPlate, rload = rLoad;
        const double rkTot = rBias + rLoad;
        // Grid DC sits at the top of the load (bootstrapped leak): vgk = -I * rBias.
        iq = bisect(0, bPlus / (ra + rkTot), [&](double i) { return t.ip(bPlus - i * (ra + rkTot), -i * rBias) - i; });
        vgq = iq * rLoad;
        for (size_t n = 0; n < table.size(); ++n)
        {
            const double vg = vgq + lo + (hi - lo) * (double)n / (double)(table.size() - 1);
            const double i = bisect(0, bPlus / (ra + rkTot), [&](double c) { return t.ip(bPlus - c * (ra + rkTot), vg - c * rkTot) - c; });
            table[n] = (float)i;
            grid[n] = (float)(vg - i * rkTot); // grid-cathode: > 0 means grid current
        }
        charge = (float)(1.0 / (0.0003 * fs));          // ponytail: grid current into .022 + the treble pot's ~100k
        recover = (float)std::exp(-1.0 / (0.4 * fs));    // .022 into the bootstrapped 1M (~20M effective)
        shift = 0;
    }
    // Grid AC -> {plate swing, cathode swing} around their quiescent voltages.
    std::pair<float, float> process(float vg)
    {
        float pos = (vg + shift - (float)lo) * (float)(table.size() - 1) / (float)(hi - lo);
        pos = juce::jlimit(0.0f, (float)(table.size() - 1) - 0.001f, pos);
        const auto i = (size_t)pos;
        const float f = pos - (float)i;
        if (const float g = grid[i] + f * (grid[i + 1] - grid[i]); g > 0)
            shift -= g * charge;
        shift *= recover;
        const float cur = table[i] + f * (table[i + 1] - table[i]) - (float)iq;
        return { -cur * (float)ra, cur * (float)rload };
    }
    void reset() { shift = 0; }

private:
    static constexpr double lo = -250.0, hi = 250.0;
    std::array<float, 4096> table {}, grid {};
    double ra = 1, rload = 1, iq = 0, vgq = 0;
    float charge = 0, recover = 1, shift = 0;
};

// The driver pair: two triodes sharing an unbypassed cathode resistor. Tabulated over both grids.
class DriverPair
{
public:
    void build(const Triode& t, double bPlus, double rPlate, double rk)
    {
        rl = rPlate;
        auto solve = [&](double g1, double g2, double& i1, double& i2)
        {
            auto at = [&](double vk, double g) { return bisect(0, bPlus / rl, [&](double i) { return t.ip(bPlus - i * rl - vk, g - vk) - i; }); };
            const double vk = bisect(0, bPlus, [&](double v) { return (at(v, g1) + at(v, g2)) * rk - v; });
            i1 = at(vk, g1), i2 = at(vk, g2);
            return vk;
        };
        double a, c;
        solve(0, 0, a, c);
        iq = a;
        for (size_t x = 0; x < n; ++x)
            for (size_t y = 0; y < n; ++y)
            {
                const double vk = solve(volts(x), volts(y), a, c);
                cur[x][y] = (float)a;
                cath[x][y] = (float)vk;
            }
        vk1 = vk2 = cath[n / 2][n / 2];
    }
    // Grid AC (grid DC at ground) -> plate swings; grids are clamped where they would conduct.
    std::pair<float, float> process(float g1, float g2)
    {
        g1 = clampGrid(g1, vk1), g2 = clampGrid(g2, vk2);
        const float a = at(cur, g1, g2), c = at(cur, g2, g1);
        vk1 = vk2 = at(cath, g1, g2);
        return { ((float)iq - a) * (float)rl, ((float)iq - c) * (float)rl };
    }
    float cathode() const { return vk1; }

private:
    static constexpr size_t n = 97;
    static constexpr double lo = -60.0, hi = 60.0;
    static double volts(size_t i) { return lo + (hi - lo) * (double)i / (double)(n - 1); }
    static float clampGrid(float g, float vk) { const float over = g - vk; return over > 0 ? vk + over / (1.0f + over) : g; }
    static float at(const std::vector<std::vector<float>>& t, float a, float b)
    {
        auto idx = [](float v, size_t& i, float& f)
        {
            const float p = juce::jlimit(0.0f, (float)(n - 1) - 0.001f, (v - (float)lo) * (float)(n - 1) / (float)(hi - lo));
            i = (size_t)p, f = p - (float)i;
        };
        size_t i, j;
        float fi, fj;
        idx(a, i, fi), idx(b, j, fj);
        const float top = t[i][j] + fj * (t[i][j + 1] - t[i][j]), bot = t[i + 1][j] + fj * (t[i + 1][j + 1] - t[i + 1][j]);
        return top + fi * (bot - top);
    }
    std::vector<std::vector<float>> cur = std::vector<std::vector<float>>(n, std::vector<float>(n)), cath = cur;
    double rl = 1, iq = 0;
    float vk1 = 0, vk2 = 0;
};

// One side of the output stage's grid: driver plate (Thevenin) -> .047 -> node with 68k to the bias supply,
// grid current above 0 V through the 5.6k stopper. Blocking distortion falls out of the cap's charge.
struct GridFeed
{
    double rs = 6.6e3, rb = 68e3, rg = 1.5e3 + 5.6e3, dt = 0, cap = 0.047e-6, vcap = 0, bias = 82;
    // Returns the grid voltage (absolute, negative = biased) for a driver AC voltage.
    float process(float vin)
    {
        const double e = vin - vcap;
        double x = e * rb / (rb + rs); // AC voltage at the node, no grid current
        if (x > bias)
            x = (e / rs + bias / rg) / (1.0 / rs + 1.0 / rb + 1.0 / rg);
        vcap += (e - x) / rs * dt / cap;
        const double vg = x - bias;
        return (float)(vg > 0 ? vg * 1.5e3 / rg : vg); // the stopper drops most of a conducting grid's rise
    }
};
} // namespace circuit

class CircuitAmpFull
{
public:
    using Jack = CircuitAmp::Jack;
    struct Design
    {
        double feedbackR, biasVolts;
        float inputGain, outputScale;
    };
    static constexpr Design as1967 { 47e3, 82.0, 1.0f, 0.0025f };     // amp B: as drawn
    static constexpr Design lead1968 { 100e3, 70.0, 2.0f, 0.0025f };  // amp C: the 1968 lead mods

    explicit CircuitAmpFull(Design d) : design(d) {}

    void prepare(double sampleRate, int maxBlock)
    {
        using namespace circuit;
        os.initProcessing((size_t)maxBlock);
        os.reset();
        fs = sampleRate * (double)os.getOversamplingFactor();
        // Supplies chosen so the quiescent voltages land on the service sheet's (see the header).
        v1Bright.build(ecc83, 268, 100e3, 2.7e3, 0.68e-6, 0, fs);
        v1Normal.build(ecc83, 268, 100e3, 820, 250e-6, 0, fs);
        v2a.build(ecc83, 313, 100e3, 470, 25e-6, 470, fs, 0.0005, 0.02);
        v2b.build(ecc83, 321, 100e3, 2.7e3, 100e3, fs);
        static const DriverPair pair = [] { DriverPair p; p.build(ecc82, 470, 47e3, 1.5e3); return p; }(); // ~0.3 s, once
        v3 = pair;
        for (auto* g : { &gridA, &gridB })
            g->dt = 1.0 / fs, g->bias = design.biasVolts, g->vcap = 0;
        buildKt88();
        rSrc1 = v1Bright.outputResistance(ecc83), rSrc2 = v1Normal.outputResistance(ecc83), rSrcTone = v2a.outputResistance(ecc83);
        volume.reset(), tone.reset(), presence.reset();
        hp1.highPass(12.5, fs), hp2.highPass(12.5, fs); // .047 into the driver's 270k grid leaks
        otLow.highPass(6, fs), otHigh.lowPass(9000, fs);
        knobs.fill(-1);
        vAA = 0, speaker = 0, bPlus = 590;
        iIdle = 4 * idleCurrent;
        vNoLoad = 590 + supplyR * (iIdle + 0.03);
    }
    int getLatency() const { return (int)std::lround(os.getLatencyInSamples()); }

    void setControls(float presenceK, float bass, float middle, float treble, float vol1, float vol2, Jack j)
    {
        jack = j;
        const std::array<float, 6> k { presenceK, bass, middle, treble, vol1, vol2 };
        if (k == knobs)
            return;
        const bool toneMoved = k[1] != knobs[1] || k[2] != knobs[2] || k[3] != knobs[3];
        const bool volMoved = k[4] != knobs[4] || k[5] != knobs[5], presMoved = k[0] != knobs[0];
        knobs = k;
        auto audio = [](float v) { return juce::jlimit(1e-4, 1.0 - 1e-4, (std::pow(10.0, v / 5.0) - 1.0) / 99.0); };
        auto lin = [](float v) { return juce::jlimit(1e-4, 1.0 - 1e-4, v / 10.0); };
        if (volMoved)
        {
            // Nodes: 0 ch I plate, 1 ch II plate, 2 V2a grid (mix), 3 pot II top, 4 pot II wiper, 5 pot I wiper, 6 pot I top.
            const double a = audio(vol2), b = audio(vol1);
            volume.begin();
            volume.r(0, -1, rSrc1), volume.r(1, -1, rSrc2);
            volume.c(0, 0, 6, 2.2e-9, fs), volume.c(1, 1, 3, 22e-9, fs);
            volume.r(3, 4, (1 - a) * 1e6), volume.r(4, -1, a * 1e6), volume.r(4, 2, 470e3);
            volume.r(6, 5, (1 - b) * 1e6), volume.r(5, -1, b * 1e6), volume.r(2, 5, 470e3), volume.c(2, 2, 5, 500e-12, fs);
            volume.end();
        }
        if (toneMoved)
        {
            // Nodes: 0 V2a plate, 1 treble top, 2 treble wiper, 3 bass top, 4 slope node, 5 bass bottom, 6 mid wiper, 7 V2b grid.
            const double t = lin(treble), l = audio(bass), m = lin(middle);
            tone.begin();
            tone.r(0, -1, rSrcTone);
            tone.c(0, 0, 1, 500e-12, fs);
            tone.r(1, 2, (1 - t) * 250e3), tone.r(2, 3, t * 250e3);
            tone.r(0, 4, 33e3), tone.c(1, 4, 3, 22e-9, fs);
            tone.r(3, 5, l * 1e6);
            tone.r(5, 6, (1 - m) * 25e3), tone.r(6, -1, m * 25e3), tone.c(2, 4, 6, 22e-9, fs);
            tone.c(3, 2, 7, 22e-9, fs), tone.r(7, -1, 20e6); // the 1M leak, bootstrapped
            tone.end();
        }
        if (presMoved)
        {
            // Nodes: 0 N (V2a's lower cathode node), 1 presence wiper. The feedback arrives through feedbackR.
            const double w = lin(presenceK);
            presence.begin();
            presence.r(0, -1, 470), presence.r(0, -1, design.feedbackR);
            presence.r(0, 1, (1 - w) * 5e3), presence.r(1, -1, w * 5e3), presence.c(0, 1, -1, 0.68e-6, fs);
            presence.end();
        }
    }

    void process(float* x, int n)
    {
        juce::dsp::AudioBlock<float> block(&x, 1, (size_t)n);
        auto up = os.processSamplesUp(block);
        float* s = up.getChannelPointer(0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            s[i] = sample(s[i]);
        os.processSamplesDown(block);
    }

    // Test hooks: quiescent voltages against the service sheet.
    double v1PlateVolts() const { return v1Normal.plateVolts(); }
    double v1CathodeVolts() const { return v1Normal.cathodeVolts(); }
    double kt88IdleAmps() const { return idleCurrent; }
    float inputVolts = 4.4f; // DI full scale: an interface's instrument input clips around +12 dBu

private:
    float sample(float di)
    {
        const float vin = di * inputVolts * design.inputGain;
        const float in1 = jack != Jack::two ? vin : 0.0f, in2 = jack != Jack::one ? vin : 0.0f;
        const double p1 = v1Bright.process(in1), p2 = v1Normal.process(in2);
        const double grid = volume.solve({ p1 / rSrc1, p2 / rSrc2, 0, 0, 0, 0, 0 })[2];
        // Feedback: the speaker drives node N through feedbackR (one sample late).
        const double vN = presence.solve({ speaker / design.feedbackR, 0 })[0];
        const double plate = v2a.process((float)(grid - vN));
        const double toV2b = tone.solve({ plate / rSrcTone, 0, 0, 0, 0, 0, 0, 0 })[7];
        const auto [ph1, ph2] = v2b.process((float)toV2b);
        const auto [d1, d2] = v3.process(hp1(ph1), hp2(ph2));
        const float g1 = gridA.process(d1), g2 = gridB.process(d2);
        // Push-pull through an ideal transformer: v = Raa (I1 - I2), two KT88s per side. Newton on v.
        double v = vAA, i1 = 0, i2 = 0;
        for (int it = 0; it < 4; ++it)
        {
            double s1, s2;
            i1 = 2 * kt88At(g1, bPlus - 0.5 * v, s1), i2 = 2 * kt88At(g2, bPlus + 0.5 * v, s2);
            const double f = v - raa * (i1 - i2), df = 1 + raa * (s1 + s2); // s = dI/dVp of one tube; x2 tubes, x0.5
            v -= f / df;
        }
        vAA = v = juce::jlimit(-2.0 * bPlus, 2.0 * bPlus, v);
        // Supply: the doubler's reservoir sags with the average current.
        bPlus += ((vNoLoad - bPlus) / supplyR - (i1 + i2 + 0.03)) / (supplyC * fs);
        speaker = otHigh(otLow((float)(-v / turns))); // the secondary is wired so the feedback is negative
        return speaker * design.outputScale;
    }

    // One KT88, ultra-linear (screen at 43% of the half primary): current and its slope vs plate voltage.
    void buildKt88()
    {
        for (size_t a = 0; a < ktN; ++a)
            for (size_t b = 0; b < ktN; ++b)
            {
                const double vg = ktVg(a), vp = ktVp(b), vs = 590 - 0.43 * (590 - vp);
                kt[a][b] = (float)(circuit::kt88.ip(std::max(0.0, vs), vg) * std::tanh(std::max(0.0, vp) / 40.0));
            }
        double s;
        idleCurrent = kt88At((float)-design.biasVolts, 590, s);
    }
    static constexpr size_t ktN = 192;
    static double ktVg(size_t i) { return -200.0 + 220.0 * (double)i / (double)(ktN - 1); }
    static double ktVp(size_t i) { return 1400.0 * (double)i / (double)(ktN - 1); }
    double kt88At(float vg, double vp, double& slope) const
    {
        const double pg = juce::jlimit(0.0, (double)ktN - 1.001, (vg + 200.0) * (double)(ktN - 1) / 220.0);
        const double pp = juce::jlimit(0.0, (double)ktN - 1.001, vp * (double)(ktN - 1) / 1400.0);
        const auto i = (size_t)pg, j = (size_t)pp;
        const double fi = pg - (double)i, fj = pp - (double)j;
        const double lo = kt[i][j] + fi * (kt[i + 1][j] - kt[i][j]), hi = kt[i][j + 1] + fi * (kt[i + 1][j + 1] - kt[i][j + 1]);
        slope = (hi - lo) * (double)(ktN - 1) / 1400.0;
        return lo + fj * (hi - lo);
    }

    Design design;
    Jack jack = Jack::one;
    double fs = 192000;
    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    circuit::Stage v1Bright, v1Normal, v2a;
    circuit::Cathodyne v2b;
    circuit::DriverPair v3;
    circuit::GridFeed gridA, gridB;
    circuit::Network<7, 3> volume;
    circuit::Network<8, 4> tone;
    circuit::Network<2, 1> presence;
    circuit::OnePole hp1, hp2, otLow, otHigh;
    std::vector<std::vector<float>> kt = std::vector<std::vector<float>>(ktN, std::vector<float>(ktN));
    std::array<float, 6> knobs {};
    double rSrc1 = 38e3, rSrc2 = 38e3, rSrcTone = 38e3, vAA = 0, bPlus = 590, vNoLoad = 600, iIdle = 0, idleCurrent = 0;
    float speaker = 0;
    // 4x KT88 into ~2.5k anode to anode (200 W at 590 V); 16R tap. ponytail: supply resistance is an estimate.
    static constexpr double raa = 2500, turns = 12.5, supplyR = 40, supplyC = 187.5e-6 + 200e-6;
};
