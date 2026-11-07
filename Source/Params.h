#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace td
{
enum Block { Gate = 0, Pedal, Amp, Eq, Cab, Delay, Reverb, NumBlocks };

enum class SlotKind { None, Model, IR };

struct KnobDef
{
    const char* paramId;
    const char* label;
    // For model blocks some knobs only appear when the loaded model supports them.
    enum Visibility { Always, Cond1, Cond2, Slimmable } visibility = Always;
};

struct BlockDef
{
    const char* shortName;   // shown in the LCD chain
    const char* longName;
    const char* onParam;
    juce::uint32 colour;
    SlotKind slot;
    std::vector<KnobDef> knobs;
};

inline const std::array<BlockDef, NumBlocks>& blocks()
{
    static const std::array<BlockDef, NumBlocks> defs {{
        { "NR",  "NOISE GATE", "gate_on", 0xff3ddc84, SlotKind::None,
          { { "gate_thresh", "THRESH" }, { "gate_rel", "RELEASE" } } },
        { "DRV", "PEDAL MODEL", "ped_on", 0xffffa62b, SlotKind::Model,
          { { "ped_in", "DRIVE" }, { "ped_out", "LEVEL" },
            { "ped_p1", "PARAM 1", KnobDef::Cond1 }, { "ped_p2", "PARAM 2", KnobDef::Cond2 },
            { "ped_size", "QUALITY", KnobDef::Slimmable } } },
        { "AMP", "AMP MODEL", "amp_on", 0xffff4d4d, SlotKind::Model,
          { { "amp_in", "GAIN" }, { "amp_out", "VOLUME" },
            { "amp_p1", "PARAM 1", KnobDef::Cond1 }, { "amp_p2", "PARAM 2", KnobDef::Cond2 },
            { "amp_size", "QUALITY", KnobDef::Slimmable } } },
        { "EQ",  "TONE STACK", "eq_on", 0xff2ec4d6, SlotKind::None,
          { { "eq_bass", "BASS" }, { "eq_mid", "MIDDLE" }, { "eq_treble", "TREBLE" }, { "eq_pres", "PRESENCE" }, { "eq_midf", "MID FREQ" } } },
        { "CAB", "CABINET IR", "cab_on", 0xffe8c547, SlotKind::IR,
          { { "cab_lowcut", "LOW CUT" }, { "cab_highcut", "HIGH CUT" }, { "cab_mix", "MIX" }, { "cab_level", "LEVEL" } } },
        { "DLY", "DELAY", "dly_on", 0xff4d8dff, SlotKind::None,
          { { "dly_time", "TIME" }, { "dly_fb", "FEEDBACK" }, { "dly_mix", "MIX" } } },
        { "REV", "REVERB", "rev_on", 0xffb067ff, SlotKind::None,
          { { "rev_size", "SIZE" }, { "rev_damp", "DAMPING" }, { "rev_width", "WIDTH" }, { "rev_mix", "MIX" } } },
    }};
    return defs;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    int v = 1;

    auto db = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, NormalisableRange<float> (lo, hi, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float x, int) { return (x > 0 ? "+" : "") + String (x, 1) + " dB"; })));
    };
    auto pct = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, NormalisableRange<float> (0.0f, 1.0f, 0.001f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x * 100.0f)) + "%"; })));
    };
    auto hz = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi, 1.0f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, r, def,
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float x, int) { return x >= 1000.0f ? String (x / 1000.0f, 1) + " kHz" : String (roundToInt (x)) + " Hz"; })));
    };
    auto ms = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi, 1.0f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, r, def,
            AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + " ms"; })));
    };
    auto onoff = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, v }, name, def));
    };

    db ("in_gain",  "Input",  -24.0f, 24.0f, 0.0f);
    db ("out_gain", "Master", -40.0f, 12.0f, 0.0f);
    onoff ("normalize", "Normalize Models", true);

    onoff ("gate_on", "Gate On", true);
    db ("gate_thresh", "Gate Threshold", -90.0f, -20.0f, -65.0f);
    ms ("gate_rel", "Gate Release", 5.0f, 500.0f, 80.0f);

    onoff ("ped_on", "Pedal On", false);
    db ("ped_in",  "Pedal Drive", -24.0f, 24.0f, 0.0f);
    db ("ped_out", "Pedal Level", -24.0f, 24.0f, 0.0f);
    pct ("ped_p1", "Pedal Param 1", 0.5f);
    pct ("ped_p2", "Pedal Param 2", 0.5f);
    pct ("ped_size", "Pedal Quality", 1.0f);

    onoff ("amp_on", "Amp On", true);
    db ("amp_in",  "Amp Gain", -24.0f, 24.0f, 0.0f);
    db ("amp_out", "Amp Volume", -24.0f, 24.0f, 0.0f);
    pct ("amp_p1", "Amp Param 1", 0.5f);
    pct ("amp_p2", "Amp Param 2", 0.5f);
    pct ("amp_size", "Amp Quality", 1.0f);

    onoff ("eq_on", "EQ On", true);
    db ("eq_bass",   "Bass",     -12.0f, 12.0f, 0.0f);
    db ("eq_mid",    "Middle",   -12.0f, 12.0f, 0.0f);
    db ("eq_treble", "Treble",   -12.0f, 12.0f, 0.0f);
    db ("eq_pres",   "Presence", -12.0f, 12.0f, 0.0f);
    hz ("eq_midf",   "Mid Freq", 250.0f, 3000.0f, 750.0f);

    onoff ("cab_on", "Cab On", true);
    hz ("cab_lowcut",  "Low Cut",  20.0f, 400.0f, 70.0f);
    hz ("cab_highcut", "High Cut", 2000.0f, 20000.0f, 12000.0f);
    pct ("cab_mix", "Cab Mix", 1.0f);
    db ("cab_level", "Cab Level", -24.0f, 12.0f, 0.0f);

    onoff ("dly_on", "Delay On", false);
    ms ("dly_time", "Delay Time", 20.0f, 2000.0f, 380.0f);
    pct ("dly_fb",  "Delay Feedback", 0.35f);
    pct ("dly_mix", "Delay Mix", 0.25f);

    onoff ("rev_on", "Reverb On", false);
    pct ("rev_size",  "Reverb Size", 0.55f);
    pct ("rev_damp",  "Reverb Damping", 0.5f);
    pct ("rev_width", "Reverb Width", 1.0f);
    pct ("rev_mix",   "Reverb Mix", 0.2f);

    return layout;
}
} // namespace td
