#pragma once

#include "Widgets.h"

namespace mm
{

juce::String describeDestination (const ModdMuddProcessor& p, const Destination& d);

/** Shows a searchable menu of every modulation destination. */
void chooseDestination (ModdMuddProcessor& p, juce::Component& target, const juce::String& filter,
                        std::function<void (Destination)> onChosen);

/** The modulation matrix: one row per connection, plus a row for adding. */
class MatrixPanel : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::ChangeListener
{
public:
    explicit MatrixPanel (ModdMuddProcessor& p);
    ~MatrixPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Row;

    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ModdMuddProcessor& processor;
    juce::ComboBox sourceBox;
    juce::TextEditor search;
    juce::TextButton destButton { "Choose destination..." }, addButton { "Add" };
    juce::Label countLabel;
    juce::ListBox list;
    Destination chosen;
    bool hasChosen = false;
    std::vector<int> connectionIds;
};

} // namespace mm
