#include "SidebarComponent.h"
#include "../../Utils/SvgUtils.h"

// SidebarButton implementation
SidebarButton::SidebarButton(const juce::String& id, const juce::String& tooltip)
    : buttonId(id), tooltipText(tooltip)
{
}

void SidebarButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(4);

    // Background
    if (active)
        g.setColour(juce::Colour(COLOR_PRIMARY).withAlpha(0.3f));
    else if (hovered)
        g.setColour(juce::Colour(0xFF4D4D57));
    else
        g.setColour(juce::Colours::transparentBlack);

    g.fillRoundedRectangle(bounds, 6.0f);

    // Active indicator bar on the left
    if (active)
    {
        g.setColour(juce::Colour(COLOR_PRIMARY));
        g.fillRoundedRectangle(0.0f, bounds.getY() + 8, 3.0f, bounds.getHeight() - 16, 1.5f);
    }

    // Icon
    if (iconDrawable != nullptr)
    {
        auto iconBounds = bounds.reduced(8);
        iconDrawable->setTransformToFit(iconBounds, juce::RectanglePlacement::centred);

        if (active)
            iconDrawable->replaceColour(juce::Colours::white, juce::Colour(COLOR_PRIMARY));
        else
            iconDrawable->replaceColour(juce::Colour(COLOR_PRIMARY), juce::Colours::white);

        iconDrawable->draw(g, active || hovered ? 1.0f : 0.7f);
    }
}

void SidebarButton::mouseEnter(const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void SidebarButton::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void SidebarButton::mouseDown(const juce::MouseEvent&)
{
    if (onClick)
        onClick(buttonId);
}

void SidebarButton::setActive(bool newActive)
{
    if (active != newActive)
    {
        active = newActive;
        repaint();
    }
}

void SidebarButton::setIcon(std::unique_ptr<juce::Drawable> icon)
{
    iconDrawable = std::move(icon);
    repaint();
}

// SidebarComponent implementation
SidebarComponent::SidebarComponent()
{
    setOpaque(false);
}

void SidebarComponent::paint(juce::Graphics& g)
{
    // Only horizontal padding, no vertical - to match panel height
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromLeft(8);
    bounds.removeFromRight(8);

    // Background with rounded corners matching panel style
    g.setColour(juce::Colour(0xFF2D2D37));
    g.fillRoundedRectangle(bounds, 8.0f);
}

void SidebarComponent::resized()
{
    // Buttons should be centered within the rounded background
    int y = 6; // Start position for first button
    int centerX = getWidth() / 2;
    for (auto* button : buttons)
    {
        button->setBounds(centerX - buttonSize / 2, y, buttonSize, buttonSize);
        y += buttonSize + 2;
    }
}

void SidebarComponent::addButton(const juce::String& id, const juce::String& tooltip, const juce::String& svgData)
{
    auto* button = new SidebarButton(id, tooltip);

    if (svgData.isNotEmpty())
    {
        auto icon = SvgUtils::createDrawableFromSvg(svgData);
        button->setIcon(std::move(icon));
    }

    button->onClick = [this](const juce::String& buttonId) { handleButtonClick(buttonId); };

    addAndMakeVisible(button);
    buttons.add(button);
    resized();
}

void SidebarComponent::setButtonActive(const juce::String& id, bool active)
{
    for (auto* button : buttons)
    {
        if (button->getId() == id)
        {
            button->setActive(active);
            break;
        }
    }
}

bool SidebarComponent::isButtonActive(const juce::String& id) const
{
    for (auto* button : buttons)
    {
        if (button->getId() == id)
            return button->isActive();
    }
    return false;
}

void SidebarComponent::handleButtonClick(const juce::String& id)
{
    // Toggle the button state
    bool newState = !isButtonActive(id);
    setButtonActive(id, newState);

    if (onPanelToggled)
        onPanelToggled(id, newState);
}
