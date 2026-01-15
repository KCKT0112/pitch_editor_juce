#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/Constants.h"

class PanelContainer;

/**
 * Base class for draggable panels in the panel container.
 * Panels have a header that can be dragged to reorder.
 */
class DraggablePanel : public juce::Component
{
public:
    DraggablePanel(const juce::String& panelId, const juce::String& title);
    ~DraggablePanel() override = default;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    const juce::String& getPanelId() const { return panelId; }
    const juce::String& getTitle() const { return title; }

    void setContentComponent(juce::Component* content);
    juce::Component* getContentComponent() const { return contentComponent; }

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed; }

    void setPanelContainer(PanelContainer* container) { panelContainer = container; }

    int getPreferredHeight() const;
    static constexpr int headerHeight = 36;

protected:
    virtual void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea);

private:
    juce::String panelId;
    juce::String title;
    juce::Component* contentComponent = nullptr;
    PanelContainer* panelContainer = nullptr;
    bool collapsed = false;
    bool isDragging = false;
    juce::Point<int> dragStartPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DraggablePanel)
};
