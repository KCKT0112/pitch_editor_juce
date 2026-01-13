#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Project.h"
#include "CoordinateMapper.h"
#include <functional>

/**
 * Handles scroll and zoom operations for the piano roll.
 * Manages scrollbars and mouse wheel/magnify gestures.
 */
class ScrollZoomController : public juce::ScrollBar::Listener {
public:
    ScrollZoomController();
    ~ScrollZoomController() override;

    void setCoordinateMapper(CoordinateMapper* mapper) { coordMapper = mapper; }
    void setProject(Project* proj) { project = proj; }

    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Mouse handling
    void handleMouseWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel,
                         int pianoKeysWidth, int timelineHeight, int componentWidth, int componentHeight);
    void handleMagnify(const juce::MouseEvent& e, float scaleFactor, int pianoKeysWidth);

    // Programmatic control
    void setScrollX(double x);
    void setPixelsPerSecond(float pps, bool centerOnCursor = false, double cursorTime = 0.0);
    void setPixelsPerSemitone(float pps);
    void centerOnPitchRange(float minMidi, float maxMidi, int visibleHeight);

    // Update scrollbar ranges
    void updateScrollBars(int visibleWidth, int visibleHeight);

    // Access scrollbars for layout
    juce::ScrollBar& getHorizontalScrollBar() { return horizontalScrollBar; }
    juce::ScrollBar& getVerticalScrollBar() { return verticalScrollBar; }

    // Callbacks
    std::function<void(float)> onZoomChanged;
    std::function<void(double)> onScrollChanged;
    std::function<void()> onRepaintNeeded;

private:
    CoordinateMapper* coordMapper = nullptr;
    Project* project = nullptr;

    juce::ScrollBar horizontalScrollBar{false};
    juce::ScrollBar verticalScrollBar{true};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScrollZoomController)
};
