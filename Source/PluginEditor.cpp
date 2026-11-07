#include "PluginEditor.h"

using namespace juce;
using namespace td;
using namespace td::ui;

//==============================================================================
void LearnableKnob::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || proc == nullptr || ! getMidiTarget)
    {
        Slider::mouseDown (e);
        return;
    }
    const int target = getMidiTarget();
    if (target == ToneDeckProcessor::kNoTarget) return;
    const int cc = proc->ccForTarget (target);
    PopupMenu m;
    m.addSectionHeader (cc >= 0 ? "MIDI: CC " + String (cc) : String ("MIDI: sin asignar"));
    m.addItem (1, "MIDI Learn (move un control de tu controladora)");
    m.addItem (2, "Borrar asignacion MIDI", cc >= 0);
    auto* p = proc;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [p, target] (int r)
    {
        if (r == 1) { p->startMidiLearn (target); p->setOnlineMessage ("MIDI LEARN: move un control..."); }
        if (r == 2) p->clearMidiFor (target);
    });
}

//==============================================================================
void Footswitch::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (fs); return; }
    pressed = true;
    proc.pressFootswitch (fs);
    repaint();
}

void Footswitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool state = proc.footswitchLit (fs);
    const Colour led = proc.footswitchColour (fs);

    auto ledArea = r.removeFromTop (18.0f);
    const auto ledR = Rectangle<float> (10.0f, 10.0f).withCentre (ledArea.getCentre());
    if (state) { g.setColour (led.withAlpha (0.35f)); g.fillEllipse (ledR.expanded (6.0f)); }
    g.setColour (state ? led : led.darker (2.6f));
    g.fillEllipse (ledR);
    g.setColour (Colours::white.withAlpha (state ? 0.7f : 0.15f));
    g.fillEllipse (ledR.reduced (3.0f).translated (-1.0f, -1.0f));

    // scribble strip
    auto labelArea = r.removeFromBottom (18.0f).reduced (8.0f, 0.0f);
    g.setColour (Colour (0xff070b14));
    g.fillRoundedRectangle (labelArea, 3.0f);
    g.setColour (state ? led : colours::silkscreen);
    g.setFont (lcdFont (11.5f, true));
    g.drawText (proc.footswitchLabel (fs), labelArea, Justification::centred, true);

    const float s = jmin (r.getWidth(), r.getHeight()) - 6.0f;
    const auto c = r.getCentre();
    Path nut;
    nut.addPolygon (c, 6, s * 0.5f, MathConstants<float>::pi / 6.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (nut, AffineTransform::translation (0.0f, 3.0f));
    g.setGradientFill (ColourGradient (Colour (0xffb9bec7), c.x, c.y - s * 0.5f, Colour (0xff4a4e55), c.x, c.y + s * 0.5f, false));
    g.fillPath (nut);
    g.setColour (Colour (0xff2b2e33));
    g.strokePath (nut, PathStrokeType (1.0f));
    const float dr = s * (pressed ? 0.30f : 0.32f);
    const auto dome = Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (c.translated (0.0f, pressed ? 1.5f : 0.0f));
    g.setGradientFill (ColourGradient (Colour (0xfff4f6fa), dome.getX() + dr * 0.6f, dome.getY() + dr * 0.4f, Colour (0xff5d626b), dome.getRight(), dome.getBottom(), true));
    g.fillEllipse (dome);
    g.setColour (Colour (0xff30333a));
    g.drawEllipse (dome, 1.2f);
}

//==============================================================================
ToneDeckEditor::ToneDeckEditor (ToneDeckProcessor& p)
    : AudioProcessorEditor (p), proc (p), lcd (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.addAndMakeVisible (lcd);

    auto setupKnob = [this] (LearnableKnob& s)
    {
        s.proc = &proc;
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, true, 90, 16);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        body.addAndMakeVisible (s);
    };
    for (int i = 0; i < numKnobs; ++i)
    {
        setupKnob (knobs[(size_t) i]);
        knobs[(size_t) i].getMidiTarget = [this, i]
        { return knobParams[(size_t) i] != nullptr ? proc.paramIndex (knobParams[(size_t) i]) : ToneDeckProcessor::kNoTarget; };
    }
    setupKnob (inputKnob);
    setupKnob (masterKnob);
    for (auto* k : { &inputKnob, &masterKnob }) k->setColour (Slider::rotarySliderFillColourId, colours::accent);
    inputAtt  = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter ("in_gain"), inputKnob);
    masterAtt = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter ("out_gain"), masterKnob);
    inputKnob.getMidiTarget  = [this] { return proc.paramIndex (proc.apvts.getParameter ("in_gain")); };
    masterKnob.getMidiTarget = [this] { return proc.paramIndex (proc.apvts.getParameter ("out_gain")); };

    for (auto* b : { &presetPrev, &presetNext, &presetSave, &t3kBtn, &tunerBtn, &looperBtn, &pageBtn,
                     &loadBtn, &filePrev, &fileNext, &libraryBtn, &normBtn })
        body.addAndMakeVisible (*b);

    normBtn.setClickingTogglesState (true);
    normBtn.setTooltip ("Iguala el volumen de los modelos NAM con metadato de loudness (-18 dB)");
    normAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "normalize", normBtn);

    presetPrev.onClick = [this] { proc.stepPreset (-1); };
    presetNext.onClick = [this] { proc.stepPreset (1); };
    presetSave.onClick = [this] { savePresetDialog(); };
    t3kBtn.onClick     = [this] { showTone3000Menu(); };
    tunerBtn.onClick   = [this] { proc.setTunerOn (! proc.isTunerOn()); };
    looperBtn.onClick  = [this] { proc.setFsPage (proc.getFsPage() == FsPage::Looper ? FsPage::Stomp : FsPage::Looper); };
    pageBtn.onClick    = [this]
    {
        proc.setFsPage (proc.getFsPage() == FsPage::Stomp ? FsPage::Snapshot : FsPage::Stomp);
    };
    loadBtn.onClick    = [this]
    {
        const int u = lcd.selectedUnit();
        if (u >= 0) openFileChooser (u, proc.getUnit (u).type == BlockType::Cab ? 1 : 0);
        else openFileChooser (unitForFileKind (SlotKind::Model), 0);
    };
    filePrev.onClick   = [this] { stepFile (-1); };
    fileNext.onClick   = [this] { stepFile (1); };
    libraryBtn.onClick = [this] { lcd.showLibrary (! lcd.isShowingLibrary()); refreshButtons(); };

    lcd.onSelect      = [this] (int cell) { onCellSelected (cell); };
    lcd.onAddRequest  = [this] (int cell) { showAddMenu (cell); };
    lcd.onContextMenu = [this] (int cell) { proc.unitAtCell (cell) >= 0 ? showBlockMenu (cell) : showAddMenu (cell); };
    lcd.onMove        = [this] (int a, int b) { proc.moveBlock (a, b); };
    lcd.onToggle      = [this] (int u) { proc.toggleUnit (u); };
    lcd.onLoadRequest = [this] (int u, int which) { openFileChooser (u, which); };
    lcd.onFileDropped = [this] (int cell, const File& f) { routeFile (cell, f); };
    lcd.onLibraryPick = [this] (const File& f) { routeFile (lcd.getSelection(), f); };
    lcd.onChooseFolder = [this] { chooseLibraryFolder(); };

    for (int i = 0; i < kNumFootswitches; ++i)
    {
        auto fs = std::make_unique<Footswitch> (proc, i);
        fs->onRightClick = [this] (int f) { showFootswitchMenu (f); };
        body.addAndMakeVisible (*fs);
        footswitches.push_back (std::move (fs));
    }

    proc.addChangeListener (this);
    const int ampCell = proc.cellOfUnit (proc.findUnitOfType (BlockType::Amp));
    lcd.setSelection (ampCell >= 0 ? ampCell : 0);
    onCellSelected (lcd.getSelection());

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    startTimerHz (20);
}

ToneDeckEditor::~ToneDeckEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void ToneDeckEditor::timerCallback()
{
    for (auto& f : footswitches) f->repaint();
    refreshButtons();
}

void ToneDeckEditor::resized()
{
    body.setBounds (0, 0, baseW, baseH);
    body.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    layoutBody();
}

void ToneDeckEditor::layoutBody()
{
    lcd.setBounds (48, 74, baseW - 96, 252);

    const int y = 342, h = 26;
    int x = 48;
    for (auto* b : { &presetPrev, &presetNext, &presetSave, &t3kBtn })
    {
        const int w = b == &t3kBtn ? 92 : (b == &presetSave ? 54 : 60);
        b->setBounds (x, y, w, h);
        x += w + 6;
    }
    x += 22;
    for (auto* b : { &tunerBtn, &looperBtn, &pageBtn })
    {
        const int w = b == &pageBtn ? 116 : 70;
        b->setBounds (x, y, w, h);
        x += w + 6;
    }
    x = baseW - 48;
    for (auto* b : { &normBtn, &libraryBtn, &fileNext, &filePrev, &loadBtn })
    {
        const int w = (b == &filePrev || b == &fileNext) ? 30 : (b == &libraryBtn ? 70 : 58);
        x -= w;
        b->setBounds (x, y, w, h);
        x -= 6;
    }

    const int knobY = 412, knobSize = 96;
    inputKnob.setBounds (40, knobY, knobSize + 8, knobSize + 16);
    masterKnob.setBounds (baseW - 48 - knobSize, knobY, knobSize + 8, knobSize + 16);
    const int left = 180, span = baseW - 2 * left;
    for (int i = 0; i < numKnobs; ++i)
    {
        const int cx = left + (int) ((i + 0.5f) * (float) span / (float) numKnobs);
        knobs[(size_t) i].setBounds (cx - knobSize / 2, knobY, knobSize, knobSize + 16);
    }

    const int fsW = 120, fsY = 560, fsH = 92;
    for (size_t i = 0; i < footswitches.size(); ++i)
    {
        const int cx = 130 + (int) ((float) i * (float) (baseW - 260) / (float) (footswitches.size() - 1));
        footswitches[i]->setBounds (cx - fsW / 2, fsY, fsW, fsH);
    }
}

//==============================================================================
void ToneDeckEditor::Body::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (Colour (0xff0b0c0e));
    auto encl = r.reduced (6.0f);
    g.setGradientFill (ColourGradient (colours::bodyTop, 0.0f, encl.getY(), colours::bodyBottom, 0.0f, encl.getBottom(), false));
    g.fillRoundedRectangle (encl, 22.0f);
    Random rng (42);
    for (int i = 0; i < 900; ++i)
    {
        g.setColour (Colours::white.withAlpha (rng.nextFloat() * 0.018f));
        g.drawHorizontalLine ((int) (encl.getY() + rng.nextFloat() * encl.getHeight()), encl.getX() + 18.0f, encl.getRight() - 18.0f);
    }
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (encl.reduced (1.0f), 22.0f, 1.5f);

    for (auto pt : { Point<float> (26, 26), Point<float> (r.getRight() - 26, 26), Point<float> (26, r.getBottom() - 26), Point<float> (r.getRight() - 26, r.getBottom() - 26) })
    {
        const auto s = Rectangle<float> (14.0f, 14.0f).withCentre (pt);
        g.setGradientFill (ColourGradient (Colour (0xff9aa0a8), s.getX(), s.getY(), Colour (0xff2b2e33), s.getRight(), s.getBottom(), false));
        g.fillEllipse (s);
        g.setColour (Colour (0xff1a1c20));
        g.drawLine (s.getX() + 3, s.getCentreY(), s.getRight() - 3, s.getCentreY(), 1.6f);
        g.drawLine (s.getCentreX(), s.getY() + 3, s.getCentreX(), s.getBottom() - 3, 1.6f);
    }

    g.setColour (colours::silkscreen);
    g.setFont (uiFont (30.0f).withExtraKerningFactor (0.18f));
    g.drawText ("TONEDECK", 48, 22, 300, 40, Justification::centredLeft);
    g.setColour (colours::accent);
    g.setFont (uiFont (11.0f).withExtraKerningFactor (0.3f));
    g.drawText ("NEURAL MULTI-FX  |  2 PATHS  |  16 BLOCKS", 262, 34, 420, 20, Justification::centredLeft);
    g.setColour (colours::silkDim);
    g.setFont (uiFont (11.0f, false));
    g.drawText ("NAM  |  A2  |  AIDA-X  |  GuitarML  |  IR", baseW - 348, 34, 300, 20, Justification::centredRight);

    const auto lcdB = owner.lcd.getBounds().toFloat();
    g.setColour (Colours::black);
    g.fillRoundedRectangle (lcdB.expanded (9.0f), 13.0f);
    g.setColour (Colour (0xff3b3f47));
    g.drawRoundedRectangle (lcdB.expanded (9.0f), 13.0f, 1.2f);

    g.setFont (uiFont (12.0f));
    for (int i = 0; i < numKnobs; ++i)
    {
        const auto kb = owner.knobs[(size_t) i].getBounds();
        g.setColour (owner.knobs[(size_t) i].isEnabled() ? colours::silkscreen : colours::silkDim.withAlpha (0.4f));
        g.drawText (owner.knobLabels[(size_t) i], kb.getX() - 10, kb.getY() - 18, kb.getWidth() + 20, 16, Justification::centred);
    }
    g.setColour (colours::silkscreen);
    for (auto* k : { &owner.inputKnob, &owner.masterKnob })
        g.drawText (k == &owner.inputKnob ? "INPUT" : "MASTER", k->getX() - 10, k->getY() - 18, k->getWidth() + 20, 16, Justification::centred);

    g.setColour (colours::panelLine.withAlpha (0.6f));
    g.drawLine (166.0f, 392.0f, 166.0f, 540.0f, 1.0f);
    g.drawLine ((float) baseW - 166.0f, 392.0f, (float) baseW - 166.0f, 540.0f, 1.0f);
    g.drawLine (40.0f, 550.0f, (float) baseW - 40.0f, 550.0f, 1.0f);

    String tag;
    Colour tagCol = colours::accent;
    const int sel = owner.lcd.getSelection();
    if (sel == kMixerSelection) tag = "EDIT  >  MEZCLADOR A/B";
    else if (const int u = owner.proc.unitAtCell (sel); u >= 0)
    {
        const auto& info = blockInfo (owner.proc.getUnit (u).type);
        tag = String ("EDIT  >  ") + info.longName;
        tagCol = Colour (info.colour);
    }
    g.setColour (tagCol);
    g.setFont (uiFont (11.0f).withExtraKerningFactor (0.2f));
    g.drawText (tag, 180, 376, baseW - 360, 14, Justification::centred);
}

//==============================================================================
void ToneDeckEditor::onCellSelected (int)
{
    boundUnit = -999;
    refreshKnobs();
    refreshButtons();
    body.repaint();
}

void ToneDeckEditor::refreshButtons()
{
    tunerBtn.setToggleState (proc.isTunerOn(), dontSendNotification);
    looperBtn.setToggleState (proc.getFsPage() == FsPage::Looper, dontSendNotification);
    pageBtn.setButtonText (proc.getFsPage() == FsPage::Snapshot ? "FS: SNAPSHOTS" : (proc.getFsPage() == FsPage::Looper ? "FS: LOOPER" : "FS: STOMP"));
    pageBtn.setToggleState (proc.getFsPage() == FsPage::Snapshot, dontSendNotification);
    libraryBtn.setToggleState (lcd.isShowingLibrary(), dontSendNotification);
}

void ToneDeckEditor::refreshKnobs()
{
    const int sel = lcd.getSelection();
    const int u = sel == kMixerSelection ? -2 : proc.unitAtCell (sel);

    // only rebind when something relevant changed (keeps drags smooth)
    int signature = u >= 0 ? (int) proc.getUnit (u).type * 64 : 0;
    if (u >= 0)
        for (int i = 0; i < kParamsPerUnit; ++i)
            if (paramVisible (proc.getUnit (u), i)) signature |= 1 << i;
    if (u == boundUnit && signature == boundSignature) return;
    boundUnit = u; boundSignature = signature;

    struct Bind { RangedAudioParameter* prm; String label; std::function<String (double)> text; Colour col; };
    std::vector<Bind> binds;
    if (u == -2)
    {
        for (auto [id, label] : { std::pair<const char*, const char*> { "merge_a", "NIVEL A" }, { "merge_b", "NIVEL B" }, { "pan_a", "PAN A" }, { "pan_b", "PAN B" } })
        {
            auto* prm = proc.apvts.getParameter (id);
            binds.push_back ({ prm, label, [prm] (double v) { return prm->getText ((float) v, 20); }, colours::accent });
        }
    }
    else if (u >= 0)
    {
        const auto& st = proc.getUnit (u);
        const auto& info = blockInfo (st.type);
        for (int i = 0; i < kParamsPerUnit; ++i)
        {
            if (! paramVisible (st, i)) continue;
            const auto spec = info.params[(size_t) i];
            binds.push_back ({ proc.unitParam (u, i), spec.name, [spec] (double v) { return spec.format (spec.toPlain ((float) v)); }, Colour (info.colour) });
        }
    }

    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        knobAttachments[(size_t) i].reset();
        if (i < (int) binds.size())
        {
            const auto& b = binds[(size_t) i];
            knobParams[(size_t) i] = b.prm;
            k.setEnabled (true);
            k.setColour (Slider::rotarySliderFillColourId, b.col);
            k.setTextBoxStyle (Slider::TextBoxBelow, true, 90, 16);
            knobAttachments[(size_t) i] = std::make_unique<SliderParameterAttachment> (*b.prm, k);
            k.textFromValueFunction = b.text;
            k.updateText();
            knobLabels[(size_t) i] = b.label;
        }
        else
        {
            knobParams[(size_t) i] = nullptr;
            k.setEnabled (false);
            k.textFromValueFunction = nullptr;
            k.setRange (0.0, 1.0);
            k.setValue (0.0, dontSendNotification);
            k.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            knobLabels[(size_t) i] = "-";
        }
    }
    body.repaint();
}

//==============================================================================
void ToneDeckEditor::addTypeItems (PopupMenu& menu, int idBase, BlockType current)
{
    for (int c = 0; c <= (int) Category::Utility; ++c)
    {
        PopupMenu sub;
        for (int t = 1; t < (int) BlockType::NumTypes; ++t)
        {
            const auto& info = blockInfo ((BlockType) t);
            if ((int) info.category == c)
                sub.addItem (idBase + t, String (info.shortName) + "   " + info.longName, true, (int) current == t);
        }
        menu.addSubMenu (categoryName ((Category) c), sub);
    }
}

void ToneDeckEditor::showAddMenu (int cell)
{
    PopupMenu m;
    m.addSectionHeader (String ("AGREGAR BLOQUE  (") + (cell >= kGridCols ? "camino B" : "camino A") + ")");
    addTypeItems (m, 1000, BlockType::Empty);
    auto safe = safeThis;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&lcd).withMousePosition(), [safe, cell] (int r)
    {
        if (safe == nullptr || r < 1000) return;
        if (safe->proc.addBlock (cell, (BlockType) (r - 1000)) >= 0)
        {
            safe->lcd.setSelection (cell);
            safe->onCellSelected (cell);
        }
    });
}

void ToneDeckEditor::showBlockMenu (int cell)
{
    const int u = proc.unitAtCell (cell);
    if (u < 0) return;
    const auto& st = proc.getUnit (u);
    const auto& info = blockInfo (st.type);
    PopupMenu m;
    m.addSectionHeader (info.longName);
    m.addItem (1, proc.isUnitOn (u) ? "Apagar (bypass)" : "Encender");
    if (info.usesModelFile)
    {
        m.addItem (10, "Cargar modelo...");
        m.addItem (13, "Quitar modelo", st.files[0].loaded || st.files[0].error.isNotEmpty());
    }
    if (info.usesIrFile)
    {
        m.addItem (11, "Cargar IR A...");
        m.addItem (12, "Cargar IR B...");
        m.addItem (14, "Quitar IR A", st.files[1].loaded);
        m.addItem (15, "Quitar IR B", st.files[2].loaded);
    }
    PopupMenu change;
    addTypeItems (change, 1000, st.type);
    m.addSubMenu ("Cambiar tipo", change);
    PopupMenu fsMenu;
    for (int f = 0; f < kNumFootswitches; ++f) fsMenu.addItem (100 + f, "Footswitch " + String (f + 1));
    m.addSubMenu ("Asignar a footswitch", fsMenu);
    m.addSeparator();
    m.addItem (2, "Quitar bloque");

    auto safe = safeThis;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&lcd).withMousePosition(), [safe, cell, u] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        if (r == 1) pr.toggleUnit (u);
        else if (r == 2) { pr.removeBlock (cell); safe->onCellSelected (cell); }
        else if (r >= 10 && r <= 12) safe->openFileChooser (u, r - 10);
        else if (r >= 13 && r <= 15) pr.clearUnitFile (u, r - 13);
        else if (r >= 100 && r < 100 + kNumFootswitches) { pr.setFsAssign (r - 100, { FootswitchAssign::ToggleUnit, u }); pr.setFsPage (FsPage::Stomp); }
        else if (r >= 1000) { pr.changeBlockType (cell, (BlockType) (r - 1000)); safe->onCellSelected (cell); }
    });
}

void ToneDeckEditor::showFootswitchMenu (int fs)
{
    const auto& a = proc.getFsAssign (fs);
    PopupMenu m;
    m.addSectionHeader ("FOOTSWITCH " + String (fs + 1) + "  (pagina STOMP)");
    m.addItem (1, "Sin asignar", true, a.kind == FootswitchAssign::None);
    m.addItem (2, "Tap tempo (delay)", true, a.kind == FootswitchAssign::TapTempo);
    m.addItem (3, "Afinador", true, a.kind == FootswitchAssign::Tuner);
    m.addSeparator();
    for (int c = 0; c < kGridCells; ++c)
    {
        const int u = proc.unitAtCell (c);
        if (u < 0) continue;
        const auto& info = blockInfo (proc.getUnit (u).type);
        m.addItem (100 + u, String ("Encender/apagar ") + info.shortName + "  (" + (c >= kGridCols ? "B" : "A") + String (c % kGridCols + 1) + ")",
                   true, a.kind == FootswitchAssign::ToggleUnit && a.unit == u);
    }
    m.addSeparator();
    const int target = -100 - fs;
    const int cc = proc.ccForTarget (target);
    m.addItem (4, "MIDI Learn" + (cc >= 0 ? "  (CC " + String (cc) + ")" : String()));
    m.addItem (5, "Borrar MIDI", cc >= 0);

    auto safe = safeThis;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (footswitches[(size_t) fs].get()), [safe, fs, target] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        if (r == 1) pr.setFsAssign (fs, {});
        else if (r == 2) pr.setFsAssign (fs, { FootswitchAssign::TapTempo, -1 });
        else if (r == 3) pr.setFsAssign (fs, { FootswitchAssign::Tuner, -1 });
        else if (r == 4) { pr.startMidiLearn (target); pr.setOnlineMessage ("MIDI LEARN: pisa tu pedal / move un control..."); }
        else if (r == 5) pr.clearMidiFor (target);
        else if (r >= 100) pr.setFsAssign (fs, { FootswitchAssign::ToggleUnit, r - 100 });
        pr.setFsPage (FsPage::Stomp);
    });
}

//==============================================================================
int ToneDeckEditor::unitForFileKind (SlotKind kind)
{
    const auto wanted = kind == SlotKind::IR ? BlockType::Cab : BlockType::Amp;
    const int selU = lcd.selectedUnit();
    if (selU >= 0 && proc.getUnit (selU).type == wanted) return selU;
    if (const int u = proc.findUnitOfType (wanted); u >= 0) return u;
    for (int c = 0; c < kGridCols; ++c)   // add one in the first free cell of path A
        if (proc.unitAtCell (c) < 0) return proc.addBlock (c, wanted);
    return -1;
}

void ToneDeckEditor::routeFile (int cell, const File& f)
{
    const auto kind = ToneDeckProcessor::kindForFile (f);
    if (kind == SlotKind::None) return;
    const auto wanted = kind == SlotKind::IR ? BlockType::Cab : BlockType::Amp;
    int u = proc.unitAtCell (cell);

    if (cell >= 0 && u < 0) u = proc.addBlock (cell, wanted);              // dropped on an empty cell
    else if (u >= 0 && proc.getUnit (u).type != wanted) u = unitForFileKind (kind);
    else if (u < 0) u = unitForFileKind (kind);
    if (u < 0) return;

    proc.loadUnitFile (u, kind == SlotKind::IR ? 1 : 0, f);   // IR B: via the block's menu or its file box
    const int c = proc.cellOfUnit (u);
    if (! lcd.isShowingLibrary()) lcd.setSelection (c);
    onCellSelected (c);
}

void ToneDeckEditor::openFileChooser (int unit, int which)
{
    if (unit < 0) return;
    const auto& st = proc.getUnit (unit).files[(size_t) which];
    auto start = st.file.existsAsFile() ? st.file.getParentDirectory() : proc.getLibraryFolder();
    if (! start.isDirectory()) start = File::getSpecialLocation (File::userDocumentsDirectory);
    const bool ir = which > 0;
    chooser = std::make_unique<FileChooser> (ir ? "Cargar IR" : "Cargar modelo (NAM / AIDA-X / GuitarML)", start,
                                             ir ? ToneDeckProcessor::irWildcard() : ToneDeckProcessor::modelWildcard());
    auto safe = safeThis;
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe, unit, which] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe != nullptr && f.existsAsFile()) safe->proc.loadUnitFile (unit, which, f);
    });
}

void ToneDeckEditor::stepFile (int delta)
{
    int u = lcd.selectedUnit();
    if (u < 0 || ! (blockInfo (proc.getUnit (u).type).usesModelFile || blockInfo (proc.getUnit (u).type).usesIrFile))
        u = proc.findUnitOfType (BlockType::Amp);
    if (u < 0) return;
    const bool ir = proc.getUnit (u).type == BlockType::Cab;
    const auto current = proc.getUnit (u).files[ir ? 1 : 0].file;
    const auto dir = current.existsAsFile() ? current.getParentDirectory() : proc.getLibraryFolder();
    auto files = dir.findChildFiles (File::findFiles, false, ir ? ToneDeckProcessor::irWildcard() : ToneDeckProcessor::modelWildcard());
    if (files.isEmpty()) return;
    files.sort();
    int idx = files.indexOf (current);
    idx = idx < 0 ? (delta > 0 ? 0 : files.size() - 1) : (idx + delta + files.size()) % files.size();
    proc.loadUnitFile (u, ir ? 1 : 0, files[idx]);
}

void ToneDeckEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<FileChooser> ("Elegi tu carpeta de modelos / IRs", proc.getLibraryFolder());
    auto safe = safeThis;
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [safe] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe != nullptr && f.isDirectory()) { safe->proc.setLibraryFolder (f); safe->lcd.refreshLibrary(); safe->lcd.repaint(); }
    });
}

void ToneDeckEditor::savePresetDialog()
{
    auto* w = new AlertWindow ("Guardar preset", "Nombre:", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getCurrentPresetName() == "INIT" ? String ("Mi tono") : proc.getCurrentPresetName());
    w->addButton ("Guardar", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    auto safe = safeThis;
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe != nullptr && result == 1) safe->proc.savePreset (w->getTextEditorContents ("name"));
    }), true);
}

//==============================================================================
// TONE3000
void ToneDeckEditor::askClientId (std::function<void()> then)
{
    auto* w = new AlertWindow ("TONE3000",
                               "Pega tu clave PUBLICA de TONE3000 (empieza con t3k_pub_).\n"
                               "La creas en tone3000.com > Settings > API Keys.\n"
                               "Nunca uses la clave secreta (t3k_cs_).",
                               MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("key", proc.tone3000().getClientId());
    w->addButton ("Guardar", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    auto safe = safeThis;
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w, then] (int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto key = w->getTextEditorContents ("key").trim();
        if (key.startsWith ("t3k_cs_"))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "TONE3000",
                                              "Esa es la clave SECRETA. No la uses en el plugin: rotala en TONE3000 y pega la publica (t3k_pub_).");
            return;
        }
        safe->proc.tone3000().setClientId (key);
        if (key.isNotEmpty() && then) then();
    }), true);
}

void ToneDeckEditor::showTone3000Menu()
{
    auto& t3k = proc.tone3000();
    if (t3k.getClientId().isEmpty()) { askClientId ([this] { showTone3000Menu(); }); return; }

    PopupMenu m;
    m.addSectionHeader (t3k.isSignedIn() && t3k.getUsername().isNotEmpty() ? "TONE3000  -  " + t3k.getUsername() : String ("TONE3000"));
    m.addItem (1, "Elegir tono en el navegador...");
    m.addSeparator();
    m.addItem (2, "Mis favoritos");
    m.addItem (3, "Mis descargados");
    m.addItem (4, "Mis tonos subidos");
    m.addItem (5, "Populares");
    m.addItem (6, "Nuevos");
    m.addSeparator();
    m.addItem (7, "Abrir carpeta de descargas");
    m.addItem (8, "Cambiar clave publica...");
    m.addItem (9, "Cerrar sesion", t3k.isSignedIn());

    auto safe = safeThis;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&t3kBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        switch (r)
        {
            case 1: self.proc.tone3000().selectTone ([safe] (auto models, auto err) { if (safe != nullptr) safe->handleTone3000Result (models, err); }); break;
            case 2: self.showToneList ("favorited", "Mis favoritos"); break;
            case 3: self.showToneList ("downloaded", "Mis descargados"); break;
            case 4: self.showToneList ("created", "Mis tonos"); break;
            case 5: self.showToneList ("trending", "Populares"); break;
            case 6: self.showToneList ("latest", "Nuevos"); break;
            case 7: { auto dir = self.proc.getLibraryFolder().getChildFile ("TONE3000"); dir.createDirectory(); dir.startAsProcess(); break; }
            case 8: self.askClientId ({}); break;
            case 9: self.proc.tone3000().signOut(); self.proc.setOnlineMessage ("TONE3000: sesion cerrada"); break;
            default: break;
        }
    });
}

void ToneDeckEditor::showToneList (const String& listName, const String& title)
{
    auto safe = safeThis;
    proc.tone3000().fetchList (listName, [safe, title] (Array<Tone3000Client::ToneItem> items, String err)
    {
        if (safe == nullptr) return;
        if (err.isNotEmpty()) { safe->proc.setOnlineMessage (err); return; }
        if (items.isEmpty()) { safe->proc.setOnlineMessage ("TONE3000: la lista \"" + title + "\" esta vacia"); return; }
        PopupMenu m;
        m.addSectionHeader (title);
        for (int i = 0; i < items.size(); ++i)
        {
            const auto& it = items.getReference (i);
            String label = it.title;
            if (it.format.isNotEmpty()) label << "   [" << it.format.toUpperCase() << "]";
            if (it.author.isNotEmpty()) label << "   by " << it.author;
            m.addItem (i + 1, label);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&safe->t3kBtn).withMaximumNumColumns (2), [safe, items] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->proc.tone3000().fetchTone (items[r - 1].id, [safe] (auto models, auto e2) { if (safe != nullptr) safe->handleTone3000Result (models, e2); });
        });
    });
}

void ToneDeckEditor::handleTone3000Result (const Array<Tone3000Client::DownloadedModel>& models, const String& error)
{
    if (models.isEmpty()) { proc.setOnlineMessage (error.isNotEmpty() ? error : String ("TONE3000: sin resultados")); return; }
    auto load = [this] (const Tone3000Client::DownloadedModel& m)
    {
        routeFile (lcd.getSelection(), m.file);
        proc.setOnlineMessage ("TONE3000: cargado " + m.modelName);
    };
    if (models.size() == 1) { load (models.getReference (0)); return; }

    proc.setOnlineMessage ("TONE3000: " + String (models.size()) + " modelos descargados");
    PopupMenu m;
    m.addSectionHeader (models.getReference (0).toneTitle);
    for (int i = 0; i < models.size(); ++i) m.addItem (i + 1, models.getReference (i).modelName);
    auto safe = safeThis;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&t3kBtn), [safe, models, load] (int r)
    {
        if (safe != nullptr && r > 0) load (models.getReference (r - 1));
    });
}
