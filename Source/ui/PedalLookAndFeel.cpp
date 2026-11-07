#include "PedalLookAndFeel.h"

namespace td::ui
{
juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font lcdFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

PedalLookAndFeel::PedalLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff15171c));
    setColour (juce::PopupMenu::textColourId, colours::silkscreen);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, colours::silkscreen);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1c1e23));
    setColour (juce::AlertWindow::textColourId, colours::silkscreen);
    setColour (juce::TextEditor::backgroundColourId, colours::lcdBg);
    setColour (juce::TextEditor::textColourId, colours::lcdText);
    setColour (juce::TextEditor::outlineColourId, colours::panelLine);
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    using namespace juce;
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size   = jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre  = bounds.getCentre();
    const float radius = size * 0.5f;
    const auto arcCol  = s.findColour (Slider::rotarySliderFillColourId);
    const bool enabled = s.isEnabled();
    const float angle  = startAngle + pos * (endAngle - startAngle);

    if (! enabled)
        g.beginTransparencyLayer (0.35f);

    // scale ticks printed on the panel
    g.setColour (colours::silkDim.withAlpha (enabled ? 0.8f : 0.25f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto p1 = centre.getPointOnCircumference (radius * 0.98f, a);
        const auto p2 = centre.getPointOnCircumference (radius * (i % 5 == 0 ? 0.84f : 0.89f), a);
        g.drawLine ({ p1, p2 }, i % 5 == 0 ? 1.6f : 1.0f);
    }

    // value arc (LED ring)
    const float arcR = radius * 0.80f;
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    if (enabled)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (arcCol.withAlpha (0.25f));
        g.strokePath (value, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (arcCol);
        g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // knob body
    const float kr = radius * 0.66f;
    const auto knob = Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (centre);

    g.setColour (Colours::black.withAlpha (0.6f));                       // drop shadow
    g.fillEllipse (knob.translated (0.0f, kr * 0.12f).expanded (kr * 0.06f));

    // skirt
    ColourGradient skirt (Colour (0xff4b4f57), knob.getX(), knob.getY(),
                          Colour (0xff121316), knob.getX(), knob.getBottom(), false);
    g.setGradientFill (skirt);
    g.fillEllipse (knob);

    // knurled grip
    const float gr = kr * 0.86f;
    g.setColour (Colour (0xff0d0e10));
    g.fillEllipse (Rectangle<float> (gr * 2.0f, gr * 2.0f).withCentre (centre));
    g.setColour (Colour (0xff2a2c31));
    for (int i = 0; i < 36; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / 36.0f + angle;
        g.drawLine ({ centre.getPointOnCircumference (gr * 0.80f, a), centre.getPointOnCircumference (gr * 0.98f, a) }, 1.2f);
    }

    // top cap
    const float cr = gr * 0.74f;
    const auto cap = Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (centre);
    ColourGradient capGrad (Colour (0xff3d4048), cap.getX(), cap.getY(),
                            Colour (0xff141518), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (capGrad);
    g.fillEllipse (cap);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // pointer
    const auto pa = centre.getPointOnCircumference (cr * 0.25f, angle);
    const auto pb = centre.getPointOnCircumference (gr * 0.96f, angle);
    g.setColour (enabled ? Colours::white : Colours::grey.withAlpha (0.4f));
    g.drawLine ({ pa, pb }, 2.6f);

    if (! enabled)
        g.endTransparencyLayer();
}

void PedalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                             bool highlighted, bool down)
{
    using namespace juce;
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool lit = b.getToggleState();

    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 5.0f);

    ColourGradient grad (Colour (down ? 0xff1d1f24 : 0xff3a3d44), r.getX(), r.getY(),
                         Colour (down ? 0xff2a2d33 : 0xff1f2126), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 5.0f);

    g.setColour (lit ? colours::accent : (highlighted ? colours::silkDim : Colour (0xff50545d)));
    g.drawRoundedRectangle (r, 5.0f, lit ? 1.5f : 1.0f);
}

void PedalLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colours::accent : colours::silkscreen.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

juce::Label* PedalLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::textColourId, colours::silkscreen);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setFont (uiFont (11.0f, false));
    return l;
}
} // namespace td::ui
