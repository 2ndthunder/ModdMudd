#pragma once

#include "Widgets.h"

namespace mm
{

/** One step of the guided tour. area returns the part of the editor to
    highlight, in the overlay's coordinates (empty: no highlight). */
struct TutorialStep
{
    juce::String title, body;
    std::function<juce::Rectangle<int>()> area;
};

/** A guided tour drawn over the editor: everything is dimmed except the
    area being explained, with a card holding the explanation and Back /
    Next / Skip. Keyboard: Left/Right to move, Escape to close. Each step is
    announced to screen readers. */
class TutorialOverlay final : public juce::Component,
                              public Refreshable
{
public:
    explicit TutorialOverlay (std::vector<TutorialStep> steps);

    /** Called when the tour is finished or skipped. */
    std::function<void()> onClose;

    void showStep (int index);
    int getStepIndex() const noexcept   { return stepIndex; }
    int getNumSteps() const noexcept    { return (int) steps.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}   // the tour is modal
    void refresh() override;

    /** Whether the tour has been shown on this computer before. */
    static bool hasBeenSeen();
    static void markSeen();

private:
    juce::TextLayout makeBodyLayout (float width) const;
    void layoutCard();
    void close();

    std::vector<TutorialStep> steps;
    int stepIndex = 0;
    juce::Rectangle<int> highlight, card;
    juce::TextButton back { "Back" }, next { "Next" }, skip { "Skip tour" };
};

} // namespace mm
