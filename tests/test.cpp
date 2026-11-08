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
    if (argc > 1 && juce::String(argv[1]) == "--devices") // what the standalone can open on this machine
    {
        juce::AudioDeviceManager dm;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        dm.createAudioDeviceTypes(types);
        for (auto* t : types)
        {
            t->scanForDevices();
            std::cout << t->getTypeName() << "\n  in:  " << t->getDeviceNames(true).joinIntoString(" | ")
                      << "\n  out: " << t->getDeviceNames(false).joinIntoString(" | ") << "\n";
        }
        return 0;
    }
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

    // 8. Input source picks the interface channel; the gate silences low-level noise.
    {
        MJRAmpProcessor p;
        p.setLibraryDir(lib);
        p.prepareToPlay(48000, block);
        waitIdle(p, block);
        auto rmsOut = [&](int sourceChannel, float level)
        {
            juce::AudioBuffer<float> buf(2, 48000);
            buf.clear();
            buf.addFrom(sourceChannel, 0, di, 0, di.getNumSamples() / 2, buf.getNumSamples(), level);
            run(p, buf, block);
            return buf.getRMSLevel(0, 24000, 24000);
        };
        setParam(p, "inputSource", 1.0f); // Input 2
        const float onCh2 = rmsOut(1, 1.0f), wrongCh = rmsOut(0, 1.0f);
        std::cout << "input 2: rms " << onCh2 << ", guitar on input 1: " << wrongCh << "\n";
        CHECK(onCh2 > 0.01f && wrongCh < onCh2 * 0.05f, "input source selection");
        setParam(p, "inputSource", 0.0f);

        const float quietOpen = rmsOut(0, 0.003f); // ~ -50 dB: hum/noise level
        setParam(p, "gate", -40.0f);
        rmsOut(0, 0.003f); // let the gate settle
        const float quietGated = rmsOut(0, 0.003f), loudGated = rmsOut(0, 1.0f);
        std::cout << "gate: noise " << quietOpen << " -> " << quietGated << ", playing " << loudGated << "\n";
        CHECK(quietGated < quietOpen * 0.1f, "gate does not cut noise");
        CHECK(loudGated > onCh2 * 0.5f, "gate cuts the playing");
    }

    // 9. Effects: each one, switched on alone, changes the sound but keeps it finite and sane;
    //    the Octaver (placeholder) leaves it bit-identical.
    {
        auto render = [&](const char* fxId)
        {
            MJRAmpProcessor p;
            p.setLibraryDir(lib);
            if (fxId != nullptr)
            {
                setParam(p, (juce::String(fxId) + "On").toRawUTF8(), 1.0f);
                if (juce::String(fxId) == "eq")
                    setParam(p, "eqK0", 0.8f); // flat PearEQ is neutral: boost the highs
            }
            p.prepareToPlay(48000, block);
            waitIdle(p, block);
            juce::AudioBuffer<float> b(2, 48000);
            b.clear();
            b.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, 48000);
            run(p, b, block);
            return b;
        };
        const auto off = render(nullptr);
        {
            MJRAmpProcessor p; // amp switched off on the panel: DI straight to the cab, still sound
            p.setLibraryDir(lib);
            setParam(p, "ampOn", 0.0f);
            p.prepareToPlay(48000, block);
            waitIdle(p, block);
            juce::AudioBuffer<float> b(2, 48000);
            b.clear();
            b.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, 48000);
            run(p, b, block);
            const float rms = b.getRMSLevel(1, 0, 48000);
            std::cout << "amp off: rms " << rms << "\n";
            const float onRms = off.getRMSLevel(1, 0, 48000);
            CHECK(std::abs(rms - onRms) > 1e-4f, "amp off switch has no effect");
            CHECK(rms > onRms * 0.5f && rms < onRms * 2.0f, "amp off level jumps: " << rms << " vs " << onRms);
        }
        const float offRms = off.getRMSLevel(1, 0, 48000);
        std::cout << "output level, default rig: rms " << juce::Decibels::gainToDecibels(offRms) << " dBFS, peak "
                  << juce::Decibels::gainToDecibels(off.getMagnitude(0, 48000)) << " dBFS\n";
        const float peakDb = juce::Decibels::gainToDecibels(off.getMagnitude(0, 48000));
        CHECK(peakDb > -14.0f && peakDb < -3.0f, "default output level off target: " << peakDb << " dBFS");
        for (auto& fx : fxChain)
        {
            const auto on = render(fx.id);
            float diff = 0;
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                {
                    finite = finite && std::isfinite(on.getSample(ch, i));
                    diff = std::max(diff, std::abs(on.getSample(ch, i) - off.getSample(ch, i)));
                }
            const float rms = on.getRMSLevel(1, 0, 48000);
            std::cout << "fx " << fx.id << ": max diff " << diff << ", rms " << rms << " (off " << offRms << ")\n";
            CHECK(finite, fx.id << " produced NaN/inf");
            if (fx.create == nullptr)
                CHECK(diff < 1e-6f, fx.id << " placeholder changed the audio");
            else
            {
                CHECK(diff > 1e-4f, fx.id << " does nothing");
                CHECK(rms > offRms * 0.1f && rms < offRms * 10.0f, fx.id << " level out of range: " << rms);
            }
        }
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
        save("main.png");
        {
            const int cap = ((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex();
            for (int i = 0; i < (int)p.captures.size(); ++i)
                if (p.captures[(size_t)i].displayName() == "JU Crunch BAL")
                    setParam(p, "capture", (float)i);
            setParam(p, "ampOn", 0.0f);
            save("amp-off-ju.png"); // lever down, lamp dark, jumper cable in
            setParam(p, "ampOn", 1.0f);
            setParam(p, "capture", (float)cap);
        }
        std::function<juce::Button*(juce::Component*, const juce::String&)> find = [&](juce::Component* c, const juce::String& text) -> juce::Button*
        {
            for (auto* ch : c->getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*>(ch); b != nullptr && b->getButtonText() == text)
                    return b;
                if (auto* b = find(ch, text))
                    return b;
            }
            return nullptr;
        };
        auto click = [&](const juce::String& text)
        {
            if (auto* b = find(ed.get(), text))
                { b->setToggleState(!b->getToggleState() || b->getRadioGroupId() != 0, juce::sendNotificationSync); if (b->onClick) b->onClick(); }
        };
        click("PRE FX");
        click("Drive"); // a pedal switched on
        save("pre.png");
        click("POST FX");
        save("post.png");
        click("AMP");
        click("TUNER");
        save("tuner.png");
        std::cout << "snapshots in " << outDir.getFullPathName() << "\n";
    }

    std::cout << (failures == 0 ? "OK\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
