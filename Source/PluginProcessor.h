#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "engine/Rig.h"
#include "dsp/Effects.h"
#include "online/Tone3000Client.h"

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

struct UnitState
{
    td::BlockType type = td::BlockType::Empty;
    std::array<SlotStatus, 3> files;   // Amp: [0] model. Cab: [1] IR A, [2] IR B.
};

enum class FsPage { Stomp = 0, Snapshot, Looper };

struct FootswitchAssign
{
    enum Kind { None = 0, ToggleUnit, TapTempo, Tuner } kind = None;
    int unit = -1;
};

enum class LooperState { Empty = 0, Recording, Playing, Overdubbing, Stopped };

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
    // Rig editing (message thread)
    const td::Layout& getLayout() const { return layout; }
    const UnitState& getUnit (int u) const { return unitStates[(size_t) juce::jlimit (0, td::kNumUnits - 1, u)]; }
    int unitAtCell (int cell) const { return juce::isPositiveAndBelow (cell, td::kGridCells) ? layout.cell[(size_t) cell] : -1; }
    int cellOfUnit (int unit) const;
    int addBlock (int cell, td::BlockType type);          // returns unit or -1 (rig full)
    void removeBlock (int cell);
    void moveBlock (int fromCell, int toCell);            // swaps when target is occupied
    void changeBlockType (int cell, td::BlockType type);
    void toggleUnit (int unit);
    bool isUnitOn (int unit) const;
    int findUnitOfType (td::BlockType type) const;
    void resetToDefaultRig();

    // Files: which = 0 model, 1 IR A, 2 IR B
    void loadUnitFile (int unit, int which, const juce::File& file);
    void clearUnitFile (int unit, int which);
    static td::SlotKind kindForFile (const juce::File& f);
    static juce::String modelWildcard() { return "*.nam;*.json;*.aidax"; }
    static juce::String irWildcard()    { return "*.wav;*.aif;*.aiff;*.flac"; }

    // Parameters of a unit
    juce::RangedAudioParameter* unitParam (int unit, int p) const;
    juce::RangedAudioParameter* unitOnParam (int unit) const;
    juce::String unitParamText (int unit, int p) const;

    // Snapshots (auto-save the current one when switching, like hardware)
    int getCurrentSnapshot() const { return currentSnapshot; }
    void recallSnapshot (int index);

    // Footswitches
    FsPage getFsPage() const { return fsPage; }
    void setFsPage (FsPage p) { fsPage = p; sendChangeMessage(); }
    const FootswitchAssign& getFsAssign (int fs) const { return fsAssign[(size_t) juce::jlimit (0, td::kNumFootswitches - 1, fs)]; }
    void setFsAssign (int fs, FootswitchAssign a) { fsAssign[(size_t) fs] = a; sendChangeMessage(); }
    void pressFootswitch (int fs);
    juce::String footswitchLabel (int fs) const;
    bool footswitchLit (int fs) const;
    juce::Colour footswitchColour (int fs) const;

    // Tuner
    bool isTunerOn() const { return tunerOn.load(); }
    void setTunerOn (bool on) { tunerOn.store (on); sendChangeMessage(); }
    float detectPitch();   // message thread, Hz or 0

    // Looper
    LooperState getLooperState() const { return (LooperState) looperState.load(); }
    float getLooperPosition() const;   // 0..1
    void looperRecPlay();              // rec -> play -> overdub -> play ...
    void looperStop();
    void looperClear();

    // Tap tempo -> first delay block
    void tapTempo();
    double getBpm() const { return bpm; }

    // MIDI learn: target = parameter index, or -(100 + footswitch)
    void startMidiLearn (int target) { midiLearnTarget.store (target); }
    bool isLearning() const { return midiLearnTarget.load() != kNoTarget; }
    void clearMidiFor (int target);
    int ccForTarget (int target) const;
    int paramIndex (const juce::RangedAudioParameter* p) const;

    // Library / presets / TONE3000 / status line
    juce::File getLibraryFolder() const;
    void setLibraryFolder (const juce::File& f);
    juce::File getPresetFolder() const;
    juce::Array<juce::File> getPresetFiles() const;
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& f);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void stepPreset (int delta);
    Tone3000Client& tone3000();
    void setOnlineMessage (const juce::String& msg);
    juce::String getOnlineMessage() const;

    juce::AudioProcessorValueTreeState apvts;
    LevelMeter inputMeter, outputMeter;

    static constexpr int kNoTarget = std::numeric_limits<int>::min();

private:
    void timerCallback() override;
    void publishLayout();
    void setUnitBlock (int unit, td::BlockType type, bool resetParams);
    void setParamPlain (juce::RangedAudioParameter* p, float norm);
    juce::ValueTree createStateTree();
    void restoreFromTree (const juce::ValueTree& tree);
    std::vector<float> captureSnapshot() const;
    void applySnapshot (const std::vector<float>& v);
    void handleMidi (const juce::MidiBuffer& midi);
    void runLooper (float* L, float* R, int n);

    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    td::Rig rig;
    td::Layout layout;
    std::array<UnitState, td::kNumUnits> unitStates;
    std::array<std::atomic<int>, td::kNumUnits> loadGeneration {};
    std::array<float, td::kNumUnits> lastQuality {};

    double currentRate = 48000.0;
    int currentBlock = 512;
    juce::AudioBuffer<float> work;
    juce::LinearSmoothedValue<float> inGain, outGain;
    std::atomic<int> rigLatency { 0 };
    int lastLatency = -1;

    // snapshots
    std::array<std::vector<float>, td::kNumSnapshots> snapshots;
    int currentSnapshot = 0;

    // footswitches
    FsPage fsPage = FsPage::Stomp;
    std::array<FootswitchAssign, td::kNumFootswitches> fsAssign;
    std::atomic<int> pendingFootswitches { 0 };   // bitmask from MIDI
    std::atomic<int> pendingProgram { -1 };

    // tuner
    std::atomic<bool> tunerOn { false };
    std::vector<float> tunerRing;
    std::atomic<int> tunerWrite { 0 };

    // looper (audio thread owns the buffer)
    juce::AudioBuffer<float> loopBuf;
    std::atomic<int> looperState { 0 }, looperCommand { 0 };
    int loopLength = 0, loopPos = 0;
    std::atomic<float> loopPosNorm { 0.0f };

    // tap tempo
    double bpm = 120.0;
    juce::Array<double> taps;

    // MIDI learn
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> midiLearnTarget { kNoTarget };
    juce::Array<juce::AudioProcessorParameter*> paramList;

    // misc
    juce::ThreadPool loaderPool { 1 };
    juce::ApplicationProperties appProps;
    juce::String currentPresetName { "INIT" };
    std::unique_ptr<Tone3000Client> t3k;
    juce::String onlineMessage;
    juce::uint32 onlineMessageTime = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ToneDeckProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneDeckProcessor)
};
