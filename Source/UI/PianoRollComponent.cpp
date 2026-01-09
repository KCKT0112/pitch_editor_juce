#include "PianoRollComponent.h"
#include <cmath>
#include <limits>

PianoRollComponent::PianoRollComponent() {
  addAndMakeVisible(horizontalScrollBar);
  addAndMakeVisible(verticalScrollBar);

  horizontalScrollBar.addListener(this);
  verticalScrollBar.addListener(this);

  // Style scrollbars to match theme
  auto thumbColor = juce::Colour(COLOR_PRIMARY).withAlpha(0.6f);
  auto trackColor = juce::Colour(0xFF252530);

  horizontalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
  horizontalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);
  verticalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
  verticalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);

  // Set initial scroll range
  verticalScrollBar.setRangeLimits(0, (MAX_MIDI_NOTE - MIN_MIDI_NOTE) *
                                          pixelsPerSemitone);
  verticalScrollBar.setCurrentRange(0, 500);

  // Default view centered on C3-C4 (MIDI 48-60)
  centerOnPitchRange(48.0f, 60.0f);
}

PianoRollComponent::~PianoRollComponent() {}

void PianoRollComponent::paint(juce::Graphics &g) {
  // Background
  g.fillAll(juce::Colour(COLOR_BACKGROUND));

  constexpr int scrollBarSize = 8;

  // Create clipping region for main area (below timeline)
  auto mainArea = getLocalBounds()
                      .withTrimmedLeft(pianoKeysWidth)
                      .withTrimmedTop(timelineHeight)
                      .withTrimmedBottom(scrollBarSize)
                      .withTrimmedRight(scrollBarSize);

  // Draw background waveform (only horizontal scroll, fills visible height)
  {
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(mainArea);
    drawBackgroundWaveform(g, mainArea);
  }

  // Draw scrolled content (grid, notes, pitch curves, cursor)
  {
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(mainArea);
    g.setOrigin(pianoKeysWidth - static_cast<int>(scrollX),
                timelineHeight - static_cast<int>(scrollY));

    drawGrid(g);
    drawNotes(g);
    drawPitchCurves(g);
    drawCursor(g);
  }

  // Draw timeline (above grid, scrolls horizontally)
  drawTimeline(g);

  // Draw piano keys
  drawPianoKeys(g);
}

void PianoRollComponent::resized() {
  auto bounds = getLocalBounds();
  constexpr int scrollBarSize = 8;

  horizontalScrollBar.setBounds(
      pianoKeysWidth, bounds.getHeight() - scrollBarSize,
      bounds.getWidth() - pianoKeysWidth - scrollBarSize, scrollBarSize);

  verticalScrollBar.setBounds(bounds.getWidth() - scrollBarSize, timelineHeight,
                              scrollBarSize,
                              bounds.getHeight() - scrollBarSize - timelineHeight);

  updateScrollBars();
}

void PianoRollComponent::drawBackgroundWaveform(
    juce::Graphics &g, const juce::Rectangle<int> &visibleArea) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  if (audioData.waveform.getNumSamples() == 0)
    return;

  float duration = audioData.getDuration();
  float totalWidth = duration * pixelsPerSecond;

  const float *samples = audioData.waveform.getReadPointer(0);
  int numSamples = audioData.waveform.getNumSamples();

  // Draw waveform filling the visible area height
  float visibleHeight = static_cast<float>(visibleArea.getHeight());
  float centerY = visibleArea.getY() + visibleHeight * 0.5f;
  float waveformHeight = visibleHeight * 0.8f;

  juce::Path waveformPath;
  int visibleWidth = visibleArea.getWidth();

  // Start from left edge of visible area
  float startX = static_cast<float>(visibleArea.getX());
  waveformPath.startNewSubPath(startX, centerY);

  // Draw only the visible portion
  for (int px = 0; px < visibleWidth; ++px) {
    float x = startX + px;
    double time = (scrollX + px) / pixelsPerSecond;
    int startSample = static_cast<int>(time * SAMPLE_RATE);
    int endSample =
        static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

    startSample = std::max(0, std::min(startSample, numSamples - 1));
    endSample = std::max(startSample + 1, std::min(endSample, numSamples));

    float maxVal = 0.0f;
    for (int i = startSample; i < endSample; ++i)
      maxVal = std::max(maxVal, std::abs(samples[i]));

    float y = centerY - maxVal * waveformHeight * 0.5f;
    waveformPath.lineTo(x, y);
  }

  // Bottom half (reverse)
  for (int px = visibleWidth - 1; px >= 0; --px) {
    float x = startX + px;
    double time = (scrollX + px) / pixelsPerSecond;
    int startSample = static_cast<int>(time * SAMPLE_RATE);
    int endSample =
        static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

    startSample = std::max(0, std::min(startSample, numSamples - 1));
    endSample = std::max(startSample + 1, std::min(endSample, numSamples));

    float maxVal = 0.0f;
    for (int i = startSample; i < endSample; ++i)
      maxVal = std::max(maxVal, std::abs(samples[i]));

    float y = centerY + maxVal * waveformHeight * 0.5f;
    waveformPath.lineTo(x, y);
  }

  waveformPath.closeSubPath();

  g.setColour(juce::Colour(COLOR_WAVEFORM));
  g.fillPath(waveformPath);
}

void PianoRollComponent::drawGrid(juce::Graphics &g) {
  float duration = project ? project->getAudioData().getDuration() : 60.0f;
  float width =
      std::max(duration * pixelsPerSecond, static_cast<float>(getWidth()));
  float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  // Horizontal lines (pitch)
  g.setColour(juce::Colour(COLOR_GRID));

  for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
    float y = midiToY(static_cast<float>(midi));
    int noteInOctave = midi % 12;

    if (noteInOctave == 0) // C
    {
      g.setColour(juce::Colour(COLOR_GRID_BAR));
      g.drawHorizontalLine(static_cast<int>(y), 0, width);
      g.setColour(juce::Colour(COLOR_GRID));
    } else {
      g.drawHorizontalLine(static_cast<int>(y), 0, width);
    }
  }

  // Vertical lines (time)
  float secondsPerBeat = 60.0f / 120.0f; // Assuming 120 BPM
  float pixelsPerBeat = secondsPerBeat * pixelsPerSecond;

  for (float x = 0; x < width; x += pixelsPerBeat) {
    g.setColour(juce::Colour(COLOR_GRID));
    g.drawVerticalLine(static_cast<int>(x), 0, height);
  }
}

void PianoRollComponent::drawTimeline(juce::Graphics &g) {
  constexpr int scrollBarSize = 8;
  auto timelineArea = juce::Rectangle<int>(
      pianoKeysWidth, 0,
      getWidth() - pianoKeysWidth - scrollBarSize, timelineHeight);

  // Background
  g.setColour(juce::Colour(0xFF1E1E28));
  g.fillRect(timelineArea);

  // Bottom border
  g.setColour(juce::Colour(COLOR_GRID_BAR));
  g.drawHorizontalLine(timelineHeight - 1, static_cast<float>(pianoKeysWidth),
                       static_cast<float>(getWidth() - scrollBarSize));

  // Determine tick interval based on zoom level
  float secondsPerTick;
  if (pixelsPerSecond >= 200.0f)
    secondsPerTick = 0.5f;
  else if (pixelsPerSecond >= 100.0f)
    secondsPerTick = 1.0f;
  else if (pixelsPerSecond >= 50.0f)
    secondsPerTick = 2.0f;
  else if (pixelsPerSecond >= 25.0f)
    secondsPerTick = 5.0f;
  else
    secondsPerTick = 10.0f;

  float duration = project ? project->getAudioData().getDuration() : 60.0f;

  // Draw ticks and labels
  g.setFont(11.0f);

  for (float time = 0.0f; time <= duration + secondsPerTick; time += secondsPerTick) {
    float x = pianoKeysWidth + time * pixelsPerSecond - static_cast<float>(scrollX);

    if (x < pianoKeysWidth - 50 || x > getWidth())
      continue;

    // Tick mark
    bool isMajor = std::fmod(time, secondsPerTick * 2.0f) < 0.001f;
    int tickHeight = isMajor ? 8 : 4;

    g.setColour(juce::Colour(COLOR_GRID_BAR));
    g.drawVerticalLine(static_cast<int>(x), static_cast<float>(timelineHeight - tickHeight),
                       static_cast<float>(timelineHeight - 1));

    // Time label (only on major ticks)
    if (isMajor) {
      int minutes = static_cast<int>(time) / 60;
      int seconds = static_cast<int>(time) % 60;
      int tenths = static_cast<int>((time - std::floor(time)) * 10);

      juce::String label;
      if (minutes > 0)
        label = juce::String::formatted("%d:%02d", minutes, seconds);
      else if (secondsPerTick < 1.0f)
        label = juce::String::formatted("%d.%d", seconds, tenths);
      else
        label = juce::String::formatted("%ds", seconds);

      g.setColour(juce::Colour(0xFFAAAAAA));
      g.drawText(label, static_cast<int>(x) + 3, 2, 50, timelineHeight - 4,
                 juce::Justification::centredLeft, false);
    }
  }
}

void PianoRollComponent::drawNotes(juce::Graphics &g) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  const float *samples = audioData.waveform.getNumSamples() > 0
                             ? audioData.waveform.getReadPointer(0)
                             : nullptr;
  int totalSamples = audioData.waveform.getNumSamples();

  for (auto &note : project->getNotes()) {
    float x = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
    float w = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
    float h = pixelsPerSemitone;

    // Center on base MIDI note's grid row, then offset by pitch adjustment
    float baseGridCenterY =
        midiToY(note.getMidiNote()) + pixelsPerSemitone * 0.5f;
    float pitchOffsetPixels = -note.getPitchOffset() * pixelsPerSemitone;
    float y = baseGridCenterY + pitchOffsetPixels - h * 0.5f;

    // Note color based on pitch
    juce::Colour noteColor = note.isSelected()
                                 ? juce::Colour(COLOR_NOTE_SELECTED)
                                 : juce::Colour(COLOR_NOTE_NORMAL);

    if (samples && totalSamples > 0 && w > 2.0f) {
      // Draw waveform slice inside note
      int startSample = static_cast<int>(framesToSeconds(note.getStartFrame()) *
                                         audioData.sampleRate);
      int endSample = static_cast<int>(framesToSeconds(note.getEndFrame()) *
                                       audioData.sampleRate);
      startSample = std::max(0, std::min(startSample, totalSamples - 1));
      endSample = std::max(startSample + 1, std::min(endSample, totalSamples));

      int numNoteSamples = endSample - startSample;
      int samplesPerPixel = std::max(1, static_cast<int>(numNoteSamples / w));

      float centerY = y + h * 0.5f;
      float waveHeight = h * 1.10f;

      // Build waveform data
      std::vector<float> waveValues;
      float step = std::max(1.0f, w / 400.0f);  // Limit to ~400 points for smoothness

      for (float px = 0; px <= w; px += step) {
        int sampleIdx =
            startSample + static_cast<int>((px / w) * numNoteSamples);
        int sampleEnd = std::min(sampleIdx + samplesPerPixel, endSample);

        float maxVal = 0.0f;
        for (int i = sampleIdx; i < sampleEnd; ++i)
          maxVal = std::max(maxVal, std::abs(samples[i]));

        waveValues.push_back(maxVal);
      }

      // Draw filled waveform
      size_t numPoints = waveValues.size();
      g.setColour(noteColor.withAlpha(0.85f));
      for (size_t i = 0; i + 1 < numPoints; ++i) {
        float px1 = (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        float px2 = (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) * w;
        float halfH1 = waveValues[i] * waveHeight * 0.5f;
        float halfH2 = waveValues[i + 1] * waveHeight * 0.5f;

        juce::Path segment;
        segment.startNewSubPath(x + px1, centerY - halfH1);
        segment.lineTo(x + px2, centerY - halfH2);
        segment.lineTo(x + px2, centerY + halfH2);
        segment.lineTo(x + px1, centerY + halfH1);
        segment.closeSubPath();

        g.fillPath(segment);
      }

      // Draw smooth outline
      juce::Path outline;
      outline.startNewSubPath(x, centerY);
      for (size_t i = 0; i < numPoints; ++i) {
        float px = (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        outline.lineTo(x + px, centerY - waveValues[i] * waveHeight * 0.5f);
      }
      for (int i = static_cast<int>(numPoints) - 1; i >= 0; --i) {
        float px = (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        outline.lineTo(x + px, centerY + waveValues[i] * waveHeight * 0.5f);
      }
      outline.closeSubPath();
      g.setColour(noteColor.brighter(0.2f));
      g.strokePath(outline, juce::PathStrokeType(1.0f));
    } else {
      // Fallback: simple rectangle for very short notes
      g.setColour(noteColor.withAlpha(0.85f));
      g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
    }
  }
}

void PianoRollComponent::drawPitchCurves(juce::Graphics &g) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  // Get global pitch offset
  float globalOffset = project->getGlobalPitchOffset();

  // Build a map of frame -> pitch offset for quick lookup
  std::vector<float> frameOffsets(audioData.f0.size(), globalOffset);
  for (const auto &note : project->getNotes()) {
    float noteOffset = note.getPitchOffset() + globalOffset;
    int startFrame = note.getStartFrame();
    int endFrame = std::min(note.getEndFrame(), static_cast<int>(audioData.f0.size()));
    for (int i = startFrame; i < endFrame; ++i) {
      frameOffsets[i] = noteOffset;
    }
  }

  // Draw a single continuous pitch curve
  g.setColour(juce::Colour(COLOR_PITCH_CURVE));
  juce::Path path;
  bool pathStarted = false;
  int gapFrames = 0;
  const int maxGapFrames = 5;  // Allow small gaps to keep curve connected

  for (size_t i = 0; i < audioData.f0.size(); ++i) {
    float f0 = audioData.f0[i];

    if (f0 > 0.0f && i < audioData.voicedMask.size() && audioData.voicedMask[i]) {
      // Apply pitch offset
      float adjustedF0 = f0 * std::pow(2.0f, frameOffsets[i] / 12.0f);
      float midi = freqToMidi(adjustedF0);
      float x = framesToSeconds(static_cast<int>(i)) * pixelsPerSecond;
      float y = midiToY(midi);

      if (!pathStarted) {
        path.startNewSubPath(x, y);
        pathStarted = true;
      } else {
        path.lineTo(x, y);
      }
      gapFrames = 0;
    } else if (pathStarted) {
      gapFrames++;
      // Only break the path after a significant gap
      if (gapFrames > maxGapFrames) {
        g.strokePath(path, juce::PathStrokeType(2.0f));
        path.clear();
        pathStarted = false;
        gapFrames = 0;
      }
    }
  }

  if (pathStarted) {
    g.strokePath(path, juce::PathStrokeType(2.0f));
  }
}

void PianoRollComponent::drawCursor(juce::Graphics &g) {
  float x = timeToX(cursorTime);
  float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  g.setColour(juce::Colours::red);
  g.drawVerticalLine(static_cast<int>(x), 0, height);
}

void PianoRollComponent::drawPianoKeys(juce::Graphics &g) {
  constexpr int scrollBarSize = 8;
  auto keyArea = getLocalBounds()
                     .withWidth(pianoKeysWidth)
                     .withTrimmedTop(timelineHeight)
                     .withTrimmedBottom(scrollBarSize);

  // Background
  g.setColour(juce::Colour(0xFF1A1A24));
  g.fillRect(keyArea);

  static const char *noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};

  // Draw each key
  for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
    float y = midiToY(static_cast<float>(midi)) - static_cast<float>(scrollY) + timelineHeight;
    int noteInOctave = midi % 12;

    // Check if it's a black key
    bool isBlack =
        (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
         noteInOctave == 8 || noteInOctave == 10);

    if (isBlack)
      g.setColour(juce::Colour(0xFF2D2D37));
    else
      g.setColour(juce::Colour(0xFF3D3D47));

    g.fillRect(0.0f, y, static_cast<float>(pianoKeysWidth - 2),
               pixelsPerSemitone - 1);

    // Draw note name for all notes
    int octave = midi / 12 - 1;
    juce::String noteName =
        juce::String(noteNames[noteInOctave]) + juce::String(octave);

    // Use dimmer color for black keys
    g.setColour(isBlack ? juce::Colour(0xFFAAAAAA) : juce::Colours::white);
    g.setFont(12.0f);
    g.drawText(noteName, pianoKeysWidth - 36, static_cast<int>(y), 32,
               static_cast<int>(pixelsPerSemitone),
               juce::Justification::centred);
  }
}

float PianoRollComponent::midiToY(float midiNote) const {
  return (MAX_MIDI_NOTE - midiNote) * pixelsPerSemitone;
}

float PianoRollComponent::yToMidi(float y) const {
  return MAX_MIDI_NOTE - y / pixelsPerSemitone;
}

float PianoRollComponent::timeToX(double time) const {
  return static_cast<float>(time * pixelsPerSecond);
}

double PianoRollComponent::xToTime(float x) const {
  return x / pixelsPerSecond;
}

void PianoRollComponent::mouseDown(const juce::MouseEvent &e) {
  if (!project)
    return;

  float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
  float adjustedY = e.y - timelineHeight + static_cast<float>(scrollY);

  // Ignore clicks in timeline area
  if (e.y < timelineHeight)
    return;

  if (editMode == EditMode::Draw) {
    // Start drawing
    isDrawing = true;
    lastDrawX = 0.0f;
    lastDrawY = 0.0f;
    drawingEdits.clear();
    drawingEditIndexByFrame.clear();

    applyPitchDrawing(adjustedX, adjustedY);

    if (onPitchEdited)
      onPitchEdited();

    repaint();
    return;
  }

  // Check if clicking on a note
  Note *note = findNoteAt(adjustedX, adjustedY);

  if (note) {
    // Select note
    project->deselectAllNotes();
    note->setSelected(true);

    if (onNoteSelected)
      onNoteSelected(note);

    // Start dragging
    isDragging = true;
    draggedNote = note;
    dragStartY = static_cast<float>(e.y);
    originalPitchOffset = note->getPitchOffset();

    repaint();
  } else {
    // Seek to position
    double time = xToTime(adjustedX);
    cursorTime = std::max(0.0, time);

    if (onSeek)
      onSeek(cursorTime);

    project->deselectAllNotes();
    repaint();
  }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent &e) {
  if (editMode == EditMode::Draw && isDrawing) {
    float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
    float adjustedY = e.y - timelineHeight + static_cast<float>(scrollY);

    applyPitchDrawing(adjustedX, adjustedY);

    if (onPitchEdited)
      onPitchEdited();

    repaint();
    return;
  }

  if (isDragging && draggedNote) {
    // Calculate pitch offset from drag
    float deltaY = dragStartY - e.y;
    float deltaSemitones = deltaY / pixelsPerSemitone;

    float newOffset = originalPitchOffset + deltaSemitones;

    // Store old offset for undo if just starting drag
    if (undoManager && std::abs(newOffset - originalPitchOffset) > 0.01f) {
      // Note: We'll create the undo action in mouseUp
    }

    draggedNote->setPitchOffset(newOffset);
    draggedNote->markDirty(); // Mark as dirty for incremental synthesis

    if (onPitchEdited)
      onPitchEdited();

    repaint();
  }
}

void PianoRollComponent::mouseUp(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);

  if (editMode == EditMode::Draw && isDrawing) {
    isDrawing = false;
    commitPitchDrawing();
    repaint();
    return;
  }

  if (isDragging && draggedNote) {
    float newOffset = draggedNote->getPitchOffset();

    // Create undo action if offset changed
    if (undoManager && std::abs(newOffset - originalPitchOffset) > 0.001f) {
      auto action = std::make_unique<PitchOffsetAction>(
          draggedNote, originalPitchOffset, newOffset);
      undoManager->addAction(std::move(action));
    }

    // Trigger incremental synthesis when pitch edit is finished
    if (onPitchEditFinished)
      onPitchEditFinished();
  }

  isDragging = false;
  draggedNote = nullptr;
}

void PianoRollComponent::mouseMove(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
  // Could implement hover effects here
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent &e) {
  if (!project)
    return;

  float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
  float adjustedY = e.y + static_cast<float>(scrollY);

  // Check if double-clicking on a note
  Note *note = findNoteAt(adjustedX, adjustedY);

  if (note) {
    // Snap pitch offset to nearest semitone
    float currentOffset = note->getPitchOffset();
    float snappedOffset = std::round(currentOffset);

    if (std::abs(snappedOffset - currentOffset) > 0.001f) {
      // Create undo action
      if (undoManager && note) {
        // Store note index for safer undo (in case notes change)
        auto action = std::make_unique<PitchOffsetAction>(note, currentOffset,
                                                          snappedOffset);
        undoManager->addAction(std::move(action));
      }

      if (note) // Double-check before use
      {
        note->setPitchOffset(snappedOffset);
        note->markDirty();
      }

      if (onPitchEdited)
        onPitchEdited();

      if (onPitchEditFinished)
        onPitchEditFinished();

      repaint();
    }
  }
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent &e,
                                        const juce::MouseWheelDetails &wheel) {
  // Sensitivity: smooth scrolling (trackpad) uses smaller deltas, mouse wheel
  // uses larger
  float scrollMultiplier = wheel.isSmooth ? 200.0f : 80.0f;

  // Cmd/Ctrl + scroll = zoom
  if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
    float zoomFactor = 1.0f + wheel.deltaY * 0.3f;

    if (e.mods.isShiftDown()) {
      // Vertical zoom
      setPixelsPerSemitone(pixelsPerSemitone * zoomFactor);
    } else {
      // Horizontal zoom - center on mouse position
      float mouseX = static_cast<float>(e.x - pianoKeysWidth);
      double timeAtMouse = xToTime(mouseX + static_cast<float>(scrollX));

      float newPps = pixelsPerSecond * zoomFactor;
      newPps =
          juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

      // Adjust scroll to keep mouse position stable
      float newMouseX = static_cast<float>(timeAtMouse * newPps);
      scrollX = std::max(0.0, static_cast<double>(newMouseX - mouseX));

      pixelsPerSecond = newPps;
      updateScrollBars();
      repaint();

      if (onZoomChanged)
        onZoomChanged(pixelsPerSecond);
    }
    return;
  }

  // Natural scrolling: use deltaX for horizontal, deltaY for vertical
  // On trackpads, horizontal swipe gives deltaX directly
  float deltaX = wheel.deltaX;
  float deltaY = wheel.deltaY;

  // Shift + scroll = force horizontal scroll (for mouse wheel users)
  if (e.mods.isShiftDown() && std::abs(deltaX) < 0.001f) {
    deltaX = deltaY;
    deltaY = 0.0f;
  }

  // Apply scrolling
  if (std::abs(deltaX) > 0.001f) {
    double newScrollX = scrollX - deltaX * scrollMultiplier;
    newScrollX = std::max(0.0, newScrollX);
    horizontalScrollBar.setCurrentRangeStart(newScrollX);
  }

  if (std::abs(deltaY) > 0.001f) {
    double newScrollY = scrollY - deltaY * scrollMultiplier;
    verticalScrollBar.setCurrentRangeStart(newScrollY);
  }
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent &e,
                                      float scaleFactor) {
  // Pinch-to-zoom on trackpad
  float mouseX = static_cast<float>(e.x - pianoKeysWidth);
  double timeAtMouse = xToTime(mouseX + static_cast<float>(scrollX));

  float newPps = pixelsPerSecond * scaleFactor;
  newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

  // Adjust scroll to keep mouse position stable
  float newMouseX = static_cast<float>(timeAtMouse * newPps);
  scrollX = std::max(0.0, static_cast<double>(newMouseX - mouseX));

  pixelsPerSecond = newPps;
  updateScrollBars();
  repaint();

  if (onZoomChanged)
    onZoomChanged(pixelsPerSecond);
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar *scrollBar,
                                        double newRangeStart) {
  if (scrollBar == &horizontalScrollBar) {
    scrollX = newRangeStart;

    // Notify scroll changed for synchronization
    if (onScrollChanged)
      onScrollChanged(scrollX);
  } else if (scrollBar == &verticalScrollBar) {
    scrollY = newRangeStart;
  }
  repaint();
}

void PianoRollComponent::setProject(Project *proj) {
  project = proj;
  updateScrollBars();
  repaint();
}

void PianoRollComponent::setCursorTime(double time) {
  cursorTime = time;
  repaint();
}

void PianoRollComponent::setPixelsPerSecond(float pps, bool centerOnCursor) {
  float oldPps = pixelsPerSecond;
  float newPps =
      juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, pps);

  if (std::abs(oldPps - newPps) < 0.01f)
    return; // No significant change

  if (centerOnCursor) {
    // Calculate cursor position relative to view
    float cursorX = static_cast<float>(cursorTime * oldPps);
    float cursorRelativeX = cursorX - static_cast<float>(scrollX);

    // Calculate new scroll position to keep cursor at same relative position
    float newCursorX = static_cast<float>(cursorTime * newPps);
    scrollX = static_cast<double>(newCursorX - cursorRelativeX);
    scrollX = std::max(0.0, scrollX);
  }

  pixelsPerSecond = newPps;
  updateScrollBars();
  repaint();

  // Don't call onZoomChanged here to avoid infinite recursion
  // The caller is responsible for synchronizing other components
}

void PianoRollComponent::setPixelsPerSemitone(float pps) {
  pixelsPerSemitone =
      juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, pps);
  updateScrollBars();
  repaint();
}

void PianoRollComponent::setScrollX(double x) {
  if (std::abs(scrollX - x) < 0.01)
    return; // No significant change

  scrollX = x;
  horizontalScrollBar.setCurrentRangeStart(x);

  // Don't call onScrollChanged here to avoid infinite recursion
  // The caller is responsible for synchronizing other components

  repaint();
}

void PianoRollComponent::centerOnPitchRange(float minMidi, float maxMidi) {
  // Calculate center MIDI note
  float centerMidi = (minMidi + maxMidi) / 2.0f;

  // Calculate Y position for center
  float centerY = midiToY(centerMidi);

  // Get visible height
  auto bounds = getLocalBounds();
  int visibleHeight = bounds.getHeight() - 8; // scrollbar height

  // Calculate scroll position to center the pitch range
  double newScrollY = centerY - visibleHeight / 2.0;

  // Clamp to valid range
  double totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
  newScrollY =
      juce::jlimit(0.0, std::max(0.0, totalHeight - visibleHeight), newScrollY);

  scrollY = newScrollY;
  verticalScrollBar.setCurrentRangeStart(newScrollY);
  repaint();
}

void PianoRollComponent::setEditMode(EditMode mode) {
  editMode = mode;

  // Change cursor based on mode
  if (mode == EditMode::Draw)
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
  else
    setMouseCursor(juce::MouseCursor::NormalCursor);

  repaint();
}

Note *PianoRollComponent::findNoteAt(float x, float y) {
  if (!project)
    return nullptr;

  for (auto &note : project->getNotes()) {
    float noteX = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
    float noteW = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
    float noteY = midiToY(note.getAdjustedMidiNote());
    float noteH = pixelsPerSemitone;

    if (x >= noteX && x < noteX + noteW && y >= noteY && y < noteY + noteH) {
      return &note;
    }
  }

  return nullptr;
}

void PianoRollComponent::updateScrollBars() {
  if (project) {
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

void PianoRollComponent::applyPitchDrawing(float x, float y) {
  if (!project)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  // Convert screen coordinates to time and MIDI
  double time = xToTime(x);
  float midi = yToMidi(y);

  // Convert MIDI to frequency
  float freq = midiToFreq(midi);

  // Convert time to frame index
  int frameIndex = static_cast<int>(secondsToFrames(static_cast<float>(time)));

  auto applyFrame = [&](int idx, float newFreq) {
    if (idx < 0 || idx >= static_cast<int>(audioData.f0.size()))
      return;

    const float oldF0 = audioData.f0[idx];
    const bool oldVoiced = (idx < static_cast<int>(audioData.voicedMask.size()))
                               ? audioData.voicedMask[idx]
                               : false;

    auto it = drawingEditIndexByFrame.find(idx);
    if (it == drawingEditIndexByFrame.end()) {
      drawingEditIndexByFrame.emplace(idx, drawingEdits.size());
      drawingEdits.push_back(F0FrameEdit{idx, oldF0, newFreq, oldVoiced, true});
    } else {
      auto &e = drawingEdits[it->second];
      e.newF0 = newFreq;
      e.newVoiced = true;
    }

    // Apply the change immediately
    audioData.f0[idx] = newFreq;
    if (idx < static_cast<int>(audioData.voicedMask.size()))
      audioData.voicedMask[idx] = true;
  };

  if (frameIndex >= 0 && frameIndex < static_cast<int>(audioData.f0.size())) {
    applyFrame(frameIndex, freq);

    // Interpolate between last draw position and current
    if (lastDrawX > 0 && lastDrawY > 0) {
      double lastTime = xToTime(lastDrawX);
      int lastFrame =
          static_cast<int>(secondsToFrames(static_cast<float>(lastTime)));
      float lastMidi = yToMidi(lastDrawY);
      float lastFreq = midiToFreq(lastMidi);

      // Interpolate intermediate frames
      int startFrame = std::min(lastFrame, frameIndex);
      int endFrame = std::max(lastFrame, frameIndex);

      for (int f = startFrame + 1; f < endFrame; ++f) {
        float t = static_cast<float>(f - startFrame) /
                  static_cast<float>(endFrame - startFrame);
        float interpFreq = lastFreq * (1.0f - t) + freq * t;
        applyFrame(f, interpFreq);
      }
    }

    lastDrawX = x;
    lastDrawY = y;
  }
}

void PianoRollComponent::commitPitchDrawing() {
  if (drawingEdits.empty())
    return;

  // Calculate the dirty frame range from the changes
  int minFrame = std::numeric_limits<int>::max();
  int maxFrame = std::numeric_limits<int>::min();
  for (const auto &e : drawingEdits) {
    minFrame = std::min(minFrame, e.idx);
    maxFrame = std::max(maxFrame, e.idx);
  }

  // Set F0 dirty range in project for incremental synthesis
  if (project && minFrame <= maxFrame) {
    project->setF0DirtyRange(minFrame, maxFrame);
  }

  // Create undo action
  if (undoManager && project) {
    auto &audioData = project->getAudioData();
    auto action = std::make_unique<F0EditAction>(
        &audioData.f0, &audioData.voicedMask, drawingEdits);
    undoManager->addAction(std::move(action));
  }

  drawingEdits.clear();
  drawingEditIndexByFrame.clear();
  lastDrawX = 0.0f;
  lastDrawY = 0.0f;

  // Trigger synthesis
  if (onPitchEditFinished)
    onPitchEditFinished();
}
