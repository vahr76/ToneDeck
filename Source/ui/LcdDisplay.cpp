#include "LcdDisplay.h"
#include "PedalLookAndFeel.h"

namespace td::ui
{
using namespace juce;

namespace
{
void drawBlockIcon (Graphics& g, int block, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const auto cx = r.getCentreX(), cy = r.getCentreY();
    const float s = jmin (r.getWidth(), r.getHeight());
    PathStrokeType stroke (1.6f, PathStrokeType::curved, PathStrokeType::rounded);

    switch (block)
    {
        case Gate:
        {
            Path p;
            p.startNewSubPath (r.getX(), cy);
            for (int i = 0; i <= 20; ++i)
            {
                const float t = (float) i / 20.0f;
                const float amp = t < 0.55f ? s * 0.38f * (1.0f - t / 0.55f * 0.3f) : 0.0f;
                p.lineTo (r.getX() + t * r.getWidth(), cy + std::sin (t * 22.0f) * amp);
            }
            g.strokePath (p, stroke);
            break;
        }
        case Pedal:
        {
            auto box = Rectangle<float> (s * 0.62f, s * 0.9f).withCentre ({ cx, cy });
            g.drawRoundedRectangle (box, 2.5f, 1.6f);
            for (int i = 0; i < 2; ++i)
                g.fillEllipse (Rectangle<float> (s * 0.14f, s * 0.14f).withCentre ({ box.getX() + box.getWidth() * (0.3f + 0.4f * (float) i), box.getY() + s * 0.2f }));
            g.drawEllipse (Rectangle<float> (s * 0.24f, s * 0.24f).withCentre ({ cx, box.getBottom() - s * 0.24f }), 1.6f);
            break;
        }
        case Amp:
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
        case Eq:
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
        case Cab:
        {
            auto box = Rectangle<float> (s * 0.95f, s * 0.95f).withCentre ({ cx, cy });
            g.drawRoundedRectangle (box, 2.5f, 1.6f);
            g.drawEllipse (box.reduced (s * 0.14f), 1.6f);
            g.fillEllipse (Rectangle<float> (s * 0.18f, s * 0.18f).withCentre ({ cx, cy }));
            break;
        }
        case Delay:
        {
            for (int i = 0; i < 4; ++i)
            {
                const float d = s * (0.34f - 0.06f * (float) i);
                g.setColour (c.withAlpha (1.0f - 0.22f * (float) i));
                g.fillEllipse (Rectangle<float> (d, d).withCentre ({ r.getX() + r.getWidth() * (0.18f + 0.22f * (float) i), cy }));
            }
            break;
        }
        case Reverb:
        {
            for (int i = 0; i < 3; ++i)
            {
                Path p;
                const float rad = s * (0.2f + 0.17f * (float) i);
                p.addCentredArc (r.getX() + s * 0.15f, cy, rad, rad, 0.0f, 0.55f, MathConstants<float>::pi - 0.55f, true);
                g.setColour (c.withAlpha (1.0f - 0.25f * (float) i));
                g.strokePath (p, stroke);
            }
            break;
        }
        default: break;
    }
}
} // namespace

//==============================================================================
LcdDisplay::LcdDisplay (ToneDeckProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

Rectangle<float> LcdDisplay::headerArea() const { return getLocalBounds().toFloat().reduced (14.0f, 10.0f).removeFromTop (26.0f); }
Rectangle<float> LcdDisplay::chainArea() const  { return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (44.0f).withHeight (74.0f); }
Rectangle<float> LcdDisplay::detailArea() const { return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (118.0f); }

Rectangle<float> LcdDisplay::blockRect (int b) const
{
    auto area = chainArea().reduced (26.0f, 0.0f);
    const float slot = area.getWidth() / (float) NumBlocks;
    const float w = slot * 0.80f, h = 52.0f;
    return { area.getX() + slot * (float) b + (slot - w) * 0.5f, area.getCentreY() - h * 0.5f - 6.0f, w, h };
}

Rectangle<float> LcdDisplay::fileArea() const
{
    auto d = detailArea();
    return d.removeFromLeft (d.getWidth() * 0.52f).withTrimmedTop (26.0f).withHeight (58.0f);
}

Rectangle<float> LcdDisplay::folderButtonArea() const
{
    return detailArea().removeFromTop (22.0f).removeFromRight (110.0f);
}

int LcdDisplay::blockAt (Point<float> pt) const
{
    for (int b = 0; b < NumBlocks; ++b)
        if (blockRect (b).expanded (4.0f, 8.0f).contains (pt))
            return b;
    return -1;
}

int LcdDisplay::libraryRowAt (Point<float> pt) const
{
    auto list = detailArea().withTrimmedTop (28.0f);
    if (! list.contains (pt))
        return -1;
    const int row = (int) ((pt.y - list.getY()) / rowH) + libScroll;
    return row < libFiles.size() ? row : -1;
}

//==============================================================================
void LcdDisplay::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // glass
    ColourGradient bg (Colour (0xff0d1424), r.getX(), r.getY(), Colour (0xff04060b), r.getX(), r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 8.0f);

    // subtle scan lines for the LCD feel
    g.setColour (Colours::white.withAlpha (0.018f));
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);

    drawHeader (g);
    drawChain (g);
    if (libraryMode) drawLibrary (g);
    else             drawDetail (g);

    // status line (TONE3000 progress etc.)
    const auto msg = proc.getOnlineMessage();
    if (msg.isNotEmpty())
    {
        auto bar = r.reduced (10.0f, 8.0f).removeFromBottom (22.0f);
        g.setColour (Colour (0xe0101a2e));
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (bar, 5.0f, 1.0f);
        g.setFont (lcdFont (12.0f, true));
        g.drawText (msg, bar.reduced (10.0f, 0.0f), Justification::centredLeft, true);
    }

    // glare
    Path glare;
    glare.addRoundedRectangle (r.withHeight (r.getHeight() * 0.42f), 8.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.045f), 0.0f, r.getY(),
                                       Colours::transparentWhite, 0.0f, r.getY() + r.getHeight() * 0.42f, false));
    g.fillPath (glare);

    if (dragOver)
    {
        g.setColour (colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (r.reduced (3.0f), 8.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (r.reduced (3.0f), 8.0f, 2.0f);
        g.setFont (lcdFont (20.0f, true));
        g.drawText ("DROP .NAM / .JSON / .WAV", r, Justification::centred);
    }
}

void LcdDisplay::drawMeter (Graphics& g, Rectangle<float> area, float level, const String& label)
{
    g.setFont (lcdFont (11.0f, true));
    g.setColour (colours::lcdDim);
    g.drawText (label, area.removeFromLeft (28.0f), Justification::centredLeft);

    const int segs = 16;
    const float db = Decibels::gainToDecibels (level, -60.0f);
    const float norm = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    const float sw = area.getWidth() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const bool lit = (float) i / (float) segs < norm;
        const Colour c = i >= segs - 2 ? Colour (0xffff4d4d) : (i >= segs - 5 ? Colour (0xffffc24d) : Colour (0xff3ddc84));
        g.setColour (lit ? c : c.withAlpha (0.12f));
        g.fillRoundedRectangle (Rectangle<float> (area.getX() + sw * (float) i + 1.0f, area.getCentreY() - 5.0f, sw - 2.0f, 10.0f), 1.5f);
    }
}

void LcdDisplay::drawHeader (Graphics& g)
{
    auto h = headerArea();

    // preset tag
    auto tag = h.removeFromLeft (40.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (tag.reduced (0.0f, 2.0f), 4.0f);
    g.setColour (colours::lcdBg);
    g.setFont (lcdFont (13.0f, true));
    g.drawText ("PST", tag, Justification::centred);

    h.removeFromLeft (10.0f);
    g.setColour (colours::lcdText);
    g.setFont (lcdFont (20.0f, true));
    g.drawText (proc.getCurrentPresetName().toUpperCase(), h.removeFromLeft (360.0f), Justification::centredLeft, true);

    auto meters = h.removeFromRight (380.0f);
    drawMeter (g, meters.removeFromLeft (185.0f), proc.inputMeter.level.load(), "IN");
    meters.removeFromLeft (10.0f);
    drawMeter (g, meters, proc.outputMeter.level.load(), "OUT");

    // thin separator
    g.setColour (colours::lcdDim.withAlpha (0.35f));
    g.drawHorizontalLine ((int) headerArea().getBottom() + 6, 14.0f, (float) getWidth() - 14.0f);
}

void LcdDisplay::drawChain (Graphics& g)
{
    const auto chain = chainArea();
    const float lineY = blockRect (0).getCentreY();

    // signal line
    g.setColour (colours::lcdDim.withAlpha (0.7f));
    g.drawLine (chain.getX() + 10.0f, lineY, chain.getRight() - 10.0f, lineY, 2.0f);
    g.setFont (lcdFont (10.0f, true));
    for (auto [x, txt] : { std::pair<float, const char*> { chain.getX() + 10.0f, "IN" }, { chain.getRight() - 10.0f, "OUT" } })
    {
        g.setColour (colours::lcdDim);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, lineY }));
        g.drawText (txt, Rectangle<float> (40.0f, 12.0f).withCentre ({ x, lineY + 16.0f }), Justification::centred);
    }

    for (int b = 0; b < NumBlocks; ++b)
    {
        const auto& def = blocks()[(size_t) b];
        const auto rect = blockRect (b);
        const bool isOn = proc.apvts.getRawParameterValue (def.onParam)->load() > 0.5f;
        const Colour c (def.colour);
        const bool sel = b == selected;
        bool empty = false;
        if (def.slot != SlotKind::None)
            empty = ! proc.getSlotStatus (b).loaded;

        if (sel)
        {
            g.setColour (c.withAlpha (0.18f));
            g.fillRoundedRectangle (rect.expanded (5.0f), 10.0f);
        }

        g.setColour (isOn ? c.withAlpha (empty ? 0.35f : 0.85f) : Colour (0xff1a2232));
        g.fillRoundedRectangle (rect, 7.0f);
        g.setColour (isOn ? c.brighter (0.4f) : c.withAlpha (0.45f));
        g.drawRoundedRectangle (rect, 7.0f, sel ? 2.4f : 1.2f);
        if (sel)
        {
            g.setColour (Colours::white);
            g.drawRoundedRectangle (rect.expanded (3.0f), 9.0f, 1.4f);
        }

        const Colour ink = isOn ? Colour (0xff081018) : c.withAlpha (0.8f);
        drawBlockIcon (g, b, rect.reduced (rect.getWidth() * 0.3f, 9.0f).withTrimmedBottom (12.0f), ink);
        g.setColour (ink);
        g.setFont (lcdFont (12.0f, true));
        g.drawText (def.shortName, rect.withTrimmedTop (rect.getHeight() - 17.0f), Justification::centred);

        if (proc.getSlotStatus (b).loading)
        {
            g.setColour (Colours::white);
            const float t = (float) (Time::getMillisecondCounter() % 1000) / 1000.0f;
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ rect.getX() + rect.getWidth() * t, rect.getY() + 4.0f }));
        }

        if (sel)
        {
            Path tri;
            const float tx = rect.getCentreX(), ty = rect.getBottom() + 8.0f;
            tri.addTriangle (tx - 6.0f, ty + 6.0f, tx + 6.0f, ty + 6.0f, tx, ty);
            g.setColour (Colours::white);
            g.fillPath (tri);
        }
    }
}

void LcdDisplay::drawDetail (Graphics& g)
{
    auto d = detailArea();
    const auto& def = blocks()[(size_t) selected];
    const Colour c (def.colour);
    const bool isOn = proc.apvts.getRawParameterValue (def.onParam)->load() > 0.5f;

    auto left  = d.removeFromLeft (d.getWidth() * 0.52f);
    d.removeFromLeft (18.0f);
    auto right = d;

    // title row
    auto title = left.removeFromTop (24.0f);
    g.setColour (c);
    g.setFont (lcdFont (17.0f, true));
    g.drawText (def.longName, title.removeFromLeft (200.0f), Justification::centredLeft);
    auto pill = title.removeFromLeft (46.0f).reduced (0.0f, 3.0f);
    g.setColour (isOn ? c : Colour (0xff2a3346));
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (isOn ? Colour (0xff081018) : colours::lcdDim);
    g.setFont (lcdFont (11.0f, true));
    g.drawText (isOn ? "ON" : "OFF", pill, Justification::centred);

    left.removeFromTop (2.0f);

    if (def.slot != SlotKind::None)
    {
        const auto& st = proc.getSlotStatus (selected);
        auto fa = fileArea();
        g.setColour (Colour (0xff111a2b));
        g.fillRoundedRectangle (fa, 6.0f);
        g.setColour (c.withAlpha (0.5f));
        g.drawRoundedRectangle (fa, 6.0f, 1.0f);

        auto inner = fa.reduced (10.0f, 6.0f);
        g.setFont (lcdFont (16.0f, true));
        if (st.loading)
        {
            g.setColour (colours::lcdText);
            g.drawText ("LOADING " + st.name.toUpperCase() + "...", inner.removeFromTop (24.0f), Justification::centredLeft, true);
        }
        else if (st.loaded || st.error.isNotEmpty())
        {
            g.setColour (st.error.isNotEmpty() ? Colour (0xffff6b6b) : colours::lcdText);
            g.drawFittedText (st.name, inner.removeFromTop (24.0f).toNearestInt(), Justification::centredLeft, 1, 0.85f);
            g.setFont (lcdFont (12.0f));
            g.setColour (colours::lcdDim);
            String meta;
            if (st.error.isNotEmpty())
                meta = st.error;
            else
            {
                meta = st.format;
                if (st.architecture.isNotEmpty()) meta << "  |  " << st.architecture;
                if (st.sampleRate > 0.0)          meta << "  |  " << String (st.sampleRate / 1000.0, 1) << " kHz";
                if (st.hasLoudness)               meta << "  |  " << String (st.loudness, 1) << " dB";
            }
            g.drawText (meta, inner.removeFromTop (18.0f), Justification::centredLeft, true);
        }
        else
        {
            g.setColour (colours::lcdDim);
            g.drawText (def.slot == SlotKind::IR ? "NO IR LOADED" : "NO MODEL LOADED", inner.removeFromTop (24.0f), Justification::centredLeft);
            g.setFont (lcdFont (12.0f));
            g.drawText (def.slot == SlotKind::IR ? "Click or drop a .wav impulse response" : "Click or drop a .nam (NAM / A2) or .json (AIDA-X)",
                        inner.removeFromTop (18.0f), Justification::centredLeft, true);
        }

        auto hint = left.withTrimmedTop (fa.getBottom() - left.getY() + 6.0f).removeFromTop (18.0f);
        g.setFont (lcdFont (11.0f));
        g.setColour (colours::lcdDim);
        g.drawText ("[LOAD] browse  [< >] prev/next file  [LIBRARY] list", hint, Justification::centredLeft, true);
    }
    else
    {
        g.setFont (lcdFont (12.0f));
        g.setColour (colours::lcdDim);
        String desc;
        switch (selected)
        {
            case Gate:   desc = "Hysteresis gate in front of the models. Keyed from the dry input."; break;
            case Eq:     desc = "Post-amp tone stack: shelving bass/treble, sweepable mid, presence."; break;
            case Delay:  desc = "Stereo analog-style delay with darkened repeats."; break;
            case Reverb: desc = "Stereo room/hall reverb."; break;
            default: break;
        }
        g.drawFittedText (desc, left.removeFromTop (40.0f).toNearestInt(), Justification::topLeft, 2);

        if (selected == Gate)
        {
            auto bar = left.removeFromTop (22.0f).withWidth (260.0f);
            g.setFont (lcdFont (11.0f, true));
            g.drawText ("GATE", bar.removeFromLeft (44.0f), Justification::centredLeft);
            const float gg = proc.gateGain.load();
            g.setColour (Colour (0xff1a2232));
            g.fillRoundedRectangle (bar.reduced (0.0f, 6.0f), 3.0f);
            g.setColour (gg > 0.5f ? Colour (0xff3ddc84) : Colour (0xffff4d4d));
            g.fillRoundedRectangle (bar.reduced (0.0f, 6.0f).withWidth (bar.getWidth() * gg), 3.0f);
        }
    }

    // parameter read-outs (mirror the physical knobs)
    const auto& st = proc.getSlotStatus (selected);
    int shown = 0;
    for (const auto& k : def.knobs)
    {
        if (! knobVisible (k, st))
            continue;
        auto* param = proc.apvts.getParameter (k.paramId);
        if (param == nullptr)
            continue;

        auto row = right.removeFromTop (22.0f);
        right.removeFromTop (3.0f);
        g.setFont (lcdFont (12.0f, true));
        g.setColour (colours::lcdText.withAlpha (0.85f));
        g.drawText (k.label, row.removeFromLeft (84.0f), Justification::centredLeft);
        auto valueBox = row.removeFromRight (78.0f);
        g.setColour (colours::lcdText);
        g.drawText (param->getCurrentValueAsText(), valueBox, Justification::centredRight);

        auto bar = row.reduced (6.0f, 7.0f);
        g.setColour (Colour (0xff1a2232));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (isOn ? c : c.withAlpha (0.4f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * param->getValue()), 3.0f);
        ++shown;
    }
    if (shown == 0)
    {
        g.setFont (lcdFont (12.0f));
        g.setColour (colours::lcdDim);
        g.drawText ("Load a model to edit", right.removeFromTop (22.0f), Justification::centredLeft);
    }
}

void LcdDisplay::drawLibrary (Graphics& g)
{
    auto d = detailArea();
    const int slot = librarySlot();
    const Colour c (blocks()[(size_t) slot].colour);

    auto head = d.removeFromTop (22.0f);
    g.setFont (lcdFont (14.0f, true));
    g.setColour (c);
    g.drawText (String ("LIBRARY > ") + blocks()[(size_t) slot].shortName + "   " + String (libFiles.size()) + " files",
                head.removeFromLeft (360.0f), Justification::centredLeft);
    auto fb = folderButtonArea();
    g.setColour (Colour (0xff1a2232));
    g.fillRoundedRectangle (fb, 4.0f);
    g.setColour (colours::lcdText);
    g.setFont (lcdFont (11.0f, true));
    g.drawText ("CHANGE FOLDER", fb, Justification::centred);
    g.setColour (colours::lcdDim);
    g.drawText (proc.getLibraryFolder().getFullPathName(), head.withTrimmedRight (120.0f), Justification::centredRight, true);

    d.removeFromTop (6.0f);
    const int visible = (int) (d.getHeight() / rowH);
    const auto& current = proc.getSlotStatus (slot).file;

    if (libFiles.isEmpty())
    {
        g.setFont (lcdFont (13.0f));
        g.setColour (colours::lcdDim);
        g.drawFittedText ("No files found. Put your .nam / .json models and .wav IRs in this folder\n"
                          "(sub-folders are scanned too), or press CHANGE FOLDER.",
                          d.toNearestInt(), Justification::topLeft, 3);
        return;
    }

    g.setFont (lcdFont (13.0f));
    for (int i = 0; i < visible && i + libScroll < libFiles.size(); ++i)
    {
        const int idx = i + libScroll;
        const auto& f = libFiles.getReference (idx);
        auto row = d.removeFromTop (rowH);
        const bool isCurrent = f == current;
        if (idx == hoverRow || isCurrent)
        {
            g.setColour (isCurrent ? c.withAlpha (0.3f) : Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (row, 3.0f);
        }
        g.setColour (isCurrent ? c : colours::lcdText);
        const auto rel = f.getRelativePathFrom (proc.getLibraryFolder());
        g.drawText ((isCurrent ? "> " : "  ") + rel, row.reduced (6.0f, 0.0f).withTrimmedRight (70.0f), Justification::centredLeft, true);
        g.setColour (colours::lcdDim);
        g.drawText (f.getFileExtension().toUpperCase().substring (1), row.removeFromRight (64.0f), Justification::centredRight);
    }

    if (libFiles.size() > visible)   // scroll bar
    {
        auto track = detailArea().withTrimmedTop (28.0f).removeFromRight (3.0f).translated (8.0f, 0.0f);
        const float frac = (float) visible / (float) libFiles.size();
        const float start = (float) libScroll / (float) libFiles.size();
        g.setColour (colours::lcdDim.withAlpha (0.4f));
        g.fillRoundedRectangle (track, 1.5f);
        g.setColour (c);
        g.fillRoundedRectangle (track.withY (track.getY() + track.getHeight() * start).withHeight (track.getHeight() * frac), 1.5f);
    }
}

//==============================================================================
void LcdDisplay::showLibrary (bool show)
{
    libraryMode = show;
    if (show)
        refreshLibrary();
    repaint();
}

void LcdDisplay::refreshLibrary()
{
    auto folder = proc.getLibraryFolder();
    folder.createDirectory();
    libFiles = folder.findChildFiles (File::findFiles, true, ToneDeckProcessor::wildcardForSlot (librarySlot()));
    libFiles.sort();
    libScroll = jlimit (0, jmax (0, libFiles.size() - 1), libScroll);
}

void LcdDisplay::mouseDown (const MouseEvent& e)
{
    const auto pt = e.position;
    const int b = blockAt (pt);
    if (b >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            if (onToggle) onToggle (b);
            return;
        }
        if (libraryMode && b != selected && blocks()[(size_t) b].slot != SlotKind::None)
        {
            selected = b;
            refreshLibrary();
        }
        else
        {
            selected = b;
            libraryMode = false;
        }
        if (onSelect) onSelect (b);
        repaint();
        return;
    }

    if (libraryMode)
    {
        if (folderButtonArea().contains (pt))
        {
            if (onChooseFolder) onChooseFolder();
            return;
        }
        const int row = libraryRowAt (pt);
        if (row >= 0 && onFileDropped)
            onFileDropped (librarySlot(), libFiles[row]);
        return;
    }

    if (blocks()[(size_t) selected].slot != SlotKind::None && fileArea().contains (pt) && onLoadRequest)
        onLoadRequest (selected);
}

void LcdDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const int b = blockAt (e.position);
    if (b >= 0 && onToggle)
        onToggle (b);
}

void LcdDisplay::mouseMove (const MouseEvent& e)
{
    const int row = libraryMode ? libraryRowAt (e.position) : -1;
    const bool overClickable = blockAt (e.position) >= 0 || row >= 0
        || (libraryMode && folderButtonArea().contains (e.position))
        || (! libraryMode && blocks()[(size_t) selected].slot != SlotKind::None && fileArea().contains (e.position));
    setMouseCursor (overClickable ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (row != hoverRow)
    {
        hoverRow = row;
        repaint();
    }
}

void LcdDisplay::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (! libraryMode)
        return;
    const int visible = (int) ((detailArea().getHeight() - 28.0f) / rowH);
    libScroll = jlimit (0, jmax (0, libFiles.size() - visible), libScroll - (w.deltaY > 0 ? 3 : -3));
    repaint();
}

bool LcdDisplay::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (ToneDeckProcessor::kindForFile (File (f)) != SlotKind::None)
            return true;
    return false;
}

void LcdDisplay::filesDropped (const StringArray& files, int x, int y)
{
    dragOver = false;
    const int target = blockAt ({ (float) x, (float) y });
    for (const auto& path : files)
    {
        const File f (path);
        const auto kind = ToneDeckProcessor::kindForFile (f);
        int slot = -1;
        if (kind == SlotKind::IR)
            slot = Cab;
        else if (kind == SlotKind::Model)
            slot = (target == Pedal || (target < 0 && selected == Pedal)) ? Pedal : Amp;
        if (slot >= 0 && onFileDropped)
            onFileDropped (slot, f);
    }
    repaint();
}
} // namespace td::ui
