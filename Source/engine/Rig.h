#pragma once

#include "Blocks.h"

namespace td
{
//==============================================================================
// Grid layout: which unit sits in each cell (-1 = empty). Row 0 = path A, row 1 = path B.
struct Layout
{
    std::array<int8_t, kGridCells> cell;
    Layout() { cell.fill (-1); }

    static int index (int row, int col) noexcept { return row * kGridCols + col; }
    int at (int row, int col) const noexcept     { return cell[(size_t) index (row, col)]; }

    // Columns spanned by path B (split before `first`, merge after `last`); false if B is empty.
    bool parallelRange (int& first, int& last) const noexcept
    {
        first = -1; last = -1;
        for (int c = 0; c < kGridCols; ++c)
            if (at (1, c) >= 0) { if (first < 0) first = c; last = c; }
        return first >= 0;
    }
};

//==============================================================================
// One of the 16 block "units". Its Block object is handed to the audio thread lock-free.
class Unit
{
public:
    ~Unit() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete active; }

    // message thread
    void setBlock (std::unique_ptr<Block> b)
    {
        latest = b.get();
        latestType = b != nullptr ? b->type : BlockType::Empty;
        hasPending = true;
        delete pending.exchange (b.release());
        pendingIsNull.store (latest == nullptr);
    }
    void collectGarbage() { delete trash.exchange (nullptr); }
    Block* getLatest() const noexcept { return latest; }      // message thread view
    BlockType getType() const noexcept { return latestType; }

    // prepareToPlay only
    void prepare (double sr, int maxBlock)
    {
        if (active != nullptr) active->prepare (sr, maxBlock);
        if (auto* p = pending.exchange (nullptr)) { p->prepare (sr, maxBlock); delete pending.exchange (p); }
    }

    // audio thread
    Block* update()
    {
        if (trash.load (std::memory_order_acquire) == nullptr)
        {
            Block* p = pending.exchange (nullptr, std::memory_order_acq_rel);
            if (p != nullptr || pendingIsNull.exchange (false))
            {
                trash.store (active, std::memory_order_release);
                active = p;
            }
        }
        return active;
    }

private:
    std::atomic<Block*> pending { nullptr }, trash { nullptr };
    std::atomic<bool> pendingIsNull { false };
    Block* active = nullptr;
    Block* latest = nullptr;
    BlockType latestType = BlockType::Empty;
    bool hasPending = false;
};

//==============================================================================
// Lock-free single-writer layout publication (seqlock).
class LayoutExchange
{
public:
    void publish (const Layout& l)       // message thread
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd = writing
        data = l;
        seq.fetch_add (1, std::memory_order_acq_rel);
    }
    bool tryRead (Layout& out) const     // audio thread
    {
        const auto s1 = seq.load (std::memory_order_acquire);
        if (s1 & 1) return false;
        out = data;
        std::atomic_thread_fence (std::memory_order_acquire);
        return seq.load (std::memory_order_acquire) == s1;
    }
private:
    std::atomic<uint32_t> seq { 0 };
    Layout data;
};

//==============================================================================
struct UnitParams
{
    std::atomic<float>* on = nullptr;
    std::array<std::atomic<float>*, kParamsPerUnit> p {};
};

class Rig
{
public:
    std::array<Unit, kNumUnits> units;
    LayoutExchange layoutExchange;
    std::array<UnitParams, kNumUnits> params;
    std::atomic<float>* mergeA = nullptr;   // dB
    std::atomic<float>* mergeB = nullptr;
    std::atomic<float>* panA = nullptr;     // -1..1
    std::atomic<float>* panB = nullptr;

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        bufBL.assign ((size_t) maxBlock, 0.0f);
        bufBR.assign ((size_t) maxBlock, 0.0f);
        for (auto& u : units) u.prepare (sr, maxBlock);
        gA.reset (sr, 0.03); gB.reset (sr, 0.03);
    }

    int latency() const
    {
        int l = 0;
        for (int c = 0; c < kGridCols; ++c)
        {
            const int u = audioLayout.at (0, c);
            if (u >= 0 && blocks[(size_t) u] != nullptr) l += blocks[(size_t) u]->latency();
        }
        return l;
    }

    // audio thread
    void process (float* L, float* R, int n, const RigContext& ctx)
    {
        Layout l;
        if (layoutExchange.tryRead (l)) audioLayout = l;
        for (int u = 0; u < kNumUnits; ++u)
            blocks[(size_t) u] = units[(size_t) u].update();

        if ((int) bufBL.size() < n) return;

        int first = -1, last = -1;
        const bool parallel = audioLayout.parallelRange (first, last);

        for (int c = 0; c < kGridCols; ++c)
        {
            if (parallel && c == first)
            {
                std::copy (L, L + n, bufBL.data());
                std::copy (R, R + n, bufBR.data());
            }

            runUnit (audioLayout.at (0, c), L, R, n, ctx);
            if (parallel && c >= first && c <= last)
                runUnit (audioLayout.at (1, c), bufBL.data(), bufBR.data(), n, ctx);

            if (parallel && c == last)
                mergePaths (L, R, n);
        }
    }

private:
    void runUnit (int u, float* L, float* R, int n, const RigContext& ctx)
    {
        if (u < 0) return;
        Block* b = blocks[(size_t) u];
        if (b == nullptr) return;
        const auto& info = blockInfo (b->type);
        const auto& up = params[(size_t) u];
        float plain[kParamsPerUnit];
        for (int i = 0; i < kParamsPerUnit; ++i)
            plain[i] = info.params[(size_t) i].used() && up.p[(size_t) i] != nullptr
                           ? info.params[(size_t) i].toPlain (up.p[(size_t) i]->load (std::memory_order_relaxed))
                           : 0.0f;
        const bool isOn = up.on == nullptr || up.on->load (std::memory_order_relaxed) > 0.5f;
        if (isOn) b->process (L, R, n, plain, ctx);
        else      b->processBypassed (L, R, n, plain, ctx);
    }

    void mergePaths (float* L, float* R, int n)
    {
        const float a = juce::Decibels::decibelsToGain (mergeA ? mergeA->load() : -3.0f);
        const float b = juce::Decibels::decibelsToGain (mergeB ? mergeB->load() : -3.0f);
        gA.setTargetValue (a); gB.setTargetValue (b);
        const float pa = panA ? panA->load() : 0.0f, pb = panB ? panB->load() : 0.0f;
        const auto panL = [] (float p) { return std::cos ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2; };
        const auto panR = [] (float p) { return std::sin ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2; };
        const float aL = panL (pa), aR = panR (pa), bL = panL (pb), bR = panR (pb);
        for (int i = 0; i < n; ++i)
        {
            const float ga = gA.getNextValue(), gb = gB.getNextValue();
            L[i] = L[i] * ga * aL + bufBL[(size_t) i] * gb * bL;
            R[i] = R[i] * ga * aR + bufBR[(size_t) i] * gb * bR;
        }
    }

    double sampleRate = 48000.0;
    Layout audioLayout;
    std::array<Block*, kNumUnits> blocks {};
    std::vector<float> bufBL, bufBR;
    juce::LinearSmoothedValue<float> gA, gB;
};
} // namespace td
