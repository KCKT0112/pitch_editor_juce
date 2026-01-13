#include "ScrollZoomController.h"

ScrollZoomController::ScrollZoomController() {
    horizontalScrollBar.addListener(this);
    verticalScrollBar.addListener(this);

    auto thumbColor = juce::Colour(COLOR_PRIMARY).withAlpha(0.6f);
    auto trackColor = juce::Colour(0xFF252530);

    horizontalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
    horizontalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);
    verticalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
    verticalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);

    verticalScrollBar.setRangeLimits(0, (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * DEFAULT_PIXELS_PER_SEMITONE);
    verticalScrollBar.setCurrentRange(0, 500);
}

ScrollZoomController::~ScrollZoomController() {
    horizontalScrollBar.removeListener(this);
    verticalScrollBar.removeListener(this);
}

void ScrollZoomController::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    if (!coordMapper)
        return;

    if (scrollBar == &horizontalScrollBar) {
        coordMapper->setScrollX(newRangeStart);
        if (onScrollChanged)
            onScrollChanged(newRangeStart);
    } else if (scrollBar == &verticalScrollBar) {
        coordMapper->setScrollY(newRangeStart);
    }

    if (onRepaintNeeded)
        onRepaintNeeded();
}

void ScrollZoomController::handleMouseWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel,
                                            int pianoKeysWidth, int timelineHeight,
                                            int componentWidth, int componentHeight) {
    if (!coordMapper)
        return;

    float scrollMultiplier = wheel.isSmooth ? 200.0f : 80.0f;

    bool isOverPianoKeys = e.x < pianoKeysWidth;
    bool isOverTimeline = e.y < timelineHeight;

    if (!e.mods.isCommandDown() && !e.mods.isCtrlDown()) {
        // Hover-based zoom
        if (isOverPianoKeys) {
            // Vertical zoom
            float mouseY = e.y - timelineHeight;
            float midiAtMouse = (mouseY + coordMapper->getScrollY()) / coordMapper->getPixelsPerSemitone();

            float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
            float newPps = coordMapper->getPixelsPerSemitone() * zoomFactor;
            newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, newPps);
            coordMapper->setPixelsPerSemitone(newPps);

            double newScrollY = midiAtMouse * newPps - mouseY;
            coordMapper->setScrollY(std::max(0.0, newScrollY));

            updateScrollBars(componentWidth - pianoKeysWidth - 14, componentHeight - 14);
            if (onRepaintNeeded) onRepaintNeeded();
            return;
        }

        if (isOverTimeline) {
            // Horizontal zoom
            float mouseX = e.x - pianoKeysWidth;
            double timeAtMouse = (mouseX + coordMapper->getScrollX()) / coordMapper->getPixelsPerSecond();

            float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
            float newPps = coordMapper->getPixelsPerSecond() * zoomFactor;
            newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);
            coordMapper->setPixelsPerSecond(newPps);

            double newScrollX = timeAtMouse * newPps - mouseX;
            coordMapper->setScrollX(std::max(0.0, newScrollX));

            updateScrollBars(componentWidth - pianoKeysWidth - 14, componentHeight - 14);
            if (onRepaintNeeded) onRepaintNeeded();
            if (onZoomChanged) onZoomChanged(newPps);
            return;
        }

        // Normal scrolling
        float deltaX = wheel.deltaX;
        float deltaY = wheel.deltaY;

        if (e.mods.isShiftDown() && std::abs(deltaX) < 0.001f) {
            deltaX = deltaY;
            deltaY = 0.0f;
        }

        if (std::abs(deltaX) > 0.001f) {
            double newScrollX = coordMapper->getScrollX() - deltaX * scrollMultiplier;
            newScrollX = std::max(0.0, newScrollX);
            horizontalScrollBar.setCurrentRangeStart(newScrollX);
        }

        if (std::abs(deltaY) > 0.001f) {
            double newScrollY = coordMapper->getScrollY() - deltaY * scrollMultiplier;
            verticalScrollBar.setCurrentRangeStart(newScrollY);
        }
        return;
    }

    // Key-based zoom
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        float zoomFactor = 1.0f + wheel.deltaY * 0.3f;

        if (e.mods.isShiftDown()) {
            // Vertical zoom
            float mouseY = static_cast<float>(e.y - timelineHeight);
            float midiAtMouse = (mouseY + static_cast<float>(coordMapper->getScrollY())) / coordMapper->getPixelsPerSemitone();

            float newPps = coordMapper->getPixelsPerSemitone() * zoomFactor;
            newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, newPps);

            float newMouseY = coordMapper->midiToY(midiAtMouse);
            coordMapper->setScrollY(std::max(0.0, static_cast<double>(newMouseY - mouseY)));
            coordMapper->setPixelsPerSemitone(newPps);

            updateScrollBars(componentWidth - pianoKeysWidth - 14, componentHeight - 14);
            if (onRepaintNeeded) onRepaintNeeded();
        } else {
            // Horizontal zoom
            float mouseX = static_cast<float>(e.x - pianoKeysWidth);
            double timeAtMouse = coordMapper->xToTime(mouseX + static_cast<float>(coordMapper->getScrollX()));

            float newPps = coordMapper->getPixelsPerSecond() * zoomFactor;
            newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

            float newMouseX = static_cast<float>(timeAtMouse * newPps);
            coordMapper->setScrollX(std::max(0.0, static_cast<double>(newMouseX - mouseX)));
            coordMapper->setPixelsPerSecond(newPps);

            updateScrollBars(componentWidth - pianoKeysWidth - 14, componentHeight - 14);
            if (onRepaintNeeded) onRepaintNeeded();
            if (onZoomChanged) onZoomChanged(newPps);
        }
    }
}

void ScrollZoomController::handleMagnify(const juce::MouseEvent& e, float scaleFactor, int pianoKeysWidth) {
    if (!coordMapper)
        return;

    float mouseX = static_cast<float>(e.x - pianoKeysWidth);
    double timeAtMouse = coordMapper->xToTime(mouseX + static_cast<float>(coordMapper->getScrollX()));

    float newPps = coordMapper->getPixelsPerSecond() * scaleFactor;
    newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

    float newMouseX = static_cast<float>(timeAtMouse * newPps);
    coordMapper->setScrollX(std::max(0.0, static_cast<double>(newMouseX - mouseX)));
    coordMapper->setPixelsPerSecond(newPps);

    if (onRepaintNeeded) onRepaintNeeded();
    if (onZoomChanged) onZoomChanged(newPps);
}

void ScrollZoomController::setScrollX(double x) {
    if (!coordMapper)
        return;

    if (std::abs(coordMapper->getScrollX() - x) < 0.01)
        return;

    coordMapper->setScrollX(x);
    horizontalScrollBar.setCurrentRangeStart(x);

    if (onRepaintNeeded) onRepaintNeeded();
}

void ScrollZoomController::setPixelsPerSecond(float pps, bool centerOnCursor, double cursorTime) {
    if (!coordMapper)
        return;

    float oldPps = coordMapper->getPixelsPerSecond();
    float newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, pps);

    if (std::abs(oldPps - newPps) < 0.01f)
        return;

    if (centerOnCursor) {
        float cursorX = static_cast<float>(cursorTime * oldPps);
        float cursorRelativeX = cursorX - static_cast<float>(coordMapper->getScrollX());
        float newCursorX = static_cast<float>(cursorTime * newPps);
        coordMapper->setScrollX(std::max(0.0, static_cast<double>(newCursorX - cursorRelativeX)));
    }

    coordMapper->setPixelsPerSecond(newPps);

    if (onRepaintNeeded) onRepaintNeeded();
}

void ScrollZoomController::setPixelsPerSemitone(float pps) {
    if (!coordMapper)
        return;

    coordMapper->setPixelsPerSemitone(juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, pps));

    if (onRepaintNeeded) onRepaintNeeded();
}

void ScrollZoomController::centerOnPitchRange(float minMidi, float maxMidi, int visibleHeight) {
    if (!coordMapper)
        return;

    float centerMidi = (minMidi + maxMidi) / 2.0f;
    float centerY = coordMapper->midiToY(centerMidi);

    double newScrollY = centerY - visibleHeight / 2.0;
    double totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * coordMapper->getPixelsPerSemitone();
    newScrollY = juce::jlimit(0.0, std::max(0.0, totalHeight - visibleHeight), newScrollY);

    coordMapper->setScrollY(newScrollY);
    verticalScrollBar.setCurrentRangeStart(newScrollY);

    if (onRepaintNeeded) onRepaintNeeded();
}

void ScrollZoomController::updateScrollBars(int visibleWidth, int visibleHeight) {
    if (!coordMapper)
        return;

    float duration = project ? project->getAudioData().getDuration() : 60.0f;
    float totalWidth = duration * coordMapper->getPixelsPerSecond();
    float totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * coordMapper->getPixelsPerSemitone();

    horizontalScrollBar.setRangeLimits(0, totalWidth);
    horizontalScrollBar.setCurrentRange(coordMapper->getScrollX(), visibleWidth);

    verticalScrollBar.setRangeLimits(0, totalHeight);
    verticalScrollBar.setCurrentRange(coordMapper->getScrollY(), visibleHeight);
}
