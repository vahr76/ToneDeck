#include "LcdDisplay.h"
#include "PedalLookAndFeel.h"

namespace td::ui
{
using namespace juce;

namespace
{
const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void drawBlockIcon (Graphics& g, BlockType t, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const auto cx = r.getCentreX(), cy = r.getCentreY();
    const float s = jmin (r.getWidth(), r.getHeight());
    PathStrokeType stroke (1.6f, PathStrokeType::curved, PathStrokeType::rounded);

    switch (t)
    {
        case BlockType::Gate:
        {
            Path p; p.startNewSubPath (r.getX(), cy);
            for (int i = 0; i <= 20; ++i)
            {
                const float k = (float) i / 20.0f;
                p.lineTo (r.getX() + k * r.getWidth(), cy + std::sin (k * 22.0f) * (k < 0.55f ? s * 0.38f : 0.0f));
            }
            g.strokePath (p, stroke); break;
        }
        case BlockType::Compressor:
        {
            for (int i = 0; i < 5; ++i)
            {
                const float h = s * (i == 2 ? 0.9f : 0.5f);
                g.fillRoundedRectangle (Rectangle<float> (s * 0.12f, h).withCentre ({ r.getX() + r.getWidth() * (0.15f + 0.175f * (float) i), cy }), 1.5f);
            }
            break;
        }
        case BlockType::Boost: case BlockType::Overdrive: case BlockType::Fuzz:
        {
            auto box = Rectangle<float> (s * 0.62f, s * 0.95f).withCentre ({ cx, cy });
            g.drawRoundedRectangle (box, 2.5f, 1.6f);
            for (int i = 0; i < (t == BlockType::Boost ? 1 : 2); ++i)
                g.fillEllipse (Rectangle<float> (s * 0.14f, s * 0.14f).withCentre ({ box.getX() + box.getWidth() * (t == BlockType::Boost ? 0.5f : 0.3f + 0.4f * (float) i), box.getY() + s * 0.2f }));
            g.drawEllipse (Rectangle<float> (s * 0.24f, s * 0.24f).withCentre ({ cx, box.getBottom() - s * 0.24f }), 1.6f);
            break;
        }
        case BlockType::Amp:
        {
            auto box = Rectangle<float> (s * 1.3f, s * 0.82f).withCentre ({ cx, cy });
            g.drawRoundedRectangle (box, 2.5f, 1.6f);
            auto top = box.removeFromTop (box.getHeight() * 0.32f);
            for (int i = 0; i < 4; ++i)
                g.fillEllipse (Rectangle<float> (s * 0.09f, s * 0.09f).withCentre ({ top.getX() + top.getWidth() * (0.2f + 0.2f * (float) i), top.getCentreY() }));
            for (int i = 1; i < 4; ++i)
                g.drawHorizontalLine ((int) (box.getY() + box.getHeight() * (float) i / 4.0f), box.getX() + 3.0f, box.getRight() - 3.0f);
            break;
        }
        case BlockType::Cab:
        {
            auto box = Rectangle<float> (s * 0.95f, s * 0.95f).withCentre ({ cx, cy });
            g.drawRoundedRectangle (box, 2.5f, 1.6f);
            g.drawEllipse (box.reduced (s * 0.14f), 1.6f);
            g.fillEllipse (Rectangle<float> (s * 0.18f, s * 0.18f).withCentre ({ cx, cy }));
            break;
        }
        case BlockType::ToneEq: case BlockType::ParamEq:
        {
            const float heights[] = { 0.3f, 0.7f, 0.45f, 0.6f };
            for (int i = 0; i < 4; ++i)
            {
                const float x = r.getX() + r.getWidth() * (0.2f + 0.2f * (float) i);
                g.drawLine (x, r.getY() + 2.0f, x, r.getBottom() - 2.0f, 1.2f);
                g.fillRoundedRectangle (Rectangle<float> (s * 0.2f, s * 0.12f).withCentre ({ x, r.getY() + r.getHeight() * heights[i] }), 1.5f);
            }
            break;
        }
        case BlockType::Chorus: case BlockType::Flanger: case BlockType::Phaser: case BlockType::Tremolo:
        {
            for (int k = 0; k < (t == BlockType::Tremolo ? 1 : 2); ++k)
            {
                Path p;
                for (int i = 0; i <= 24; ++i)
                {
                    const float u = (float) i / 24.0f;
                    const float y = cy + std::sin (u * MathConstants<float>::twoPi * (t == BlockType::Flanger ? 2.0f : 1.5f) + (float) k * 1.2f) * s * 0.32f;
                    if (i == 0) p.startNewSubPath (r.getX() + u * r.getWidth(), y); else p.lineTo (r.getX() + u * r.getWidth(), y);
                }
                g.setColour (c.withAlpha (k == 0 ? 1.0f : 0.5f));
                g.strokePath (p, stroke);
            }
            break;
        }
        case BlockType::Wah:
        {
            Path p;
            p.addQuadrilateral (cx - s * 0.6f, cy + s * 0.35f, cx + s * 0.6f, cy + s * 0.35f, cx + s * 0.5f, cy - s * 0.35f, cx - s * 0.45f, cy - s * 0.05f);
            g.strokePath (p, stroke);
            break;
        }
        case BlockType::Delay:
        {
            for (int i = 0; i < 4; ++i)
            {
                const float d = s * (0.34f - 0.06f * (float) i);
                g.setColour (c.withAlpha (1.0f - 0.22f * (float) i));
                g.fillEllipse (Rectangle<float> (d, d).withCentre ({ r.getX() + r.getWidth() * (0.18f + 0.22f * (float) i), cy }));
            }
            break;
        }
        case BlockType::Reverb:
        {
            for (int i = 0; i < 3; ++i)
            {
                Path p; const float rad = s * (0.2f + 0.17f * (float) i);
                p.addCentredArc (r.getX() + s * 0.15f, cy, rad, rad, 0.0f, 0.55f, MathConstants<float>::pi - 0.55f, true);
                g.setColour (c.withAlpha (1.0f - 0.25f * (float) i));
                g.strokePath (p, stroke);
            }
            break;
        }
        case BlockType::Volume:
        {
            Path p;
            p.addTriangle (cx - s * 0.6f, cy + s * 0.35f, cx + s * 0.6f, cy + s * 0.35f, cx + s * 0.6f, cy - s * 0.35f);
            g.strokePath (p, stroke);
            break;
        }
        case BlockType::Empty: case BlockType::NumTypes: break;
    }
}
} // namespace

//==============================================================================
LcdDisplay::LcdDisplay (ToneDeckProcessor& p) : proc (p) { startTimerHz (30); }

void LcdDisplay::timerCallback()
{
    if (proc.isTunerOn())
    {
        if (++tunerHold % 2 == 0)
        {
            const float hz = proc.detectPitch();
            if (hz > 25.0f && hz < 1500.0f)
            {
                tunerSmoothed = tunerSmoothed > 0.0f && std::abs (std::log2 (hz / tunerSmoothed)) < 0.05f ? tunerSmoothed * 0.6f + hz * 0.4f : hz;
                tunerHz = tunerSmoothed;
            }
            else
            {
                tunerHz = 0.0f;
            }
        }
    }
    repaint();
}

Rectangle<float> LcdDisplay::headerArea() const { return getLocalBounds().toFloat().reduced (14.0f, 8.0f).removeFromTop (24.0f); }
Rectangle<float> LcdDisplay::gridArea() const   { return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (40.0f).withHeight (106.0f); }
Rectangle<float> LcdDisplay::detailArea() const { return getLocalBounds().toFloat().reduced (14.0f, 8.0f).withTrimmedTop (146.0f); }

Rectangle<float> LcdDisplay::cellRect (int cell) const
{
    auto area = gridArea().reduced (30.0f, 0.0f);
    const int row = cell / kGridCols, col = cell % kGridCols;
    const float slot = area.getWidth() / (float) kGridCols;
    const float w = slot * 0.84f;
    const float h = row == 0 ? 46.0f : 40.0f;
    const float y = row == 0 ? area.getY() + 6.0f : area.getY() + 60.0f;
    return { area.getX() + slot * (float) col + (slot - w) * 0.5f, y, w, h };
}

Rectangle<float> LcdDisplay::mixerNode() const
{
    int first = 0, last = 0;
    if (! proc.getLayout().parallelRange (first, last)) return {};
    const auto r = cellRect (Layout::index (0, last));
    const float slot = gridArea().reduced (30.0f, 0.0f).getWidth() / (float) kGridCols;
    return Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getRight() + (slot - r.getWidth()) * 0.5f, r.getCentreY() });
}

Rectangle<float> LcdDisplay::fileBox (int which) const
{
    auto d = detailArea();
    auto left = d.removeFromLeft (d.getWidth() * 0.5f).withTrimmedTop (24.0f).withHeight (46.0f);
    if (which == 0) return left;
    auto half = left.getWidth() * 0.5f - 3.0f;
    return which == 1 ? left.withWidth (half) : left.withTrimmedLeft (half + 6.0f);
}

Rectangle<float> LcdDisplay::folderButtonArea() const { return detailArea().removeFromTop (20.0f).removeFromRight (120.0f); }

int LcdDisplay::cellAt (Point<float> pt) const
{
    for (int c = 0; c < kGridCells; ++c)
        if (cellRect (c).expanded (4.0f, 3.0f).contains (pt)) return c;
    return -1;
}

int LcdDisplay::libraryRowAt (Point<float> pt) const
{
    auto list = detailArea().withTrimmedTop (24.0f);
    if (! list.contains (pt)) return -1;
    const int row = (int) ((pt.y - list.getY()) / rowH) + libScroll;
    return row < libFiles.size() ? row : -1;
}

bool LcdDisplay::selectionWantsIR() const
{
    const int u = selectedUnit();
    return u >= 0 && proc.getUnit (u).type == BlockType::Cab;
}

//==============================================================================
void LcdDisplay::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff0d1424), r.getX(), r.getY(), Colour (0xff04060b), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::white.withAlpha (0.018f));
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);

    drawHeader (g);
    if (proc.isTunerOn())
    {
        drawTuner (g);
    }
    else
    {
        drawGrid (g);
        if (proc.getFsPage() == FsPage::Looper) drawLooper (g);
        else if (libraryMode)                  drawLibrary (g);
        else if (selected == kMixerSelection)  drawMixer (g);
        else                                   drawDetail (g);
    }

    const auto msg = proc.getOnlineMessage();
    if (msg.isNotEmpty())
    {
        auto bar = r.reduced (10.0f, 6.0f).removeFromBottom (22.0f);
        g.setColour (Colour (0xf0101a2e));
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (bar, 5.0f, 1.0f);
        g.setFont (lcdFont (12.0f, true));
        g.drawText (msg, bar.reduced (10.0f, 0.0f), Justification::centredLeft, true);
    }

    Path glare;
    glare.addRoundedRectangle (r.withHeight (r.getHeight() * 0.4f), 8.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.04f), 0.0f, r.getY(), Colours::transparentWhite, 0.0f, r.getY() + r.getHeight() * 0.4f, false));
    g.fillPath (glare);

    if (dragOver)
    {
        g.setColour (colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (r.reduced (3.0f), 8.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (r.reduced (3.0f), 8.0f, 2.0f);
        g.setFont (lcdFont (18.0f, true));
        g.drawText ("SOLTA SOBRE UN BLOQUE  (.NAM / .JSON / .WAV)", r, Justification::centred);
    }
}

void LcdDisplay::drawMeter (Graphics& g, Rectangle<float> area, float level, const String& label)
{
    g.setFont (lcdFont (11.0f, true));
    g.setColour (colours::lcdDim);
    g.drawText (label, area.removeFromLeft (28.0f), Justification::centredLeft);
    const int segs = 14;
    const float norm = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
    const float sw = area.getWidth() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const Colour c = i >= segs - 2 ? Colour (0xffff4d4d) : (i >= segs - 4 ? Colour (0xffffc24d) : Colour (0xff3ddc84));
        g.setColour ((float) i / (float) segs < norm ? c : c.withAlpha (0.12f));
        g.fillRoundedRectangle (Rectangle<float> (area.getX() + sw * (float) i + 1.0f, area.getCentreY() - 5.0f, sw - 2.0f, 10.0f), 1.5f);
    }
}

void LcdDisplay::drawHeader (Graphics& g)
{
    auto h = headerArea();
    auto tag = h.removeFromLeft (40.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (tag.reduced (0.0f, 2.0f), 4.0f);
    g.setColour (colours::lcdBg);
    g.setFont (lcdFont (13.0f, true));
    g.drawText ("PST", tag, Justification::centred);
    h.removeFromLeft (10.0f);
    g.setColour (colours::lcdText);
    g.setFont (lcdFont (19.0f, true));
    g.drawText (proc.getCurrentPresetName().toUpperCase(), h.removeFromLeft (300.0f), Justification::centredLeft, true);

    // snapshot indicator
    auto snap = h.removeFromLeft (120.0f);
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        auto dot = Rectangle<float> (22.0f, 16.0f).withPosition (snap.getX() + (float) i * 26.0f, snap.getCentreY() - 8.0f);
        const bool cur = i == proc.getCurrentSnapshot();
        g.setColour (cur ? Colour (0xffffd84d) : Colour (0xff1a2232));
        g.fillRoundedRectangle (dot, 3.0f);
        g.setColour (cur ? colours::lcdBg : colours::lcdDim);
        g.setFont (lcdFont (11.0f, true));
        g.drawText (String (i + 1), dot, Justification::centred);
    }

    auto meters = h.removeFromRight (340.0f);
    drawMeter (g, meters.removeFromLeft (165.0f), proc.inputMeter.level.load(), "IN");
    meters.removeFromLeft (10.0f);
    drawMeter (g, meters, proc.outputMeter.level.load(), "OUT");
}

void LcdDisplay::drawGrid (Graphics& g)
{
    const auto& layout = proc.getLayout();
    const auto grid = gridArea();
    const float lineY = cellRect (0).getCentreY();
    int first = -1, last = -1;
    const bool parallel = layout.parallelRange (first, last);

    // path lines
    g.setColour (colours::lcdDim.withAlpha (0.7f));
    g.drawLine (grid.getX() + 10.0f, lineY, grid.getRight() - 10.0f, lineY, 2.0f);
    g.setFont (lcdFont (10.0f, true));
    for (auto [x, txt] : { std::pair<float, const char*> { grid.getX() + 10.0f, "IN" }, { grid.getRight() - 10.0f, "OUT" } })
    {
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, lineY }));
        g.drawText (txt, Rectangle<float> (40.0f, 12.0f).withCentre ({ x, lineY + 14.0f }), Justification::centred);
    }
    g.setColour (colours::lcdDim.withAlpha (0.35f));
    g.drawText ("B", Rectangle<float> (20.0f, 14.0f).withCentre ({ grid.getX() + 10.0f, cellRect (kGridCols).getCentreY() }), Justification::centred);

    if (parallel)
    {
        const float slot = grid.reduced (30.0f, 0.0f).getWidth() / (float) kGridCols;
        const auto a = cellRect (Layout::index (1, first)), b = cellRect (Layout::index (1, last));
        const float xs = a.getX() - (slot - a.getWidth()) * 0.5f, xe = b.getRight() + (slot - b.getWidth()) * 0.5f;
        const float yB = a.getCentreY();
        g.setColour (Colour (0xff34d1ff).withAlpha (0.7f));
        Path p;
        p.startNewSubPath (xs, lineY); p.lineTo (xs, yB); p.lineTo (xe, yB); p.lineTo (xe, lineY);
        g.strokePath (p, PathStrokeType (2.0f));
        auto node = mixerNode();
        g.setColour (selected == kMixerSelection ? Colours::white : Colour (0xff34d1ff));
        g.fillEllipse (node);
        g.setColour (colours::lcdBg);
        g.setFont (lcdFont (10.0f, true));
        g.drawText ("M", node, Justification::centred);
    }

    for (int c = 0; c < kGridCells; ++c)
    {
        const auto rect = cellRect (c);
        const int u = layout.cell[(size_t) c];
        const bool sel = c == selected;
        const bool beingDragged = draggingBlock && c == dragFrom;

        if (u < 0)
        {
            g.setColour (Colour (0xff121a2a).withAlpha (c == hoverCell || sel ? 1.0f : 0.55f));
            g.fillRoundedRectangle (rect, 6.0f);
            const float dash[] = { 3.0f, 3.0f };
            Path border; border.addRoundedRectangle (rect, 6.0f);
            Path dashed; PathStrokeType (1.0f).createDashedStroke (dashed, border, dash, 2);
            g.setColour (sel ? Colours::white.withAlpha (0.8f) : colours::lcdDim.withAlpha (c == hoverCell ? 0.8f : 0.3f));
            g.fillPath (dashed);
            if (c == hoverCell || sel)
            {
                g.setFont (lcdFont (18.0f, true));
                g.drawText ("+", rect, Justification::centred);
            }
            continue;
        }

        const auto& st = proc.getUnit (u);
        const auto& info = blockInfo (st.type);
        const Colour col (info.colour);
        const bool isOn = proc.isUnitOn (u);
        const bool empty = (info.usesModelFile && ! st.files[0].loaded) || (info.usesIrFile && ! st.files[1].loaded && ! st.files[2].loaded);

        if (sel) { g.setColour (col.withAlpha (0.2f)); g.fillRoundedRectangle (rect.expanded (4.0f), 9.0f); }
        g.setColour (isOn ? col.withAlpha (empty ? 0.35f : 0.88f) : Colour (0xff1a2232));
        g.fillRoundedRectangle (rect, 6.0f);
        g.setColour (isOn ? col.brighter (0.4f) : col.withAlpha (0.45f));
        g.drawRoundedRectangle (rect, 6.0f, sel ? 2.2f : 1.1f);
        if (sel) { g.setColour (Colours::white); g.drawRoundedRectangle (rect.expanded (2.5f), 8.0f, 1.3f); }

        const Colour ink = isOn ? Colour (0xff081018) : col.withAlpha (0.85f);
        auto iconType = st.type;
        if (st.type == BlockType::Amp)
        {
            const auto gear = proc.unitGear (u);
            iconType = gear == Gear::Pedal || gear == Gear::Outboard ? BlockType::Overdrive : (gear == Gear::Rig ? BlockType::Cab : BlockType::Amp);
        }
        drawBlockIcon (g, iconType, rect.reduced (rect.getWidth() * 0.3f, 7.0f).withTrimmedBottom (12.0f), ink);
        g.setColour (ink);
        g.setFont (lcdFont (11.0f, true));
        g.drawText (proc.unitShortName (u), rect.withTrimmedTop (rect.getHeight() - 15.0f), Justification::centred);

        if (st.files[0].loading)
        {
            const float t = (float) (Time::getMillisecondCounter() % 1000) / 1000.0f;
            g.setColour (Colours::white);
            g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre ({ rect.getX() + rect.getWidth() * t, rect.getY() + 4.0f }));
        }
        if (beingDragged)
        {
            g.setColour (colours::lcdBg.withAlpha (0.6f));
            g.fillRoundedRectangle (rect, 6.0f);
        }
    }

    if (draggingBlock && dragFrom >= 0)
    {
        const int u = layout.cell[(size_t) dragFrom];
        if (u >= 0)
        {
            const auto& info = blockInfo (proc.getUnit (u).type);
            auto ghost = cellRect (dragFrom).withCentre (dragPos);
            g.setColour (Colour (info.colour).withAlpha (0.75f));
            g.fillRoundedRectangle (ghost, 6.0f);
            g.setColour (Colours::white);
            g.drawRoundedRectangle (ghost, 6.0f, 1.5f);
            g.setFont (lcdFont (11.0f, true));
            g.drawText (proc.unitShortName (u), ghost, Justification::centred);
            const int target = cellAt (dragPos);
            if (target >= 0)
            {
                g.setColour (Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (cellRect (target).expanded (3.0f), 8.0f, 2.0f);
            }
        }
    }
}

void LcdDisplay::drawFileBox (Graphics& g, Rectangle<float> box, const SlotStatus& st, const String& emptyText, Colour c)
{
    g.setColour (Colour (0xff111a2b));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (c.withAlpha (0.5f));
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    auto inner = box.reduced (8.0f, 4.0f);
    const bool narrow = box.getWidth() < 250.0f;
    g.setFont (lcdFont (narrow ? 12.0f : 14.0f, true));
    if (st.loading)
    {
        g.setColour (colours::lcdText);
        g.drawText ("CARGANDO " + st.name.toUpperCase() + "...", inner.removeFromTop (20.0f), Justification::centredLeft, true);
    }
    else if (st.loaded || st.error.isNotEmpty())
    {
        g.setColour (st.error.isNotEmpty() ? Colour (0xffff6b6b) : colours::lcdText);
        g.drawFittedText (st.name, inner.removeFromTop (20.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
        g.setFont (lcdFont (10.5f));
        g.setColour (colours::lcdDim);
        String meta = st.error;
        if (meta.isEmpty())
        {
            meta = st.format;
            if (st.architecture.isNotEmpty()) meta << " | " << st.architecture;
            if (st.sampleRate > 0.0)          meta << " | " << String (st.sampleRate / 1000.0, 1) << "k";
            if (st.hasLoudness && ! narrow)   meta << " | " << String (st.loudness, 1) << " dB";
        }
        g.drawText (meta, inner.removeFromTop (16.0f), Justification::centredLeft, true);
    }
    else
    {
        g.setColour (colours::lcdDim);
        g.drawText (emptyText, inner.removeFromTop (20.0f), Justification::centredLeft, true);
        g.setFont (lcdFont (10.5f));
        g.drawText ("clic o solta un archivo", inner.removeFromTop (16.0f), Justification::centredLeft, true);
    }
}

void LcdDisplay::drawParams (Graphics& g, Rectangle<float> area, int unit)
{
    const auto& st = proc.getUnit (unit);
    const Colour c (blockInfo (st.type).colour);
    const bool isOn = proc.isUnitOn (unit);
    const float colW = area.getWidth() * 0.5f - 6.0f;
    int shown = 0;
    for (int i = 0; i < kParamsPerUnit; ++i)
    {
        if (! paramVisible (st, i)) continue;
        auto* prm = proc.unitParam (unit, i);
        const int col = shown / 3, row = shown % 3;
        auto cell = Rectangle<float> (area.getX() + (float) col * (colW + 12.0f), area.getY() + (float) row * 26.0f, colW, 22.0f);
        g.setFont (lcdFont (11.0f, true));
        g.setColour (colours::lcdText.withAlpha (0.8f));
        g.drawText (blockInfo (st.type).params[(size_t) i].name, cell.removeFromTop (11.0f), Justification::centredLeft);
        g.setColour (colours::lcdText);
        g.drawText (proc.unitParamText (unit, i), cell.withTrimmedTop (-11.0f).removeFromTop (11.0f), Justification::centredRight);
        auto bar = cell.reduced (0.0f, 3.0f);
        g.setColour (Colour (0xff1a2232));
        g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (isOn ? c : c.withAlpha (0.4f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prm->getValue()), 2.5f);
        ++shown;
    }
    if (shown == 0)
    {
        g.setFont (lcdFont (12.0f));
        g.setColour (colours::lcdDim);
        g.drawText ("Carga un archivo para editar", area.removeFromTop (20.0f), Justification::centredLeft);
    }
}

void LcdDisplay::drawDetail (Graphics& g)
{
    auto d = detailArea();
    const int u = selectedUnit();
    if (u < 0)
    {
        g.setFont (lcdFont (14.0f, true));
        g.setColour (colours::lcdDim);
        g.drawText (selected >= 0 ? "CELDA VACIA - clic para agregar un bloque" : "", d.removeFromTop (22.0f), Justification::centredLeft);
        g.setFont (lcdFont (11.5f));
        g.drawFittedText ("Fila de arriba = camino A. Fila de abajo = camino B en paralelo (se mezcla en M).\n"
                          "Arrastra bloques para moverlos, doble clic = on/off, clic derecho = menu.",
                          d.toNearestInt(), Justification::topLeft, 3);
        return;
    }

    const auto& st = proc.getUnit (u);
    const auto& info = blockInfo (st.type);
    const Colour c (info.colour);
    const bool isOn = proc.isUnitOn (u);

    auto left = d.removeFromLeft (d.getWidth() * 0.5f);
    d.removeFromLeft (16.0f);
    auto title = left.removeFromTop (20.0f);
    g.setColour (c);
    g.setFont (lcdFont (15.0f, true));
    const String name = proc.unitLongName (u);
    g.drawText (name, title.removeFromLeft (jmin (title.getWidth() - 50.0f, 330.0f)), Justification::centredLeft, true);
    auto pill = title.removeFromRight (44.0f).reduced (0.0f, 2.0f);
    g.setColour (isOn ? c : Colour (0xff2a3346));
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (isOn ? Colour (0xff081018) : colours::lcdDim);
    g.setFont (lcdFont (11.0f, true));
    g.drawText (isOn ? "ON" : "OFF", pill, Justification::centred);

    if (info.usesModelFile)
        drawFileBox (g, fileBox (0), st.files[0], "SIN MODELO", c);
    else if (info.usesIrFile)
    {
        drawFileBox (g, fileBox (1), st.files[1], "IR A: vacia", c);
        drawFileBox (g, fileBox (2), st.files[2], "IR B: vacia", c);
    }
    else
    {
        g.setFont (lcdFont (11.5f));
        g.setColour (colours::lcdDim);
        g.drawFittedText (String (categoryName (info.category)) + ".  Las perillas de abajo controlan este bloque.",
                          left.withTrimmedTop (6.0f).toNearestInt(), Justification::topLeft, 2);
    }
    drawParams (g, d, u);
}

void LcdDisplay::drawMixer (Graphics& g)
{
    auto d = detailArea();
    g.setColour (colours::accent);
    g.setFont (lcdFont (15.0f, true));
    g.drawText ("MEZCLADOR A/B", d.removeFromTop (20.0f), Justification::centredLeft);
    g.setFont (lcdFont (11.5f));
    g.setColour (colours::lcdDim);
    g.drawFittedText ("Nivel y paneo de cada camino al juntarse. Las perillas controlan: NIVEL A, NIVEL B, PAN A, PAN B.",
                      d.removeFromTop (34.0f).toNearestInt(), Justification::topLeft, 2);
    int i = 0;
    for (auto* id : { "merge_a", "merge_b", "pan_a", "pan_b" })
    {
        auto* prm = proc.apvts.getParameter (id);
        auto cell = Rectangle<float> (d.getX() + (float) (i % 2) * (d.getWidth() * 0.5f), d.getY() + (float) (i / 2) * 22.0f, d.getWidth() * 0.5f - 12.0f, 20.0f);
        g.setColour (colours::lcdText);
        g.setFont (lcdFont (11.0f, true));
        g.drawText (prm->getName (20).toUpperCase(), cell.removeFromLeft (110.0f), Justification::centredLeft);
        g.drawText (prm->getCurrentValueAsText(), cell.removeFromRight (70.0f), Justification::centredRight);
        auto bar = cell.reduced (4.0f, 6.0f);
        g.setColour (Colour (0xff1a2232)); g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (colours::accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prm->getValue()), 2.5f);
        ++i;
    }
}

void LcdDisplay::drawLibrary (Graphics& g)
{
    auto d = detailArea();
    const bool ir = selectionWantsIR();
    auto head = d.removeFromTop (20.0f);
    g.setFont (lcdFont (13.0f, true));
    g.setColour (colours::accent);
    g.drawText (String (ir ? "BIBLIOTECA > IRs   " : "BIBLIOTECA > MODELOS   ") + String (libFiles.size()) + " archivos",
                head.removeFromLeft (360.0f), Justification::centredLeft);
    auto fb = folderButtonArea();
    g.setColour (Colour (0xff1a2232)); g.fillRoundedRectangle (fb, 4.0f);
    g.setColour (colours::lcdText); g.setFont (lcdFont (11.0f, true));
    g.drawText ("CAMBIAR CARPETA", fb, Justification::centred);
    g.setColour (colours::lcdDim);
    g.drawText (proc.getLibraryFolder().getFullPathName(), head.withTrimmedRight (130.0f), Justification::centredRight, true);

    d.removeFromTop (4.0f);
    if (libFiles.isEmpty())
    {
        g.setFont (lcdFont (12.0f));
        g.drawFittedText ("No hay archivos. Pone tus .nam / .json / .wav en esta carpeta (incluye subcarpetas).",
                          d.toNearestInt(), Justification::topLeft, 2);
        return;
    }
    const int visible = (int) (d.getHeight() / rowH);
    const int u = selectedUnit();
    const auto current = u >= 0 ? proc.getUnit (u).files[ir ? 1 : 0].file : File();
    g.setFont (lcdFont (12.0f));
    for (int i = 0; i < visible && i + libScroll < libFiles.size(); ++i)
    {
        const int idx = i + libScroll;
        const auto& f = libFiles.getReference (idx);
        auto row = d.removeFromTop (rowH);
        const bool isCur = f == current;
        if (idx == hoverRow || isCur)
        {
            g.setColour (isCur ? colours::accent.withAlpha (0.3f) : Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (row, 3.0f);
        }
        g.setColour (isCur ? colours::accent : colours::lcdText);
        g.drawText ((isCur ? "> " : "  ") + f.getRelativePathFrom (proc.getLibraryFolder()), row.reduced (6.0f, 0.0f).withTrimmedRight (60.0f), Justification::centredLeft, true);
        g.setColour (colours::lcdDim);
        g.drawText (f.getFileExtension().toUpperCase().substring (1), row.removeFromRight (56.0f), Justification::centredRight);
    }
}

void LcdDisplay::drawTuner (Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (20.0f).withTrimmedTop (36.0f);
    g.setColour (colours::accent);
    g.setFont (lcdFont (13.0f, true));
    g.drawText ("AFINADOR  (salida silenciada)", area.removeFromTop (18.0f), Justification::centred);

    if (tunerHz <= 0.0f)
    {
        g.setColour (colours::lcdDim);
        g.setFont (lcdFont (40.0f, true));
        g.drawText ("--", area, Justification::centred);
        return;
    }
    const float midi = 69.0f + 12.0f * std::log2 (tunerHz / 440.0f);
    const int note = roundToInt (midi);
    const float cents = (midi - (float) note) * 100.0f;
    const bool inTune = std::abs (cents) < 3.0f;
    const Colour c = inTune ? Colour (0xff3ddc84) : (std::abs (cents) < 15.0f ? Colour (0xffffc24d) : Colour (0xffff4d4d));

    g.setColour (c);
    g.setFont (lcdFont (64.0f, true));
    g.drawText (String (noteNames[(note % 12 + 12) % 12]) + String (note / 12 - 1), area.removeFromTop (80.0f), Justification::centred);

    auto meter = area.removeFromTop (34.0f).reduced (area.getWidth() * 0.15f, 6.0f);
    for (int i = -25; i <= 25; ++i)
    {
        const float x = meter.getCentreX() + (float) i / 25.0f * meter.getWidth() * 0.5f;
        const bool lit = (cents >= 0 ? i >= 0 && i <= cents / 2.0f : i <= 0 && i >= cents / 2.0f) || i == 0;
        g.setColour (lit ? (i == 0 ? Colours::white : c) : colours::lcdDim.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (3.0f, i % 5 == 0 ? meter.getHeight() : meter.getHeight() * 0.6f).withCentre ({ x, meter.getCentreY() }));
    }
    g.setColour (colours::lcdText);
    g.setFont (lcdFont (13.0f, true));
    g.drawText ((cents > 0 ? "+" : "") + String (cents, 1) + " cents   " + String (tunerHz, 1) + " Hz", area.removeFromTop (20.0f), Justification::centred);
}

void LcdDisplay::drawLooper (Graphics& g)
{
    auto d = detailArea();
    const auto state = proc.getLooperState();
    const char* names[] = { "VACIO", "GRABANDO", "REPRODUCIENDO", "OVERDUB", "DETENIDO" };
    const Colour cols[] = { colours::lcdDim, Colour (0xffff3b3b), Colour (0xff3ddc84), Colour (0xffff7a3b), Colour (0xffffa62b) };
    g.setColour (colours::accent);
    g.setFont (lcdFont (15.0f, true));
    g.drawText ("LOOPER", d.removeFromTop (20.0f), Justification::centredLeft);
    g.setColour (cols[(int) state]);
    g.setFont (lcdFont (22.0f, true));
    g.drawText (names[(int) state], d.removeFromTop (30.0f), Justification::centredLeft);
    auto bar = d.removeFromTop (14.0f);
    g.setColour (Colour (0xff1a2232)); g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (cols[(int) state]); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.getLooperPosition()), 4.0f);
    g.setFont (lcdFont (11.0f));
    g.setColour (colours::lcdDim);
    g.drawText ("Footswitches: REC/PLAY/DUB  |  STOP  |  CLEAR  |  EXIT   (hasta 40 s)", d.removeFromTop (20.0f), Justification::centredLeft);
}

//==============================================================================
void LcdDisplay::showLibrary (bool show) { libraryMode = show; if (show) refreshLibrary(); repaint(); }

void LcdDisplay::refreshLibrary()
{
    auto folder = proc.getLibraryFolder();
    folder.createDirectory();
    libFiles = folder.findChildFiles (File::findFiles, true, selectionWantsIR() ? ToneDeckProcessor::irWildcard() : ToneDeckProcessor::modelWildcard());
    libFiles.sort();
    libScroll = jlimit (0, jmax (0, libFiles.size() - 1), libScroll);
}

void LcdDisplay::mouseDown (const MouseEvent& e)
{
    const auto pt = e.position;
    draggingBlock = false;
    dragFrom = -1;
    if (proc.isTunerOn()) { proc.setTunerOn (false); return; }

    if (mixerNode().expanded (4.0f).contains (pt))
    {
        selected = kMixerSelection; libraryMode = false;
        if (onSelect) onSelect (selected);
        repaint();
        return;
    }

    const int cell = cellAt (pt);
    if (cell >= 0)
    {
        if (e.mods.isPopupMenu()) { selected = cell; if (onSelect) onSelect (cell); if (onContextMenu) onContextMenu (cell); repaint(); return; }
        if (proc.unitAtCell (cell) >= 0) { dragFrom = cell; dragPos = pt; }
        return;   // select / add on mouseUp so drags work
    }

    if (libraryMode && proc.getFsPage() != FsPage::Looper)
    {
        if (folderButtonArea().contains (pt)) { if (onChooseFolder) onChooseFolder(); return; }
        const int row = libraryRowAt (pt);
        if (row >= 0 && onLibraryPick) onLibraryPick (libFiles[row]);
        return;
    }

    const int u = selectedUnit();
    if (u >= 0 && onLoadRequest)
    {
        const auto& info = blockInfo (proc.getUnit (u).type);
        if (info.usesModelFile && fileBox (0).contains (pt)) onLoadRequest (u, 0);
        else if (info.usesIrFile && fileBox (1).contains (pt)) onLoadRequest (u, 1);
        else if (info.usesIrFile && fileBox (2).contains (pt)) onLoadRequest (u, 2);
    }
}

void LcdDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! draggingBlock && e.getDistanceFromDragStart() > 6) draggingBlock = true;
    dragPos = e.position;
    if (draggingBlock) repaint();
}

void LcdDisplay::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int cell = cellAt (e.position);
    if (draggingBlock)
    {
        if (cell >= 0 && cell != dragFrom && onMove) { onMove (dragFrom, cell); selected = cell; if (onSelect) onSelect (cell); }
        draggingBlock = false; dragFrom = -1;
        repaint();
        return;
    }
    dragFrom = -1;
    if (cell < 0 || cellAt (e.mouseDownPosition) != cell) return;

    selected = cell;
    const int u = proc.unitAtCell (cell);
    const bool fileBlock = u >= 0 && (blockInfo (proc.getUnit (u).type).usesModelFile || blockInfo (proc.getUnit (u).type).usesIrFile);
    if (libraryMode && fileBlock) refreshLibrary();
    else libraryMode = false;
    if (onSelect) onSelect (cell);
    if (u < 0 && onAddRequest) onAddRequest (cell);
    repaint();
}

void LcdDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const int u = proc.unitAtCell (cellAt (e.position));
    if (u >= 0 && onToggle) onToggle (u);
}

void LcdDisplay::mouseMove (const MouseEvent& e)
{
    const int row = libraryMode ? libraryRowAt (e.position) : -1;
    const int cell = cellAt (e.position);
    setMouseCursor (cell >= 0 || row >= 0 || mixerNode().contains (e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (row != hoverRow || cell != hoverCell) { hoverRow = row; hoverCell = cell; repaint(); }
}

void LcdDisplay::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (! libraryMode) return;
    const int visible = (int) ((detailArea().getHeight() - 24.0f) / rowH);
    libScroll = jlimit (0, jmax (0, libFiles.size() - visible), libScroll - (w.deltaY > 0 ? 3 : -3));
    repaint();
}

bool LcdDisplay::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (ToneDeckProcessor::kindForFile (File (f)) != SlotKind::None) return true;
    return false;
}

void LcdDisplay::filesDropped (const StringArray& files, int x, int y)
{
    dragOver = false;
    const int cell = cellAt ({ (float) x, (float) y });
    for (const auto& path : files)
        if (onFileDropped) onFileDropped (cell, File (path));
    repaint();
}
} // namespace td::ui
