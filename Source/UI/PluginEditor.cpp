#include "PluginEditor.h"

namespace mm
{

ModdMuddEditor::ModdMuddEditor (ModdMuddProcessor& p)
    : AudioProcessorEditor (p), processor (p), hosted (p)
{
    lnf.setTheme (processor.getThemeIndex());
    setLookAndFeel (&lnf);

    for (auto* b : { &addButton, &viewButton, &presetButton, &settingsButton })
        addAndMakeVisible (b);

    addButton.setTooltip ("Load a VST3 plug-in into ModdMudd");
    addButton.onClick = [this] { showLoadMenu (-1); };
    viewButton.setComponentID ("view");
    viewButton.setTooltip ("Switch between the plug-in's own window and ModdMudd's parameter list");
    viewButton.onClick = [this] { preferNative = ! preferNative; showSelectedSlot(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    hosted.onPreferredSizeChanged = [this] { updateSize(); };
    hosted.onLoadRequested = [this] { showLoadMenu (-1); };
    addAndMakeVisible (hosted);

    // Modulator tabs
    auto addTab = [this] (const juce::String& name, juce::Component* panel)
    {
        auto* b = tabButtons.add (new juce::TextButton (name));
        const int index = tabButtons.size() - 1;
        b->setClickingTogglesState (false);
        b->setComponentID ("tab" + juce::String (index));
        b->onClick = [this, index] { selectTab (index); };
        addAndMakeVisible (b);
        panels.add (panel);
        addChildComponent (panel);
    };

    for (int i = 0; i < kNumLfos; ++i)    addTab ("LFO " + juce::String (i + 1), new LfoPanel (processor, i));
    for (int i = 0; i < kNumRandoms; ++i) addTab ("RND " + juce::String (i + 1), new RandomPanel (processor, i));
    addTab ("MACROS", new MacroPanel (processor));
    addTab ("XY", new XYPanel (processor));
    addTab ("BLEND", new BlendPanel (processor));
    for (int i = 0; i < kNumEnvs; ++i)    addTab ("ENV " + juce::String (i + 1), new EnvPanel (processor, i));
    addTab ("MATRIX", new MatrixPanel (processor));

    status.setFont (MuddLookAndFeel::font (12.5f));
    addAndMakeVisible (status);

    processor.addSlotListener (this);
    processor.addChangeListener (this);
    processor.getCatalog().addChangeListener (this);

    selectTab (0);
    rebuildSlotButtons();
    showSelectedSlot();

    setResizeLimits (minWidth, 600, 4000, 3000);
    setWantsKeyboardFocus (true);
    startTimerHz (10);
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

    if ((int) processor.getSlots().size() != slotCountShown || ! hosted.isShowingNative())
    {
        rebuildSlotButtons();
        showSelectedSlot();
    }
    else
    {
        rebuildSlotButtons();
    }
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
}

void ModdMuddEditor::rebuildSlotButtons()
{
    for (auto* b : slotButtons)
        removeChildComponent (b);
    slotButtons.clear();

    const auto& slots = processor.getSlots();
    slotCountShown = (int) slots.size();
    selectedSlot = juce::jlimit (0, juce::jmax (0, (int) slots.size() - 1), selectedSlot);

    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto& slot = *slots[(size_t) i];
        auto* b = slotButtons.add (new juce::TextButton (juce::String (i + 1) + "  " + slot.getDisplayName()
                                                         + (slot.isMissing() ? " (missing)" : "")));
        b->setToggleState (i == selectedSlot, juce::dontSendNotification);
        b->setTooltip ("Click to show, right-click for options");
        b->onClick = [this, i]
        {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu())
            {
                showSlotMenu (i);
                return;
            }

            selectedSlot = i;
            rebuildSlotButtons();
            showSelectedSlot();
        };
        addAndMakeVisible (b);
    }

    addButton.setEnabled ((int) slots.size() < kMaxHostedPlugins);
    resized();
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
    const auto pref = hosted.getPreferredSize();
    const bool native = hosted.isShowingNative();

    setResizable (! native, ! native);

    if (native)
    {
        const int w = juce::jmax (minWidth, pref.x + 2 * gap);
        const int h = topBarHeight + pref.y + gap + bottomHeight + statusHeight + gap;
        setSize (w, h);
    }
    else if (getWidth() < minWidth || getHeight() < 600)
    {
        setSize (juce::jmax (getWidth(), 1100), juce::jmax (getHeight(), topBarHeight + pref.y + gap + bottomHeight + statusHeight + gap));
    }

    resized();
}

void ModdMuddEditor::selectTab (int index)
{
    selectedTab = index;
    for (int i = 0; i < panels.size(); ++i)
    {
        panels[i]->setVisible (i == index);
        tabButtons[i]->setToggleState (i == index, juce::dontSendNotification);
    }
}

//==============================================================================
void ModdMuddEditor::paint (juce::Graphics& g)
{
    const auto& p = lnf.getPalette();
    g.fillAll (p.background);

    auto bar = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (p.panelHeader);
    g.fillRect (bar);
    g.setColour (p.outline);
    g.drawHorizontalLine (topBarHeight - 1, 0.0f, (float) getWidth());

    // Wordmark
    auto logo = bar.reduced (12, 0).removeFromLeft (150);
    g.setFont (MuddLookAndFeel::font (21.0f, true));
    g.setColour (p.text);
    const auto modd = juce::String ("MODD");
    const int moddW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), modd);
    g.drawText (modd, logo, juce::Justification::centredLeft);
    g.setColour (p.value);
    g.drawText ("MUDD", logo.withTrimmedLeft (moddW + 1), juce::Justification::centredLeft);

    if constexpr (kIsInstrumentBuild)
    {
        g.setColour (p.textDim);
        g.setFont (MuddLookAndFeel::font (10.0f, true));
        g.drawText ("INSTRUMENT", logo.withTrimmedLeft (moddW * 2 + 6), juce::Justification::centredLeft);
    }

    g.setColour (p.panelHeader);
    g.fillRect (getLocalBounds().removeFromBottom (statusHeight));
}

void ModdMuddEditor::resized()
{
    auto r = getLocalBounds();

    auto bar = r.removeFromTop (topBarHeight).reduced (8, 8);
    bar.removeFromLeft (kIsInstrumentBuild ? 230 : 160);
    settingsButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    presetButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    viewButton.setBounds (bar.removeFromRight (96));
    bar.removeFromRight (16);

    for (auto* b : slotButtons)
    {
        b->setBounds (bar.removeFromLeft (juce::jmin (180, juce::jmax (100, bar.getWidth() / 5))));
        bar.removeFromLeft (4);
    }
    addButton.setBounds (bar.removeFromLeft (80));

    status.setBounds (r.removeFromBottom (statusHeight).reduced (10, 0));

    auto bottom = r.removeFromBottom (bottomHeight).reduced (gap, 0);
    auto tabs = bottom.removeFromTop (26);
    const int tabW = juce::jmin (90, tabs.getWidth() / juce::jmax (1, tabButtons.size()));
    for (auto* b : tabButtons)
        b->setBounds (tabs.removeFromLeft (tabW).reduced (1, 0));

    bottom.removeFromTop (4);
    bottom.removeFromBottom (gap);
    for (auto* panel : panels)
        panel->setBounds (bottom);

    hosted.setBounds (r.reduced (gap, gap));
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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
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

void ModdMuddEditor::showSlotMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, "Replace...");
    m.addItem (2, "Move left", index > 0);
    m.addItem (3, "Move right", index < (int) processor.getSlots().size() - 1);
    m.addSeparator();
    m.addItem (4, "Remove");

    juce::Component::SafePointer<ModdMuddEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[index]), [safe, index] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;

        switch (r)
        {
            case 1: safe->showLoadMenu (index); break;
            case 2: proc.moveSlot (index, index - 1); safe->selectedSlot = index - 1; break;
            case 3: proc.moveSlot (index, index + 1); safe->selectedSlot = index + 1; break;
            case 4: proc.removePlugin (index); safe->selectedSlot = juce::jmax (0, index - 1); break;
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
