// Offline test runner for the ToneDeck engine.
//   ToneDeckTests <outputDir> <model files...>
// Renders a synthetic guitar through the rig in many configurations and checks
// that every feature produces sane, audible, finite output. Also saves UI snapshots.

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace td;

static int failures = 0;
static void check (bool ok, const String& what)
{
    failures += ok ? 0 : 1;
    std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
}

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static bool waitModel (ToneDeckProcessor& p, int unit)
{
    for (int i = 0; i < 400; ++i)
    {
        pump (20);
        const auto& st = p.getUnit (unit).files[0];
        if (! st.loading && (st.loaded || st.error.isNotEmpty())) return st.loaded;
    }
    return false;
}

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

static File makeIR (const File& dir, const String& name, float decay)
{
    const auto f = dir.getChildFile (name);
    AudioBuffer<float> ir (1, 2048);
    Random rng ((int64) (decay * 1000));
    float lp = 0.0f;
    for (int i = 0; i < ir.getNumSamples(); ++i)
    {
        lp = 0.65f * lp + 0.35f * (rng.nextFloat() * 2.0f - 1.0f);
        ir.setSample (0, i, lp * std::exp (-(float) i / decay) + (i == 0 ? 1.0f : 0.0f));
    }
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), 48000.0, 1, 24, {}, 0));
    w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    return f;
}

static void setNorm (RangedAudioParameter* prm, float v) { prm->setValueNotifyingHost (v); }
static void setOn (ToneDeckProcessor& p, int unit, bool on) { setNorm (p.unitOnParam (unit), on ? 1.0f : 0.0f); }

struct Render { bool finite = true; float rms = 0, peak = 0; double rt = 0; AudioBuffer<float> out; };

static Render render (ToneDeckProcessor& p, double sr, int block, double seconds, bool keep = false)
{
    auto in = makeGuitar (sr, seconds);
    Render r;
    if (keep) r.out.makeCopyOf (in);
    AudioBuffer<float> io (2, block);
    MidiBuffer midi;
    double sq = 0; int count = 0;
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
                if (! std::isfinite (v)) r.finite = false;
                r.peak = jmax (r.peak, std::abs (v));
                sq += (double) v * v; ++count;
                if (keep) r.out.setSample (c, pos + i, v);
            }
        if ((pos / block) % 16 == 0) pump (1);
    }
    r.rt = seconds / jmax (1e-9, (Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    r.rms = (float) std::sqrt (sq / jmax (1, count));
    return r;
}

static double relDiff (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    double d = 0, e = 0;
    const int n = jmin (a.getNumSamples(), b.getNumSamples());
    for (int i = 0; i < n; ++i) { const double x = a.getSample (0, i) - b.getSample (0, i); d += x * x; e += (double) a.getSample (0, i) * a.getSample (0, i); }
    return std::sqrt (d / jmax (1e-12, e));
}

static std::unique_ptr<ToneDeckProcessor> makeProc (double sr, int block)
{
    auto p = std::make_unique<ToneDeckProcessor>();
    p->setPlayConfigDetails (2, 2, sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::cout << "usage: ToneDeckTests <outDir> <models...>\n"; return 2; }
    const File outDir (argv[1]);
    outDir.createDirectory();
    const auto irA = makeIR (outDir, "test_ir_a.wav", 300.0f);
    const auto irB = makeIR (outDir, "test_ir_b.wav", 60.0f);
    Array<File> models;
    for (int a = 2; a < argc; ++a) models.add (File (String::fromUTF8 (argv[a])));

    // --- TONE3000 helpers
    check (Tone3000Client::makeCodeChallenge ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWChk2wM") == "g3PvRCpOycJ1Z_9rYibl5munWdXW-v-frWfa0OPnnVQ",
           "TONE3000 PKCE S256 challenge (vs Python hashlib)");
    {
        const auto q = Tone3000Client::parseQuery ("code=abc%2F123&state=x-y_z&tone_id=4521");
        check (q["code"] == "abc/123" && q["state"] == "x-y_z" && q["tone_id"] == "4521", "TONE3000 callback query parsing");
    }

    // --- default rig layout
    {
        auto p = makeProc (48000, 512);
        const int amp = p->findUnitOfType (BlockType::Amp), cab = p->findUnitOfType (BlockType::Cab);
        check (amp >= 0 && cab >= 0 && p->cellOfUnit (amp) == 2 && p->cellOfUnit (cab) == 4, "default rig: NR OD AMP EQ CAB DLY REV");
    }

    // --- every model file at several rates / block sizes, and that it changes the sound
    for (const auto& model : models)
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 512, 333 })
            {
                auto p = makeProc (sr, block);
                const int amp = p->findUnitOfType (BlockType::Amp);
                p->loadUnitFile (amp, 0, model);
                const bool loaded = waitModel (*p, amp);
                render (*p, sr, block, 0.3);
                const auto r = render (*p, sr, block, 2.0);
                const auto& st = p->getUnit (amp).files[0];
                check (loaded && r.finite && r.rms > 1e-4f && r.peak < 50.0f,
                       model.getFileName().paddedRight (' ', 30) + " " + st.format.paddedRight (' ', 8) + st.architecture.paddedRight (' ', 18)
                       + " sr=" + String ((int) sr) + " blk=" + String (block) + " lat=" + String (p->getLatencySamples())
                       + " x" + String (r.rt, 1) + " RT" + (st.error.isNotEmpty() ? " ERR=" + st.error : String()));
            }

        auto run = [&] (bool ampOn)
        {
            auto p = makeProc (48000, 512);
            const int amp = p->findUnitOfType (BlockType::Amp);
            p->loadUnitFile (amp, 0, model);
            waitModel (*p, amp);
            setOn (*p, amp, ampOn);
            setOn (*p, p->findUnitOfType (BlockType::Gate), false);
            render (*p, 48000, 512, 0.2);
            return render (*p, 48000, 512, 1.0, true).out;
        };
        check (relDiff (run (false), run (true)) > 0.05, "model changes the sound vs bypass: " + model.getFileName());
    }

    // --- cab: IR A, IR B, blend
    {
        auto run = [&] (int mode)   // 0 = cab off, 1 = IR A, 2 = IR A+B 50%
        {
            auto p = makeProc (48000, 512);
            const int cab = p->findUnitOfType (BlockType::Cab);
            setOn (*p, p->findUnitOfType (BlockType::Gate), false);
            setOn (*p, cab, mode > 0);
            p->loadUnitFile (cab, 1, irA);
            if (mode == 2) { p->loadUnitFile (cab, 2, irB); setNorm (p->unitParam (cab, 2), 0.5f); }
            render (*p, 48000, 512, 0.3);
            return render (*p, 48000, 512, 1.0, true).out;
        };
        const auto off = run (0), a = run (1), ab = run (2);
        check (relDiff (off, a) > 0.05, "cab IR A changes the sound");
        check (relDiff (a, ab) > 0.02, "cab IR B blend changes the sound");
    }

    // --- every block type: placed in path A and B, fuzzed parameters, finite output
    {
        auto p = makeProc (48000, 256);
        for (int t = 1; t < (int) BlockType::NumTypes; ++t)
        {
            const auto type = (BlockType) t;
            const int cell = Layout::index (1, 3);
            p->removeBlock (cell);
            const int u = p->addBlock (cell, type);
            Random rng (t);
            bool ok = u >= 0;
            for (int pass = 0; pass < 4 && ok; ++pass)
            {
                for (int i = 0; i < kParamsPerUnit; ++i) setNorm (p->unitParam (u, i), pass == 0 ? 0.0f : (pass == 1 ? 1.0f : rng.nextFloat()));
                const auto r = render (*p, 48000, 256, 0.25);
                ok = r.finite && r.peak < 100.0f;
            }
            check (ok, String ("block ") + String (blockInfo (type).shortName).paddedRight (' ', 4) + " in parallel path B, params fuzzed");
        }
    }

    // --- octaver: down / up land on the right pitch, both modes
    {
        const auto measure = [&] (int mode, float d1, float u1)
        {
            auto p = makeProc (48000, 512);
            for (int c = 0; c < kGridCells; ++c) if (p->unitAtCell (c) >= 0) p->removeBlock (c);
            const int u = p->addBlock (0, BlockType::Octaver);
            setNorm (p->unitParam (u, 0), 0.0f);
            setNorm (p->unitParam (u, 1), d1);
            setNorm (p->unitParam (u, 2), u1);
            setNorm (p->unitParam (u, 3), 0.0f);    // no dry
            setNorm (p->unitParam (u, 4), 1.0f);    // tone open
            setNorm (p->unitParam (u, 5), (float) mode);
            AudioBuffer<float> b (2, 512);
            MidiBuffer m;
            double ph = 0;
            std::vector<float> out;
            for (int k = 0; k < 120; ++k)
            {
                for (int i = 0; i < 512; ++i) { const float s = 0.3f * (float) std::sin (ph); b.setSample (0, i, s); b.setSample (1, i, s); ph += MathConstants<double>::twoPi * 220.0 / 48000.0; }
                p->processBlock (b, m);
                if (k >= 40) for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
            }
            // normalised autocorrelation over the steady part; pick the strongest lag in 60..900 Hz
            const int N = 16384, minLag = 48000 / 900, maxLag = 48000 / 60;
            double mean = 0; for (int i = 0; i < N + maxLag; ++i) mean += out[(size_t) i]; mean /= (N + maxLag);
            std::vector<double> r ((size_t) maxLag + 2, 0.0);
            double top = -1e9;
            for (int lag = minLag; lag <= maxLag + 1; ++lag)
            {
                double num = 0, e1 = 0, e2 = 0;
                for (int i = 0; i < N; ++i)
                {
                    const double a = out[(size_t) i] - mean, c = out[(size_t) (i + lag)] - mean;
                    num += a * c; e1 += a * a; e2 += c * c;
                }
                r[(size_t) lag] = num / std::sqrt (e1 * e2 + 1e-12);
                top = jmax (top, r[(size_t) lag]);
            }
            // first peak that is nearly as strong as the best one = fundamental period
            for (int lag = minLag + 1; lag <= maxLag; ++lag)
                if (r[(size_t) lag] >= 0.9 * top && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
                {
                    const double a = r[(size_t) lag - 1], b2 = r[(size_t) lag], c = r[(size_t) lag + 1];
                    const double den = a - 2 * b2 + c;
                    const double shift = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
                    return (float) (48000.0 / (lag + shift));
                }
            return 0.0f;
        };
        const float polyDown = measure (0, 1.0f, 0.0f), polyUp = measure (0, 0.0f, 1.0f);
        const float anaDown = measure (1, 1.0f, 0.0f), anaUp = measure (1, 0.0f, 1.0f);
        check (std::abs (polyDown - 110.0f) < 6.0f, "octaver polyphonic -1 oct: 220 Hz -> " + String (polyDown, 1) + " Hz");
        check (std::abs (polyUp - 440.0f) < 20.0f,  "octaver polyphonic +1 oct: 220 Hz -> " + String (polyUp, 1) + " Hz");
        check (std::abs (anaDown - 110.0f) < 3.0f,  "octaver analog -1 oct: 220 Hz -> " + String (anaDown, 1) + " Hz");
        check (std::abs (anaUp - 440.0f) < 10.0f,   "octaver analog +1 oct: 220 Hz -> " + String (anaUp, 1) + " Hz");
    }

    // --- parallel paths: B path changes output; merge levels work
    {
        auto run = [&] (bool withB)
        {
            auto p = makeProc (48000, 512);
            setOn (*p, p->findUnitOfType (BlockType::Gate), false);
            if (withB)
            {
                const int u = p->addBlock (Layout::index (1, 2), BlockType::Fuzz);
                setOn (*p, u, true);
            }
            render (*p, 48000, 512, 0.2);
            return render (*p, 48000, 512, 1.0, true).out;
        };
        check (relDiff (run (false), run (true)) > 0.05, "parallel path B (fuzz) is mixed into the output");
    }

    // --- moving blocks changes order
    {
        auto p = makeProc (48000, 512);
        const int ampCell = 2, eqCell = 3;
        const int ampU = p->unitAtCell (ampCell), eqU = p->unitAtCell (eqCell);
        p->moveBlock (ampCell, eqCell);
        check (p->unitAtCell (ampCell) == eqU && p->unitAtCell (eqCell) == ampU, "drag/move swaps blocks in the grid");
        p->moveBlock (eqCell, Layout::index (1, 6));
        check (p->unitAtCell (Layout::index (1, 6)) == ampU, "move block into path B");
    }

    // --- snapshots
    {
        auto p = makeProc (48000, 512);
        const int dly = p->findUnitOfType (BlockType::Delay);
        setOn (*p, dly, false);
        p->recallSnapshot (1);             // snap 2 starts as copy of snap 1
        setOn (*p, dly, true);
        setNorm (p->unitParam (dly, 2), 0.9f);
        p->recallSnapshot (0);
        const bool backTo1 = ! p->isUnitOn (dly);
        p->recallSnapshot (1);
        const bool backTo2 = p->isUnitOn (dly) && std::abs (p->unitParam (dly, 2)->getValue() - 0.9f) < 0.01f;
        check (backTo1 && backTo2, "snapshots store and recall block on/off + params");
    }

    // --- looper
    {
        auto p = makeProc (48000, 512);
        p->looperRecPlay();
        render (*p, 48000, 512, 1.0);
        p->looperRecPlay();                // -> play
        // silence in, loop should still sound
        AudioBuffer<float> silent (2, 512);
        MidiBuffer midi;
        float peak = 0.0f;
        for (int i = 0; i < 60; ++i) { silent.clear(); p->processBlock (silent, midi); peak = jmax (peak, silent.getMagnitude (0, 512)); }
        check (p->getLooperState() == LooperState::Playing && peak > 0.01f, "looper records and plays back");
        p->looperClear();
        silent.clear(); p->processBlock (silent, midi);
        check (p->getLooperState() == LooperState::Empty, "looper clear");
    }

    // --- tuner
    {
        auto p = makeProc (48000, 512);
        p->setTunerOn (true);
        AudioBuffer<float> b (2, 512);
        MidiBuffer midi;
        double ph = 0;
        float out = 0;
        for (int k = 0; k < 20; ++k)
        {
            for (int i = 0; i < 512; ++i) { const float s = 0.3f * (float) std::sin (ph); b.setSample (0, i, s); b.setSample (1, i, s); ph += MathConstants<double>::twoPi * 110.0 / 48000.0; }
            p->processBlock (b, midi);
            out = jmax (out, b.getMagnitude (0, 512));
        }
        const float hz = p->detectPitch();
        check (std::abs (hz - 110.0f) < 0.5f, "tuner detects A2 = 110 Hz (got " + String (hz, 2) + ")");
        check (out < 1e-6f, "tuner mutes the output");
    }

    // --- tap tempo sets delay time
    {
        auto p = makeProc (48000, 512);
        const int dly = p->findUnitOfType (BlockType::Delay);
        for (int i = 0; i < 4; ++i) { p->tapTempo(); Thread::sleep (500); }
        const float ms = blockInfo (BlockType::Delay).params[0].toPlain (p->unitParam (dly, 0)->getValue());
        check (std::abs (ms - 500.0f) < 40.0f, "tap tempo -> delay time (" + String (ms, 0) + " ms, " + String (p->getBpm(), 1) + " BPM)");
    }

    // --- MIDI learn: CC -> parameter and CC -> footswitch
    {
        auto p = makeProc (48000, 512);
        auto* prm = p->apvts.getParameter ("out_gain");
        p->startMidiLearn (p->paramIndex (prm));
        AudioBuffer<float> b (2, 512);
        MidiBuffer m1; m1.addEvent (MidiMessage::controllerEvent (1, 20, 0), 0);
        b.clear(); p->processBlock (b, m1);
        MidiBuffer m2; m2.addEvent (MidiMessage::controllerEvent (1, 20, 127), 0);
        b.clear(); p->processBlock (b, m2);
        check (p->ccForTarget (p->paramIndex (prm)) == 20 && prm->getValue() > 0.99f, "MIDI learn: CC20 -> Master");

        const int dly = p->findUnitOfType (BlockType::Delay);
        p->setFsAssign (1, { FootswitchAssign::ToggleUnit, dly });
        const bool before = p->isUnitOn (dly);
        p->startMidiLearn (-101);
        MidiBuffer m3; m3.addEvent (MidiMessage::controllerEvent (1, 64, 127), 0);
        b.clear(); p->processBlock (b, m3);
        pump (120);
        check (p->isUnitOn (dly) != before, "MIDI learn: CC64 -> footswitch 2 toggles delay");
    }

    // --- gear-aware capture placement (pedal before amp), naming, warnings
    {
        // make a "pedal" and an "amp" capture from the first model by rewriting gear_type
        auto json = models.getFirst().loadFileAsString();
        const auto withGear = [&] (const String& g)
        {
            if (json.contains ("\"gear_type\"")) return json.replace ("\"gear_type\": \"amp\"", "\"gear_type\": \"" + g + "\"");
            return json.replaceFirstOccurrenceOf ("{", "{\"metadata\": {\"gear_type\": \"" + g + "\"}, ");
        };
        const auto pedalFile = outDir.getChildFile ("TS808 Chorus capture.nam");
        const auto ampFile   = outDir.getChildFile ("Plexi capture.nam");
        pedalFile.replaceWithText (withGear ("pedal"));
        ampFile.replaceWithText (withGear ("amp"));
        check (ToneDeckProcessor::peekGear (pedalFile) == "pedal" && ToneDeckProcessor::peekGear (ampFile) == "amp", "read gear_type from .nam metadata");

        auto p = makeProc (48000, 256);
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
        auto* tde = dynamic_cast<ToneDeckEditor*> (ed.get());
        const int ampUnit = p->findUnitOfType (BlockType::Amp);
        const int ampCellBefore = p->cellOfUnit (ampUnit);

        tde->routeFile (-1, ampFile, {}, true);
        waitModel (*p, ampUnit);
        check (p->getUnit (ampUnit).files[0].loaded && p->unitShortName (ampUnit) == "AMP", "amp capture goes into the AMP slot and is labelled AMP");

        tde->routeFile (-1, pedalFile, {}, true);
        const int pedCell = Layout::index (0, ampCellBefore);
        const int pedUnit = p->unitAtCell (pedCell);
        waitModel (*p, pedUnit);
        check (pedUnit >= 0 && pedUnit != ampUnit && p->unitShortName (pedUnit) == "PED" && p->cellOfUnit (ampUnit) == ampCellBefore + 1,
               "pedal capture is inserted before the amp (amp shifts right), labelled PED");
        check (p->getOnlineMessage().contains ("CHORUS") && p->getOnlineMessage().contains ("CHO"), "warning for a chorus capture suggests the CHO block");

        tde->routeFile (-1, pedalFile, "pedal", true);
        check (p->unitAtCell (pedCell) == pedUnit && p->cellOfUnit (ampUnit) == ampCellBefore + 1, "a second pedal capture replaces the pedal slot (no duplicate)");

        const int cabUnit = p->findUnitOfType (BlockType::Cab);
        tde->routeFile (-1, ampFile, "amp-cab", true);
        waitModel (*p, ampUnit);
        check (p->unitShortName (ampUnit) == "RIG", "TONE3000 gear amp-cab -> RIG label");
        check (! p->isUnitOn (cabUnit), "RIG capture switches the CAB off (no double speaker)");
        tde->routeFile (-1, ampFile, "amp", true);
        waitModel (*p, ampUnit);
        check (p->isUnitOn (cabUnit), "AMP (head) capture switches the CAB back on");
    }

    // --- full state round trip: layout, types, files, snapshots, footswitches, MIDI
    {
        auto p = makeProc (48000, 256);
        const int amp = p->findUnitOfType (BlockType::Amp);
        p->loadUnitFile (amp, 0, models.getFirst());
        waitModel (*p, amp);
        const int cab = p->findUnitOfType (BlockType::Cab);
        p->loadUnitFile (cab, 1, irA);
        p->loadUnitFile (cab, 2, irB);
        const int cho = p->addBlock (Layout::index (1, 5), BlockType::Chorus);
        setNorm (p->unitParam (cho, 0), 0.77f);
        p->recallSnapshot (2);
        p->setFsAssign (3, { FootswitchAssign::ToggleUnit, cho });
        p->startMidiLearn (p->paramIndex (p->unitParam (cho, 3)));
        { AudioBuffer<float> b (2, 256); MidiBuffer m; m.addEvent (MidiMessage::controllerEvent (1, 11, 64), 0); p->processBlock (b, m); }

        MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeProc (48000, 256);
        q->setStateInformation (state.getData(), (int) state.getSize());
        pump (50);
        waitModel (*q, amp);
        pump (100);
        const bool ok = q->unitAtCell (Layout::index (1, 5)) == cho && q->getUnit (cho).type == BlockType::Chorus
                        && std::abs (q->unitParam (cho, 0)->getValue() - 0.77f) < 0.01f
                        && q->getUnit (amp).files[0].loaded && q->getUnit (cab).files[1].loaded && q->getUnit (cab).files[2].loaded
                        && q->getCurrentSnapshot() == 2 && q->getFsAssign (3).unit == cho
                        && q->ccForTarget (q->paramIndex (q->unitParam (cho, 3))) == 11;
        check (ok, "state save/restore: grid, block types, files, snapshots, footswitches, MIDI map");

        // UI snapshots from this rig
        setOn (*q, q->findUnitOfType (BlockType::Overdrive), true);
        render (*q, 48000, 256, 0.3);
        std::unique_ptr<AudioProcessorEditor> ed (q->createEditor());
        ed->setSize (ToneDeckEditor::baseW, ToneDeckEditor::baseH);
        auto* tde = dynamic_cast<ToneDeckEditor*> (ed.get());
        const auto save = [&] (const String& name)
        {
            pump (80);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            PNGImageFormat png;
            const auto f = outDir.getChildFile (name);
            f.deleteFile();
            FileOutputStream os (f);
            png.writeImageToStream (img, os);
            std::cout << "snapshot " << f.getFullPathName() << "\n";
        };
        auto select = [&] (int cell) { tde->getLcd().setSelection (cell); if (tde->getLcd().onSelect) tde->getLcd().onSelect (cell); };
        select (q->cellOfUnit (amp)); save ("ui_amp.png");
        select (q->cellOfUnit (cab)); save ("ui_cab.png");
        select (Layout::index (1, 5)); save ("ui_parallel.png");
        q->setFsPage (FsPage::Snapshot); save ("ui_snapshots.png");
        q->setFsPage (FsPage::Looper); q->looperRecPlay(); render (*q, 48000, 256, 0.5); save ("ui_looper.png");
        q->setFsPage (FsPage::Stomp);
        q->setTunerOn (true);
        {
            AudioBuffer<float> b (2, 256); MidiBuffer m; double ph = 0;
            for (int k = 0; k < 40; ++k) { for (int i = 0; i < 256; ++i) { const float s = 0.3f * (float) std::sin (ph); b.setSample (0, i, s); b.setSample (1, i, s); ph += MathConstants<double>::twoPi * 109.2 / 48000.0; } q->processBlock (b, m); }
        }
        save ("ui_tuner.png");
        q->setTunerOn (false);
    }

    std::cout << (failures == 0 ? String ("ALL TESTS PASSED") : String (failures) + " FAILURE(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
