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

    void showLoadMenu (int replaceIndex);
    void showSlotMenu (int index);
    void showSettingsMenu();
    void showPresetMenu();

    ModdMuddProcessor& processor;
    MuddLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::OwnedArray<juce::TextButton> slotButtons;
    juce::TextButton addButton { "+ Load" }, viewButton, presetButton { "Presets" }, settingsButton { "Settings" };
    HostedView hosted;

    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::OwnedArray<juce::Component> panels;
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;
    int selectedSlot = 0;
    int selectedTab = 0;
    bool preferNative = true;
    juce::String lastCatalogStatus;
    int slotCountShown = -1;

    static constexpr int topBarHeight = 44, bottomHeight = 260, statusHeight = 24, gap = 6, minWidth = 1000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModdMuddEditor)
};

} // namespace mm
