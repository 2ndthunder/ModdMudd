#include "MatrixPanel.h"

namespace mm
{

juce::String describeDestination (const ModdMuddProcessor& p, const Destination& d)
{
    if (d.internal)
        return getInternalParamName (d.internalId);

    if (auto* slot = p.findSlot (d.slotUid))
    {
        const auto prefix = p.getSlots().size() > 1 ? juce::String (p.getSlotIndex (d.slotUid) + 1) + " " : juce::String();
        const int pi = slot->findParamIndex (d.paramId);
        return prefix + slot->getDisplayName() + ": " + (pi >= 0 ? slot->getParamName (pi) : "(missing parameter)");
    }

    return "(plug-in not loaded)";
}

void chooseDestination (ModdMuddProcessor& p, juce::Component& target, const juce::String& filter,
                        std::function<void (Destination)> onChosen)
{
    struct Item { Destination dest; juce::String name; };
    std::vector<Item> items;

    juce::PopupMenu menu;
    const auto f = filter.trim();

    // Hosted parameters
    for (auto& slot : p.getSlots())
    {
        if (slot->isMissing())
            continue;

        juce::PopupMenu slotMenu, chunk;
        int inChunk = 0, chunkStart = 0;

        for (int pi = 0; pi < slot->getNumParams(); ++pi)
        {
            if (! slot->isModulatable (pi))
                continue;

            const auto name = slot->getParamName (pi);
            if (f.isNotEmpty() && ! name.containsIgnoreCase (f))
                continue;

            items.push_back ({ Destination::hosted (slot->getUid(), slot->getParamId (pi)), name });
            chunk.addItem ((int) items.size(), name);

            if (++inChunk == 40)
            {
                slotMenu.addSubMenu (juce::String (chunkStart + 1) + "-" + juce::String (chunkStart + inChunk), chunk);
                chunk = {};
                chunkStart += inChunk;
                inChunk = 0;
            }
        }

        if (inChunk > 0)
        {
            if (chunkStart == 0) slotMenu = chunk;
            else slotMenu.addSubMenu (juce::String (chunkStart + 1) + "-" + juce::String (chunkStart + inChunk), chunk);
        }

        if (slotMenu.getNumItems() > 0)
            menu.addSubMenu (slot->getDisplayName(), slotMenu);
    }

    // Modulator settings (cross-modulation)
    juce::PopupMenu internal;
    for (int ip = 0; ip < kNumInternalParams; ++ip)
    {
        const auto name = getInternalParamName (ip);
        if (f.isNotEmpty() && ! name.containsIgnoreCase (f))
            continue;

        items.push_back ({ Destination::internalParam (ip), name });
        internal.addItem ((int) items.size(), name);
    }

    if (internal.getNumItems() > 0)
        menu.addSubMenu ("Modulators", internal);

    if (items.empty())
        menu.addItem (-1, f.isNotEmpty() ? "No matches for \"" + f + "\"" : "Load a plug-in first", false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [items, onChosen] (int r)
                        {
                            if (r > 0 && r <= (int) items.size())
                                onChosen (items[(size_t) r - 1].dest);
                        });
}

//==============================================================================
class MatrixPanel::Row : public juce::Component
{
public:
    explicit Row (ModdMuddProcessor& p) : processor (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &enabled, &name, &depth, &offset, &polarity,
                                                                &rangeMin, &rangeMax, &smoothing, &remove })
            addAndMakeVisible (c);

        enabled.setTitle ("Enabled");
        name.setFont (MuddLookAndFeel::font (12.5f));
        name.setMinimumHorizontalScale (0.7f);

        auto setup = [] (juce::Slider& s, double lo, double hi, const juce::String& title, const juce::String& tip)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
            s.setRange (lo, hi, 0.01);
            s.setTitle (title);
            s.setTooltip (tip);
        };

        setup (depth, -1.0, 1.0, "Depth", "Amount and direction of modulation");
        setup (offset, -1.0, 1.0, "Offset", "Added to this connection's output");
        setup (rangeMin, 0.0, 1.0, "Range minimum", "Lowest value the destination can reach (shared by all its connections)");
        setup (rangeMax, 0.0, 1.0, "Range maximum", "Highest value the destination can reach (shared by all its connections)");
        smoothing.setSliderStyle (juce::Slider::LinearHorizontal);
        smoothing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        smoothing.setRange (0.0, 500.0, 1.0);
        smoothing.setSkewFactorFromMidPoint (40.0);
        smoothing.setTextValueSuffix (" ms");
        smoothing.setTitle ("Smoothing");
        smoothing.setTooltip ("Smooths the destination's movement (shared by all its connections)");

        polarity.addItemList ({ "Natural", "Unipolar", "Bipolar" }, 1);
        polarity.setTitle ("Polarity");
        polarity.setTooltip ("Natural: the source's own range. Unipolar: 0..1. Bipolar: -1..1");

        remove.setButtonText ("X");
        remove.setTitle ("Remove connection");

        enabled.onClick = [this] { push(); };
        depth.onDragEnd = [this] { push(); };
        depth.onValueChange = [this] { if (! depth.isMouseButtonDown()) push(); };
        offset.onDragEnd = [this] { push(); };
        offset.onValueChange = [this] { if (! offset.isMouseButtonDown()) push(); };
        polarity.onChange = [this] { push(); };
        rangeMin.onValueChange = [this] { pushSettings(); };
        rangeMax.onValueChange = [this] { pushSettings(); };
        smoothing.onValueChange = [this] { pushSettings(); };
        remove.onClick = [this]
        {
            // Defer: removing the connection rebuilds the list, which may delete this row.
            juce::WeakReference<ModdMuddProcessor> weak (&processor);
            const int id = connectionId;
            juce::MessageManager::callAsync ([weak, id] { if (auto* proc = weak.get()) proc->removeConnection (id); });
        };
    }

    void setConnection (int id)
    {
        connectionId = id;
        const auto* c = processor.getMatrix().find (id);
        if (c == nullptr)
            return;

        updating = true;
        enabled.setToggleState (c->enabled, juce::dontSendNotification);
        name.setText (getSourceName (c->source) + "  >  " + describeDestination (processor, c->dest), juce::dontSendNotification);
        depth.setValue (c->depth, juce::dontSendNotification);
        offset.setValue (c->offset, juce::dontSendNotification);
        polarity.setSelectedId ((int) c->polarity + 1, juce::dontSendNotification);

        const auto s = processor.getMatrix().getSettings (c->dest);
        rangeMin.setValue (s.rangeMin, juce::dontSendNotification);
        rangeMax.setValue (s.rangeMax, juce::dontSendNotification);
        smoothing.setValue (s.smoothingMs, juce::dontSendNotification);

        const bool hosted = ! c->dest.internal;
        rangeMin.setEnabled (hosted);
        rangeMax.setEnabled (hosted);
        smoothing.setEnabled (hosted);
        updating = false;
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 3);
        enabled.setBounds (r.removeFromLeft (40));
        remove.setBounds (r.removeFromRight (26));
        r.removeFromRight (4);
        smoothing.setBounds (r.removeFromRight (120));
        rangeMax.setBounds (r.removeFromRight (100));
        rangeMin.setBounds (r.removeFromRight (100));
        r.removeFromRight (4);
        polarity.setBounds (r.removeFromRight (86));
        r.removeFromRight (4);
        offset.setBounds (r.removeFromRight (110));
        depth.setBounds (r.removeFromRight (130));
        name.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (paletteFor (*this).outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

private:
    void push()
    {
        if (updating)
            return;

        if (const auto* existing = processor.getMatrix().find (connectionId))
        {
            auto c = *existing;
            c.enabled = enabled.getToggleState();
            c.depth = (float) depth.getValue();
            c.offset = (float) offset.getValue();
            c.polarity = (Polarity) juce::jlimit (0, 2, polarity.getSelectedId() - 1);
            processor.updateConnection (c);
        }
    }

    void pushSettings()
    {
        if (updating)
            return;

        if (const auto* c = processor.getMatrix().find (connectionId))
        {
            DestinationSettings s;
            s.rangeMin = (float) juce::jmin (rangeMin.getValue(), rangeMax.getValue());
            s.rangeMax = (float) juce::jmax (rangeMin.getValue(), rangeMax.getValue());
            s.smoothingMs = (float) smoothing.getValue();
            processor.setDestinationSettings (c->dest, s);
        }
    }

    ModdMuddProcessor& processor;
    int connectionId = 0;
    bool updating = false;
    juce::ToggleButton enabled;
    juce::Label name;
    juce::Slider depth, offset, rangeMin, rangeMax, smoothing;
    juce::ComboBox polarity;
    juce::TextButton remove;
};

//==============================================================================
MatrixPanel::MatrixPanel (ModdMuddProcessor& p) : processor (p)
{
    for (int s = 0; s < kNumSources; ++s)
        sourceBox.addItem (getSourceName (s), s + 1);
    sourceBox.setSelectedId (1, juce::dontSendNotification);
    sourceBox.setTitle ("Source");

    search.setTextToShowWhenEmpty ("Search destinations", paletteFor (*this).textDim);
    search.setTitle ("Search destinations");
    search.onReturnKey = [this] { destButton.triggerClick(); };

    destButton.onClick = [this]
    {
        chooseDestination (processor, destButton, search.getText(), [this] (Destination d)
        {
            chosen = d;
            hasChosen = true;
            destButton.setButtonText (describeDestination (processor, d));
        });
    };

    addButton.onClick = [this]
    {
        if (! hasChosen)
        {
            processor.setStatus ("Choose a destination first.");
            return;
        }

        Connection c;
        c.source = sourceBox.getSelectedId() - 1;
        c.dest = chosen;
        c.depth = 0.5f;
        const auto r = processor.addConnection (c);
        processor.setStatus (describeAddResult (r));
    };

    countLabel.setFont (MuddLookAndFeel::font (12.0f));
    countLabel.setJustificationType (juce::Justification::centredRight);

    list.setModel (this);
    list.setRowHeight (30);
    list.setTitle ("Modulation connections");

    for (auto* c : std::initializer_list<juce::Component*> { &sourceBox, &search, &destButton, &addButton, &countLabel, &list })
        addAndMakeVisible (c);

    processor.addChangeListener (this);
    changeListenerCallback (nullptr);
}

MatrixPanel::~MatrixPanel()
{
    processor.removeChangeListener (this);
}

void MatrixPanel::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);
    g.setColour (p.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Column headings, matching Row::resized()
    auto h = list.getBounds().withHeight (16).translated (0, -17).reduced (2, 0);
    g.setColour (p.textDim);
    g.setFont (MuddLookAndFeel::font (11.0f, true));
    h.removeFromLeft (40);
    h.removeFromRight (30);
    g.drawText ("SMOOTH", h.removeFromRight (120), juce::Justification::centredLeft);
    g.drawText ("MAX", h.removeFromRight (100), juce::Justification::centredLeft);
    g.drawText ("MIN", h.removeFromRight (100), juce::Justification::centredLeft);
    h.removeFromRight (4);
    g.drawText ("POLARITY", h.removeFromRight (86), juce::Justification::centredLeft);
    h.removeFromRight (4);
    g.drawText ("OFFSET", h.removeFromRight (110), juce::Justification::centredLeft);
    g.drawText ("DEPTH", h.removeFromRight (130), juce::Justification::centredLeft);
    g.drawText ("SOURCE  >  DESTINATION", h, juce::Justification::centredLeft);

    if (connectionIds.empty())
    {
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (13.0f));
        g.drawFittedText ("No modulation yet. Drag a source onto a parameter, click a source and move a control in the plug-in, "
                          "or add a connection above.", list.getBounds().reduced (20), juce::Justification::centred, 3);
    }
}

void MatrixPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (26);
    sourceBox.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (6);
    search.setBounds (top.removeFromLeft (160));
    top.removeFromLeft (6);
    destButton.setBounds (top.removeFromLeft (260));
    top.removeFromLeft (6);
    addButton.setBounds (top.removeFromLeft (60));
    countLabel.setBounds (top);
    r.removeFromTop (22);   // room for column headings
    list.setBounds (r);
}

int MatrixPanel::getNumRows()
{
    return (int) connectionIds.size();
}

juce::Component* MatrixPanel::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, (int) connectionIds.size()))
    {
        delete existing;
        return nullptr;
    }

    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (processor);
    }

    r->setConnection (connectionIds[(size_t) row]);
    return r;
}

void MatrixPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    connectionIds.clear();
    for (auto& c : processor.getMatrix().getConnections())
        connectionIds.push_back (c.id);

    countLabel.setText (juce::String ((int) connectionIds.size()) + " connections", juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    repaint();
}

} // namespace mm
