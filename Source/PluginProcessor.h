#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "Params.h"
#include "dsp/Effects.h"
#include "dsp/ModelSlot.h"

// What the UI shows about a loaded file (message thread only).
struct SlotStatus
{
    juce::File file;
    juce::String name, format, architecture, error;
    double sampleRate = 0.0;
    bool loading = false, loaded = false, slimmable = false, hasLoudness = false;
    double loudness = 0.0;
    int conditioningParams = 0;
};

class ToneDeckProcessor final : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster,
                                private juce::Timer
{
public:
    ToneDeckProcessor();
    ~ToneDeckProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // File loading (message thread). slot = td::Pedal, td::Amp or td::Cab.
    void loadFile (int slot, const juce::File& file);
    void clearSlot (int slot);
    const SlotStatus& getSlotStatus (int slot) const;
    static td::SlotKind kindForFile (const juce::File& f);
    static juce::String wildcardForSlot (int slot);

    // Library folder (shared by all instances, remembered between sessions)
    juce::File getLibraryFolder() const;
    void setLibraryFolder (const juce::File& f);

    // Presets
    juce::File getPresetFolder() const;
    juce::Array<juce::File> getPresetFiles() const;
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& f);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void stepPreset (int delta);

    juce::AudioProcessorValueTreeState apvts;
    LevelMeter inputMeter, outputMeter;
    std::atomic<float> gateGain { 1.0f };

private:
    void timerCallback() override;
    void updateFilters();
    juce::ValueTree createStateTree();
    void restoreFromTree (const juce::ValueTree& tree);
    ModelSlot& modelSlot (int slot) { return slot == td::Pedal ? pedalSlot : ampSlot; }

    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return apvts.getRawParameterValue (id)->load() > 0.5f; }

    // DSP ---------------------------------------------------------------------
    ModelSlot pedalSlot, ampSlot;
    juce::dsp::Convolution cabinet;   // zero-latency, loads IRs on its own background thread
    std::atomic<bool> cabLoaded { false };
    NoiseGate gate;
    StereoDelay delay;
    juce::dsp::Reverb reverb;

    juce::dsp::IIR::Filter<float> eqBass, eqMid, eqTreble, eqPres, cabLow, cabHigh;
    std::array<float, 8> lastFilterParams {};

    juce::AudioBuffer<float> mono, key, dry;
    juce::LinearSmoothedValue<float> inGain, outGain, pedIn, pedOut, ampIn, ampOut, cabLevel;
    double currentRate = 48000.0;
    int currentBlock = 512;
    bool delayWasOn = false, reverbWasOn = false;
    std::atomic<int> pendingProgram { -1 };

    // Loading -----------------------------------------------------------------
    juce::ThreadPool loaderPool { 1 };
    std::array<SlotStatus, td::NumBlocks> status;
    std::array<std::atomic<int>, td::NumBlocks> loadGeneration {};
    float lastPedSize = -1.0f, lastAmpSize = -1.0f;
    int lastLatency = -1;

    juce::ApplicationProperties appProps;
    juce::String currentPresetName { "INIT" };

    JUCE_DECLARE_WEAK_REFERENCEABLE (ToneDeckProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneDeckProcessor)
};
