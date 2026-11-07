#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace td::ui
{
inline bool knobVisible (const KnobDef& k, const SlotStatus& st)
{
    switch (k.visibility)
    {
        case KnobDef::Cond1:     return st.loaded && st.conditioningParams >= 1;
        case KnobDef::Cond2:     return st.loaded && st.conditioningParams >= 2;
        case KnobDef::Slimmable: return st.loaded && st.slimmable;
        case KnobDef::Always:    break;
    }
    return true;
}

// The colour "screen" in the middle of the pedal: signal chain, block details,
// parameter read-outs and the model / IR library browser.
class LcdDisplay final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit LcdDisplay (ToneDeckProcessor&);

    void setSelectedBlock (int b) { selected = b; libraryMode = false; repaint(); }
    int getSelectedBlock() const  { return selected; }

    void showLibrary (bool show);
    bool isShowingLibrary() const { return libraryMode; }
    void refreshLibrary();

    std::function<void (int)> onSelect, onToggle, onLoadRequest;
    std::function<void (int, const juce::File&)> onFileDropped;
    std::function<void()> onChooseFolder;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void timerCallback() override { repaint(); }

    juce::Rectangle<float> headerArea() const;
    juce::Rectangle<float> chainArea() const;
    juce::Rectangle<float> detailArea() const;
    juce::Rectangle<float> blockRect (int b) const;
    juce::Rectangle<float> fileArea() const;
    juce::Rectangle<float> folderButtonArea() const;
    int blockAt (juce::Point<float>) const;
    int libraryRowAt (juce::Point<float>) const;
    int librarySlot() const { return selected == Cab ? Cab : (selected == Pedal ? Pedal : Amp); }

    void drawHeader (juce::Graphics&);
    void drawChain (juce::Graphics&);
    void drawDetail (juce::Graphics&);
    void drawLibrary (juce::Graphics&);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);

    ToneDeckProcessor& proc;
    int selected = Amp;
    bool libraryMode = false, dragOver = false;
    juce::Array<juce::File> libFiles;
    int libScroll = 0, hoverRow = -1;
    static constexpr float rowH = 21.0f;
};
} // namespace td::ui
