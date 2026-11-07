#include "PluginEditor.h"

using namespace juce;
using namespace td::ui;

//==============================================================================
Footswitch::Footswitch (RangedAudioParameter& param, String label, Colour ledColour)
    : attachment (param, [this] (float v) { state = v > 0.5f; repaint(); }),
      text (std::move (label)), led (ledColour)
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Footswitch::mouseDown (const MouseEvent&)
{
    pressed = true;
    attachment.setValueAsCompleteGesture (state ? 0.0f : 1.0f);
    if (onPress) onPress();
    repaint();
}

void Footswitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // LED
    auto ledArea = r.removeFromTop (18.0f);
    const auto ledR = Rectangle<float> (10.0f, 10.0f).withCentre (ledArea.getCentre());
    if (state)
    {
        g.setColour (led.withAlpha (0.35f));
        g.fillEllipse (ledR.expanded (6.0f));
    }
    g.setColour (state ? led : led.darker (2.6f));
    g.fillEllipse (ledR);
    g.setColour (Colours::white.withAlpha (state ? 0.7f : 0.15f));
    g.fillEllipse (ledR.reduced (3.0f).translated (-1.0f, -1.0f));

    // label strip
    auto labelArea = r.removeFromBottom (16.0f);
    g.setColour (colours::silkscreen);
    g.setFont (uiFont (12.0f));
    g.drawText (text, labelArea, Justification::centred);

    // switch: hex nut + chrome dome
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
    g.setGradientFill (ColourGradient (Colour (0xfff4f6fa), dome.getX() + dr * 0.6f, dome.getY() + dr * 0.4f,
                                       Colour (0xff5d626b), dome.getRight(), dome.getBottom(), true));
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

    auto setupKnob = [this] (Slider& s)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, true, 90, 16);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        s.setVelocityBasedMode (false);
        body.addAndMakeVisible (s);
    };
    for (auto& k : knobs) setupKnob (k);
    setupKnob (inputKnob);
    setupKnob (masterKnob);
    inputKnob.setColour (Slider::rotarySliderFillColourId, colours::accent);
    masterKnob.setColour (Slider::rotarySliderFillColourId, colours::accent);
    inputAtt  = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "in_gain", inputKnob);
    masterAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "out_gain", masterKnob);

    for (auto* b : { &presetPrev, &presetNext, &presetSave, &loadBtn, &filePrev, &fileNext, &libraryBtn, &ejectBtn, &normBtn })
        body.addAndMakeVisible (*b);

    normBtn.setClickingTogglesState (true);
    normBtn.setTooltip ("Level-match NAM models that carry loudness metadata (-18 dB)");
    normAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "normalize", normBtn);

    presetPrev.onClick = [this] { proc.stepPreset (-1); };
    presetNext.onClick = [this] { proc.stepPreset (1); };
    presetSave.onClick = [this] { savePresetDialog(); };
    loadBtn.onClick    = [this] { openFileChooser (currentFileSlot()); };
    filePrev.onClick   = [this] { stepFile (-1); };
    fileNext.onClick   = [this] { stepFile (1); };
    libraryBtn.onClick = [this] { lcd.showLibrary (! lcd.isShowingLibrary()); libraryBtn.setToggleState (lcd.isShowingLibrary(), dontSendNotification); };
    ejectBtn.onClick   = [this] { proc.clearSlot (currentFileSlot()); };

    lcd.onSelect      = [this] (int b) { selectBlock (b); };
    lcd.onToggle      = [this] (int b) { toggleBlock (b); };
    lcd.onLoadRequest = [this] (int b) { openFileChooser (b); };
    lcd.onFileDropped = [this] (int slot, const File& f) { proc.loadFile (slot, f); };
    lcd.onChooseFolder = [this] { chooseLibraryFolder(); };

    const std::array<int, 4> fsBlocks { td::Pedal, td::Amp, td::Delay, td::Reverb };
    for (int b : fsBlocks)
    {
        const auto& def = td::blocks()[(size_t) b];
        auto* param = proc.apvts.getParameter (def.onParam);
        auto fs = std::make_unique<Footswitch> (*param, def.shortName, Colour (def.colour));
        fs->onPress = [this, b] { selectBlock (b); };
        body.addAndMakeVisible (*fs);
        footswitches.push_back (std::move (fs));
    }

    proc.addChangeListener (this);
    selectBlock (td::Amp);

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
}

ToneDeckEditor::~ToneDeckEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
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

    // button row under the screen
    int x = 48;
    const int y = 342, h = 26;
    for (auto* b : { &presetPrev, &presetNext, &presetSave })
    {
        b->setBounds (x, y, b == &presetSave ? 64 : 88, h);
        x += b->getWidth() + 6;
    }
    x = baseW - 48;
    for (auto* b : { &normBtn, &ejectBtn, &libraryBtn, &fileNext, &filePrev, &loadBtn })
    {
        const int w = (b == &filePrev || b == &fileNext) ? 34 : (b == &normBtn ? 92 : 70);
        x -= w;
        b->setBounds (x, y, w, h);
        x -= 6;
    }

    // knobs
    const int knobY = 412, knobSize = 104;
    inputKnob.setBounds (40, knobY, knobSize, knobSize + 16);
    masterKnob.setBounds (baseW - 40 - knobSize, knobY, knobSize, knobSize + 16);
    const int span = baseW - 2 * 190;
    for (int i = 0; i < numKnobs; ++i)
    {
        const int cx = 190 + (int) ((i + 0.5f) * (float) span / (float) numKnobs);
        knobs[(size_t) i].setBounds (cx - knobSize / 2, knobY, knobSize, knobSize + 16);
    }

    // footswitches
    const int fsW = 110, fsY = 562, fsH = 86;
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

    // enclosure
    g.fillAll (Colour (0xff0b0c0e));
    auto encl = r.reduced (6.0f);
    g.setGradientFill (ColourGradient (colours::bodyTop, 0.0f, encl.getY(), colours::bodyBottom, 0.0f, encl.getBottom(), false));
    g.fillRoundedRectangle (encl, 22.0f);

    // brushed texture
    Random rng (42);
    for (int i = 0; i < 900; ++i)
    {
        const float y = encl.getY() + rng.nextFloat() * encl.getHeight();
        g.setColour (Colours::white.withAlpha (rng.nextFloat() * 0.018f));
        g.drawHorizontalLine ((int) y, encl.getX() + 18.0f, encl.getRight() - 18.0f);
    }
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (encl.reduced (1.0f), 22.0f, 1.5f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (encl, 22.0f, 2.0f);

    // screws
    for (auto pt : { Point<float> (26, 26), Point<float> (r.getRight() - 26, 26),
                     Point<float> (26, r.getBottom() - 26), Point<float> (r.getRight() - 26, r.getBottom() - 26) })
    {
        const auto s = Rectangle<float> (14.0f, 14.0f).withCentre (pt);
        g.setGradientFill (ColourGradient (Colour (0xff9aa0a8), s.getX(), s.getY(), Colour (0xff2b2e33), s.getRight(), s.getBottom(), false));
        g.fillEllipse (s);
        g.setColour (Colour (0xff1a1c20));
        g.drawLine (s.getX() + 3, s.getCentreY(), s.getRight() - 3, s.getCentreY(), 1.6f);
        g.drawLine (s.getCentreX(), s.getY() + 3, s.getCentreX(), s.getBottom() - 3, 1.6f);
    }

    // logo
    g.setColour (colours::silkscreen);
    g.setFont (uiFont (30.0f).withExtraKerningFactor (0.18f));
    g.drawText ("TONEDECK", 48, 22, 300, 40, Justification::centredLeft);
    g.setColour (colours::accent);
    g.setFont (uiFont (11.0f).withExtraKerningFactor (0.3f));
    g.drawText ("NEURAL AMP & IR MULTI-FX", 262, 34, 260, 20, Justification::centredLeft);
    g.setColour (colours::silkDim);
    g.setFont (uiFont (11.0f, false));
    g.drawText ("NAM  |  NAM A2  |  AIDA-X  |  IR", baseW - 348, 34, 300, 20, Justification::centredRight);

    // LCD bezel
    const auto lcdB = owner.lcd.getBounds().toFloat();
    g.setColour (Colours::black);
    g.fillRoundedRectangle (lcdB.expanded (9.0f), 13.0f);
    g.setColour (Colour (0xff3b3f47));
    g.drawRoundedRectangle (lcdB.expanded (9.0f), 13.0f, 1.2f);

    // knob labels
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

    // section rules
    g.setColour (colours::panelLine.withAlpha (0.6f));
    g.drawLine (170.0f, 392.0f, 170.0f, 540.0f, 1.0f);
    g.drawLine ((float) baseW - 170.0f, 392.0f, (float) baseW - 170.0f, 540.0f, 1.0f);
    g.drawLine (40.0f, 552.0f, (float) baseW - 40.0f, 552.0f, 1.0f);

    // selected block tag above the knob row
    const auto& def = td::blocks()[(size_t) owner.lcd.getSelectedBlock()];
    g.setColour (Colour (def.colour));
    g.setFont (uiFont (11.0f).withExtraKerningFactor (0.2f));
    g.drawText (String ("EDIT  >  ") + def.longName, 190, 376, baseW - 380, 14, Justification::centred);
}

//==============================================================================
int ToneDeckEditor::currentFileSlot() const
{
    const int b = lcd.getSelectedBlock();
    return b == td::Cab ? td::Cab : (b == td::Pedal ? td::Pedal : td::Amp);
}

void ToneDeckEditor::selectBlock (int b)
{
    if (lcd.getSelectedBlock() != b || lcd.isShowingLibrary())
    {
        const bool keepLibrary = lcd.isShowingLibrary() && td::blocks()[(size_t) b].slot != td::SlotKind::None;
        lcd.setSelectedBlock (b);
        if (keepLibrary)
            lcd.showLibrary (true);
    }
    libraryBtn.setToggleState (lcd.isShowingLibrary(), dontSendNotification);
    refreshKnobs();
    body.repaint();
}

void ToneDeckEditor::toggleBlock (int b)
{
    if (auto* param = proc.apvts.getParameter (td::blocks()[(size_t) b].onParam))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        param->endChangeGesture();
    }
}

void ToneDeckEditor::refreshKnobs()
{
    const int b = lcd.getSelectedBlock();
    const auto& def = td::blocks()[(size_t) b];
    const auto& st = proc.getSlotStatus (b);

    std::vector<const td::KnobDef*> visible;
    for (const auto& k : def.knobs)
        if (knobVisible (k, st))
            visible.push_back (&k);

    for (int i = 0; i < numKnobs; ++i)
    {
        auto& knob = knobs[(size_t) i];
        knobAttachments[(size_t) i].reset();
        if (i < (int) visible.size())
        {
            knob.setEnabled (true);
            knob.setColour (Slider::rotarySliderFillColourId, Colour (def.colour));
            knobAttachments[(size_t) i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, visible[(size_t) i]->paramId, knob);
            knobLabels[(size_t) i] = visible[(size_t) i]->label;
            knob.setTextBoxStyle (Slider::TextBoxBelow, true, 90, 16);
        }
        else
        {
            knob.setEnabled (false);
            knob.setRange (0.0, 1.0);
            knob.setValue (0.0, dontSendNotification);
            knob.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            knobLabels[(size_t) i] = "-";
        }
    }

    const bool fileBlock = def.slot != td::SlotKind::None;
    for (auto* btn : { &loadBtn, &filePrev, &fileNext, &ejectBtn })
        btn->setEnabled (fileBlock || lcd.isShowingLibrary());
    body.repaint();
}

void ToneDeckEditor::openFileChooser (int slot)
{
    const auto& st = proc.getSlotStatus (slot);
    auto start = st.file.existsAsFile() ? st.file.getParentDirectory() : proc.getLibraryFolder();
    if (! start.isDirectory())
        start = File::getSpecialLocation (File::userDocumentsDirectory);

    const String title = slot == td::Cab ? "Load impulse response" : "Load NAM / AIDA-X model";
    chooser = std::make_unique<FileChooser> (title, start, ToneDeckProcessor::wildcardForSlot (slot));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this, slot] (const FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  proc.loadFile (slot, f);
                          });
}

void ToneDeckEditor::stepFile (int delta)
{
    const int slot = currentFileSlot();
    const auto current = proc.getSlotStatus (slot).file;
    const auto dir = current.existsAsFile() ? current.getParentDirectory() : proc.getLibraryFolder();
    auto files = dir.findChildFiles (File::findFiles, false, ToneDeckProcessor::wildcardForSlot (slot));
    if (files.isEmpty())
        return;
    files.sort();
    int idx = files.indexOf (current);
    idx = idx < 0 ? (delta > 0 ? 0 : files.size() - 1) : (idx + delta + files.size()) % files.size();
    proc.loadFile (slot, files[idx]);
}

void ToneDeckEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<FileChooser> ("Choose your model / IR library folder", proc.getLibraryFolder());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this] (const FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.isDirectory())
                              {
                                  proc.setLibraryFolder (f);
                                  lcd.refreshLibrary();
                                  lcd.repaint();
                              }
                          });
}

void ToneDeckEditor::savePresetDialog()
{
    auto* w = new AlertWindow ("Save preset", "Preset name:", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getCurrentPresetName() == "INIT" ? String ("My Tone") : proc.getCurrentPresetName());
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
            proc.savePreset (w->getTextEditorContents ("name"));
    }), true);
}
