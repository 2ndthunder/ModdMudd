#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Plugin/PluginProcessor.h"
#include "MuddLookAndFeel.h"

namespace mm
{

const Palette& paletteFor (const juce::Component& c);

/** Drag-and-drop description used for modulation sources. */
inline juce::String sourceDragId (int source)       { return "mmsrc:" + juce::String (source); }
inline int sourceFromDragId (const juce::var& d)    { auto s = d.toString(); return s.startsWith ("mmsrc:") ? s.substring (6).getIntValue() : -1; }

/** Explains a failed connection in plain words. */
juce::String describeAddResult (ModMatrix::AddResult r);

//==============================================================================
/** A titled panel, like the boxed sections of a hardware-style instrument. */
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& title = {});
    void setTitleText (const juce::String& t)   { titleText = t; repaint(); }
    void paint (juce::Graphics&) override;
    /** The area inside the frame, in the parent component's coordinates. */
    juce::Rectangle<int> getContentBounds() const;

    static constexpr int headerHeight = 20;

private:
    juce::String titleText;
};

//==============================================================================
class MuddSlider : public juce::Slider
{
public:
    std::function<void()> onPopup;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onPopup)
            onPopup();
        else
            juce::Slider::mouseDown (e);
    }
};

/** A rotary knob bound to a ModdMudd parameter, with a name label, MIDI
    learn and (optionally) a cross-modulation drop target. */
class Knob : public juce::Component,
             public juce::DragAndDropTarget,
             private juce::ChangeListener
{
public:
    Knob (ModdMuddProcessor& p, const juce::String& paramId, const juce::String& label,
          int internalTarget = -1, bool bipolar = false);
    ~Knob() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override   { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override    { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    MuddSlider slider;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showMenu();
    void updateModRing();

    ModdMuddProcessor& processor;
    juce::String paramId, label;
    int internalTarget;
    bool dragOver = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (ModdMuddProcessor& p, const juce::String& paramId, const juce::String& text);

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

class ParamChoice : public juce::ComboBox
{
public:
    ParamChoice (ModdMuddProcessor& p, const juce::String& paramId);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
/** A modulation source handle: drag it onto a parameter, or click it and
    move a control in the hosted plug-in to assign by touch. Shows the
    source's live value. */
class SourceChip : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    SourceChip (ModdMuddProcessor& p, int source, const juce::String& text = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    ModdMuddProcessor& processor;
    int source;
    juce::String text;
    float shownValue = 0.0f;
    bool dragging = false;
};

} // namespace mm
