#pragma once

#include "Widgets.h"

namespace mm
{

//==============================================================================
/** A two-parameter pad (XY modulator, blender position). */
class XYPadComponent : public juce::Component, private juce::Timer
{
public:
    XYPadComponent (ModdMuddProcessor& p, const juce::String& xId, const juce::String& yId,
                    std::function<juce::Point<float>()> livePosition, bool yUpIsOne,
                    juce::StringArray cornerLabels = {});

    void setCentreIds (const juce::String& cx, const juce::String& cy)   { centreX = cx; centreY = cy; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override   { repaint(); }
    void setFromPosition (juce::Point<float>);
    juce::RangedAudioParameter* param (const juce::String& id) const;
    juce::Point<float> toScreen (float x, float y) const;

    ModdMuddProcessor& processor;
    juce::String xId, yId, centreX, centreY;
    std::function<juce::Point<float>()> live;
    bool yUp;
    juce::StringArray corners;
};

//==============================================================================
class WaveView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    WaveView (ModdMuddProcessor& p, int lfoIndex);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override   { repaint(); }
    bool isCustom() const;
    juce::Point<float> pointToScreen (const WavePoint&) const;
    int hitPoint (juce::Point<float> pos) const;
    void commit();

    ModdMuddProcessor& processor;
    int lfo;
    std::vector<WavePoint> points;
    std::vector<float> table;
    int dragIndex = -1;
    bool curveDrag = false;
    float curveStart = 0.0f;
};

//==============================================================================
class LfoPanel : public juce::Component, private juce::Timer
{
public:
    LfoPanel (ModdMuddProcessor& p, int index);
    void resized() override;

private:
    void timerCallback() override;

    ModdMuddProcessor& processor;
    int index;
    Section section, controls;
    WaveView wave;
    ParamChoice shape, division, retrig;
    Knob rate, phase, depth, offset, fade;
    ParamToggle sync, invert;
    SourceChip chip;
    juce::Label shapeLabel, retrigLabel, divisionLabel;
};

class RandomPanel : public juce::Component, private juce::Timer
{
public:
    RandomPanel (ModdMuddProcessor& p, int index);
    void resized() override;
    void paint (juce::Graphics&) override {}

private:
    struct History : juce::Component
    {
        std::array<float, 160> values {};
        int write = 0;
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    void showPresets();
    void applyPreset (std::initializer_list<std::pair<const char*, float>> values);

    ModdMuddProcessor& processor;
    int index;
    Section section, controls;
    History history;
    ParamChoice division;
    Knob rate, probability, glide, depth, offset;
    ParamToggle sync, smooth, independent;
    juce::TextButton seedButton, presetsButton;
    SourceChip chip;
};

class MacroPanel : public juce::Component
{
public:
    explicit MacroPanel (ModdMuddProcessor& p);
    void resized() override;

private:
    Section section;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<SourceChip> chips;
};

class XYPanel : public juce::Component
{
public:
    explicit XYPanel (ModdMuddProcessor& p);
    void resized() override;

private:
    Section section, controls;
    XYPadComponent pad;
    Knob x, y, centreX, centreY, glide;
    ParamToggle bipolar;
    SourceChip chipX, chipY;
};

class BlendPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit BlendPanel (ModdMuddProcessor& p);
    ~BlendPanel() override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }
    void refresh();
    void snapshotClicked (int index, bool popup);

    ModdMuddProcessor& processor;
    Section section, controls;
    XYPadComponent pad;
    ParamToggle enabled;
    Knob blendX, blendY;
    std::array<juce::TextButton, kNumSnapshots> snapButtons;
    juce::Label info;
};

class EnvPanel : public juce::Component, private juce::Timer
{
public:
    EnvPanel (ModdMuddProcessor& p, int index);
    void resized() override;

private:
    struct Meter : juce::Component
    {
        std::array<float, 160> values {};
        int write = 0;
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;

    ModdMuddProcessor& processor;
    int index;
    Section section, controls;
    Meter meter;
    ParamChoice source, detector;
    Knob attack, release, gain, threshold, sensitivity, curve, smooth, depth;
    ParamToggle invert;
    SourceChip chip;
    juce::Label sourceLabel, detectorLabel;
};

} // namespace mm
