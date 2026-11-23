#include "Widgets.h"
#include "MatrixPanel.h"

namespace mm
{

const Palette& paletteFor (const juce::Component& c)
{
    if (auto* lnf = dynamic_cast<MuddLookAndFeel*> (&c.getLookAndFeel()))
        return lnf->getPalette();

    // Components can ask before they are added to the editor. Keep only a
    // copy of the default colours: a LookAndFeel object in a static would be
    // destroyed at process exit, after JUCE itself has shut down, which can
    // crash the host on exit.
    static const Palette fallback = [] { MuddLookAndFeel lnf; return lnf.getPalette(); }();
    return fallback;
}

void refreshShowing (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (! child->isVisible())
            continue;

        if (auto* r = dynamic_cast<Refreshable*> (child))
            r->refresh();

        refreshShowing (*child);
    }
}

std::vector<juce::Rectangle<int>> flexRects (juce::Rectangle<int> area, const std::vector<std::pair<int, int>>& sizes, int gap)
{
    std::vector<juce::Rectangle<int>> result;
    if (sizes.empty())
        return result;

    int totalPref = 0, totalMin = 0;
    for (auto [pref, min] : sizes)
    {
        totalPref += pref;
        totalMin += juce::jmin (min, pref);
    }

    const int available = juce::jmax (0, area.getWidth() - gap * ((int) sizes.size() - 1));

    for (auto [pref, min] : sizes)
    {
        min = juce::jmin (min, pref);
        int w;

        if (available >= totalPref)
            w = pref;
        else if (available >= totalMin && totalPref > totalMin)
            w = pref - (int) std::lround ((double) (pref - min) * (totalPref - available) / (double) (totalPref - totalMin));
        else
            w = totalMin > 0 ? (int) ((double) min * available / totalMin) : 0;

        result.push_back (area.removeFromLeft (juce::jmax (0, w)));
        area.removeFromLeft (gap);
    }

    return result;
}

void layoutFlex (juce::Rectangle<int> area, std::initializer_list<FlexItem> items, int gap)
{
    std::vector<std::pair<int, int>> sizes;
    for (auto& i : items)
        sizes.emplace_back (i.preferred, i.minimum);

    const auto rects = flexRects (area, sizes, gap);
    size_t k = 0;

    for (auto& i : items)
    {
        if (i.component != nullptr)
        {
            const auto rect = rects[k];
            i.component->setBounds (rect.getWidth() >= 8 ? rect : rect.withWidth (0));
        }
        ++k;
    }
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

static constexpr int knobLabelHeight = 16;

void Knob::resized()
{
    // Knob and label stay together, centred in whatever space the panel gives.
    auto r = getLocalBounds();
    const int size = juce::jmax (16, juce::jmin (r.getWidth(), r.getHeight() - knobLabelHeight, 76));
    const int top = r.getY() + (r.getHeight() - size - knobLabelHeight) / 2;
    slider.setBounds (r.getCentreX() - size / 2, top, size, size);
}

void Knob::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    g.setColour (p.textDim);
    g.setFont (MuddLookAndFeel::font (11.5f));
    g.drawFittedText (label, getLocalBounds().withY (slider.getBottom()).withHeight (knobLabelHeight),
                      juce::Justification::centred, 1);
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
//==============================================================================
/** The targets of one source, with depth and remove, shown in a pop-up from
    the source's handle. */
class TargetList final : public juce::Component,
                         private juce::ChangeListener
{
public:
    TargetList (ModdMuddProcessor& p, int src) : processor (p), source (src)
    {
        title.setText (getSourceName (src) + " targets", juce::dontSendNotification);
        title.setFont (MuddLookAndFeel::font (13.0f, true));
        addAndMakeVisible (title);

        empty.setText ("No targets yet. Drag the handle onto a parameter, or click it and move a control in the plug-in.",
                       juce::dontSendNotification);
        empty.setFont (MuddLookAndFeel::font (12.5f));
        empty.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (empty);

        rebuild();
        const int rows = juce::jlimit (1, 8, (int) ids.size());
        setSize (400, 30 + rows * 30 + 8);
        processor.addChangeListener (this);
    }

    ~TargetList() override  { processor.removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 4);
        title.setBounds (r.removeFromTop (24));
        empty.setBounds (r);

        for (auto* row : rows)
            row->setBounds (r.removeFromTop (30));
    }

private:
    struct Row final : juce::Component
    {
        Row (ModdMuddProcessor& p, int connectionId) : processor (p), id (connectionId)
        {
            name.setFont (MuddLookAndFeel::font (12.5f));
            name.setMinimumHorizontalScale (0.7f);
            depth.setSliderStyle (juce::Slider::LinearHorizontal);
            depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
            depth.setRange (-1.0, 1.0, 0.01);
            depth.setTitle ("Depth");
            depth.onDragEnd = [this] { push(); };
            depth.onValueChange = [this] { if (! depth.isMouseButtonDown()) push(); };
            remove.setButtonText ("X");
            remove.setTitle ("Remove target");
            remove.onClick = [this]
            {
                juce::WeakReference<ModdMuddProcessor> weak (&processor);
                const int cid = id;
                juce::MessageManager::callAsync ([weak, cid] { if (auto* proc = weak.get()) proc->removeConnection (cid); });
            };

            addAndMakeVisible (name);
            addAndMakeVisible (depth);
            addAndMakeVisible (remove);
            update();
        }

        void update()
        {
            if (const auto* c = processor.getMatrix().find (id))
            {
                name.setText (describeDestination (processor, c->dest), juce::dontSendNotification);
                name.setTooltip (name.getText());
                if (! depth.isMouseButtonDown())
                    depth.setValue (c->depth, juce::dontSendNotification);
            }
        }

        void push()
        {
            if (const auto* existing = processor.getMatrix().find (id))
            {
                auto c = *existing;
                c.depth = (float) depth.getValue();
                processor.updateConnection (c);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 3);
            remove.setBounds (r.removeFromRight (26));
            r.removeFromRight (4);
            depth.setBounds (r.removeFromRight (150));
            name.setBounds (r);
        }

        ModdMuddProcessor& processor;
        const int id;
        juce::Label name;
        juce::Slider depth;
        juce::TextButton remove;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override   { rebuild(); }

    void rebuild()
    {
        std::vector<int> now;
        for (auto& c : processor.getMatrix().getConnections())
            if (c.source == source)
                now.push_back (c.id);

        if (now != ids)
        {
            ids = now;
            rows.clear();
            for (int id : ids)
                if ((int) rows.size() < 8)
                    addAndMakeVisible (rows.add (new Row (processor, id)));

            resized();
        }
        else
        {
            for (auto* row : rows)
                row->update();
        }

        empty.setVisible (ids.empty());
    }

    ModdMuddProcessor& processor;
    int source;
    juce::Label title, empty;
    juce::OwnedArray<Row> rows;
    std::vector<int> ids;
};

std::unique_ptr<juce::Component> createTargetList (ModdMuddProcessor& p, int source)
{
    return std::make_unique<TargetList> (p, source);
}

//==============================================================================
SourceChip::SourceChip (ModdMuddProcessor& p, int src, const juce::String& t)
    : processor (p), source (src), text (t.isNotEmpty() ? t : getSourceName (src))
{
    setTooltip ("Drag onto a parameter to modulate it, or click and then move a control in the plug-in. "
                "Click the number to see and adjust this source's targets.");
    setTitle (getSourceName (src) + " source");
    setDescription ("Space: assign by touch. Enter: show targets.");
    targetCount = countTargets();
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

int SourceChip::countTargets() const
{
    int n = 0;
    for (auto& c : processor.getMatrix().getConnections())
        n += c.source == source ? 1 : 0;
    return n;
}

juce::Rectangle<int> SourceChip::badgeArea() const
{
    return getLocalBounds().removeFromRight (24).reduced (2, 4).withTrimmedBottom (2);
}

void SourceChip::refresh()
{
    const float v = processor.getEngine().sourceDisplay[(size_t) source].load (std::memory_order_relaxed);
    const int count = countTargets();

    if (std::abs (v - shownValue) > 0.002f || processor.getArmedSource() == source || count != targetCount)
    {
        shownValue = v;
        targetCount = count;
        repaint();
    }
}

void SourceChip::showTargets()
{
    auto* top = getTopLevelComponent();
    auto list = createTargetList (processor, source);
    juce::CallOutBox::launchAsynchronously (std::move (list), top->getLocalArea (this, getLocalBounds()), top);
}

bool SourceChip::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey)
    {
        showTargets();
        return true;
    }

    if (k == juce::KeyPress::spaceKey)
    {
        if (processor.getArmedSource() == source) processor.disarmAssign();
        else                                      processor.armAssign (source);
        repaint();
        return true;
    }

    return false;
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

    // Number of targets; click it to see and adjust them.
    auto textArea = r.toNearestInt().withTrimmedRight (4);
    if (targetCount > 0 && getWidth() >= 70)
    {
        const auto badge = badgeArea().toFloat();
        g.setColour (p.value);
        g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);
        g.setColour (juce::Colours::black);
        g.setFont (MuddLookAndFeel::font (11.0f, true));
        g.drawText (juce::String (targetCount), badge, juce::Justification::centred);
        textArea = textArea.withRight (badgeArea().getX() - 2);
    }

    g.setColour (p.text);
    g.setFont (MuddLookAndFeel::font (12.0f, true));
    g.drawFittedText (armed ? "Move a control..." : text, textArea, juce::Justification::centredLeft, 1);
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
        m.addItem (3, "Show targets (" + juce::String (targetCount) + ")");
        m.addItem (2, "Remove all connections from " + getSourceName (source), targetCount > 0);

        juce::Component::SafePointer<SourceChip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->processor.armAssign (safe->source);
            if (r == 2) safe->processor.removeConnectionsFrom (safe->source);
            if (r == 3) safe->showTargets();
        });
        return;
    }

    if (targetCount > 0 && getWidth() >= 70 && badgeArea().contains (e.getPosition()))
    {
        showTargets();
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
