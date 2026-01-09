#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"
#include "../Utils/UndoManager.h"

#include <unordered_map>

class PitchUndoManager;

/**
 * Edit mode for the piano roll.
 */
enum class EditMode
{
    Select,     // Normal selection and dragging
    Draw        // Pitch drawing mode
};

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
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    
    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    
    // Project
    void setProject(Project* proj);
    Project* getProject() const { return project; }
    
    // Undo Manager
    void setUndoManager(PitchUndoManager* manager) { undoManager = manager; }
    PitchUndoManager* getUndoManager() const { return undoManager; }
    
    // Cursor
    void setCursorTime(double time);
    double getCursorTime() const { return cursorTime; }
    
    // Zoom with optional center point
    void setPixelsPerSecond(float pps, bool centerOnCursor = false);
    void setPixelsPerSemitone(float pps);
    float getPixelsPerSecond() const { return pixelsPerSecond; }
    float getPixelsPerSemitone() const { return pixelsPerSemitone; }
    
    // Scroll
    void setScrollX(double x);
    double getScrollX() const { return scrollX; }
    void centerOnPitchRange(float minMidi, float maxMidi);
    
    // Edit mode
    void setEditMode(EditMode mode);
    EditMode getEditMode() const { return editMode; }
    
    // Callbacks
    std::function<void(Note*)> onNoteSelected;
    std::function<void()> onPitchEdited;
    std::function<void()> onPitchEditFinished;  // Called when dragging ends
    std::function<void(double)> onSeek;
    std::function<void(float)> onZoomChanged;
    std::function<void(double)> onScrollChanged;
    
private:
    void drawBackgroundWaveform(juce::Graphics& g, const juce::Rectangle<int>& visibleArea);
    void drawGrid(juce::Graphics& g);
    void drawTimeline(juce::Graphics& g);
    void drawNotes(juce::Graphics& g);
    void drawPitchCurves(juce::Graphics& g);
    void drawCursor(juce::Graphics& g);
    void drawPianoKeys(juce::Graphics& g);
    void drawDrawingCursor(juce::Graphics& g);  // Draw mode indicator
    
    float midiToY(float midiNote) const;
    float yToMidi(float y) const;
    float timeToX(double time) const;
    double xToTime(float x) const;
    
    Note* findNoteAt(float x, float y);
    void updateScrollBars();
    
    // Pitch drawing helpers
    void applyPitchDrawing(float x, float y);
    void commitPitchDrawing();
    
    Project* project = nullptr;
    PitchUndoManager* undoManager = nullptr;
    
    float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
    float pixelsPerSemitone = DEFAULT_PIXELS_PER_SEMITONE;
    
    double cursorTime = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    
    // Piano keys area width
    static constexpr int pianoKeysWidth = 60;
    static constexpr int timelineHeight = 24;
    
    // Edit mode
    EditMode editMode = EditMode::Select;
    
    // Dragging state
    bool isDragging = false;
    Note* draggedNote = nullptr;
    float dragStartY = 0.0f;
    float originalPitchOffset = 0.0f;
    
    // Pitch drawing state
    bool isDrawing = false;
    std::vector<F0FrameEdit> drawingEdits;  // unique edits per frame
    std::unordered_map<int, size_t> drawingEditIndexByFrame;
    float lastDrawX = 0.0f;
    float lastDrawY = 0.0f;
    
    // Scrollbars
    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};
