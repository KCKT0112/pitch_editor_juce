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

/**
 * Custom styled message box component matching the app's dark theme.
 */
class StyledMessageBox : public juce::Component
{
public:
    enum IconType
    {
        NoIcon,
        InfoIcon,
        WarningIcon,
        ErrorIcon
    };

    StyledMessageBox(const juce::String& title, const juce::String& message, IconType iconType = NoIcon)
        : titleText(title), messageText(message), iconType(iconType)
    {
        setOpaque(true);
        
        // Add OK button
        okButton = std::make_unique<juce::TextButton>("OK");
        okButton->setSize(80, 32);
        okButton->onClick = [this] { 
            if (onClose != nullptr)
                onClose();
        };
        addAndMakeVisible(okButton.get());
        
        // Style the button
        okButton->setColour(juce::TextButton::buttonColourId, juce::Colour(COLOR_PRIMARY));
        okButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        okButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(COLOR_PRIMARY).brighter(0.2f));
        
        setSize(400, 200);
    }
    
    void resized() override
    {
        if (okButton != nullptr)
        {
            okButton->setCentrePosition(getWidth() / 2, getHeight() - 30);
        }
    }
    
    std::function<void()> onClose;

    void paint(juce::Graphics& g) override
    {
        // Background
        g.fillAll(juce::Colour(COLOR_BACKGROUND));

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(titleText, 20, 20, getWidth() - 40, 30, juce::Justification::left);

        // Icon (if any)
        int iconX = 20;
        int iconY = 60;
        int iconSize = 32;
        
        if (iconType != NoIcon)
        {
            juce::Colour iconColour;
            if (iconType == InfoIcon)
                iconColour = juce::Colour(COLOR_PRIMARY);
            else if (iconType == WarningIcon)
                iconColour = juce::Colour(0xFFFFAA00);
            else if (iconType == ErrorIcon)
                iconColour = juce::Colour(0xFFFF4444);

            g.setColour(iconColour);
            g.fillEllipse(iconX, iconY, iconSize, iconSize);
            g.setColour(juce::Colour(COLOR_BACKGROUND));
            g.setFont(juce::Font(iconSize * 0.6f, juce::Font::bold));
            
            juce::String iconChar;
            if (iconType == InfoIcon)
                iconChar = "i";
            else if (iconType == WarningIcon)
                iconChar = "!";
            else if (iconType == ErrorIcon)
                iconChar = "X";

            g.drawText(iconChar, iconX, iconY, iconSize, iconSize, juce::Justification::centred);
            iconX += iconSize + 15;
        }

        // Message text
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(14.0f));
        g.drawMultiLineText(messageText, iconX, iconY + 5, getWidth() - iconX - 20, juce::Justification::topLeft);
    }

    static void show(juce::Component* parent, const juce::String& title, const juce::String& message, IconType iconType = NoIcon)
    {
        auto* dialog = new StyledMessageDialog(parent, title, message, iconType);
        dialog->enterModalState(true, nullptr, true);
    }

private:
    juce::String titleText;
    juce::String messageText;
    IconType iconType;
    std::unique_ptr<juce::TextButton> okButton;

    class StyledMessageDialog : public juce::DialogWindow
    {
    public:
        StyledMessageDialog(juce::Component* parent, const juce::String& title, const juce::String& message, StyledMessageBox::IconType iconType)
            : juce::DialogWindow(title, juce::Colour(COLOR_BACKGROUND), true)
        {
            setOpaque(true);
            setUsingNativeTitleBar(false);
            setResizable(false, false);
            
            // Remove close button from title bar
            setTitleBarButtonsRequired(0, false);
            
            messageBox = std::make_unique<StyledMessageBox>(title, message, iconType);
            messageBox->onClose = [this] { closeDialog(); };
            setContentOwned(messageBox.get(), false);
            
            int dialogWidth = 420;
            int dialogHeight = 220;
            setSize(dialogWidth, dialogHeight);
            
            if (parent != nullptr)
                centreAroundComponent(parent, dialogWidth, dialogHeight);
            else
                centreWithSize(dialogWidth, dialogHeight);
        }

        void closeButtonPressed() override
        {
            // This should not be called since we removed the close button
            closeDialog();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(COLOR_BACKGROUND));
        }

    private:
        void closeDialog()
        {
            exitModalState(0);
        }

        std::unique_ptr<StyledMessageBox> messageBox;
    };
};
