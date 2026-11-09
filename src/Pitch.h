#pragma once
// YIN pitch detection (de Cheveigné & Kawahara 2002) for the tuner. Message thread.
#include <cmath>
#include <vector>

// Frequency in Hz of the mono signal x[0..n), or 0 if no clear pitch.
// Needs n >= 2 * sampleRate / lowestHz.
inline double detectPitch(const float* x, int n, double sampleRate, double lowestHz = 50.0)
{
    const int w = std::min(n / 2, (int)std::ceil(sampleRate / lowestHz));
    double energy = 0;
    for (int i = 0; i < n; ++i)
        energy += (double)x[i] * x[i];
    if (w < 4 || energy / n < 1e-7) // below ~ -70 dBFS: nothing to tune
        return 0;

    thread_local std::vector<double> d; // reused: the tuner calls this 20 times a second
    d.assign((size_t)w + 1, 0.0);
    for (int tau = 1; tau <= w; ++tau)
        for (int j = 0; j < w; ++j)
        {
            const double diff = (double)x[j] - x[j + tau];
            d[(size_t)tau] += diff * diff;
        }

    // Cumulative mean normalised difference; first dip under the threshold wins.
    double sum = 0;
    d[0] = 1;
    for (int tau = 1; tau <= w; ++tau)
    {
        sum += d[(size_t)tau];
        d[(size_t)tau] = sum > 0 ? d[(size_t)tau] * tau / sum : 1;
    }
    int tau = 2;
    for (; tau < w; ++tau)
        if (d[(size_t)tau] < 0.15)
        {
            while (tau + 1 < w && d[(size_t)tau + 1] < d[(size_t)tau])
                ++tau;
            break;
        }
    if (tau >= w)
        return 0;

    // Parabolic interpolation around the minimum.
    const double a = d[(size_t)tau - 1], b = d[(size_t)tau], c = d[(size_t)tau + 1];
    const double den = a - 2 * b + c;
    const double t = tau + (std::abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
    return sampleRate / t;
}
