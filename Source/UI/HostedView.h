#pragma once

#include "Widgets.h"

namespace mm
{

/** A searchable list of the hosted plug-in's parameters. Works with every
    plug-in (including ones whose own window can't be embedded), accepts
    dragged modulation sources and shows modulation live. */
class GenericParamView : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    GenericParamView (ModdMuddProcessor& p, int slotIndex);
    ~GenericParamView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Row;

    int getNumRows() override                      { return (int) visible.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyFilter();

    ModdMuddProcessor& processor;
    int slotIndex;
    juce::TextEditor search;
    juce::Label title;
    juce::ListBox list;
    std::vector<int> visible;
};

/** The centre of the editor: the hosted plug-in's own window, or the generic
    parameter list. */
class HostedView : public juce::Component,
                   private juce::ComponentListener
{
public:
    explicit HostedView (ModdMuddProcessor& p);
    ~HostedView() override;

    /** Shows a slot. preferNative uses the plug-in's own editor if it has one. */
    void showSlot (int slotIndex, bool preferNative);

    /** Deletes the hosted editor. Must be called before the plug-in is destroyed. */
    void clear();

    bool isShowingNative() const noexcept   { return editor != nullptr; }
    juce::Point<int> getPreferredSize() const;

    std::function<void()> onPreferredSizeChanged;
    std::function<void()> onLoadRequested;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    ModdMuddProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<GenericParamView> generic;
    juce::TextButton loadButton { "Load a plug-in..." };
    juce::String message;
};

} // namespace mm
