#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Plugin/PluginProcessor.h"
#include "MuddLookAndFeel.h"

namespace mm
{

const Palette& paletteFor (const juce::Component& c);

/** Something that redraws live values. The editor runs one shared timer and
    refreshes only components that are showing, instead of every widget
    running its own timer (which also keeps hidden panels busy). */
struct Refreshable
{
    virtual ~Refreshable() = default;
    virtual void refresh() = 0;
};

/** Calls refresh() on every visible Refreshable under root. Hidden
    subtrees are skipped. */
void refreshShowing (juce::Component& root);

/** Drag-and-drop description used for modulation sources. */
inline juce::String sourceDragId (int source)       { return "mmsrc:" + juce::String (source); }
inline int sourceFromDragId (const juce::var& d)    { auto s = d.toString(); return s.startsWith ("mmsrc:") ? s.substring (6).getIntValue() : -1; }

/** One item in a flexible row: preferred and minimum width in pixels. */
struct FlexItem
{
    juce::Component* component = nullptr;
    int preferred = 0;
    int minimum = 0;
};

/** Splits a row into widths: preferred widths if they fit, otherwise each
    item shrinks towards its minimum in proportion to how much it can give
    up. Items given a width of zero end up hidden. Leftover space stays on
    the right. */
std::vector<juce::Rectangle<int>> flexRects (juce::Rectangle<int> area, const std::vector<std::pair<int, int>>& sizes, int gap = 6);

void layoutFlex (juce::Rectangle<int> area, std::initializer_list<FlexItem> items, int gap = 6);

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

/** The pop-up list of one source's targets (depth and remove per target). */
std::unique_ptr<juce::Component> createTargetList (ModdMuddProcessor& p, int source);

//==============================================================================
/** A modulation source handle: drag it onto a parameter, or click it and
    move a control in the hosted plug-in to assign by touch. Shows the
    source's live value. */
class SourceChip : public juce::Component,
                   public juce::SettableTooltipClient,
                   public Refreshable
{
public:
    SourceChip (ModdMuddProcessor& p, int source, const juce::String& text = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void refresh() override;

    /** Opens the pop-up listing this source's targets. */
    void showTargets();

private:
    juce::Rectangle<int> badgeArea() const;
    int countTargets() const;

    ModdMuddProcessor& processor;
    int source;
    juce::String text;
    float shownValue = 0.0f;
    int targetCount = 0;
    bool dragging = false;
};

} // namespace mm
