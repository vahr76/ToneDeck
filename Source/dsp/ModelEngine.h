#pragma once

// Format-agnostic neural amp / pedal model interface.
// NamEngine.cpp and AidaEngine.cpp live in separate translation units on purpose:
// NAM and RTNeural ship different nlohmann::json versions.

#include <memory>
#include <string>
#include <vector>

struct ModelInfo
{
    std::string format;        // "NAM", "NAM A2", "AIDA-X"
    std::string architecture;  // "WaveNet", "LSTM", "GRU" ...
    std::string displayName;   // name from metadata or file name
    std::string author;        // metadata if present
    std::string gearType;      // NAM metadata gear_type: amp, pedal, amp_cab, pedal_amp...
    double sampleRate = 48000.0;  // rate the model was trained at
    bool arbitrarySampleRate = false;
    bool hasLoudness = false;
    double loudnessDb = 0.0;   // NAM loudness metadata (used for normalisation)
    int numConditioningParams = 0; // AIDA-X conditioned models (0, 1 or 2 knobs)
};

class ModelEngine
{
public:
    virtual ~ModelEngine() = default;

    // Called off the audio thread, at the model's own sample rate.
    virtual void prepare (int maxBlockSize) = 0;

    // Mono in-place processing at the model's sample rate.
    // p1/p2 are 0..1 conditioning values (only used by conditioned AIDA-X models).
    virtual void process (float* io, int numSamples, float p1, float p2) = 0;

    // NAM A2 "slimmable" models can trade quality for CPU. Message thread only.
    virtual bool isSlimmable() const { return false; }
    virtual void setSlimmableSize (double /*0..1*/) {}

    ModelInfo info;
};

// Loaders take the raw file contents so unicode paths work on every OS.
// They return nullptr and fill `error` on failure.
std::unique_ptr<ModelEngine> createNamEngine  (const std::string& jsonText, const std::string& fileStem, std::string& error);
std::unique_ptr<ModelEngine> createAidaEngine (const std::string& jsonText, const std::string& fileStem, std::string& error);
