#pragma once

#include "Widgets.h"
#include "HostedView.h"
#include "ModulatorPanels.h"
#include "MatrixPanel.h"

namespace mm
{

class ModdMuddEditor final : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
                             private ModdMuddProcessor::Listener,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit ModdMuddEditor (ModdMuddProcessor&);
    ~ModdMuddEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void slotsAboutToChange() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildSlotButtons();
    void showSelectedSlot();
    void updateSize();
    void selectTab (int index);
    void layoutPanel (juce::Rectangle<int> panel);

    void showLoadMenu (int replaceIndex);
    void showSlotMenu();
    void showSettingsMenu();
    void showPresetMenu();

    ModdMuddProcessor& processor;
    MuddLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton slotButton, addButton { "+" }, viewButton, presetButton { "Presets" }, settingsButton { "Settings" };
    HostedView hosted;

    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::OwnedArray<juce::Component> panels;
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;
    int selectedSlot = 0;
    int selectedTab = 0;
    bool preferNative = true;
    juce::String lastCatalogStatus;
    juce::StringArray slotUidsShown;
    bool forwardingResize = false;   // resizing the hosted plug-in to follow the window

public:
    // The modulation panel snaps under the hosted plug-in at the plug-in's
    // own width. It needs at least minPanelWidth to stay usable.
    static constexpr int stripHeight = 30, tabsHeight = 24, contentHeight = 190, statusHeight = 20, pad = 6;
    static constexpr int panelHeight = stripHeight + pad + tabsHeight + 4 + contentHeight + pad + statusHeight;
    static constexpr int minPanelWidth = 660, defaultWidth = 900, defaultCentreHeight = 440;

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModdMuddEditor)
};

} // namespace mm
