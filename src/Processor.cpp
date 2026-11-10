#include "Processor.h"
#include "BinaryData.h"
#include "audioeffectx.h"
#include "Shimmer.h"

AudioEffect* mjr_create_Reverb(void*) { return new MjrVerb(false); }
AudioEffect* mjr_create_Shimmer(void*) { return new MjrVerb(true); }

namespace
{
juce::File rememberedLibraryFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MJR Amp/library.txt");
}
juce::File rememberedPedalFile(int fx) // "drive.txt", "boost.txt"
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MJR Amp/" + juce::String(fxChain[fx].id) + ".txt");
}
constexpr int driveFx = 2, eqFx = 4; // fxChain indices of our own DSP: NAM drive pedal, graphic EQ

// TapeDelay2's Time knob sets a tape speed, not a time: the head moves `speed` slots per internal sample
// round an 88200-slot loop, the internal rate being sr / cycleEnd (about 44.1k). So the echo comes after
// 88200 / speed internal samples, with speed = 1 + 25 * A^4. Inverted here: milliseconds -> A.
float tapeDelayTime(float ms, double sr)
{
    const double cycleEnd = juce::jlimit(1.0, 4.0, std::floor(sr / 44100.0));
    const double speed = 88200.0 * cycleEnd / (sr * ms * 0.001);
    return (float)std::pow(juce::jlimit(0.0, 1.0, (speed - 1.0) / 25.0), 0.25);
}
static_assert(std::string_view(fxChain[driveFx].id) == "drive" && std::string_view(fxChain[eqFx].id) == "eq"
              && std::string_view(fxChain[MJRAmpProcessor::namFx[0]].id) == "boost" && MJRAmpProcessor::namFx[1] == driveFx);

std::vector<Capture> parseCaptures()
{
    std::vector<Capture> out;
    const auto json = juce::JSON::parse(juce::String::fromUTF8(BinaryData::captures_json, BinaryData::captures_jsonSize));
    for (const auto& c : *json["captures"].getArray())
    {
        Capture cap { c["file"], c["channel"], c["level"], c["voicing"], c["pedal"] };
        if (auto* k = c["knobs"].getDynamicObject())
        {
            cap.hasKnobs = true;
            for (int i = 0; i < 6; ++i)
                cap.knobs[(size_t)i] = (float)k->getProperty(MJRAmpProcessor::knobIds[i]);
        }
        out.push_back(cap);
    }
    return out;
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const std::vector<Capture>& captures)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    StringArray names;
    int defaultIndex = 0;
    for (auto& c : captures)
    {
        if (c.displayName() == "I Crunch BAL")
            defaultIndex = names.size();
        names.add(c.displayName());
    }
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "capture", 1 }, "Capture", names, defaultIndex));

    const char* knobNames[] = { "Presence", "Bass", "Middle", "Treble", "Volume I", "Volume II" };
    for (int i = 0; i < 6; ++i)
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { MJRAmpProcessor::knobIds[i], 1 }, knobNames[i],
                                                    NormalisableRange<float>(0.0f, 10.0f, 0.1f), 5.0f));

    auto db = [](const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(lo, hi, 0.1f), def,
                                                     AudioParameterFloatAttributes().withLabel("dB"));
    };
    l.add(db("input", "Input", -24.0f, 12.0f, 0.0f));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "inputSource", 1 }, "Input Source", StringArray { "Input 1", "Input 2" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "gate", 1 }, "Gate", NormalisableRange<float>(-100.0f, 0.0f, 0.5f), -100.0f,
                                                AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
                                                    [](float v, int) { return v <= -99.9f ? String("Off") : String(v, 1); })));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "transpose", 1 }, "Transpose", -12, 12, 0,
                                              AudioParameterIntAttributes().withLabel("st").withStringFromValueFunction(
                                                  [](int v, int) { return v == 0 ? String("Off") : (v > 0 ? "+" : "") + String(v); })));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "ampOn", 1 }, "Amp On", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "ampMode", 1 }, "Amp Model", StringArray { "Circuit", "Capture" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "jack", 1 }, "Input Jack", StringArray { "I", "II", "I+II" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "circuit", 1 }, "Circuit", StringArray { "A", "B", "C" }, 0));
    l.add(db("output", "Output", -24.0f, 12.0f, 0.0f));

    for (int s = 1; s <= MJRAmpProcessor::numIrSlots; ++s)
    {
        const String p = "ir" + String(s);
        l.add(std::make_unique<AudioParameterBool>(ParameterID { p + "On", 1 }, "IR " + String(s) + " On", s <= 2));
        l.add(db((p + "Level").toRawUTF8(), ("IR " + String(s) + " Level").toRawUTF8(), -36.0f, 6.0f, -3.0f));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "Pan", 1 }, "IR " + String(s) + " Pan",
                                                    NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    }
    for (auto& fx : fxChain)
    {
        const String name = String(fx.name) + (fx.stage == FxStage::pre ? " (pre)" : fx.stage == FxStage::post ? " (post)" : "");
        l.add(std::make_unique<AudioParameterBool>(ParameterID { String(fx.id) + "On", 1 }, name, false));
        for (int k = 0; k < fx.numKnobs(); ++k)
        {
            const auto& knob = fx.knobs[(size_t)k];
            const ParameterID id { String(fx.id) + "K" + String(k), 1 };
            const String knobName = name + " " + String(knob.name).toLowerCase();
            if (knob.unit == FxUnit::ms)
            {
                NormalisableRange<float> range(knob.lo, knob.hi, 1.0f);
                range.setSkewForCentre(500.0f); // finer steps for short, rhythmic times
                l.add(std::make_unique<AudioParameterFloat>(id, knobName, range, knob.def,
                                                            AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(
                                                                [](float v, int) { return String(roundToInt(v)); })));
            }
            else if (knob.unit == FxUnit::db)
                l.add(std::make_unique<AudioParameterFloat>(id, knobName, NormalisableRange<float>(knob.lo, knob.hi, 0.1f), knob.def,
                                                            AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
                                                                [](float v, int) { return String(v, 1); })));
            else
                l.add(std::make_unique<AudioParameterFloat>(id, knobName, NormalisableRange<float>(0.0f, 1.0f, 0.001f), knob.def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v * 10.0f, 1); })));
        }
    }
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "metroOn", 1 }, "Metronome", false));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "bpm", 1 }, "Tempo", NormalisableRange<float>(30.0f, 300.0f, 1.0f), 120.0f,
                                                AudioParameterFloatAttributes().withLabel("BPM")));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "beats", 1 }, "Beats per bar", 1, 12, 4));
    l.add(db("metroLevel", "Metronome Level", -36.0f, 0.0f, -12.0f));
    return l;
}
} // namespace

MJRAmpProcessor::MJRAmpProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true) // pick the guitar's channel with inputSource
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "MJRAmp", makeLayout(captures = parseCaptures()))
{
    for (int i = 0; i < 6; ++i)
        knob[(size_t)i] = params.getRawParameterValue(knobIds[i]);
    input = params.getRawParameterValue("input");
    output = params.getRawParameterValue("output");
    inputSource = params.getRawParameterValue("inputSource");
    gate = params.getRawParameterValue("gate");
    transpose = params.getRawParameterValue("transpose");
    ampOn = params.getRawParameterValue("ampOn");
    ampMode = params.getRawParameterValue("ampMode");
    jack = params.getRawParameterValue("jack");
    circuitModel = params.getRawParameterValue("circuit");
    metroOn = params.getRawParameterValue("metroOn");
    for (int i = 0; i < numFx; ++i)
    {
        fxOn[(size_t)i] = params.getRawParameterValue(juce::String(fxChain[i].id) + "On");
        for (int k = 0; k < fxChain[i].numKnobs(); ++k)
            fxKnob[(size_t)i][(size_t)k] = params.getRawParameterValue(juce::String(fxChain[i].id) + "K" + juce::String(k));
    }
    bpm = params.getRawParameterValue("bpm");
    beats = params.getRawParameterValue("beats");
    metroLevel = params.getRawParameterValue("metroLevel");
    for (int s = 0; s < numIrSlots; ++s)
    {
        const juce::String p = "ir" + juce::String(s + 1);
        irOn[(size_t)s] = params.getRawParameterValue(p + "On");
        irLevel[(size_t)s] = params.getRawParameterValue(p + "Level");
        irPan[(size_t)s] = params.getRawParameterValue(p + "Pan");
    }

    auto remembered = rememberedLibraryFile().loadFileAsString().trim();
    setLibraryDir(remembered.isNotEmpty() ? juce::File(remembered)
                                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("MJR Amp"));
    const auto pedals = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Music Production/NAM Profile/Pedals");
    for (int fx : namFx)
    {
        const auto remembered = rememberedPedalFile(fx).loadFileAsString().trim();
        const char* fallback = fx == driveFx ? "Ibanez TS9 Tube Screamer" : "Fortin 33";
        setPedalFolder(fx, remembered.isNotEmpty() ? juce::File(remembered) : pedals.getChildFile(fallback));
    }
    startTimerHz(20);
}

void MJRAmpProcessor::setPedalFolder(int fx, const juce::File& dir)
{
    if (auto* pedal = namPedalFor(fx); pedal != nullptr && pedal->setFolder(dir) > 0)
        rememberedPedalFile(fx).create(), rememberedPedalFile(fx).replaceWithText(dir.getFullPathName());
}

MJRAmpProcessor::~MJRAmpProcessor() { stopTimer(); }

void MJRAmpProcessor::setLibraryDir(const juce::File& dir)
{
    libraryDir = dir;
    if (dir.isDirectory())
        rememberedLibraryFile().create(), rememberedLibraryFile().replaceWithText(dir.getFullPathName());

    // Default cab: the library's IRs, until a session says otherwise.
    auto wavs = dir.findChildFiles(juce::File::findFiles, false, "*.wav");
    wavs.sort();
    for (int s = 0; s < numIrSlots; ++s)
        if (irFiles[(size_t)s] == juce::File() && s < wavs.size())
            setIrFile(s, wavs[s]);

    loadedCapture = -1; // reload from the new folder on next poll
}

void MJRAmpProcessor::setIrFile(int s, const juce::File& f)
{
    irFiles[(size_t)s] = f;
    irLoaded[(size_t)s] = f.existsAsFile();
    if (f.existsAsFile())
        // 200 ms is plenty for a cab (the library's 2 s IR has no energy left after 100 ms); convolution cost grows with length.
        cab[(size_t)s].loadImpulseResponse(f, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                           (size_t)(0.2 * 48000), juce::dsp::Convolution::Normalise::yes);
}

void MJRAmpProcessor::poll()
{
    const int idx = ((juce::AudioParameterChoice*)params.getParameter("capture"))->getIndex();
    if (idx != loadedCapture)
        loadCapture(idx, true);
    engine.collectGarbage();
    for (size_t p = 0; p < namPedals.size(); ++p)
    {
        const auto k = pedalKnobs(namFx[p]);
        namPedals[p].update(k[0], k[1], k[2]);
    }
}

void MJRAmpProcessor::loadCapture(int idx, bool resetKnobs)
{
    const auto& c = captures[(size_t)idx];
    engine.load(libraryDir.getChildFile(c.file));
    // Captures with known settings: knobs jump there, so the added DSP starts neutral.
    // Unknown settings: wherever the knobs are now becomes neutral.
    for (int i = 0; i < 6; ++i)
        baseline[(size_t)i] = c.hasKnobs ? c.knobs[(size_t)i] : knob[(size_t)i]->load();
    loadedCapture = idx;
    if (resetKnobs)
        resetKnobsToCapture();
}

void MJRAmpProcessor::resetKnobsToCapture()
{
    const auto& c = captures[(size_t)loadedCapture.load()];
    if (!c.hasKnobs)
        return;
    for (int i = 0; i < 6; ++i)
    {
        auto* p = params.getParameter(knobIds[i]);
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(c.knobs[(size_t)i]));
        p->endChangeGesture();
    }
}

void MJRAmpProcessor::prepareToPlay(double sr, int maxBlock)
{
    sampleRate = sr;
    engine.prepare(sr, maxBlock);
    circuit.prepare(sr, maxBlock);
    circuitB.prepare(sr, maxBlock);
    circuitC.prepare(sr, maxBlock);
    for (auto& pedal : namPedals)
        pedal.prepare(sr, maxBlock);
    // ponytail: the larger of the two amp models' latencies, and the drive's resampler latency (host rate != 48k),
    // are reported whatever is active: a host can't follow latency changes at runtime.
    setLatencySamples(std::max({ engine.getLatency(), circuit.getLatency(), circuitB.getLatency() }) + namPedals[0].getLatency() + namPedals[1].getLatency());

    driveGain.reset(sr, 0.02);
    outGain.reset(sr, 0.02);
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlock, 1 };
    for (auto& f : eq)
        f.prepare(spec), f.reset();
    noiseGate.prepare(spec);
    noiseGate.setRatio(10.0f);
    noiseGate.setAttack(1.0f);
    noiseGate.setRelease(120.0f);
    eqDb.fill(1e9f);
    updateFilters(); // first assignment sizes the coefficient arrays, later ones don't allocate
    for (auto& c : cab)
        c.prepare(spec);
    dry.setSize(1, maxBlock);
    slotBuf.setSize(1, maxBlock);
    fxScratch.setSize(1, maxBlock);
    fxDry.setSize(2, maxBlock);
    fxWet.fill(-1.0f), fxDryGain.fill(-1.0f);
    fxGain.fill(1.0f);
    for (auto& f : graphicEq)
        f.prepare(spec), f.reset();
    graphicEqDb.fill(1e9f);
    updateGraphicEq(); // first assignment sizes the coefficient arrays, later ones don't allocate
    for (int i = 0; i < numFx; ++i) // fresh instances: no stale delay/reverb tails across a restart
        if (fxChain[i].create != nullptr)
        {
            fx[(size_t)i].reset(fxChain[i].create(nullptr));
            fx[(size_t)i]->setSampleRate((float)sr);
        }
}

bool MJRAmpProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MJRAmpProcessor::updateFilters()
{
    using C = juce::dsp::IIR::ArrayCoefficients<float>;
    // ponytail: linear 1.5 dB per knob step around the capture's setting; tune by ear.
    for (size_t b = 0; b < 4; ++b)
    {
        const float db = juce::jlimit(-12.0f, 12.0f, 1.5f * (knob[b]->load() - baseline[b]));
        if (std::abs(db - eqDb[b]) < 0.01f)
            continue;
        eqDb[b] = db;
        if (std::abs(db) <= 0.01f)
            eq[b].reset(); // bypassed from now on: no stale state when it comes back
        const float g = juce::Decibels::decibelsToGain(db);
        switch (b)
        {
            case 0: *eq[b].coefficients = C::makeHighShelf(sampleRate, 6000.0, 0.707, g); break; // presence
            case 1: *eq[b].coefficients = C::makeLowShelf(sampleRate, 100.0, 0.707, g); break;   // bass
            case 2: *eq[b].coefficients = C::makePeakFilter(sampleRate, 650.0, 0.7, g); break;   // middle
            default: *eq[b].coefficients = C::makeHighShelf(sampleRate, 3000.0, 0.707, g); break; // treble
        }
    }
}

void MJRAmpProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numOut = getTotalNumOutputChannels();
    // Guitar is mono: the chosen input channel becomes channel 0, which the chain processes.
    if (inputSource->load() > 0.5f && getTotalNumInputChannels() > 1)
        buffer.copyFrom(0, 0, buffer, 1, 0, n);
    float* mono = buffer.getWritePointer(0);
    inputPeak = std::max(inputPeak.load(), buffer.getMagnitude(0, 0, n));
    const bool tuning = tunerActive.load();
    if (tuning)
    {
        const int mask = (int)tunerRing.size() - 1, w = tunerWrite.load();
        for (int i = 0; i < n; ++i)
            tunerRing[(size_t)((w + i) & mask)] = mono[i];
        tunerWrite.store((w + n) & mask);
    }

    // Volume knobs drive the capture harder or softer than it was recorded.
    const int cap = loadedCapture;
    const bool modelled = circuitMode();
    float driveDb = 0.0f;
    if (cap >= 0 && !modelled)
    {
        const bool i = captures[(size_t)cap].channel == "I";
        const float now = i ? knob[4]->load() : 0.5f * (knob[4]->load() + knob[5]->load());
        const float base = i ? baseline[4].load() : 0.5f * (baseline[4] + baseline[5]);
        driveDb = juce::jlimit(-24.0f, 24.0f, 3.0f * (now - base)); // ponytail: 3 dB per step, tune by ear
    }
    if (const float threshold = gate->load(); threshold > -99.9f)
    {
        noiseGate.setThreshold(threshold);
        juce::dsp::AudioBlock<float> block(&mono, 1, (size_t)n);
        noiseGate.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // ponytail: transpose is a placeholder pass-through; pitch shifter (e.g. signalsmith-stretch, MIT) goes here, right after the gate.
    juce::ignoreUnused(transpose);

    runFx(FxStage::pre, buffer, 1);

    // Amp switched off on the panel: the pedals go straight to the cab.
    // ponytail: hard switch (may click); crossfade like the capture swap if it bothers.
    const bool amp = ampOn->load() > 0.5f;
    driveGain.setTargetValue(juce::Decibels::decibelsToGain(input->load() + (amp ? driveDb : 0.0f)));
    driveGain.applyGain(mono, n);

    if (amp && modelled)
    {
        auto run = [&](auto& amp)
        {
            amp.setControls(knob[0]->load(), knob[1]->load(), knob[2]->load(), knob[3]->load(), knob[4]->load(),
                            knob[5]->load(), (CircuitAmp::Jack)juce::roundToInt(jack->load()));
            amp.process(mono, n);
        };
        // ponytail: hard switch between models (may click), like the amp on/off switch
        switch (juce::roundToInt(circuitModel->load()))
        {
            case 1: run(circuitB); break;
            case 2: run(circuitC); break;
            default: run(circuit); break;
        }
    }
    else if (amp)
        engine.process(mono, n);
    else
        juce::FloatVectorOperations::multiply(mono, 0.25f, n); // -12 dB: a raw DI into the cab is far hotter than the levelled amp

    updateFilters();
    if (amp && !modelled) // the circuit's knobs are in the circuit
    {
        juce::dsp::AudioBlock<float> block(&mono, 1, (size_t)n);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        for (size_t b = 0; b < eq.size(); ++b)
            if (std::abs(eqDb[b]) > 0.01f) // knobs at the capture's settings: that band is flat, skip it
                eq[b].process(ctx);
    }

    runFx(FxStage::eq, buffer, 1);

    // Cab: sum of the active IR slots, each with level and constant-power pan.
    dry.copyFrom(0, 0, mono, n);
    bool anyIr = false;
    for (int ch = 0; ch < numOut; ++ch)
        buffer.clear(ch, 0, n);
    for (size_t s = 0; s < numIrSlots; ++s)
    {
        if (!irLoaded[s] || irOn[s]->load() < 0.5f)
            continue;
        anyIr = true;
        slotBuf.copyFrom(0, 0, dry, 0, 0, n);
        float* p = slotBuf.getWritePointer(0);
        juce::dsp::AudioBlock<float> block(&p, 1, (size_t)n);
        cab[s].process(juce::dsp::ProcessContextReplacing<float>(block));
        // JUCE normalises every IR to an energy of 0.125 (-18 dB). Win back 12 of those dB:
        // the default rig (two IRs) then peaks around -8 dBFS, leaving headroom for hard picking.
        const float g = 4.0f * juce::Decibels::decibelsToGain(irLevel[s]->load());
        if (numOut == 1)
            buffer.addFrom(0, 0, slotBuf, 0, 0, n, g);
        else
        {
            const float a = (irPan[s]->load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            buffer.addFrom(0, 0, slotBuf, 0, 0, n, g * std::cos(a) * juce::MathConstants<float>::sqrt2);
            buffer.addFrom(1, 0, slotBuf, 0, 0, n, g * std::sin(a) * juce::MathConstants<float>::sqrt2);
        }
    }
    if (!anyIr)
        for (int ch = 0; ch < numOut; ++ch)
            buffer.copyFrom(ch, 0, dry, 0, 0, n);

    runFx(FxStage::post, buffer, numOut);

    outGain.setTargetValue(juce::Decibels::decibelsToGain(output->load()));
    if (!outGain.isSmoothing())
        for (int ch = 0; ch < numOut; ++ch)
            buffer.applyGain(ch, 0, n, outGain.getTargetValue());
    else
    {
        float* const* out = buffer.getArrayOfWritePointers();
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            for (int ch = 0; ch < numOut; ++ch)
                out[ch][i] *= g;
        }
    }
    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
    if (tuning) // silent tuning; the metronome keeps playing
        for (int ch = 0; ch < numOut; ++ch)
            buffer.clear(ch, 0, n);
    addMetronome(buffer, numOut);
    outputPeak = std::max(outputPeak.load(), buffer.getMagnitude(0, n));
}

void MJRAmpProcessor::runFx(FxStage stage, juce::AudioBuffer<float>& buffer, int numChannels)
{
    const int n = buffer.getNumSamples();
    for (size_t i = 0; i < (size_t)numFx; ++i)
    {
        if (fxChain[i].stage != stage || fxOn[i]->load() < 0.5f)
            continue;
        if (i == (size_t)eqFx)
        {
            updateGraphicEq();
            float* mono = buffer.getWritePointer(0); // the eq stage is mono, between amp and cab
            juce::dsp::AudioBlock<float> block(&mono, 1, (size_t)n);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            for (size_t b = 0; b < graphicEq.size(); ++b)
                if (std::abs(graphicEqDb[b]) > 0.01f) // flat bands cost nothing
                    graphicEq[b].process(ctx);
            continue;
        }
        if (auto* pedal = namPedalFor((int)i))
        {
            const auto k = pedalKnobs((int)i);
            pedal->process(buffer.getWritePointer(0), n, k[0], k[1], k[2]);
            continue;
        }
        auto* effect = fx[i].get();
        if (effect == nullptr)
            continue;
        float gainDb = 0, mix = -1; // our dB knobs (e.g. the compressor's makeup) act after the effect
        for (int k = 0; k < fxChain[i].numKnobs(); ++k)
        {
            const float v = fxKnob[i][(size_t)k]->load();
            switch (fxChain[i].knobs[(size_t)k].unit)
            {
                case FxUnit::db: gainDb += v; break;
                case FxUnit::ms: effect->setParameter(k, tapeDelayTime(v, sampleRate)); break;
                case FxUnit::plain: effect->setParameter(k, v); break;
                case FxUnit::mix: mix = v; break;
            }
        }
        if (mix >= 0)
            for (int c = 0; c < numChannels; ++c)
                fxDry.copyFrom(c, 0, buffer, c, 0, n);
        // Airwindows is stereo; a mono stage feeds it the same signal twice and keeps the left side.
        if (numChannels < 2)
            fxScratch.copyFrom(0, 0, buffer, 0, 0, n);
        float* ch[2] = { buffer.getWritePointer(0), numChannels < 2 ? fxScratch.getWritePointer(0) : buffer.getWritePointer(1) };
        effect->processReplacing(ch, ch, n);
        if (mix >= 0) // dry whole up to half way and the wet coming in, then the dry going out
        {
            const float wet = std::min(1.0f, 2.0f * mix) * fxChain[i].wetGain, dryG = std::min(1.0f, 2.0f * (1.0f - mix));
            const float w0 = fxWet[i] < 0 ? wet : fxWet[i], d0 = fxDryGain[i] < 0 ? dryG : fxDryGain[i]; // first block: no ramp
            for (int c = 0; c < numChannels; ++c)
            {
                buffer.applyGainRamp(c, 0, n, w0, wet);
                buffer.addFromWithRamp(c, 0, fxDry.getReadPointer(c), n, d0, dryG);
            }
            fxWet[i] = wet, fxDryGain[i] = dryG;
        }
        if (const float g = juce::Decibels::decibelsToGain(gainDb); g != 1.0f || fxGain[i] != 1.0f)
        {
            for (int c = 0; c < numChannels; ++c)
                buffer.applyGainRamp(c, 0, n, fxGain[i], g);
            fxGain[i] = g;
        }
    }
}

// A NAM pedal's knobs on its own 0..10 scale; knobs it doesn't have read 0.
std::array<float, 3> MJRAmpProcessor::pedalKnobs(int fx) const
{
    std::array<float, 3> k {};
    for (int j = 0; j < std::min(3, fxChain[fx].numKnobs()); ++j)
        k[(size_t)j] = fxKnob[(size_t)fx][(size_t)j]->load() * 10.0f;
    return k;
}

void MJRAmpProcessor::updateGraphicEq()
{
    for (size_t b = 0; b < graphicEq.size(); ++b)
    {
        const float db = fxKnob[eqFx][b]->load();
        if (std::abs(db - graphicEqDb[b]) < 0.01f)
            continue;
        graphicEqDb[b] = db;
        if (std::abs(db) <= 0.01f)
            graphicEq[b].reset(); // bypassed from now on: no stale state when it comes back
        // Q 1.4: about an octave wide, like the bands of a hardware graphic EQ.
        *graphicEq[b].coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
            sampleRate, std::min((double)eqFreqs[b], sampleRate * 0.45), 1.4, juce::Decibels::decibelsToGain(db));
    }
}

int MJRAmpProcessor::copyTunerInput(float* dest, int n) const
{
    const int size = (int)tunerRing.size(), mask = size - 1;
    n = std::min(n, size);
    const int end = tunerWrite.load();
    for (int i = 0; i < n; ++i) // ponytail: may tear against the writer for one block; harmless for a tuner
        dest[i] = tunerRing[(size_t)((end - n + i + size) & mask)];
    return n;
}

void MJRAmpProcessor::addMetronome(juce::AudioBuffer<float>& buffer, int numOut)
{
    const int n = buffer.getNumSamples();
    if (metroOn->load() < 0.5f)
    {
        metroBeat = -1;
        metroSamples = 0;
        clickPos = 1 << 30;
        return;
    }

    // Follow the host while its transport runs; otherwise free-running at our tempo.
    double tempo = bpm->load(), ppq = -1, barStart = 0;
    int perBar = (int)beats->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition(); pos && pos->getIsPlaying() && pos->getPpqPosition())
        {
            ppq = *pos->getPpqPosition();
            tempo = pos->getBpm().orFallback(tempo);
            barStart = pos->getPpqPositionOfLastBarStart().orFallback(0.0);
            if (auto ts = pos->getTimeSignature())
                perBar = ts->numerator;
        }
    const double samplesPerBeat = sampleRate * 60.0 / tempo;
    const float gain = juce::Decibels::decibelsToGain(metroLevel->load());
    const int clickLen = (int)(0.03 * sampleRate);
    float* const* out = buffer.getArrayOfWritePointers();

    for (int i = 0; i < n; ++i)
    {
        int beat;
        if (ppq >= 0)
            beat = (int)std::floor(ppq + i / samplesPerBeat - barStart);
        else
        {
            if (metroBeat >= 0 && (metroSamples += 1.0) >= samplesPerBeat)
                metroSamples -= samplesPerBeat;
            beat = metroSamples < 1.0 ? metroBeat + 1 : metroBeat;
        }
        if (beat != metroBeat)
        {
            metroBeat = beat;
            const int inBar = ((beat % perBar) + perBar) % perBar;
            clickPos = 0;
            clickFreq = inBar == 0 ? 1600.0f : 1000.0f;
            beatInBar = inBar;
            ++beatCount;
        }
        if (clickPos < clickLen)
        {
            const float t = (float)clickPos++ / (float)sampleRate;
            const float v = gain * std::sin(juce::MathConstants<float>::twoPi * clickFreq * t) * std::exp(-t / 0.006f);
            for (int ch = 0; ch < numOut; ++ch)
                out[ch][i] += v;
        }
    }
}

// Session state = parameters + IR files + preset name; the folders only when they belong (a session, not a preset).
juce::ValueTree MJRAmpProcessor::makeState(bool withFolders)
{
    auto state = params.copyState();
    if (withFolders)
    {
        state.setProperty("libraryDir", libraryDir.getFullPathName(), nullptr);
        for (int fx : namFx)
            state.setProperty(juce::String(fxChain[fx].id) + "Dir", namPedalFor(fx)->getFolder().getFullPathName(), nullptr);
        state.setProperty("preset", currentPreset, nullptr);
    }
    for (int s = 0; s < numIrSlots; ++s)
        state.setProperty("irFile" + juce::String(s + 1), irFiles[(size_t)s].getFullPathName(), nullptr);
    return state;
}

void MJRAmpProcessor::applyState(const juce::ValueTree& state)
{
    params.replaceState(state);
    for (int s = 0; s < numIrSlots; ++s)
    {
        const auto path = state.getProperty("irFile" + juce::String(s + 1)).toString();
        juce::File f = path.isEmpty() ? juce::File() : juce::File(path);
        if (path.isNotEmpty() && !f.existsAsFile()) // moved or another machine: same name in the library
            f = libraryDir.getChildFile(f.getFileName());
        setIrFile(s, f);
    }
    for (int fx : namFx)
        if (const auto dir = state.getProperty(juce::String(fxChain[fx].id) + "Dir").toString(); dir.isNotEmpty())
            setPedalFolder(fx, juce::File(dir));
    if (const auto dir = state.getProperty("libraryDir").toString(); dir.isNotEmpty())
        setLibraryDir(juce::File(dir));
    if (state.hasProperty("preset"))
        currentPreset = state.getProperty("preset").toString();
    // Keep the stored knobs: they are relative to the capture's own settings.
    loadCapture(((juce::AudioParameterChoice*)params.getParameter("capture"))->getIndex(), false);
}

void MJRAmpProcessor::getStateInformation(juce::MemoryBlock& dest) { copyXmlToBinary(*makeState(true).createXml(), dest); }

void MJRAmpProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml != nullptr && xml->hasTagName(params.state.getType()))
        applyState(juce::ValueTree::fromXml(*xml));
}

juce::Array<juce::File> MJRAmpProcessor::listPresets() const
{
    auto files = presetDir.findChildFiles(juce::File::findFiles, false, "*.mjrpreset");
    files.sort();
    return files;
}

bool MJRAmpProcessor::savePreset(const juce::String& name)
{
    const auto file = presetDir.getChildFile(juce::File::createLegalFileName(name.trim()) + ".mjrpreset");
    if (name.trim().isEmpty() || !presetDir.createDirectory())
        return false;
    if (!makeState(false).createXml()->writeTo(file))
        return false;
    currentPreset = file.getFileNameWithoutExtension();
    return true;
}

bool MJRAmpProcessor::loadPreset(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(params.state.getType()))
        return false;
    applyState(juce::ValueTree::fromXml(*xml));
    currentPreset = file.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessorEditor* MJRAmpProcessor::createEditor() { return createMJREditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MJRAmpProcessor(); }
