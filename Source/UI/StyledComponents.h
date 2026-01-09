#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"

/**
 * Shared dark theme LookAndFeel for the application.
 */
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                          bool isSeparator, bool isActive, bool isHighlighted,
                          bool isTicked, bool hasSubMenu,
                          const juce::String& text, const juce::String& shortcutKeyText,
                          const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawTickBox(juce::Graphics& g, juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    static DarkLookAndFeel& getInstance()
    {
        static DarkLookAndFeel instance;
        return instance;
    }
};

/**
 * Pre-styled slider with dark theme colors.
 */
class StyledSlider : public juce::Slider
{
public:
    StyledSlider()
    {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2D2D37));
        setColour(juce::Slider::trackColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.6f));
        setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2D2D37));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
};

/**
 * Pre-styled combo box with dark theme colors.
 */
class StyledComboBox : public juce::ComboBox
{
public:
    StyledComboBox()
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF3D3D47));
        setColour(juce::ComboBox::textColourId, juce::Colours::white);
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF4A4A55));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(COLOR_PRIMARY));
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledComboBox() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled toggle button with custom checkbox.
 */
class StyledToggleButton : public juce::ToggleButton
{
public:
    StyledToggleButton(const juce::String& buttonText = {}) : juce::ToggleButton(buttonText)
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledToggleButton() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled label with light grey text.
 */
class StyledLabel : public juce::Label
{
public:
    StyledLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }
};

/**
 * Section header label with primary color.
 */
class SectionLabel : public juce::Label
{
public:
    SectionLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, juce::Colour(COLOR_PRIMARY));
        setFont(juce::Font(14.0f, juce::Font::bold));
    }
};
