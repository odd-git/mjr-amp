// Offline renderer for the fitter (nam-twin): runs audio through a .nam capture or through the circuit amp
// with any parameter set. Usage: mjr-render <job.json> | mjr-render --serve | mjr-render --selftest
// Audio files are raw float32 mono at 48 kHz. The job:
//   { "base": "B" | "C", "params": { name: value, ... },          (CircuitAmpDesign's fields; base defaults to B)
//     "renders": [ { "in": path, "out": path, "nam": path }                                  (a capture)
//                | { "in": path, "out": path, "knobs": { presence, bass, middle, treble, volume1, volume2 },
//                    "channel": "I" | "II" | "JU" } ] }                                      (the circuit)
#include "../src/CircuitAmp.h"
#include <NAM/get_dsp.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

// Amps B and C as of e159f3e (x86-64, -ffp-contract=off): the runtime parameters must not change their sound.
constexpr std::uint64_t AMP_B_FINGERPRINT = 11958423914863402295ull, AMP_C_FINGERPRINT = 17046909789752692814ull;

static std::uint64_t fingerprint(CircuitAmpFull::Design d)
{
    std::uint64_t h = 1469598103934665603ull;
    using J = CircuitAmp::Jack;
    for (J j : { J::one, J::two, J::jumped })
        for (float k : { 2.0f, 5.0f, 9.0f })
        {
            CircuitAmpFull a(d);
            const int n = 24000;
            a.prepare(48000, 256);
            a.setControls(k, 10 - k, k, 5, k, 10 - k, j);
            std::vector<float> x((size_t)n);
            for (int i = 0; i < n; ++i) // a decaying chord plus a low tone: clean to clipping
                x[(size_t)i] = (float)(std::exp(-i / 9000.0) * (0.5 * std::sin(i * 0.0108) + 0.3 * std::sin(i * 0.0431))
                                       + 0.05 * std::sin(i * 0.131));
            for (int pos = 0; pos < n; pos += 256)
                a.process(x.data() + pos, std::min(256, n - pos));
            for (float v : x)
            {
                std::uint32_t b;
                std::memcpy(&b, &v, 4);
                h = (h ^ b) * 1099511628211ull;
            }
        }
    return h;
}

static std::vector<float> readRaw(const juce::String& path)
{
    juce::MemoryBlock mb;
    if (!juce::File::getCurrentWorkingDirectory().getChildFile(path).loadFileAsData(mb))
        throw std::runtime_error(("cannot read " + path).toStdString());
    std::vector<float> x(mb.getSize() / sizeof(float));
    std::memcpy(x.data(), mb.getData(), x.size() * sizeof(float));
    return x;
}

static void renderOne(const juce::var& r, const CircuitAmpFull::Design& design)
{
    const int block = 512;
    auto x = readRaw(r["in"].toString());
    const int n = (int)x.size();
    if (r.hasProperty("nam"))
    {
        auto dsp = nam::get_dsp(std::filesystem::path(r["nam"].toString().toStdString()));
        if (std::abs(dsp->GetExpectedSampleRate() - 48000.0) > 1)
            throw std::runtime_error("capture is not 48 kHz: " + r["nam"].toString().toStdString());
        dsp->ResetAndPrewarm(48000, block);
        std::vector<float> y(x.size());
        for (int pos = 0; pos < n; pos += block)
        {
            float *in = x.data() + pos, *out = y.data() + pos;
            dsp->process(&in, &out, std::min(block, n - pos));
        }
        x = y;
    }
    else
    {
        const auto k = r["knobs"];
        const auto ch = r["channel"].toString();
        using J = CircuitAmp::Jack;
        CircuitAmpFull a(design);
        a.prepare(48000, block);
        a.setControls((float)k["presence"], (float)k["bass"], (float)k["middle"], (float)k["treble"], (float)k["volume1"],
                      (float)k["volume2"], ch == "JU" ? J::jumped : ch == "II" ? J::two : J::one);
        for (int pos = 0; pos < n; pos += block)
            a.process(x.data() + pos, std::min(block, n - pos));
    }
    if (!juce::File::getCurrentWorkingDirectory().getChildFile(r["out"].toString()).replaceWithData(x.data(), x.size() * sizeof(float)))
        throw std::runtime_error("cannot write " + r["out"].toString().toStdString());
}

static int runJob(const char* path)
{
    const auto job = juce::JSON::parse(juce::File::getCurrentWorkingDirectory().getChildFile(path));
    const auto* renders = job["renders"].getArray();
    if (renders == nullptr)
        return std::cerr << "job has no renders\n", 2;
    auto design = job["base"].toString() == "C" ? CircuitAmpFull::lead1968 : CircuitAmpFull::as1967;
    if (const auto* params = job["params"].getDynamicObject())
        for (const auto& p : params->getProperties())
            if (!design.set(p.name.toString().toStdString(), (double)p.value))
                return std::cerr << "unknown parameter: " << p.name.toString() << "\n", 2;
    std::atomic<int> next { 0 }, failed { 0 };
    auto work = [&]
    {
        for (int i; (i = next++) < renders->size();)
            try
            {
                renderOne(renders->getReference(i), design);
            }
            catch (const std::exception& e)
            {
                std::cerr << "render " << i << ": " << e.what() << "\n";
                ++failed;
            }
    };
    { // warm the driver pair's shared table before the threads race to build it
        CircuitAmpFull a(design);
        a.prepare(48000, 64);
    }
    const char* env = std::getenv("MJR_THREADS");
    const int threads = std::min(renders->size(), env ? std::atoi(env) : (int)std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(work);
    work();
    for (auto& t : pool)
        t.join();
    return failed ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0)
    {
        const auto b = fingerprint(CircuitAmpFull::as1967), c = fingerprint(CircuitAmpFull::lead1968);
        std::cout << "amp B fingerprint: " << b << "\namp C fingerprint: " << c << "\n";
        const bool ok = b == AMP_B_FINGERPRINT && c == AMP_C_FINGERPRINT;
        if (!ok)
            std::cerr << "FAIL: amp B or C changed\n";
        return ok ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--serve") == 0) // job paths on stdin, one exit code per line on stdout:
    {                                                       // the tables built at start-up are paid once
        for (std::string line; std::getline(std::cin, line);)
            std::cout << runJob(line.c_str()) << std::endl;
        return 0;
    }
    if (argc == 2)
        return runJob(argv[1]);
    return std::cerr << "usage: mjr-render <job.json> | mjr-render --serve | mjr-render --selftest\n", 2;
}
