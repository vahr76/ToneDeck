// AIDA-X / RTNeural (.json, Keras-style) model loader.
// Mirrors the reference AIDA-X behaviour: in_skip, in_gain, out_gain and up to two
// conditioning inputs (in_shape = 1, 2 or 3).

#include "ModelEngine.h"

#include <RTNeural/RTNeural.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace
{
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

class AidaEngine final : public ModelEngine
{
public:
    AidaEngine (std::unique_ptr<RTNeural::Model<float>> m, int inSize, bool skip, float inGain, float outGain)
        : model (std::move (m)), inputSize (inSize), inputSkip (skip), inputGain (inGain), outputGain (outGain) {}

    void prepare (int) override
    {
        model->reset();
        // Feed a little silence so recurrent state settles before audio starts.
        float zeros[3] = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 2048; ++i)
            model->forward (zeros);
    }

    void process (float* io, int numSamples, float p1, float p2) override
    {
        alignas (16) float in[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        in[1] = p1;
        in[2] = p2;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = io[i] * inputGain;
            in[0] = x;
            const float y = model->forward (in);
            io[i] = (inputSkip ? (x + y) : y) * outputGain;

            if (! std::isfinite (io[i]))   // never let a bad model blow up the host
            {
                io[i] = 0.0f;
                model->reset();
            }
        }
    }

private:
    std::unique_ptr<RTNeural::Model<float>> model;
    int inputSize = 1;
    bool inputSkip = false;
    float inputGain = 1.0f, outputGain = 1.0f;
};

std::string layerSummary (const nlohmann::json& j)
{
    if (! j.contains ("layers") || ! j["layers"].is_array())
        return "RNN";
    for (const auto& l : j["layers"])
    {
        const auto t = l.value ("type", std::string());
        if (t == "lstm") return "LSTM";
        if (t == "gru")  return "GRU";
        if (t == "conv1d") return "Conv1D";
    }
    return "Dense";
}
} // namespace

std::unique_ptr<ModelEngine> createAidaEngine (const std::string& jsonText, const std::string& fileStem, std::string& error)
{
    try
    {
        const auto j = nlohmann::json::parse (jsonText);
        if (! j.contains ("layers") || ! j.contains ("in_shape"))
        {
            error = "Not an AIDA-X / RTNeural model (missing layers / in_shape)";
            return nullptr;
        }

        const int inSize = j["in_shape"].back().get<int>();
        if (inSize < 1 || inSize > 3)
        {
            error = "Unsupported AIDA-X input size";
            return nullptr;
        }

        const bool skip    = j.contains ("in_skip")  && j["in_skip"].is_number()  && j["in_skip"].get<int>() == 1;
        const float inG    = (j.contains ("in_gain")  && j["in_gain"].is_number())  ? dbToGain (j["in_gain"].get<float>())  : 1.0f;
        const float outG   = (j.contains ("out_gain") && j["out_gain"].is_number()) ? dbToGain (j["out_gain"].get<float>()) : 1.0f;

        auto model = RTNeural::json_parser::parseJson<float> (j, false);
        if (model == nullptr || model->getInSize() != inSize)
        {
            error = "Could not build the RTNeural network from this file";
            return nullptr;
        }

        auto engine = std::make_unique<AidaEngine> (std::move (model), inSize, skip, inG, outG);
        auto& info = engine->info;
        info.format                = "AIDA-X";
        info.architecture          = layerSummary (j);
        info.displayName           = fileStem;
        info.sampleRate            = 48000.0;   // AIDA-X models are trained at 48 kHz
        info.numConditioningParams = inSize - 1;
        if (j.contains ("samplerate") && j["samplerate"].is_number())
            info.sampleRate = j["samplerate"].get<double>();
        return engine;
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    catch (...)
    {
        error = "Unknown error while loading AIDA-X model";
    }
    return nullptr;
}
