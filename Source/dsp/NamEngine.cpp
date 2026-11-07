#include "ModelEngine.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#include <NAM/slimmable.h>
#include <json.hpp>

#include <algorithm>
#include <exception>

namespace
{
class NamEngine final : public ModelEngine
{
public:
    explicit NamEngine (std::unique_ptr<nam::DSP> d) : dsp (std::move (d))
    {
        slim = dynamic_cast<nam::SlimmableModel*> (dsp.get());
    }

    bool isSlimmable() const override { return slim != nullptr; }

    void setSlimmableSize (double v) override
    {
        if (slim != nullptr)
            slim->SetSlimmableSize (std::clamp (v, 0.0, 1.0));
    }

    void prepare (int maxBlockSize) override
    {
        maxBlock = std::max (1, maxBlockSize);
        const int nIn  = std::max (1, dsp->NumInputChannels());
        const int nOut = std::max (1, dsp->NumOutputChannels());
        inBufs.assign  ((size_t) nIn,  std::vector<float> ((size_t) maxBlock, 0.0f));
        outBufs.assign ((size_t) nOut, std::vector<float> ((size_t) maxBlock, 0.0f));
        inPtrs.resize  ((size_t) nIn);
        outPtrs.resize ((size_t) nOut);
        for (int c = 0; c < nIn;  ++c) inPtrs[(size_t) c]  = inBufs[(size_t) c].data();
        for (int c = 0; c < nOut; ++c) outPtrs[(size_t) c] = outBufs[(size_t) c].data();

        // Reset() also pre-warms the model so it starts without a thump.
        dsp->Reset (info.sampleRate, maxBlock);
    }

    void process (float* io, int numSamples, float, float) override
    {
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (maxBlock, numSamples - done);
            for (auto& b : inBufs)
                std::copy (io + done, io + done + n, b.data());

            dsp->process (inPtrs.data(), outPtrs.data(), n);

            std::copy (outBufs[0].data(), outBufs[0].data() + n, io + done);
            done += n;
        }
    }

private:
    std::unique_ptr<nam::DSP> dsp;
    nam::SlimmableModel* slim = nullptr;
    int maxBlock = 512;
    std::vector<std::vector<float>> inBufs, outBufs;
    std::vector<float*> inPtrs, outPtrs;
};

std::string metaString (const nlohmann::json& j, const char* key)
{
    if (j.contains ("metadata") && j["metadata"].is_object())
    {
        const auto& m = j["metadata"];
        if (m.contains (key) && m[key].is_string())
            return m[key].get<std::string>();
    }
    return {};
}

bool looksLikeA2 (const nlohmann::json& j)
{
    // A2 files are tagged in metadata and/or use the slimmable / a2 config.
    const auto dumped = j.contains ("config") ? j["config"].dump() : std::string();
    const auto arch   = j.value ("architecture", std::string());
    return arch.find ("A2") != std::string::npos
        || arch.find ("Slimmable") != std::string::npos
        || metaString (j, "architecture").find ("A2") != std::string::npos
        || dumped.find ("\"a2") != std::string::npos;
}
} // namespace

std::unique_ptr<ModelEngine> createNamEngine (const std::string& jsonText, const std::string& fileStem, std::string& error)
{
    try
    {
        const auto j = nlohmann::json::parse (jsonText);
        auto dsp = nam::get_dsp (j);
        if (dsp == nullptr)
        {
            error = "Unsupported NAM architecture";
            return nullptr;
        }

        const bool arbitrary = dsp->SupportsArbitrarySampleRate();
        double sr = dsp->GetExpectedSampleRate();
        if (sr <= 0.0)
            sr = 48000.0;

        const bool hasLoud  = dsp->HasLoudness();
        const double loud   = hasLoud ? dsp->GetLoudness() : 0.0;

        auto engine = std::make_unique<NamEngine> (std::move (dsp));
        auto& info = engine->info;
        info.format              = looksLikeA2 (j) ? "NAM A2" : "NAM";
        info.architecture        = j.value ("architecture", std::string ("NAM"));
        info.sampleRate          = sr;
        info.arbitrarySampleRate = arbitrary;
        info.hasLoudness         = hasLoud;
        info.loudnessDb          = loud;

        // The file name is usually the most descriptive label (TONE3000 downloads etc.)
        info.displayName = fileStem.empty() ? metaString (j, "name") : fileStem;
        info.author      = metaString (j, "modeled_by");
        return engine;
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    catch (...)
    {
        error = "Unknown error while loading NAM model";
    }
    return nullptr;
}
