#include "WorkspaceComponent.h"

WorkspaceComponent::WorkspaceComponent()
{
    setOpaque(true);

    addAndMakeVisible(mainCard);
    addAndMakeVisible(panelContainer);
    addAndMakeVisible(sidebar);

    // Initially hide panel container (no panels visible)
    panelContainer.setVisible(false);

    // Connect sidebar to panel container
    sidebar.onPanelToggled = [this](const juce::String& id, bool active)
    {
        panelContainer.showPanel(id, active);
        updatePanelContainerVisibility();

        if (onPanelVisibilityChanged)
            onPanelVisibilityChanged(id, active);
    };
}

void WorkspaceComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A24));
}

void WorkspaceComponent::resized()
{
    auto bounds = getLocalBounds();
    const int margin = 8;
    const int topMargin = 2; // Smaller top margin to be closer to toolbar

    // Apply top margin first so sidebar aligns with content
    bounds.removeFromTop(topMargin);

    // Sidebar on the right edge (same height as content area)
    auto sidebarBounds = bounds.removeFromRight(SidebarComponent::sidebarWidth);
    sidebarBounds.removeFromBottom(margin); // Match bottom margin
    sidebar.setBounds(sidebarBounds);

    // Panel container (if any panels are visible)
    bool hasPanels = false;
    for (const auto& id : panelContainer.getPanelOrder())
    {
        if (panelContainer.isPanelVisible(id))
        {
            hasPanels = true;
            break;
        }
    }

    // Apply left/bottom margins
    bounds.removeFromLeft(margin);
    bounds.removeFromBottom(margin);

    if (hasPanels)
    {
        // Panel on right, consistent margin between sidebar and panel
        auto panelBounds = bounds.removeFromRight(panelContainerWidth);
        bounds.removeFromRight(margin); // Gap between piano roll and panel
        panelContainer.setBounds(panelBounds);
    }

    // Main content card
    mainCard.setBounds(bounds);
}

void WorkspaceComponent::setMainContent(juce::Component* content)
{
    mainContent = content;
    mainCard.setContentComponent(content);
}

void WorkspaceComponent::addPanel(const juce::String& id, const juce::String& title,
                                   const juce::String& iconSvg, juce::Component* content,
                                   bool initiallyVisible)
{
    // Add button to sidebar
    sidebar.addButton(id, title, iconSvg);

    // Set content size before adding to panel
    if (content != nullptr)
        content->setSize(panelContainerWidth - 32, 500);

    // Create draggable panel wrapper
    auto panel = std::make_unique<DraggablePanel>(id, title);
    panel->setContentComponent(content);

    // Add to panel container
    panelContainer.addPanel(std::move(panel));

    // Set initial visibility
    if (initiallyVisible)
    {
        sidebar.setButtonActive(id, true);
        panelContainer.showPanel(id, true);
        updatePanelContainerVisibility();
    }
}

void WorkspaceComponent::showPanel(const juce::String& id, bool show)
{
    sidebar.setButtonActive(id, show);
    panelContainer.showPanel(id, show);
    updatePanelContainerVisibility();
}

bool WorkspaceComponent::isPanelVisible(const juce::String& id) const
{
    return panelContainer.isPanelVisible(id);
}

void WorkspaceComponent::updatePanelContainerVisibility()
{
    bool hasPanels = false;
    for (const auto& id : panelContainer.getPanelOrder())
    {
        if (panelContainer.isPanelVisible(id))
        {
            hasPanels = true;
            break;
        }
    }

    panelContainer.setVisible(hasPanels);
    resized();
}
