#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"
#include "NamPedal.h"
#include "CircuitAmp.h"

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
#define MJR_AIRWINDOWS(X) X(Dynamics3) X(TapeDelay2) X(StereoChorus) X(Reverb) X(Shimmer) // the last two: src/Shimmer.h
#define MJR_DECLARE(name) AudioEffect* mjr_create_##name(void*);
MJR_AIRWINDOWS(MJR_DECLARE)
#undef MJR_DECLARE

enum class FxStage { pre, eq, post };
// Knob units: plain 0..1 (Airwindows' own scale, shown 0..10); ms = a delay time (see tapeDelayTime);
// dB = our own gain after the effect (an Airwindows effect never sees it), or a band of our graphic EQ;
// mix = our own dry/wet (the effect outputs full wet): the dry stays whole up to half way, the wet comes in.
enum class FxUnit { plain, ms, db, mix };
struct FxKnob
{
    const char* name;
    float def;
    FxUnit unit = FxUnit::plain;
    float lo = 0, hi = 1;
};
inline constexpr int maxFxKnobs = 9;
struct FxInfo
{
    const char* id;
    const char* name;
    FxStage stage;
    AudioEffect* (*create)(void*); // nullptr: our own DSP (NAM pedals, graphic EQ)
    std::array<FxKnob, maxFxKnobs> knobs; // unused entries have a null name
    float wetGain = 1.0f;                 // FxUnit::mix: level of the full-wet signal against the dry
    int numKnobs() const
    {
        int n = 0;
        while (n < maxFxKnobs && knobs[(size_t)n].name != nullptr)
            ++n;
        return n;
    }
};
// Graphic EQ bands (Hz), one octave apart; the knob names below match.
inline constexpr float eqFreqs[9] = { 65, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
inline constexpr FxKnob eqBand(const char* name) { return { name, 0.0f, FxUnit::db, -12.0f, 12.0f }; }
#define MJR_VERB_WET 4.3f // Galactic3 full wet is 12.6 dB below its input on a guitar DI (measured in the tests)
inline constexpr FxKnob delayKnobs[6] = { { "TIME", 500.0f, FxUnit::ms, 80.0f, 1800.0f }, { "REPEATS", 0.4f }, { "TONE", 0.5f }, { "RESO", 0.0f }, { "FLUTTER", 0.1f }, { "MIX", 0.25f } };
inline constexpr FxInfo fxChain[] = {
    { "comp", "Compressor", FxStage::pre, mjr_create_Dynamics3, { { { "THRESHOLD", 0.6f }, { "ATTACK", 0.382f }, { "RELEASE", 0.618f }, { "MIX", 1.0f }, { "LEVEL", 0.0f, FxUnit::db, -12.0f, 18.0f } } } },
    // Boost and Drive: NAM captures of real pedals (MJRAmpProcessor::namPedals), knobs 0..10 like the pedals'.
    { "boost", "Boost", FxStage::pre, nullptr, { { { "BOOST", 0.65f } } } },
    { "drive", "Drive", FxStage::pre, nullptr, { { { "DRIVE", 0.7f }, { "TONE", 0.7f }, { "LEVEL", 0.7f } } } },
    { "preDelay", "Delay", FxStage::pre, mjr_create_TapeDelay2, { delayKnobs[0], delayKnobs[1], delayKnobs[2], delayKnobs[3], delayKnobs[4], delayKnobs[5] } },
    // EQ: our own 9-band graphic EQ (MJRAmpProcessor::graphicEq).
    { "eq", "Graphic EQ", FxStage::eq, nullptr,
      { eqBand("65 Hz"), eqBand("125 Hz"), eqBand("250 Hz"), eqBand("500 Hz"), eqBand("1 kHz"), eqBand("2 kHz"), eqBand("4 kHz"), eqBand("8 kHz"), eqBand("16 kHz") } },
    { "chorus", "Chorus", FxStage::post, mjr_create_StereoChorus, { { { "SPEED", 0.3f }, { "DEPTH", 0.4f } } } },
    { "postDelay", "Delay", FxStage::post, mjr_create_TapeDelay2, { delayKnobs[0], delayKnobs[1], delayKnobs[2], delayKnobs[3], delayKnobs[4], delayKnobs[5] } },
    { "reverb", "Reverb", FxStage::post, mjr_create_Reverb,
      { { { "DECAY", 0.5f }, { "SIZE", 0.6f }, { "TONE", 0.5f }, { "MOD", 0.4f }, { "MIX", 0.25f, FxUnit::mix } } }, MJR_VERB_WET },
    { "shimmer", "Shimmer", FxStage::post, mjr_create_Shimmer,
      { { { "DECAY", 0.7f }, { "SIZE", 0.8f }, { "TONE", 0.5f }, { "MOD", 0.3f }, { "MIX", 0.3f, FxUnit::mix }, { "SHIMMER", 0.5f } } }, MJR_VERB_WET },
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
    bool circuitMode() const { return ampMode->load() < 0.5f; } // the circuit model, else the NAM captures
    bool isIdle() const { return engine.isIdle() && namPedals[0].isIdle() && namPedals[1].isIdle(); }

    // NAM pedals, by fxChain index: Boost (one knob, captures named "... 65%") and Drive ("Drive 7 Tone 7 Level 9").
    static constexpr int namFx[2] = { 1, 2 };
    std::array<NamPedal, 2> namPedals; // the editor reads their folders and current captures
    NamPedal* namPedalFor(int fx)
    {
        for (size_t i = 0; i < 2; ++i)
            if (namFx[i] == fx)
                return &namPedals[i];
        return nullptr;
    }
    void setPedalFolder(int fx, const juce::File&); // remembered for the next start

    // User presets: the tone (capture, knobs, effects, IRs) as XML files, without this machine's folders.
    juce::File presetDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MJR Amp/Presets");
    juce::Array<juce::File> listPresets() const;
    bool savePreset(const juce::String& name); // overwrites a preset with the same name
    bool loadPreset(const juce::File&);
    juce::String currentPreset; // shown in the editor; kept in the session

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
    juce::ValueTree makeState(bool withFolders);
    void applyState(const juce::ValueTree&);
    void updateFilters();
    void updateGraphicEq();
    std::array<float, 3> pedalKnobs(int fx) const;

    NamEngine engine;
    CircuitAmp circuit; // amp A
    CircuitAmpFull circuitB { CircuitAmpFull::as1967 }, circuitC { CircuitAmpFull::lead1968 };
    // Amp D: amp C with the parameters fitted to the captures by nam-twin, read from fitted.json in the user's app
    // data folder ({"base": "B"|"C", "params": {name: value}}). Without the file it plays as C.
    static CircuitAmpFull::Design fittedDesign();
    CircuitAmpFull circuitD { fittedDesign() };
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
    std::atomic<float> *input, *output, *inputSource, *gate, *transpose, *ampOn, *ampMode, *jack, *circuitModel, *metroOn, *bpm, *beats, *metroLevel;

    void addMetronome(juce::AudioBuffer<float>&, int numOut);
    void runFx(FxStage, juce::AudioBuffer<float>&, int numChannels);
    std::array<std::atomic<float>*, numFx> fxOn {};
    std::array<std::array<std::atomic<float>*, maxFxKnobs>, numFx> fxKnob {};
    std::array<std::unique_ptr<AudioEffect>, numFx> fx; // see Processor.cpp: deleted where AudioEffect is complete
    juce::AudioBuffer<float> fxScratch;                 // right channel for the mono pre/eq stages
    juce::AudioBuffer<float> fxDry;                     // FxUnit::mix: the effect's input
    std::array<float, numFx> fxGain {};                 // last applied dB knob gain, ramped per block
    std::array<float, numFx> fxWet {}, fxDryGain {};    // last applied mix gains (FxUnit::mix), ramped per block
    std::array<juce::dsp::IIR::Filter<float>, 9> graphicEq;
    std::array<float, 9> graphicEqDb {};
    double metroSamples = 0; // free-running position, in samples since the last beat
    int metroBeat = -1;
    int clickPos = 1 << 30;
    float clickFreq = 1000;

    std::vector<float> tunerRing = std::vector<float>(1 << 14);
    std::atomic<int> tunerWrite { 0 };
    std::array<std::atomic<float>*, numIrSlots> irOn {}, irLevel {}, irPan {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MJRAmpProcessor)
};
