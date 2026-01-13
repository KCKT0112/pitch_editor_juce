#include "PianoRollComponent.h"
#include "../Utils/BasePitchCurve.h"
#include "../Utils/Constants.h"
#include "../Utils/PitchCurveProcessor.h"
#include <cmath>
#include <limits>

PianoRollComponent::PianoRollComponent() {
  // Initialize modular components
  coordMapper = std::make_unique<CoordinateMapper>();
  renderer = std::make_unique<PianoRollRenderer>();
  scrollZoomController = std::make_unique<ScrollZoomController>();
  pitchEditor = std::make_unique<PitchEditor>();

  // Wire up components
  renderer->setCoordinateMapper(coordMapper.get());
  scrollZoomController->setCoordinateMapper(coordMapper.get());
  pitchEditor->setCoordinateMapper(coordMapper.get());

  // Setup scrollZoomController callbacks
  scrollZoomController->onRepaintNeeded = [this]() { repaint(); };
  scrollZoomController->onZoomChanged = [this](float pps) {
    if (onZoomChanged) onZoomChanged(pps);
  };
  scrollZoomController->onScrollChanged = [this](double x) {
    if (onScrollChanged) onScrollChanged(x);
  };

  // Setup pitchEditor callbacks
  pitchEditor->onNoteSelected = [this](Note* note) {
    if (onNoteSelected) onNoteSelected(note);
  };
  pitchEditor->onPitchEdited = [this]() {
    repaint();
    if (onPitchEdited) onPitchEdited();
  };
  pitchEditor->onPitchEditFinished = [this]() {
    if (onPitchEditFinished) onPitchEditFinished();
  };
  pitchEditor->onBasePitchCacheInvalidated = [this]() {
    invalidateBasePitchCache();
  };

  addAndMakeVisible(horizontalScrollBar);
  addAndMakeVisible(verticalScrollBar);

  // Use scrollZoomController's scrollbars
  addAndMakeVisible(scrollZoomController->getHorizontalScrollBar());
  addAndMakeVisible(scrollZoomController->getVerticalScrollBar());

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
    float cursorBottom =
        static_cast<float>(getHeight() - 8); // Exclude scrollbar

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

    // Position at grid cell center for MIDI note, then offset by pitch
    // adjustment
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

      // Build waveform data with increased resolution for smoother curves
      std::vector<float> waveValues;
      // Increase point density for smoother curves (up to 800 points)
      float step = std::max(0.5f, w / 1024.0f);

      for (float px = 0; px <= w; px += step) {
        int sampleIdx =
            startSample + static_cast<int>((px / w) * numNoteSamples);
        int sampleEnd = std::min(sampleIdx + samplesPerPixel, endSample);

        float maxVal = 0.0f;
        for (int i = sampleIdx; i < sampleEnd; ++i)
          maxVal = std::max(maxVal, std::abs(samples[i]));

        waveValues.push_back(maxVal);
      }

      // Apply smoothing filter to reduce aliasing artifacts
      if (waveValues.size() > 2) {
        std::vector<float> smoothed(waveValues.size());
        smoothed[0] = waveValues[0];
        for (size_t i = 1; i + 1 < waveValues.size(); ++i) {
          // Simple 3-point moving average for gentle smoothing
          smoothed[i] = (waveValues[i - 1] * 0.25f + waveValues[i] * 0.5f +
                         waveValues[i + 1] * 0.25f);
        }
        smoothed[waveValues.size() - 1] = waveValues[waveValues.size() - 1];
        waveValues = std::move(smoothed);
      }

      size_t numPoints = waveValues.size();
      if (numPoints < 2) {
        // Fallback for very short notes
        g.setColour(noteColor.withAlpha(0.85f));
        g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
      } else {
        // Helper function for Catmull-Rom spline interpolation
        auto catmullRom = [](float t, float p0, float p1, float p2,
                             float p3) -> float {
          // Catmull-Rom spline: smooth interpolation between p1 and p2
          float t2 = t * t;
          float t3 = t2 * t;
          return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                         (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                         (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        };

        // Draw filled waveform using smooth curves
        g.setColour(noteColor.withAlpha(0.85f));
        juce::Path waveformPath;

        // Build top curve with Catmull-Rom spline
        waveformPath.startNewSubPath(x, centerY -
                                            waveValues[0] * waveHeight * 0.5f);

        // Use cubic curves for smooth interpolation
        const int curveSegments = 4; // Interpolate 4 points between each pair
        for (size_t i = 0; i + 1 < numPoints; ++i) {
          float px1 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
          float px2 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;

          // Get control points for spline
          size_t idx0 = (i > 0) ? i - 1 : i;
          size_t idx1 = i;
          size_t idx2 = i + 1;
          size_t idx3 = (i + 2 < numPoints) ? i + 2 : i + 1;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          // Draw smooth curve segment
          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY - val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
          }
        }

        // Build bottom curve (mirror of top)
        waveformPath.lineTo(x + w, centerY + waveValues[numPoints - 1] *
                                                 waveHeight * 0.5f);

        for (int i = static_cast<int>(numPoints) - 2; i >= 0; --i) {
          float px1 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;
          float px2 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;

          size_t idx0 = (i + 2 < numPoints) ? i + 2 : i + 1;
          size_t idx1 = i + 1;
          size_t idx2 = i;
          size_t idx3 = (i > 0) ? i - 1 : i;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY + val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
          }
        }

        waveformPath.closeSubPath();
        g.fillPath(waveformPath);

        // Draw smooth outline with anti-aliasing
        juce::Path outline;
        outline.startNewSubPath(x, centerY - waveValues[0] * waveHeight * 0.5f);

        // Top curve
        for (size_t i = 0; i + 1 < numPoints; ++i) {
          float px1 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
          float px2 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;

          size_t idx0 = (i > 0) ? i - 1 : i;
          size_t idx1 = i;
          size_t idx2 = i + 1;
          size_t idx3 = (i + 2 < numPoints) ? i + 2 : i + 1;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY - val * waveHeight * 0.5f;
            outline.lineTo(x + px, yPos);
          }
        }

        // Bottom curve
        for (int i = static_cast<int>(numPoints) - 2; i >= 0; --i) {
          float px1 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;
          float px2 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;

          size_t idx0 = (i + 2 < numPoints) ? i + 2 : i + 1;
          size_t idx1 = i + 1;
          size_t idx2 = i;
          size_t idx3 = (i > 0) ? i - 1 : i;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY + val * waveHeight * 0.5f;
            outline.lineTo(x + px, yPos);
          }
        }

        outline.closeSubPath();
        g.setColour(noteColor.brighter(0.2f));
        // Use slightly thicker stroke with anti-aliasing for smoother
        // appearance
        g.strokePath(outline,
                     juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
      }
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

  // Get global pitch offset (applied to display only)
  float globalOffset = project->getGlobalPitchOffset();

  // Draw pitch curves per note with their pitch offsets applied
  g.setColour(juce::Colour(COLOR_PITCH_CURVE));

  for (const auto &note : project->getNotes()) {
    if (note.isRest())
      continue;

    juce::Path path;
    bool pathStarted = false;

    int startFrame = note.getStartFrame();
    int endFrame =
        std::min(note.getEndFrame(), static_cast<int>(audioData.f0.size()));

    for (int i = startFrame; i < endFrame; ++i) {
      // Base pitch: during drag, add pitchOffset to simulate the new base pitch
      // This gives real-time preview of how the curve will look after drag completes
      float baseMidi =
          (i < static_cast<int>(audioData.basePitch.size()))
              ? audioData.basePitch[static_cast<size_t>(i)] + note.getPitchOffset()
              : ((i < static_cast<int>(audioData.f0.size()) &&
                  audioData.f0[static_cast<size_t>(i)] > 0.0f)
                     ? freqToMidi(audioData.f0[static_cast<size_t>(i)]) + note.getPitchOffset()
                     : 0.0f);
      float deltaMidi = (i < static_cast<int>(audioData.deltaPitch.size()))
                            ? audioData.deltaPitch[static_cast<size_t>(i)]
                            : 0.0f;
      // Final = base (with drag offset) + delta + global offset only
      float finalMidi = baseMidi + deltaMidi + globalOffset;

      if (finalMidi > 0.0f) {
        float x = framesToSeconds(i) * pixelsPerSecond;
        float y = midiToY(finalMidi) + pixelsPerSemitone * 0.5f;

        if (!pathStarted) {
          path.startNewSubPath(x, y);
          pathStarted = true;
        } else {
          path.lineTo(x, y);
        }
      }
    }

    if (pathStarted) {
      g.strokePath(path, juce::PathStrokeType(2.0f));
    }
  }

  // Draw base pitch curve as dashed line for development/debugging
  // Use cached base pitch to avoid expensive recalculation on every repaint
  if constexpr (ENABLE_BASE_PITCH_DEBUG) {
    updateBasePitchCacheIfNeeded();

    if (!cachedBasePitch.empty()) {
      // Calculate visible frame range
      double visibleStartTime = scrollX / pixelsPerSecond;
      double visibleEndTime = (scrollX + getWidth()) / pixelsPerSecond;
      int visStartFrame =
          std::max(0, static_cast<int>(visibleStartTime * audioData.sampleRate /
                                       HOP_SIZE));
      int visEndFrame = std::min(
          static_cast<int>(cachedBasePitch.size()),
          static_cast<int>(visibleEndTime * audioData.sampleRate / HOP_SIZE) +
              1);

      // Draw base pitch curve with dashed line
      g.setColour(
          juce::Colour(0xFF00FF00).withAlpha(0.6f)); // Green with transparency
      juce::Path basePath;
      bool basePathStarted = false;

      for (int i = visStartFrame; i < visEndFrame; ++i) {
        if (i >= 0 && i < static_cast<int>(cachedBasePitch.size())) {
          float baseMidi = cachedBasePitch[static_cast<size_t>(i)];
          if (baseMidi > 0.0f) {
            float x = framesToSeconds(i) * pixelsPerSecond;
            float y = midiToY(baseMidi) +
                      pixelsPerSemitone * 0.5f; // Center in grid cell

            if (!basePathStarted) {
              basePath.startNewSubPath(x, y);
              basePathStarted = true;
            } else {
              basePath.lineTo(x, y);
            }
          } else if (basePathStarted) {
            // Break path at unvoiced regions - draw current segment before
            // breaking
            juce::Path dashedPath;
            juce::PathStrokeType stroke(1.5f);
            const float dashLengths[] = {4.0f, 4.0f}; // 4px dash, 4px gap
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
        const float dashLengths[] = {4.0f, 4.0f}; // 4px dash, 4px gap
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
    drawingEdits.clear();
    drawingEditIndexByFrame.clear();
    drawCurves.clear();
    activeDrawCurve = nullptr;
    lastDrawFrame = -1;
    lastDrawValueCents = 0;

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

    // Capture delta slice from global dense deltaPitch for this note
    if (project) {
      auto &audioData = project->getAudioData();
      int startFrame = note->getStartFrame();
      int endFrame = note->getEndFrame();
      int numFrames = endFrame - startFrame;

      std::vector<float> delta(numFrames, 0.0f);
      for (int i = 0; i < numFrames; ++i) {
        int globalFrame = startFrame + i;
        if (globalFrame >= 0 &&
            globalFrame < static_cast<int>(audioData.deltaPitch.size()))
          delta[i] = audioData.deltaPitch[static_cast<size_t>(globalFrame)];
      }
      note->setDeltaPitch(std::move(delta));
    }

    // Start dragging
    isDragging = true;
    draggedNote = note;
    // Store adjusted Y coordinate (accounting for timeline and scroll)
    dragStartY = adjustedY;
    originalPitchOffset = note->getPitchOffset();
    originalMidiNote = note->getMidiNote();

    // Save boundary F0 values and original F0 for undo
    if (project) {
      auto &audioData = project->getAudioData();
      int startFrame = note->getStartFrame();
      int endFrame = note->getEndFrame();
      int f0Size = static_cast<int>(audioData.f0.size());

      boundaryF0Start = (startFrame > 0 && startFrame - 1 < f0Size)
                            ? audioData.f0[startFrame - 1]
                            : 0.0f;
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
    // Calculate adjusted Y coordinate (accounting for timeline and scroll)
    float adjustedY = e.y - timelineHeight + static_cast<float>(scrollY);

    // Calculate pitch change from drag (using adjusted coordinates)
    float deltaY = dragStartY - adjustedY;
    float deltaSemitones = deltaY / pixelsPerSemitone;

    // Update pitch offset for visual feedback only (lightweight)
    draggedNote->setPitchOffset(deltaSemitones);
    draggedNote->markDirty();

    // Always repaint during drag for real-time preview (throttled)
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

    // Check if there was any meaningful change (threshold: 0.001 semitones)
    constexpr float CHANGE_THRESHOLD = 0.001f;
    bool hasChange = std::abs(newOffset) >= CHANGE_THRESHOLD;

    if (hasChange && project) {
      int startFrame = draggedNote->getStartFrame();
      int endFrame = draggedNote->getEndFrame();
      auto &audioData = project->getAudioData();
      int f0Size = static_cast<int>(audioData.f0.size());

      // Update note's midiNote with final offset (bake pitchOffset into
      // midiNote)
      draggedNote->setMidiNote(originalMidiNote + newOffset);
      draggedNote->setPitchOffset(
          0.0f); // Reset offset since it's baked into midiNote

      // Find adjacent notes to expand dirty range (basePitch smoothing affects neighbors)
      const auto& notes = project->getNotes();
      int expandedStart = startFrame;
      int expandedEnd = endFrame;
      for (const auto& note : notes) {
        if (&note == draggedNote) continue;
        // If note is adjacent (within smoothing window ~20 frames), include it
        if (note.getEndFrame() > startFrame - 30 && note.getEndFrame() <= startFrame) {
          expandedStart = std::min(expandedStart, note.getStartFrame());
        }
        if (note.getStartFrame() < endFrame + 30 && note.getStartFrame() >= endFrame) {
          expandedEnd = std::max(expandedEnd, note.getEndFrame());
        }
      }

      // Rebuild base pitch curve and F0 with final note position
      PitchCurveProcessor::rebuildBaseFromNotes(*project);
      PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);

      // Invalidate base pitch cache so it gets regenerated on next paint
      invalidateBasePitchCache();

      // Mark dirty range for synthesis (use expanded range)
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
        // Capture frame range for undo callback
        int capturedExpandedStart = expandedStart;
        int capturedExpandedEnd = expandedEnd;
        int capturedF0Size = f0Size;
        auto action = std::make_unique<NotePitchDragAction>(
            draggedNote, &audioData.f0, originalMidiNote,
            originalMidiNote + newOffset, std::move(f0Edits),
            [this, capturedExpandedStart, capturedExpandedEnd, capturedF0Size](Note *n) {
              if (project) {
                PitchCurveProcessor::rebuildBaseFromNotes(*project);
                PitchCurveProcessor::composeF0InPlace(*project,
                                                      /*applyUvMask=*/false);
                // Invalidate base pitch cache
                invalidateBasePitchCache();
                // Set dirty range for synthesis (use expanded range)
                int smoothStart = std::max(0, capturedExpandedStart - 60);
                int smoothEnd = std::min(capturedF0Size, capturedExpandedEnd + 60);
                project->setF0DirtyRange(smoothStart, smoothEnd);
                // Clear note's dirty flag since we're using F0 dirty range
                // instead This prevents getDirtyFrameRange() from expanding the
                // range unnecessarily
                if (n) {
                  n->clearDirty();
                }
              }
            });
        undoManager->addAction(std::move(action));
      }

      if (onPitchEdited)
        onPitchEdited();
      repaint();
      if (onPitchEditFinished)
        onPitchEditFinished();
    } else {
      // No meaningful change: just reset pitchOffset and repaint
      draggedNote->setPitchOffset(0.0f);
      repaint();
    }
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
      // Calculate MIDI note at mouse position before zoom
      float mouseY = e.y - timelineHeight;
      float midiAtMouse = (mouseY + scrollY) / pixelsPerSemitone;

      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSemitone * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE,
                            newPps);
      pixelsPerSemitone = newPps;

      // Adjust scroll position to keep MIDI note at mouse position fixed
      double newScrollY = midiAtMouse * pixelsPerSemitone - mouseY;
      newScrollY = std::max(0.0, newScrollY);
      scrollY = newScrollY;

      updateScrollBars();
      repaint();
      return;
    }

    // Over timeline: horizontal zoom
    if (isOverTimeline) {
      // Calculate time at mouse position before zoom
      float mouseX = e.x - pianoKeysWidth;
      double timeAtMouse = (mouseX + scrollX) / pixelsPerSecond;

      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSecond * zoomFactor;
      newPps =
          juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);
      pixelsPerSecond = newPps;

      // Adjust scroll position to keep time at mouse position fixed
      double newScrollX = timeAtMouse * pixelsPerSecond - mouseX;
      newScrollX = std::max(0.0, newScrollX);
      scrollX = newScrollX;

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
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE,
                            newPps);

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

  // Update modular components
  renderer->setProject(proj);
  scrollZoomController->setProject(proj);
  pitchEditor->setProject(proj);

  invalidateBasePitchCache(); // Clear cache when project changes
  updateScrollBars();
  repaint();
}

void PianoRollComponent::setUndoManager(PitchUndoManager *manager) {
  undoManager = manager;
  pitchEditor->setUndoManager(manager);
}

void PianoRollComponent::setCursorTime(double time) {
  if (std::abs(cursorTime - time) < 0.0001)
    return; // Skip if no change

  // Calculate dirty rectangles for old and new cursor positions
  auto getCursorRect = [this](double t) -> juce::Rectangle<int> {
    float x =
        static_cast<float>(t * pixelsPerSecond - scrollX) + pianoKeysWidth;
    int width = 3; // Cursor width + 1px margin
    return juce::Rectangle<int>(static_cast<int>(x - 1), timelineHeight, width,
                                getHeight() - timelineHeight);
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

  const auto &notes = project->getNotes();
  const auto &audioData = project->getAudioData();
  int totalFrames = static_cast<int>(audioData.f0.size());

  // Check if cache is valid
  size_t currentNoteCount = 0;
  for (const auto &note : notes) {
    if (!note.isRest()) {
      currentNoteCount++;
    }
  }

  // Invalidate cache if notes changed or total frames changed or explicitly
  // invalidated For performance, we only check note count and total frames A
  // more precise check would compare note positions/pitches, but that's
  // expensive
  if (cacheInvalidated || cachedNoteCount != currentNoteCount ||
      cachedTotalFrames != totalFrames || cachedBasePitch.empty()) {
    // Only regenerate if we have notes and frames
    if (currentNoteCount > 0 && totalFrames > 0) {
      // Collect all notes
      std::vector<BasePitchCurve::NoteSegment> noteSegments;
      noteSegments.reserve(currentNoteCount);
      for (const auto &note : notes) {
        if (!note.isRest()) {
          noteSegments.push_back(
              {note.getStartFrame(), note.getEndFrame(), note.getMidiNote()});
        }
      }

      if (!noteSegments.empty()) {
        // Generate smoothed base pitch curve (expensive operation, cached)
        // This is only called when notes change, not on every repaint
        cachedBasePitch =
            BasePitchCurve::generateForNotes(noteSegments, totalFrames);
        cachedNoteCount = currentNoteCount;
        cachedTotalFrames = totalFrames;
        cacheInvalidated = false; // Mark cache as valid
      } else {
        cachedBasePitch.clear();
        cachedNoteCount = 0;
        cachedTotalFrames = 0;
        cacheInvalidated = false; // Mark as processed (even if empty)
      }
    } else {
      cachedBasePitch.clear();
      cachedNoteCount = 0;
      cachedTotalFrames = 0;
      cacheInvalidated = false; // Mark as processed (even if empty)
    }
  }
}

void PianoRollComponent::reapplyBasePitchForNote(Note *note) {
  if (!note || !project)
    return;

  auto &audioData = project->getAudioData();
  int startFrame = note->getStartFrame();
  int endFrame = note->getEndFrame();
  int f0Size = static_cast<int>(audioData.f0.size());

  // Reapply base + delta from dense curves
  for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
    float base = (i < static_cast<int>(audioData.basePitch.size()))
                     ? audioData.basePitch[static_cast<size_t>(i)]
                     : 0.0f;
    float delta = (i < static_cast<int>(audioData.deltaPitch.size()))
                      ? audioData.deltaPitch[static_cast<size_t>(i)]
                      : 0.0f;
    audioData.f0[i] = midiToFreq(base + delta);
  }

  // Always set F0 dirty range for synthesis (needed for undo/redo to trigger
  // resynthesis)
  int smoothStart = std::max(0, startFrame - 60);
  int smoothEnd = std::min(f0Size, endFrame + 60);
  project->setF0DirtyRange(smoothStart, smoothEnd);

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
  // Compensate for centering offset used in display
  float midi = yToMidi(y - pixelsPerSemitone * 0.5f);
  int frameIndex = static_cast<int>(secondsToFrames(static_cast<float>(time)));
  int midiCents = static_cast<int>(std::round(midi * 100.0f));
  applyPitchPoint(frameIndex, midiCents);
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

  // Clear deltaPitch for notes in the edited range so they use the drawn F0
  // values
  if (project && minFrame <= maxFrame) {
    auto &notes = project->getNotes();
    for (auto &note : notes) {
      // Check if note overlaps with edited range
      if (note.getEndFrame() > minFrame && note.getStartFrame() < maxFrame) {
        // Clear deltaPitch so the note will use audioData.f0 instead of
        // computed values
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
        &audioData.f0, &audioData.voicedMask, drawingEdits,
        [this](int minFrame, int maxFrame) {
          // Callback to trigger resynthesis after undo/redo
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

  // Trigger synthesis
  if (onPitchEditFinished)
    onPitchEditFinished();
}

void PianoRollComponent::applyPitchPoint(int frameIndex, int midiCents) {
  if (!project)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  const int f0Size = static_cast<int>(audioData.f0.size());
  if (audioData.deltaPitch.size() < audioData.f0.size())
    audioData.deltaPitch.resize(audioData.f0.size(), 0.0f);
  if (audioData.basePitch.size() < audioData.f0.size())
    audioData.basePitch.resize(audioData.f0.size(), 0.0f);
  if (frameIndex < 0 || frameIndex >= f0Size)
    return;

  if (!activeDrawCurve || frameIndex < activeDrawCurve->localStart()) {
    startNewPitchCurve(frameIndex, midiCents);
    // First point of the new curve: apply and exit
    auto applyFrameFirst = [&](int idx, int cents) {
      const float newFreq = midiToFreq(static_cast<float>(cents) / 100.0f);
      const float oldF0 = audioData.f0[idx];
      const bool oldVoiced =
          (idx < static_cast<int>(audioData.voicedMask.size()))
              ? audioData.voicedMask[idx]
              : false;

      auto it = drawingEditIndexByFrame.find(idx);
      if (it == drawingEditIndexByFrame.end()) {
        drawingEditIndexByFrame.emplace(idx, drawingEdits.size());
        drawingEdits.push_back(
            F0FrameEdit{idx, oldF0, newFreq, oldVoiced, true});
        // Clear deltaPitch for any note containing this frame so changes are
        // visible immediately
        auto &notes = project->getNotes();
        for (auto &note : notes) {
          if (note.getStartFrame() <= idx && note.getEndFrame() > idx &&
              note.hasDeltaPitch()) {
            note.setDeltaPitch(std::vector<float>());
            break;
          }
        }
      } else {
        auto &e = drawingEdits[it->second];
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
    applyFrameFirst(frameIndex, midiCents);
    return;
  }

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

      // Clear deltaPitch for any note containing this frame so changes are
      // visible immediately
      auto &notes = project->getNotes();
      for (auto &note : notes) {
        if (note.getStartFrame() <= idx && note.getEndFrame() > idx &&
            note.hasDeltaPitch()) {
          note.setDeltaPitch(std::vector<float>());
          break;
        }
      }
    } else {
      auto &e = drawingEdits[it->second];
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

  auto appendValue = [&](int idx, int cents) {
    if (!activeDrawCurve)
      return;
    const int offset = idx - activeDrawCurve->localStart();
    if (offset < 0)
      return;

    auto &vals = activeDrawCurve->mutableValues();
    if (offset < static_cast<int>(vals.size())) {
      vals[static_cast<std::size_t>(offset)] = cents;
      return;
    }

    while (static_cast<int>(vals.size()) < offset) {
      int fill = vals.empty() ? cents : vals.back();
      vals.push_back(fill);
    }
    vals.push_back(cents);
  };

  if (lastDrawFrame < 0) {
    appendValue(frameIndex, midiCents);
    applyFrame(frameIndex, midiCents);
  } else {
    int start = lastDrawFrame;
    int end = frameIndex;
    int startVal = lastDrawValueCents;
    int endVal = midiCents;

    if (start == end) {
      appendValue(frameIndex, midiCents);
      applyFrame(frameIndex, midiCents);
    } else {
      int step = (end > start) ? 1 : -1;
      int length = std::abs(end - start);
      for (int i = 0; i <= length; ++i) {
        int idx = start + i * step;
        float t = length == 0
                      ? 0.0f
                      : static_cast<float>(i) / static_cast<float>(length);
        float v = juce::jmap(t, 0.0f, 1.0f, static_cast<float>(startVal),
                             static_cast<float>(endVal));
        int cents = static_cast<int>(std::round(v));
        appendValue(idx, cents);
        applyFrame(idx, cents);
      }
    }
  }

  lastDrawFrame = frameIndex;
  lastDrawValueCents = midiCents;
}

void PianoRollComponent::startNewPitchCurve(int frameIndex, int midiCents) {
  drawCurves.push_back(std::make_unique<DrawCurve>(frameIndex, 1));
  activeDrawCurve = drawCurves.back().get();
  activeDrawCurve->appendValue(midiCents);
  lastDrawFrame = frameIndex;
  lastDrawValueCents = midiCents;
}
