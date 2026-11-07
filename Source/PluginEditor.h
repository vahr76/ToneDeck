#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/LcdDisplay.h"
#include "ui/PedalLookAndFeel.h"

//==============================================================================
// Chrome stomp switch with a status LED, bound to a block's on/off parameter.
class Footswitch final : public juce::Component
{
public:
    Footswitch (juce::RangedAudioParameter& param, juce::String label, juce::Colour ledColour);

    std::function<void()> onPress;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }

private:
    juce::ParameterAttachment attachment;
    juce::String text;
    juce::Colour led;
    bool state = false, pressed = false;
};

//==============================================================================
class ToneDeckEditor final : public juce::AudioProcessorEditor,
                             private juce::ChangeListener
{
public:
    explicit ToneDeckEditor (ToneDeckProcessor&);
    ~ToneDeckEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseW = 980, baseH = 660;

private:
    struct Body final : public juce::Component
    {
        explicit Body (ToneDeckEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        ToneDeckEditor& owner;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshKnobs(); body.repaint(); }
    void selectBlock (int b);
    void toggleBlock (int b);
    void refreshKnobs();
    void openFileChooser (int slot);
    void stepFile (int delta);
    void chooseLibraryFolder();
    void savePresetDialog();
    void layoutBody();
    void showTone3000Menu();
    void askClientId (std::function<void()> then);
    void handleTone3000Result (const juce::Array<Tone3000Client::DownloadedModel>& models, const juce::String& error, int slot);
    void showToneList (const juce::String& listName, const juce::String& title);
    int currentFileSlot() const;

    ToneDeckProcessor& proc;
    td::ui::PedalLookAndFeel lnf;
    Body body { *this };
    td::ui::LcdDisplay lcd;

    static constexpr int numKnobs = 5;
    std::array<juce::Slider, numKnobs> knobs;
    std::array<juce::String, numKnobs> knobLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, numKnobs> knobAttachments;
    juce::Slider inputKnob, masterKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAtt, masterAtt;

    juce::TextButton presetPrev { "< PRESET" }, presetNext { "PRESET >" }, presetSave { "SAVE" },
                     loadBtn { "LOAD" }, filePrev { "<" }, fileNext { ">" }, libraryBtn { "LIBRARY" },
                     ejectBtn { "EJECT" }, normBtn { "NORMALIZE" }, t3kBtn { "TONE3000" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normAtt;

    std::vector<std::unique_ptr<Footswitch>> footswitches;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<ToneDeckEditor> safeThis { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneDeckEditor)
};
