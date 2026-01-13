#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Project.h"
#include "../../Utils/UndoManager.h"
#include "../../Utils/DrawCurve.h"
#include "../../Utils/PitchCurveProcessor.h"
#include "CoordinateMapper.h"
#include <deque>
#include <memory>
#include <unordered_map>
#include <functional>

/**
 * Handles pitch editing operations including note dragging and pitch drawing.
 */
class PitchEditor {
public:
    PitchEditor();
    ~PitchEditor() = default;

    void setProject(Project* proj) { project = proj; }
    void setUndoManager(PitchUndoManager* manager) { undoManager = manager; }
    void setCoordinateMapper(CoordinateMapper* mapper) { coordMapper = mapper; }

    // Note selection and dragging
    Note* findNoteAt(float x, float y);
    void startNoteDrag(Note* note, float y);
    void updateNoteDrag(float y);
    void endNoteDrag();
    bool isDraggingNote() const { return isDragging; }
    Note* getDraggedNote() const { return draggedNote; }

    // Pitch drawing
    void startDrawing(float x, float y);
    void continueDrawing(float x, float y);
    void endDrawing();
    bool isDrawingPitch() const { return isDrawing; }

    // Snap note to semitone
    void snapNoteToSemitone(Note* note);

    // Callbacks
    std::function<void(Note*)> onNoteSelected;
    std::function<void()> onPitchEdited;
    std::function<void()> onPitchEditFinished;
    std::function<void()> onBasePitchCacheInvalidated;

private:
    void applyPitchPoint(int frameIndex, int midiCents);
    void startNewPitchCurve(int frameIndex, int midiCents);

    Project* project = nullptr;
    PitchUndoManager* undoManager = nullptr;
    CoordinateMapper* coordMapper = nullptr;

    // Drag state
    bool isDragging = false;
    Note* draggedNote = nullptr;
    float dragStartY = 0.0f;
    float originalPitchOffset = 0.0f;
    float originalMidiNote = 60.0f;
    float boundaryF0Start = 0.0f;
    float boundaryF0End = 0.0f;
    std::vector<float> originalF0Values;

    // Draw state
    bool isDrawing = false;
    std::vector<F0FrameEdit> drawingEdits;
    std::unordered_map<int, size_t> drawingEditIndexByFrame;
    int lastDrawFrame = -1;
    int lastDrawValueCents = 0;
    DrawCurve* activeDrawCurve = nullptr;
    std::deque<std::unique_ptr<DrawCurve>> drawCurves;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchEditor)
};
