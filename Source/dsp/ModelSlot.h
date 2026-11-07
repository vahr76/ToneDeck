#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ModelEngine.h"

#include <atomic>
#include <mutex>

//==============================================================================
// Streaming 4-point Hermite resampler (sample in -> 0..n samples out).
class StreamResampler
{
public:
    void setRatio (double inRate, double outRate) { step = inRate / outRate; reset(); }
    void reset() { pos = 0.0; std::fill (std::begin (x), std::end (x), 0.0f); }

    template <typename Emit>
    void push (float s, Emit&& emit)
    {
        x[0] = x[1]; x[1] = x[2]; x[2] = x[3]; x[3] = s;
        while (pos < 1.0)
        {
            const float t  = (float) pos;
            const float c0 = x[1];
            const float c1 = 0.5f * (x[2] - x[0]);
            const float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
            const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
            emit (((c3 * t + c2) * t + c1) * t + c0);
            pos += step;
        }
        pos -= 1.0;
    }

private:
    double step = 1.0, pos = 0.0;
    float x[4] {};
};

//==============================================================================
// A loaded model plus everything needed to run it at the host sample rate.
struct ModelInstance
{
    std::unique_ptr<ModelEngine> engine;   // nullptr = empty slot
    juce::File file;
    double hostRate = 0.0;
    float normGain = 1.0f;   // brings models with loudness metadata to -18 dB

    void prepare (double newHostRate, int maxHostBlock)
    {
        hostRate = newHostRate;
        if (engine == nullptr)
            return;

        const double modelRate = engine->info.sampleRate;
        resample = ! engine->info.arbitrarySampleRate && std::abs (modelRate - hostRate) > 1.0;

        const int maxModelBlock = resample ? (int) std::ceil (maxHostBlock * modelRate / hostRate) + 16 : maxHostBlock;
        engine->prepare (maxModelBlock);

        if (resample)
        {
            up.setRatio (hostRate, modelRate);
            down.setRatio (modelRate, hostRate);
            modelBuf.assign ((size_t) maxModelBlock + 16, 0.0f);

            // Band-limit in the model domain (4th-order Butterworth) to keep images/aliases out.
            const double fc = 0.45 * std::min (hostRate, modelRate);
            auto c1 = juce::dsp::IIR::Coefficients<float>::makeLowPass (modelRate, fc, 0.5412);
            auto c2 = juce::dsp::IIR::Coefficients<float>::makeLowPass (modelRate, fc, 1.3066);
            for (auto* f : { &preA, &postA }) { f->coefficients = c1; f->reset(); }
            for (auto* f : { &preB, &postB }) { f->coefficients = c2; f->reset(); }

            fifo.assign ((size_t) (maxHostBlock * 4 + 256), 0.0f);
            readPos = 0;
            writePos = kResampleLatency;   // pre-roll so the output never starves
            fifoCount = kResampleLatency;
        }
    }

    int latencySamples() const { return resample ? kResampleLatency : 0; }

    // Audio thread. Mono, in place, host sample rate.
    void process (float* io, int n, float p1, float p2)
    {
        if (engine == nullptr)
            return;

        if (! resample)
        {
            engine->process (io, n, p1, p2);
            return;
        }

        int m = 0;
        const int cap = (int) modelBuf.size();
        for (int i = 0; i < n; ++i)
            up.push (io[i], [&] (float s) { if (m < cap) modelBuf[(size_t) m++] = s; });

        for (int i = 0; i < m; ++i)
            modelBuf[(size_t) i] = preB.processSample (preA.processSample (modelBuf[(size_t) i]));

        engine->process (modelBuf.data(), m, p1, p2);

        const int fsize = (int) fifo.size();
        for (int i = 0; i < m; ++i)
        {
            const float s = postB.processSample (postA.processSample (modelBuf[(size_t) i]));
            down.push (s, [&] (float y)
            {
                if (fifoCount < fsize)
                {
                    fifo[(size_t) writePos] = y;
                    writePos = (writePos + 1) % fsize;
                    ++fifoCount;
                }
            });
        }

        for (int i = 0; i < n; ++i)
        {
            if (fifoCount > 0)
            {
                io[i] = fifo[(size_t) readPos];
                readPos = (readPos + 1) % fsize;
                --fifoCount;
            }
            else
            {
                io[i] = 0.0f;
            }
        }
    }

private:
    static constexpr int kResampleLatency = 16;
    bool resample = false;
    StreamResampler up, down;
    juce::dsp::IIR::Filter<float> preA, preB, postA, postB;
    std::vector<float> modelBuf, fifo;
    int readPos = 0, writePos = 0, fifoCount = 0;
};

//==============================================================================
// Lock-free hand-over of ModelInstances between loader threads and the audio thread.
class ModelSlot
{
public:
    ~ModelSlot()
    {
        delete pending.exchange (nullptr);
        delete trash.exchange (nullptr);
        delete active;
    }

    // Any non-audio thread. `inst` must already be prepared.
    void setInstance (std::unique_ptr<ModelInstance> inst)
    {
        std::lock_guard<std::mutex> lock (uiMutex);
        lastLoaded = inst.get();
        delete pending.exchange (inst.release());
    }

    // Message thread: free instances the audio thread has finished with.
    void collectGarbage() { delete trash.exchange (nullptr); }

    // Message thread: forwards to the most recently loaded engine.
    void setSlimmableSize (double v)
    {
        std::lock_guard<std::mutex> lock (uiMutex);
        if (lastLoaded != nullptr && lastLoaded->engine != nullptr)
            lastLoaded->engine->setSlimmableSize (v);
    }

    // prepareToPlay (audio callback is stopped).
    void prepare (double hostRate, int maxBlock)
    {
        std::lock_guard<std::mutex> lock (uiMutex);
        if (active  != nullptr) active->prepare (hostRate, maxBlock);
        if (auto* p = pending.exchange (nullptr))
        {
            p->prepare (hostRate, maxBlock);
            delete pending.exchange (p);
        }
        fadeGain = 1.0f;
    }

    // Audio thread, once per block before process().
    // Returns true when a new model was just swapped in.
    bool update()
    {
        if (trash.load (std::memory_order_acquire) != nullptr)
            return false;   // previous model not freed yet; try next block

        if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            trash.store (active, std::memory_order_release);
            active = p;
            fadeGain = 0.0f;
            return true;
        }
        return false;
    }

    bool hasModel() const noexcept { return active != nullptr && active->engine != nullptr; }
    int latency() const noexcept   { return active != nullptr ? active->latencySamples() : 0; }
    float normGain() const noexcept { return active != nullptr ? active->normGain : 1.0f; }

    // Audio thread.
    void process (float* io, int n, float p1, float p2)
    {
        if (! hasModel())
            return;

        active->process (io, n, p1, p2);

        if (fadeGain < 1.0f)   // short fade-in after a model change avoids clicks
        {
            for (int i = 0; i < n; ++i)
            {
                io[i] *= fadeGain;
                fadeGain = std::min (1.0f, fadeGain + 1.0f / 512.0f);
            }
        }
    }

private:
    std::atomic<ModelInstance*> pending { nullptr }, trash { nullptr };
    ModelInstance* active = nullptr;
    ModelInstance* lastLoaded = nullptr;
    std::mutex uiMutex;
    float fadeGain = 1.0f;
};
