#include "Widgets.h"

namespace mm
{

const Palette& paletteFor (const juce::Component& c)
{
    if (auto* lnf = dynamic_cast<MuddLookAndFeel*> (&c.getLookAndFeel()))
        return lnf->getPalette();

    static MuddLookAndFeel fallback;
    return fallback.getPalette();
}

juce::String describeAddResult (ModMatrix::AddResult r)
{
    switch (r)
    {
        case ModMatrix::AddResult::added:     return {};
        case ModMatrix::AddResult::duplicate: return "That source already modulates this parameter.";
        case ModMatrix::AddResult::cycle:     return "Not connected: that would create a modulation loop.";
        case ModMatrix::AddResult::invalid:   return "That parameter can't be modulated.";
    }
    return {};
}

//==============================================================================
Section::Section (const juce::String& title) : titleText (title) {}

void Section::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (p.panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (p.outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    if (titleText.isNotEmpty())
    {
        g.setColour (p.text);
        g.setFont (MuddLookAndFeel::font (12.0f, true));
        g.drawText (titleText.toUpperCase(), getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (8),
                    juce::Justification::centredLeft);
    }
}

juce::Rectangle<int> Section::getContentBounds() const
{
    // In the parent's coordinates: sections are backgrounds, and the controls
    // drawn on them are siblings rather than children.
    return getBounds().reduced (6).withTrimmedTop (titleText.isNotEmpty() ? headerHeight - 4 : 0);
}

//==============================================================================
Knob::Knob (ModdMuddProcessor& p, const juce::String& id, const juce::String& text, int internal, bool bipolar)
    : processor (p), paramId (id), label (text), internalTarget (internal)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setPopupDisplayEnabled (true, true, nullptr, 1500);
    slider.setTitle (text);
    slider.setWantsKeyboardFocus (true);
    slider.onPopup = [this] { showMenu(); };
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getValueTreeState(), id, slider);

    if (internalTarget >= 0)
    {
        processor.addChangeListener (this);
        updateModRing();
    }
}

Knob::~Knob()
{
    processor.removeChangeListener (this);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (16);
    const int size = juce::jmin (r.getWidth(), r.getHeight(), 76);
    slider.setBounds (r.withSizeKeepingCentre (size, size).withY (r.getBottom() - size));
}

void Knob::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    g.setColour (p.textDim);
    g.setFont (MuddLookAndFeel::font (11.5f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (16), juce::Justification::centred, 1);
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (paletteFor (*this).active.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    return internalTarget >= 0 && sourceFromDragId (d.description) >= 0;
}

void Knob::itemDropped (const SourceDetails& d)
{
    dragOver = false;
    repaint();

    Connection c;
    c.source = sourceFromDragId (d.description);
    c.dest = Destination::internalParam (internalTarget);
    c.depth = 0.5f;

    const auto r = processor.addConnection (c);
    if (r != ModMatrix::AddResult::added)
        processor.setStatus (describeAddResult (r));
}

void Knob::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateModRing();
}

void Knob::updateModRing()
{
    float total = 0.0f;
    for (auto* c : processor.getMatrix().getConnectionsTo (Destination::internalParam (internalTarget)))
        if (c->enabled)
            total += c->depth;

    if (std::abs (total) > 1.0e-4f)
        slider.getProperties().set ("modAmount", total);
    else
        slider.getProperties().remove ("modAmount");

    slider.repaint();
}

void Knob::showMenu()
{
    const int index = processor.findParameterIndex (paramId);
    const int cc = processor.getCcForParameter (index);

    juce::PopupMenu m;
    m.addSectionHeader (label);
    m.addItem (1, "MIDI Learn");
    if (cc >= 0)
        m.addItem (2, "Clear MIDI (CC " + juce::String (cc) + ")");

    int numMods = 0;
    if (internalTarget >= 0)
    {
        numMods = (int) processor.getMatrix().getConnectionsTo (Destination::internalParam (internalTarget)).size();
        m.addSeparator();
        m.addItem (3, "Remove modulation (" + juce::String (numMods) + ")", numMods > 0);
    }

    juce::Component::SafePointer<Knob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe, index] (int result)
    {
        if (safe == nullptr)
            return;

        auto& proc = safe->processor;
        if (result == 1)
        {
            proc.startMidiLearn (index);
            proc.setStatus ("MIDI learn: move a controller to assign it to " + safe->label);
        }
        else if (result == 2)
        {
            proc.clearMidiForParameter (index);
            proc.setStatus ({});
        }
        else if (result == 3)
        {
            proc.removeConnectionsTo (Destination::internalParam (safe->internalTarget));
        }
    });
}

//==============================================================================
ParamToggle::ParamToggle (ModdMuddProcessor& p, const juce::String& id, const juce::String& buttonText)
    : juce::ToggleButton (buttonText), attachment (p.getValueTreeState(), id, *this)
{
    setTitle (buttonText);
}

ParamChoice::ParamChoice (ModdMuddProcessor& p, const juce::String& id)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.getValueTreeState().getParameter (id)))
    {
        addItemList (choice->choices, 1);
        setTitle (choice->getName (64));
    }

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.getValueTreeState(), id, *this);
}

//==============================================================================
SourceChip::SourceChip (ModdMuddProcessor& p, int src, const juce::String& t)
    : processor (p), source (src), text (t.isNotEmpty() ? t : getSourceName (src))
{
    setTooltip ("Drag onto a parameter to modulate it, or click and then move a control in the plug-in");
    setTitle (getSourceName (src) + " source");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (30);
}

void SourceChip::timerCallback()
{
    const float v = processor.getEngine().sourceDisplay[(size_t) source].load (std::memory_order_relaxed);
    if (std::abs (v - shownValue) > 0.002f || processor.getArmedSource() == source)
    {
        shownValue = v;
        repaint();
    }
}

void SourceChip::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool armed = processor.getArmedSource() == source;

    g.setColour (armed ? p.active.withAlpha (0.25f) : p.panelHeader);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (armed ? p.active : p.outline);
    g.drawRoundedRectangle (r, 4.0f, armed ? 2.0f : 1.0f);

    // Crosshair "plug" icon
    auto icon = r.removeFromLeft (r.getHeight()).reduced (5.0f);
    g.setColour (p.value);
    g.drawEllipse (icon, 1.5f);
    g.drawLine (icon.getCentreX(), icon.getY() - 2.0f, icon.getCentreX(), icon.getBottom() + 2.0f, 1.2f);
    g.drawLine (icon.getX() - 2.0f, icon.getCentreY(), icon.getRight() + 2.0f, icon.getCentreY(), 1.2f);

    // Live value bar
    auto bar = r.removeFromBottom (4.0f).reduced (4.0f, 0.0f);
    g.setColour (p.inset);
    g.fillRect (bar);
    const bool bipolar = processor.getEngine().isSourceBipolar (source);
    const float v = juce::jlimit (-1.0f, 1.0f, shownValue);
    g.setColour (p.active);
    if (bipolar)
    {
        const float mid = bar.getCentreX();
        const float x = mid + v * bar.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float> (juce::jmin (mid, x), bar.getY(), std::abs (x - mid), bar.getHeight()));
    }
    else
    {
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
    }

    g.setColour (p.text);
    g.setFont (MuddLookAndFeel::font (12.0f, true));
    g.drawFittedText (armed ? "Move a control..." : text, r.toNearestInt().withTrimmedRight (4), juce::Justification::centredLeft, 1);
}

void SourceChip::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void SourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 4)
    {
        dragging = true;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            container->startDragging (sourceDragId (source), this);
    }
}

void SourceChip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
        return;

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader (getSourceName (source));
        m.addItem (1, "Assign by touch");
        m.addItem (2, "Remove all connections from " + getSourceName (source));

        juce::Component::SafePointer<SourceChip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->processor.armAssign (safe->source);
            if (r == 2) safe->processor.removeConnectionsFrom (safe->source);
        });
        return;
    }

    if (processor.getArmedSource() == source)
    {
        processor.disarmAssign();
        processor.setStatus ({});
    }
    else
    {
        processor.armAssign (source);
        processor.setStatus ("Move a control in the plug-in to modulate it with " + getSourceName (source)
                             + ". Click the source again to cancel.");
    }

    repaint();
}

} // namespace mm
