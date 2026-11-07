#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace td::ui
{
// Which of a unit's parameters are shown (model blocks hide unsupported ones).
inline bool paramVisible (const UnitState& st, int index)
{
    const auto& spec = blockInfo (st.type).params[(size_t) index];
    if (! spec.used()) return false;
    if (st.type == BlockType::Amp)
    {
        const auto& f = st.files[0];
        if (index == 2) return f.loaded && f.conditioningParams >= 1;
        if (index == 3) return f.loaded && f.conditioningParams >= 2;
        if (index == 4) return f.loaded && f.slimmable;
    }
    if (st.type == BlockType::Cab && index == 2)
        return st.files[1].loaded && st.files[2].loaded;
    return true;
}

constexpr int kMixerSelection = -2;

// The colour screen: 2 x 8 block grid (path A + parallel path B), block details,
// parameter read-outs, library browser, tuner and looper pages.
class LcdDisplay final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit LcdDisplay (ToneDeckProcessor&);

    void setSelection (int cell) { selected = cell; libraryMode = false; repaint(); }
    int getSelection() const     { return selected; }
    int selectedUnit() const     { return selected >= 0 ? proc.unitAtCell (selected) : -1; }

    void showLibrary (bool show);
    bool isShowingLibrary() const { return libraryMode; }
    void refreshLibrary();

    std::function<void (int cell)> onSelect, onAddRequest, onContextMenu;
    std::function<void (int from, int to)> onMove;
    std::function<void (int unit)> onToggle;
    std::function<void (int unit, int which)> onLoadRequest;
    std::function<void (int cell, const juce::File&)> onFileDropped;
    std::function<void (const juce::File&)> onLibraryPick;
    std::function<void()> onChooseFolder;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; hoverCell = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void timerCallback() override;

    juce::Rectangle<float> headerArea() const;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> detailArea() const;
    juce::Rectangle<float> cellRect (int cell) const;
    juce::Rectangle<float> mixerNode() const;
    juce::Rectangle<float> fileBox (int which) const;   // 0 model, 1/2 IRs
    juce::Rectangle<float> folderButtonArea() const;
    int cellAt (juce::Point<float>) const;
    int libraryRowAt (juce::Point<float>) const;
    bool selectionWantsIR() const;

    void drawHeader (juce::Graphics&);
    void drawGrid (juce::Graphics&);
    void drawDetail (juce::Graphics&);
    void drawMixer (juce::Graphics&);
    void drawLibrary (juce::Graphics&);
    void drawTuner (juce::Graphics&);
    void drawLooper (juce::Graphics&);
    void drawFileBox (juce::Graphics&, juce::Rectangle<float>, const SlotStatus&, const juce::String& emptyText, juce::Colour);
    void drawParams (juce::Graphics&, juce::Rectangle<float>, int unit);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);

    ToneDeckProcessor& proc;
    int selected = 2;
    bool libraryMode = false, dragOver = false;
    juce::Array<juce::File> libFiles;
    int libScroll = 0, hoverRow = -1, hoverCell = -1;
    int dragFrom = -1;
    bool draggingBlock = false;
    juce::Point<float> dragPos;
    float tunerHz = 0.0f, tunerSmoothed = 0.0f;
    int tunerHold = 0;
    static constexpr float rowH = 20.0f;
};
} // namespace td::ui
