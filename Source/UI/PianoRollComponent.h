#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"

/**
 * Piano roll component for displaying and editing notes.
 */
class PianoRollComponent : public juce::Component,
                            public juce::ScrollBar::Listener
{
public:
    PianoRollComponent();
    ~PianoRollComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    
    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    
    // Project
    void setProject(Project* proj);
    Project* getProject() const { return project; }
    
    // Cursor
    void setCursorTime(double time);
    double getCursorTime() const { return cursorTime; }
    
    // Zoom
    void setPixelsPerSecond(float pps);
    void setPixelsPerSemitone(float pps);
    float getPixelsPerSecond() const { return pixelsPerSecond; }
    float getPixelsPerSemitone() const { return pixelsPerSemitone; }
    
    // Callbacks
    std::function<void(Note*)> onNoteSelected;
    std::function<void()> onPitchEdited;
    std::function<void(double)> onSeek;
    
private:
    void drawGrid(juce::Graphics& g);
    void drawNotes(juce::Graphics& g);
    void drawPitchCurves(juce::Graphics& g);
    void drawCursor(juce::Graphics& g);
    void drawPianoKeys(juce::Graphics& g);
    
    float midiToY(float midiNote) const;
    float yToMidi(float y) const;
    float timeToX(double time) const;
    double xToTime(float x) const;
    
    Note* findNoteAt(float x, float y);
    void updateScrollBars();
    
    Project* project = nullptr;
    
    float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
    float pixelsPerSemitone = DEFAULT_PIXELS_PER_SEMITONE;
    
    double cursorTime = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    
    // Piano keys area width
    static constexpr int pianoKeysWidth = 60;
    
    // Dragging state
    bool isDragging = false;
    Note* draggedNote = nullptr;
    float dragStartY = 0.0f;
    float originalPitchOffset = 0.0f;
    
    // Scrollbars
    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};
