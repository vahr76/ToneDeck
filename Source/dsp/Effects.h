#pragma once

#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Noise gate with hysteresis, hold and smooth attack/release (mono).
class NoiseGate
{
public:
    void prepare (double sr) { sampleRate = sr; env = 0.0f; gain = 1.0f; holdCounter = 0; }

    void setParams (float thresholdDb, float releaseMs)
    {
        openThresh  = juce::Decibels::decibelsToGain (thresholdDb);
        closeThresh = juce::Decibels::decibelsToGain (thresholdDb - 6.0f);   // 6 dB hysteresis
        releaseCoef = std::exp (-1.0f / (0.001f * juce::jmax (5.0f, releaseMs) * (float) sampleRate));
        attackCoef  = std::exp (-1.0f / (0.0005f * (float) sampleRate));
        envCoef     = std::exp (-1.0f / (0.005f * (float) sampleRate));
        holdSamples = (int) (0.02 * sampleRate);
    }

    // Detect on `key` (pre-amp signal), apply to `io`.
    void process (const float* key, float* io, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (key[i]);
            env = a > env ? a : env * envCoef + a * (1.0f - envCoef);

            if (env > openThresh)       { open = true; holdCounter = holdSamples; }
            else if (env < closeThresh) { if (holdCounter > 0) --holdCounter; else open = false; }

            const float target = open ? 1.0f : 0.0f;
            const float coef   = target > gain ? attackCoef : releaseCoef;
            gain = target + (gain - target) * coef;
            io[i] *= gain;
        }
    }

    float getGainReduction() const noexcept { return gain; }

private:
    double sampleRate = 48000.0;
    float openThresh = 0.001f, closeThresh = 0.0005f;
    float attackCoef = 0.9f, releaseCoef = 0.999f, envCoef = 0.99f;
    float env = 0.0f, gain = 1.0f;
    int holdSamples = 960, holdCounter = 0;
    bool open = false;
};

//==============================================================================
// Mono-in / stereo-out delay with filtered feedback (analog-ish repeats).
class StereoDelay
{
public:
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        const int maxSamples = (int) (sr * 2.1) + maxBlock;
        bufL.assign ((size_t) maxSamples, 0.0f);
        bufR.assign ((size_t) maxSamples, 0.0f);
        writePos = 0;
        smoothedTime.reset (sr, 0.15);
        lpL.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        lpR.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        lpL.coefficients = lpR.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 4500.0f);
    }

    void setParams (float timeMs, float feedback, float mix)
    {
        smoothedTime.setTargetValue (juce::jlimit (20.0f, 2000.0f, timeMs) * 0.001f * (float) sampleRate);
        fb = juce::jlimit (0.0f, 0.95f, feedback);
        wet = mix;
    }

    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }

    // Adds the wet signal to L/R.
    void process (const float* in, float* L, float* R, int n)
    {
        const int size = (int) bufL.size();
        for (int i = 0; i < n; ++i)
        {
            const float d  = smoothedTime.getNextValue();
            const float dR = d * 1.015f;   // tiny offset for width
            const float yL = read (bufL, d, size);
            const float yR = read (bufR, dR, size);

            bufL[(size_t) writePos] = in[i] + lpL.processSample (yL) * fb;
            bufR[(size_t) writePos] = in[i] + lpR.processSample (yR) * fb;
            writePos = (writePos + 1) % size;

            L[i] += yL * wet;
            R[i] += yR * wet;
        }
    }

private:
    float read (const std::vector<float>& b, float delay, int size) const
    {
        float rp = (float) writePos - delay;
        while (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const float f = rp - (float) i0;
        return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
    }

    double sampleRate = 48000.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    float fb = 0.3f, wet = 0.3f;
    juce::SmoothedValue<float> smoothedTime { 12000.0f };
    juce::dsp::IIR::Filter<float> lpL, lpR;
};

//==============================================================================
// Smoothed level meter (peak, with decay), written on the audio thread.
struct LevelMeter
{
    std::atomic<float> level { 0.0f };

    void push (const float* data, int n)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
        const float old = level.load (std::memory_order_relaxed);
        level.store (peak > old ? peak : old * 0.92f, std::memory_order_relaxed);
    }
};
