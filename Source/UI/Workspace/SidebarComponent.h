#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/Constants.h"

class SidebarComponent;

/**
 * A single icon button in the sidebar.
 */
class SidebarButton : public juce::Component,
                      public juce::TooltipClient
{
public:
    SidebarButton(const juce::String& id, const juce::String& tooltip);
    ~SidebarButton() override = default;

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setActive(bool active);
    bool isActive() const { return active; }

    void setIcon(std::unique_ptr<juce::Drawable> icon);
    const juce::String& getId() const { return buttonId; }

    juce::String getTooltip() override { return tooltipText; }

    std::function<void(const juce::String&)> onClick;

private:
    juce::String buttonId;
    juce::String tooltipText;
    std::unique_ptr<juce::Drawable> iconDrawable;
    bool active = false;
    bool hovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarButton)
};

/**
 * Right sidebar with icon buttons for activating panels.
 * Similar to VS Code or Synthesizer V sidebar.
 */
class SidebarComponent : public juce::Component
{
public:
    SidebarComponent();
    ~SidebarComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void addButton(const juce::String& id, const juce::String& tooltip, const juce::String& svgData);
    void setButtonActive(const juce::String& id, bool active);
    bool isButtonActive(const juce::String& id) const;

    std::function<void(const juce::String&, bool)> onPanelToggled;

    static constexpr int buttonSize = 40;
    static constexpr int sidebarWidth = 64;

private:
    void handleButtonClick(const juce::String& id);

    juce::OwnedArray<SidebarButton> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)
};
