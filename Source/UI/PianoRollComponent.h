#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"
#include "../Utils/UndoManager.h"
#include "../Utils/DrawCurve.h"
#include "PianoRoll/CoordinateMapper.h"
#include "PianoRoll/PianoRollRenderer.h"
#include "PianoRoll/ScrollZoomController.h"
#include "PianoRoll/PitchEditor.h"

#include <deque>
#include <memory>
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
    void setUndoManager(PitchUndoManager* manager);
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
    std::function<void(int, int)> onReinterpolateUV;  // Called to re-infer UV regions (startFrame, endFrame)
    
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
    void updateBasePitchCacheIfNeeded();
    void reapplyBasePitchForNote(Note* note);  // Recalculate F0 from base pitch + delta after undo/redo
    
    // Pitch drawing helpers
    void applyPitchDrawing(float x, float y);
    void commitPitchDrawing();
    void applyPitchPoint(int frameIndex, int midiCents);
    void startNewPitchCurve(int frameIndex, int midiCents);
    
    Project* project = nullptr;
    PitchUndoManager* undoManager = nullptr;

    // New modular components
    std::unique_ptr<CoordinateMapper> coordMapper;
    std::unique_ptr<PianoRollRenderer> renderer;
    std::unique_ptr<ScrollZoomController> scrollZoomController;
    std::unique_ptr<PitchEditor> pitchEditor;

    float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
    float pixelsPerSemitone = DEFAULT_PIXELS_PER_SEMITONE;
    
    double cursorTime = 0.0;
    double lastCursorTime = -1.0;  // Track previous cursor position for dirty rect
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
    float originalMidiNote = 60.0f;  // Original MIDI note before drag
    float boundaryF0Start = 0.0f;    // F0 value before note start (for smooth transition)
    float boundaryF0End = 0.0f;      // F0 value after note end (for smooth transition)
    std::vector<float> originalF0Values;  // F0 values before drag for undo
    
    // Pitch drawing state
    bool isDrawing = false;
    std::vector<F0FrameEdit> drawingEdits;  // unique edits per frame
    std::unordered_map<int, size_t> drawingEditIndexByFrame;
    int lastDrawFrame = -1;
    int lastDrawValueCents = 0;
    DrawCurve* activeDrawCurve = nullptr;
    std::deque<std::unique_ptr<DrawCurve>> drawCurves;
    
    // Scrollbars
    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };

    // Waveform cache for performance
    juce::Image waveformCache;
    double cachedScrollX = -1.0;
    float cachedPixelsPerSecond = -1.0f;
    int cachedWidth = 0;
    int cachedHeight = 0;

    // Base pitch curve cache for performance
    // Only recalculates when notes change, not on every repaint
    std::vector<float> cachedBasePitch;
    size_t cachedNoteCount = 0;
    int cachedTotalFrames = 0;
    bool cacheInvalidated = true;  // Start invalidated, force first calculation

public:
    void invalidateBasePitchCache() { cacheInvalidated = true; cachedNoteCount = 0; cachedBasePitch.clear(); }

private:
    // Optional: disable base pitch rendering for performance testing
    static constexpr bool ENABLE_BASE_PITCH_DEBUG = true;  // Set to false to disable

    // Mouse drag throttling
    juce::int64 lastDragRepaintTime = 0;
    static constexpr juce::int64 minDragRepaintInterval = 16;  // ~60fps max
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};
