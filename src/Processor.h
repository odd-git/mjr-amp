#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"

struct Capture
{
    juce::String file, channel, level, voicing, pedal;
    std::array<float, 6> knobs {}; // presence, bass, middle, treble, volume1, volume2
    bool hasKnobs = false;
    juce::String displayName() const { return channel + " " + level + " " + voicing + (pedal.isEmpty() ? "" : " " + pedal); }
};

// Effect modules, in signal-chain order (same chain as Archetype: Plini X).
// DSP comes from Airwindows (MIT, libs/airwindows); knob values are the plugin's own 0..1 parameters.
class AudioEffect; // Airwindows base class, libs/airwindows/audioeffectx.h
#define MJR_AIRWINDOWS(X) X(Dynamics3) X(Density3) X(TapeDelay2) X(PearEQ) X(StereoChorus) X(Galactic3) X(Doublelay)
#define MJR_DECLARE(name) AudioEffect* mjr_create_##name(void*);
MJR_AIRWINDOWS(MJR_DECLARE)
#undef MJR_DECLARE

enum class FxStage { pre, eq, post };
struct FxKnob
{
    const char* name;
    float def;
};
struct FxInfo
{
    const char* id;
    const char* name;
    FxStage stage;
    AudioEffect* (*create)(void*); // nullptr: placeholder, audio passes through
    std::array<FxKnob, 6> knobs;   // unused entries have a null name
    int numKnobs() const
    {
        int n = 0;
        while (n < 6 && knobs[(size_t)n].name != nullptr)
            ++n;
        return n;
    }
};
inline constexpr FxKnob delayKnobs[6] = { { "TIME", 0.5f }, { "REPEATS", 0.4f }, { "TONE", 0.5f }, { "RESO", 0.0f }, { "FLUTTER", 0.1f }, { "MIX", 0.25f } };
inline constexpr FxInfo fxChain[] = {
    { "comp", "Compressor", FxStage::pre, mjr_create_Dynamics3, { { { "THRESHOLD", 0.6f }, { "ATTACK", 0.382f }, { "RELEASE", 0.618f }, { "MIX", 1.0f } } } },
    { "octaver", "Octaver", FxStage::pre, nullptr, {} },
    { "drive", "Drive", FxStage::pre, mjr_create_Density3, { { { "DRIVE", 0.3f }, { "LOW CUT", 0.0f }, { "LEVEL", 1.0f }, { "MIX", 1.0f } } } },
    { "preDelay", "Delay", FxStage::pre, mjr_create_TapeDelay2, { delayKnobs[0], delayKnobs[1], delayKnobs[2], delayKnobs[3], delayKnobs[4], delayKnobs[5] } },
    { "eq", "EQ", FxStage::eq, mjr_create_PearEQ,
      { { { "HIGH", 0.5f }, { "HI MID", 0.5f }, { "MID", 0.5f }, { "LO MID", 0.5f }, { "BASS", 0.5f }, { "SUB", 0.5f } } } },
    { "chorus", "Chorus", FxStage::post, mjr_create_StereoChorus, { { { "SPEED", 0.3f }, { "DEPTH", 0.4f } } } },
    { "postDelay", "Delay", FxStage::post, mjr_create_TapeDelay2, { delayKnobs[0], delayKnobs[1], delayKnobs[2], delayKnobs[3], delayKnobs[4], delayKnobs[5] } },
    { "reverb", "Reverb", FxStage::post, mjr_create_Galactic3,
      { { { "REPLACE", 0.5f }, { "BRIGHT", 0.5f }, { "DETUNE", 0.5f }, { "DEREZ", 1.0f }, { "SIZE", 0.6f }, { "MIX", 0.1f } } } },
    { "doubler", "Doubler", FxStage::post, mjr_create_Doublelay,
      { { { "DETUNE", 0.2f }, { "DELAY L", 0.1f }, { "DELAY R", 0.2f }, { "FEEDBACK", 0.0f }, { "MIX", 0.6f } } } },
};
inline constexpr int numFx = (int)std::size(fxChain);

class MJRAmpProcessor;
juce::AudioProcessorEditor* createMJREditor(MJRAmpProcessor&);

class MJRAmpProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numIrSlots = 4;
    static inline const char* knobIds[6] = { "presence", "bass", "middle", "treble", "volume1", "volume2" };

    MJRAmpProcessor();
    ~MJRAmpProcessor() override;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Message thread API (editor, tests)
    void setLibraryDir(const juce::File&);
    juce::File getLibraryDir() const { return libraryDir; }
    void setIrFile(int slot, const juce::File&);
    juce::File getIrFile(int slot) const { return irFiles[(size_t)slot]; }
    void poll(); // applies capture changes; called by the timer
    void resetKnobsToCapture(); // knobs back to the active capture's recorded settings
    bool isIdle() const { return engine.isIdle(); }

    std::vector<Capture> captures; // before params: the parameter layout is built from it
    juce::AudioProcessorValueTreeState params;
    std::atomic<float> inputPeak { 0 }, outputPeak { 0 };

    // Tuner: while active the amp is muted and the dry input is kept for analysis.
    std::atomic<bool> tunerActive { false };
    int copyTunerInput(float* dest, int n) const; // latest n input samples; returns how many
    double getSampleRate() const { return sampleRate; }

    // Metronome: beat counter for the editor's light (beat number, 0 = downbeat).
    std::atomic<int> beatCount { 0 }, beatInBar { 0 };

private:
    void timerCallback() override { poll(); }
    void loadCapture(int index, bool resetKnobs);
    void updateFilters();

    NamEngine engine;
    juce::File libraryDir;
    std::atomic<int> loadedCapture { -1 };
    std::array<std::atomic<float>, 6> baseline {}; // knob values the active capture was recorded with

    double sampleRate = 48000.0;
    juce::SmoothedValue<float> driveGain, outGain;
    juce::dsp::NoiseGate<float> noiseGate;
    std::array<juce::dsp::IIR::Filter<float>, 4> eq;
    std::array<float, 4> eqDb { 1e9f, 1e9f, 1e9f, 1e9f };

    std::array<juce::dsp::Convolution, numIrSlots> cab;
    std::array<juce::File, numIrSlots> irFiles;
    std::array<std::atomic<bool>, numIrSlots> irLoaded {};
    juce::AudioBuffer<float> dry, slotBuf;

    std::array<std::atomic<float>*, 6> knob {};
    std::atomic<float> *input, *output, *inputSource, *gate, *transpose, *metroOn, *bpm, *beats, *metroLevel;

    void addMetronome(juce::AudioBuffer<float>&, int numOut);
    void runFx(FxStage, juce::AudioBuffer<float>&, int numChannels);
    std::array<std::atomic<float>*, numFx> fxOn {};
    std::array<std::array<std::atomic<float>*, 6>, numFx> fxKnob {};
    std::array<std::unique_ptr<AudioEffect>, numFx> fx; // see Processor.cpp: deleted where AudioEffect is complete
    juce::AudioBuffer<float> fxScratch;                 // right channel for the mono pre/eq stages
    double metroSamples = 0; // free-running position, in samples since the last beat
    int metroBeat = -1;
    int clickPos = 1 << 30;
    float clickFreq = 1000;

    std::vector<float> tunerRing = std::vector<float>(1 << 14);
    std::atomic<int> tunerWrite { 0 };
    std::array<std::atomic<float>*, numIrSlots> irOn {}, irLevel {}, irPan {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MJRAmpProcessor)
};
