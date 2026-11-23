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
/** Column layout shared by the rows and the headings. Offset, range and
    smoothing give way first when the panel is narrow (they stay editable
    when there is room). */
enum MatrixColumn { colEnabled, colName, colDepth, colOffset, colPolarity, colMin, colMax, colSmooth, colRemove, numColumns };

static std::vector<juce::Rectangle<int>> matrixColumns (juce::Rectangle<int> row)
{
    // Below this width the optional columns are dropped entirely rather than
    // squeezed into controls too small to use. They stay editable in a wider
    // window.
    const bool wide = row.getWidth() >= 820;
    const int optional = wide ? 1 : 0;

    auto rects = flexRects (row, { { 36, 36 }, { 260, 110 }, { 130, 84 }, { 110 * optional, 70 * optional }, { 86, 70 },
                                   { 100 * optional, 70 * optional }, { 100 * optional, 70 * optional },
                                   { 120 * optional, 80 * optional }, { 26, 26 } }, 4);

    // Give any spare width to the name column.
    const int spare = row.getRight() - rects.back().getRight();
    if (spare > 0)
    {
        rects[colName].setWidth (rects[colName].getWidth() + spare);
        for (size_t i = colName + 1; i < rects.size(); ++i)
            rects[i].translate (spare, 0);
    }

    return rects;
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
        sourceIndex = c->source;
        repaint();
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
        const auto cols = matrixColumns (getLocalBounds().reduced (2, 3));
        juce::Component* order[] { &enabled, &name, &depth, &offset, &polarity, &rangeMin, &rangeMax, &smoothing, &remove };

        for (int i = 0; i < numColumns; ++i)
        {
            order[i]->setBounds (i == colName ? cols[(size_t) i].withTrimmedLeft (6) : cols[(size_t) i]);
            order[i]->setVisible (cols[(size_t) i].getWidth() > 0);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (paletteFor (*this).outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // The source's colour beside its name.
        g.setColour (sourceColour (sourceIndex));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, (float) getHeight() - 12.0f)
                                    .withPosition ((float) name.getX() - 2.0f, 6.0f), 2.0f);
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
    int connectionId = 0, sourceIndex = 0;
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

    // Column headings, using the same layout as the rows.
    const auto row = list.getBounds().withHeight (16).translated (0, -17).reduced (2, 0)
                         .withTrimmedRight (list.getVerticalScrollBar().isVisible() ? list.getVerticalScrollBar().getWidth() : 0);
    const auto cols = matrixColumns (row);
    const char* headings[] { "", "SOURCE  >  DESTINATION", "DEPTH", "OFFSET", "POLARITY", "MIN", "MAX", "SMOOTH", "" };

    g.setColour (p.textDim);
    g.setFont (MuddLookAndFeel::font (11.0f, true));
    for (int i = 0; i < numColumns; ++i)
        if (cols[(size_t) i].getWidth() > 0)
            g.drawText (headings[i], cols[(size_t) i], juce::Justification::centredLeft);

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
    layoutFlex (r.removeFromTop (26), { { &sourceBox, 120, 86 }, { &search, 160, 90 }, { &destButton, 260, 130 },
                                        { &addButton, 60, 46 }, { &countLabel, 400, 0 } });
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
