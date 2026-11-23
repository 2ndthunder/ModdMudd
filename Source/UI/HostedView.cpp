#include "HostedView.h"
#include "MatrixPanel.h"

namespace mm
{

class GenericParamView::Row : public juce::Component,
                              public juce::DragAndDropTarget
{
public:
    Row (ModdMuddProcessor& p, int slot) : processor (p), slotIndex (slot)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.onValueChange = [this]
        {
            if (! updating)
                processor.setHostedBase (slotIndex, paramIndex, (float) slider.getValue());
        };
        slider.onPopup = [this] { showMenu(); };

        name.setFont (MuddLookAndFeel::font (12.5f));
        name.setMinimumHorizontalScale (0.7f);
        value.setFont (MuddLookAndFeel::font (12.0f));
        value.setJustificationType (juce::Justification::centredRight);

        addAndMakeVisible (name);
        addAndMakeVisible (slider);
        addAndMakeVisible (value);
    }

    void setParam (int index)
    {
        paramIndex = index;
        auto* slot = getSlot();
        if (slot == nullptr)
            return;

        name.setText (slot->getParamName (index), juce::dontSendNotification);
        slider.setTitle (slot->getParamName (index));
        update();
    }

    void update()
    {
        auto* slot = getSlot();
        if (slot == nullptr || ! juce::isPositiveAndBelow (paramIndex, slot->getNumParams()))
            return;

        auto& ps = slot->getState (paramIndex);
        const float base = ps.base.load();
        const float shown = ps.display.load();
        const bool driven = ps.driven.load();

        updating = true;
        if (! slider.isMouseButtonDown())
            slider.setValue (base, juce::dontSendNotification);
        updating = false;

        if (driven)
            slider.getProperties().set ("modValue", shown);
        else
            slider.getProperties().remove ("modValue");

        // getText talks to the plug-in's controller on the message thread.
        if (auto* param = slot->getParam (paramIndex))
            value.setText (param->getText (driven ? shown : base, 24), juce::dontSendNotification);

        // Which modulators move this parameter, and how far they can take it.
        std::vector<int> sources;
        float lowest = 0.0f, highest = 0.0f;
        DestinationSettings range;

        if (driven)
        {
            const auto dest = destination();
            range = processor.getMatrix().getSettings (dest);

            for (auto* c : processor.getMatrix().getConnectionsTo (dest))
            {
                if (! c->enabled)
                    continue;

                if (std::find (sources.begin(), sources.end(), c->source) == sources.end())
                    sources.push_back (c->source);

                const bool bipolarSource = processor.getEngine().isSourceBipolar (c->source);
                float vMin = bipolarSource ? -1.0f : 0.0f, vMax = 1.0f;
                if (c->polarity == Polarity::unipolar) vMin = 0.0f;
                if (c->polarity == Polarity::bipolar)  vMin = -1.0f;

                const float a = c->depth * vMin + c->offset, b = c->depth * vMax + c->offset;
                lowest += juce::jmin (a, b);
                highest += juce::jmax (a, b);
            }
        }

        modCount = (int) sources.size();

        if (! sources.empty())
        {
            slider.getProperties().set ("modLo", juce::jlimit (range.rangeMin, range.rangeMax, base + lowest));
            slider.getProperties().set ("modHi", juce::jlimit (range.rangeMin, range.rangeMax, base + highest));
        }
        else
        {
            slider.getProperties().remove ("modLo");
            slider.getProperties().remove ("modHi");
        }

        if (sources != tagSources)
        {
            tagSources = sources;
            resized();
        }

        slider.repaint();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        auto nameArea = r.removeFromLeft (r.getWidth() * 35 / 100);
        tagArea = nameArea.removeFromRight (juce::jmin (nameArea.getWidth() / 2, tagsWidth()));
        name.setBounds (nameArea);
        value.setBounds (r.removeFromRight (90));
        r.removeFromRight (16);
        slider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteFor (*this);
        if (dragOver)
        {
            g.setColour (p.active.withAlpha (0.25f));
            g.fillRect (getLocalBounds());
        }

        if (modCount > 0)
        {
            g.setColour (p.active);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) slider.getRight() + 9.0f, getHeight() * 0.5f }));
        }

        // Coloured tags naming the modulators that move this parameter.
        auto tags = tagArea;
        g.setFont (MuddLookAndFeel::font (10.5f, true));
        for (size_t i = 0; i < tagSources.size() && i < 3; ++i)
        {
            auto tag = tags.removeFromLeft (tagWidth (tagSources[i])).reduced (1, 6).toFloat();
            tags.removeFromLeft (2);
            g.setColour (sourceColour (tagSources[i]));
            g.fillRoundedRectangle (tag, 3.0f);
            g.setColour (juce::Colours::black);
            g.drawText (getSourceShortName (tagSources[i]), tag, juce::Justification::centred);
        }

        if (tagSources.size() > 3)
        {
            g.setColour (p.textDim);
            g.drawText ("+" + juce::String ((int) tagSources.size() - 3), tags, juce::Justification::centredLeft);
        }

        g.setColour (p.outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    bool isInterestedInDragSource (const SourceDetails& d) override   { return sourceFromDragId (d.description) >= 0; }
    void itemDragEnter (const SourceDetails&) override                 { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override                  { dragOver = false; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        dragOver = false;
        Connection c;
        c.source = sourceFromDragId (d.description);
        c.dest = destination();
        c.depth = 0.5f;
        processor.setStatus (describeAddResult (processor.addConnection (c)));
        repaint();
    }

private:
    HostedSlot* getSlot() const
    {
        const auto& slots = processor.getSlots();
        return juce::isPositiveAndBelow (slotIndex, (int) slots.size()) ? slots[(size_t) slotIndex].get() : nullptr;
    }

    Destination destination() const
    {
        auto* slot = getSlot();
        return slot != nullptr ? Destination::hosted (slot->getUid(), slot->getParamId (paramIndex)) : Destination {};
    }

    void showMenu()
    {
        juce::PopupMenu m, macros;
        for (int i = 0; i < kNumMacros; ++i)
            macros.addItem (100 + i, "Macro " + juce::String (i + 1));

        m.addSectionHeader (name.getText());
        m.addSubMenu ("Assign to macro", macros);
        m.addItem (1, "Remove modulation", modCount > 0);

        juce::Component::SafePointer<Row> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe] (int r)
        {
            if (safe == nullptr) return;

            if (r == 1)
            {
                safe->processor.removeConnectionsTo (safe->destination());
            }
            else if (r >= 100)
            {
                // A macro sweeps this parameter from its current value upwards.
                Connection c;
                c.source = srcMacro0 + (r - 100);
                c.dest = safe->destination();
                c.depth = 1.0f;
                safe->processor.setStatus (describeAddResult (safe->processor.addConnection (c)));
            }
        });
    }

    static int tagWidth (int source)   { return getSourceShortName (source).length() > 2 ? 38 : 26; }

    int tagsWidth() const
    {
        int w = 0;
        for (size_t i = 0; i < tagSources.size() && i < 3; ++i)
            w += tagWidth (tagSources[i]) + 2;
        return tagSources.size() > 3 ? w + 24 : w;
    }

    ModdMuddProcessor& processor;
    int slotIndex, paramIndex = 0;
    juce::Label name, value;
    MuddSlider slider;
    bool updating = false, dragOver = false;
    int modCount = 0;
    std::vector<int> tagSources;
    juce::Rectangle<int> tagArea;
};

//==============================================================================
GenericParamView::GenericParamView (ModdMuddProcessor& p, int slot) : processor (p), slotIndex (slot)
{
    search.setTextToShowWhenEmpty ("Search parameters", paletteFor (*this).textDim);
    search.setTitle ("Search parameters");
    search.onTextChange = [this] { applyFilter(); };

    title.setFont (MuddLookAndFeel::font (13.0f, true));

    list.setModel (this);
    list.setRowHeight (28);
    list.setTitle ("Parameters");

    addAndMakeVisible (search);
    addAndMakeVisible (title);
    addAndMakeVisible (list);

    processor.addChangeListener (this);
    applyFilter();
}

GenericParamView::~GenericParamView()
{
    processor.removeChangeListener (this);
}

void GenericParamView::applyFilter()
{
    visible.clear();
    const auto& slots = processor.getSlots();

    if (juce::isPositiveAndBelow (slotIndex, (int) slots.size()))
    {
        auto& slot = *slots[(size_t) slotIndex];
        const auto f = search.getText().trim();

        title.setText (slot.getDisplayName() + (slot.isMissing() ? " (not installed)" : ""), juce::dontSendNotification);

        for (int i = 0; i < slot.getNumParams(); ++i)
            if (slot.isModulatable (i) && (f.isEmpty() || slot.getParamName (i).containsIgnoreCase (f)))
                visible.push_back (i);
    }

    list.updateContent();
    list.repaint();
}

juce::Component* GenericParamView::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
    {
        delete existing;
        return nullptr;
    }

    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (processor, slotIndex);
    }

    r->setParam (visible[(size_t) row]);
    return r;
}

void GenericParamView::refresh()
{
    for (int i = 0; i < list.getNumRowsOnScreen() + 2; ++i)
        if (auto* r = dynamic_cast<Row*> (list.getComponentForRowNumber (list.getRowContainingPosition (0, 0) + i)))
            r->update();
}

void GenericParamView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    list.updateContent();
    list.repaint();
}

void GenericParamView::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (26);
    search.setBounds (top.removeFromRight (240));
    title.setBounds (top);
    r.removeFromTop (6);
    list.setBounds (r);
}

void GenericParamView::paint (juce::Graphics& g)
{
    g.fillAll (paletteFor (*this).panel);

    if (visible.empty())
    {
        g.setColour (paletteFor (*this).textDim);
        g.setFont (MuddLookAndFeel::font (13.0f));
        g.drawFittedText ("No automatable parameters to show.", list.getBounds(), juce::Justification::centred, 2);
    }
}

//==============================================================================
HostedView::HostedView (ModdMuddProcessor& p) : processor (p)
{
    loadButton.onClick = [this] { if (onLoadRequested) onLoadRequested(); };
    addChildComponent (loadButton);
}

HostedView::~HostedView()
{
    clear();
}

void HostedView::clear()
{
    if (editor != nullptr)
    {
        editor->removeComponentListener (this);
        editor.reset();
    }

    generic.reset();
}

void HostedView::showSlot (int slotIndex, bool preferNative)
{
    clear();

    const auto& slots = processor.getSlots();
    loadButton.setVisible (slots.empty());
    message = {};

    if (! juce::isPositiveAndBelow (slotIndex, (int) slots.size()))
    {
        message = processor.getCatalog().getPlugins (false).empty() && processor.getCatalog().getPlugins (true).empty()
                      ? "No plug-ins found yet. Use Settings > Scan for plug-ins, then load one."
                      : "Load a VST3 plug-in to start modulating it.";
        resized();
        repaint();
        if (onPreferredSizeChanged) onPreferredSizeChanged();
        return;
    }

    auto& slot = *slots[(size_t) slotIndex];

    if (preferNative && ! slot.isMissing())
    {
        if (auto* inst = slot.getInstance(); inst != nullptr && inst->hasEditor())
        {
            editor.reset (inst->createEditorAndMakeActive());

            if (editor != nullptr)
            {
                addAndMakeVisible (*editor);
                editor->addComponentListener (this);
            }
        }
    }

    if (editor == nullptr)
    {
        generic = std::make_unique<GenericParamView> (processor, slotIndex);
        addAndMakeVisible (*generic);
    }

    resized();
    if (onPreferredSizeChanged) onPreferredSizeChanged();
}

juce::Point<int> HostedView::getPreferredSize() const
{
    if (editor != nullptr)
        return { editor->getWidth(), editor->getHeight() };

    return { 0, 420 };
}

bool HostedView::isHostedResizable() const
{
    return editor != nullptr && editor->isResizable();
}

juce::Rectangle<int> HostedView::getHostedSizeLimits() const
{
    if (editor != nullptr)
        if (auto* c = editor->getConstrainer())
            return { c->getMinimumWidth(), c->getMinimumHeight(),
                     juce::jmax (c->getMinimumWidth(), c->getMaximumWidth()),
                     juce::jmax (c->getMinimumHeight(), c->getMaximumHeight()) };

    return { 100, 100, 8000, 8000 };
}

void HostedView::resizeHosted (int width, int height)
{
    if (! isHostedResizable())
        return;

    const auto limits = getHostedSizeLimits();
    editor->setSize (juce::jlimit (limits.getX(), limits.getWidth(), width),
                     juce::jlimit (limits.getY(), limits.getHeight(), height));
}

void HostedView::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (wasResized && onPreferredSizeChanged)
        onPreferredSizeChanged();
}

void HostedView::resized()
{
    if (editor != nullptr)
    {
        // Centre the plug-in's window; never resize it ourselves.
        editor->setTopLeftPosition ((getWidth() - editor->getWidth()) / 2, 0);
    }

    if (generic != nullptr)
        generic->setBounds (getLocalBounds());

    loadButton.setBounds (getLocalBounds().withSizeKeepingCentre (200, 34).translated (0, 30));
}

void HostedView::paint (juce::Graphics& g)
{
    const auto& p = paletteFor (*this);

    // Flush, square background: the plug-in's window sits edge to edge with
    // ModdMudd's panel. It only shows where the plug-in is narrower than the
    // panel.
    g.fillAll (editor != nullptr ? p.background : p.inset);

    if (message.isNotEmpty())
    {
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (15.0f));
        g.drawFittedText (message, getLocalBounds().reduced (30).translated (0, -20), juce::Justification::centred, 3);
    }
}

} // namespace mm
