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
    std::array<juce::dsp::IIR::Filter<float>, 4> eq;
    std::array<float, 4> eqDb { 1e9f, 1e9f, 1e9f, 1e9f };

    std::array<juce::dsp::Convolution, numIrSlots> cab;
    std::array<juce::File, numIrSlots> irFiles;
    std::array<std::atomic<bool>, numIrSlots> irLoaded {};
    juce::AudioBuffer<float> dry, slotBuf;

    std::array<std::atomic<float>*, 6> knob {};
    std::atomic<float> *input, *output;
    std::array<std::atomic<float>*, numIrSlots> irOn {}, irLevel {}, irPan {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MJRAmpProcessor)
};
