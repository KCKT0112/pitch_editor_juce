#include "PianoRollComponent.h"
#include <cmath>

PianoRollComponent::PianoRollComponent()
{
    addAndMakeVisible(horizontalScrollBar);
    addAndMakeVisible(verticalScrollBar);
    
    horizontalScrollBar.addListener(this);
    verticalScrollBar.addListener(this);
    
    // Set initial scroll range
    verticalScrollBar.setRangeLimits(0, (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone);
    verticalScrollBar.setCurrentRange(0, 500);
}

PianoRollComponent::~PianoRollComponent()
{
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(COLOR_BACKGROUND));
    
    // Create clipping region for main area
    auto mainArea = getLocalBounds()
        .withTrimmedLeft(pianoKeysWidth)
        .withTrimmedBottom(14)
        .withTrimmedRight(14);
    
    {
        juce::Graphics::ScopedSaveState saveState(g);
        g.reduceClipRegion(mainArea);
        g.setOrigin(pianoKeysWidth - static_cast<int>(scrollX), -static_cast<int>(scrollY));
        
        drawGrid(g);
        drawNotes(g);
        drawPitchCurves(g);
        drawCursor(g);
    }
    
    // Draw piano keys
    drawPianoKeys(g);
}

void PianoRollComponent::resized()
{
    auto bounds = getLocalBounds();
    
    horizontalScrollBar.setBounds(
        pianoKeysWidth, bounds.getHeight() - 14,
        bounds.getWidth() - pianoKeysWidth - 14, 14);
    
    verticalScrollBar.setBounds(
        bounds.getWidth() - 14, 0,
        14, bounds.getHeight() - 14);
    
    updateScrollBars();
}

void PianoRollComponent::drawGrid(juce::Graphics& g)
{
    if (!project) return;
    
    float duration = project->getAudioData().getDuration();
    float width = duration * pixelsPerSecond;
    float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
    
    // Horizontal lines (pitch)
    g.setColour(juce::Colour(COLOR_GRID));
    
    for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi)
    {
        float y = midiToY(static_cast<float>(midi));
        int noteInOctave = midi % 12;
        
        if (noteInOctave == 0)  // C
        {
            g.setColour(juce::Colour(COLOR_GRID_BAR));
            g.drawHorizontalLine(static_cast<int>(y), 0, width);
            g.setColour(juce::Colour(COLOR_GRID));
        }
        else
        {
            g.drawHorizontalLine(static_cast<int>(y), 0, width);
        }
    }
    
    // Vertical lines (time)
    float secondsPerBeat = 60.0f / 120.0f;  // Assuming 120 BPM
    float pixelsPerBeat = secondsPerBeat * pixelsPerSecond;
    
    for (float x = 0; x < width; x += pixelsPerBeat)
    {
        g.setColour(juce::Colour(COLOR_GRID));
        g.drawVerticalLine(static_cast<int>(x), 0, height);
    }
}

void PianoRollComponent::drawNotes(juce::Graphics& g)
{
    if (!project) return;
    
    for (auto& note : project->getNotes())
    {
        float x = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
        float w = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
        float y = midiToY(note.getAdjustedMidiNote());
        float h = pixelsPerSemitone;
        
        // Note color
        juce::Colour noteColor = note.isSelected() 
            ? juce::Colour(COLOR_NOTE_SELECTED) 
            : getNoteColor(static_cast<int>(note.getAdjustedMidiNote()));
        
        // Draw note rectangle
        g.setColour(noteColor.withAlpha(0.8f));
        g.fillRoundedRectangle(x, y, w, h, 3.0f);
        
        // Border
        g.setColour(noteColor.brighter(0.3f));
        g.drawRoundedRectangle(x, y, w, h, 3.0f, 1.5f);
    }
}

void PianoRollComponent::drawPitchCurves(juce::Graphics& g)
{
    if (!project) return;
    
    const auto& audioData = project->getAudioData();
    if (audioData.f0.empty()) return;
    
    // Get global pitch offset
    float globalOffset = project->getGlobalPitchOffset();
    
    // Draw pitch curves per note with their pitch offsets applied
    g.setColour(juce::Colour(COLOR_PITCH_CURVE));
    
    for (const auto& note : project->getNotes())
    {
        juce::Path path;
        bool pathStarted = false;
        
        // Get pitch offset for this note
        float noteOffset = note.getPitchOffset() + globalOffset;
        
        int startFrame = note.getStartFrame();
        int endFrame = std::min(note.getEndFrame(), static_cast<int>(audioData.f0.size()));
        
        for (int i = startFrame; i < endFrame; ++i)
        {
            float f0 = audioData.f0[i];
            
            if (f0 > 0.0f && i < static_cast<int>(audioData.voicedMask.size()) && audioData.voicedMask[i])
            {
                // Apply pitch offset to the displayed F0
                float adjustedF0 = f0 * std::pow(2.0f, noteOffset / 12.0f);
                float midi = freqToMidi(adjustedF0);
                float x = framesToSeconds(i) * pixelsPerSecond;
                float y = midiToY(midi);
                
                if (!pathStarted)
                {
                    path.startNewSubPath(x, y);
                    pathStarted = true;
                }
                else
                {
                    path.lineTo(x, y);
                }
            }
            else if (pathStarted)
            {
                g.strokePath(path, juce::PathStrokeType(2.0f));
                path.clear();
                pathStarted = false;
            }
        }
        
        if (pathStarted)
        {
            g.strokePath(path, juce::PathStrokeType(2.0f));
        }
    }
    
    // Also draw unassigned F0 regions (outside of notes) in a dimmer color
    g.setColour(juce::Colour(COLOR_PITCH_CURVE).withAlpha(0.3f));
    juce::Path unassignedPath;
    bool unassignedStarted = false;
    
    for (size_t i = 0; i < audioData.f0.size(); ++i)
    {
        // Check if this frame is inside any note
        bool inNote = false;
        for (const auto& note : project->getNotes())
        {
            if (i >= static_cast<size_t>(note.getStartFrame()) && 
                i < static_cast<size_t>(note.getEndFrame()))
            {
                inNote = true;
                break;
            }
        }
        
        if (!inNote)
        {
            float f0 = audioData.f0[i];
            if (f0 > 0.0f && i < audioData.voicedMask.size() && audioData.voicedMask[i])
            {
                float midi = freqToMidi(f0);
                float x = framesToSeconds(static_cast<int>(i)) * pixelsPerSecond;
                float y = midiToY(midi);
                
                if (!unassignedStarted)
                {
                    unassignedPath.startNewSubPath(x, y);
                    unassignedStarted = true;
                }
                else
                {
                    unassignedPath.lineTo(x, y);
                }
            }
            else if (unassignedStarted)
            {
                g.strokePath(unassignedPath, juce::PathStrokeType(1.0f));
                unassignedPath.clear();
                unassignedStarted = false;
            }
        }
        else if (unassignedStarted)
        {
            g.strokePath(unassignedPath, juce::PathStrokeType(1.0f));
            unassignedPath.clear();
            unassignedStarted = false;
        }
    }
    
    if (unassignedStarted)
    {
        g.strokePath(unassignedPath, juce::PathStrokeType(1.0f));
    }
}

void PianoRollComponent::drawCursor(juce::Graphics& g)
{
    float x = timeToX(cursorTime);
    float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
    
    g.setColour(juce::Colours::red);
    g.drawVerticalLine(static_cast<int>(x), 0, height);
}

void PianoRollComponent::drawPianoKeys(juce::Graphics& g)
{
    auto keyArea = getLocalBounds().withWidth(pianoKeysWidth).withTrimmedBottom(14);
    
    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRect(keyArea);
    
    // Draw each key
    for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi)
    {
        float y = midiToY(static_cast<float>(midi)) - scrollY;
        int noteInOctave = midi % 12;
        
        // Check if it's a black key
        bool isBlack = (noteInOctave == 1 || noteInOctave == 3 || 
                        noteInOctave == 6 || noteInOctave == 8 || noteInOctave == 10);
        
        if (isBlack)
        {
            g.setColour(juce::Colour(0xFF2D2D37));
        }
        else
        {
            g.setColour(juce::Colour(0xFF3D3D47));
        }
        
        g.fillRect(0.0f, y, static_cast<float>(pianoKeysWidth - 2), pixelsPerSemitone - 1);
        
        // Draw note name for C notes
        if (noteInOctave == 0)
        {
            int octave = midi / 12 - 1;
            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText("C" + juce::String(octave), 2, static_cast<int>(y), 
                      pianoKeysWidth - 4, static_cast<int>(pixelsPerSemitone),
                      juce::Justification::centredLeft);
        }
    }
}

float PianoRollComponent::midiToY(float midiNote) const
{
    return (MAX_MIDI_NOTE - midiNote) * pixelsPerSemitone;
}

float PianoRollComponent::yToMidi(float y) const
{
    return MAX_MIDI_NOTE - y / pixelsPerSemitone;
}

float PianoRollComponent::timeToX(double time) const
{
    return static_cast<float>(time * pixelsPerSecond);
}

double PianoRollComponent::xToTime(float x) const
{
    return x / pixelsPerSecond;
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!project) return;
    
    float adjustedX = e.x - pianoKeysWidth + scrollX;
    float adjustedY = e.y + scrollY;
    
    // Check if clicking on a note
    Note* note = findNoteAt(adjustedX, adjustedY);
    
    if (note)
    {
        // Select note
        project->deselectAllNotes();
        note->setSelected(true);
        
        if (onNoteSelected)
            onNoteSelected(note);
        
        // Start dragging
        isDragging = true;
        draggedNote = note;
        dragStartY = e.y;
        originalPitchOffset = note->getPitchOffset();
        
        repaint();
    }
    else
    {
        // Seek to position
        double time = xToTime(adjustedX);
        cursorTime = std::max(0.0, time);
        
        if (onSeek)
            onSeek(cursorTime);
        
        project->deselectAllNotes();
        repaint();
    }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (isDragging && draggedNote)
    {
        // Calculate pitch offset from drag
        float deltaY = dragStartY - e.y;
        float deltaSemitones = deltaY / pixelsPerSemitone;
        
        draggedNote->setPitchOffset(originalPitchOffset + deltaSemitones);
        
        if (onPitchEdited)
            onPitchEdited();
        
        repaint();
    }
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& e)
{
    isDragging = false;
    draggedNote = nullptr;
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& e)
{
    // Could implement hover effects here
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        // Zoom
        float zoomFactor = 1.0f + wheel.deltaY * 0.1f;
        
        if (e.mods.isShiftDown())
        {
            // Vertical zoom
            setPixelsPerSemitone(pixelsPerSemitone * zoomFactor);
        }
        else
        {
            // Horizontal zoom
            setPixelsPerSecond(pixelsPerSecond * zoomFactor);
        }
    }
    else
    {
        // Scroll
        if (e.mods.isShiftDown())
        {
            horizontalScrollBar.setCurrentRangeStart(scrollX - wheel.deltaY * 50.0);
        }
        else
        {
            verticalScrollBar.setCurrentRangeStart(scrollY - wheel.deltaY * 50.0);
        }
    }
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollBar)
    {
        scrollX = newRangeStart;
    }
    else if (scrollBar == &verticalScrollBar)
    {
        scrollY = newRangeStart;
    }
    repaint();
}

void PianoRollComponent::setProject(Project* proj)
{
    project = proj;
    updateScrollBars();
    repaint();
}

void PianoRollComponent::setCursorTime(double time)
{
    cursorTime = time;
    repaint();
}

void PianoRollComponent::setPixelsPerSecond(float pps)
{
    pixelsPerSecond = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, pps);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::setPixelsPerSemitone(float pps)
{
    pixelsPerSemitone = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, pps);
    updateScrollBars();
    repaint();
}

Note* PianoRollComponent::findNoteAt(float x, float y)
{
    if (!project) return nullptr;
    
    for (auto& note : project->getNotes())
    {
        float noteX = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
        float noteW = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
        float noteY = midiToY(note.getAdjustedMidiNote());
        float noteH = pixelsPerSemitone;
        
        if (x >= noteX && x < noteX + noteW && y >= noteY && y < noteY + noteH)
        {
            return &note;
        }
    }
    
    return nullptr;
}

void PianoRollComponent::updateScrollBars()
{
    if (project)
    {
        float totalWidth = project->getAudioData().getDuration() * pixelsPerSecond;
        float totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
        
        int visibleWidth = getWidth() - pianoKeysWidth - 14;
        int visibleHeight = getHeight() - 14;
        
        horizontalScrollBar.setRangeLimits(0, totalWidth);
        horizontalScrollBar.setCurrentRange(scrollX, visibleWidth);
        
        verticalScrollBar.setRangeLimits(0, totalHeight);
        verticalScrollBar.setCurrentRange(scrollY, visibleHeight);
    }
}
