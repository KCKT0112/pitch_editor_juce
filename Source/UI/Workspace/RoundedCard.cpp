#include "RoundedCard.h"

RoundedCard::RoundedCard()
{
    setOpaque(false);
}

void RoundedCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw background with rounded corners
    g.setColour(backgroundColour);
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Draw border
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

void RoundedCard::paintOverChildren(juce::Graphics& g)
{
    // Create a rounded rectangle path for clipping effect
    auto bounds = getLocalBounds().toFloat();
    juce::Path clipPath;
    clipPath.addRoundedRectangle(bounds, cornerRadius);

    // Draw border on top to create clean rounded edge
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

void RoundedCard::resized()
{
    if (contentComponent != nullptr)
    {
        contentComponent->setBounds(getLocalBounds().reduced(padding));
    }
}

void RoundedCard::setContentComponent(juce::Component* content)
{
    if (contentComponent != nullptr)
        removeChildComponent(contentComponent);

    contentComponent = content;

    if (contentComponent != nullptr)
    {
        addAndMakeVisible(contentComponent);
        resized();
    }
}
