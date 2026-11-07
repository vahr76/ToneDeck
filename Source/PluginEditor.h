#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/LcdDisplay.h"
#include "ui/PedalLookAndFeel.h"

//==============================================================================
// Rotary knob with a right-click menu for MIDI learn.
class LearnableKnob final : public juce::Slider
{
public:
    LearnableKnob() = default;
    std::function<int()> getMidiTarget;   // parameter index or ToneDeckProcessor::kNoTarget
    ToneDeckProcessor* proc = nullptr;
    void mouseDown (const juce::MouseEvent&) override;
};

//==============================================================================
// Chrome stomp switch; label / LED come from the processor's current footswitch page.
class Footswitch final : public juce::Component
{
public:
    Footswitch (ToneDeckProcessor& p, int index) : proc (p), fs (index) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void (int fs)> onRightClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }
private:
    ToneDeckProcessor& proc;
    const int fs;
    bool pressed = false;
};

//==============================================================================
class ToneDeckEditor final : public juce::AudioProcessorEditor,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit ToneDeckEditor (ToneDeckProcessor&);
    ~ToneDeckEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseW = 980, baseH = 660;
    static constexpr int numKnobs = 6;

    td::ui::LcdDisplay& getLcd() { return lcd; }   // for tests

private:
    struct Body final : public juce::Component
    {
        explicit Body (ToneDeckEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        ToneDeckEditor& owner;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshKnobs(); refreshButtons(); body.repaint(); }
    void timerCallback() override;
    void onCellSelected (int cell);
    void refreshKnobs();
    void refreshButtons();
    void layoutBody();

    void showAddMenu (int cell);
    void showBlockMenu (int cell);
    void showFootswitchMenu (int fs);
    void addTypeItems (juce::PopupMenu& menu, int idBase, td::BlockType current);

    void openFileChooser (int unit, int which);
    void stepFile (int delta);
    void chooseLibraryFolder();
    void savePresetDialog();
    void routeFile (int cell, const juce::File& f);
    int unitForFileKind (td::SlotKind kind);   // finds or creates an Amp / Cab unit

    void showTone3000Menu();
    void askClientId (std::function<void()> then);
    void handleTone3000Result (const juce::Array<Tone3000Client::DownloadedModel>& models, const juce::String& error);
    void showToneList (const juce::String& listName, const juce::String& title);

    ToneDeckProcessor& proc;
    td::ui::PedalLookAndFeel lnf;
    Body body { *this };
    td::ui::LcdDisplay lcd;

    std::array<LearnableKnob, numKnobs> knobs;
    std::array<juce::String, numKnobs> knobLabels;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, numKnobs> knobAttachments;
    std::array<juce::RangedAudioParameter*, numKnobs> knobParams {};
    LearnableKnob inputKnob, masterKnob;
    std::unique_ptr<juce::SliderParameterAttachment> inputAtt, masterAtt;
    int boundUnit = -999, boundSignature = -1;

    juce::TextButton presetPrev { "< PST" }, presetNext { "PST >" }, presetSave { "SAVE" }, t3kBtn { "TONE3000" },
                     tunerBtn { "TUNER" }, looperBtn { "LOOPER" }, pageBtn { "FS: STOMP" },
                     loadBtn { "LOAD" }, filePrev { "<" }, fileNext { ">" }, libraryBtn { "LIBRARY" }, normBtn { "NORM" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normAtt;

    std::vector<std::unique_ptr<Footswitch>> footswitches;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<ToneDeckEditor> safeThis { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneDeckEditor)
};
