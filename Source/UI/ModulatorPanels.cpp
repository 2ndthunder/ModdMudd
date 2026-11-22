#include "ModulatorPanels.h"

namespace mm
{

static void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap = 4)
{
    const int n = (int) items.size();
    if (n == 0)
        return;

    const int w = (area.getWidth() - gap * (n - 1)) / n;
    for (auto* c : items)
    {
        if (c != nullptr)
            c->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }
}

static void setupLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setFont (MuddLookAndFeel::font (11.5f));
}

//==============================================================================
XYPadComponent::XYPadComponent (ModdMuddProcessor& p, const juce::String& x, const juce::String& y,
                                std::function<juce::Point<float>()> livePos, bool yUpIsOne, juce::StringArray c)
    : processor (p), xId (x), yId (y), live (std::move (livePos)), yUp (yUpIsOne), corners (std::move (c))
{
    setWantsKeyboardFocus (true);
    setTitle ("XY pad");
    startTimerHz (30);
}

juce::RangedAudioParameter* XYPadComponent::param (const juce::String& id) const
{
    return processor.getValueTreeState().getParameter (id);
}

juce::Point<float> XYPadComponent::toScreen (float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    return { r.getX() + x * r.getWidth(), yUp ? r.getBottom() - y * r.getHeight() : r.getY() + y * r.getHeight() };
}

void XYPadComponent::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto bounds = getLocalBounds().toFloat();

    g.setColour (p.inset);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (hasKeyboardFocus (false) ? p.active : p.outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Grid
    g.setColour (p.outline.brighter (0.15f));
    auto inner = bounds.reduced (8.0f);
    for (int i = 1; i < 4; ++i)
    {
        const float fx = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float fy = inner.getY() + inner.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine ((int) fx, inner.getY(), inner.getBottom());
        g.drawHorizontalLine ((int) fy, inner.getX(), inner.getRight());
    }

    if (centreX.isNotEmpty())
    {
        const auto c = toScreen (param (centreX)->getValue(), param (centreY)->getValue());
        g.setColour (p.value.withAlpha (0.6f));
        g.drawLine (c.x - 8.0f, c.y, c.x + 8.0f, c.y, 1.5f);
        g.drawLine (c.x, c.y - 8.0f, c.x, c.y + 8.0f, 1.5f);
    }

    if (corners.size() == 4)
    {
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (13.0f, true));
        auto lr = bounds.reduced (6.0f);
        g.drawText (corners[0], lr, juce::Justification::topLeft);
        g.drawText (corners[1], lr, juce::Justification::topRight);
        g.drawText (corners[2], lr, juce::Justification::bottomLeft);
        g.drawText (corners[3], lr, juce::Justification::bottomRight);
    }

    // Set position (ring) and actual, modulated/smoothed position (dot)
    const auto set = toScreen (param (xId)->getValue(), param (yId)->getValue());
    g.setColour (p.text.withAlpha (0.7f));
    g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (set), 1.5f);

    if (live)
    {
        const auto lp = live();
        const auto pos = toScreen (lp.x, lp.y);
        g.setColour (p.active);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (pos));
    }
}

void XYPadComponent::setFromPosition (juce::Point<float> pos)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    const float x = juce::jlimit (0.0f, 1.0f, (pos.x - r.getX()) / r.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, (pos.y - r.getY()) / r.getHeight());
    if (yUp)
        y = 1.0f - y;

    param (xId)->setValueNotifyingHost (x);
    param (yId)->setValueNotifyingHost (y);
}

void XYPadComponent::mouseDown (const juce::MouseEvent& e)
{
    param (xId)->beginChangeGesture();
    param (yId)->beginChangeGesture();
    setFromPosition (e.position);
}

void XYPadComponent::mouseDrag (const juce::MouseEvent& e)   { setFromPosition (e.position); }

void XYPadComponent::mouseUp (const juce::MouseEvent&)
{
    param (xId)->endChangeGesture();
    param (yId)->endChangeGesture();
}

bool XYPadComponent::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 0.1f : 0.01f;
    float dx = 0.0f, dy = 0.0f;

    if (k.isKeyCode (juce::KeyPress::leftKey))  dx = -step;
    if (k.isKeyCode (juce::KeyPress::rightKey)) dx = step;
    if (k.isKeyCode (juce::KeyPress::upKey))    dy = yUp ? step : -step;
    if (k.isKeyCode (juce::KeyPress::downKey))  dy = yUp ? -step : step;

    if (juce::exactlyEqual (dx, 0.0f) && juce::exactlyEqual (dy, 0.0f))
        return false;

    auto* px = param (xId);
    auto* py = param (yId);
    px->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, px->getValue() + dx));
    py->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, py->getValue() + dy));
    return true;
}

//==============================================================================
WaveView::WaveView (ModdMuddProcessor& p, int lfoIndex)
    : processor (p), lfo (lfoIndex), table ((size_t) kLfoTableSize)
{
    points = processor.getCustomWave (lfo);
    Lfo::buildCustomTable (points, table.data());
    setTitle ("LFO " + juce::String (lfo + 1) + " waveform");
    setTooltip ("Custom shape: drag points, double-click to add or remove, Alt-drag to bend a segment");
    startTimerHz (30);
}

bool WaveView::isCustom() const
{
    return juce::roundToInt (processor.getValueTreeState().getRawParameterValue (ids::lfo (lfo, "shape"))->load())
               == (int) LfoShape::custom;
}

juce::Point<float> WaveView::pointToScreen (const WavePoint& wp) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    return { r.getX() + wp.x * r.getWidth(), r.getCentreY() - wp.y * r.getHeight() * 0.5f };
}

int WaveView::hitPoint (juce::Point<float> pos) const
{
    for (size_t i = 0; i < points.size(); ++i)
        if (pointToScreen (points[i]).getDistanceFrom (pos) < 8.0f)
            return (int) i;

    return -1;
}

void WaveView::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto bounds = getLocalBounds().toFloat();
    g.setColour (p.inset);
    g.fillRoundedRectangle (bounds, 4.0f);

    auto r = bounds.reduced (6.0f);
    g.setColour (p.outline.brighter (0.15f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const auto shape = (LfoShape) juce::roundToInt (processor.getValueTreeState().getRawParameterValue (ids::lfo (lfo, "shape"))->load());

    juce::Path path;
    const int n = juce::jmax (2, (int) r.getWidth());
    for (int i = 0; i <= n; ++i)
    {
        const double ph = (double) i / n;
        const float v = Lfo::evaluate (shape, ph, table.data());
        const float x = r.getX() + (float) ph * r.getWidth();
        const float y = r.getCentreY() - v * r.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }

    g.setColour (p.active);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Play position
    const float phase = processor.getEngine().lfoPhaseDisplay[(size_t) lfo].load (std::memory_order_relaxed);
    const float px = r.getX() + phase * r.getWidth();
    g.setColour (p.value);
    g.drawVerticalLine ((int) px, r.getY(), r.getBottom());

    if (shape == LfoShape::custom)
    {
        for (auto& wp : points)
        {
            const auto sp = pointToScreen (wp);
            g.setColour (p.panel);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (sp));
            g.setColour (p.value);
            g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (sp), 1.5f);
        }
    }
}

void WaveView::commit()
{
    Lfo::buildCustomTable (points, table.data());
    processor.setCustomWave (lfo, points);
    repaint();
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (! isCustom())
        return;

    dragIndex = hitPoint (e.position);
    curveDrag = false;

    if (dragIndex < 0 && e.mods.isAltDown())
    {
        // Bend the segment under the mouse.
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        const float x = (e.position.x - r.getX()) / r.getWidth();
        auto sorted = points;
        std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        points = sorted;

        for (int i = (int) points.size() - 1; i >= 0; --i)
            if (points[(size_t) i].x <= x) { dragIndex = i; break; }

        if (dragIndex < 0)
            dragIndex = (int) points.size() - 1;

        curveDrag = true;
        curveStart = points[(size_t) dragIndex].curve;
    }
    else if (dragIndex >= 0 && e.mods.isPopupMenu() && points.size() > 2)
    {
        points.erase (points.begin() + dragIndex);
        dragIndex = -1;
        commit();
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || ! isCustom())
        return;

    auto r = getLocalBounds().toFloat().reduced (6.0f);
    auto& wp = points[(size_t) dragIndex];

    if (curveDrag)
    {
        wp.curve = juce::jlimit (-1.0f, 1.0f, curveStart - (float) e.getDistanceFromDragStartY() / 80.0f);
    }
    else
    {
        wp.x = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        wp.y = juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f));
    }

    Lfo::buildCustomTable (points, table.data());
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex >= 0 && isCustom())
        commit();

    dragIndex = -1;
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isCustom())
        return;

    const int hit = hitPoint (e.position);
    if (hit >= 0)
    {
        if (points.size() > 2)
            points.erase (points.begin() + hit);
    }
    else if ((int) points.size() < kMaxCustomPoints)
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        points.push_back ({ juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()),
                            juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f)), 0.0f });
    }

    commit();
}

//==============================================================================
LfoPanel::LfoPanel (ModdMuddProcessor& p, int i)
    : processor (p), index (i),
      section ("LFO " + juce::String (i + 1)), controls ("Settings"),
      wave (p, i),
      shape (p, ids::lfo (i, "shape")), division (p, ids::lfo (i, "div")), retrig (p, ids::lfo (i, "retrig")),
      rate (p, ids::lfo (i, "rate"), "Rate", lfoInternal (i, 0)),
      phase (p, ids::lfo (i, "phase"), "Phase", lfoInternal (i, 2)),
      depth (p, ids::lfo (i, "depth"), "Depth", lfoInternal (i, 1)),
      offset (p, ids::lfo (i, "offset"), "Offset", -1, true),
      fade (p, ids::lfo (i, "fade"), "Fade In"),
      sync (p, ids::lfo (i, "sync"), "Sync"),
      invert (p, ids::lfo (i, "invert"), "Invert"),
      chip (p, srcLfo0 + i)
{
    for (auto* c : std::initializer_list<juce::Component*> { &section, &controls, &wave, &shape, &division, &retrig, &rate,
                                                            &phase, &depth, &offset, &fade, &sync, &invert, &chip,
                                                            &shapeLabel, &retrigLabel, &divisionLabel })
        addAndMakeVisible (c);

    setupLabel (shapeLabel, "Shape");
    setupLabel (retrigLabel, "Retrigger");
    setupLabel (divisionLabel, "Division");
    startTimerHz (10);
    timerCallback();
}

void LfoPanel::timerCallback()
{
    const bool synced = processor.getValueTreeState().getRawParameterValue (ids::lfo (index, "sync"))->load() >= 0.5f;
    division.setVisible (synced);
    divisionLabel.setVisible (synced);
    rate.setEnabled (! synced);
}

void LfoPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jlimit (170, 420, r.getWidth() * 2 / 5));
    section.setBounds (left);
    auto content = section.getContentBounds();
    chip.setBounds (content.removeFromBottom (28));
    content.removeFromBottom (4);
    wave.setBounds (content);

    r.removeFromLeft (6);
    controls.setBounds (r);
    auto c = controls.getContentBounds();

    // Labels go first when space is short; the menus show their own values.
    auto topRow = c.removeFromTop (24);
    const bool roomy = topRow.getWidth() >= 470;
    layoutFlex (topRow, { { &shapeLabel, roomy ? 40 : 0, 0 }, { &shape, 110, 76 },
                          { &retrigLabel, roomy ? 60 : 0, 0 }, { &retrig, 96, 70 },
                          { &divisionLabel, roomy ? 52 : 0, 0 }, { &division, 80, 58 } });

    c.removeFromTop (6);
    layoutFlex (c.removeFromBottom (24), { { &sync, 80, 62 }, { &invert, 90, 70 } });

    layoutRow (c, { &rate, &phase, &depth, &offset, &fade });
}

//==============================================================================
void RandomPanel::History::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (p.inset);
    g.fillRoundedRectangle (r, 4.0f);
    r = r.reduced (6.0f);
    g.setColour (p.outline.brighter (0.15f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    juce::Path path;
    const int n = (int) values.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = values[(size_t) ((write + i) % n)];
        const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
        const float y = r.getCentreY() - v * r.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }

    g.setColour (p.active);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

RandomPanel::RandomPanel (ModdMuddProcessor& p, int i)
    : processor (p), index (i),
      section ("Random " + juce::String (i + 1)), controls ("Settings"),
      division (p, ids::rnd (i, "div")),
      rate (p, ids::rnd (i, "rate"), "Rate", randomInternal (i, 0)),
      probability (p, ids::rnd (i, "prob"), "Chance"),
      glide (p, ids::rnd (i, "glide"), "Glide"),
      depth (p, ids::rnd (i, "depth"), "Depth", randomInternal (i, 1)),
      offset (p, ids::rnd (i, "offset"), "Offset", -1, true),
      sync (p, ids::rnd (i, "sync"), "Sync"),
      smooth (p, ids::rnd (i, "smooth"), "Smooth"),
      independent (p, ids::rnd (i, "indep"), "Independent"),
      chip (p, srcRandom0 + i)
{
    for (auto* c : std::initializer_list<juce::Component*> { &section, &controls, &history, &division, &rate, &probability,
                                                            &glide, &depth, &offset, &sync, &smooth, &independent,
                                                            &seedButton, &presetsButton, &chip })
        addAndMakeVisible (c);

    independent.setTooltip ("Give each destination its own random stream");
    seedButton.setTooltip ("The seed makes the random sequence repeatable. Click for a new one.");
    seedButton.onClick = [this]
    {
        processor.setRandomSeed (index, (uint32_t) juce::Random::getSystemRandom().nextInt());
        seedButton.setButtonText ("Seed " + juce::String (processor.getRandomSeed (index) % 100000u));
    };
    seedButton.setButtonText ("Seed " + juce::String (processor.getRandomSeed (index) % 100000u));

    presetsButton.setButtonText ("Presets");
    presetsButton.onClick = [this] { showPresets(); };

    startTimerHz (30);
}

void RandomPanel::applyPreset (std::initializer_list<std::pair<const char*, float>> values)
{
    auto& s = processor.getValueTreeState();
    for (auto& [what, value] : values)
        if (auto* param = s.getParameter (ids::rnd (index, what)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
}

void RandomPanel::showPresets()
{
    juce::PopupMenu m;
    m.addItem (1, "Gentle drift");
    m.addItem (2, "Sample and hold");
    m.addItem (3, "Rhythmic 1/16");
    m.addItem (4, "Sparse jumps");
    m.addItem (5, "Chaos");

    juce::Component::SafePointer<RandomPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        switch (r)
        {
            case 1: safe->applyPreset ({ { "sync", 0 }, { "rate", 0.2f }, { "smooth", 1 }, { "prob", 1 }, { "depth", 0.4f } }); break;
            case 2: safe->applyPreset ({ { "sync", 0 }, { "rate", 4 }, { "smooth", 0 }, { "glide", 0.02f }, { "prob", 1 }, { "depth", 1 } }); break;
            case 3: safe->applyPreset ({ { "sync", 1 }, { "div", 13 }, { "smooth", 0 }, { "glide", 0.05f }, { "prob", 1 }, { "depth", 0.8f } }); break;
            case 4: safe->applyPreset ({ { "sync", 1 }, { "div", 7 }, { "smooth", 0 }, { "glide", 0.2f }, { "prob", 0.3f }, { "depth", 1 } }); break;
            case 5: safe->applyPreset ({ { "sync", 0 }, { "rate", 18 }, { "smooth", 0 }, { "glide", 0.0f }, { "prob", 1 }, { "depth", 1 } }); break;
            default: break;
        }
    });
}

void RandomPanel::timerCallback()
{
    history.values[(size_t) history.write] = processor.getEngine().sourceDisplay[(size_t) (srcRandom0 + index)].load (std::memory_order_relaxed);
    history.write = (history.write + 1) % (int) history.values.size();
    history.repaint();

    const bool synced = processor.getValueTreeState().getRawParameterValue (ids::rnd (index, "sync"))->load() >= 0.5f;
    division.setVisible (synced);
    rate.setEnabled (! synced);
    glide.setEnabled (processor.getValueTreeState().getRawParameterValue (ids::rnd (index, "smooth"))->load() < 0.5f);
}

void RandomPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jlimit (170, 420, r.getWidth() * 2 / 5));
    section.setBounds (left);
    auto content = section.getContentBounds();
    chip.setBounds (content.removeFromBottom (28));
    content.removeFromBottom (4);
    history.setBounds (content);

    r.removeFromLeft (6);
    controls.setBounds (r);
    auto c = controls.getContentBounds();

    layoutFlex (c.removeFromTop (24), { { &sync, 70, 60 }, { &division, 80, 58 }, { nullptr, 400, 0 },
                                        { &seedButton, 100, 72 }, { &presetsButton, 80, 62 } });

    c.removeFromTop (6);
    layoutFlex (c.removeFromBottom (24), { { &smooth, 90, 74 }, { &independent, 130, 104 } });

    layoutRow (c, { &rate, &probability, &glide, &depth, &offset });
}

//==============================================================================
MacroPanel::MacroPanel (ModdMuddProcessor& p) : section ("Macros")
{
    addAndMakeVisible (section);

    for (int i = 0; i < kNumMacros; ++i)
    {
        auto* k = knobs.add (new Knob (p, ids::macro (i), "Macro " + juce::String (i + 1), ipMacroAmount0 + i));
        k->slider.setTooltip ("Drop a modulator here to scale this macro's output");
        addAndMakeVisible (k);
        addAndMakeVisible (chips.add (new SourceChip (p, srcMacro0 + i, "M" + juce::String (i + 1))));
    }
}

void MacroPanel::resized()
{
    section.setBounds (getLocalBounds());
    auto r = section.getContentBounds();
    const int w = r.getWidth() / kNumMacros;

    for (int i = 0; i < kNumMacros; ++i)
    {
        auto col = r.removeFromLeft (w).reduced (3, 0);
        chips[i]->setBounds (col.removeFromBottom (28));
        col.removeFromBottom (4);
        knobs[i]->setBounds (col);
    }
}

//==============================================================================
XYPanel::XYPanel (ModdMuddProcessor& p)
    : section ("XY Pad"), controls ("Settings"),
      pad (p, ids::xyX, ids::xyY,
           [&p] { return juce::Point<float> (p.getEngine().xyXDisplay.load(), p.getEngine().xyYDisplay.load()); }, true),
      x (p, ids::xyX, "X", ipXyX), y (p, ids::xyY, "Y", ipXyY),
      centreX (p, ids::xyCenterX, "Centre X"), centreY (p, ids::xyCenterY, "Centre Y"),
      glide (p, ids::xyGlide, "Glide"),
      bipolar (p, ids::xyBipolar, "Bipolar"),
      chipX (p, srcXyX), chipY (p, srcXyY)
{
    pad.setCentreIds (ids::xyCenterX, ids::xyCenterY);
    bipolar.setTooltip ("On: output is relative to the centre (-100%..100%). Off: 0..100%");

    for (auto* c : std::initializer_list<juce::Component*> { &section, &controls, &pad, &x, &y, &centreX, &centreY, &glide,
                                                            &bipolar, &chipX, &chipY })
        addAndMakeVisible (c);
}

void XYPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jmin (r.getHeight() + 40, r.getWidth() / 2));
    section.setBounds (left);
    pad.setBounds (section.getContentBounds());

    r.removeFromLeft (6);
    controls.setBounds (r);
    auto c = controls.getContentBounds();
    layoutFlex (c.removeFromBottom (28), { { &chipX, 110, 74 }, { &chipY, 110, 74 }, { &bipolar, 100, 80 } });
    c.removeFromBottom (6);
    layoutRow (c, { &x, &y, &centreX, &centreY, &glide });
}

//==============================================================================
BlendPanel::BlendPanel (ModdMuddProcessor& p)
    : processor (p), section ("Preset Blender"), controls ("Snapshots"),
      pad (p, ids::blendX, ids::blendY,
           [&p] { return juce::Point<float> (p.getEngine().blendXDisplay.load(), p.getEngine().blendYDisplay.load()); },
           false, { "A", "B", "C", "D" }),
      enabled (p, ids::blendOn, "Blend on"),
      blendX (p, ids::blendX, "Blend X", ipBlendX), blendY (p, ids::blendY, "Blend Y", ipBlendY)
{
    for (auto* c : std::initializer_list<juce::Component*> { &section, &controls, &pad, &enabled, &blendX, &blendY, &info })
        addAndMakeVisible (c);

    const char* names[] { "A", "B", "C", "D" };
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        auto& b = snapButtons[(size_t) i];
        b.setButtonText (names[i]);
        b.setTooltip ("Click: capture the current plug-in settings (or recall if already captured). Right-click for options.");
        b.onClick = [this, i] { snapshotClicked (i, juce::ModifierKeys::currentModifiers.isPopupMenu()); };
        addAndMakeVisible (b);
    }

    info.setFont (MuddLookAndFeel::font (12.0f));
    info.setJustificationType (juce::Justification::topLeft);
    processor.addChangeListener (this);
    refresh();
}

BlendPanel::~BlendPanel()
{
    processor.removeChangeListener (this);
}

void BlendPanel::refresh()
{
    int filled = 0;
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        const bool has = processor.hasSnapshot (i);
        snapButtons[(size_t) i].setToggleState (has, juce::dontSendNotification);
        filled += has ? 1 : 0;
    }

    juce::String text;
    if (filled == 0)
        text = "Capture two or more snapshots, then move the pad to morph between them.";
    else
        text << filled << " snapshot" << (filled == 1 ? "" : "s") << ", " << processor.getNumSnapshotValues()
             << " parameters. Switches and menus jump to the nearest snapshot.";

    info.setText (text, juce::dontSendNotification);
}

void BlendPanel::snapshotClicked (int index, bool popup)
{
    const bool has = processor.hasSnapshot (index);

    if (! popup)
    {
        if (has) processor.recallSnapshot (index);
        else     processor.captureSnapshot (index);
        return;
    }

    juce::PopupMenu m;
    m.addItem (1, "Capture current settings");
    m.addItem (2, "Recall", has);
    m.addItem (3, "Clear", has);

    juce::Component::SafePointer<BlendPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapButtons[(size_t) index]), [safe, index] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->processor.captureSnapshot (index);
        if (r == 2) safe->processor.recallSnapshot (index);
        if (r == 3) safe->processor.clearSnapshot (index);
    });
}

void BlendPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jmin (r.getHeight() + 40, r.getWidth() / 2));
    section.setBounds (left);
    pad.setBounds (section.getContentBounds());

    r.removeFromLeft (6);
    controls.setBounds (r);
    auto c = controls.getContentBounds();

    layoutFlex (c.removeFromTop (30), { { &snapButtons[0], 44, 30 }, { &snapButtons[1], 44, 30 }, { &snapButtons[2], 44, 30 },
                                        { &snapButtons[3], 44, 30 }, { &enabled, 110, 86 } });

    c.removeFromTop (6);
    info.setBounds (c.removeFromBottom (36));
    auto knobs = c.withWidth (juce::jmin (c.getWidth(), 180));
    layoutRow (knobs, { &blendX, &blendY });
}

//==============================================================================
void EnvPanel::Meter::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (p.inset);
    g.fillRoundedRectangle (r, 4.0f);
    r = r.reduced (6.0f);

    juce::Path path;
    const int n = (int) values.size();
    path.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i < n; ++i)
    {
        const float v = values[(size_t) ((write + i) % n)];
        path.lineTo (r.getX() + r.getWidth() * (float) i / (float) (n - 1), r.getBottom() - v * r.getHeight());
    }
    path.lineTo (r.getRight(), r.getBottom());
    path.closeSubPath();

    g.setColour (p.active.withAlpha (0.35f));
    g.fillPath (path);
    g.setColour (p.active);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

EnvPanel::EnvPanel (ModdMuddProcessor& p, int i)
    : processor (p), index (i),
      section ("Envelope " + juce::String (i + 1)), controls ("Settings"),
      source (p, ids::env (i, "source")), detector (p, ids::env (i, "detector")),
      attack (p, ids::env (i, "attack"), "Attack"),
      release (p, ids::env (i, "release"), "Release"),
      gain (p, ids::env (i, "gain"), "Gain", envInternal (i, 0), true),
      threshold (p, ids::env (i, "threshold"), "Threshold"),
      sensitivity (p, ids::env (i, "sens"), "Sensitivity"),
      curve (p, ids::env (i, "curve"), "Curve", -1, true),
      smooth (p, ids::env (i, "smooth"), "Smooth"),
      depth (p, ids::env (i, "depth"), "Depth", envInternal (i, 1)),
      invert (p, ids::env (i, "invert"), "Invert"),
      chip (p, srcEnv0 + i)
{
    for (auto* c : std::initializer_list<juce::Component*> { &section, &controls, &meter, &source, &detector, &attack, &release,
                                                            &gain, &threshold, &sensitivity, &curve, &smooth, &depth, &invert,
                                                            &chip, &sourceLabel, &detectorLabel })
        addAndMakeVisible (c);

    setupLabel (sourceLabel, "Listen to");
    setupLabel (detectorLabel, "Detector");

    if constexpr (kIsInstrumentBuild)
        source.setTooltip ("In the instrument version there is no audio input: use Output to follow the hosted instrument");
    else
        source.setTooltip ("Sidechain needs the sidechain input enabled in your DAW");

    startTimerHz (30);
}

void EnvPanel::timerCallback()
{
    meter.values[(size_t) meter.write] = processor.getEngine().envLevelDisplay[(size_t) index].load (std::memory_order_relaxed);
    meter.write = (meter.write + 1) % (int) meter.values.size();
    meter.repaint();
}

void EnvPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jlimit (150, 340, r.getWidth() / 3));
    section.setBounds (left);
    auto content = section.getContentBounds();
    chip.setBounds (content.removeFromBottom (28));
    content.removeFromBottom (4);
    meter.setBounds (content);

    r.removeFromLeft (6);
    controls.setBounds (r);
    auto c = controls.getContentBounds();

    auto topRow = c.removeFromTop (24);
    const bool roomy = topRow.getWidth() >= 440;
    layoutFlex (topRow, { { &sourceLabel, roomy ? 60 : 0, 0 }, { &source, 100, 76 },
                          { &detectorLabel, roomy ? 56 : 0, 0 }, { &detector, 80, 62 }, { &invert, 90, 70 } });

    c.removeFromTop (6);
    layoutRow (c, { &attack, &release, &gain, &threshold, &sensitivity, &curve, &smooth, &depth });
}

} // namespace mm
