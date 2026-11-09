// Headless checks. Usage: mjr-test <capture library dir> <DI wav>
#include "../src/Processor.h"
#include "../src/Pitch.h"
#include "../src/CircuitAmp.h"
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

    // C1. Circuit model, linear part: the tone stack's per-sample solve matches the exact network response.
    {
        const double fs = 192000;
        auto gainAt = [&](float t, float b, float m, double hz)
        {
            circuit::ToneStack ts;
            ts.setRate(fs);
            ts.setKnobs(t, b, m);
            double acc = 0;
            const int settle = (int)fs / 5, len = (int)fs / 5;
            for (int i = 0; i < settle + len; ++i)
            {
                const float y = ts.process((float)std::sin(2 * juce::MathConstants<double>::pi * hz * i / fs));
                if (i >= settle)
                    acc += (double)y * y;
            }
            return std::make_pair(std::sqrt(2 * acc / len), ts.magnitudeAt(hz));
        };
        for (double hz : { 50.0, 200.0, 700.0, 2000.0, 6000.0 })
        {
            const auto [sim, exact] = gainAt(0.5f, 0.5f, 0.5f, hz);
            std::cout << "tone stack " << hz << " Hz: " << juce::Decibels::gainToDecibels(sim) << " dB (exact "
                      << juce::Decibels::gainToDecibels(exact) << ")\n";
            CHECK(std::abs(juce::Decibels::gainToDecibels(sim / exact)) < 0.3, "tone stack sim vs exact at " << hz);
        }
        CHECK(gainAt(1, .5f, .5f, 4000).second > 1.4 * gainAt(0, .5f, .5f, 4000).second, "treble raises highs");
        CHECK(gainAt(.5f, 1, .5f, 100).second > 1.4 * gainAt(.5f, 0, .5f, 100).second, "bass raises lows");
        CHECK(gainAt(.5f, .5f, 1, 700).second > 1.4 * gainAt(.5f, .5f, 0, 700).second, "middle raises mids");
    }

    // C2. Circuit model, whole amp: sines through it, level and distortion (residual after the fundamental).
    {
        struct Result { double rms, thd, crest; };
        auto measure = [](float vol1, float vol2, CircuitAmp::Jack j, int period, float amp, float presence = 5,
                          float treble = 5)
        {
            CircuitAmp a;
            const int n = period * 400;
            a.prepare(48000, n);
            a.setControls(presence, 5, 5, treble, vol1, vol2, j);
            std::vector<float> x((size_t)n);
            for (int pass = 0; pass < 2; ++pass) // the first pass settles the coupling caps
            {
                for (int i = 0; i < n; ++i)
                    x[(size_t)i] = amp * (float)std::sin(2 * juce::MathConstants<double>::pi * i / period);
                a.process(x.data(), n);
            }
            double s = 0, c = 0, all = 0, peak = 0;
            for (int i = 0; i < n; ++i)
            {
                const double w = 2 * juce::MathConstants<double>::pi * i / period, v = x[(size_t)i];
                if (!std::isfinite(v))
                    return Result { -1, -1, -1 };
                s += v * std::sin(w), c += v * std::cos(w), all += v * v, peak = std::max(peak, std::abs(v));
            }
            const double a1 = 2.0 / n * std::sqrt(s * s + c * c), fund = a1 * a1 / 2, power = all / n;
            return Result { std::sqrt(power), std::sqrt(std::max(0.0, power - fund) / fund), peak / std::sqrt(power) };
        };
        using J = CircuitAmp::Jack;
        double lastThd = -1, lastCrest = 10;
        for (float v : { 1.0f, 3.0f, 5.0f, 7.0f, 10.0f })
        {
            const auto r = measure(v, 0, J::one, 220, 0.3f);
            std::cout << "circuit vol I " << v << ": rms " << r.rms << ", thd " << r.thd * 100 << "%, crest " << r.crest << "\n";
            CHECK(r.rms > 0, "circuit output finite at vol " << v);
            // Residual THD tops out once the wave squares off; past that, it is the crest factor that keeps falling.
            CHECK(r.thd >= lastThd * 0.95 || r.crest <= lastCrest * 1.02, "distortion grows with volume (" << v << ")");
            lastThd = r.thd, lastCrest = r.crest;
        }
        // No self-oscillation anywhere on the panel: an impulse dies out.
        for (float k : { 0.0f, 10.0f })
            for (float pr : { 0.0f, 10.0f })
                for (J j : { J::one, J::two, J::jumped })
                {
                    CircuitAmp a;
                    const int n = 48000;
                    a.prepare(48000, n);
                    a.setControls(pr, k, k, k, 10, 10, j);
                    std::vector<float> x((size_t)n, 0.0f);
                    x[0] = 0.5f;
                    a.process(x.data(), n);
                    for (int pass = 0; pass < 4; ++pass) // 4 s of silence
                        std::fill(x.begin(), x.end(), 0.0f), a.process(x.data(), n);
                    float tail = 0;
                    for (float v : x)
                        tail = std::max(tail, std::abs(v));
                    CHECK(tail < 1e-4f, "circuit stable at tone " << k << ", presence " << pr << ", jack " << (int)j << " (" << tail << ")");
                }
        // Small signal so the stages stay linear: highs relative to lows.
        auto tilt = [&](float vol1, float vol2, J j, float presence = 5)
        { return measure(vol1, vol2, j, 16, 0.01f, presence).rms / measure(vol1, vol2, j, 240, 0.01f, presence).rms; };
        const double t1 = tilt(3, 0, J::one), t2 = tilt(0, 3, J::two);
        std::cout << "circuit 3 kHz/200 Hz: ch I " << t1 << ", ch II " << t2 << "\n";
        CHECK(t1 > 1.2 * t2, "channel I brighter than II");
        CHECK(measure(3, 3, J::jumped, 220, 0.01f).rms > 1.2 * measure(3, 0, J::one, 220, 0.01f).rms,
              "jumped louder than I alone");
        double lastP = 0;
        for (float p : { 0.0f, 5.0f, 10.0f })
        {
            const double t = tilt(3, 0, J::one, p);
            std::cout << "circuit presence " << p << ": 3 kHz/200 Hz " << t << "\n";
            CHECK(t > lastP, "presence raises highs (" << p << ")");
            lastP = t;
        }
    }

    // 0. Benchmark (informative, machine dependent): seconds of audio per second of CPU, block 128.
    {
        auto bench = [&](const char* label, double sr, bool irs, bool allFx)
        {
            MJRAmpProcessor p;
            p.setLibraryDir(lib);
            for (int s = 1; !irs && s <= MJRAmpProcessor::numIrSlots; ++s)
                setParam(p, ("ir" + juce::String(s) + "On").toRawUTF8(), 0.0f);
            for (auto& fx : fxChain)
                if (allFx)
                    setParam(p, (juce::String(fx.id) + "On").toRawUTF8(), 1.0f);
            p.prepareToPlay(sr, 128);
            waitIdle(p, 128);
            juce::AudioBuffer<float> b(2, (int)sr);
            auto fill = [&]
            {
                b.clear();
                b.copyFrom(0, 0, di, 0, 0, std::min(di.getNumSamples(), b.getNumSamples()));
            };
            fill();
            run(p, b, 128); // warm-up: IRs swap in on their loader thread
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < 10; ++i)
                fill(), run(p, b, 128);
            const double cpu = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
            std::cout << "bench " << label << ": " << juce::String(10.0 / cpu, 1) << "x real time\n";
        };
        {
            CircuitAmp a;
            a.prepare(48000, 128);
            a.setControls(5, 5, 5, 5, 7, 0, CircuitAmp::Jack::one);
            std::vector<float> x((size_t)di.getNumSamples());
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < 10; ++i)
            {
                std::copy(di.getReadPointer(0), di.getReadPointer(0) + x.size(), x.begin());
                for (size_t pos = 0; pos < x.size(); pos += 128)
                    a.process(x.data() + pos, (int)std::min<size_t>(128, x.size() - pos));
            }
            const double cpu = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
            std::cout << "bench circuit amp   : " << juce::String(10.0 * (double)x.size() / 48000 / cpu, 1) << "x real time\n";
        }
        bench("amp only      ", 48000, false, false);
        bench("amp + cab     ", 48000, true, false);
        bench("full chain    ", 48000, true, true);
        bench("amp + cab 44.1", 44100, true, false);
    }
    if (std::getenv("MJR_BENCH_ONLY") != nullptr)
        return 0;

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
    {
        auto render = [&](const char* fxId)
        {
            MJRAmpProcessor p;
            p.setLibraryDir(lib);
            if (fxId != nullptr)
            {
                setParam(p, (juce::String(fxId) + "On").toRawUTF8(), 1.0f);
                if (juce::String(fxId) == "eq")
                    setParam(p, "eqK7", 9.0f); // a flat EQ is neutral: boost 8 kHz
            }
            p.prepareToPlay(48000, block);
            waitIdle(p, block);
            juce::AudioBuffer<float> b(2, 48000);
            b.clear();
            b.copyFrom(0, 0, di, 0, di.getNumSamples() / 2, 48000);
            run(p, b, block);
            return b;
        };
        {
            NamPedal ts; // nearest-capture choice from the knobs
            if (ts.setFolder(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                 .getChildFile("Music Production/NAM Profile/Pedals/Ibanez TS9 Tube Screamer")) > 0)
            {
                ts.update(7, 7, 7);
                CHECK(ts.currentName().endsWith("Drive 7 Tone 7 Level 7"), "drive 7/7/7 picked " << ts.currentName());
                ts.update(0.5f, 6, 5);
                CHECK(ts.currentName().endsWith("Drive 0 Tone 6 Level 6"), "drive 0.5/6/5 picked " << ts.currentName());
                ts.update(9.5f, 9, 7);
                CHECK(ts.currentName().endsWith("Drive 10 Tone 9 Level 7"), "drive 9.5/9/7 picked " << ts.currentName());
                std::cout << "drive pedal: nearest-capture choice ok\n";
            }
            NamPedal boost; // one knob, captures named by percent; the level follows their loudness in between
            if (boost.setFolder(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                    .getChildFile("Music Production/NAM Profile/Pedals/Fortin 33")) > 0)
            {
                boost.update(6.5f, 0, 0);
                CHECK(boost.currentName().endsWith("65%") && std::abs(boost.outputDb()) < 1e-4f, "boost 6.5: " << boost.currentName() << " " << boost.outputDb());
                boost.update(7.2f, 0, 0); // nearer 65% than 80%, and 0.7/1.5 of the way to 80%'s loudness
                CHECK(boost.currentName().endsWith("65%") && boost.outputDb() > 1.0f && boost.outputDb() < 2.5f,
                      "boost 7.2: " << boost.currentName() << " " << boost.outputDb() << " dB");
                std::cout << "boost pedal: 7.2 -> " << boost.currentName() << " " << boost.outputDb() << " dB\n";
            }
        }
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
            CHECK(diff > 1e-4f, fx.id << " does nothing");
            CHECK(rms > offRms * 0.1f && rms < offRms * 10.0f, fx.id << " level out of range: " << rms);
        }
    }

    // 11. Delay time in ms: an impulse through the post delay alone (amp off, no IR, full wet, one repeat)
    //     comes back after the time on the knob.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float ms : { 100.0f, 300.0f, 500.0f, 1500.0f })
        {
            MJRAmpProcessor p;
            setParam(p, "ampOn", 0.0f);
            for (int s = 1; s <= MJRAmpProcessor::numIrSlots; ++s)
                setParam(p, ("ir" + juce::String(s) + "On").toRawUTF8(), 0.0f);
            setParam(p, "postDelayOn", 1.0f);
            setParam(p, "postDelayK0", ms);
            setParam(p, "postDelayK1", 0.0f); // no feedback
            setParam(p, "postDelayK4", 0.0f); // no flutter
            setParam(p, "postDelayK5", 1.0f); // wet only
            p.prepareToPlay(sr, block);
            juce::AudioBuffer<float> b(2, (int)(sr * 2));
            b.clear();
            for (int i = 0; i < 8; ++i) // 8 samples: at 96k TapeDelay2 only reads every other one
                b.setSample(0, i, 1.0f);
            run(p, b, block);
            int peak = 0;
            for (int i = 1; i < b.getNumSamples(); ++i)
                if (std::abs(b.getSample(0, i)) > std::abs(b.getSample(0, peak)))
                    peak = i;
            const double got = peak * 1000.0 / sr;
            CHECK(std::abs(got / ms - 1.0) < 0.03, "delay " << ms << " ms at " << sr << " Hz came after " << got << " ms");
            if (sr == 48000.0)
                std::cout << "delay " << ms << " ms: echo after " << got << " ms\n";
        }

    // 12. Our dB knobs, on a linear chain (amp off, no IR): compressor LEVEL +6 dB doubles the output;
    //     graphic EQ 1 kHz band at +12 dB lifts a 1 kHz tone about 4x.
    {
        auto rms = [&](std::initializer_list<std::pair<const char*, float>> settings, bool tone)
        {
            MJRAmpProcessor p;
            setParam(p, "ampOn", 0.0f);
            for (int s = 1; s <= MJRAmpProcessor::numIrSlots; ++s)
                setParam(p, ("ir" + juce::String(s) + "On").toRawUTF8(), 0.0f);
            for (auto& [id, v] : settings)
                setParam(p, id, v);
            p.prepareToPlay(48000, block);
            juce::AudioBuffer<float> b(2, 48000);
            b.clear();
            for (int i = 0; i < 48000; ++i)
                b.setSample(0, i, tone ? 0.1f * std::sin(juce::MathConstants<float>::twoPi * 1000.0f * (float)i / 48000.0f)
                                       : di.getSample(0, (di.getNumSamples() / 2 + i) % di.getNumSamples()));
            run(p, b, block);
            return b.getRMSLevel(0, 24000, 24000);
        };
        const float comp0 = rms({ { "compOn", 1.0f } }, false), comp6 = rms({ { "compOn", 1.0f }, { "compK4", 6.0f } }, false);
        std::cout << "comp level +6 dB: x" << comp6 / comp0 << "\n";
        CHECK(std::abs(comp6 / comp0 - 2.0f) < 0.02f, "comp makeup +6 dB gave x" << comp6 / comp0);
        const float flat = rms({ { "eqOn", 1.0f } }, true), boosted = rms({ { "eqOn", 1.0f }, { "eqK4", 12.0f } }, true);
        std::cout << "eq 1 kHz +12 dB: x" << boosted / flat << "\n";
        CHECK(std::abs(boosted / flat - 3.98f) < 0.2f, "eq 1 kHz +12 dB gave x" << boosted / flat);
    }

    // 13. Presets: save, change, load -> the tone comes back; the library folder stays this machine's.
    {
        const auto dir = juce::File::createTempFile("mjr-presets");
        MJRAmpProcessor p;
        p.presetDir = dir;
        p.setLibraryDir(lib);
        *(juce::AudioParameterChoice*)p.params.getParameter("capture") = 5;
        p.poll();
        setParam(p, "treble", 9.0f);
        setParam(p, "postDelayK0", 350.0f);
        setParam(p, "eqK2", -4.0f);
        CHECK(p.savePreset("Test / Lead"), "preset not saved");
        const auto files = p.listPresets();
        CHECK(files.size() == 1, "preset files: " << files.size());
        setParam(p, "treble", 2.0f);
        setParam(p, "postDelayK0", 900.0f);
        setParam(p, "eqK2", 0.0f);
        *(juce::AudioParameterChoice*)p.params.getParameter("capture") = 1;
        p.poll();
        CHECK(files.size() == 1 && p.loadPreset(files[0]), "preset not loaded");
        CHECK(((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex() == 5, "preset capture not restored");
        CHECK(std::abs(p.params.getRawParameterValue("treble")->load() - 9.0f) < 1e-4f, "preset knob not restored");
        CHECK(std::abs(p.params.getRawParameterValue("postDelayK0")->load() - 350.0f) < 1.0f, "preset delay time not restored");
        CHECK(std::abs(p.params.getRawParameterValue("eqK2")->load() + 4.0f) < 1e-3f, "preset eq not restored");
        CHECK(p.getLibraryDir() == lib, "preset changed the library folder");
        CHECK(p.currentPreset == files[0].getFileNameWithoutExtension(), "current preset: " << p.currentPreset);
        std::cout << "presets: saved and restored " << files[0].getFileName() << "\n";
        dir.deleteRecursively();
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
        click("EQ");
        save("eq.png");
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
