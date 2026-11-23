#include "Tutorial.h"
#include "../Host/PluginCatalog.h"

namespace mm
{

static constexpr int cardPadding = 14, buttonRow = 30;

TutorialOverlay::TutorialOverlay (std::vector<TutorialStep> s) : steps (std::move (s))
{
    setTitle ("ModdMudd tutorial");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    back.setTitle ("Previous step");
    next.setTitle ("Next step");
    skip.setTitle ("Close the tutorial");

    back.onClick = [this] { showStep (stepIndex - 1); };
    next.onClick = [this]
    {
        if (stepIndex + 1 >= (int) steps.size()) close();
        else                                     showStep (stepIndex + 1);
    };
    skip.onClick = [this] { close(); };

    addAndMakeVisible (back);
    addAndMakeVisible (next);
    addAndMakeVisible (skip);
}

static juce::File tutorialFlagFile()
{
    return PluginCatalog::getDataDirectory().getChildFile ("tutorial-seen");
}

bool TutorialOverlay::hasBeenSeen()   { return tutorialFlagFile().existsAsFile(); }
void TutorialOverlay::markSeen()      { tutorialFlagFile().replaceWithText ("1"); }

void TutorialOverlay::close()
{
    markSeen();

    // Deferred: the owner deletes this overlay, possibly from inside one of
    // its own button callbacks.
    juce::Component::SafePointer<TutorialOverlay> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr && safe->onClose)
            safe->onClose();
    });
}

void TutorialOverlay::showStep (int index)
{
    if (steps.empty())
        return;

    stepIndex = juce::jlimit (0, (int) steps.size() - 1, index);
    const auto& step = steps[(size_t) stepIndex];

    back.setEnabled (stepIndex > 0);
    next.setButtonText (stepIndex + 1 >= (int) steps.size() ? "Done" : "Next");
    skip.setVisible (stepIndex + 1 < (int) steps.size());

    const auto counter = "Step " + juce::String (stepIndex + 1) + " of " + juce::String ((int) steps.size());
    setDescription (counter + ". " + step.title + ". " + step.body);

    if (auto* handler = getAccessibilityHandler())
        handler->postAnnouncement (counter + ". " + step.title + ". " + step.body,
                                   juce::AccessibilityHandler::AnnouncementPriority::high);
    else
        juce::AccessibilityHandler::postAnnouncement (step.title + ". " + step.body,
                                                      juce::AccessibilityHandler::AnnouncementPriority::high);

    layoutCard();
    repaint();

    if (isShowing())
        next.grabKeyboardFocus();
}

juce::TextLayout TutorialOverlay::makeBodyLayout (float width) const
{
    const auto& p = paletteFor (*this);
    juce::AttributedString text;
    text.setWordWrap (juce::AttributedString::byWord);
    text.append (steps[(size_t) stepIndex].body, MuddLookAndFeel::font (13.5f), p.text);

    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

void TutorialOverlay::layoutCard()
{
    if (steps.empty() || getWidth() <= 0)
        return;

    const auto& step = steps[(size_t) stepIndex];
    highlight = step.area ? step.area().expanded (4) : juce::Rectangle<int>();
    highlight = highlight.getIntersection (getLocalBounds());

    const int width = juce::jmin (360, getWidth() - 24);
    const auto layout = makeBodyLayout ((float) (width - cardPadding * 2));
    const int height = cardPadding + 22 + 6 + (int) std::ceil (layout.getHeight()) + 12 + buttonRow + cardPadding;

    juce::Rectangle<int> c (width, height);

    if (highlight.isEmpty())
    {
        c.setCentre (getLocalBounds().getCentre());
    }
    else if (highlight.getBottom() + 12 + height <= getHeight())
    {
        c.setPosition (highlight.getCentreX() - width / 2, highlight.getBottom() + 12);   // below
    }
    else if (highlight.getY() - 12 - height >= 0)
    {
        c.setPosition (highlight.getCentreX() - width / 2, highlight.getY() - 12 - height); // above
    }
    else
    {
        c.setCentre (getLocalBounds().getCentre());                                         // too big: centre
    }

    card = c.constrainedWithin (getLocalBounds().reduced (8));

    auto buttons = card.reduced (cardPadding).removeFromBottom (buttonRow);
    next.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (6);
    back.setBounds (buttons.removeFromRight (70));
    skip.setBounds (buttons.removeFromLeft (86));
}

void TutorialOverlay::resized()
{
    layoutCard();
}

void TutorialOverlay::refresh()
{
    // Follow the highlighted area if the editor's layout changed.
    if (! steps.empty() && steps[(size_t) stepIndex].area)
    {
        const auto now = steps[(size_t) stepIndex].area().expanded (4).getIntersection (getLocalBounds());
        if (now != highlight)
        {
            layoutCard();
            repaint();
        }
    }
}

void TutorialOverlay::paint (juce::Graphics& g)
{
    if (steps.empty())
        return;

    const auto& p = paletteFor (*this);

    // Dim everything except the highlighted area.
    juce::Path shade;
    shade.addRectangle (getLocalBounds().toFloat());
    if (! highlight.isEmpty())
    {
        shade.addRoundedRectangle (highlight.toFloat(), 6.0f);
        shade.setUsingNonZeroWinding (false);
    }

    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillPath (shade);

    if (! highlight.isEmpty())
    {
        g.setColour (p.value);
        g.drawRoundedRectangle (highlight.toFloat(), 6.0f, 2.5f);
    }

    // The card
    g.setColour (p.panel);
    g.fillRoundedRectangle (card.toFloat(), 8.0f);
    g.setColour (p.value);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 8.0f, 1.5f);

    auto inner = card.reduced (cardPadding);
    auto header = inner.removeFromTop (22);

    g.setColour (p.textDim);
    g.setFont (MuddLookAndFeel::font (11.5f, true));
    g.drawText (juce::String (stepIndex + 1) + " / " + juce::String ((int) steps.size()), header.removeFromRight (50),
                juce::Justification::centredRight);

    g.setColour (p.text);
    g.setFont (MuddLookAndFeel::font (16.0f, true));
    g.drawText (steps[(size_t) stepIndex].title, header, juce::Justification::centredLeft, true);

    inner.removeFromTop (6);
    makeBodyLayout ((float) inner.getWidth()).draw (g, inner.toFloat());
}

bool TutorialOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)                                    { close(); return true; }
    if (k == juce::KeyPress::rightKey)                                     { next.triggerClick(); return true; }
    if (k == juce::KeyPress::leftKey && stepIndex > 0)                     { showStep (stepIndex - 1); return true; }
    return false;
}

} // namespace mm
