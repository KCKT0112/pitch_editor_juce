#include "StyledComponents.h"

DarkLookAndFeel::DarkLookAndFeel()
{
    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF3D3D47));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(COLOR_PRIMARY));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF3D3D47));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF4A4A55));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(COLOR_PRIMARY));
    setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(COLOR_PRIMARY));

    // Label
    setColour(juce::Label::textColourId, juce::Colours::white);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    // TextButton
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3D3D47));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(COLOR_PRIMARY));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    // ListBox
    setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF2D2D37));
    setColour(juce::ListBox::textColourId, juce::Colours::white);
    setColour(juce::ListBox::outlineColourId, juce::Colour(0xFF4A4A55));

    // ScrollBar
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xFF5A5A65));
    setColour(juce::ScrollBar::trackColourId, juce::Colour(0xFF2D2D37));

    // TextEditor
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2D2D37));
    setColour(juce::TextEditor::textColourId, juce::Colours::white);
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF4A4A55));
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(COLOR_PRIMARY));
    setColour(juce::CaretComponent::caretColourId, juce::Colour(COLOR_PRIMARY));

    // AlertWindow / DialogWindow
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(COLOR_BACKGROUND));
    setColour(juce::AlertWindow::textColourId, juce::Colours::white);
    setColour(juce::AlertWindow::outlineColourId, juce::Colour(0xFF4A4A55));
}

void DarkLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(juce::Colour(0xFF3D3D47));
    g.setColour(juce::Colour(0xFF4A4A55));
    g.drawRect(0, 0, width, height, 1);
}

void DarkLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu,
                                        const juce::String& text, const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon, const juce::Colour* textColour)
{
    juce::ignoreUnused(hasSubMenu, shortcutKeyText, icon, textColour);

    if (isSeparator)
    {
        auto r = area.reduced(5, 0).withHeight(1).withY(area.getCentreY());
        g.setColour(juce::Colour(0xFF4A4A55));
        g.fillRect(r);
        return;
    }

    auto textArea = area.reduced(10, 0);

    if (isHighlighted && isActive)
    {
        g.setColour(juce::Colour(COLOR_PRIMARY));
        g.fillRect(area);
        g.setColour(juce::Colours::white);
    }
    else
    {
        g.setColour(isActive ? juce::Colours::white : juce::Colours::grey);
    }

    g.setFont(juce::Font(14.0f));
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

    if (isTicked)
    {
        auto tickArea = area.withLeft(area.getRight() - area.getHeight()).reduced(6);
        g.drawText(juce::String::charToString(0x2713), tickArea, juce::Justification::centred);
    }
}

void DarkLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component,
                                   float x, float y, float w, float h,
                                   bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(component, shouldDrawButtonAsDown);

    auto boxSize = std::min(w, h) * 0.9f;
    auto boxX = x + (w - boxSize) * 0.5f;
    auto boxY = y + (h - boxSize) * 0.5f;
    auto cornerSize = boxSize * 0.2f;

    juce::Rectangle<float> boxBounds(boxX, boxY, boxSize, boxSize);

    if (ticked)
    {
        g.setColour(juce::Colour(COLOR_PRIMARY));
        g.fillRoundedRectangle(boxBounds, cornerSize);

        g.setColour(juce::Colours::white);
        auto tick = boxBounds.reduced(boxSize * 0.25f);
        juce::Path path;
        path.startNewSubPath(tick.getX(), tick.getCentreY());
        path.lineTo(tick.getX() + tick.getWidth() * 0.35f, tick.getBottom());
        path.lineTo(tick.getRight(), tick.getY());
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }
    else
    {
        auto alpha = isEnabled ? (shouldDrawButtonAsHighlighted ? 1.0f : 0.7f) : 0.4f;
        g.setColour(juce::Colour(0xFF4A4A55).withAlpha(alpha));
        g.drawRoundedRectangle(boxBounds, cornerSize, 1.5f);
    }
}
