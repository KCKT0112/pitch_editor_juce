#pragma once

#include "../JuceHeader.h"
#include "../Models/Note.h"
#include <vector>
#include <memory>
#include <functional>

/**
 * Base class for undoable actions.
 */
class UndoableAction
{
public:
    virtual ~UndoableAction() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual juce::String getName() const = 0;
};

/**
 * Action for changing a note's pitch offset.
 */
class PitchOffsetAction : public UndoableAction
{
public:
    PitchOffsetAction(Note* note, float oldOffset, float newOffset)
        : note(note), oldOffset(oldOffset), newOffset(newOffset) {}
    
    void undo() override { if (note) note->setPitchOffset(oldOffset); }
    void redo() override { if (note) note->setPitchOffset(newOffset); }
    juce::String getName() const override { return "Change Pitch Offset"; }
    
private:
    Note* note;
    float oldOffset;
    float newOffset;
};

/**
 * Action for changing multiple F0 values (hand-drawing).
 */
struct F0FrameEdit
{
    int idx = -1;
    float oldF0 = 0.0f;
    float newF0 = 0.0f;
    bool oldVoiced = false;
    bool newVoiced = false;
};

class F0EditAction : public UndoableAction
{
public:
    F0EditAction(std::vector<float>* f0Array,
                 std::vector<bool>* voicedMask,
                 std::vector<F0FrameEdit> edits)
        : f0Array(f0Array), voicedMask(voicedMask), edits(std::move(edits)) {}

    void undo() override
    {
        if (!f0Array) return;
        for (const auto& e : edits)
        {
            if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                (*f0Array)[e.idx] = e.oldF0;
            if (voicedMask && e.idx >= 0 && e.idx < static_cast<int>(voicedMask->size()))
                (*voicedMask)[e.idx] = e.oldVoiced;
        }
    }

    void redo() override
    {
        if (!f0Array) return;
        for (const auto& e : edits)
        {
            if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                (*f0Array)[e.idx] = e.newF0;
            if (voicedMask && e.idx >= 0 && e.idx < static_cast<int>(voicedMask->size()))
                (*voicedMask)[e.idx] = e.newVoiced;
        }
    }

    juce::String getName() const override { return "Edit Pitch Curve"; }

private:
    std::vector<float>* f0Array;
    std::vector<bool>* voicedMask;
    std::vector<F0FrameEdit> edits;
};

/**
 * Action for dragging a note to change pitch (MIDI note + F0 values).
 */
class NotePitchDragAction : public UndoableAction
{
public:
    NotePitchDragAction(Note* note, std::vector<float>* f0Array,
                        float oldMidi, float newMidi,
                        std::vector<F0FrameEdit> f0Edits)
        : note(note), f0Array(f0Array), oldMidi(oldMidi), newMidi(newMidi),
          f0Edits(std::move(f0Edits)) {}

    void undo() override
    {
        if (note) {
            note->setMidiNote(oldMidi);
            note->markDirty();
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.oldF0;
            }
        }
    }

    void redo() override
    {
        if (note) {
            note->setMidiNote(newMidi);
            note->markDirty();
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.newF0;
            }
        }
    }

    juce::String getName() const override { return "Drag Note Pitch"; }

private:
    Note* note;
    std::vector<float>* f0Array;
    float oldMidi;
    float newMidi;
    std::vector<F0FrameEdit> f0Edits;
};

/**
 * Simple undo manager for the pitch editor.
 */
class PitchUndoManager
{
public:
    PitchUndoManager(size_t maxHistory = 100) : maxHistory(maxHistory) {}
    
    void addAction(std::unique_ptr<UndoableAction> action)
    {
        // Clear redo stack when new action is added
        redoStack.clear();
        
        undoStack.push_back(std::move(action));
        
        // Limit history size
        while (undoStack.size() > maxHistory)
        {
            undoStack.erase(undoStack.begin());
        }
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    
    void undo()
    {
        if (undoStack.empty()) return;
        
        auto action = std::move(undoStack.back());
        undoStack.pop_back();
        
        action->undo();
        redoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void redo()
    {
        if (redoStack.empty()) return;
        
        auto action = std::move(redoStack.back());
        redoStack.pop_back();
        
        action->redo();
        undoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void clear()
    {
        undoStack.clear();
        redoStack.clear();
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    juce::String getUndoName() const
    {
        return undoStack.empty() ? "" : undoStack.back()->getName();
    }
    
    juce::String getRedoName() const
    {
        return redoStack.empty() ? "" : redoStack.back()->getName();
    }
    
    std::function<void()> onHistoryChanged;
    
private:
    std::vector<std::unique_ptr<UndoableAction>> undoStack;
    std::vector<std::unique_ptr<UndoableAction>> redoStack;
    size_t maxHistory;
};
