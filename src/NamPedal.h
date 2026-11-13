#pragma once
// A pedal built like the amp: a folder of NAM captures of one pedal, each named with the settings it
// was taken at. Two naming schemes:
//  - three knobs, "... Drive 7 Tone 7 Level 9.nam": the knobs pick the nearest capture, then DSP covers
//    the distance: drive = input gain, tone = treble shelf, level = output gain;
//  - one knob, "... 65%.nam" (a boost): the nearest capture, and between captures the output level
//    follows the captures' own loudness, interpolated (in dB) along the knob. Pedal captures often lose
//    their absolute level (one boost set attenuates by 9 dB even at full Level), so the curve is
//    anchored at the top: the loudest capture gives maxBoostDb.

#include <juce_dsp/juce_dsp.h>
#include <regex>
#include "Engine.h"

class NamPedal
{
public:
    struct Shot
    {
        juce::File file;
        float drive, tone, level; // 0..10, as on the pedal (one-knob pedals: the knob is `drive`)
        float loudness = 0;       // dB, from the capture's metadata (one-knob pedals only)
    };

    NamPedal() { engine.normaliseLoudness = false; } // a pedal's own output level is part of its sound

    // Message thread. Returns how many captures the folder has.
    int setFolder(const juce::File& dir)
    {
        folder = dir;
        shots.clear();
        static const std::regex three(R"(Drive\s*([0-9.]+)\s*Tone\s*([0-9.]+)\s*Level\s*([0-9.]+))", std::regex::icase);
        static const std::regex percent(R"(([0-9.]+)\s*%)");
        oneKnob = false;
        for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.nam"))
        {
            std::smatch m;
            const auto name = f.getFileNameWithoutExtension().toStdString();
            if (std::regex_search(name, m, three))
                shots.push_back({ f, std::stof(m[1]), std::stof(m[2]), std::stof(m[3]) });
            else if (std::regex_search(name, m, percent))
            {
                const auto meta = juce::JSON::parse(f)["metadata"];
                shots.push_back({ f, std::stof(m[1]) / 10.0f, 0, 0, (float)(double)meta.getProperty("loudness", 0.0) });
                oneKnob = true;
            }
        }
        std::sort(shots.begin(), shots.end(), [](auto& a, auto& b) { return a.drive != b.drive ? a.drive < b.drive : a.file.getFileName() < b.file.getFileName(); });
        loaded = -1;
        return (int)shots.size();
    }
    juce::File getFolder() const { return folder; }

    // Message thread, polled: loads the capture nearest to the knobs when that changes.
    void update(float drive, float tone, float level)
    {
        int best = -1;
        float bestDist = 1e9f;
        for (size_t i = 0; i < shots.size(); ++i) // drive is the nonlinear part: it weighs most
        {
            const auto& s = shots[i];
            const float d = 3.0f * std::abs(s.drive - drive) + std::abs(s.tone - tone) + 0.5f * std::abs(s.level - level);
            if (d < bestDist)
                best = (int)i, bestDist = d;
        }
        if (best >= 0 && best != loaded)
        {
            const auto& s = shots[(size_t)best];
            engine.load(s.file);
            base[0] = s.drive, base[1] = s.tone, base[2] = s.level;
            loaded = best;
        }
        if (oneKnob && best >= 0)
            levelDb = gainDb(drive) - (shots[(size_t)best].loudness + loudnessToGainDb);
        engine.collectGarbage();
    }
    juce::String currentName() const { return loaded >= 0 ? shots[(size_t)loaded].file.getFileNameWithoutExtension() : juce::String(); }
    bool isIdle() const { return engine.isIdle(); }
    float outputDb() const { return levelDb.load(); } // one-knob pedals: level on top of the capture
    // One-knob pedals: the pedal's gain (dB, output over input) at a knob position.
    float gainDb(float knob) const
    {
        float top = -1e9f;
        for (auto& s : shots)
            top = std::max(top, s.loudness);
        return maxBoostDb + loudnessAt(knob) - top;
    }
    // A capture's gain on a guitar DI is its metadata loudness + 16.2 dB (measured on a boost set, every
    // capture within 0.1 dB; tests check it).
    static constexpr float loudnessToGainDb = 16.2f;
    // ponytail: the rated maximum boost of the boost circuit modelled here; make it per-folder if another one-knob pedal needs it.
    static constexpr float maxBoostDb = 20.0f;
    int getLatency() const { return engine.getLatency(); }

    void prepare(double sr, int maxBlock)
    {
        rate = sr;
        engine.prepare(sr, maxBlock);
        inGain.reset(sr, 0.02);
        outGain.reset(sr, 0.02);
        tone.prepare({ sr, (juce::uint32)maxBlock, 1 });
        toneDb = 1e9f;
        setTone(0.0f); // first assignment sizes the coefficient array; later ones don't allocate
    }

    // Audio thread, mono in place. Knobs 0..10.
    void process(float* x, int n, float drive, float toneKnob, float level)
    {
        if (!engine.hasModel() && engine.isIdle())
            return; // no captures: pass through
        if (oneKnob)
        {
            engine.process(x, n);
            outGain.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
            outGain.applyGain(x, n);
            return;
        }
        // ponytail: linear dB per knob step around the capture's settings; tune by ear.
        inGain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, 2.0f * (drive - base[0]))));
        inGain.applyGain(x, n);
        engine.process(x, n);
        setTone(juce::jlimit(-12.0f, 12.0f, 1.5f * (toneKnob - base[1])));
        juce::dsp::AudioBlock<float> block(&x, 1, (size_t)n);
        tone.process(juce::dsp::ProcessContextReplacing<float>(block));
        outGain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, 2.0f * (level - base[2]))));
        outGain.applyGain(x, n);
    }

private:
    // One-knob pedals: the captures' loudness along the knob, linear in dB between neighbours and
    // continued past the ends with the nearest slope (shots are sorted by knob).
    float loudnessAt(float knob) const
    {
        if (shots.size() < 2)
            return shots.empty() ? 0.0f : shots[0].loudness;
        size_t i = 1;
        while (i + 1 < shots.size() && shots[i].drive < knob)
            ++i;
        const auto &a = shots[i - 1], &b = shots[i];
        const float t = b.drive > a.drive ? (knob - a.drive) / (b.drive - a.drive) : 0.0f;
        return juce::jlimit(a.loudness - 40.0f, b.loudness + 12.0f, a.loudness + t * (b.loudness - a.loudness));
    }

    void setTone(float db)
    {
        if (std::abs(db - toneDb) < 0.01f)
            return;
        toneDb = db;
        *tone.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf(rate, 1500.0, 0.707, juce::Decibels::decibelsToGain(db));
    }

    NamEngine engine;
    juce::File folder;
    std::vector<Shot> shots;
    int loaded = -1;
    std::atomic<bool> oneKnob { false }; // set on the message thread, read by process()
    std::atomic<float> levelDb { 0 }; // one-knob pedals: output level relative to the loaded capture
    std::array<std::atomic<float>, 3> base {};
    double rate = 48000.0;
    juce::SmoothedValue<float> inGain, outGain;
    juce::dsp::IIR::Filter<float> tone;
    float toneDb = 1e9f;
};
