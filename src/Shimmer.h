#pragma once
// Reverb and shimmer reverb: Airwindows Galactic3 (MIT) as the tank, with plain-language controls.
// Shimmer: like Danial Kooshki's Shimmer-Reverb (MIT), the tank's output is pitched up an octave and
// fed back into its input, so every pass climbs; here the shifter is two crossfaded delay taps (cheap,
// its grain artefacts vanish in the tail) and nothing allocates on the audio thread.
// Parameters (0..1): 0 DECAY, 1 SIZE, 2 TONE, 3 MOD, 4 MIX (the host blends dry/wet: we output full wet),
// 5 SHIMMER (how much of the octave is fed into the tank; 0 = plain reverb).

#include "audioeffectx.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

AudioEffect* mjr_create_Galactic3(void*);

class MjrVerb : public AudioEffect
{
public:
    explicit MjrVerb(bool shimmer) : AudioEffect(nullptr, 0, 6), canShimmer(shimmer), tank(mjr_create_Galactic3(nullptr))
    {
        tank->setParameter(3, 1.0f); // Derez off: full resolution
        tank->setParameter(5, 1.0f); // fully wet
        for (auto& o : octave)
            o.buf.assign(maxWindow + 4, 0.0f);
    }

    void setParameter(VstInt32 i, float v) override
    {
        switch (i)
        {
            case 0: tank->setParameter(0, 1.0f - v); break; // Galactic's Replace shortens the tail
            case 1: tank->setParameter(4, v); break;
            case 2: tank->setParameter(1, v), tone = v; break;
            case 3: tank->setParameter(2, v); break;
            case 5: amount = canShimmer ? v : 0.0f; break;
            default: break;
        }
    }

    void processReplacing(float** in, float** out, VstInt32 n) override
    {
        tank->setSampleRate(getSampleRate());
        const float sr = getSampleRate();
        const int window = std::min(maxWindow, (int)(0.05f * sr)); // 50 ms grains
        // The tank gives back ~12.6 dB less than it is fed, so the loop needs real gain to be heard: at the
        // default amount the octave enters the tank 4 dB below the note and each pass climbs again.
        const float fb = 1.2f * amount;
        const float hp = std::exp(-2.0f * 3.14159265f * 150.0f / sr);                   // loop: no mud climbing up
        const float lp = 1.0f - std::exp(-2.0f * 3.14159265f * (2000.0f + 10000.0f * tone) / sr);
        // Chunks keep the loop's delay short (the tank only takes blocks).
        for (int pos = 0; pos < n; pos += chunk)
        {
            const int m = std::min(chunk, n - pos);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < m; ++i)
                    dryBuf[(size_t)c][(size_t)i] = in[c][pos + i], inBuf[(size_t)c][(size_t)i] = in[c][pos + i] + loop[(size_t)c][(size_t)i];
            float* ins[2] = { inBuf[0].data(), inBuf[1].data() };
            float* outs[2] = { outBuf[0].data(), outBuf[1].data() };
            tank->processReplacing(ins, outs, m);
            for (int c = 0; c < 2; ++c)
            {
                auto& o = octave[(size_t)c];
                for (int i = 0; i < m; ++i)
                {
                    const float y = outBuf[(size_t)c][(size_t)i];
                    out[c][pos + i] = y;
                    if (fb <= 0.0f)
                    {
                        loop[(size_t)c][(size_t)i] = 0.0f;
                        continue;
                    }
                    // The octave comes from the note as well as from the tail, so it is heard on the first chord.
                    const float x = y + dryBuf[(size_t)c][(size_t)i];
                    o.hpState = hp * o.hpState + (1 - hp) * x; // one-pole high-pass = x - low-pass
                    o.lpState += lp * (o.shift(x - o.hpState, window) - o.lpState);
                    loop[(size_t)c][(size_t)i] = fb * std::tanh(1.5f * o.lpState);
                }
            }
        }
    }

private:
    // Octave up: two read taps sweep the delay from one window down to zero at twice the write speed,
    // half a window apart, each faded by sin^2 so the pair sums to unity.
    struct OctaveUp
    {
        std::vector<float> buf;
        int w = 0;
        float phase = 0, hpState = 0, lpState = 0;
        float shift(float x, int window)
        {
            const int size = (int)buf.size();
            buf[(size_t)w] = x;
            phase += 1.0f / (float)window;
            if (phase >= 1.0f)
                phase -= 1.0f;
            float y = 0;
            for (float p : { phase, phase < 0.5f ? phase + 0.5f : phase - 0.5f })
            {
                const float d = (1.0f - p) * (float)window + 1.0f;
                float r = (float)w - d;
                if (r < 0)
                    r += (float)size;
                const int i = (int)r;
                const float f = r - (float)i, a = buf[(size_t)i], b = buf[(size_t)((i + 1) % size)];
                const float s = std::sin(3.14159265f * p);
                y += (a + f * (b - a)) * s * s;
            }
            w = (w + 1) % size;
            return y;
        }
    };
    static constexpr int chunk = 32, maxWindow = 9600; // 50 ms at 192 kHz
    bool canShimmer;
    std::unique_ptr<AudioEffect> tank;
    float amount = 0, tone = 0.5f;
    std::array<OctaveUp, 2> octave;
    std::array<std::array<float, chunk>, 2> dryBuf {}, inBuf {}, outBuf {}, loop {};
};
