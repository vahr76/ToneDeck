#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace td::ui
{
namespace colours
{
    const juce::Colour bodyTop    { 0xff33363d };
    const juce::Colour bodyBottom { 0xff18191d };
    const juce::Colour panelLine  { 0xff4a4e57 };
    const juce::Colour silkscreen { 0xffd8dce3 };
    const juce::Colour silkDim    { 0xff8a909b };
    const juce::Colour lcdBg      { 0xff070b14 };
    const juce::Colour lcdText    { 0xffe9f1ff };
    const juce::Colour lcdDim     { 0xff5d6b82 };
    const juce::Colour accent     { 0xff34d1ff };
}

juce::Font uiFont (float height, bool bold = true);
juce::Font lcdFont (float height, bool bold = false);

class PedalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (11.0f); }

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return uiFont (14.0f, false); }
};
} // namespace td::ui
