#include "PluginEditor.h"

namespace mm
{

//==============================================================================
/** A modulator in the chain: click to edit it, drag to reorder, x to remove. */
class ModuleTab final : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::SettableTooltipClient
{
public:
    ModuleTab (ModdMuddEditor& e, int m) : editor (e), module (m)
    {
        setComponentID ("module" + juce::String (m));
        setTitle (getModuleName (m) + " modulator");
        setDescription ("Press Enter to edit, Delete to remove");
        setTooltip (getModuleName (m) + ": click to edit, drag to reorder, x to remove");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    const int module;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteFor (*this);
        auto r = getLocalBounds().toFloat().reduced (1.0f, 0.5f);

        g.setColour (selected ? p.active : (isMouseOver() ? p.panelHeader.brighter (0.1f) : p.panelHeader));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (hasKeyboardFocus (false) ? p.active : p.outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        if (dropMarker)
        {
            g.setColour (p.value);
            g.fillRect (r.removeFromLeft (3.0f));
        }

        auto textArea = getLocalBounds().reduced (4, 0);
        if (showClose())
        {
            const auto x = closeArea().toFloat().withSizeKeepingCentre (7.0f, 7.0f);
            g.setColour (selected ? juce::Colours::black.withAlpha (0.7f) : p.textDim);
            g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.5f);
            g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.5f);
            textArea.removeFromRight (closeArea().getWidth() - 2);
        }

        g.setColour (selected ? juce::Colours::black : p.text);
        g.setFont (MuddLookAndFeel::font (12.0f, true));
        g.drawFittedText (getModuleTabName (module), textArea, juce::Justification::centred, 1, 0.7f);
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    void mouseDown (const juce::MouseEvent&) override   { dragged = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragged && e.getDistanceFromDragStart() > 5)
        {
            dragged = true;
            if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
                c->startDragging ("mmtab:" + juce::String (module), this);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragged)
            return;

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addSectionHeader (getModuleName (module));
            m.addItem (1, "Remove " + getModuleName (module));

            juce::Component::SafePointer<ModuleTab> safe (this);
            auto* ed = &editor;
            const int id = module;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [ed, safe, id] (int r)
            {
                if (safe != nullptr && r == 1)
                    ed->requestRemoveModule (id);
            });
            return;
        }

        if (showClose() && closeArea().contains (e.getPosition()))
            editor.requestRemoveModule (module);
        else
            editor.selectModule (module);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)  { editor.selectModule (module); return true; }
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { editor.requestRemoveModule (module); return true; }
        return false;
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith ("mmtab:") && d.description.toString() != "mmtab:" + juce::String (module);
    }

    void itemDragEnter (const SourceDetails&) override  { dropMarker = true; repaint(); }
    void itemDragExit (const SourceDetails&) override   { dropMarker = false; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        dropMarker = false;
        const int moving = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
        editor.moveModuleBefore (moving, module);
    }

private:
    bool showClose() const                       { return getWidth() >= 62 && (selected || isMouseOver()); }
    juce::Rectangle<int> closeArea() const       { return getLocalBounds().removeFromRight (juce::jmin (18, getWidth() / 3)); }

    ModdMuddEditor& editor;
    bool dragged = false, dropMarker = false;
};

//==============================================================================
ModdMuddEditor::ModdMuddEditor (ModdMuddProcessor& p)
    : AudioProcessorEditor (p), processor (p), hosted (p)
{
    lnf.setTheme (processor.getThemeIndex());
    setLookAndFeel (&lnf);

    for (auto* b : { &slotButton, &addButton, &viewButton, &presetButton, &settingsButton })
        addAndMakeVisible (b);

    slotButton.setTooltip ("The hosted plug-in. Click to switch, load, replace or remove plug-ins.");
    slotButton.setTitle ("Hosted plug-in");
    slotButton.onClick = [this] { showSlotMenu(); };
    addButton.setTooltip ("Load another VST3 plug-in into ModdMudd");
    addButton.setTitle ("Load plug-in");
    addButton.onClick = [this] { showLoadMenu (-1); };
    viewButton.setComponentID ("view");
    viewButton.setTooltip ("Switch between the plug-in's own window and ModdMudd's parameter list");
    viewButton.onClick = [this] { preferNative = ! preferNative; showSelectedSlot(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    hosted.onPreferredSizeChanged = [this] { updateSize(); };
    hosted.onLoadRequested = [this] { showLoadMenu (-1); };
    addAndMakeVisible (hosted);

    // The modulation chain: one editor per module, shown when selected.
    for (int i = 0; i < kNumLfos; ++i)    modulePanels[(size_t) (modLfo0 + i)] = std::make_unique<LfoPanel> (processor, i);
    for (int i = 0; i < kNumRandoms; ++i) modulePanels[(size_t) (modRandom0 + i)] = std::make_unique<RandomPanel> (processor, i);
    modulePanels[(size_t) modMacros] = std::make_unique<MacroPanel> (processor);
    modulePanels[(size_t) modXy]     = std::make_unique<XYPanel> (processor);
    modulePanels[(size_t) modBlend]  = std::make_unique<BlendPanel> (processor);
    for (int i = 0; i < kNumEnvs; ++i)    modulePanels[(size_t) (modEnv0 + i)] = std::make_unique<EnvPanel> (processor, i);

    for (auto& panel : modulePanels)
        addChildComponent (*panel);

    matrixPanel = std::make_unique<MatrixPanel> (processor);
    addChildComponent (*matrixPanel);

    addModuleButton.setTooltip ("Add a modulator to the chain");
    addModuleButton.setComponentID ("addModule");
    addModuleButton.onClick = [this] { showAddModuleMenu(); };
    routingButton.setTooltip ("Every modulation connection in one list");
    routingButton.setComponentID ("routing");
    routingButton.setClickingTogglesState (false);
    routingButton.onClick = [this]
    {
        if (! showingRouting)
        {
            showRouting();
        }
        else if (! processor.getModules().empty())
        {
            selectModule (processor.isModuleActive (selectedModule) ? selectedModule : processor.getModules().front());
        }
        else
        {
            showingRouting = false;
            rebuildModuleTabs();
        }
    };
    addAndMakeVisible (addModuleButton);
    addAndMakeVisible (routingButton);

    status.setFont (MuddLookAndFeel::font (12.5f));
    addAndMakeVisible (status);

    processor.addSlotListener (this);
    processor.addChangeListener (this);
    processor.getCatalog().addChangeListener (this);

    rebuildModuleTabs();
    rebuildSlotButtons();
    showSelectedSlot();

    setWantsKeyboardFocus (true);
    startTimerHz (30);   // the only UI timer: see Refreshable
}

ModdMuddEditor::~ModdMuddEditor()
{
    processor.getCatalog().removeChangeListener (this);
    processor.removeChangeListener (this);
    processor.removeSlotListener (this);
    hosted.clear();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ModdMuddEditor::slotsAboutToChange()
{
    // The hosted plug-in's editor must go before the plug-in does.
    hosted.clear();
}

void ModdMuddEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &processor.getCatalog())
        return;   // scan progress is shown by the timer

    // Only rebuild the centre view when the set of hosted plug-ins changed;
    // the parameter list updates itself for routing changes.
    juce::StringArray uids;
    for (auto& s : processor.getSlots())
        uids.add (s->getUid() + (s->isMissing() ? "?" : ""));

    const bool slotsChanged = uids != slotUidsShown;
    rebuildSlotButtons();

    if (processor.getModules() != modulesShown)
        rebuildModuleTabs();

    if (slotsChanged)
        showSelectedSlot();
}

void ModdMuddEditor::timerCallback()
{
    juce::String text;
    auto& catalog = processor.getCatalog();

    if (catalog.isScanning())
        text = catalog.getScanStatus() + " (" + juce::String (juce::roundToInt (catalog.getScanProgress() * 100.0f)) + "%)";
    else if (processor.getMidiLearnTarget() >= 0)
        text = "MIDI learn: move a controller...";
    else
        text = processor.getLastError();

    if (text.isEmpty() && catalog.getScanStatus() != lastCatalogStatus)
        text = catalog.getScanStatus();

    if (! catalog.isScanning())
        lastCatalogStatus = catalog.getScanStatus();

    if (status.getText() != text)
        status.setText (text, juce::dontSendNotification);

    refreshShowing (*this);
}

void ModdMuddEditor::rebuildSlotButtons()
{
    const auto& slots = processor.getSlots();
    slotUidsShown.clear();
    for (auto& s : slots)
        slotUidsShown.add (s->getUid() + (s->isMissing() ? "?" : ""));
    selectedSlot = juce::jlimit (0, juce::jmax (0, (int) slots.size() - 1), selectedSlot);

    if (slots.empty())
    {
        slotButton.setButtonText ("No plug-in loaded");
    }
    else
    {
        auto& slot = *slots[(size_t) selectedSlot];
        juce::String text;
        if (slots.size() > 1)
            text << (selectedSlot + 1) << "/" << (int) slots.size() << "  ";
        text << slot.getDisplayName() << (slot.isMissing() ? " (missing)" : "");
        slotButton.setButtonText (text);
    }

    addButton.setEnabled ((int) slots.size() < kMaxHostedPlugins);
}

void ModdMuddEditor::showSelectedSlot()
{
    hosted.showSlot (selectedSlot, preferNative);
    viewButton.setButtonText (hosted.isShowingNative() ? "Parameters" : "Plug-in UI");
    viewButton.setEnabled (! processor.getSlots().empty());
    updateSize();
}

void ModdMuddEditor::updateSize()
{
    if (forwardingResize)
        return;

    if (hosted.isShowingNative())
    {
        // Snap to the plug-in: same width (unless it's narrower than the
        // panel can go), plug-in on top, panel underneath.
        const auto pref = hosted.getPreferredSize();
        const bool canResize = hosted.isHostedResizable();
        const auto limits = hosted.getHostedSizeLimits();

        setResizable (canResize, false);
        if (canResize)
            setResizeLimits (juce::jmax (minPanelWidth, limits.getX()), limits.getY() + panelHeight,
                             juce::jmax (minPanelWidth, limits.getWidth()), limits.getHeight() + panelHeight);

        setSize (juce::jmax (minPanelWidth, pref.x), pref.y + panelHeight);
    }
    else
    {
        setResizable (true, true);
        setResizeLimits (minPanelWidth, 320 + panelHeight, 4000, 3000);

        if (getWidth() < minPanelWidth || getHeight() < 320 + panelHeight)
            setSize (defaultWidth, defaultCentreHeight + panelHeight);
    }

    resized();
}

void ModdMuddEditor::rebuildModuleTabs()
{
    modulesShown = processor.getModules();

    for (auto* t : moduleTabs)
        removeChildComponent (t);
    moduleTabs.clear();

    for (int m : modulesShown)
        addAndMakeVisible (moduleTabs.add (new ModuleTab (*this, m)));

    addModuleButton.setEnabled ((int) modulesShown.size() < kNumModules);

    // Keep the selection on a module that is still in the chain.
    if (! showingRouting && std::find (modulesShown.begin(), modulesShown.end(), selectedModule) == modulesShown.end())
        selectedModule = modulesShown.empty() ? -1 : modulesShown.front();

    if (modulesShown.empty() && ! showingRouting)
    {
        // The empty chain shows a hint (painted) instead of a module.
        for (auto& panel : modulePanels)
            panel->setVisible (false);
        matrixPanel->setVisible (false);
        routingButton.setToggleState (false, juce::dontSendNotification);
    }
    else if (showingRouting || selectedModule < 0)
    {
        showRouting();
    }
    else
    {
        selectModule (selectedModule);
    }

    resized();
}

void ModdMuddEditor::selectModule (int module)
{
    if (! processor.isModuleActive (module))
        return;

    selectedModule = module;
    showingRouting = false;

    for (int m = 0; m < kNumModules; ++m)
        modulePanels[(size_t) m]->setVisible (m == module);

    matrixPanel->setVisible (false);
    routingButton.setToggleState (false, juce::dontSendNotification);

    for (auto* t : moduleTabs)
    {
        t->selected = t->module == module;
        t->repaint();
    }

    repaint();
}

void ModdMuddEditor::showRouting()
{
    showingRouting = true;

    for (auto& panel : modulePanels)
        panel->setVisible (false);

    matrixPanel->setVisible (true);
    routingButton.setToggleState (true, juce::dontSendNotification);

    for (auto* t : moduleTabs)
    {
        t->selected = false;
        t->repaint();
    }

    repaint();
}

void ModdMuddEditor::requestRemoveModule (int module)
{
    // Deferred: removing rebuilds the tabs, including the one that was clicked.
    const auto& list = processor.getModules();
    auto it = std::find (list.begin(), list.end(), module);
    int next = -1;
    if (it != list.end())
    {
        if (it + 1 != list.end()) next = *(it + 1);
        else if (it != list.begin()) next = *(it - 1);
    }

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    juce::MessageManager::callAsync ([safe, module, next]
    {
        if (safe == nullptr)
            return;

        if (safe->selectedModule == module)
            safe->selectedModule = next;

        safe->processor.removeModule (module);
        safe->processor.setStatus (getModuleName (module) + " removed, with its connections.");
    });
}

void ModdMuddEditor::moveModuleBefore (int module, int targetModule)
{
    const auto& list = processor.getModules();
    auto target = std::find (list.begin(), list.end(), targetModule);
    auto moving = std::find (list.begin(), list.end(), module);
    if (target == list.end() || moving == list.end())
        return;

    int index = (int) (target - list.begin());
    if (moving < target)
        --index;   // the list shrinks by one before the insert

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    juce::MessageManager::callAsync ([safe, module, index]
    {
        if (safe != nullptr)
            safe->processor.moveModule (module, index);
    });
}

void ModdMuddEditor::showAddModuleMenu()
{
    juce::PopupMenu m;
    auto addGroup = [this, &m] (const juce::String& title, int first, int count)
    {
        m.addSectionHeader (title);
        for (int i = first; i < first + count; ++i)
            m.addItem (1 + i, getModuleName (i), ! processor.isModuleActive (i));
    };

    addGroup ("Movement", modLfo0, kNumLfos);
    addGroup ("Randomness", modRandom0, kNumRandoms);
    addGroup ("Control", modMacros, 3);   // macros, XY pad, preset blender
    addGroup ("Follow audio", modEnv0, kNumEnvs);

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addModuleButton), [safe] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;

        const int module = r - 1;
        safe->processor.addModule (module);
        safe->rebuildModuleTabs();
        safe->selectModule (module);
    });
}

//==============================================================================
void ModdMuddEditor::paint (juce::Graphics& g)
{
    const auto& p = lnf.getPalette();
    g.fillAll (p.background);

    auto panel = getLocalBounds().removeFromBottom (panelHeight);

    // Header strip of the panel
    auto strip = panel.removeFromTop (stripHeight);
    g.setColour (p.panelHeader);
    g.fillRect (strip);

    // The seam where the panel snaps onto the plug-in
    g.setColour (p.value);
    g.fillRect (strip.removeFromTop (2));

    auto logo = strip.reduced (10, 0).removeFromLeft (110);
    g.setFont (MuddLookAndFeel::font (15.0f, true));
    const auto modd = juce::String ("MODD");
    const int moddW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), modd);
    g.setColour (p.text);
    g.drawText (modd, logo, juce::Justification::centredLeft);
    g.setColour (p.value);
    g.drawText ("MUDD", logo.withTrimmedLeft (moddW + 1), juce::Justification::centredLeft);

    if constexpr (kIsInstrumentBuild)
    {
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (9.0f, true));
        g.drawText ("INST", logo.withTrimmedLeft (moddW * 2 + 5), juce::Justification::centredLeft);
    }

    if (processor.getModules().empty() && ! showingRouting)
    {
        g.setColour (p.panel);
        g.fillRoundedRectangle (contentArea.toFloat(), 4.0f);
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (14.0f));
        g.drawFittedText ("Your modulation chain is empty. Click \"+ Add\" to add an LFO, randomizer, macros, "
                          "XY pad, preset blender or envelope follower.", contentArea.reduced (30), juce::Justification::centred, 3);
    }

    g.setColour (p.panelHeader);
    g.fillRect (getLocalBounds().removeFromBottom (statusHeight));
}

void ModdMuddEditor::layoutPanel (juce::Rectangle<int> panel)
{
    auto strip = panel.removeFromTop (stripHeight).withTrimmedTop (2).reduced (6, 3);
    strip.removeFromLeft (kIsInstrumentBuild ? 130 : 104);

    layoutFlex (strip, { { &slotButton, 260, 110 }, { &addButton, 28, 28 }, { nullptr, 4000, 0 } }, 4);
    auto right = strip.removeFromRight (juce::jmin (strip.getWidth() / 2, 250));
    layoutFlex (right, { { &viewButton, 86, 66 }, { &presetButton, 74, 58 }, { &settingsButton, 74, 58 } }, 4);

    // Keep the plug-in button and "+" clear of the right-hand buttons.
    const int maxLeft = right.getX() - 8;
    if (slotButton.getRight() > maxLeft - 32)
    {
        slotButton.setBounds (slotButton.getBounds().withRight (juce::jmax (slotButton.getX() + 60, maxLeft - 32)));
        addButton.setBounds (addButton.getBounds().withX (slotButton.getRight() + 4));
    }

    status.setBounds (panel.removeFromBottom (statusHeight).reduced (10, 0));

    auto content = panel.reduced (pad, 0);
    content.removeFromTop (pad);

    // Chain row: the user's modules, "+ Add", and Routing on the right.
    auto tabs = content.removeFromTop (tabsHeight);
    routingButton.setBounds (tabs.removeFromRight (86));
    tabs.removeFromRight (8);

    const int n = moduleTabs.size();
    const int addW = 64;
    const int tabW = n == 0 ? 0 : juce::jlimit (40, 96, (tabs.getWidth() - addW - 4) / n);
    for (auto* t : moduleTabs)
    {
        t->setBounds (tabs.removeFromLeft (tabW).reduced (1, 0));
    }
    tabs.removeFromLeft (4);
    addModuleButton.setBounds (tabs.removeFromLeft (addW));

    content.removeFromTop (4);
    content.removeFromBottom (pad);
    contentArea = content;

    for (auto& panelComponent : modulePanels)
        panelComponent->setBounds (content);
    matrixPanel->setBounds (content);
}

void ModdMuddEditor::resized()
{
    auto r = getLocalBounds();
    layoutPanel (r.removeFromBottom (panelHeight));
    hosted.setBounds (r);

    // A resizable plug-in follows the window; the window then snaps back to
    // whatever size the plug-in actually accepted.
    if (hosted.isShowingNative() && hosted.isHostedResizable() && ! forwardingResize)
    {
        const auto pref = hosted.getPreferredSize();
        if (pref.x != r.getWidth() || pref.y != r.getHeight())
        {
            forwardingResize = true;
            hosted.resizeHosted (r.getWidth(), r.getHeight());
            forwardingResize = false;

            juce::Component::SafePointer<ModdMuddEditor> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->updateSize(); });
        }
    }
}

bool ModdMuddEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (processor.getArmedSource() >= 0) processor.disarmAssign();
        if (processor.getMidiLearnTarget() >= 0) processor.cancelMidiLearn();
        processor.setStatus ({});
        return true;
    }

    return false;
}

//==============================================================================
void ModdMuddEditor::showLoadMenu (int replaceIndex)
{
    auto& catalog = processor.getCatalog();
    const bool wantInstruments = kIsInstrumentBuild && (replaceIndex == 0 || (replaceIndex < 0 && processor.getSlots().empty()));

    const auto plugins = catalog.getPlugins (wantInstruments);
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> byMaker;

    for (size_t i = 0; i < plugins.size(); ++i)
        byMaker[plugins[i].manufacturerName.isNotEmpty() ? plugins[i].manufacturerName : "Unknown"]
            .addItem ((int) i + 1, plugins[i].name);

    menu.addSectionHeader (wantInstruments ? "Instruments" : "Effects");
    for (auto& [maker, sub] : byMaker)
        menu.addSubMenu (maker, sub);

    if (plugins.empty())
        menu.addItem (-1, catalog.isScanning() ? "Scanning..." : "No plug-ins found yet", false);

    menu.addSeparator();
    menu.addItem (10001, "Scan for new plug-ins", ! catalog.isScanning());
    menu.addItem (10002, "Rescan all plug-ins", ! catalog.isScanning());

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slotButton),
                        [safe, plugins, replaceIndex] (int r)
                        {
                            if (safe == nullptr) return;
                            auto& proc = safe->processor;

                            if (r == 10001) { proc.getCatalog().startScan (false); return; }
                            if (r == 10002) { proc.getCatalog().startScan (true);  return; }

                            if (r > 0 && r <= (int) plugins.size())
                            {
                                const auto error = proc.loadPlugin (plugins[(size_t) r - 1], replaceIndex);
                                if (error.isEmpty())
                                {
                                    safe->selectedSlot = replaceIndex >= 0 ? replaceIndex : (int) proc.getSlots().size() - 1;
                                    safe->rebuildSlotButtons();
                                    safe->showSelectedSlot();
                                }
                                proc.setStatus (error);
                            }
                        });
}

void ModdMuddEditor::showSlotMenu()
{
    const auto& slots = processor.getSlots();
    const int count = (int) slots.size();
    const int index = selectedSlot;

    juce::PopupMenu m;
    for (int i = 0; i < count; ++i)
        m.addItem (1 + i, juce::String (i + 1) + "  " + slots[(size_t) i]->getDisplayName()
                              + (slots[(size_t) i]->isMissing() ? " (missing)" : ""), true, i == index);

    if (count > 0)
        m.addSeparator();

    m.addItem (100, count == 0 ? "Load a plug-in..." : "Load another plug-in...", count < kMaxHostedPlugins);

    if (count > 0)
    {
        const auto name = slots[(size_t) index]->getDisplayName();
        m.addItem (101, "Replace " + name + "...");
        m.addItem (102, "Move " + name + " earlier", index > 0);
        m.addItem (103, "Move " + name + " later", index < count - 1);
        m.addSeparator();
        m.addItem (104, "Remove " + name);
    }

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slotButton), [safe, index] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& proc = safe->processor;

        if (r >= 1 && r < 100)
        {
            safe->selectedSlot = r - 1;
        }
        else switch (r)
        {
            case 100: safe->showLoadMenu (-1); return;
            case 101: safe->showLoadMenu (index); return;
            case 102: proc.moveSlot (index, index - 1); safe->selectedSlot = index - 1; break;
            case 103: proc.moveSlot (index, index + 1); safe->selectedSlot = index + 1; break;
            case 104: proc.removePlugin (index); safe->selectedSlot = juce::jmax (0, index - 1); break;
            default: return;
        }

        safe->rebuildSlotButtons();
        safe->showSelectedSlot();
    });
}

void ModdMuddEditor::showSettingsMenu()
{
    auto& catalog = processor.getCatalog();

    juce::PopupMenu sub, theme, folders;
    const int current = processor.getSubBlockSize();
    for (int s : { 16, 32, 64, 128 })
        sub.addItem (100 + s, juce::String (s) + " samples" + (s == kDefaultSubBlock ? " (default)" : ""), true, current == s);
    sub.addItem (100, "Once per host block (lowest CPU)", true, current == 0);

    for (int t = 0; t < MuddLookAndFeel::getNumThemes(); ++t)
        theme.addItem (300 + t, MuddLookAndFeel::getThemeName (t), true, processor.getThemeIndex() == t);

    const auto path = catalog.getSearchPath();
    for (int i = 0; i < path.getNumPaths(); ++i)
        folders.addItem (500 + i, "Remove " + path.getRawString (i));
    folders.addSeparator();
    folders.addItem (499, "Add folder...");

    juce::PopupMenu m;
    m.addItem (1, "Scan for new plug-ins", ! catalog.isScanning());
    m.addItem (2, "Rescan all plug-ins", ! catalog.isScanning());
    m.addItem (3, "Cancel scan", catalog.isScanning());
    m.addSubMenu ("Plug-in folders", folders);
    m.addSeparator();
    m.addSubMenu ("Modulation resolution", sub);
    m.addSubMenu ("Theme", theme);

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;
        auto& cat = proc.getCatalog();

        if (r == 1) cat.startScan (false);
        else if (r == 2) cat.startScan (true);
        else if (r == 3) cat.cancelScan();
        else if (r >= 100 && r < 300) proc.setSubBlockSize (r - 100);
        else if (r >= 300 && r < 400)
        {
            proc.setThemeIndex (r - 300);
            safe->lnf.setTheme (r - 300);
            safe->sendLookAndFeelChange();
            safe->repaint();
        }
        else if (r == 499)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Add a VST3 folder", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            if (safe == nullptr || fc.getResult() == juce::File()) return;
                                            auto& c = safe->processor.getCatalog();
                                            auto p = c.getSearchPath();
                                            p.addIfNotAlreadyThere (fc.getResult());
                                            c.setSearchPath (p);
                                            c.startScan (false);
                                        });
        }
        else if (r >= 500)
        {
            auto p = cat.getSearchPath();
            p.remove (r - 500);
            cat.setSearchPath (p);
        }
    });
}

void ModdMuddEditor::showPresetMenu()
{
    const auto dir = ModdMuddProcessor::getPresetDirectory();
    const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.mmpreset");

    juce::PopupMenu m;
    m.addItem (1, "Save preset...");
    m.addItem (2, "Load preset...");
    m.addItem (3, "Open preset folder");

    if (! files.isEmpty())
    {
        m.addSeparator();
        for (int i = 0; i < files.size(); ++i)
            m.addItem (100 + i, files[i].getFileNameWithoutExtension());
    }

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [safe, files, dir] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;

        if (r == 1)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Save ModdMudd preset", dir.getChildFile ("New preset.mmpreset"), "*.mmpreset");
            safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            if (safe == nullptr || fc.getResult() == juce::File()) return;
                                            auto f = fc.getResult().withFileExtension ("mmpreset");
                                            safe->processor.setStatus (safe->processor.savePresetFile (f) ? "Saved " + f.getFileName()
                                                                                                          : "Couldn't save " + f.getFullPathName());
                                        });
        }
        else if (r == 2)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Load ModdMudd preset", dir, "*.mmpreset");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            if (safe != nullptr && fc.getResult().existsAsFile()
                                                 && ! safe->processor.loadPresetFile (fc.getResult()))
                                                safe->processor.setStatus ("Not a ModdMudd preset: " + fc.getResult().getFileName());
                                        });
        }
        else if (r == 3)
        {
            dir.startAsProcess();
        }
        else if (r >= 100 && r - 100 < files.size())
        {
            if (! proc.loadPresetFile (files[r - 100]))
                proc.setStatus ("Couldn't load " + files[r - 100].getFileName());
        }
    });
}

} // namespace mm
