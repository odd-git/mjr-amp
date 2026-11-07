// Headless checks. Usage: mjr-test <capture library dir> <DI wav>
#include "../src/Processor.h"
#include "../src/Pitch.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static void waitIdle(MJRAmpProcessor& p, int block)
{
    juce::AudioBuffer<float> silence(2, block);
    juce::MidiBuffer midi;
    p.poll(); // queues the capture load, if the parameter changed
    for (int i = 0; i < 2000 && !p.isIdle(); ++i)
    {
        juce::Thread::sleep(2);
        silence.clear();
        p.processBlock(silence, midi); // the swap happens on the audio thread
    }
}

static void run(MJRAmpProcessor& p, juce::AudioBuffer<float>& buf, int block)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        const int n = std::min(block, buf.getNumSamples() - pos);
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), pos, n);
        p.processBlock(view, midi);
    }
}

static void setParam(MJRAmpProcessor& p, const char* id, float value)
{
    auto* prm = p.params.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 3)
        return std::cerr << "usage: mjr-test <library dir> <DI wav>\n", 2;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File lib = cwd.getChildFile(argv[1]);
    const int block = 256;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(cwd.getChildFile(argv[2])));
    if (reader == nullptr)
        return std::cerr << "cannot read DI\n", 2;
    juce::AudioBuffer<float> di(1, (int)reader->lengthInSamples);
    reader->read(&di, 0, di.getNumSamples(), 0, true, false);

    // 1. Every capture loads and makes sound.
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        p.prepareToPlay(48000, block);
        auto* choice = (juce::AudioParameterChoice*)p.params.getParameter("capture");
        for (int i = 0; i < choice->choices.size(); ++i)
        {
            CHECK(lib.getChildFile(p.captures[(size_t)i].file).existsAsFile(), "missing file " << p.captures[(size_t)i].file);
            *choice = i;
            waitIdle(p, block);
            juce::AudioBuffer<float> buf(2, 48000 / 2);
            buf.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, buf.getNumSamples()); // the DI starts with 1 s of silence
            run(p, buf, block);
            const float peak = buf.getMagnitude(0, 0, buf.getNumSamples());
            CHECK(std::isfinite(peak) && peak > 1e-3f, "capture " << i << " silent or invalid: " << peak);
        }
        std::cout << "captures: " << choice->choices.size() << " checked\n";
    }

    // 2. Null test: knobs on the capture's settings, cab off -> identical to the raw NAM model.
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        for (int s = 1; s <= MJRAmpProcessor::numIrSlots; ++s)
            setParam(p, ("ir" + juce::String(s) + "On").toRawUTF8(), 0.0f);
        p.prepareToPlay(48000, block);
        waitIdle(p, block); // loads the default capture and sets the knobs to its settings

        const auto& cap = p.captures[(size_t)((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex()];
        auto raw = nam::get_dsp(std::filesystem::path(lib.getChildFile(cap.file).getFullPathName().toStdString()));
        raw->ResetAndPrewarm(48000, block + 16);
        const float g = juce::Decibels::decibelsToGain(NamEngine::targetLoudnessDb - (float)raw->GetLoudness());

        // WaveNet has a finite receptive field: after 1 s of silence both models are in the same state.
        juce::AudioBuffer<float> buf(2, 48000 + di.getNumSamples());
        buf.clear();
        buf.copyFrom(0, 48000, di, 0, 0, di.getNumSamples());
        std::vector<float> ref((size_t)buf.getNumSamples());
        juce::FloatVectorOperations::copy(ref.data(), buf.getReadPointer(0), buf.getNumSamples());
        for (int pos = 0; pos < buf.getNumSamples(); pos += block)
        {
            float* in = ref.data() + pos;
            raw->process(&in, &in, std::min(block, buf.getNumSamples() - pos));
        }
        run(p, buf, block);

        float maxDiff = 0, peak = 0;
        for (int i = 48000; i < buf.getNumSamples(); ++i)
        {
            maxDiff = std::max(maxDiff, std::abs(buf.getSample(0, i) - ref[(size_t)i] * g));
            peak = std::max(peak, std::abs(ref[(size_t)i] * g));
        }
        std::cout << "null test: max diff " << maxDiff << " (peak " << peak << ")\n";
        CHECK(peak > 1e-3f && maxDiff < 1e-4f, "null test");
        CHECK(std::abs(buf.getSample(0, 48100) - buf.getSample(1, 48100)) < 1e-6f, "dry output not on both channels");

        // Knobs away from the capture's settings must change the sound.
        setParam(p, "bass", cap.knobs[1] + 3.0f);
        buf.clear();
        buf.copyFrom(0, 48000, di, 0, 0, di.getNumSamples());
        run(p, buf, block);
        float diff = 0;
        for (int i = 48000 + 4800; i < buf.getNumSamples(); ++i)
            diff = std::max(diff, std::abs(buf.getSample(0, i) - ref[(size_t)i] * g));
        CHECK(diff > 1e-3f, "bass knob has no effect");
    }

    // 3. Host at 44.1 kHz: resampled, reports latency, still sounds; cab on.
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        p.prepareToPlay(44100, block);
        waitIdle(p, block);
        juce::AudioBuffer<float> buf(2, 22050);
        buf.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, buf.getNumSamples()); // the DI starts with 1 s of silence
        run(p, buf, block);
        const float peak = buf.getMagnitude(0, buf.getNumSamples());
        std::cout << "44.1k: latency " << p.getLatencySamples() << ", peak " << peak << "\n";
        CHECK(p.getLatencySamples() > 0 && std::isfinite(peak) && peak > 1e-3f, "44.1k path");
        CHECK(p.getIrFile(0).existsAsFile() && p.getIrFile(1).existsAsFile(), "default IRs not picked from library");
    }

    // 4. State roundtrip keeps capture, knobs and IR files.
    {
        MJRAmpProcessor a;
        a.setLibraryDir(lib);
        *(juce::AudioParameterChoice*)a.params.getParameter("capture") = 5;
        a.poll();
        setParam(a, "treble", 9.0f);
        a.setIrFile(2, a.getIrFile(0));
        juce::MemoryBlock state;
        a.getStateInformation(state);

        MJRAmpProcessor b;
        b.setStateInformation(state.getData(), (int)state.getSize());
        b.poll();
        CHECK(((juce::AudioParameterChoice*)b.params.getParameter("capture"))->getIndex() == 5, "capture not restored");
        CHECK(std::abs(b.params.getRawParameterValue("treble")->load() - 9.0f) < 1e-4f, "knob not restored (or reset)");
        CHECK(b.getIrFile(2) == a.getIrFile(0), "IR file not restored");
    }

    // 6. Tuner: guitar-like tones (fundamental + harmonics) within 1 cent.
    for (double f : { 61.74, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 987.77 })
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            std::vector<float> x(4096);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double)i / sr, w = juce::MathConstants<double>::twoPi * f * t;
                x[i] = (float)(0.3 * std::sin(w) + 0.2 * std::sin(2 * w + 0.5) + 0.1 * std::sin(3 * w + 1.0));
            }
            const double got = detectPitch(x.data(), (int)x.size(), sr);
            const double cents = got > 0 ? 1200 * std::log2(got / f) : 1e9;
            CHECK(std::abs(cents) < 1.0, "tuner " << f << " Hz @ " << sr << ": got " << got);
        }
    {
        std::vector<float> quiet(4096, 0.0f);
        CHECK(detectPitch(quiet.data(), 4096, 48000) == 0, "tuner finds pitch in silence");
    }

    // 7. Metronome: free-running clicks on the beat, accent on the downbeat; tuner mutes the amp only.
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        for (int s = 1; s <= MJRAmpProcessor::numIrSlots; ++s)
            setParam(p, ("ir" + juce::String(s) + "On").toRawUTF8(), 0.0f);
        p.prepareToPlay(48000, block);
        waitIdle(p, block);
        setParam(p, "metroOn", 1.0f);
        setParam(p, "bpm", 120.0f);
        setParam(p, "beats", 4.0f);
        setParam(p, "metroLevel", 0.0f);
        p.tunerActive = true;

        juce::AudioBuffer<float> buf(2, 48000 * 2 + 100);
        buf.clear();
        buf.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, 48000); // guitar playing: must not be heard
        const int before = p.beatCount;
        run(p, buf, block);
        std::vector<int> onsets;
        for (int i = 0, last = -100000; i < buf.getNumSamples(); ++i)
            if (std::abs(buf.getSample(0, i)) > 0.05f && i - last > 1000)
                onsets.push_back(i), last = i;
        std::cout << "metronome onsets:";
        for (int o : onsets)
            std::cout << " " << o;
        std::cout << "\n";
        CHECK(onsets.size() == 5, "expected 5 clicks in 2 s at 120 BPM, got " << onsets.size());
        for (size_t k = 0; k < onsets.size(); ++k)
            CHECK(std::abs(onsets[k] - (int)k * 24000) < 10, "click " << k << " off the beat: " << onsets[k]);
        CHECK(p.beatCount - before == 5, "beat counter");
        CHECK(p.beatInBar == 0, "fifth click should be a downbeat");
        float tunerBuf[512];
        CHECK(p.copyTunerInput(tunerBuf, 512) == 512, "tuner input");
    }

    // 5. Optional: editor snapshots, mjr-test <lib> <DI> <out dir>
    if (argc > 3)
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        p.poll();
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        const auto outDir = cwd.getChildFile(argv[3]);
        outDir.createDirectory();
        auto save = [&](const char* name)
        {
            juce::FileOutputStream os(outDir.getChildFile(name));
            os.setPosition(0), os.truncate();
            juce::PNGImageFormat().writeImageToStream(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), os);
        };
        save("amp.png");
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "CAB")
                b->setToggleState(true, juce::dontSendNotification), b->onClick();
        save("cab.png");
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "TUNER")
                b->setToggleState(true, juce::dontSendNotification), b->onClick();
        save("tuner.png");
        std::cout << "snapshots in " << outDir.getFullPathName() << "\n";
    }

    std::cout << (failures == 0 ? "OK\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
