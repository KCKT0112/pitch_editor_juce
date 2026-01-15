#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/Constants.h"

/**
 * A rounded card container component with consistent styling.
 * Used for piano roll, panels, and other content areas.
 */
class RoundedCard : public juce::Component
{
public:
    RoundedCard();
    ~RoundedCard() override = default;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void setContentComponent(juce::Component* content);
    juce::Component* getContentComponent() const { return contentComponent; }

    void setCornerRadius(float radius) { cornerRadius = radius; repaint(); }
    void setBackgroundColour(juce::Colour colour) { backgroundColour = colour; repaint(); }
    void setBorderColour(juce::Colour colour) { borderColour = colour; repaint(); }
    void setPadding(int p) { padding = p; resized(); }

private:
    juce::Component* contentComponent = nullptr;
    float cornerRadius = 8.0f;
    juce::Colour backgroundColour { 0xFF2D2D37 };
    juce::Colour borderColour { 0xFF3A3A45 };
    int padding = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoundedCard)
};
