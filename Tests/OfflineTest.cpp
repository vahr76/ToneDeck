// Offline test runner: pushes a synthetic guitar signal through the full ToneDeck
// chain for several models / sample rates / block sizes and renders UI snapshots.
//   ToneDeckTests <outputDir> <model files...>

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static bool waitLoaded (ToneDeckProcessor& p, int slot)
{
    for (int i = 0; i < 400; ++i)
    {
        pump (25);
        const auto& st = p.getSlotStatus (slot);
        if (! st.loading && (st.loaded || st.error.isNotEmpty()))
            return st.loaded;
    }
    return false;
}

// Karplus-Strong "guitar" with a few notes, roughly -12 dBFS peaks
static AudioBuffer<float> makeGuitar (double sr, double seconds)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    b.clear();
    Random rng (1);
    const double notes[] = { 82.41, 110.0, 146.83, 196.0, 82.41 };
    const int noteLen = b.getNumSamples() / 5;
    for (int n = 0; n < 5; ++n)
    {
        const int period = (int) (sr / notes[n]);
        std::vector<float> line ((size_t) period);
        for (auto& v : line) v = rng.nextFloat() * 2.0f - 1.0f;
        int idx = 0;
        for (int i = 0; i < noteLen; ++i)
        {
            const int next = (idx + 1) % period;
            const float out = line[(size_t) idx];
            line[(size_t) idx] = 0.996f * 0.5f * (line[(size_t) idx] + line[(size_t) next]);
            idx = next;
            b.setSample (0, n * noteLen + i, out * 0.25f);
        }
    }
    b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    return b;
}

static File makeIR (const File& dir)
{
    const auto f = dir.getChildFile ("test_cab_ir.wav");
    const double sr = 48000.0;
    AudioBuffer<float> ir (1, 2048);
    Random rng (7);
    float lp = 0.0f;
    for (int i = 0; i < ir.getNumSamples(); ++i)
    {
        lp = 0.65f * lp + 0.35f * (rng.nextFloat() * 2.0f - 1.0f);
        ir.setSample (0, i, lp * std::exp (-(float) i / 300.0f) + (i == 0 ? 1.0f : 0.0f));
    }
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), sr, 1, 24, {}, 0));
    w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    return f;
}

static void setParam (ToneDeckProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

struct RenderResult { bool ok = true; float rms = 0.0f, peak = 0.0f; double rtFactor = 0.0; };

static RenderResult render (ToneDeckProcessor& p, double sr, int block, double seconds)
{
    auto in = makeGuitar (sr, seconds);
    AudioBuffer<float> io (2, block);
    MidiBuffer midi;
    RenderResult r;
    double sq = 0.0;
    int count = 0;
    const auto t0 = Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, n);
        for (int c = 0; c < 2; ++c) view.copyFrom (c, 0, in, c, pos, n);
        p.processBlock (view, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = view.getSample (c, i);
                if (! std::isfinite (v)) r.ok = false;
                r.peak = jmax (r.peak, std::abs (v));
                sq += (double) v * v; ++count;
            }
        if ((pos / block) % 16 == 0) pump (1);   // let async IR swaps / GC happen
    }
    const double elapsed = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    r.rms = (float) std::sqrt (sq / jmax (1, count));
    r.rtFactor = seconds / jmax (1e-9, elapsed);
    return r;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::cout << "usage: ToneDeckTests <outDir> <models...>\n"; return 2; }

    const File outDir (argv[1]);
    outDir.createDirectory();
    const auto ir = makeIR (outDir);
    int failures = 0;

    for (int a = 2; a < argc; ++a)
    {
        const File model (String::fromUTF8 (argv[a]));
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 512, 333 })
            {
                ToneDeckProcessor p;
                p.setPlayConfigDetails (2, 2, sr, block);
                p.prepareToPlay (sr, block);
                p.loadFile (td::Amp, model);
                p.loadFile (td::Cab, ir);
                const bool loaded = waitLoaded (p, td::Amp);
                setParam (p, "dly_on", 1.0f);
                setParam (p, "rev_on", 1.0f);
                const auto& st = p.getSlotStatus (td::Amp);

                render (p, sr, block, 0.3);               // warm-up: IR + model swap in
                const auto r = render (p, sr, block, 3.0);
                const bool pass = loaded && r.ok && r.rms > 1e-4f && r.peak < 50.0f;
                failures += pass ? 0 : 1;
                std::cout << (pass ? "PASS " : "FAIL ") << model.getFileName().paddedRight (' ', 30)
                          << " " << st.format.paddedRight (' ', 7) << " " << st.architecture.paddedRight (' ', 18)
                          << " sr=" << (int) sr << " blk=" << block
                          << " lat=" << p.getLatencySamples()
                          << " rms=" << String (Decibels::gainToDecibels (r.rms), 1) << "dB"
                          << " peak=" << String (r.peak, 2)
                          << " x" << String (r.rtFactor, 1) << " realtime"
                          << (st.slimmable ? " [slimmable]" : "")
                          << (st.conditioningParams > 0 ? " [cond " + String (st.conditioningParams) + "]" : String())
                          << (st.error.isNotEmpty() ? " ERR=" + st.error : String()) << "\n";
            }
        }
    }

    // Pedal + amp chained, A2 quality knob sweep, state round trip
    {
        ToneDeckProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        p.loadFile (td::Pedal, File (String::fromUTF8 (argv[2])));
        p.loadFile (td::Amp, File (String::fromUTF8 (argv[argc - 1])));
        p.loadFile (td::Cab, ir);
        waitLoaded (p, td::Pedal); waitLoaded (p, td::Amp);
        setParam (p, "ped_on", 1.0f);
        for (float q : { 0.1f, 0.5f, 1.0f })
        {
            setParam (p, "amp_size", q); setParam (p, "ped_size", q);
            pump (120);
            const auto r = render (p, 48000.0, 256, 1.0);
            const bool pass = r.ok && r.rms > 1e-4f;
            failures += pass ? 0 : 1;
            std::cout << (pass ? "PASS " : "FAIL ") << "chain pedal+amp quality=" << q << " rms=" << String (Decibels::gainToDecibels (r.rms), 1) << "dB x" << String (r.rtFactor, 1) << " realtime\n";
        }

        MemoryBlock state;
        p.getStateInformation (state);
        ToneDeckProcessor p2;
        p2.setPlayConfigDetails (2, 2, 48000.0, 256);
        p2.prepareToPlay (48000.0, 256);
        p2.setStateInformation (state.getData(), (int) state.getSize());
        pump (50);
        const bool restored = waitLoaded (p2, td::Amp) && p2.getSlotStatus (td::Cab).loaded
                              && p2.apvts.getRawParameterValue ("ped_on")->load() > 0.5f;
        failures += restored ? 0 : 1;
        std::cout << (restored ? "PASS " : "FAIL ") << "state save/restore (files + params)\n";

        // UI snapshots
        {
            setParam (p, "dly_on", 1.0f);
            render (p, 48000.0, 256, 0.2);
            std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (ToneDeckEditor::baseW, ToneDeckEditor::baseH);
            pump (100);
            const auto save = [&] (const String& name)
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                PNGImageFormat png;
                FileOutputStream os (outDir.getChildFile (name));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (img, os);
                std::cout << "snapshot " << outDir.getChildFile (name).getFullPathName() << "\n";
            };
            save ("ui_amp.png");

            auto* lcd = dynamic_cast<td::ui::LcdDisplay*> (ed->getChildComponent (0)->getChildComponent (0));
            if (lcd != nullptr)
            {
                lcd->setSelectedBlock (td::Cab);
                if (lcd->onSelect) lcd->onSelect (td::Cab);
                pump (50);
                save ("ui_cab.png");
                lcd->setSelectedBlock (td::Amp);
                if (lcd->onSelect) lcd->onSelect (td::Amp);
                lcd->showLibrary (true);
                pump (50);
                save ("ui_library.png");
            }
        }
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : String (failures) + " FAILURE(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
