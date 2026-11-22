#include "MuddLookAndFeel.h"

namespace mm
{

static const Palette& themeAt (int index)
{
    using C = juce::Colour;

    static const Palette themes[] {
        { "Slate",
          C (0xff2a2d32), C (0xff34383e), C (0xff3d4249), C (0xff1d2024), C (0xff222529),
          C (0xffdde1e6), C (0xff959ca5), C (0xffe9a23b), C (0xff39b3d6), C (0xffe0604a) },

        { "Midnight",
          C (0xff15171c), C (0xff1d2027), C (0xff252932), C (0xff0f1115), C (0xff0b0c0f),
          C (0xffd5d9e2), C (0xff7f8796), C (0xffef8f4f), C (0xff6c8cff), C (0xffff6670) },

        { "High Contrast",
          C (0xff000000), C (0xff0d0d0d), C (0xff1a1a1a), C (0xff000000), C (0xffffffff),
          C (0xffffffff), C (0xffd8d8d8), C (0xffffd400), C (0xff00e0ff), C (0xffff5050) },
    };

    return themes[juce::jlimit (0, (int) std::size (themes) - 1, index)];
}

int MuddLookAndFeel::getNumThemes()                    { return 3; }
juce::String MuddLookAndFeel::getThemeName (int index) { return themeAt (index).name; }

MuddLookAndFeel::MuddLookAndFeel()
{
    setTheme (0);
}

juce::Font MuddLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void MuddLookAndFeel::setTheme (int index)
{
    palette = themeAt (index);
    const auto& p = palette;

    // The colour scheme resets every colour, so it must come first.
    auto scheme = getCurrentColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, p.background);
    scheme.setUIColour (ColourScheme::widgetBackground, p.panel);
    scheme.setUIColour (ColourScheme::menuBackground, p.panel);
    scheme.setUIColour (ColourScheme::outline, p.outline);
    scheme.setUIColour (ColourScheme::defaultText, p.text);
    scheme.setUIColour (ColourScheme::defaultFill, p.active);
    scheme.setUIColour (ColourScheme::highlightedText, juce::Colours::black);
    scheme.setUIColour (ColourScheme::highlightedFill, p.active);
    scheme.setUIColour (ColourScheme::menuText, p.text);
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, p.background);
    setColour (juce::DocumentWindow::backgroundColourId, p.background);

    setColour (juce::Label::textColourId, p.text);
    setColour (juce::Label::textWhenEditingColourId, p.text);

    setColour (juce::Slider::rotarySliderFillColourId, p.value);
    setColour (juce::Slider::rotarySliderOutlineColourId, p.inset);
    setColour (juce::Slider::thumbColourId, p.text);
    setColour (juce::Slider::trackColourId, p.value);
    setColour (juce::Slider::backgroundColourId, p.inset);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::Slider::textBoxBackgroundColourId, p.inset);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId, p.panelHeader);
    setColour (juce::TextButton::buttonOnColourId, p.active);
    setColour (juce::TextButton::textColourOffId, p.text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    setColour (juce::ToggleButton::textColourId, p.text);
    setColour (juce::ToggleButton::tickColourId, p.active);

    setColour (juce::ComboBox::backgroundColourId, p.inset);
    setColour (juce::ComboBox::textColourId, p.text);
    setColour (juce::ComboBox::outlineColourId, p.outline);
    setColour (juce::ComboBox::arrowColourId, p.textDim);

    setColour (juce::PopupMenu::backgroundColourId, p.panel);
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.active.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, p.textDim);

    setColour (juce::TextEditor::backgroundColourId, p.inset);
    setColour (juce::TextEditor::textColourId, p.text);
    setColour (juce::TextEditor::outlineColourId, p.outline);
    setColour (juce::TextEditor::focusedOutlineColourId, p.active);
    setColour (juce::TextEditor::highlightColourId, p.active.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, p.active);

    setColour (juce::ListBox::backgroundColourId, p.inset);
    setColour (juce::ListBox::outlineColourId, p.outline);
    setColour (juce::ScrollBar::thumbColourId, p.textDim.withAlpha (0.5f));

    setColour (juce::TooltipWindow::backgroundColourId, p.panelHeader);
    setColour (juce::TooltipWindow::textColourId, p.text);
    setColour (juce::TooltipWindow::outlineColourId, p.outline);

}

juce::Font MuddLookAndFeel::getLabelFont (juce::Label& l)                 { return font (juce::jmin (13.0f, (float) l.getHeight() * 0.8f)); }
juce::Font MuddLookAndFeel::getTextButtonFont (juce::TextButton&, int h)  { return font (juce::jmin (13.0f, (float) h * 0.55f), true); }
juce::Font MuddLookAndFeel::getComboBoxFont (juce::ComboBox& c)           { return font (juce::jmin (13.0f, (float) c.getHeight() * 0.6f)); }
juce::Font MuddLookAndFeel::getPopupMenuFont()                            { return font (14.0f); }

//==============================================================================
void MuddLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 2.0f;
    const float lineW = juce::jmax (2.5f, radius * 0.14f);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = slider.isEnabled();
    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (palette.inset);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    auto valueColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    g.setColour (enabled ? valueColour : palette.textDim.withAlpha (0.4f));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Modulation ring (set by Knob for cross-modulation targets)
    if (slider.getProperties().contains ("modAmount"))
    {
        const float mod = (float) slider.getProperties()["modAmount"];
        const float modAngle = juce::jlimit (startAngle, endAngle, angle + mod * (endAngle - startAngle));
        juce::Path ring;
        const float r2 = arcRadius + lineW * 0.9f;
        ring.addCentredArc (centre.x, centre.y, r2, r2, 0.0f, juce::jmin (angle, modAngle), juce::jmax (angle, modAngle), true);
        g.setColour (palette.active);
        g.strokePath (ring, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body
    const float bodyR = arcRadius - lineW * 1.1f;
    juce::ColourGradient grad (palette.panelHeader.brighter (0.25f), centre.x, centre.y - bodyR,
                               palette.panel.darker (0.35f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (palette.outline);
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer
    const juce::Point<float> tip (centre.x + std::sin (angle) * bodyR * 0.85f, centre.y - std::cos (angle) * bodyR * 0.85f);
    const juce::Point<float> inner (centre.x + std::sin (angle) * bodyR * 0.35f, centre.y - std::cos (angle) * bodyR * 0.35f);
    g.setColour (enabled ? palette.text : palette.textDim);
    g.drawLine ({ inner, tip }, juce::jmax (1.5f, radius * 0.08f));

    if (slider.hasKeyboardFocus (true))
    {
        g.setColour (palette.active.withAlpha (0.6f));
        g.drawEllipse (bounds, 1.0f);
    }
}

void MuddLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                        juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float trackH = juce::jmin (6.0f, r.getHeight() * 0.35f);
    auto track = r.withSizeKeepingCentre (r.getWidth(), trackH);

    // Lists use the inset colour as their background, so the track needs to
    // stand out from it.
    g.setColour (palette.panelHeader.brighter (0.1f));
    g.fillRoundedRectangle (track, trackH * 0.5f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroX = bipolar ? (float) slider.getPositionOfValue (0.0) : track.getX();
    auto fill = track.withLeft (juce::jmin (zeroX, sliderPos)).withRight (juce::jmax (zeroX, sliderPos));

    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (fill, trackH * 0.5f);

    // Modulated position marker (generic parameter rows)
    if (slider.getProperties().contains ("modValue"))
    {
        const float mv = (float) slider.getProperties()["modValue"];
        const float mx = track.getX() + mv * track.getWidth();
        g.setColour (palette.active);
        g.fillRect (juce::Rectangle<float> (mx - 1.5f, r.getY() + 2.0f, 3.0f, r.getHeight() - 4.0f));
    }

    g.setColour (palette.text);
    g.fillEllipse (juce::Rectangle<float> (trackH * 2.0f, trackH * 2.0f).withCentre ({ sliderPos, track.getCentreY() }));
}

void MuddLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    // Pill switch on the left, label on the right.
    const float h = juce::jmin (16.0f, r.getHeight());
    auto pill = r.removeFromLeft (h * 1.9f).withSizeKeepingCentre (h * 1.9f, h);

    g.setColour (on ? palette.active : palette.inset);
    g.fillRoundedRectangle (pill, h * 0.5f);
    g.setColour (palette.outline);
    g.drawRoundedRectangle (pill, h * 0.5f, 1.0f);

    const float knobD = h - 4.0f;
    const float kx = on ? pill.getRight() - knobD - 2.0f : pill.getX() + 2.0f;
    g.setColour (on ? juce::Colours::white : palette.textDim);
    g.fillEllipse (kx, pill.getY() + 2.0f, knobD, knobD);

    if (highlighted || b.hasKeyboardFocus (false))
    {
        g.setColour (palette.active.withAlpha (0.5f));
        g.drawRoundedRectangle (pill.expanded (1.5f), h * 0.5f + 1.5f, 1.0f);
    }

    g.setColour (b.isEnabled() ? palette.text : palette.textDim);
    g.setFont (font (juce::jmin (13.0f, r.getHeight() * 0.8f)));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft, 1);
}

void MuddLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                            bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : background;

    if (down)             c = c.darker (0.2f);
    else if (highlighted) c = c.brighter (0.1f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (b.hasKeyboardFocus (false) ? palette.active : palette.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void MuddLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (false) ? palette.active : palette.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (palette.textDim);
    g.fillPath (arrow);
}

void MuddLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? palette.active : palette.outline);
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), 3.0f, 1.0f);
}

void MuddLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), 3.0f);
}

} // namespace mm
