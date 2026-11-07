#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace td;

namespace
{
juce::String toStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 2;
    const auto dbText = [] (float x, int) { return (x > 0 ? "+" : "") + String (x, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "in_gain", v }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out_gain", v }, "Master", NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "normalize", v }, "Normalize Models", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "merge_a", v }, "Path A Level", NormalisableRange<float> (-40.0f, 6.0f, 0.1f), -3.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "merge_b", v }, "Path B Level", NormalisableRange<float> (-40.0f, 6.0f, 0.1f), -3.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    const auto panText = [] (float x, int) { return std::abs (x) < 0.02f ? String ("C") : (x < 0 ? "L" : "R") + String (roundToInt (std::abs (x) * 100)); };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pan_a", v }, "Path A Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (panText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pan_b", v }, "Path B Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (panText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "loop_level", v }, "Looper Level", NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f));

    for (int u = 0; u < kNumUnits; ++u)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { unitOnId (u), v }, "Slot " + String (u + 1) + " On", true));
        for (int i = 0; i < kParamsPerUnit; ++i)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { unitParamId (u, i), v },
                                                               "Slot " + String (u + 1) + " Param " + String (i + 1),
                                                               NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f));
    }
    return layout;
}
} // namespace

//==============================================================================
ToneDeckProcessor::ToneDeckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ToneDeck", createLayout())
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "ToneDeck";
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "ToneDeck";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    for (int u = 0; u < kNumUnits; ++u)
    {
        rig.params[(size_t) u].on = apvts.getRawParameterValue (unitOnId (u));
        for (int i = 0; i < kParamsPerUnit; ++i)
            rig.params[(size_t) u].p[(size_t) i] = apvts.getRawParameterValue (unitParamId (u, i));
    }
    rig.mergeA = apvts.getRawParameterValue ("merge_a");
    rig.mergeB = apvts.getRawParameterValue ("merge_b");
    rig.panA   = apvts.getRawParameterValue ("pan_a");
    rig.panB   = apvts.getRawParameterValue ("pan_b");

    for (auto& c : ccMap) c.store (kNoTarget);
    paramList = getParameters();
    tunerRing.assign (8192, 0.0f);
    lastQuality.fill (-1.0f);

    resetToDefaultRig();
    startTimerHz (20);
}

ToneDeckProcessor::~ToneDeckProcessor()
{
    stopTimer();
    t3k.reset();
    loaderPool.removeAllJobs (true, 5000);
}

//==============================================================================
bool ToneDeckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto ok = [] (const juce::AudioChannelSet& s)
    { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (layouts.getMainInputChannelSet()) && ok (layouts.getMainOutputChannelSet());
}

void ToneDeckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate  = sampleRate;
    currentBlock = juce::jmax (1, samplesPerBlock);
    work.setSize (2, currentBlock);
    rig.prepare (sampleRate, currentBlock);
    inGain.reset (sampleRate, 0.03);
    outGain.reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("in_gain")));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("out_gain")));

    loopBuf.setSize (2, (int) (sampleRate * 40.0));   // 40 s stereo looper
    loopBuf.clear();
    loopLength = loopPos = 0;
    looperState.store ((int) LooperState::Empty);
}

//==============================================================================
void ToneDeckProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isProgramChange())
        {
            pendingProgram.store (m.getProgramChangeNumber());
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int value = m.getControllerValue();

            const int learn = midiLearnTarget.load();
            if (learn != kNoTarget)
            {
                for (auto& c : ccMap) if (c.load() == learn) c.store (kNoTarget);
                ccMap[(size_t) cc].store (learn);
                midiLearnTarget.store (kNoTarget);
            }

            const int target = ccMap[(size_t) cc].load();
            if (target == kNoTarget)
                continue;
            if (target >= 0 && target < paramList.size())
            {
                paramList[target]->setValueNotifyingHost ((float) value / 127.0f);
            }
            else if (target <= -100)
            {
                const int fs = -100 - target;
                if (value >= 64 && juce::isPositiveAndBelow (fs, kNumFootswitches))
                    pendingFootswitches.fetch_or (1 << fs);
            }
        }
    }
}

void ToneDeckProcessor::runLooper (float* L, float* R, int n)
{
    const int cmd = looperCommand.exchange (0);
    auto state = (LooperState) looperState.load();
    const int maxLen = loopBuf.getNumSamples();

    if (cmd == 3) { state = LooperState::Empty; loopLength = loopPos = 0; }
    else if (cmd == 2 && state != LooperState::Empty)
    {
        if (state == LooperState::Recording) loopLength = juce::jmax (1, loopPos);
        state = LooperState::Stopped; loopPos = 0;
    }
    else if (cmd == 1)
    {
        switch (state)
        {
            case LooperState::Empty:       state = LooperState::Recording; loopLength = 0; loopPos = 0; break;
            case LooperState::Recording:   loopLength = juce::jmax (1, loopPos); loopPos = 0; state = LooperState::Playing; break;
            case LooperState::Playing:     state = LooperState::Overdubbing; break;
            case LooperState::Overdubbing: state = LooperState::Playing; break;
            case LooperState::Stopped:     state = LooperState::Playing; loopPos = 0; break;
        }
    }

    if (maxLen == 0) return;
    auto* bl = loopBuf.getWritePointer (0);
    auto* br = loopBuf.getWritePointer (1);
    const float level = p ("loop_level");

    for (int i = 0; i < n; ++i)
    {
        if (state == LooperState::Recording)
        {
            bl[loopPos] = L[i]; br[loopPos] = R[i];
            if (++loopPos >= maxLen) { loopLength = maxLen; loopPos = 0; state = LooperState::Playing; }
        }
        else if (state == LooperState::Playing || state == LooperState::Overdubbing)
        {
            const float pl = bl[loopPos], pr = br[loopPos];
            if (state == LooperState::Overdubbing) { bl[loopPos] = pl * 0.98f + L[i]; br[loopPos] = pr * 0.98f + R[i]; }
            L[i] += pl * level; R[i] += pr * level;
            if (++loopPos >= loopLength) loopPos = 0;
        }
    }
    looperState.store ((int) state);
    loopPosNorm.store (loopLength > 0 ? (float) loopPos / (float) loopLength
                                      : (state == LooperState::Recording ? (float) loopPos / (float) maxLen : 0.0f));
}

void ToneDeckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);

    const int n = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0)
        return;
    if (n > work.getNumSamples())
        work.setSize (2, n, false, false, true);

    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    if (numIn > 0) juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
    else           juce::FloatVectorOperations::clear (L, n);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (p ("in_gain")));
    inGain.applyGain (L, n);
    inputMeter.push (L, n);
    juce::FloatVectorOperations::copy (R, L, n);   // guitar in = mono, chain runs stereo

    // tuner tap (pre-chain)
    {
        const int size = (int) tunerRing.size();
        int w = tunerWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { tunerRing[(size_t) w] = L[i]; w = (w + 1) % size; }
        tunerWrite.store (w, std::memory_order_release);
    }

    RigContext ctx;
    ctx.normalizeModels = p ("normalize") > 0.5f;
    ctx.bpm = bpm;
    rig.process (L, R, n, ctx);
    rigLatency.store (rig.latency(), std::memory_order_relaxed);

    runLooper (L, R, n);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (p ("out_gain")));
    const bool mute = tunerOn.load();
    for (int i = 0; i < n; ++i)
    {
        const float g = mute ? 0.0f : outGain.getNextValue();
        L[i] *= g; R[i] *= g;
    }

    buffer.copyFrom (0, 0, L, n);
    if (numOut > 1)
        buffer.copyFrom (1, 0, R, n);
    else
    {
        buffer.addFrom (0, 0, R, n);
        buffer.applyGain (0, 0, n, 0.5f);
    }
    for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, n);
    outputMeter.push (L, n);
}

//==============================================================================
// Rig editing
void ToneDeckProcessor::publishLayout() { rig.layoutExchange.publish (layout); sendChangeMessage(); }

int ToneDeckProcessor::cellOfUnit (int unit) const
{
    for (int c = 0; c < kGridCells; ++c)
        if (layout.cell[(size_t) c] == unit) return c;
    return -1;
}

void ToneDeckProcessor::setParamPlain (juce::RangedAudioParameter* prm, float norm)
{
    if (prm == nullptr) return;
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (norm);
    prm->endChangeGesture();
}

void ToneDeckProcessor::setUnitBlock (int unit, BlockType type, bool resetParams)
{
    auto& st = unitStates[(size_t) unit];
    st.type = type;
    st.files = {};
    ++loadGeneration[(size_t) unit];
    lastQuality[(size_t) unit] = -1.0f;

    auto block = createBlock (type);
    if (block != nullptr)
        block->prepare (currentRate, currentBlock);
    rig.units[(size_t) unit].setBlock (std::move (block));

    if (resetParams)
    {
        const auto& info = blockInfo (type);
        setParamPlain (unitOnParam (unit), 1.0f);
        for (int i = 0; i < kParamsPerUnit; ++i)
        {
            const auto& spec = info.params[(size_t) i];
            setParamPlain (unitParam (unit, i), spec.used() ? spec.toNorm (spec.def) : 0.0f);
        }
    }
}

int ToneDeckProcessor::addBlock (int cell, BlockType type)
{
    if (! juce::isPositiveAndBelow (cell, kGridCells) || type == BlockType::Empty)
        return -1;
    if (layout.cell[(size_t) cell] >= 0)
    {
        changeBlockType (cell, type);
        return layout.cell[(size_t) cell];
    }
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (unitStates[(size_t) u].type != BlockType::Empty)
            continue;
        setUnitBlock (u, type, true);
        layout.cell[(size_t) cell] = (int8_t) u;
        publishLayout();
        return u;
    }
    setOnlineMessage ("Rig lleno: maximo " + juce::String (kNumUnits) + " bloques");
    return -1;
}

void ToneDeckProcessor::removeBlock (int cell)
{
    const int u = unitAtCell (cell);
    if (u < 0) return;
    layout.cell[(size_t) cell] = -1;
    publishLayout();
    setUnitBlock (u, BlockType::Empty, false);
    for (auto& a : fsAssign)
        if (a.kind == FootswitchAssign::ToggleUnit && a.unit == u) a = {};
    sendChangeMessage();
}

void ToneDeckProcessor::moveBlock (int fromCell, int toCell)
{
    if (fromCell == toCell || ! juce::isPositiveAndBelow (fromCell, kGridCells) || ! juce::isPositiveAndBelow (toCell, kGridCells))
        return;
    std::swap (layout.cell[(size_t) fromCell], layout.cell[(size_t) toCell]);
    publishLayout();
}

void ToneDeckProcessor::changeBlockType (int cell, BlockType type)
{
    const int u = unitAtCell (cell);
    if (u < 0) { addBlock (cell, type); return; }
    if (unitStates[(size_t) u].type == type) return;
    setUnitBlock (u, type, true);
    sendChangeMessage();
}

void ToneDeckProcessor::toggleUnit (int unit)
{
    if (auto* prm = unitOnParam (unit))
        setParamPlain (prm, prm->getValue() > 0.5f ? 0.0f : 1.0f);
    sendChangeMessage();
}

bool ToneDeckProcessor::isUnitOn (int unit) const
{
    auto* prm = unitOnParam (unit);
    return prm != nullptr && prm->getValue() > 0.5f;
}

int ToneDeckProcessor::findUnitOfType (BlockType type) const
{
    for (int c = 0; c < kGridCells; ++c)
    {
        const int u = layout.cell[(size_t) c];
        if (u >= 0 && unitStates[(size_t) u].type == type) return u;
    }
    return -1;
}

void ToneDeckProcessor::resetToDefaultRig()
{
    for (int c = 0; c < kGridCells; ++c)
        if (layout.cell[(size_t) c] >= 0) removeBlock (c);
    layout = {};

    const BlockType chain[] = { BlockType::Gate, BlockType::Overdrive, BlockType::Amp, BlockType::ToneEq,
                                BlockType::Cab, BlockType::Delay, BlockType::Reverb };
    int units[7] {};
    for (int i = 0; i < 7; ++i)
        units[i] = addBlock (Layout::index (0, i), chain[i]);

    setParamPlain (unitOnParam (units[1]), 0.0f);   // OD, delay, reverb start bypassed
    setParamPlain (unitOnParam (units[5]), 0.0f);
    setParamPlain (unitOnParam (units[6]), 0.0f);

    fsAssign[0] = { FootswitchAssign::ToggleUnit, units[1] };
    fsAssign[1] = { FootswitchAssign::ToggleUnit, units[5] };
    fsAssign[2] = { FootswitchAssign::ToggleUnit, units[6] };
    fsAssign[3] = { FootswitchAssign::Tuner, -1 };
    for (auto& s : snapshots) s.clear();
    currentSnapshot = 0;
    currentPresetName = "INIT";
    sendChangeMessage();
}

int ToneDeckProcessor::insertBlock (int cell, BlockType type)
{
    if (! juce::isPositiveAndBelow (cell, kGridCells)) return -1;
    if (layout.cell[(size_t) cell] < 0) return addBlock (cell, type);

    // shift this row right from `cell` into the first free slot after it
    const int row = cell / kGridCols, col = cell % kGridCols;
    int freeCol = -1;
    for (int c = col + 1; c < kGridCols; ++c)
        if (layout.at (row, c) < 0) { freeCol = c; break; }
    if (freeCol < 0)
    {
        for (int c = col - 1; c >= 0; --c)   // or use a free slot just before it
            if (layout.at (row, c) < 0) return addBlock (Layout::index (row, c), type);
        setOnlineMessage ("No hay lugar en esta fila para otro bloque");
        return -1;
    }
    for (int c = freeCol; c > col; --c)
        layout.cell[(size_t) Layout::index (row, c)] = layout.cell[(size_t) Layout::index (row, c - 1)];
    layout.cell[(size_t) cell] = -1;
    return addBlock (cell, type);
}

void ToneDeckProcessor::autoCabForCapture (int unit)
{
    // A "RIG" capture (amp + cab) already contains the speaker: bypass the cab blocks that
    // follow it in the same path. An "AMP" (head only) capture needs one: switch them back on.
    const auto gear = unitGear (unit);
    if (gear != Gear::Rig && gear != Gear::Amp) return;
    const int cell = cellOfUnit (unit);
    if (cell < 0) return;
    const int row = cell / kGridCols;
    int changed = 0;
    for (int c = cell % kGridCols + 1; c < kGridCols; ++c)
    {
        const int u = layout.at (row, c);
        if (u < 0 || unitStates[(size_t) u].type != BlockType::Cab) continue;
        const bool wantOn = gear == Gear::Amp;
        if (isUnitOn (u) != wantOn) { setParamPlain (unitOnParam (u), wantOn ? 1.0f : 0.0f); ++changed; }
    }
    if (changed > 0)
        setOnlineMessage (gear == Gear::Rig ? "RIG (ampli + gabinete): CAB apagado para no tener dos parlantes"
                                            : "AMP (solo cabezal): CAB encendido");
}

td::Gear ToneDeckProcessor::unitGear (int unit) const
{
    const auto& st = getUnit (unit);
    return st.type == BlockType::Amp ? gearFromString (st.files[0].gear) : Gear::Unknown;
}

juce::String ToneDeckProcessor::unitShortName (int unit) const
{
    const auto& st = getUnit (unit);
    return st.type == BlockType::Amp ? juce::String (gearShortName (unitGear (unit))) : juce::String (blockInfo (st.type).shortName);
}

juce::String ToneDeckProcessor::unitLongName (int unit) const
{
    const auto& st = getUnit (unit);
    return st.type == BlockType::Amp ? juce::String (gearLongName (unitGear (unit))) : juce::String (blockInfo (st.type).longName);
}

juce::String ToneDeckProcessor::peekGear (const juce::File& f)
{
    // cheap look into the first part of a .nam file's metadata
    // metadata may sit before or after the weights: look at both ends
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    juce::MemoryBlock head, tail;
    in.readIntoMemoryBlock (head, 8192);
    if (in.getTotalLength() > 16384)
    {
        in.setPosition (in.getTotalLength() - 8192);
        in.readIntoMemoryBlock (tail, 8192);
    }
    const auto text = head.toString() + " " + tail.toString();
    const auto pos = text.indexOf ("\"gear_type\"");
    if (pos < 0) return {};
    const auto rest = text.substring (pos + 11).fromFirstOccurrenceOf ("\"", false, false);
    return rest.upToFirstOccurrenceOf ("\"", false, false);
}

juce::RangedAudioParameter* ToneDeckProcessor::unitParam (int unit, int i) const
{
    return juce::isPositiveAndBelow (unit, kNumUnits) ? apvts.getParameter (unitParamId (unit, i)) : nullptr;
}

juce::RangedAudioParameter* ToneDeckProcessor::unitOnParam (int unit) const
{
    return juce::isPositiveAndBelow (unit, kNumUnits) ? apvts.getParameter (unitOnId (unit)) : nullptr;
}

juce::String ToneDeckProcessor::unitParamText (int unit, int i) const
{
    const auto& spec = blockInfo (getUnit (unit).type).params[(size_t) i];
    auto* prm = unitParam (unit, i);
    return spec.used() && prm != nullptr ? spec.format (spec.toPlain (prm->getValue())) : juce::String();
}

//==============================================================================
// Files
td::SlotKind ToneDeckProcessor::kindForFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".nam" || ext == ".json" || ext == ".aidax") return SlotKind::Model;
    if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac") return SlotKind::IR;
    return SlotKind::None;
}

void ToneDeckProcessor::clearUnitFile (int unit, int which)
{
    auto& st = unitStates[(size_t) unit];
    st.files[(size_t) which] = {};
    if (auto* cab = dynamic_cast<CabBlock*> (rig.units[(size_t) unit].getLatest()))
    {
        if (which == 1) cab->loadedA.store (false);
        if (which == 2) cab->loadedB.store (false);
    }
    else if (auto* mb = dynamic_cast<ModelBlock*> (rig.units[(size_t) unit].getLatest()))
    {
        ++loadGeneration[(size_t) unit];
        mb->slot.setInstance (std::make_unique<ModelInstance>());
    }
    sendChangeMessage();
}

void ToneDeckProcessor::loadUnitFile (int unit, int which, const juce::File& file, const juce::String& gearHint, bool autoCab)
{
    if (! juce::isPositiveAndBelow (unit, kNumUnits)) return;
    auto& st = unitStates[(size_t) unit].files[(size_t) which];

    if (! file.existsAsFile())
    {
        st = {};
        st.file = file;
        st.name = file.getFileNameWithoutExtension();
        st.error = "ARCHIVO NO ENCONTRADO";
        sendChangeMessage();
        return;
    }

    if (which == 1 || which == 2)
    {
        auto* cab = dynamic_cast<CabBlock*> (rig.units[(size_t) unit].getLatest());
        st = {};
        st.file = file;
        st.name = file.getFileNameWithoutExtension();
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (cab != nullptr && r != nullptr)
        {
            st.sampleRate   = r->sampleRate;
            st.format       = "IR";
            st.architecture = juce::String (juce::roundToInt ((double) r->lengthInSamples / juce::jmax (1.0, r->sampleRate) * 1000.0)) + " ms";
            auto& conv = which == 1 ? cab->irA : cab->irB;
            conv.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0,
                                      juce::dsp::Convolution::Normalise::yes);
            (which == 1 ? cab->loadedA : cab->loadedB).store (true);
            st.loaded = true;
        }
        else
        {
            st.error = "IR ILEGIBLE";
        }
        sendChangeMessage();
        return;
    }

    // model: parse + pre-warm in the background
    st.loading = true;
    st.loaded = false;
    st.error.clear();
    st.file = file;
    st.name = file.getFileNameWithoutExtension();
    st.gear = gearHint.isNotEmpty() ? gearHint : peekGear (file);
    sendChangeMessage();

    const int gen      = ++loadGeneration[(size_t) unit];
    const double rate  = currentRate;
    const int block    = currentBlock;
    auto* qp           = unitParam (unit, 4);
    const double size  = qp != nullptr ? qp->getValue() : 1.0;
    juce::WeakReference<ToneDeckProcessor> weak (this);

    loaderPool.addJob ([weak, unit, file, gen, rate, block, size, gearHint, autoCab]
    {
        const auto text = file.loadFileAsString().toStdString();
        const auto stem = file.getFileNameWithoutExtension().toStdString();
        std::string err;
        std::unique_ptr<ModelEngine> engine;
        if (file.getFileExtension().toLowerCase() == ".nam")
            engine = createNamEngine (text, stem, err);
        else
        {
            engine = createAidaEngine (text, stem, err);
            if (engine == nullptr)
            {
                std::string err2;
                engine = createNamEngine (text, stem, err2);
                if (engine != nullptr) err.clear();
            }
        }

        SlotStatus result;
        result.file = file;
        result.name = file.getFileNameWithoutExtension();
        auto inst = std::make_shared<std::unique_ptr<ModelInstance>>();

        if (engine != nullptr)
        {
            const auto info = engine->info;
            const bool slim = engine->isSlimmable();
            if (slim) engine->setSlimmableSize (size);
            auto i = std::make_unique<ModelInstance>();
            i->engine = std::move (engine);
            i->file = file;
            if (info.hasLoudness)
                i->normGain = juce::Decibels::decibelsToGain ((float) (-18.0 - info.loudnessDb));
            i->prepare (rate, block);
            *inst = std::move (i);

            result.loaded             = true;
            result.format             = toStd (info.format);
            result.architecture       = toStd (info.architecture);
            result.sampleRate         = info.sampleRate;
            result.slimmable          = slim;
            result.hasLoudness        = info.hasLoudness;
            result.loudness           = info.loudnessDb;
            result.conditioningParams = info.numConditioningParams;
            result.gear               = gearHint.isNotEmpty() ? gearHint : toStd (info.gearType);
        }
        else
        {
            result.error = "ERROR AL CARGAR: " + toStd (err);
        }

        juce::MessageManager::callAsync ([weak, unit, gen, result, inst, autoCab]
        {
            auto* self = weak.get();
            if (self == nullptr || gen != self->loadGeneration[(size_t) unit])
                return;
            if (*inst != nullptr)
            {
                auto* mb = dynamic_cast<ModelBlock*> (self->rig.units[(size_t) unit].getLatest());
                if (mb == nullptr) return;
                if (std::abs ((*inst)->hostRate - self->currentRate) > 0.5)   // host rate changed while loading
                    (*inst)->prepare (self->currentRate, self->currentBlock);
                mb->slot.setInstance (std::move (*inst));
            }
            self->unitStates[(size_t) unit].files[0] = result;
            if (result.loaded && autoCab)
                self->autoCabForCapture (unit);
            self->sendChangeMessage();
        });
    });
}

//==============================================================================
// Snapshots
std::vector<float> ToneDeckProcessor::captureSnapshot() const
{
    std::vector<float> v;
    v.reserve (kNumUnits * (kParamsPerUnit + 1) + 4);
    for (int u = 0; u < kNumUnits; ++u)
    {
        v.push_back (unitOnParam (u)->getValue());
        for (int i = 0; i < kParamsPerUnit; ++i) v.push_back (unitParam (u, i)->getValue());
    }
    for (auto* id : { "merge_a", "merge_b", "pan_a", "pan_b" })
        v.push_back (apvts.getParameter (id)->getValue());
    return v;
}

void ToneDeckProcessor::applySnapshot (const std::vector<float>& v)
{
    if (v.size() != (size_t) (kNumUnits * (kParamsPerUnit + 1) + 4)) return;
    size_t k = 0;
    const auto set = [this, &v, &k] (juce::RangedAudioParameter* prm)
    {
        const float val = v[k++];
        if (prm != nullptr && std::abs (prm->getValue() - val) > 1e-5f) setParamPlain (prm, val);
    };
    for (int u = 0; u < kNumUnits; ++u)
    {
        set (unitOnParam (u));
        for (int i = 0; i < kParamsPerUnit; ++i) set (unitParam (u, i));
    }
    for (auto* id : { "merge_a", "merge_b", "pan_a", "pan_b" })
        set (apvts.getParameter (id));
}

void ToneDeckProcessor::recallSnapshot (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumSnapshots)) return;
    snapshots[(size_t) currentSnapshot] = captureSnapshot();   // auto-save the one we leave
    if (snapshots[(size_t) index].empty())
        snapshots[(size_t) index] = snapshots[(size_t) currentSnapshot];   // new snapshots start as a copy
    currentSnapshot = index;
    applySnapshot (snapshots[(size_t) index]);
    setOnlineMessage ("SNAPSHOT " + juce::String (index + 1));
}

//==============================================================================
// Footswitches, tuner, looper, tap
void ToneDeckProcessor::pressFootswitch (int fs)
{
    if (! juce::isPositiveAndBelow (fs, kNumFootswitches)) return;
    switch (fsPage)
    {
        case FsPage::Stomp:
        {
            const auto& a = fsAssign[(size_t) fs];
            if (a.kind == FootswitchAssign::ToggleUnit && a.unit >= 0) toggleUnit (a.unit);
            else if (a.kind == FootswitchAssign::TapTempo) tapTempo();
            else if (a.kind == FootswitchAssign::Tuner) setTunerOn (! isTunerOn());
            break;
        }
        case FsPage::Snapshot: recallSnapshot (fs); break;
        case FsPage::Looper:
            if (fs == 0) looperRecPlay();
            else if (fs == 1) looperStop();
            else if (fs == 2) looperClear();
            else setFsPage (FsPage::Stomp);
            break;
    }
    sendChangeMessage();
}

juce::String ToneDeckProcessor::footswitchLabel (int fs) const
{
    switch (fsPage)
    {
        case FsPage::Stomp:
        {
            const auto& a = fsAssign[(size_t) fs];
            if (a.kind == FootswitchAssign::ToggleUnit && a.unit >= 0) return unitShortName (a.unit);
            if (a.kind == FootswitchAssign::TapTempo) return "TAP " + juce::String (juce::roundToInt (bpm));
            if (a.kind == FootswitchAssign::Tuner) return "TUNER";
            return "---";
        }
        case FsPage::Snapshot: return "SNAP " + juce::String (fs + 1);
        case FsPage::Looper:
        {
            if (fs == 0)
            {
                switch (getLooperState())
                {
                    case LooperState::Recording:   return "PLAY";
                    case LooperState::Playing:     return "DUB";
                    case LooperState::Overdubbing: return "PLAY";
                    case LooperState::Stopped:     return "PLAY";
                    case LooperState::Empty:       break;
                }
                return "REC";
            }
            return fs == 1 ? "STOP" : (fs == 2 ? "CLEAR" : "EXIT");
        }
    }
    return {};
}

bool ToneDeckProcessor::footswitchLit (int fs) const
{
    switch (fsPage)
    {
        case FsPage::Stomp:
        {
            const auto& a = fsAssign[(size_t) fs];
            if (a.kind == FootswitchAssign::ToggleUnit) return isUnitOn (a.unit);
            if (a.kind == FootswitchAssign::Tuner) return isTunerOn();
            if (a.kind == FootswitchAssign::TapTempo) return (juce::Time::getMillisecondCounter() % (juce::uint32) (60000.0 / bpm)) < 80;
            return false;
        }
        case FsPage::Snapshot: return fs == currentSnapshot;
        case FsPage::Looper:
        {
            const auto s = getLooperState();
            if (fs == 0) return s == LooperState::Recording || s == LooperState::Overdubbing || s == LooperState::Playing;
            if (fs == 1) return s == LooperState::Stopped;
            return false;
        }
    }
    return false;
}

juce::Colour ToneDeckProcessor::footswitchColour (int fs) const
{
    switch (fsPage)
    {
        case FsPage::Stomp:
        {
            const auto& a = fsAssign[(size_t) fs];
            if (a.kind == FootswitchAssign::ToggleUnit && a.unit >= 0) return juce::Colour (blockInfo (getUnit (a.unit).type).colour);
            return juce::Colour (0xff34d1ff);
        }
        case FsPage::Snapshot: return juce::Colour (0xffffd84d);
        case FsPage::Looper:
        {
            const auto s = getLooperState();
            if (fs == 0) return s == LooperState::Recording || s == LooperState::Overdubbing ? juce::Colour (0xffff3b3b) : juce::Colour (0xff3ddc84);
            return juce::Colour (0xffffa62b);
        }
    }
    return juce::Colours::white;
}

float ToneDeckProcessor::detectPitch()
{
    // YIN on the last ~85 ms, decimated x4 (good down to ~25 Hz, drop tunings included)
    constexpr int kDecim = 4, N = 1024, maxTau = 512;
    const int size = (int) tunerRing.size();
    const int w = tunerWrite.load (std::memory_order_acquire);
    std::array<float, N> x {};
    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float s = 0.0f;
        for (int k = 0; k < kDecim; ++k)
            s += tunerRing[(size_t) ((w - (N - i) * kDecim + k + size * 4) % size)];
        x[(size_t) i] = s / (float) kDecim;
        peak = juce::jmax (peak, std::abs (x[(size_t) i]));
    }
    if (peak < 0.003f) return 0.0f;

    std::array<float, maxTau> d {};
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau < maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int i = 0; i < N - maxTau; ++i) { const float diff = x[(size_t) i] - x[(size_t) (i + tau)]; sum += diff * diff; }
        running += sum;
        d[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
    }
    const double sr = currentRate / kDecim;
    for (int tau = 2; tau < maxTau - 1; ++tau)
    {
        if (d[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < maxTau && d[(size_t) (tau + 1)] < d[(size_t) tau]) ++tau;
            const float a = d[(size_t) (tau - 1)], b = d[(size_t) tau], c = d[(size_t) (tau + 1)];
            const float denom = a - 2.0f * b + c;
            const float shift = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
            return (float) (sr / ((double) tau + shift));
        }
    }
    return 0.0f;
}

float ToneDeckProcessor::getLooperPosition() const { return loopPosNorm.load(); }
void ToneDeckProcessor::looperRecPlay() { looperCommand.store (1); }
void ToneDeckProcessor::looperStop()    { looperCommand.store (2); }
void ToneDeckProcessor::looperClear()   { looperCommand.store (3); }

void ToneDeckProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    while (taps.size() > 4) taps.remove (0);
    if (taps.size() >= 2)
    {
        const double interval = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
        bpm = juce::jlimit (40.0, 300.0, 60000.0 / interval);
        for (int c = 0; c < kGridCells; ++c)
        {
            const int u = layout.cell[(size_t) c];
            if (u >= 0 && unitStates[(size_t) u].type == BlockType::Delay)
            {
                const auto& spec = blockInfo (BlockType::Delay).params[0];
                setParamPlain (unitParam (u, 0), spec.toNorm ((float) interval));
            }
        }
        setOnlineMessage ("TAP: " + juce::String (juce::roundToInt (bpm)) + " BPM");
    }
}

void ToneDeckProcessor::clearMidiFor (int target)
{
    for (auto& c : ccMap) if (c.load() == target) c.store (kNoTarget);
}

int ToneDeckProcessor::ccForTarget (int target) const
{
    for (int i = 0; i < 128; ++i) if (ccMap[(size_t) i].load() == target) return i;
    return -1;
}

int ToneDeckProcessor::paramIndex (const juce::RangedAudioParameter* prm) const
{
    return paramList.indexOf (const_cast<juce::RangedAudioParameter*> (prm));
}

//==============================================================================
void ToneDeckProcessor::timerCallback()
{
    for (int u = 0; u < kNumUnits; ++u)
    {
        rig.units[(size_t) u].collectGarbage();
        if (auto* mb = dynamic_cast<ModelBlock*> (rig.units[(size_t) u].getLatest()))
        {
            mb->slot.collectGarbage();
            const float q = unitParam (u, 4)->getValue();
            if (! juce::exactlyEqual (q, lastQuality[(size_t) u])) { lastQuality[(size_t) u] = q; mb->slot.setSlimmableSize (q); }
        }
    }

    const int lat = rigLatency.load();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples (lat); }

    if (const int fsMask = pendingFootswitches.exchange (0))
        for (int fs = 0; fs < kNumFootswitches; ++fs)
            if (fsMask & (1 << fs)) pressFootswitch (fs);

    const int prog = pendingProgram.exchange (-1);
    if (prog >= 0)
    {
        const auto files = getPresetFiles();
        if (juce::isPositiveAndBelow (prog, files.size())) loadPreset (files[prog]);
    }
}

//==============================================================================
// Library, presets, TONE3000
juce::File ToneDeckProcessor::getLibraryFolder() const
{
    auto* props = const_cast<juce::ApplicationProperties&> (appProps).getUserSettings();
    const auto path = props != nullptr ? props->getValue ("libraryFolder") : juce::String();
    juce::File f (path);
    if (path.isNotEmpty() && f.isDirectory()) return f;
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ToneDeck").getChildFile ("Library");
}

void ToneDeckProcessor::setLibraryFolder (const juce::File& f)
{
    if (auto* props = appProps.getUserSettings()) { props->setValue ("libraryFolder", f.getFullPathName()); props->saveIfNeeded(); }
}

juce::File ToneDeckProcessor::getPresetFolder() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ToneDeck").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> ToneDeckProcessor::getPresetFiles() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.tdpreset");
    files.sort();
    return files;
}

bool ToneDeckProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    currentPresetName = clean;
    if (auto xml = createStateTree().createXml())
        if (xml->writeTo (getPresetFolder().getChildFile (clean + ".tdpreset")))
        {
            sendChangeMessage();
            return true;
        }
    return false;
}

bool ToneDeckProcessor::loadPreset (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts.state.getType()) && tree.getChildWithName ("RIG").isValid())
        {
            restoreFromTree (tree);
            currentPresetName = f.getFileNameWithoutExtension();
            sendChangeMessage();
            return true;
        }
    }
    setOnlineMessage ("Preset incompatible: " + f.getFileNameWithoutExtension());
    return false;
}

void ToneDeckProcessor::stepPreset (int delta)
{
    const auto files = getPresetFiles();
    if (files.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == currentPresetName) idx = i;
    idx = idx < 0 ? 0 : (idx + delta + files.size()) % files.size();
    loadPreset (files[idx]);
}

Tone3000Client& ToneDeckProcessor::tone3000()
{
    if (t3k == nullptr)
    {
        t3k = std::make_unique<Tone3000Client> (*appProps.getUserSettings(), [this] { return getLibraryFolder(); });
        t3k->onStatus = [this] (const juce::String& m) { setOnlineMessage (m); };
    }
    return *t3k;
}

void ToneDeckProcessor::setOnlineMessage (const juce::String& msg)
{
    onlineMessage = msg;
    onlineMessageTime = juce::Time::getMillisecondCounter();
    sendChangeMessage();
}

juce::String ToneDeckProcessor::getOnlineMessage() const
{
    return juce::Time::getMillisecondCounter() - onlineMessageTime < 6000 ? onlineMessage : juce::String();
}

//==============================================================================
// State
juce::ValueTree ToneDeckProcessor::createStateTree()
{
    snapshots[(size_t) currentSnapshot] = captureSnapshot();

    auto tree = apvts.copyState();
    for (auto* name : { "RIG", "SNAPSHOTS", "FOOTSWITCHES", "MIDI" })
        tree.removeChild (tree.getChildWithName (name), nullptr);
    tree.setProperty ("presetName", currentPresetName, nullptr);
    tree.setProperty ("format", 2, nullptr);

    juce::ValueTree rigTree ("RIG");
    juce::StringArray cells;
    for (auto c : layout.cell) cells.add (juce::String ((int) c));
    rigTree.setProperty ("cells", cells.joinIntoString (","), nullptr);
    for (int u = 0; u < kNumUnits; ++u)
    {
        const auto& st = unitStates[(size_t) u];
        if (st.type == BlockType::Empty) continue;
        juce::ValueTree ut ("UNIT");
        ut.setProperty ("index", u, nullptr);
        ut.setProperty ("type", (int) st.type, nullptr);
        for (int f = 0; f < 3; ++f)
            if (st.files[(size_t) f].file != juce::File())
                ut.setProperty ("file" + juce::String (f), st.files[(size_t) f].file.getFullPathName(), nullptr);
        if (st.files[0].gear.isNotEmpty())
            ut.setProperty ("gear", st.files[0].gear, nullptr);
        rigTree.appendChild (ut, nullptr);
    }
    tree.appendChild (rigTree, nullptr);

    juce::ValueTree snaps ("SNAPSHOTS");
    snaps.setProperty ("current", currentSnapshot, nullptr);
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        if (snapshots[(size_t) i].empty()) continue;
        juce::StringArray vals;
        for (float v : snapshots[(size_t) i]) vals.add (juce::String (v, 5));
        juce::ValueTree s ("SNAP");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("values", vals.joinIntoString (","), nullptr);
        snaps.appendChild (s, nullptr);
    }
    tree.appendChild (snaps, nullptr);

    juce::ValueTree fsTree ("FOOTSWITCHES");
    for (int i = 0; i < kNumFootswitches; ++i)
    {
        juce::ValueTree f ("FS");
        f.setProperty ("index", i, nullptr);
        f.setProperty ("kind", (int) fsAssign[(size_t) i].kind, nullptr);
        f.setProperty ("unit", fsAssign[(size_t) i].unit, nullptr);
        fsTree.appendChild (f, nullptr);
    }
    tree.appendChild (fsTree, nullptr);

    juce::ValueTree midiTree ("MIDI");
    for (int cc = 0; cc < 128; ++cc)
    {
        const int t = ccMap[(size_t) cc].load();
        if (t == kNoTarget) continue;
        juce::ValueTree m ("MAP");
        m.setProperty ("cc", cc, nullptr);
        m.setProperty ("target", t, nullptr);
        midiTree.appendChild (m, nullptr);
    }
    tree.appendChild (midiTree, nullptr);
    return tree;
}

void ToneDeckProcessor::restoreFromTree (const juce::ValueTree& tree)
{
    const auto rigTree = tree.getChildWithName ("RIG");
    if (! rigTree.isValid()) return;

    // rebuild blocks first (types), then parameter values from the tree
    std::array<int, kNumUnits> types {};
    std::array<juce::StringArray, kNumUnits> files;
    std::array<juce::String, kNumUnits> gears;
    for (const auto& ut : rigTree)
    {
        const int u = ut.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (u, kNumUnits)) continue;
        types[(size_t) u] = ut.getProperty ("type", 0);
        for (int f = 0; f < 3; ++f) files[(size_t) u].add (ut.getProperty ("file" + juce::String (f)).toString());
        gears[(size_t) u] = ut.getProperty ("gear").toString();
    }

    layout = {};
    const auto cells = juce::StringArray::fromTokens (rigTree.getProperty ("cells").toString(), ",", "");
    for (int c = 0; c < juce::jmin (kGridCells, cells.size()); ++c)
    {
        const int u = cells[c].getIntValue();
        layout.cell[(size_t) c] = (int8_t) (juce::isPositiveAndBelow (u, kNumUnits) && types[(size_t) u] != 0 ? u : -1);
    }
    for (int u = 0; u < kNumUnits; ++u)
        setUnitBlock (u, (BlockType) juce::jlimit (0, (int) BlockType::NumTypes - 1, types[(size_t) u]), false);

    auto copy = tree.createCopy();
    for (auto* name : { "RIG", "SNAPSHOTS", "FOOTSWITCHES", "MIDI" })
        copy.removeChild (copy.getChildWithName (name), nullptr);
    apvts.replaceState (copy);
    publishLayout();
    currentPresetName = tree.getProperty ("presetName", "INIT").toString();

    for (auto& s : snapshots) s.clear();
    const auto snaps = tree.getChildWithName ("SNAPSHOTS");
    for (const auto& s : snaps)
    {
        const int i = s.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (i, kNumSnapshots)) continue;
        for (const auto& v : juce::StringArray::fromTokens (s.getProperty ("values").toString(), ",", ""))
            snapshots[(size_t) i].push_back (v.getFloatValue());
    }
    currentSnapshot = juce::jlimit (0, kNumSnapshots - 1, (int) snaps.getProperty ("current", 0));

    for (const auto& f : tree.getChildWithName ("FOOTSWITCHES"))
    {
        const int i = f.getProperty ("index", -1);
        if (juce::isPositiveAndBelow (i, kNumFootswitches))
            fsAssign[(size_t) i] = { (FootswitchAssign::Kind) (int) f.getProperty ("kind", 0), (int) f.getProperty ("unit", -1) };
    }

    for (auto& c : ccMap) c.store (kNoTarget);
    for (const auto& m : tree.getChildWithName ("MIDI"))
    {
        const int cc = m.getProperty ("cc", -1);
        if (juce::isPositiveAndBelow (cc, 128)) ccMap[(size_t) cc].store ((int) m.getProperty ("target", (int) kNoTarget));
    }

    juce::WeakReference<ToneDeckProcessor> weak (this);
    juce::MessageManager::callAsync ([weak, files, types, gears]
    {
        auto* self = weak.get();
        if (self == nullptr) return;
        for (int u = 0; u < kNumUnits; ++u)
        {
            const auto t = (BlockType) types[(size_t) u];
            for (int f = 0; f < files[(size_t) u].size(); ++f)
            {
                const auto path = files[(size_t) u][f];
                const bool relevant = (t == BlockType::Amp && f == 0) || (t == BlockType::Cab && f > 0);
                if (relevant && path.isNotEmpty() && juce::File::isAbsolutePath (path))
                    self->loadUnitFile (u, f, juce::File (path), f == 0 ? gears[(size_t) u] : juce::String(), false);   // keep saved cab state
            }
        }
        self->sendChangeMessage();
    });
    sendChangeMessage();
}

void ToneDeckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void ToneDeckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts.state.getType()))
            restoreFromTree (tree);
    }
}

juce::AudioProcessorEditor* ToneDeckProcessor::createEditor() { return new ToneDeckEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ToneDeckProcessor(); }
