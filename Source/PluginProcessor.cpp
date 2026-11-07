#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String toStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

//==============================================================================
ToneDeckProcessor::ToneDeckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ToneDeck", td::createLayout())
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "ToneDeck";
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "ToneDeck";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    startTimerHz (20);
}

ToneDeckProcessor::~ToneDeckProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 5000);
}

//==============================================================================
bool ToneDeckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s)
    { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (in) && ok (out);
}

void ToneDeckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate  = sampleRate;
    currentBlock = juce::jmax (1, samplesPerBlock);

    mono.setSize (1, currentBlock);
    key.setSize  (1, currentBlock);
    dry.setSize  (1, currentBlock);

    pedalSlot.prepare (sampleRate, currentBlock);
    ampSlot.prepare   (sampleRate, currentBlock);

    const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) currentBlock, 1 };
    cabinet.prepare (monoSpec);
    for (auto* f : { &eqBass, &eqMid, &eqTreble, &eqPres, &cabLow, &cabHigh })
    {
        f->coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        f->prepare (monoSpec);
    }
    lastFilterParams.fill (-12345.0f);
    updateFilters();

    gate.prepare (sampleRate);
    delay.prepare (sampleRate, currentBlock);
    reverb.prepare ({ sampleRate, (juce::uint32) currentBlock, 2 });
    reverb.reset();

    for (auto* s : { &inGain, &outGain, &pedIn, &pedOut, &ampIn, &ampOut, &cabLevel })
        s->reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue  (juce::Decibels::decibelsToGain (p ("in_gain")));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("out_gain")));

    setLatencySamples (pedalSlot.latency() + ampSlot.latency());
}

void ToneDeckProcessor::updateFilters()
{
    const std::array<float, 8> now { p ("eq_bass"), p ("eq_mid"), p ("eq_treble"), p ("eq_pres"),
                                     p ("eq_midf"), p ("cab_lowcut"), p ("cab_highcut"), (float) currentRate };
    if (now == lastFilterParams)
        return;
    lastFilterParams = now;

    // ArrayCoefficients never allocate, so this is safe on the audio thread.
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto sr = currentRate;
    const auto g  = [] (float db) { return juce::Decibels::decibelsToGain (db); };
    *eqBass.coefficients   = AC::makeLowShelf  (sr, 120.0f,  0.7f, g (now[0]));
    *eqMid.coefficients    = AC::makePeakFilter (sr, now[4], 0.8f, g (now[1]));
    *eqTreble.coefficients = AC::makeHighShelf (sr, 2800.0f, 0.7f, g (now[2]));
    *eqPres.coefficients   = AC::makePeakFilter (sr, 5500.0f, 0.9f, g (now[3]));
    *cabLow.coefficients   = AC::makeHighPass (sr, now[5], 0.707f);
    *cabHigh.coefficients  = AC::makeLowPass  (sr, juce::jmin ((double) now[6], sr * 0.45), 0.707f);
}

//==============================================================================
void ToneDeckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
        if (meta.getMessage().isProgramChange())
            pendingProgram.store (meta.getMessage().getProgramChangeNumber());

    const int n = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0)
        return;
    if (n > mono.getNumSamples())   // defensive: some hosts exceed the announced block size
    {
        mono.setSize (1, n, false, false, true);
        key.setSize  (1, n, false, false, true);
        dry.setSize  (1, n, false, false, true);
    }

    pedalSlot.update();
    ampSlot.update();
    updateFilters();

    float* x = mono.getWritePointer (0);
    if (numIn > 0)
        juce::FloatVectorOperations::copy (x, buffer.getReadPointer (0), n);
    else
        juce::FloatVectorOperations::clear (x, n);

    // --- input
    inGain.setTargetValue (juce::Decibels::decibelsToGain (p ("in_gain")));
    inGain.applyGain (x, n);
    inputMeter.push (x, n);
    juce::FloatVectorOperations::copy (key.getWritePointer (0), x, n);

    // --- noise gate (detects on the clean input)
    if (on ("gate_on"))
    {
        gate.setParams (p ("gate_thresh"), p ("gate_rel"));
        gate.process (key.getReadPointer (0), x, n);
        gateGain.store (gate.getGainReduction());
    }
    else
    {
        gateGain.store (1.0f);
    }

    const bool normalize = on ("normalize");

    // --- pedal model
    pedIn.setTargetValue  (juce::Decibels::decibelsToGain (p ("ped_in")));
    pedOut.setTargetValue (juce::Decibels::decibelsToGain (p ("ped_out")) * (normalize ? pedalSlot.normGain() : 1.0f));
    if (on ("ped_on") && pedalSlot.hasModel())
    {
        pedIn.applyGain (x, n);
        pedalSlot.process (x, n, p ("ped_p1"), p ("ped_p2"));
        pedOut.applyGain (x, n);
    }
    else
    {
        pedIn.skip (n); pedOut.skip (n);
    }

    // --- amp model
    ampIn.setTargetValue  (juce::Decibels::decibelsToGain (p ("amp_in")));
    ampOut.setTargetValue (juce::Decibels::decibelsToGain (p ("amp_out")) * (normalize ? ampSlot.normGain() : 1.0f));
    if (on ("amp_on") && ampSlot.hasModel())
    {
        ampIn.applyGain (x, n);
        ampSlot.process (x, n, p ("amp_p1"), p ("amp_p2"));
        ampOut.applyGain (x, n);
    }
    else
    {
        ampIn.skip (n); ampOut.skip (n);
    }

    // --- EQ
    if (on ("eq_on"))
    {
        for (int i = 0; i < n; ++i)
            x[i] = eqPres.processSample (eqTreble.processSample (eqMid.processSample (eqBass.processSample (x[i]))));
    }

    // --- cabinet IR
    cabLevel.setTargetValue (juce::Decibels::decibelsToGain (p ("cab_level")));
    if (on ("cab_on") && cabLoaded.load())
    {
        const float mix = p ("cab_mix");
        juce::FloatVectorOperations::copy (dry.getWritePointer (0), x, n);

        juce::dsp::AudioBlock<float> block (mono.getArrayOfWritePointers(), 1, (size_t) n);
        cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));

        for (int i = 0; i < n; ++i)
            x[i] = cabHigh.processSample (cabLow.processSample (x[i]));

        if (mix < 0.999f)
        {
            juce::FloatVectorOperations::multiply (x, mix, n);
            juce::FloatVectorOperations::addWithMultiply (x, dry.getReadPointer (0), 1.0f - mix, n);
        }
        cabLevel.applyGain (x, n);
    }
    else
    {
        cabLevel.skip (n);
    }

    // --- stereo section
    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::FloatVectorOperations::copy (L, x, n);
    if (R != nullptr)
        juce::FloatVectorOperations::copy (R, x, n);

    const bool delayOn = on ("dly_on");
    if (delayOn && ! delayWasOn)
        delay.reset();
    delayWasOn = delayOn;
    if (delayOn)
    {
        delay.setParams (p ("dly_time"), p ("dly_fb"), p ("dly_mix"));
        delay.process (x, L, R != nullptr ? R : L, n);
    }

    const bool reverbOn = on ("rev_on");
    if (reverbOn && ! reverbWasOn)
        reverb.reset();
    reverbWasOn = reverbOn;
    if (reverbOn)
    {
        const float mix = p ("rev_mix");
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + p ("rev_size") * 0.68f;
        rp.damping  = p ("rev_damp");
        rp.width    = p ("rev_width");
        rp.wetLevel = mix * 0.6f;
        rp.dryLevel = 1.0f - mix * 0.5f;
        reverb.setParameters (rp);

        if (R != nullptr)
        {
            float* ch[] = { L, R };
            juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else
        {
            float* ch[] = { L };
            juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
            reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
    }

    // --- master
    outGain.setTargetValue (juce::Decibels::decibelsToGain (p ("out_gain")));
    if (R != nullptr)
    {
        // keep both channels on the same smoothing ramp
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            L[i] *= g;
            R[i] *= g;
        }
    }
    else
    {
        outGain.applyGain (L, n);
    }

    for (int c = 2; c < numOut; ++c)
        buffer.clear (c, 0, n);

    outputMeter.push (L, n);
}

//==============================================================================
td::SlotKind ToneDeckProcessor::kindForFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".nam" || ext == ".json" || ext == ".aidax")
        return td::SlotKind::Model;
    if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac")
        return td::SlotKind::IR;
    return td::SlotKind::None;
}

juce::String ToneDeckProcessor::wildcardForSlot (int slot)
{
    return slot == td::Cab ? "*.wav;*.aif;*.aiff;*.flac" : "*.nam;*.json;*.aidax";
}

const SlotStatus& ToneDeckProcessor::getSlotStatus (int slot) const
{
    return status[(size_t) juce::jlimit (0, (int) td::NumBlocks - 1, slot)];
}

void ToneDeckProcessor::clearSlot (int slot)
{
    if (slot == td::Cab)
    {
        cabLoaded.store (false);
        cabinet.reset();
    }
    else if (slot == td::Pedal || slot == td::Amp)
    {
        ++loadGeneration[(size_t) slot];
        modelSlot (slot).setInstance (std::make_unique<ModelInstance>());
    }
    status[(size_t) slot] = {};
    sendChangeMessage();
}

void ToneDeckProcessor::loadFile (int slot, const juce::File& file)
{
    if (! file.existsAsFile())
    {
        auto& st = status[(size_t) slot];
        st = {};
        st.file  = file;
        st.name  = file.getFileNameWithoutExtension();
        st.error = "FILE NOT FOUND";
        sendChangeMessage();
        return;
    }

    if (slot == td::Cab)
    {
        auto& st = status[(size_t) slot];
        st = {};
        st.file = file;
        st.name = file.getFileNameWithoutExtension();

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (file) })
        {
            st.sampleRate   = r->sampleRate;
            st.format       = "IR";
            st.architecture = juce::String ((double) r->lengthInSamples / juce::jmax (1.0, r->sampleRate) * 1000.0, 0) + " ms";
            // trimmed + normalised; Convolution resamples to the host rate itself
            cabinet.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes, 0,
                                         juce::dsp::Convolution::Normalise::yes);
            cabLoaded.store (true);
            st.loaded = true;
        }
        else
        {
            st.error = "UNREADABLE IR";
        }
        sendChangeMessage();
        return;
    }

    // Model slots: parse + pre-warm on a background thread.
    auto& st = status[(size_t) slot];
    st.loading = true;
    st.error.clear();
    st.file = file;
    st.name = file.getFileNameWithoutExtension();
    sendChangeMessage();

    const int gen        = ++loadGeneration[(size_t) slot];
    const double rate    = currentRate;
    const int block      = currentBlock;
    const double size    = p (slot == td::Pedal ? "ped_size" : "amp_size");
    juce::WeakReference<ToneDeckProcessor> weak (this);

    loaderPool.addJob ([this, weak, slot, file, gen, rate, block, size]
    {
        const auto text = file.loadFileAsString().toStdString();
        const auto stem = file.getFileNameWithoutExtension().toStdString();
        std::string err;

        std::unique_ptr<ModelEngine> engine;
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".nam")
        {
            engine = createNamEngine (text, stem, err);
        }
        else
        {
            engine = createAidaEngine (text, stem, err);
            if (engine == nullptr)   // some .json files are NAM exports
            {
                std::string err2;
                engine = createNamEngine (text, stem, err2);
                if (engine != nullptr) err.clear();
            }
        }

        SlotStatus result;
        result.file = file;
        result.name = file.getFileNameWithoutExtension();

        if (engine != nullptr)
        {
            auto inst = std::make_unique<ModelInstance>();
            const auto info = engine->info;
            const bool slim = engine->isSlimmable();
            if (slim)
                engine->setSlimmableSize (size);

            inst->engine = std::move (engine);
            inst->file   = file;
            if (info.hasLoudness)
                inst->normGain = juce::Decibels::decibelsToGain ((float) (-18.0 - info.loudnessDb));
            inst->prepare (rate, block);

            result.loaded             = true;
            result.format             = toStd (info.format);
            result.architecture       = toStd (info.architecture);
            result.sampleRate         = info.sampleRate;
            result.slimmable          = slim;
            result.hasLoudness        = info.hasLoudness;
            result.loudness           = info.loudnessDb;
            result.conditioningParams = info.numConditioningParams;

            if (gen == loadGeneration[(size_t) slot])
                modelSlot (slot).setInstance (std::move (inst));
        }
        else
        {
            result.error = "LOAD ERROR: " + toStd (err);
        }

        juce::MessageManager::callAsync ([weak, slot, gen, result]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;
            if (gen != self->loadGeneration[(size_t) slot])
                return;
            self->status[(size_t) slot] = result;
            self->sendChangeMessage();
        });
    });
}

//==============================================================================
void ToneDeckProcessor::timerCallback()
{
    pedalSlot.collectGarbage();
    ampSlot.collectGarbage();

    // A2 "slimmable" quality knob: applied off the audio thread.
    const float ps = p ("ped_size"), as = p ("amp_size");
    if (! juce::exactlyEqual (ps, lastPedSize)) { lastPedSize = ps; pedalSlot.setSlimmableSize (ps); }
    if (! juce::exactlyEqual (as, lastAmpSize)) { lastAmpSize = as; ampSlot.setSlimmableSize (as); }

    const int lat = pedalSlot.latency() + ampSlot.latency();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        setLatencySamples (lat);
    }

    const int prog = pendingProgram.exchange (-1);
    if (prog >= 0)
    {
        const auto files = getPresetFiles();
        if (juce::isPositiveAndBelow (prog, files.size()))
            loadPreset (files[prog]);
    }
}

//==============================================================================
juce::File ToneDeckProcessor::getLibraryFolder() const
{
    auto* props = const_cast<juce::ApplicationProperties&> (appProps).getUserSettings();
    const auto path = props != nullptr ? props->getValue ("libraryFolder") : juce::String();
    juce::File f (path);
    if (path.isNotEmpty() && f.isDirectory())
        return f;
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ToneDeck").getChildFile ("Library");
}

void ToneDeckProcessor::setLibraryFolder (const juce::File& f)
{
    if (auto* props = appProps.getUserSettings())
    {
        props->setValue ("libraryFolder", f.getFullPathName());
        props->saveIfNeeded();
    }
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
    if (clean.isEmpty())
        return false;
    auto tree = createStateTree();
    tree.setProperty ("presetName", clean, nullptr);
    if (auto xml = tree.createXml())
    {
        if (xml->writeTo (getPresetFolder().getChildFile (clean + ".tdpreset")))
        {
            currentPresetName = clean;
            sendChangeMessage();
            return true;
        }
    }
    return false;
}

bool ToneDeckProcessor::loadPreset (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts.state.getType()))
        {
            restoreFromTree (tree);
            currentPresetName = f.getFileNameWithoutExtension();
            sendChangeMessage();
            return true;
        }
    }
    return false;
}

void ToneDeckProcessor::stepPreset (int delta)
{
    const auto files = getPresetFiles();
    if (files.isEmpty())
        return;
    int idx = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == currentPresetName)
            idx = i;
    idx = idx < 0 ? 0 : (idx + delta + files.size()) % files.size();
    loadPreset (files[idx]);
}

//==============================================================================
juce::ValueTree ToneDeckProcessor::createStateTree()
{
    auto tree = apvts.copyState();
    tree.setProperty ("pedalFile", status[td::Pedal].file.getFullPathName(), nullptr);
    tree.setProperty ("ampFile",   status[td::Amp].file.getFullPathName(),   nullptr);
    tree.setProperty ("cabFile",   status[td::Cab].file.getFullPathName(),   nullptr);
    tree.setProperty ("presetName", currentPresetName, nullptr);
    return tree;
}

void ToneDeckProcessor::restoreFromTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    currentPresetName = tree.getProperty ("presetName", "INIT").toString();

    const auto restore = [this, tree] (int slot, const char* prop)
    {
        const auto path = tree.getProperty (prop).toString();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
            loadFile (slot, juce::File (path));
        else
            clearSlot (slot);
    };

    juce::WeakReference<ToneDeckProcessor> weak (this);
    juce::MessageManager::callAsync ([weak, restore]
    {
        if (weak.get() == nullptr)
            return;
        restore (td::Pedal, "pedalFile");
        restore (td::Amp,   "ampFile");
        restore (td::Cab,   "cabFile");
    });
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

juce::AudioProcessorEditor* ToneDeckProcessor::createEditor()
{
    return new ToneDeckEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToneDeckProcessor();
}
