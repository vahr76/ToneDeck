#include "Blocks.h"

namespace td
{
using namespace juce;
using AC = dsp::IIR::ArrayCoefficients<float>;

namespace
{
inline float dbToGain (float db) { return Decibels::decibelsToGain (db, -100.0f); }

// Pair of mono IIR filters (one per channel) with allocation-free coefficient updates.
struct StereoIIR
{
    dsp::IIR::Filter<float> l, r;
    void prepare (double sr, int maxBlock)
    {
        for (auto* f : { &l, &r })
        {
            f->coefficients = new dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            f->prepare ({ sr, (uint32) maxBlock, 1 });
            f->reset();
        }
    }
    template <size_t N> void set (const std::array<float, N>& c) { *l.coefficients = c; *r.coefficients = c; }
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i) { L[i] = l.processSample (L[i]); R[i] = r.processSample (R[i]); }
    }
    void reset() { l.reset(); r.reset(); }
};

// Change detection helper for coefficient updates
template <size_t N>
bool changed (std::array<float, N>& cache, std::initializer_list<float> now)
{
    bool diff = false;
    size_t i = 0;
    for (float v : now)
    {
        if (i < N && std::abs (cache[i] - v) > 1e-6f) { cache[i] = v; diff = true; }
        ++i;
    }
    return diff;
}

void mix (float* wet, const float* dryIn, float amount, int n)
{
    if (amount >= 0.999f) return;
    for (int i = 0; i < n; ++i)
        wet[i] = dryIn[i] + (wet[i] - dryIn[i]) * amount;
}

//==============================================================================
class GateBlock final : public Block
{
public:
    GateBlock() : Block (BlockType::Gate) {}
    void prepare (double s, int) override { sr = s; env = 0; gain = 1; hold = 0; isOpen = false; }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        const float open  = dbToGain (p[0]), close = dbToGain (p[0] - 6.0f);   // 6 dB hysteresis
        const float floor = dbToGain (p[2]);                                    // RANGE: how far it closes
        const float rel   = std::exp (-1.0f / (0.001f * jmax (5.0f, p[1]) * (float) sr));
        const float att   = std::exp (-1.0f / (0.0005f * (float) sr));
        // RMS-style detector (10 ms): noise spikes don't keep the gate open, playing does
        const float envC  = std::exp (-1.0f / (0.010f * (float) sr));
        const int holdSamples = (int) (0.05 * sr);
        float peakEnv = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = 0.5f * (L[i] * L[i] + R[i] * R[i]);
            env = envC * env + (1.0f - envC) * a;
            const float level = std::sqrt (env) * 1.4142f;   // ~ peak of a sine with that RMS
            peakEnv = jmax (peakEnv, level);
            if (level > open)       { isOpen = true; hold = holdSamples; }
            else if (level < close) { if (hold > 0) --hold; else isOpen = false; }
            const float target = isOpen ? 1.0f : floor;
            gain = target + (gain - target) * (target > gain ? att : rel);
            L[i] *= gain; R[i] *= gain;
        }
        gainOut.store (gain, std::memory_order_relaxed);
        levelOut.store (Decibels::gainToDecibels (peakEnv, -100.0f), std::memory_order_relaxed);
    }
    float activity() const override   { return gainOut.load (std::memory_order_relaxed); }
    float detectedDb() const override { return levelOut.load (std::memory_order_relaxed); }
private:
    double sr = 48000; float env = 0, gain = 1; int hold = 0; bool isOpen = false;
    std::atomic<float> gainOut { 1.0f }, levelOut { -100.0f };
};

//==============================================================================
class CompressorBlock final : public Block
{
public:
    CompressorBlock() : Block (BlockType::Compressor) {}
    void prepare (double sr, int maxBlock) override
    {
        comp.prepare ({ sr, (uint32) maxBlock, 2 });
        dryBuf.setSize (2, maxBlock);
        makeup.reset (sr, 0.05);
    }
    void reset() override { comp.reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        comp.setThreshold (p[0]); comp.setRatio (p[1]); comp.setAttack (p[2]); comp.setRelease (p[3]);
        makeup.setTargetValue (dbToGain (p[4]));
        dryBuf.copyFrom (0, 0, L, n); dryBuf.copyFrom (1, 0, R, n);
        float* ch[] = { L, R };
        dsp::AudioBlock<float> b (ch, 2, (size_t) n);
        comp.process (dsp::ProcessContextReplacing<float> (b));
        for (int i = 0; i < n; ++i) { const float g = makeup.getNextValue(); L[i] *= g; R[i] *= g; }
        mix (L, dryBuf.getReadPointer (0), p[5], n);
        mix (R, dryBuf.getReadPointer (1), p[5], n);
    }
private:
    dsp::Compressor<float> comp;
    AudioBuffer<float> dryBuf;
    LinearSmoothedValue<float> makeup;
};

//==============================================================================
class BoostBlock final : public Block
{
public:
    BoostBlock() : Block (BlockType::Boost) {}
    void prepare (double s, int maxBlock) override { sr = s; low.prepare (s, maxBlock); high.prepare (s, maxBlock); cache.fill (-999); gain.reset (s, 0.05); }
    void reset() override { low.reset(); high.reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        if (changed (cache, { p[1], p[2] }))
        {
            low.set (AC::makeLowShelf (sr, 150.0f, 0.7f, dbToGain (p[1])));
            high.set (AC::makeHighShelf (sr, 2500.0f, 0.7f, dbToGain (p[2])));
        }
        gain.setTargetValue (dbToGain (p[0]));
        low.process (L, R, n); high.process (L, R, n);
        for (int i = 0; i < n; ++i) { const float g = gain.getNextValue(); L[i] *= g; R[i] *= g; }
    }
private:
    double sr = 48000; StereoIIR low, high; std::array<float, 2> cache {}; LinearSmoothedValue<float> gain;
};

//==============================================================================
// Overdrive / Fuzz: 2x oversampled waveshapers with pre/post filtering.
class DriveBlock final : public Block
{
public:
    explicit DriveBlock (bool isFuzz) : Block (isFuzz ? BlockType::Fuzz : BlockType::Overdrive), fuzz (isFuzz) {}

    void prepare (double s, int maxBlock) override
    {
        sr = s;
        os = std::make_unique<dsp::Oversampling<float>> (2, 1, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        os->initProcessing ((size_t) maxBlock);
        pre.prepare (s, maxBlock); tone.prepare (s, maxBlock);
        dryBuf.setSize (2, maxBlock);
        cache.fill (-999);
        level.reset (s, 0.05);
    }
    void reset() override { if (os) os->reset(); pre.reset(); tone.reset(); }

    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        const float amount = p[0];
        if (changed (cache, { p[1] }))
        {
            // TS-style: cut lows before clipping; tone = sweepable low-pass after
            pre.set (fuzz ? AC::makeHighPass (sr, 60.0f, 0.707f) : AC::makeHighPass (sr, 720.0f * 0.5f, 0.6f));
            tone.set (AC::makeLowPass (sr, 600.0f * std::pow (12.0f, p[1]), 0.707f));
        }
        level.setTargetValue (dbToGain (p[2]));
        dryBuf.copyFrom (0, 0, L, n); dryBuf.copyFrom (1, 0, R, n);

        pre.process (L, R, n);

        float* ch[] = { L, R };
        dsp::AudioBlock<float> block (ch, 2, (size_t) n);
        auto up = os->processSamplesUp (block);
        const float drive = fuzz ? 10.0f * std::pow (60.0f, amount) : 1.5f * std::pow (60.0f, amount);
        const float gateThresh = fuzz ? p[3] * 0.02f : 0.0f;
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = d[i] * drive;
                if (fuzz)
                {
                    const float e = std::abs (d[i]);
                    x = (x + 0.25f) ;                                   // bias -> asymmetric, "spitty"
                    x = x > 1.0f ? 1.0f : (x < -0.6f ? -0.6f : x);       // hard-ish clip
                    x = std::tanh (x * 1.5f) - std::tanh (0.375f);
                    if (e < gateThresh) x *= e / jmax (1e-6f, gateThresh);
                }
                else
                {
                    x = x >= 0.0f ? std::tanh (x) : std::tanh (x * 0.8f) / 0.8f * 0.9f;   // soft, asymmetric
                }
                d[i] = x;
            }
        }
        os->processSamplesDown (block);
        tone.process (L, R, n);

        if (! fuzz)   // keep some clean low end like a TS808's clean blend
        {
            const auto* dl = dryBuf.getReadPointer (0); const auto* dr = dryBuf.getReadPointer (1);
            for (int i = 0; i < n; ++i) { L[i] = L[i] * 0.7f + dl[i] * 0.15f; R[i] = R[i] * 0.7f + dr[i] * 0.15f; }
        }
        for (int i = 0; i < n; ++i) { const float g = level.getNextValue(); L[i] *= g; R[i] *= g; }
        if (! fuzz)
        {
            mix (L, dryBuf.getReadPointer (0), p[3], n);
            mix (R, dryBuf.getReadPointer (1), p[3], n);
        }
    }
    int latency() const override { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

private:
    const bool fuzz;
    double sr = 48000;
    std::unique_ptr<dsp::Oversampling<float>> os;
    StereoIIR pre, tone;
    AudioBuffer<float> dryBuf;
    std::array<float, 1> cache {};
    LinearSmoothedValue<float> level;
};

//==============================================================================
class ToneEqBlock final : public Block
{
public:
    ToneEqBlock() : Block (BlockType::ToneEq) {}
    void prepare (double s, int mb) override { sr = s; for (auto* f : { &b, &m, &t, &pr }) f->prepare (s, mb); cache.fill (-999); level.reset (s, 0.05); }
    void reset() override { for (auto* f : { &b, &m, &t, &pr }) f->reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        if (changed (cache, { p[0], p[1], p[2], p[3], p[4] }))
        {
            b.set  (AC::makeLowShelf  (sr, 120.0f, 0.7f, dbToGain (p[0])));
            m.set  (AC::makePeakFilter (sr, p[4], 0.8f, dbToGain (p[1])));
            t.set  (AC::makeHighShelf (sr, 2800.0f, 0.7f, dbToGain (p[2])));
            pr.set (AC::makePeakFilter (sr, 5500.0f, 0.9f, dbToGain (p[3])));
        }
        level.setTargetValue (dbToGain (p[5]));
        b.process (L, R, n); m.process (L, R, n); t.process (L, R, n); pr.process (L, R, n);
        for (int i = 0; i < n; ++i) { const float g = level.getNextValue(); L[i] *= g; R[i] *= g; }
    }
private:
    double sr = 48000; StereoIIR b, m, t, pr; std::array<float, 5> cache {}; LinearSmoothedValue<float> level;
};

class ParamEqBlock final : public Block
{
public:
    ParamEqBlock() : Block (BlockType::ParamEq) {}
    void prepare (double s, int mb) override { sr = s; for (auto* f : { &lo, &mid, &hi }) f->prepare (s, mb); cache.fill (-999); level.reset (s, 0.05); }
    void reset() override { for (auto* f : { &lo, &mid, &hi }) f->reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        if (changed (cache, { p[0], p[1], p[2], p[3], p[4] }))
        {
            lo.set  (AC::makeLowShelf  (sr, 100.0f, 0.7f, dbToGain (p[0])));
            mid.set (AC::makePeakFilter (sr, jmin (p[1], (float) sr * 0.45f), p[3], dbToGain (p[2])));
            hi.set  (AC::makeHighShelf (sr, 4000.0f, 0.7f, dbToGain (p[4])));
        }
        level.setTargetValue (dbToGain (p[5]));
        lo.process (L, R, n); mid.process (L, R, n); hi.process (L, R, n);
        for (int i = 0; i < n; ++i) { const float g = level.getNextValue(); L[i] *= g; R[i] *= g; }
    }
private:
    double sr = 48000; StereoIIR lo, mid, hi; std::array<float, 5> cache {}; LinearSmoothedValue<float> level;
};

//==============================================================================
class ChorusBlock final : public Block
{
public:
    explicit ChorusBlock (bool flanger) : Block (flanger ? BlockType::Flanger : BlockType::Chorus), isFlanger (flanger) {}
    void prepare (double sr, int mb) override { fx.prepare ({ sr, (uint32) mb, 2 }); }
    void reset() override { fx.reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        fx.setRate (p[0]);
        fx.setDepth (p[1]);
        fx.setCentreDelay (isFlanger ? 1.0f + p[1] * 2.0f : p[2]);
        fx.setFeedback (isFlanger ? p[2] : 0.0f);
        fx.setMix (p[3]);
        float* ch[] = { L, R };
        dsp::AudioBlock<float> b (ch, 2, (size_t) n);
        fx.process (dsp::ProcessContextReplacing<float> (b));
    }
private:
    const bool isFlanger;
    dsp::Chorus<float> fx;
};

class PhaserBlock final : public Block
{
public:
    PhaserBlock() : Block (BlockType::Phaser) {}
    void prepare (double sr, int mb) override { fx.prepare ({ sr, (uint32) mb, 2 }); }
    void reset() override { fx.reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        fx.setRate (p[0]); fx.setDepth (p[1]); fx.setCentreFrequency (p[2]); fx.setFeedback (p[3]); fx.setMix (p[4]);
        float* ch[] = { L, R };
        dsp::AudioBlock<float> b (ch, 2, (size_t) n);
        fx.process (dsp::ProcessContextReplacing<float> (b));
    }
private:
    dsp::Phaser<float> fx;
};

class TremoloBlock final : public Block
{
public:
    TremoloBlock() : Block (BlockType::Tremolo) {}
    void prepare (double s, int) override { sr = s; phase = 0; }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        const float inc = p[0] / (float) sr;
        const int shape = (int) p[2];
        const bool pan = p[3] > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float a = lfo (phase, shape), b = lfo (std::fmod (phase + 0.5f, 1.0f), shape);
            const float gl = 1.0f - p[1] * a;
            const float gr = 1.0f - p[1] * (pan ? b : a);
            L[i] *= gl; R[i] *= gr;
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
    }
private:
    static float lfo (float ph, int shape)
    {
        switch (shape)
        {
            case 1:  return ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
            case 2:  return ph < 0.5f ? 1.0f : 0.0f;
            default: return 0.5f + 0.5f * std::sin (MathConstants<float>::twoPi * ph);
        }
    }
    double sr = 48000; float phase = 0;
};

class WahBlock final : public Block
{
public:
    WahBlock() : Block (BlockType::Wah) {}
    void prepare (double s, int mb) override
    {
        sr = s;
        svf.prepare ({ s, (uint32) mb, 2 });
        svf.setType (dsp::StateVariableTPTFilterType::bandpass);
        dryBuf.setSize (2, mb);
        env = 0; phase = 0; pos.reset (s, 0.02);
    }
    void reset() override { svf.reset(); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        const int mode = (int) p[1];
        svf.setResonance (p[3]);
        pos.setTargetValue (p[0]);
        dryBuf.copyFrom (0, 0, L, n); dryBuf.copyFrom (1, 0, R, n);
        const float envC = std::exp (-1.0f / (0.03f * (float) sr));
        const float makeup = 1.0f + p[3] * 0.25f;
        for (int i = 0; i < n; ++i)
        {
            float x = pos.getNextValue();
            if (mode == 1)
            {
                const float a = std::abs (0.5f * (L[i] + R[i]));
                env = jmax (a, env * envC);
                x = jlimit (0.0f, 1.0f, env * (2.0f + p[2] * 30.0f));
            }
            else if (mode == 2)
            {
                x = 0.5f + 0.5f * std::sin (MathConstants<float>::twoPi * phase);
                phase += p[4] / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
            }
            if ((i & 15) == 0)
                svf.setCutoffFrequency (350.0f * std::pow (2200.0f / 350.0f, x));
            L[i] = svf.processSample (0, L[i]) * makeup;
            R[i] = svf.processSample (1, R[i]) * makeup;
        }
        mix (L, dryBuf.getReadPointer (0), p[5], n);
        mix (R, dryBuf.getReadPointer (1), p[5], n);
    }
private:
    double sr = 48000; dsp::StateVariableTPTFilter<float> svf; AudioBuffer<float> dryBuf;
    float env = 0, phase = 0; LinearSmoothedValue<float> pos;
};

//==============================================================================
class DelayBlock final : public Block
{
public:
    DelayBlock() : Block (BlockType::Delay) {}
    void prepare (double s, int mb) override
    {
        sr = s;
        const int size = (int) (s * 2.1) + mb;
        bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
        w = 0; time.reset (s, 0.15); time.setCurrentAndTargetValue (0.38f * (float) s);
        lpL = lpR = 0.0f;
        inBuf.resize ((size_t) mb);
    }
    void reset() override { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }
    void process (float* L, float* R, int n, const float* p, const RigContext& c) override   { run (L, R, n, p, c, false); }
    void processBypassed (float* L, float* R, int n, const float* p, const RigContext& c) override { run (L, R, n, p, c, true); }
private:
    void run (float* L, float* R, int n, const float* p, const RigContext&, bool bypassed)
    {
        if ((int) inBuf.size() < n) return;
        time.setTargetValue (p[0] * 0.001f * (float) sr);
        const float fb = p[1], wet = p[2];
        const float lpCoef = std::exp (-MathConstants<float>::twoPi * p[3] / (float) sr);
        const bool ping = p[4] > 0.5f;
        const int size = (int) bufL.size();
        for (int i = 0; i < n; ++i) inBuf[(size_t) i] = bypassed ? 0.0f : 0.5f * (L[i] + R[i]);
        for (int i = 0; i < n; ++i)
        {
            const float d = time.getNextValue();
            const float yL = read (bufL, d, size), yR = read (bufR, ping ? d : d * 1.013f, size);
            lpL = yL + (lpL - yL) * lpCoef;
            lpR = yR + (lpR - yR) * lpCoef;
            const float x = inBuf[(size_t) i];
            if (ping) { bufL[(size_t) w] = x + lpR * fb; bufR[(size_t) w] = lpL * fb; }
            else      { bufL[(size_t) w] = x + lpL * fb; bufR[(size_t) w] = x + lpR * fb; }
            w = (w + 1) % size;
            L[i] += yL * wet; R[i] += yR * wet;
        }
    }
    float read (const std::vector<float>& b, float delay, int size) const
    {
        float rp = (float) w - delay;
        while (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
        const float f = rp - std::floor (rp);
        return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
    }
    double sr = 48000; std::vector<float> bufL, bufR, inBuf; int w = 0;
    LinearSmoothedValue<float> time; float lpL = 0, lpR = 0;
};

class ReverbBlock final : public Block
{
public:
    ReverbBlock() : Block (BlockType::Reverb) {}
    void prepare (double s, int mb) override
    {
        sr = s;
        verb.prepare ({ s, (uint32) mb, 2 });
        wet.setSize (2, mb);
        pre.assign ((size_t) (s * 0.13) + 4, 0.0f);
        preW = 0;
    }
    void reset() override { verb.reset(); std::fill (pre.begin(), pre.end(), 0.0f); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override         { run (L, R, n, p, false); }
    void processBypassed (float* L, float* R, int n, const float* p, const RigContext&) override { run (L, R, n, p, true); }
private:
    void run (float* L, float* R, int n, const float* p, bool bypassed)
    {
        if (wet.getNumSamples() < n) return;
        Reverb::Parameters rp;
        rp.roomSize = 0.3f + p[0] * 0.68f; rp.damping = p[1]; rp.width = p[2];
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f;
        verb.setParameters (rp);
        auto* wl = wet.getWritePointer (0); auto* wr = wet.getWritePointer (1);
        const int size = (int) pre.size();
        const int d = jlimit (0, size - 1, (int) (p[4] * 0.001f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            pre[(size_t) preW] = bypassed ? 0.0f : 0.5f * (L[i] + R[i]);
            const float x = pre[(size_t) ((preW - d + size) % size)];
            preW = (preW + 1) % size;
            wl[i] = wr[i] = x;
        }
        float* ch[] = { wl, wr };
        dsp::AudioBlock<float> b (ch, 2, (size_t) n);
        verb.process (dsp::ProcessContextReplacing<float> (b));
        const float g = p[3] * 0.6f;
        for (int i = 0; i < n; ++i) { L[i] += wl[i] * g; R[i] += wr[i] * g; }
    }
    double sr = 48000; dsp::Reverb verb; AudioBuffer<float> wet; std::vector<float> pre; int preW = 0;
};

class VolumeBlock final : public Block
{
public:
    VolumeBlock() : Block (BlockType::Volume) {}
    void prepare (double s, int) override { gl.reset (s, 0.03); gr.reset (s, 0.03); }
    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        const float g = dbToGain (p[0]);
        const float angle = (p[1] + 1.0f) * MathConstants<float>::pi * 0.25f;   // constant power
        gl.setTargetValue (g * std::cos (angle) * MathConstants<float>::sqrt2);
        gr.setTargetValue (g * std::sin (angle) * MathConstants<float>::sqrt2);
        for (int i = 0; i < n; ++i) { L[i] *= gl.getNextValue(); R[i] *= gr.getNextValue(); }
    }
private:
    LinearSmoothedValue<float> gl, gr;
};
//==============================================================================
// Octaver. Polyphonic: two crossfaded delay-line pitch shifters per interval (works with chords).
// Analog: OC-2 style flip-flop sub octaves + full-wave-rectifier octave up (single notes, synthy).
class OctaverBlock final : public Block
{
public:
    OctaverBlock() : Block (BlockType::Octaver) {}

    void prepare (double s, int maxBlock) override
    {
        sr = s;
        window = jmax (512, (int) (s * 0.05));   // 50 ms grains
        buf.assign ((size_t) (window * 4), 0.0f);
        w = 0;
        for (auto& sh : shifters) sh.phase = 0.0;
        mono.assign ((size_t) maxBlock, 0.0f);
        resetAnalog();
    }
    void reset() override { std::fill (buf.begin(), buf.end(), 0.0f); resetAnalog(); }

    void process (float* L, float* R, int n, const float* p, const RigContext&) override
    {
        if ((int) mono.size() < n) return;
        const float down2 = p[0], down1 = p[1], up1 = p[2], dry = p[3];
        const float toneHz = 300.0f * std::pow (20.0f, p[4]);          // 300 Hz .. 6 kHz
        const float toneCoef = std::exp (-MathConstants<float>::twoPi * toneHz / (float) sr);
        const bool analog = p[5] > 0.5f;

        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);

        for (int i = 0; i < n; ++i)
        {
            const float x = mono[(size_t) i];
            float wet = 0.0f;
            if (analog)
                wet = analogStep (x, down2, down1, up1);
            else
            {
                const int size = (int) buf.size();
                buf[(size_t) w] = x;
                if (down2 > 0.001f) wet += down2 * shift (shifters[0], 0.25, x);
                if (down1 > 0.001f) wet += down1 * shift (shifters[1], 0.5, x);
                if (up1   > 0.001f) wet += up1   * shift (shifters[2], 2.0, x) * 0.9f;
                w = (w + 1) % size;
            }
            lp = wet + (lp - wet) * toneCoef;                              // tone only darkens the octaves
            L[i] = R[i] = x * dry + lp;
        }
    }

private:
    struct Shifter { double phase = 0.0; };

    float read (double delay) const
    {
        const int size = (int) buf.size();
        double rp = (double) w - delay;
        while (rp < 0.0) rp += size;
        const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
        const float f = (float) (rp - std::floor (rp));
        return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
    }

    // classic two-tap rotating delay: the delay changes at (1 - ratio) samples per sample
    float shift (Shifter& sh, double ratio, float)
    {
        const double W = (double) window;
        sh.phase += (1.0 - ratio);
        while (sh.phase >= W) sh.phase -= W;
        while (sh.phase < 0.0) sh.phase += W;
        const double d1 = sh.phase + 2.0, d2 = std::fmod (sh.phase + W * 0.5, W) + 2.0;
        const float g1 = (float) std::sin (MathConstants<double>::pi * sh.phase / W);
        const float g2 = (float) std::sin (MathConstants<double>::pi * std::fmod (sh.phase + W * 0.5, W) / W);
        return read (d1) * g1 * g1 + read (d2) * g2 * g2;
    }

    float analogStep (float x, float down2, float down1, float up1)
    {
        // track the fundamental: low-pass, then Schmitt-trigger zero crossings drive flip-flops
        trackLp = x + (trackLp - x) * trackCoef;
        trackLp2 = trackLp + (trackLp2 - trackLp) * trackCoef;
        const float e = std::abs (x);
        env = e > env ? env + (e - env) * 0.01f : env * envRelease;
        const float hyst = 0.02f * env + 1e-4f;
        const bool high = trackLp2 > hyst ? true : (trackLp2 < -hyst ? false : lastHigh);
        if (high && ! lastHigh)
        {
            ff1 = -ff1;
            if (ff1 > 0.0f) ff2 = -ff2;
        }
        lastHigh = high;
        const float gate = env > 0.002f ? 1.0f : env / 0.002f;
        float y = (down1 * ff1 + down2 * ff2) * env * 1.6f * gate;
        // octave up: full-wave rectified signal minus its DC
        const float rect = std::abs (x);
        upDc = rect + (upDc - rect) * 0.999f;
        y += up1 * (rect - upDc) * 1.8f;
        return y;
    }

    void resetAnalog()
    {
        trackCoef  = std::exp (-MathConstants<float>::twoPi * 500.0f / (float) sr);
        envRelease = std::exp (-1.0f / (0.08f * (float) sr));
        trackLp = trackLp2 = env = upDc = lp = 0.0f;
        ff1 = ff2 = 1.0f;
        lastHigh = false;
    }

    double sr = 48000.0;
    int window = 2400, w = 0;
    std::vector<float> buf, mono;
    std::array<Shifter, 3> shifters;
    float trackCoef = 0.9f, envRelease = 0.999f, trackLp = 0, trackLp2 = 0, env = 0, upDc = 0, lp = 0;
    float ff1 = 1.0f, ff2 = 1.0f;
    bool lastHigh = false;
};
} // namespace

//==============================================================================
void ModelBlock::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    maxBlockSize = maxBlock;
    slot.prepare (sr, maxBlock);
    mono.assign ((size_t) maxBlock, 0.0f);
    inGain.reset (sr, 0.03);
    outGain.reset (sr, 0.03);
}

void ModelBlock::process (float* L, float* R, int n, const float* p, const RigContext& ctx)
{
    slot.update();
    if (! slot.hasModel() || (int) mono.size() < n)
        return;
    inGain.setTargetValue (dbToGain (p[0]));
    outGain.setTargetValue (dbToGain (p[1]) * (ctx.normalizeModels ? slot.normGain() : 1.0f));
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (L[i] + R[i]) * inGain.getNextValue();
    slot.process (mono.data(), n, p[2], p[3]);
    for (int i = 0; i < n; ++i)
        L[i] = R[i] = mono[(size_t) i] * outGain.getNextValue();
}

//==============================================================================
void CabBlock::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    const dsp::ProcessSpec spec { sr, (uint32) maxBlock, 1 };
    irA.prepare (spec);
    irB.prepare (spec);
    bufA.setSize (1, maxBlock); bufB.setSize (1, maxBlock); dry.setSize (1, maxBlock);
    for (auto* f : { &lowCut, &highCut })
    {
        f->coefficients = new dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        f->prepare (spec);
    }
    lastLow = lastHigh = -1.0f;
    level.reset (sr, 0.03);
}

void CabBlock::reset() { irA.reset(); irB.reset(); lowCut.reset(); highCut.reset(); }

void CabBlock::process (float* L, float* R, int n, const float* p, const RigContext&)
{
    const bool a = loadedA.load(), b = loadedB.load();
    if ((! a && ! b) || bufA.getNumSamples() < n)
        return;

    if (std::abs (lastLow - p[0]) > 0.01f || std::abs (lastHigh - p[1]) > 0.01f)
    {
        lastLow = p[0]; lastHigh = p[1];
        *lowCut.coefficients  = AC::makeHighPass (sampleRate, p[0], 0.707f);
        *highCut.coefficients = AC::makeLowPass  (sampleRate, jmin ((double) p[1], sampleRate * 0.45), 0.707f);
    }

    auto* d  = dry.getWritePointer (0);
    auto* ba = bufA.getWritePointer (0);
    auto* bb = bufB.getWritePointer (0);
    for (int i = 0; i < n; ++i)
        d[i] = ba[i] = bb[i] = 0.5f * (L[i] + R[i]);

    if (a) { float* ch[] = { ba }; dsp::AudioBlock<float> blk (ch, 1, (size_t) n); irA.process (dsp::ProcessContextReplacing<float> (blk)); }
    if (b) { float* ch[] = { bb }; dsp::AudioBlock<float> blk (ch, 1, (size_t) n); irB.process (dsp::ProcessContextReplacing<float> (blk)); }

    // blend: 0 = only A, 1 = only B (falls back to whichever is loaded)
    const float blend = a && b ? p[2] : (a ? 0.0f : 1.0f);
    const float mixAmt = p[4];
    level.setTargetValue (dbToGain (p[3]));
    for (int i = 0; i < n; ++i)
    {
        float y = ba[i] * (1.0f - blend) + bb[i] * blend;
        y = highCut.processSample (lowCut.processSample (y));
        y = d[i] + (y - d[i]) * mixAmt;
        L[i] = R[i] = y * level.getNextValue();
    }
}

//==============================================================================
std::unique_ptr<Block> createBlock (BlockType t)
{
    switch (t)
    {
        case BlockType::Gate:       return std::make_unique<GateBlock>();
        case BlockType::Compressor: return std::make_unique<CompressorBlock>();
        case BlockType::Boost:      return std::make_unique<BoostBlock>();
        case BlockType::Overdrive:  return std::make_unique<DriveBlock> (false);
        case BlockType::Fuzz:       return std::make_unique<DriveBlock> (true);
        case BlockType::Amp:        return std::make_unique<ModelBlock>();
        case BlockType::Cab:        return std::make_unique<CabBlock>();
        case BlockType::ToneEq:     return std::make_unique<ToneEqBlock>();
        case BlockType::ParamEq:    return std::make_unique<ParamEqBlock>();
        case BlockType::Chorus:     return std::make_unique<ChorusBlock> (false);
        case BlockType::Flanger:    return std::make_unique<ChorusBlock> (true);
        case BlockType::Phaser:     return std::make_unique<PhaserBlock>();
        case BlockType::Tremolo:    return std::make_unique<TremoloBlock>();
        case BlockType::Wah:        return std::make_unique<WahBlock>();
        case BlockType::Delay:      return std::make_unique<DelayBlock>();
        case BlockType::Reverb:     return std::make_unique<ReverbBlock>();
        case BlockType::Volume:     return std::make_unique<VolumeBlock>();
        case BlockType::Octaver:    return std::make_unique<OctaverBlock>();
        case BlockType::Empty:
        case BlockType::NumTypes:   break;
    }
    return nullptr;
}
} // namespace td
