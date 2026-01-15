#include "DraggablePanel.h"
#include "PanelContainer.h"

DraggablePanel::DraggablePanel(const juce::String& id, const juce::String& panelTitle)
    : panelId(id), title(panelTitle)
{
    setOpaque(false);
}

void DraggablePanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float cornerRadius = 8.0f;

    // Clip to rounded rectangle for entire panel
    juce::Path clipPath;
    clipPath.addRoundedRectangle(bounds, cornerRadius);
    g.reduceClipRegion(clipPath);

    // Content background (full area)
    g.setColour(juce::Colour(0xFF2D2D37));
    g.fillRect(bounds);

    // Header background
    auto headerBounds = bounds.removeFromTop(static_cast<float>(headerHeight));
    g.setColour(juce::Colour(0xFF252530));
    g.fillRect(headerBounds);

    // Header text
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText(title, headerBounds.reduced(12, 0).toNearestInt(), juce::Justification::centredLeft);

    // Separator line under header
    g.setColour(juce::Colour(0xFF3A3A45));
    g.drawHorizontalLine(headerHeight - 1, 0, static_cast<float>(getWidth()));
}

void DraggablePanel::paintOverChildren(juce::Graphics& g)
{
    // Draw rounded border on top
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF3A3A45));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

void DraggablePanel::resized()
{
    if (contentComponent != nullptr)
    {
        auto contentBounds = getLocalBounds().withTrimmedTop(headerHeight);
        contentComponent->setBounds(contentBounds);
    }
}

void DraggablePanel::mouseDown(const juce::MouseEvent& e)
{
    auto headerBounds = getLocalBounds().removeFromTop(headerHeight);

    if (headerBounds.contains(e.getPosition()))
    {
        // Start drag for reordering
        isDragging = true;
        dragStartPos = e.getPosition();
    }
}

void DraggablePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (isDragging && panelContainer != nullptr)
    {
        auto delta = e.getPosition() - dragStartPos;
        if (std::abs(delta.y) > 10)
        {
            panelContainer->handlePanelDrag(this, e.getEventRelativeTo(panelContainer));
        }
    }
}

void DraggablePanel::mouseUp(const juce::MouseEvent&)
{
    if (isDragging && panelContainer != nullptr)
    {
        panelContainer->handlePanelDragEnd(this);
    }
    isDragging = false;
}

void DraggablePanel::setContentComponent(juce::Component* content)
{
    if (contentComponent != nullptr)
        removeChildComponent(contentComponent);

    contentComponent = content;

    if (contentComponent != nullptr)
    {
        addAndMakeVisible(contentComponent);
        contentComponent->setVisible(!collapsed);
        resized();
    }
}

void DraggablePanel::setCollapsed(bool newCollapsed)
{
    if (collapsed != newCollapsed)
    {
        collapsed = newCollapsed;
        if (contentComponent != nullptr)
            contentComponent->setVisible(!collapsed);

        if (panelContainer != nullptr)
            panelContainer->updateLayout();

        repaint();
    }
}

int DraggablePanel::getPreferredHeight() const
{
    if (collapsed)
        return headerHeight;

    if (contentComponent != nullptr)
    {
        // Try to get preferred height from content component
        int contentHeight = contentComponent->getHeight();
        if (contentHeight <= 0)
            contentHeight = 400; // Default content height
        return headerHeight + contentHeight + 16; // 8px padding top and bottom
    }

    return headerHeight + 400; // Default content height
}

void DraggablePanel::paintContent(juce::Graphics&, juce::Rectangle<int>)
{
    // Override in subclasses for custom content painting
}
