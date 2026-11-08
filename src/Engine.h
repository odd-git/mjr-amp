#pragma once
// NAM engine: runs the active capture at its training rate (resampling if the host
// differs) and crossfades when a new capture is loaded. Models are loaded on a
// background thread; the audio thread only swaps pointers.

#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "NAM/get_dsp.h"
// The resampler comes from iPlug2 and expects two of its symbols.
namespace iplug { constexpr double PI = juce::MathConstants<double>::pi; }
#define DEFAULT_BLOCK_SIZE 512
#include "dsp/ResamplingContainer/ResamplingContainer.h"

class NamEngine
{
public:
    static constexpr double modelRate = 48000.0; // every capture in the library is 48 kHz
    static constexpr float targetLoudnessDb = -18.0f;
    bool normaliseLoudness = true; // amps: level every capture to targetLoudnessDb. Set before load().

    ~NamEngine()
    {
        loader.removeAllJobs(true, 5000);
        delete pending.exchange(nullptr);
        delete retired.exchange(nullptr);
        delete current;
        delete next;
    }

    // Not real-time: call from prepareToPlay (audio stopped).
    void prepare(double hostRate, int maxBlock)
    {
        rate = hostRate;
        maxModelBlock = (int)std::ceil(maxBlock * modelRate / hostRate) + 16;
        resampler.Reset(hostRate, maxBlock);
        mix.assign((size_t)maxModelBlock, 0.0f);
        for (auto* m : { current, next })
            if (m != nullptr)
                m->dsp->ResetAndPrewarm(modelRate, maxModelBlock);
        if (auto* m = pending.load())
            m->dsp->ResetAndPrewarm(modelRate, maxModelBlock); // ponytail: races a loader still writing pending; prepare during a load is rare
    }

    int getLatency() const { return needsResampling() ? resampler.GetLatency() : 0; }

    // Message thread. Returns immediately; the swap happens on the audio thread.
    void load(const juce::File& file)
    {
        const int block = maxModelBlock;
        const bool normalise = normaliseLoudness;
        loader.addJob([this, file, block, normalise]
        {
            std::unique_ptr<Model> m;
            try
            {
                m.reset(new Model { nam::get_dsp(std::filesystem::path(file.getFullPathName().toStdString())), 1.0f });
                m->dsp->ResetAndPrewarm(modelRate, block);
                if (normalise && m->dsp->HasLoudness())
                    m->gain = juce::Decibels::decibelsToGain(targetLoudnessDb - (float)m->dsp->GetLoudness());
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog("NAM load failed: " + file.getFullPathName() + " " + e.what());
                return;
            }
            delete pending.exchange(m.release()); // a newer load replaces one not yet picked up
        });
    }

    // Message thread, periodically: frees the model that faded out.
    void collectGarbage() { delete retired.exchange(nullptr); }

    bool isIdle() const { return loader.getNumJobs() == 0 && pending.load() == nullptr && next == nullptr; }
    bool hasModel() const { return current != nullptr; }

    // Audio thread. Mono in/out, in place.
    void process(float* data, int n)
    {
        if (!needsResampling())
            return processAtModelRate(data, data, n);
        float* in[] = { data };
        float* out[] = { data };
        resampler.ProcessBlock(in, out, n, [this](float** i, float** o, int k) { processAtModelRate(i[0], o[0], k); });
    }

private:
    struct Model
    {
        std::unique_ptr<nam::DSP> dsp;
        float gain;
    };

    bool needsResampling() const { return std::abs(rate - modelRate) > 0.5; }

    void processAtModelRate(float* in, float* out, int n)
    {
        if (next == nullptr && retired.load() == nullptr)
            if (auto* m = pending.exchange(nullptr))
            {
                next = m;
                fadePos = 0;
            }

        // `in` may alias `out`; the model reads its input before writing, but the
        // second model needs the dry signal, so run it first into `mix`.
        if (next != nullptr)
        {
            float* m = mix.data();
            next->dsp->process(&in, &m, n);
        }

        if (current != nullptr)
        {
            current->dsp->process(&in, &out, n);
            juce::FloatVectorOperations::multiply(out, current->gain, n);
        }
        else
            juce::FloatVectorOperations::clear(out, n);

        if (next == nullptr)
            return;

        const int fadeLen = (int)(0.05 * modelRate);
        for (int i = 0; i < n; ++i)
        {
            const float t = std::min(1.0f, (float)(fadePos + i) / (float)fadeLen);
            out[i] = out[i] * (1.0f - t) + mix[(size_t)i] * next->gain * t;
        }
        fadePos += n;
        if (fadePos >= fadeLen)
        {
            retired.store(current);
            current = next;
            next = nullptr;
        }
    }

    double rate = modelRate;
    int maxModelBlock = 4096;
    dsp::ResamplingContainer<float, 1, 12> resampler { modelRate };
    std::vector<float> mix = std::vector<float>(4096);

    Model* current = nullptr; // audio thread only
    Model* next = nullptr;    // audio thread only, fading in
    int fadePos = 0;
    std::atomic<Model*> pending { nullptr }; // loader -> audio
    std::atomic<Model*> retired { nullptr }; // audio -> message thread
    juce::ThreadPool loader { juce::ThreadPoolOptions {}.withNumberOfThreads(1) };
};
