#pragma once

#include <juce_dsp/juce_dsp.h>
#include "BlockTypes.h"
#include "../dsp/ModelSlot.h"

namespace td
{
// Shared, read-only-ish context for all blocks (set by the processor every block).
struct RigContext
{
    bool normalizeModels = true;
    double bpm = 120.0;
};

//==============================================================================
class Block
{
public:
    explicit Block (BlockType t) : type (t) {}
    virtual ~Block() = default;

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() {}
    // Stereo in place. `p` holds the 6 plain (already mapped) parameter values.
    virtual void process (float* L, float* R, int n, const float* p, const RigContext& ctx) = 0;
    // Called instead of process() when bypassed; blocks with tails (delay/reverb) keep ringing.
    virtual void processBypassed (float* /*L*/, float* /*R*/, int /*n*/, const float* /*p*/, const RigContext&) {}
    virtual int latency() const { return 0; }
    // For the UI: gate gain (0 closed .. 1 open), detected input level in dB; -1 / -100 when n/a.
    virtual float activity() const { return -1.0f; }
    virtual float detectedDb() const { return -100.0f; }
    // For the UI: compressor gain reduction in dB (>= 0); -1 when n/a.
    virtual float gainReductionDb() const { return -1.0f; }

    const BlockType type;
};

std::unique_ptr<Block> createBlock (BlockType t);

//==============================================================================
// Model block: NAM (incl. A2), AIDA-X and GuitarML/Proteus. Mono (sums L+R).
class ModelBlock final : public Block
{
public:
    ModelBlock() : Block (BlockType::Amp) {}
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n, const float* p, const RigContext&) override;
    int latency() const override { return slot.latency(); }

    ModelSlot slot;
    double sampleRate = 48000.0;
    int maxBlockSize = 512;

private:
    juce::LinearSmoothedValue<float> inGain, outGain;
    std::vector<float> mono;
};

//==============================================================================
// Cabinet: two IR loaders (A/B) with blend, filters and dry/wet. Mono.
class CabBlock final : public Block
{
public:
    CabBlock() : Block (BlockType::Cab) {}
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n, const float* p, const RigContext&) override;

    juce::dsp::Convolution irA, irB;
    std::atomic<bool> loadedA { false }, loadedB { false };

private:
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> bufA, bufB, dry;
    juce::dsp::IIR::Filter<float> lowCut, highCut;
    float lastLow = -1.0f, lastHigh = -1.0f;
    juce::LinearSmoothedValue<float> level;
};
} // namespace td
