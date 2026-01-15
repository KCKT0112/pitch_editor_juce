#pragma once

#include "../../JuceHeader.h"
#include "RoundedCard.h"
#include "SidebarComponent.h"
#include "PanelContainer.h"
#include "DraggablePanel.h"

/**
 * Main workspace component that manages the layout of:
 * - Piano roll (main content area with rounded card)
 * - Panel container (right side panels)
 * - Sidebar (icon buttons for toggling panels)
 */
class WorkspaceComponent : public juce::Component
{
public:
    WorkspaceComponent();
    ~WorkspaceComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setMainContent(juce::Component* content);
    void addPanel(const juce::String& id, const juce::String& title,
                  const juce::String& iconSvg, juce::Component* content,
                  bool initiallyVisible = false);

    void showPanel(const juce::String& id, bool show);
    bool isPanelVisible(const juce::String& id) const;

    SidebarComponent& getSidebar() { return sidebar; }
    PanelContainer& getPanelContainer() { return panelContainer; }
    RoundedCard& getMainCard() { return mainCard; }

    std::function<void(const juce::String&, bool)> onPanelVisibilityChanged;

private:
    void updatePanelContainerVisibility();

    RoundedCard mainCard;
    PanelContainer panelContainer;
    SidebarComponent sidebar;

    juce::Component* mainContent = nullptr;
    int panelContainerWidth = 280;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceComponent)
};
