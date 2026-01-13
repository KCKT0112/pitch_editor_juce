#include "PitchEditor.h"

PitchEditor::PitchEditor() = default;

Note* PitchEditor::findNoteAt(float x, float y) {
    if (!project || !coordMapper)
        return nullptr;

    for (auto& note : project->getNotes()) {
        if (note.isRest())
            continue;

        float noteX = framesToSeconds(note.getStartFrame()) * coordMapper->getPixelsPerSecond();
        float noteW = framesToSeconds(note.getDurationFrames()) * coordMapper->getPixelsPerSecond();
        float noteY = coordMapper->midiToY(note.getAdjustedMidiNote());
        float noteH = coordMapper->getPixelsPerSemitone();

        if (x >= noteX && x < noteX + noteW && y >= noteY && y < noteY + noteH) {
            return &note;
        }
    }

    return nullptr;
}

void PitchEditor::startNoteDrag(Note* note, float y) {
    if (!note || !project)
        return;

    // Capture delta slice from global dense deltaPitch
    auto& audioData = project->getAudioData();
    int startFrame = note->getStartFrame();
    int endFrame = note->getEndFrame();
    int numFrames = endFrame - startFrame;

    std::vector<float> delta(numFrames, 0.0f);
    for (int i = 0; i < numFrames; ++i) {
        int globalFrame = startFrame + i;
        if (globalFrame >= 0 && globalFrame < static_cast<int>(audioData.deltaPitch.size()))
            delta[i] = audioData.deltaPitch[static_cast<size_t>(globalFrame)];
    }
    note->setDeltaPitch(std::move(delta));

    isDragging = true;
    draggedNote = note;
    dragStartY = y;
    originalPitchOffset = note->getPitchOffset();
    originalMidiNote = note->getMidiNote();

    // Save boundary F0 values
    int f0Size = static_cast<int>(audioData.f0.size());
    boundaryF0Start = (startFrame > 0 && startFrame - 1 < f0Size)
                          ? audioData.f0[startFrame - 1]
                          : 0.0f;
    boundaryF0End = (endFrame < f0Size) ? audioData.f0[endFrame] : 0.0f;

    // Save original F0 values for undo
    originalF0Values.clear();
    for (int i = startFrame; i < endFrame && i < f0Size; ++i)
        originalF0Values.push_back(audioData.f0[i]);

    if (onNoteSelected)
        onNoteSelected(note);
}

void PitchEditor::updateNoteDrag(float y) {
    if (!isDragging || !draggedNote || !coordMapper)
        return;

    float deltaY = dragStartY - y;
    float deltaSemitones = deltaY / coordMapper->getPixelsPerSemitone();

    draggedNote->setPitchOffset(deltaSemitones);
    draggedNote->markDirty();
}

void PitchEditor::endNoteDrag() {
    if (!isDragging || !draggedNote || !project)
        return;

    float newOffset = draggedNote->getPitchOffset();
    constexpr float CHANGE_THRESHOLD = 0.001f;
    bool hasChange = std::abs(newOffset) >= CHANGE_THRESHOLD;

    if (hasChange) {
        int startFrame = draggedNote->getStartFrame();
        int endFrame = draggedNote->getEndFrame();
        auto& audioData = project->getAudioData();
        int f0Size = static_cast<int>(audioData.f0.size());

        // Bake pitchOffset into midiNote
        draggedNote->setMidiNote(originalMidiNote + newOffset);
        draggedNote->setPitchOffset(0.0f);

        // Find adjacent notes to expand dirty range
        const auto& notes = project->getNotes();
        int expandedStart = startFrame;
        int expandedEnd = endFrame;
        for (const auto& note : notes) {
            if (&note == draggedNote) continue;
            if (note.getEndFrame() > startFrame - 30 && note.getEndFrame() <= startFrame) {
                expandedStart = std::min(expandedStart, note.getStartFrame());
            }
            if (note.getStartFrame() < endFrame + 30 && note.getStartFrame() >= endFrame) {
                expandedEnd = std::max(expandedEnd, note.getEndFrame());
            }
        }

        // Rebuild pitch curves
        PitchCurveProcessor::rebuildBaseFromNotes(*project);
        PitchCurveProcessor::composeF0InPlace(*project, false);

        if (onBasePitchCacheInvalidated)
            onBasePitchCacheInvalidated();

        // Mark dirty range
        int smoothStart = std::max(0, expandedStart - 60);
        int smoothEnd = std::min(f0Size, expandedEnd + 60);
        project->setF0DirtyRange(smoothStart, smoothEnd);

        // Create undo action
        if (undoManager) {
            std::vector<F0FrameEdit> f0Edits;
            for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
                int localIdx = i - startFrame;
                F0FrameEdit edit;
                edit.idx = i;
                edit.oldF0 = (localIdx < static_cast<int>(originalF0Values.size()))
                                 ? originalF0Values[localIdx]
                                 : 0.0f;
                edit.newF0 = audioData.f0[static_cast<size_t>(i)];
                f0Edits.push_back(edit);
            }

            int capturedExpandedStart = expandedStart;
            int capturedExpandedEnd = expandedEnd;
            int capturedF0Size = f0Size;
            auto action = std::make_unique<NotePitchDragAction>(
                draggedNote, &audioData.f0, originalMidiNote,
                originalMidiNote + newOffset, std::move(f0Edits),
                [this, capturedExpandedStart, capturedExpandedEnd, capturedF0Size](Note* n) {
                    if (project) {
                        PitchCurveProcessor::rebuildBaseFromNotes(*project);
                        PitchCurveProcessor::composeF0InPlace(*project, false);
                        if (onBasePitchCacheInvalidated)
                            onBasePitchCacheInvalidated();
                        int smoothStart = std::max(0, capturedExpandedStart - 60);
                        int smoothEnd = std::min(capturedF0Size, capturedExpandedEnd + 60);
                        project->setF0DirtyRange(smoothStart, smoothEnd);
                        if (n) n->clearDirty();
                    }
                });
            undoManager->addAction(std::move(action));
        }

        if (onPitchEdited)
            onPitchEdited();
        if (onPitchEditFinished)
            onPitchEditFinished();
    } else {
        draggedNote->setPitchOffset(0.0f);
    }

    isDragging = false;
    draggedNote = nullptr;
}

void PitchEditor::startDrawing(float x, float y) {
    isDrawing = true;
    drawingEdits.clear();
    drawingEditIndexByFrame.clear();
    drawCurves.clear();
    activeDrawCurve = nullptr;
    lastDrawFrame = -1;
    lastDrawValueCents = 0;

    continueDrawing(x, y);
}

void PitchEditor::continueDrawing(float x, float y) {
    if (!project || !coordMapper)
        return;

    auto& audioData = project->getAudioData();
    if (audioData.f0.empty())
        return;

    double time = coordMapper->xToTime(x);
    float midi = coordMapper->yToMidi(y - coordMapper->getPixelsPerSemitone() * 0.5f);
    int frameIndex = coordMapper->secondsToFrames(static_cast<float>(time));
    int midiCents = static_cast<int>(std::round(midi * 100.0f));

    applyPitchPoint(frameIndex, midiCents);

    if (onPitchEdited)
        onPitchEdited();
}

void PitchEditor::endDrawing() {
    if (drawingEdits.empty()) {
        isDrawing = false;
        return;
    }

    // Calculate dirty frame range
    int minFrame = std::numeric_limits<int>::max();
    int maxFrame = std::numeric_limits<int>::min();
    for (const auto& e : drawingEdits) {
        minFrame = std::min(minFrame, e.idx);
        maxFrame = std::max(maxFrame, e.idx);
    }

    // Clear deltaPitch for notes in edited range
    if (project && minFrame <= maxFrame) {
        auto& notes = project->getNotes();
        for (auto& note : notes) {
            if (note.getEndFrame() > minFrame && note.getStartFrame() < maxFrame) {
                if (note.hasDeltaPitch()) {
                    note.setDeltaPitch(std::vector<float>());
                }
            }
        }
        project->setF0DirtyRange(minFrame, maxFrame);
    }

    // Create undo action
    if (undoManager && project) {
        auto& audioData = project->getAudioData();
        auto action = std::make_unique<F0EditAction>(
            &audioData.f0, &audioData.voicedMask, drawingEdits,
            [this](int minFrame, int maxFrame) {
                if (project) {
                    project->setF0DirtyRange(minFrame, maxFrame);
                    if (onPitchEditFinished)
                        onPitchEditFinished();
                }
            });
        undoManager->addAction(std::move(action));
    }

    drawingEdits.clear();
    drawingEditIndexByFrame.clear();
    lastDrawFrame = -1;
    lastDrawValueCents = 0;
    activeDrawCurve = nullptr;
    drawCurves.clear();

    isDrawing = false;

    if (onPitchEditFinished)
        onPitchEditFinished();
}

void PitchEditor::applyPitchPoint(int frameIndex, int midiCents) {
    if (!project)
        return;

    auto& audioData = project->getAudioData();
    if (audioData.f0.empty())
        return;

    const int f0Size = static_cast<int>(audioData.f0.size());
    if (audioData.deltaPitch.size() < audioData.f0.size())
        audioData.deltaPitch.resize(audioData.f0.size(), 0.0f);
    if (audioData.basePitch.size() < audioData.f0.size())
        audioData.basePitch.resize(audioData.f0.size(), 0.0f);
    if (frameIndex < 0 || frameIndex >= f0Size)
        return;

    auto applyFrame = [&](int idx, int cents) {
        if (idx < 0 || idx >= f0Size)
            return;

        const float newFreq = midiToFreq(static_cast<float>(cents) / 100.0f);
        const float oldF0 = audioData.f0[idx];
        const bool oldVoiced = (idx < static_cast<int>(audioData.voicedMask.size()))
                                   ? audioData.voicedMask[idx]
                                   : false;

        auto it = drawingEditIndexByFrame.find(idx);
        if (it == drawingEditIndexByFrame.end()) {
            drawingEditIndexByFrame.emplace(idx, drawingEdits.size());
            drawingEdits.push_back(F0FrameEdit{idx, oldF0, newFreq, oldVoiced, true});

            // Clear deltaPitch for notes containing this frame
            auto& notes = project->getNotes();
            for (auto& note : notes) {
                if (note.getStartFrame() <= idx && note.getEndFrame() > idx && note.hasDeltaPitch()) {
                    note.setDeltaPitch(std::vector<float>());
                    break;
                }
            }
        } else {
            auto& e = drawingEdits[it->second];
            e.newF0 = newFreq;
            e.newVoiced = true;
        }

        audioData.f0[idx] = newFreq;
        if (idx < static_cast<int>(audioData.basePitch.size()) &&
            idx < static_cast<int>(audioData.deltaPitch.size())) {
            float baseMidi = audioData.basePitch[static_cast<size_t>(idx)];
            float newMidi = static_cast<float>(cents) / 100.0f;
            audioData.deltaPitch[static_cast<size_t>(idx)] = newMidi - baseMidi;
        }
        if (idx < static_cast<int>(audioData.voicedMask.size()))
            audioData.voicedMask[idx] = true;
    };

    if (!activeDrawCurve || frameIndex < activeDrawCurve->localStart()) {
        startNewPitchCurve(frameIndex, midiCents);
        applyFrame(frameIndex, midiCents);
        return;
    }

    if (lastDrawFrame < 0) {
        applyFrame(frameIndex, midiCents);
    } else {
        int start = lastDrawFrame;
        int end = frameIndex;
        int startVal = lastDrawValueCents;
        int endVal = midiCents;

        if (start == end) {
            applyFrame(frameIndex, midiCents);
        } else {
            int step = (end > start) ? 1 : -1;
            int length = std::abs(end - start);
            for (int i = 0; i <= length; ++i) {
                int idx = start + i * step;
                float t = length == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(length);
                float v = juce::jmap(t, 0.0f, 1.0f, static_cast<float>(startVal), static_cast<float>(endVal));
                int cents = static_cast<int>(std::round(v));
                applyFrame(idx, cents);
            }
        }
    }

    lastDrawFrame = frameIndex;
    lastDrawValueCents = midiCents;
}

void PitchEditor::startNewPitchCurve(int frameIndex, int midiCents) {
    drawCurves.push_back(std::make_unique<DrawCurve>(frameIndex, 1));
    activeDrawCurve = drawCurves.back().get();
    activeDrawCurve->appendValue(midiCents);
    lastDrawFrame = frameIndex;
    lastDrawValueCents = midiCents;
}

void PitchEditor::snapNoteToSemitone(Note* note) {
    if (!note || !project)
        return;

    float currentOffset = note->getPitchOffset();
    float snappedOffset = std::round(currentOffset);

    if (std::abs(snappedOffset - currentOffset) > 0.001f) {
        if (undoManager) {
            auto action = std::make_unique<PitchOffsetAction>(note, currentOffset, snappedOffset);
            undoManager->addAction(std::move(action));
        }

        note->setPitchOffset(snappedOffset);
        note->markDirty();

        if (onPitchEdited)
            onPitchEdited();
        if (onPitchEditFinished)
            onPitchEditFinished();
    }
}
