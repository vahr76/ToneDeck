#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

// Catalogue of every block that can be placed in the grid.
// Each unit in the rig owns 6 generic, host-automatable parameters (0..1);
// a block type gives them meaning through ParamSpec.
namespace td
{
enum class SlotKind { None, Model, IR };

constexpr int kNumUnits    = 16;   // blocks per preset
constexpr int kGridCols    = 8;
constexpr int kGridRows    = 2;    // row 0 = path A, row 1 = parallel path B
constexpr int kGridCells   = kGridCols * kGridRows;
constexpr int kParamsPerUnit = 6;
constexpr int kNumSnapshots  = 4;
constexpr int kNumFootswitches = 4;

enum class BlockType : int
{
    Empty = 0,
    Gate, Compressor, Boost, Overdrive, Fuzz,
    Amp, Cab,
    ToneEq, ParamEq,
    Chorus, Flanger, Phaser, Tremolo, Wah,
    Delay, Reverb,
    Volume,
    Octaver,      // appended: block type numbers are stored in presets
    NumTypes
};

enum class Category { Dynamics, Drive, AmpCab, Eq, Modulation, Filter, Pitch, Time, Utility };

enum class Fmt { Percent, Db, Hz, Ms, Ratio, Plain, Choice };

struct ParamSpec
{
    const char* name = nullptr;   // nullptr = unused slot
    float min = 0, max = 1, def = 0;
    Fmt fmt = Fmt::Percent;
    bool log = false;             // logarithmic mapping (frequencies, times)
    const char* choices = nullptr; // "Sine|Square" for Fmt::Choice

    bool used() const noexcept { return name != nullptr; }

    float toPlain (float n) const noexcept
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        if (fmt == Fmt::Choice)
            return (float) juce::roundToInt (n * (max - min)) + min;
        return log ? min * std::pow (max / min, n) : min + n * (max - min);
    }

    float toNorm (float v) const noexcept
    {
        v = juce::jlimit (min, max, v);
        if (log) return std::log (v / min) / std::log (max / min);
        return max > min ? (v - min) / (max - min) : 0.0f;
    }

    juce::String format (float v) const
    {
        switch (fmt)
        {
            case Fmt::Percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
            case Fmt::Db:      return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
            case Fmt::Hz:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                                   : (v < 10.0f ? juce::String (v, 2) : juce::String (juce::roundToInt (v))) + " Hz";
            case Fmt::Ms:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
            case Fmt::Ratio:   return juce::String (v, 1) + ":1";
            case Fmt::Plain:   return juce::String (v, 2);
            case Fmt::Choice:
            {
                const auto items = juce::StringArray::fromTokens (choices, "|", "");
                const int i = juce::roundToInt (v - min);
                return juce::isPositiveAndBelow (i, items.size()) ? items[i] : juce::String (i);
            }
        }
        return {};
    }
};

struct BlockInfo
{
    const char* shortName;
    const char* longName;
    Category category;
    juce::uint32 colour;
    std::array<ParamSpec, kParamsPerUnit> params;
    bool usesModelFile = false;   // NAM / AIDA-X / GuitarML
    bool usesIrFile    = false;   // dual IR loader
};

namespace col
{
    constexpr juce::uint32 dyn = 0xff3ddc84, drv = 0xffffa62b, amp = 0xffff4d4d, cab = 0xffe8c547,
                           eq = 0xff2ec4d6, mod = 0xff4dd8a6, filt = 0xffff7ad9, time = 0xff4d8dff,
                           rev = 0xffb067ff, util = 0xff9aa5b8;
}

inline const BlockInfo& blockInfo (BlockType t)
{
    using P = ParamSpec;
    static const std::array<BlockInfo, (size_t) BlockType::NumTypes> table {{
        { "---", "EMPTY", Category::Utility, 0xff2a3346, {} },
        { "NR", "NOISE GATE", Category::Dynamics, col::dyn,
          { P { "THRESH", -90, -20, -65, Fmt::Db }, P { "RELEASE", 5, 500, 80, Fmt::Ms, true } } },
        { "CMP", "COMPRESSOR", Category::Dynamics, col::dyn,
          { P { "THRESH", -50, 0, -24, Fmt::Db }, P { "RATIO", 1, 20, 4, Fmt::Ratio, true },
            P { "ATTACK", 0.5f, 100, 10, Fmt::Ms, true }, P { "RELEASE", 10, 1000, 120, Fmt::Ms, true },
            P { "LEVEL", -12, 24, 6, Fmt::Db }, P { "MIX", 0, 1, 1, Fmt::Percent } } },
        { "BST", "CLEAN BOOST", Category::Drive, col::drv,
          { P { "GAIN", 0, 24, 9, Fmt::Db }, P { "BASS", -12, 12, 0, Fmt::Db }, P { "TREBLE", -12, 12, 0, Fmt::Db } } },
        { "OD", "OVERDRIVE", Category::Drive, col::drv,
          { P { "DRIVE", 0, 1, 0.4f }, P { "TONE", 0, 1, 0.5f }, P { "LEVEL", -24, 12, 0, Fmt::Db }, P { "MIX", 0, 1, 1 } } },
        { "FUZ", "FUZZ", Category::Drive, col::drv,
          { P { "FUZZ", 0, 1, 0.6f }, P { "TONE", 0, 1, 0.5f }, P { "LEVEL", -24, 12, -6, Fmt::Db }, P { "GATE", 0, 1, 0.2f } } },
        { "CAP", "CAPTURE (NAM / AIDA-X / GuitarML)", Category::AmpCab, col::amp,
          { P { "GAIN", -24, 24, 0, Fmt::Db }, P { "VOLUME", -24, 24, 0, Fmt::Db },
            P { "PARAM 1", 0, 1, 0.5f }, P { "PARAM 2", 0, 1, 0.5f }, P { "QUALITY", 0, 1, 1 } }, true },
        { "CAB", "CABINET IR (A/B)", Category::AmpCab, col::cab,
          { P { "LOW CUT", 20, 400, 70, Fmt::Hz, true }, P { "HIGH CUT", 2000, 20000, 12000, Fmt::Hz, true },
            P { "IR BLEND", 0, 1, 0, Fmt::Percent }, P { "LEVEL", -24, 12, 0, Fmt::Db }, P { "MIX", 0, 1, 1 } }, false, true },
        { "EQ", "TONE STACK", Category::Eq, col::eq,
          { P { "BASS", -12, 12, 0, Fmt::Db }, P { "MIDDLE", -12, 12, 0, Fmt::Db }, P { "TREBLE", -12, 12, 0, Fmt::Db },
            P { "PRESENCE", -12, 12, 0, Fmt::Db }, P { "MID FREQ", 250, 3000, 750, Fmt::Hz, true }, P { "LEVEL", -12, 12, 0, Fmt::Db } } },
        { "PEQ", "PARAMETRIC EQ", Category::Eq, col::eq,
          { P { "LOW", -15, 15, 0, Fmt::Db }, P { "MID FREQ", 150, 8000, 1000, Fmt::Hz, true }, P { "MID", -15, 15, 0, Fmt::Db },
            P { "MID Q", 0.3f, 6, 1, Fmt::Plain, true }, P { "HIGH", -15, 15, 0, Fmt::Db }, P { "LEVEL", -12, 12, 0, Fmt::Db } } },
        { "CHO", "CHORUS", Category::Modulation, col::mod,
          { P { "RATE", 0.05f, 8, 0.8f, Fmt::Hz, true }, P { "DEPTH", 0, 1, 0.4f }, P { "DELAY", 2, 30, 8, Fmt::Ms }, P { "MIX", 0, 1, 0.5f } } },
        { "FLG", "FLANGER", Category::Modulation, col::mod,
          { P { "RATE", 0.05f, 5, 0.3f, Fmt::Hz, true }, P { "DEPTH", 0, 1, 0.7f }, P { "FEEDBACK", -0.95f, 0.95f, 0.6f, Fmt::Plain }, P { "MIX", 0, 1, 0.5f } } },
        { "PHA", "PHASER", Category::Modulation, col::mod,
          { P { "RATE", 0.05f, 8, 0.5f, Fmt::Hz, true }, P { "DEPTH", 0, 1, 0.6f }, P { "CENTER", 200, 3000, 900, Fmt::Hz, true },
            P { "FEEDBACK", -0.9f, 0.9f, 0.4f, Fmt::Plain }, P { "MIX", 0, 1, 0.5f } } },
        { "TRM", "TREMOLO", Category::Modulation, col::mod,
          { P { "RATE", 0.5f, 15, 5, Fmt::Hz, true }, P { "DEPTH", 0, 1, 0.5f }, P { "SHAPE", 0, 2, 0, Fmt::Choice, false, "Sine|Triangle|Square" },
            P { "STEREO", 0, 1, 0, Fmt::Choice, false, "Mono|Pan" } } },
        { "WAH", "WAH", Category::Filter, col::filt,
          { P { "PEDAL", 0, 1, 0.5f }, P { "MODE", 0, 2, 0, Fmt::Choice, false, "Manual|Auto|LFO" }, P { "SENS", 0, 1, 0.5f },
            P { "Q", 1, 12, 5, Fmt::Plain, true }, P { "RATE", 0.1f, 8, 1.5f, Fmt::Hz, true }, P { "MIX", 0, 1, 1 } } },
        { "DLY", "DELAY", Category::Time, col::time,
          { P { "TIME", 20, 2000, 380, Fmt::Ms, true }, P { "FEEDBACK", 0, 0.95f, 0.35f }, P { "MIX", 0, 1, 0.25f },
            P { "TONE", 800, 12000, 4500, Fmt::Hz, true }, P { "MODE", 0, 1, 0, Fmt::Choice, false, "Stereo|Ping-Pong" } } },
        { "REV", "REVERB", Category::Time, col::rev,
          { P { "SIZE", 0, 1, 0.55f }, P { "DAMPING", 0, 1, 0.5f }, P { "WIDTH", 0, 1, 1 }, P { "MIX", 0, 1, 0.2f }, P { "PRE-DLY", 0, 120, 10, Fmt::Ms } } },
        { "VOL", "VOLUME / PAN", Category::Utility, col::util,
          { P { "VOLUME", -60, 12, 0, Fmt::Db }, P { "PAN", -1, 1, 0, Fmt::Plain } } },
        { "OCT", "OCTAVER", Category::Pitch, 0xffff8a5c,
          { P { "-2 OCT", 0, 1, 0 }, P { "-1 OCT", 0, 1, 0.7f }, P { "+1 OCT", 0, 1, 0 }, P { "DRY", 0, 1, 0.8f },
            P { "TONE", 0, 1, 0.6f }, P { "MODE", 0, 1, 0, Fmt::Choice, false, "Polyphonic|Analog" } } },
    }};
    return table[(size_t) juce::jlimit (0, (int) BlockType::NumTypes - 1, (int) t)];
}

inline const char* categoryName (Category c)
{
    switch (c)
    {
        case Category::Dynamics:   return "Dinamica";
        case Category::Drive:      return "Drive";
        case Category::AmpCab:     return "Ampli / Gabinete";
        case Category::Eq:         return "EQ";
        case Category::Modulation: return "Modulacion";
        case Category::Filter:     return "Filtro";
        case Category::Time:       return "Delay / Reverb";
        case Category::Pitch:      return "Octavador / Pitch";
        case Category::Utility:    return "Utilidad";
    }
    return "";
}

// Capture gear (from NAM metadata "gear_type" or TONE3000 "gear") ---------------
enum class Gear { Unknown, Pedal, Amp, Rig, Outboard };

inline Gear gearFromString (const juce::String& raw)
{
    const auto g = raw.toLowerCase().replace ("-", "_").trim();
    if (g.isEmpty()) return Gear::Unknown;
    if (g == "pedal") return Gear::Pedal;
    if (g == "outboard" || g == "studio") return Gear::Outboard;
    if (g == "amp" || g == "preamp" || g == "pedal_amp" || g == "amp_pedal") return Gear::Amp;
    if (g.contains ("cab") || g == "full_rig") return Gear::Rig;
    return Gear::Unknown;
}

inline const char* gearShortName (Gear g)
{
    switch (g)
    {
        case Gear::Pedal:    return "PED";
        case Gear::Amp:      return "AMP";
        case Gear::Rig:      return "RIG";
        case Gear::Outboard: return "OUT";
        case Gear::Unknown:  break;
    }
    return "CAP";
}

inline const char* gearLongName (Gear g)
{
    switch (g)
    {
        case Gear::Pedal:    return "CAPTURE - PEDAL";
        case Gear::Amp:      return "CAPTURE - AMP";
        case Gear::Rig:      return "CAPTURE - AMP + CAB";
        case Gear::Outboard: return "CAPTURE - OUTBOARD";
        case Gear::Unknown:  break;
    }
    return "CAPTURE";
}

// Captures can't reproduce time-varying effects; warn when a name suggests one.
inline juce::String timeBasedEffectIn (const juce::String& text)
{
    const auto t = text.toLowerCase();
    for (auto* k : { "chorus", "flanger", "phaser", "phase 90", "tremolo", "vibrato", "univibe", "uni-vibe", "rotary",
                     "leslie", "delay", "echo", "reverb", "wah", "octave", "pitch", "harmonizer", "whammy" })
        if (t.contains (k)) return k;
    return {};
}

// Parameter IDs ----------------------------------------------------------------
inline juce::String unitOnId (int u)              { return "u" + juce::String (u) + "_on"; }
inline juce::String unitParamId (int u, int p)    { return "u" + juce::String (u) + "_p" + juce::String (p); }
} // namespace td
