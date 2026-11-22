#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mm
{

/** Colours for one theme. */
struct Palette
{
    juce::String name;
    juce::Colour background, panel, panelHeader, inset, outline;
    juce::Colour text, textDim;
    juce::Colour value;     // knob arcs, active values (warm)
    juce::Colour active;    // toggles, graphs, selection (cool)
    juce::Colour warning;
};

class MuddLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MuddLookAndFeel();

    static int getNumThemes();
    static juce::String getThemeName (int index);

    void setTheme (int index);
    const Palette& getPalette() const noexcept   { return palette; }

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;

    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    static juce::Font font (float height, bool bold = false);

private:
    Palette palette;
};

} // namespace mm
