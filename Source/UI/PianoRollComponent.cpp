#include "PianoRollComponent.h"
#include "../Utils/BasePitchCurve.h"
#include "../Utils/Constants.h"
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

PianoRollComponent::~PianoRollComponent() {
  horizontalScrollBar.removeListener(this);
  verticalScrollBar.removeListener(this);
}

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

  // Draw scrolled content (grid, notes, pitch curves)
  {
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(mainArea);
    g.setOrigin(pianoKeysWidth - static_cast<int>(scrollX),
                timelineHeight - static_cast<int>(scrollY));

    drawGrid(g);
    drawNotes(g);
    drawPitchCurves(g);
  }

  // Draw timeline (above grid, scrolls horizontally)
  drawTimeline(g);

  // Draw unified cursor line (spans from timeline through grid)
  {
    float x = static_cast<float>(pianoKeysWidth) + timeToX(cursorTime) -
              static_cast<float>(scrollX);
    float cursorTop = 0.0f;
    float cursorBottom = static_cast<float>(getHeight() - 8); // Exclude scrollbar

    g.setColour(juce::Colours::white);
    g.fillRect(x - 0.5f, cursorTop, 1.0f, cursorBottom);
  }

  // Draw piano keys
  drawPianoKeys(g);
}

void PianoRollComponent::resized() {
  auto bounds = getLocalBounds();
  constexpr int scrollBarSize = 8;

  horizontalScrollBar.setBounds(
      pianoKeysWidth, bounds.getHeight() - scrollBarSize,
      bounds.getWidth() - pianoKeysWidth - scrollBarSize, scrollBarSize);

  verticalScrollBar.setBounds(
      bounds.getWidth() - scrollBarSize, timelineHeight, scrollBarSize,
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

  // Check if we can use cached waveform
  bool cacheValid = waveformCache.isValid() &&
                    std::abs(cachedScrollX - scrollX) < 1.0 &&
                    std::abs(cachedPixelsPerSecond - pixelsPerSecond) < 0.01f &&
                    cachedWidth == visibleArea.getWidth() &&
                    cachedHeight == visibleArea.getHeight();

  if (cacheValid) {
    g.drawImageAt(waveformCache, visibleArea.getX(), visibleArea.getY());
    return;
  }

  // Render waveform to cache
  waveformCache = juce::Image(juce::Image::ARGB, visibleArea.getWidth(),
                               visibleArea.getHeight(), true);
  juce::Graphics cacheGraphics(waveformCache);

  const float *samples = audioData.waveform.getReadPointer(0);
  int numSamples = audioData.waveform.getNumSamples();

  // Draw waveform filling the visible area height
  float visibleHeight = static_cast<float>(visibleArea.getHeight());
  float centerY = visibleHeight * 0.5f;
  float waveformHeight = visibleHeight * 0.8f;

  juce::Path waveformPath;
  int visibleWidth = visibleArea.getWidth();

  waveformPath.startNewSubPath(0.0f, centerY);

  // Draw only the visible portion
  for (int px = 0; px < visibleWidth; ++px) {
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
    waveformPath.lineTo(static_cast<float>(px), y);
  }

  // Bottom half (reverse)
  for (int px = visibleWidth - 1; px >= 0; --px) {
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
    waveformPath.lineTo(static_cast<float>(px), y);
  }

  waveformPath.closeSubPath();

  cacheGraphics.setColour(juce::Colour(COLOR_WAVEFORM));
  cacheGraphics.fillPath(waveformPath);

  // Update cache metadata
  cachedScrollX = scrollX;
  cachedPixelsPerSecond = pixelsPerSecond;
  cachedWidth = visibleArea.getWidth();
  cachedHeight = visibleArea.getHeight();

  // Draw cached image
  g.drawImageAt(waveformCache, visibleArea.getX(), visibleArea.getY());
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
      pianoKeysWidth, 0, getWidth() - pianoKeysWidth - scrollBarSize,
      timelineHeight);

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

  for (float time = 0.0f; time <= duration + secondsPerTick;
       time += secondsPerTick) {
    float x =
        pianoKeysWidth + time * pixelsPerSecond - static_cast<float>(scrollX);

    if (x < pianoKeysWidth - 50 || x > getWidth())
      continue;

    // Tick mark
    bool isMajor = std::fmod(time, secondsPerTick * 2.0f) < 0.001f;
    int tickHeight = isMajor ? 8 : 4;

    g.setColour(juce::Colour(COLOR_GRID_BAR));
    g.drawVerticalLine(static_cast<int>(x),
                       static_cast<float>(timelineHeight - tickHeight),
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

  // Calculate visible time range for culling
  double visibleStartTime = scrollX / pixelsPerSecond;
  double visibleEndTime = (scrollX + getWidth()) / pixelsPerSecond;

  for (auto &note : project->getNotes()) {
    // Skip rest notes (they have no pitch)
    if (note.isRest())
      continue;

    // Viewport culling: skip notes outside visible area
    double noteStartTime = framesToSeconds(note.getStartFrame());
    double noteEndTime = framesToSeconds(note.getEndFrame());
    if (noteEndTime < visibleStartTime || noteStartTime > visibleEndTime)
      continue;

    float x = static_cast<float>(noteStartTime * pixelsPerSecond);
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
      float waveHeight = h * 3.0f;

      // Build waveform data
      std::vector<float> waveValues;
      float step =
          std::max(1.0f, w / 400.0f); // Limit to ~400 points for smoothness

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
        float px1 =
            (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        float px2 =
            (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) * w;
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
        float px =
            (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        outline.lineTo(x + px, centerY - waveValues[i] * waveHeight * 0.5f);
      }
      for (int i = static_cast<int>(numPoints) - 1; i >= 0; --i) {
        float px =
            (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
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

  // Calculate visible frame range for culling
  double visibleStartTime = scrollX / pixelsPerSecond;
  double visibleEndTime = (scrollX + getWidth()) / pixelsPerSecond;
  int startFrame = std::max(0, static_cast<int>(visibleStartTime * audioData.sampleRate / HOP_SIZE));
  int endFrame = std::min(static_cast<int>(audioData.f0.size()),
                          static_cast<int>(visibleEndTime * audioData.sampleRate / HOP_SIZE) + 1);

  // Build adjusted F0 curve from notes' actual pitch
  std::vector<float> adjustedF0(audioData.f0.size(), 0.0f);
  float globalOffset = project->getGlobalPitchOffset();
  float globalRatio = std::pow(2.0f, globalOffset / 12.0f);

  const auto& notes = project->getNotes();

  // For each note, get its F0 values
  for (const auto& note : notes) {
    if (note.isRest())
      continue;

    int noteStart = note.getStartFrame();
    int noteEnd = note.getEndFrame();

    // If note has deltaPitch (has been dragged), use computed F0
    // Otherwise use original F0 from audioData
    if (note.hasDeltaPitch()) {
      // Get the note's adjusted F0 values (midiNote + pitchOffset + deltaPitch)
      std::vector<float> noteF0 = note.computeF0FromDelta();

      // Copy into the global F0 curve
      for (int i = 0; i < static_cast<int>(noteF0.size()); ++i) {
        int globalFrame = noteStart + i;
        if (globalFrame >= 0 && globalFrame < static_cast<int>(adjustedF0.size())) {
          float f0 = noteF0[i];
          // Apply global offset
          if (f0 > 0.0f && globalOffset != 0.0f)
            f0 *= globalRatio;
          adjustedF0[globalFrame] = f0;
        }
      }
    } else {
      // Use original F0 values with note's pitch offset
      float noteOffset = note.getPitchOffset();
      float noteRatio = (noteOffset != 0.0f) ? std::pow(2.0f, noteOffset / 12.0f) : 1.0f;

      for (int i = noteStart; i < noteEnd; ++i) {
        if (i >= 0 && i < static_cast<int>(audioData.f0.size())) {
          float f0 = audioData.f0[i];
          if (f0 > 0.0f) {
            // Apply note offset
            if (noteOffset != 0.0f)
              f0 *= noteRatio;
            // Apply global offset
            if (globalOffset != 0.0f)
              f0 *= globalRatio;
          }
          adjustedF0[i] = f0;
        }
      }
    }
  }

  // Extract visible portion
  std::vector<float> visibleF0;
  visibleF0.reserve(std::max(0, endFrame - startFrame));
  for (int i = startFrame; i < endFrame; ++i) {
    if (i >= 0 && i < static_cast<int>(adjustedF0.size()))
      visibleF0.push_back(adjustedF0[i]);
    else
      visibleF0.push_back(0.0f);
  }

  // Draw a single continuous pitch curve (only visible portion, smoothed)
  g.setColour(juce::Colour(COLOR_PITCH_CURVE));
  juce::Path path;
  bool pathStarted = false;
  int gapFrames = 0;
  const int maxGapFrames = 5; // Allow small gaps to keep curve connected

  for (int i = startFrame; i < endFrame; ++i) {
    int localIdx = i - startFrame;
    if (localIdx < 0 || localIdx >= static_cast<int>(visibleF0.size()))
      continue;

    float f0 = visibleF0[static_cast<size_t>(localIdx)];

    if (f0 > 0.0f && i < static_cast<int>(audioData.voicedMask.size()) &&
        audioData.voicedMask[i]) {
      // Already adjusted, just convert to MIDI for drawing
      float midi = freqToMidi(f0);
      float x = framesToSeconds(i) * pixelsPerSecond;
      float y = midiToY(midi) + pixelsPerSemitone * 0.5f;  // Center in row

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

  // Draw base pitch curve as dashed line for development/debugging
  // Use cached base pitch to avoid expensive recalculation on every repaint
  if constexpr (ENABLE_BASE_PITCH_DEBUG) {
    updateBasePitchCacheIfNeeded();
    
    if (!cachedBasePitch.empty()) {
        // Draw base pitch curve with dashed line
        g.setColour(juce::Colour(0xFF00FF00).withAlpha(0.6f));  // Green with transparency
        juce::Path basePath;
        bool basePathStarted = false;

        for (int i = startFrame; i < endFrame; ++i) {
          if (i >= 0 && i < static_cast<int>(cachedBasePitch.size())) {
            float baseMidi = cachedBasePitch[static_cast<size_t>(i)];
            if (baseMidi > 0.0f) {
              float x = framesToSeconds(i) * pixelsPerSecond;
              float y = midiToY(baseMidi) + pixelsPerSemitone * 0.5f;  // Center in row

              if (!basePathStarted) {
                basePath.startNewSubPath(x, y);
                basePathStarted = true;
              } else {
                basePath.lineTo(x, y);
              }
            } else if (basePathStarted) {
              // Break path at unvoiced regions - draw current segment before breaking
              juce::Path dashedPath;
              juce::PathStrokeType stroke(1.5f);
              const float dashLengths[] = {4.0f, 4.0f};  // 4px dash, 4px gap
              stroke.createDashedStroke(dashedPath, basePath, dashLengths, 2);
              g.strokePath(dashedPath, juce::PathStrokeType(1.5f));
              basePath.clear();
              basePathStarted = false;
            }
          }
        }

        if (basePathStarted) {
          // Use dashed stroke for base pitch curve
          juce::Path dashedPath;
          juce::PathStrokeType stroke(1.5f);
          const float dashLengths[] = {4.0f, 4.0f};  // 4px dash, 4px gap
          stroke.createDashedStroke(dashedPath, basePath, dashLengths, 2);
          g.strokePath(dashedPath, juce::PathStrokeType(1.5f));
        }
    }
  }
}

void PianoRollComponent::drawCursor(juce::Graphics &g) {
  float x = timeToX(cursorTime);
  float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  g.setColour(juce::Colours::white);
  g.fillRect(x - 0.5f, 0.0f, 1.0f, height);
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
    float y = midiToY(static_cast<float>(midi)) - static_cast<float>(scrollY) +
              timelineHeight;
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

  // Handle timeline clicks - seek to position
  if (e.y < timelineHeight && e.x >= pianoKeysWidth) {
    double time = xToTime(adjustedX);
    cursorTime = std::max(0.0, time);

    if (onSeek)
      onSeek(cursorTime);

    repaint();
    return;
  }

  // Ignore clicks outside main area
  if (e.y < timelineHeight || e.x < pianoKeysWidth)
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

    // Calculate delta pitch before starting drag
    // CRITICAL: Delta should be relative to FIXED note MIDI value, not smoothed base pitch
    // This ensures when we apply: newF0 = newNoteMidi + delta, it aligns perfectly
    if (project)
    {
        auto& audioData = project->getAudioData();
        int startFrame = note->getStartFrame();
        int endFrame = note->getEndFrame();
        int numFrames = endFrame - startFrame;

        std::vector<float> delta(numFrames, 0.0f);

        // CRITICAL: If note already has deltaPitch (from previous drag), use adjusted F0
        // Otherwise use original F0 from audioData
        std::vector<float> currentF0Values;
        if (note->hasDeltaPitch()) {
            // Note was previously dragged, use its adjusted F0 values
            currentF0Values = note->computeF0FromDelta();
        } else {
            // First time dragging, extract original F0 values for this note
            currentF0Values.resize(numFrames, 0.0f);
            int f0Size = static_cast<int>(audioData.f0.size());
            for (int i = 0; i < numFrames; ++i) {
                int globalFrame = startFrame + i;
                if (globalFrame >= 0 && globalFrame < f0Size) {
                    currentF0Values[i] = audioData.f0[globalFrame];
                }
            }
        }

        float fixedBaseMidi = note->getMidiNote();  // Use current note MIDI as reference

        for (int i = 0; i < numFrames; ++i)
        {
            if (currentF0Values[i] > 0.0f)
            {
                float f0Midi = freqToMidi(currentF0Values[i]);
                // Delta = actual F0 - fixed note MIDI value
                delta[i] = f0Midi - fixedBaseMidi;
            }
            else
            {
                // For unvoiced frames, set delta to 0 (will use note MIDI as base)
                delta[i] = 0.0f;
            }
        }
        note->setDeltaPitch(std::move(delta));
    }

    // Start dragging
    isDragging = true;
    draggedNote = note;
    dragStartY = static_cast<float>(e.y);
    originalPitchOffset = note->getPitchOffset();
    originalMidiNote = note->getMidiNote();

    // Save boundary F0 values and original F0 for undo
    if (project)
    {
        auto& audioData = project->getAudioData();
        int startFrame = note->getStartFrame();
        int endFrame = note->getEndFrame();
        int f0Size = static_cast<int>(audioData.f0.size());

        boundaryF0Start = (startFrame > 0 && startFrame - 1 < f0Size) ? audioData.f0[startFrame - 1] : 0.0f;
        boundaryF0End = (endFrame < f0Size) ? audioData.f0[endFrame] : 0.0f;

        // Save original F0 values for undo
        originalF0Values.clear();
        for (int i = startFrame; i < endFrame && i < f0Size; ++i)
            originalF0Values.push_back(audioData.f0[i]);
    }

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
  // Throttle repaints during drag to ~60fps max
  juce::int64 now = juce::Time::getMillisecondCounter();
  bool shouldRepaint = (now - lastDragRepaintTime) >= minDragRepaintInterval;

  if (editMode == EditMode::Draw && isDrawing) {
    float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
    float adjustedY = e.y - timelineHeight + static_cast<float>(scrollY);

    applyPitchDrawing(adjustedX, adjustedY);

    if (onPitchEdited)
      onPitchEdited();

    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  if (isDragging && draggedNote) {
    // Calculate pitch change from drag
    float deltaY = dragStartY - e.y;
    float deltaSemitones = deltaY / pixelsPerSemitone;

    // Update pitch offset (visual feedback during drag)
    // The actual F0 values will be updated in mouseUp
    draggedNote->setPitchOffset(deltaSemitones);
    draggedNote->markDirty();

    if (onPitchEdited)
      onPitchEdited();

    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
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
    int startFrame = draggedNote->getStartFrame();
    int endFrame = draggedNote->getEndFrame();

    // Update the note's midiNote and F0 values
    if (project) {
      auto& audioData = project->getAudioData();
      int f0Size = static_cast<int>(audioData.f0.size());

      // Apply visual offset even if tiny, so UI reflects final state consistently
      draggedNote->setPitchOffset(newOffset);

      // IMPORTANT: Update note's midiNote FIRST, then invalidate cache, then update F0
      // This ensures the cache uses the new midiNote value
      draggedNote->setMidiNote(originalMidiNote + newOffset);
      invalidateBasePitchCache();  // Invalidate cache so it will be regenerated with new note pitch
      
      // Find adjacent notes for boundary smoothing (needed for both delta and fallback paths)
      Note* prevNote = nullptr;
      Note* nextNote = nullptr;
      auto& notes = project->getNotes();  // Non-const reference to allow taking address
      for (auto& note : notes) {
        if (note.isRest()) continue;
        if (note.getEndFrame() <= startFrame && (!prevNote || note.getEndFrame() > prevNote->getEndFrame())) {
          prevNote = &note;
        }
        if (note.getStartFrame() >= endFrame && (!nextNote || note.getStartFrame() < nextNote->getStartFrame())) {
          nextNote = &note;
        }
      }
      
      if (std::abs(newOffset) > 0.001f) {
        // Update F0 values using delta pitch model with smoothed base pitch
        if (draggedNote->hasDeltaPitch()) {
        const auto& delta = draggedNote->getDeltaPitch();
        float newBaseMidi = originalMidiNote + newOffset;
        
        // Update base pitch cache (will regenerate with new note pitch)
        updateBasePitchCacheIfNeeded();
        
        // Apply base pitch + delta deviation
        // CRITICAL: Delta is relative to fixed note MIDI, so directly apply: newF0 = newNoteMidi + delta
        // This ensures F0 aligns perfectly with the note's MIDI value throughout the entire note range
        // CRITICAL: Ensure frame indices are properly aligned between calculation and application
        for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
            // CRITICAL: Calculate local index to match delta array indexing
            // delta[0] corresponds to startFrame, delta[1] to startFrame+1, etc.
            int localIdx = i - startFrame;
            
            // CRITICAL: Verify localIdx is within delta array bounds
            if (localIdx < 0 || localIdx >= static_cast<int>(delta.size()))
            {
                // Fallback: use note MIDI if delta index is out of bounds
                audioData.f0[i] = midiToFreq(newBaseMidi);
                continue;
            }
            
          float d = delta[localIdx];
          
          // Directly apply: newF0 = newNoteMidi + delta
          // Delta was calculated relative to original note MIDI, so this preserves the shape
          // while aligning to the new note MIDI value
          float targetMidi = newBaseMidi + d;
          audioData.f0[i] = midiToFreq(targetMidi);
          }

          // Set F0 dirty range for synthesis (extend to include smoothing region)
          int smoothStart = std::max(0, startFrame - 60);  // ~0.7s before for smoothing
          int smoothEnd = std::min(f0Size, endFrame + 60);  // ~0.7s after for smoothing
          project->setF0DirtyRange(smoothStart, smoothEnd);
        } else {
          // Fallback: apply uniform shift when no delta map
          float ratio = std::pow(2.0f, newOffset / 12.0f);
          for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
          float baseVal = (i < static_cast<int>(audioData.baseF0.size()) && audioData.baseF0[i] > 0.0f)
                              ? audioData.baseF0[i]
                              : audioData.f0[i];
          if (baseVal > 0.0f) {
            audioData.f0[i] = baseVal * ratio;
            audioData.voicedMask[i] = true;
          }
          }
          project->setF0DirtyRange(startFrame, endFrame);
        }
      }

      // Create undo action before updating note
      if (undoManager) {
        std::vector<F0FrameEdit> f0Edits;
        for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
          int localIdx = i - startFrame;
          F0FrameEdit edit;
          edit.idx = i;
          edit.oldF0 = (localIdx < static_cast<int>(originalF0Values.size())) ? originalF0Values[localIdx] : 0.0f;
          edit.newF0 = audioData.f0[i];
          f0Edits.push_back(edit);
        }
        auto action = std::make_unique<NotePitchDragAction>(
            draggedNote, &audioData.f0,
            originalMidiNote, originalMidiNote + newOffset,
            std::move(f0Edits),
            [this](Note* n) { reapplyBasePitchForNote(n); });  // Callback to recalculate F0 after undo/redo
        undoManager->addAction(std::move(action));
      }

      // Note: midiNote was already updated above before cache invalidation
      // Reset offset since base note was updated
      draggedNote->setPitchOffset(0.0f);

      // Improved boundary smoothing: find UV regions or silence for seamless transitions
      // This avoids phase discontinuities by splicing at unvoiced regions
      auto findNearestUVRegion = [&](int centerFrame, int searchRange) -> int {
        // Search for unvoiced (UV) region or silence near the boundary
        for (int offset = 0; offset <= searchRange; ++offset) {
          // Check both directions
          for (int dir = -1; dir <= 1; dir += 2) {
            int frame = centerFrame + dir * offset;
            if (frame >= 0 && frame < f0Size) {
              // Check if this is an unvoiced frame (F0 == 0 or voicedMask == false)
              bool isUnvoiced = (audioData.f0[frame] <= 0.0f);
              if (!isUnvoiced && frame < static_cast<int>(audioData.voicedMask.size())) {
                isUnvoiced = !audioData.voicedMask[frame];
              }
              
              // Also check for silence (very low amplitude)
              if (isUnvoiced) {
                return frame;
              }
            }
          }
        }
        return -1;  // No UV region found
      };

      // Smooth boundary with crossfade, preferring UV regions or adjacent notes
      // Capture prevNote and nextNote by reference (they're in the same scope)
      auto smoothBoundaryAtUV = [&](int noteBoundary, bool isStart) {
        const int maxSearchRange = 20;  // Search up to 20 frames for UV region
        const int maxCrossfadeFrames = 20;  // Increased for smoother transitions
        
        // Find adjacent note for smooth transition
        Note* adjacentNote = isStart ? prevNote : nextNote;
        float adjacentNoteMidi = 0.0f;
        int adjacentNoteStart = -1, adjacentNoteEnd = -1;
        if (adjacentNote) {
          adjacentNoteMidi = adjacentNote->getMidiNote();
          adjacentNoteStart = adjacentNote->getStartFrame();
          adjacentNoteEnd = adjacentNote->getEndFrame();
        }
        
        // Find nearest UV region
        int uvFrame = findNearestUVRegion(noteBoundary, maxSearchRange);
        
        // Determine target F0 for smooth transition
        float targetF0 = 0.0f;
        bool useAdjacentNote = false;
        
        if (adjacentNote && adjacentNoteStart >= 0 && adjacentNoteEnd > adjacentNoteStart) {
          // Check if adjacent note has F0 values at boundary
          int boundaryFrame = isStart ? (startFrame - 1) : endFrame;
          if (boundaryFrame >= 0 && boundaryFrame < f0Size && audioData.f0[boundaryFrame] > 0.0f) {
            targetF0 = audioData.f0[boundaryFrame];
            useAdjacentNote = true;
          } else {
            // Use adjacent note's MIDI value
            targetF0 = midiToFreq(adjacentNoteMidi);
            useAdjacentNote = true;
          }
        }
        
        if (uvFrame >= 0) {
          // Found UV region: use it as splice point with crossfade
          int crossfadeStart, crossfadeEnd;
          if (isStart) {
            // At note start: crossfade from UV region or adjacent note into note
            crossfadeStart = std::max(0, (uvFrame >= 0 ? uvFrame : noteBoundary) - maxCrossfadeFrames);
            crossfadeEnd = std::min(f0Size, noteBoundary + maxCrossfadeFrames);
          } else {
            // At note end: crossfade from note into UV region or adjacent note
            crossfadeStart = std::max(0, noteBoundary - maxCrossfadeFrames);
            crossfadeEnd = std::min(f0Size, (uvFrame >= 0 ? uvFrame : noteBoundary) + maxCrossfadeFrames);
          }
          
          // Limit crossfade to transition region only (not inside adjacent note)
          if (adjacentNote && adjacentNoteStart >= 0 && adjacentNoteEnd > adjacentNoteStart) {
            if (isStart) {
              // At start: don't modify inside previous note
              crossfadeStart = std::max(crossfadeStart, adjacentNoteEnd);
            } else {
              // At end: don't modify inside next note
              crossfadeEnd = std::min(crossfadeEnd, adjacentNoteStart);
            }
          }
          
          // Apply crossfade: blend original F0 (outside note) with new F0 (inside note)
          for (int i = crossfadeStart; i < crossfadeEnd && i < f0Size; ++i) {
            bool isInsideNote = (i >= startFrame && i < endFrame);
            bool isInCrossfadeRegion = (i >= crossfadeStart && i < crossfadeEnd);
            
            // Don't modify inside adjacent note
            bool isInsideAdjacentNote = false;
            if (adjacentNote && adjacentNoteStart >= 0 && adjacentNoteEnd > adjacentNoteStart) {
              isInsideAdjacentNote = (i >= adjacentNoteStart && i < adjacentNoteEnd);
            }
            
            if (isInCrossfadeRegion && !isInsideAdjacentNote) {
              // Calculate crossfade weight (0 = original/adjacent, 1 = new)
              float t;
              if (isStart) {
                t = static_cast<float>(i - crossfadeStart) / (crossfadeEnd - crossfadeStart);
              } else {
                t = static_cast<float>(crossfadeEnd - i) / (crossfadeEnd - crossfadeStart);
              }
              t = std::clamp(t, 0.0f, 1.0f);
              
              // Use smooth curve (ease-in-out)
              t = t * t * (3.0f - 2.0f * t);
              
              // Get original/target F0 value (before edit or from adjacent note)
              float originalF0 = 0.0f;
              if (isInsideNote) {
                int localIdx = i - startFrame;
                if (localIdx >= 0 && localIdx < static_cast<int>(originalF0Values.size())) {
                  originalF0 = originalF0Values[localIdx];
                }
              } else {
                // Outside note: use current F0 or adjacent note's F0
                if (useAdjacentNote && targetF0 > 0.0f) {
                  originalF0 = targetF0;
                } else if (i >= 0 && i < f0Size) {
                  originalF0 = audioData.f0[i];
                }
              }
              
              // Blend original/target and new F0
              float newF0 = isInsideNote ? audioData.f0[i] : originalF0;
              if (isInsideNote) {
                // Inside note: blend from original to new
                if (originalF0 > 0.0f && newF0 > 0.0f) {
                  audioData.f0[i] = originalF0 * (1.0f - t) + newF0 * t;
                } else if (newF0 > 0.0f) {
                  audioData.f0[i] = newF0 * t;  // Fade in new F0
                }
              } else {
                // Outside note: blend from adjacent/target to current
                if (originalF0 > 0.0f && newF0 > 0.0f) {
                  audioData.f0[i] = originalF0 * (1.0f - t) + newF0 * t;
                }
              }
            }
          }
        } else if (useAdjacentNote && targetF0 > 0.0f) {
          // No UV region but have adjacent note: smooth transition to adjacent note
          // IMPORTANT: Only modify transition region, not inside adjacent note
          int crossfadeFrames = maxCrossfadeFrames;
          int crossfadeStart, crossfadeEnd;
          if (isStart) {
            crossfadeStart = std::max(0, noteBoundary - crossfadeFrames);
            crossfadeEnd = std::min(f0Size, noteBoundary + crossfadeFrames);
          } else {
            crossfadeStart = std::max(0, noteBoundary - crossfadeFrames);
            crossfadeEnd = std::min(f0Size, noteBoundary + crossfadeFrames);
          }
          
          // Limit crossfade to transition region only (not inside adjacent note)
          if (adjacentNote && adjacentNoteStart >= 0 && adjacentNoteEnd > adjacentNoteStart) {
            if (isStart) {
              // At start: don't modify inside previous note
              crossfadeStart = std::max(crossfadeStart, adjacentNoteEnd);
            } else {
              // At end: don't modify inside next note
              crossfadeEnd = std::min(crossfadeEnd, adjacentNoteStart);
            }
          }
          
          for (int i = crossfadeStart; i < crossfadeEnd && i < f0Size; ++i) {
            bool isInsideNote = (i >= startFrame && i < endFrame);
            if (isInsideNote) continue;  // Don't modify inside current note
            
            // Don't modify inside adjacent note either
            if (adjacentNote && adjacentNoteStart >= 0 && adjacentNoteEnd > adjacentNoteStart) {
              if (i >= adjacentNoteStart && i < adjacentNoteEnd) {
                continue;  // Skip adjacent note's internal frames
              }
            }
            
            // Calculate blend weight
            float t;
            if (isStart) {
              t = static_cast<float>(i - crossfadeStart) / (crossfadeEnd - crossfadeStart);
            } else {
              t = static_cast<float>(crossfadeEnd - i) / (crossfadeEnd - crossfadeStart);
            }
            t = std::clamp(t, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t);  // Smooth curve
            
            // Blend from adjacent note's F0 to current F0 (only in transition region)
            float currentF0 = audioData.f0[i];
            if (targetF0 > 0.0f && currentF0 > 0.0f) {
              audioData.f0[i] = targetF0 * (1.0f - t) + currentF0 * t;
            } else if (targetF0 > 0.0f) {
              audioData.f0[i] = targetF0;
            }
          }
        } else {
          // No UV region found: use extended Gaussian smoothing as fallback
          const int smoothRadius = 10;  // Larger smoothing window
          if (noteBoundary < smoothRadius || noteBoundary >= f0Size - smoothRadius)
            return;
          
          // Extended Gaussian smoothing
          constexpr int kernelSize = 21;  // -10 to +10
          constexpr float sigma = 3.0f;
          float weights[kernelSize];
          float weightSum = 0.0f;
          
          for (int i = 0; i < kernelSize; ++i) {
            float x = static_cast<float>(i - kernelSize / 2);
            weights[i] = std::exp(-0.5f * x * x / (sigma * sigma));
            weightSum += weights[i];
          }
          
          // Normalize weights
          for (int i = 0; i < kernelSize; ++i) {
            weights[i] /= weightSum;
          }
          
          // Apply smoothing
          std::vector<float> smoothed(kernelSize);
          for (int i = 0; i < kernelSize; ++i) {
            int idx = noteBoundary + i - kernelSize / 2;
            smoothed[i] = (idx >= 0 && idx < f0Size) ? audioData.f0[idx] : 0.0f;
          }
          
          for (int i = 0; i < kernelSize; ++i) {
            int idx = noteBoundary + i - kernelSize / 2;
            if (idx >= 0 && idx < f0Size) {
              float sum = 0.0f;
              for (int j = 0; j < kernelSize; ++j) {
                if (smoothed[j] > 0.0f) {
                  sum += smoothed[j] * weights[j];
                }
              }
              if (sum > 0.0f) {
                audioData.f0[idx] = sum;
              }
            }
          }
        }
      };

      // Apply improved boundary smoothing
      smoothBoundaryAtUV(startFrame, true);   // Start boundary
      smoothBoundaryAtUV(endFrame, false);    // End boundary
      
      // Smooth interpolation in gaps between notes to avoid sharp corners
      // Check gap between current note and next note
      if (nextNote && nextNote->getStartFrame() > endFrame) {
        int gapStart = endFrame;
        int gapEnd = nextNote->getStartFrame();
        int gapSize = gapEnd - gapStart;
        
        if (gapSize > 0 && gapSize < 100) {  // Only smooth small gaps (up to 100 frames)
          // Get F0 values at gap boundaries
          float f0AtGapStart = 0.0f;
          float f0AtGapEnd = 0.0f;
          
          if (gapStart > 0 && gapStart - 1 < f0Size && audioData.f0[gapStart - 1] > 0.0f) {
            f0AtGapStart = audioData.f0[gapStart - 1];
          } else if (gapStart < f0Size && audioData.f0[gapStart] > 0.0f) {
            f0AtGapStart = audioData.f0[gapStart];
          }
          
          if (gapEnd < f0Size && audioData.f0[gapEnd] > 0.0f) {
            f0AtGapEnd = audioData.f0[gapEnd];
          } else if (gapEnd > 0 && gapEnd - 1 < f0Size && audioData.f0[gapEnd - 1] > 0.0f) {
            f0AtGapEnd = audioData.f0[gapEnd - 1];
          }
          
          // If both boundaries have F0 values, interpolate smoothly
          if (f0AtGapStart > 0.0f && f0AtGapEnd > 0.0f) {
            for (int i = gapStart; i < gapEnd && i < f0Size; ++i) {
              float t = static_cast<float>(i - gapStart) / gapSize;
              t = std::clamp(t, 0.0f, 1.0f);
              // Use smooth curve (ease-in-out) for natural interpolation
              t = t * t * (3.0f - 2.0f * t);
              
              // Linear interpolation in frequency domain (more natural for pitch)
              float interpolatedF0 = f0AtGapStart * (1.0f - t) + f0AtGapEnd * t;
              
              // Only set if current value is 0 or significantly different
              if (audioData.f0[i] <= 0.0f || std::abs(audioData.f0[i] - interpolatedF0) > 1.0f) {
                audioData.f0[i] = interpolatedF0;
                if (i < static_cast<int>(audioData.voicedMask.size())) {
                  audioData.voicedMask[i] = true;
                }
              }
            }
          }
        }
      }
      
      // Check gap between previous note and current note
      if (prevNote && prevNote->getEndFrame() < startFrame) {
        int gapStart = prevNote->getEndFrame();
        int gapEnd = startFrame;
        int gapSize = gapEnd - gapStart;
        
        if (gapSize > 0 && gapSize < 100) {  // Only smooth small gaps (up to 100 frames)
          // Get F0 values at gap boundaries
          float f0AtGapStart = 0.0f;
          float f0AtGapEnd = 0.0f;
          
          if (gapStart < f0Size && audioData.f0[gapStart] > 0.0f) {
            f0AtGapStart = audioData.f0[gapStart];
          } else if (gapStart > 0 && gapStart - 1 < f0Size && audioData.f0[gapStart - 1] > 0.0f) {
            f0AtGapStart = audioData.f0[gapStart - 1];
          }
          
          if (gapEnd < f0Size && audioData.f0[gapEnd] > 0.0f) {
            f0AtGapEnd = audioData.f0[gapEnd];
          } else if (gapEnd > 0 && gapEnd - 1 < f0Size && audioData.f0[gapEnd - 1] > 0.0f) {
            f0AtGapEnd = audioData.f0[gapEnd - 1];
          }
          
          // If both boundaries have F0 values, interpolate smoothly
          if (f0AtGapStart > 0.0f && f0AtGapEnd > 0.0f) {
            for (int i = gapStart; i < gapEnd && i < f0Size; ++i) {
              float t = static_cast<float>(i - gapStart) / gapSize;
              t = std::clamp(t, 0.0f, 1.0f);
              // Use smooth curve (ease-in-out) for natural interpolation
              t = t * t * (3.0f - 2.0f * t);
              
              // Linear interpolation in frequency domain (more natural for pitch)
              float interpolatedF0 = f0AtGapStart * (1.0f - t) + f0AtGapEnd * t;
              
              // Only set if current value is 0 or significantly different
              if (audioData.f0[i] <= 0.0f || std::abs(audioData.f0[i] - interpolatedF0) > 1.0f) {
                audioData.f0[i] = interpolatedF0;
                if (i < static_cast<int>(audioData.voicedMask.size())) {
                  audioData.voicedMask[i] = true;
                }
              }
            }
          }
        }
      }
    }

    // Reset pitchOffset to 0 since we've already incorporated it into midiNote
    draggedNote->setPitchOffset(0.0f);

    // Trigger UI update + incremental synthesis when pitch edit is finished
    if (onPitchEdited)
      onPitchEdited();   // immediate repaint/update of pitch curves
    repaint();
    if (onPitchEditFinished)
      onPitchEditFinished();  // downstream synthesis
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
  float scrollMultiplier = wheel.isSmooth ? 200.0f : 80.0f;

  bool isOverPianoKeys = e.x < pianoKeysWidth;
  bool isOverTimeline = e.y < timelineHeight;

  // Hover-based zoom (no modifier keys needed)
  if (!e.mods.isCommandDown() && !e.mods.isCtrlDown()) {
    // Over piano keys: vertical zoom
    if (isOverPianoKeys) {
      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSemitone * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, newPps);
      pixelsPerSemitone = newPps;
      updateScrollBars();
      repaint();
      return;
    }

    // Over timeline: horizontal zoom
    if (isOverTimeline) {
      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSecond * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);
      pixelsPerSecond = newPps;
      updateScrollBars();
      repaint();
      if (onZoomChanged)
        onZoomChanged(pixelsPerSecond);
      return;
    }

    // Normal scrolling in grid area
    float deltaX = wheel.deltaX;
    float deltaY = wheel.deltaY;

    if (e.mods.isShiftDown() && std::abs(deltaX) < 0.001f) {
      deltaX = deltaY;
      deltaY = 0.0f;
    }

    if (std::abs(deltaX) > 0.001f) {
      double newScrollX = scrollX - deltaX * scrollMultiplier;
      newScrollX = std::max(0.0, newScrollX);
      horizontalScrollBar.setCurrentRangeStart(newScrollX);
    }

    if (std::abs(deltaY) > 0.001f) {
      double newScrollY = scrollY - deltaY * scrollMultiplier;
      verticalScrollBar.setCurrentRangeStart(newScrollY);
    }
    return;
  }

  // Key-based zoom in grid area
  if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
    float zoomFactor = 1.0f + wheel.deltaY * 0.3f;

    if (e.mods.isShiftDown()) {
      // Vertical zoom - center on mouse position
      float mouseY = static_cast<float>(e.y - timelineHeight);
      float midiAtMouse = yToMidi(mouseY + static_cast<float>(scrollY));

      float newPps = pixelsPerSemitone * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, newPps);

      // Adjust scroll to keep mouse position stable
      float newMouseY = midiToY(midiAtMouse);
      scrollY = std::max(0.0, static_cast<double>(newMouseY - mouseY));

      pixelsPerSemitone = newPps;
      updateScrollBars();
      repaint();
    } else {
      // Horizontal zoom - center on mouse position
      float mouseX = static_cast<float>(e.x - pianoKeysWidth);
      double timeAtMouse = xToTime(mouseX + static_cast<float>(scrollX));

      float newPps = pixelsPerSecond * zoomFactor;
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
  }
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent &e,
                                      float scaleFactor) {
  // Pinch-to-zoom on trackpad - horizontal zoom, center on mouse position
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
  invalidateBasePitchCache();  // Clear cache when project changes
  updateScrollBars();
  repaint();
}

void PianoRollComponent::setCursorTime(double time) {
  if (std::abs(cursorTime - time) < 0.0001) return;  // Skip if no change

  // Calculate dirty rectangles for old and new cursor positions
  auto getCursorRect = [this](double t) -> juce::Rectangle<int> {
    float x = static_cast<float>(t * pixelsPerSecond - scrollX) + pianoKeysWidth;
    int width = 3;  // Cursor width + 1px margin
    return juce::Rectangle<int>(static_cast<int>(x - 1), timelineHeight,
                                 width, getHeight() - timelineHeight);
  };

  // Repaint old cursor position
  if (lastCursorTime >= 0.0) {
    repaint(getCursorRect(lastCursorTime));
  }

  lastCursorTime = cursorTime;
  cursorTime = time;

  // Repaint new cursor position
  repaint(getCursorRect(cursorTime));
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
  if (mode == EditMode::Draw) {
    // Create a custom pen cursor
    // Simple pen icon: 16x16 pixels with pen tip at bottom-left
    juce::Image penImage(juce::Image::ARGB, 16, 16, true);
    juce::Graphics g(penImage);

    // Draw a simple pen shape
    g.setColour(juce::Colours::white);
    // Pen body (diagonal line from top-right to bottom-left)
    g.drawLine(12.0f, 2.0f, 2.0f, 12.0f, 2.0f);
    // Pen tip (small triangle at bottom-left)
    juce::Path tip;
    tip.addTriangle(0.0f, 14.0f, 4.0f, 10.0f, 2.0f, 12.0f);
    g.fillPath(tip);

    // Set hotspot at pen tip (bottom-left corner)
    setMouseCursor(juce::MouseCursor(penImage, 0, 14));
  } else {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }

  repaint();
}

Note *PianoRollComponent::findNoteAt(float x, float y) {
  if (!project)
    return nullptr;

  for (auto &note : project->getNotes()) {
    // Skip rest notes
    if (note.isRest())
      continue;

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

void PianoRollComponent::updateBasePitchCacheIfNeeded() {
  if (!project) {
    cachedBasePitch.clear();
    cachedNoteCount = 0;
    cachedTotalFrames = 0;
    return;
  }

  const auto& notes = project->getNotes();
  const auto& audioData = project->getAudioData();
  int totalFrames = static_cast<int>(audioData.f0.size());

  // Check if cache is valid
  size_t currentNoteCount = 0;
  for (const auto& note : notes) {
    if (!note.isRest()) {
      currentNoteCount++;
    }
  }

  // Invalidate cache if notes changed or total frames changed or explicitly invalidated
  // For performance, we only check note count and total frames
  // A more precise check would compare note positions/pitches, but that's expensive
  if (cacheInvalidated || cachedNoteCount != currentNoteCount || cachedTotalFrames != totalFrames || cachedBasePitch.empty()) {
    // Only regenerate if we have notes and frames
    if (currentNoteCount > 0 && totalFrames > 0) {
      // Collect all notes
      std::vector<BasePitchCurve::NoteSegment> noteSegments;
      noteSegments.reserve(currentNoteCount);
      for (const auto& note : notes) {
        if (!note.isRest()) {
          noteSegments.push_back({
            note.getStartFrame(),
            note.getEndFrame(),
            note.getMidiNote()
          });
        }
      }

      if (!noteSegments.empty()) {
        // Generate smoothed base pitch curve (expensive operation, cached)
        // This is only called when notes change, not on every repaint
        cachedBasePitch = BasePitchCurve::generateForNotes(noteSegments, totalFrames);
        cachedNoteCount = currentNoteCount;
        cachedTotalFrames = totalFrames;
        cacheInvalidated = false;  // Mark cache as valid
      } else {
        cachedBasePitch.clear();
        cachedNoteCount = 0;
        cachedTotalFrames = 0;
        cacheInvalidated = false;  // Mark as processed (even if empty)
      }
    } else {
      cachedBasePitch.clear();
      cachedNoteCount = 0;
      cachedTotalFrames = 0;
      cacheInvalidated = false;  // Mark as processed (even if empty)
    }
  }
}

void PianoRollComponent::reapplyBasePitchForNote(Note* note) {
  if (!note || !project) return;
  
  auto& audioData = project->getAudioData();
  int startFrame = note->getStartFrame();
  int endFrame = note->getEndFrame();
  int f0Size = static_cast<int>(audioData.f0.size());
  
  // Invalidate base pitch cache since note MIDI changed
  invalidateBasePitchCache();
  updateBasePitchCacheIfNeeded();
  
  // Reapply base pitch + delta to F0
  // CRITICAL: Delta is relative to fixed note MIDI, so directly apply: newF0 = noteMidi + delta
  if (note->hasDeltaPitch()) {
    const auto& delta = note->getDeltaPitch();
    float noteMidi = note->getMidiNote();
    
    for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
      int localIdx = i - startFrame;
      float d = (localIdx < static_cast<int>(delta.size())) ? delta[localIdx] : 0.0f;
      
      // Directly apply: newF0 = noteMidi + delta
      // Delta was calculated relative to note MIDI, so this preserves the shape
      // while aligning to the note MIDI value
      float targetMidi = noteMidi + d;
      audioData.f0[i] = midiToFreq(targetMidi);
    }
    
    // Set F0 dirty range for synthesis
    int smoothStart = std::max(0, startFrame - 60);
    int smoothEnd = std::min(f0Size, endFrame + 60);
    project->setF0DirtyRange(smoothStart, smoothEnd);
  }
  
  // Trigger repaint
  repaint();
}

void PianoRollComponent::applyPitchDrawing(float x, float y) {
  if (!project)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  // Convert screen coordinates to time and MIDI
  double time = xToTime(x);
  // Account for the centering offset used when drawing the pitch curve
  float midi = yToMidi(y - pixelsPerSemitone * 0.5f);

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

      // Clear deltaPitch for any note containing this frame so changes are visible immediately
      auto& notes = project->getNotes();
      for (auto& note : notes) {
        if (note.getStartFrame() <= idx && note.getEndFrame() > idx && note.hasDeltaPitch()) {
          note.setDeltaPitch(std::vector<float>());
          break;
        }
      }
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

  // Clear deltaPitch for notes in the edited range so they use the drawn F0 values
  if (project && minFrame <= maxFrame) {
    auto& notes = project->getNotes();
    for (auto& note : notes) {
      // Check if note overlaps with edited range
      if (note.getEndFrame() > minFrame && note.getStartFrame() < maxFrame) {
        // Clear deltaPitch so the note will use audioData.f0 instead of computed values
        if (note.hasDeltaPitch()) {
          note.setDeltaPitch(std::vector<float>());
        }
      }
    }
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
