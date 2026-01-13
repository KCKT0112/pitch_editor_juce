#include "PianoRollRenderer.h"

PianoRollRenderer::PianoRollRenderer() = default;

void PianoRollRenderer::invalidateWaveformCache() {
    cachedScrollX = -1.0;
    cachedPixelsPerSecond = -1.0f;
    cachedWidth = 0;
    cachedHeight = 0;
}

void PianoRollRenderer::invalidateBasePitchCache() {
    cacheInvalidated = true;
    cachedNoteCount = 0;
    cachedBasePitch.clear();
}

float PianoRollRenderer::catmullRom(float t, float p0, float p1, float p2, float p3) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void PianoRollRenderer::drawBackgroundWaveform(juce::Graphics& g, const juce::Rectangle<int>& area) {
    if (!project || !coordMapper)
        return;

    const auto& audioData = project->getAudioData();
    if (audioData.waveform.getNumSamples() == 0)
        return;

    double scrollX = coordMapper->getScrollX();
    float pixelsPerSecond = coordMapper->getPixelsPerSecond();

    // Check cache validity
    bool cacheValid = waveformCache.isValid() &&
                      std::abs(cachedScrollX - scrollX) < 1.0 &&
                      std::abs(cachedPixelsPerSecond - pixelsPerSecond) < 0.01f &&
                      cachedWidth == area.getWidth() &&
                      cachedHeight == area.getHeight();

    if (cacheValid) {
        g.drawImageAt(waveformCache, area.getX(), area.getY());
        return;
    }

    // Render to cache
    waveformCache = juce::Image(juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics cacheGraphics(waveformCache);

    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    float visibleHeight = static_cast<float>(area.getHeight());
    float centerY = visibleHeight * 0.5f;
    float waveformHeight = visibleHeight * 0.8f;

    juce::Path waveformPath;
    int visibleWidth = area.getWidth();

    waveformPath.startNewSubPath(0.0f, centerY);

    // Top half
    for (int px = 0; px < visibleWidth; ++px) {
        double time = (scrollX + px) / pixelsPerSecond;
        int startSample = static_cast<int>(time * SAMPLE_RATE);
        int endSample = static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

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
        int endSample = static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

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
    cachedWidth = area.getWidth();
    cachedHeight = area.getHeight();

    g.drawImageAt(waveformCache, area.getX(), area.getY());
}

void PianoRollRenderer::drawGrid(juce::Graphics& g, int width, int height) {
    if (!coordMapper)
        return;

    float duration = project ? project->getAudioData().getDuration() : 60.0f;
    float totalWidth = std::max(duration * coordMapper->getPixelsPerSecond(), static_cast<float>(width));
    float totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * coordMapper->getPixelsPerSemitone();

    // Horizontal lines (pitch)
    g.setColour(juce::Colour(COLOR_GRID));

    for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
        float y = coordMapper->midiToY(static_cast<float>(midi));
        int noteInOctave = midi % 12;

        if (noteInOctave == 0) {
            g.setColour(juce::Colour(COLOR_GRID_BAR));
            g.drawHorizontalLine(static_cast<int>(y), 0, totalWidth);
            g.setColour(juce::Colour(COLOR_GRID));
        } else {
            g.drawHorizontalLine(static_cast<int>(y), 0, totalWidth);
        }
    }

    // Vertical lines (time)
    float secondsPerBeat = 60.0f / 120.0f;
    float pixelsPerBeat = secondsPerBeat * coordMapper->getPixelsPerSecond();

    for (float x = 0; x < totalWidth; x += pixelsPerBeat) {
        g.setColour(juce::Colour(COLOR_GRID));
        g.drawVerticalLine(static_cast<int>(x), 0, totalHeight);
    }
}

void PianoRollRenderer::drawTimeline(juce::Graphics& g, int width) {
    if (!coordMapper)
        return;

    constexpr int scrollBarSize = 8;
    auto timelineArea = juce::Rectangle<int>(
        CoordinateMapper::pianoKeysWidth, 0,
        width - CoordinateMapper::pianoKeysWidth - scrollBarSize,
        CoordinateMapper::timelineHeight);

    g.setColour(juce::Colour(0xFF1E1E28));
    g.fillRect(timelineArea);

    g.setColour(juce::Colour(COLOR_GRID_BAR));
    g.drawHorizontalLine(CoordinateMapper::timelineHeight - 1,
                         static_cast<float>(CoordinateMapper::pianoKeysWidth),
                         static_cast<float>(width - scrollBarSize));

    float pixelsPerSecond = coordMapper->getPixelsPerSecond();
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
    double scrollX = coordMapper->getScrollX();

    g.setFont(11.0f);

    for (float time = 0.0f; time <= duration + secondsPerTick; time += secondsPerTick) {
        float x = CoordinateMapper::pianoKeysWidth + time * pixelsPerSecond - static_cast<float>(scrollX);

        if (x < CoordinateMapper::pianoKeysWidth - 50 || x > width)
            continue;

        bool isMajor = std::fmod(time, secondsPerTick * 2.0f) < 0.001f;
        int tickHeight = isMajor ? 8 : 4;

        g.setColour(juce::Colour(COLOR_GRID_BAR));
        g.drawVerticalLine(static_cast<int>(x),
                           static_cast<float>(CoordinateMapper::timelineHeight - tickHeight),
                           static_cast<float>(CoordinateMapper::timelineHeight - 1));

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
            g.drawText(label, static_cast<int>(x) + 3, 2, 50, CoordinateMapper::timelineHeight - 4,
                       juce::Justification::centredLeft, false);
        }
    }
}

void PianoRollRenderer::drawNotes(juce::Graphics& g, double visibleStartTime, double visibleEndTime) {
    if (!project || !coordMapper)
        return;

    const auto& audioData = project->getAudioData();
    const float* samples = audioData.waveform.getNumSamples() > 0
                               ? audioData.waveform.getReadPointer(0)
                               : nullptr;
    int totalSamples = audioData.waveform.getNumSamples();

    for (auto& note : project->getNotes()) {
        if (note.isRest())
            continue;

        double noteStartTime = framesToSeconds(note.getStartFrame());
        double noteEndTime = framesToSeconds(note.getEndFrame());
        if (noteEndTime < visibleStartTime || noteStartTime > visibleEndTime)
            continue;

        float x = static_cast<float>(noteStartTime * coordMapper->getPixelsPerSecond());
        float w = framesToSeconds(note.getDurationFrames()) * coordMapper->getPixelsPerSecond();
        float h = coordMapper->getPixelsPerSemitone();

        float baseGridCenterY = coordMapper->midiToY(note.getMidiNote()) + h * 0.5f;
        float pitchOffsetPixels = -note.getPitchOffset() * coordMapper->getPixelsPerSemitone();
        float y = baseGridCenterY + pitchOffsetPixels - h * 0.5f;

        juce::Colour noteColor = note.isSelected()
                                     ? juce::Colour(COLOR_NOTE_SELECTED)
                                     : juce::Colour(COLOR_NOTE_NORMAL);

        if (samples && totalSamples > 0 && w > 2.0f) {
            drawNoteWaveform(g, note, x, y, w, h, samples, totalSamples, audioData.sampleRate);
        } else {
            g.setColour(noteColor.withAlpha(0.85f));
            g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
        }
    }
}

void PianoRollRenderer::drawNoteWaveform(juce::Graphics& g, const Note& note, float x, float y, float w, float h,
                                         const float* samples, int totalSamples, int sampleRate) {
    juce::Colour noteColor = note.isSelected()
                                 ? juce::Colour(COLOR_NOTE_SELECTED)
                                 : juce::Colour(COLOR_NOTE_NORMAL);

    int startSample = static_cast<int>(framesToSeconds(note.getStartFrame()) * sampleRate);
    int endSample = static_cast<int>(framesToSeconds(note.getEndFrame()) * sampleRate);
    startSample = std::max(0, std::min(startSample, totalSamples - 1));
    endSample = std::max(startSample + 1, std::min(endSample, totalSamples));

    int numNoteSamples = endSample - startSample;
    int samplesPerPixel = std::max(1, static_cast<int>(numNoteSamples / w));

    float centerY = y + h * 0.5f;
    float waveHeight = h * 3.0f;

    std::vector<float> waveValues;
    float step = std::max(0.5f, w / 1024.0f);

    for (float px = 0; px <= w; px += step) {
        int sampleIdx = startSample + static_cast<int>((px / w) * numNoteSamples);
        int sampleEnd = std::min(sampleIdx + samplesPerPixel, endSample);

        float maxVal = 0.0f;
        for (int i = sampleIdx; i < sampleEnd; ++i)
            maxVal = std::max(maxVal, std::abs(samples[i]));

        waveValues.push_back(maxVal);
    }

    // Smooth waveform
    if (waveValues.size() > 2) {
        std::vector<float> smoothed(waveValues.size());
        smoothed[0] = waveValues[0];
        for (size_t i = 1; i + 1 < waveValues.size(); ++i) {
            smoothed[i] = (waveValues[i - 1] * 0.25f + waveValues[i] * 0.5f + waveValues[i + 1] * 0.25f);
        }
        smoothed[waveValues.size() - 1] = waveValues[waveValues.size() - 1];
        waveValues = std::move(smoothed);
    }

    size_t numPoints = waveValues.size();
    if (numPoints < 2) {
        g.setColour(noteColor.withAlpha(0.85f));
        g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
        return;
    }

    // Draw filled waveform
    g.setColour(noteColor.withAlpha(0.85f));
    juce::Path waveformPath;

    waveformPath.startNewSubPath(x, centerY - waveValues[0] * waveHeight * 0.5f);

    const int curveSegments = 4;
    for (size_t i = 0; i + 1 < numPoints; ++i) {
        float px1 = (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
        float px2 = (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) * w;

        size_t idx0 = (i > 0) ? i - 1 : i;
        size_t idx1 = i;
        size_t idx2 = i + 1;
        size_t idx3 = (i + 2 < numPoints) ? i + 2 : i + 1;

        for (int seg = 1; seg <= curveSegments; ++seg) {
            float t = static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, waveValues[idx0], waveValues[idx1], waveValues[idx2], waveValues[idx3]);
            float yPos = centerY - val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
        }
    }

    // Bottom curve
    waveformPath.lineTo(x + w, centerY + waveValues[numPoints - 1] * waveHeight * 0.5f);

    for (int i = static_cast<int>(numPoints) - 2; i >= 0; --i) {
        float px1 = (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) * w;
        float px2 = (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;

        size_t idx0 = (i + 2 < numPoints) ? i + 2 : i + 1;
        size_t idx1 = i + 1;
        size_t idx2 = i;
        size_t idx3 = (i > 0) ? i - 1 : i;

        for (int seg = 1; seg <= curveSegments; ++seg) {
            float t = static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, waveValues[idx0], waveValues[idx1], waveValues[idx2], waveValues[idx3]);
            float yPos = centerY + val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
        }
    }

    waveformPath.closeSubPath();
    g.fillPath(waveformPath);

    // Draw outline
    g.setColour(noteColor.brighter(0.2f));
    g.strokePath(waveformPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
}

void PianoRollRenderer::drawPitchCurves(juce::Graphics& g, float globalPitchOffset) {
    if (!project || !coordMapper)
        return;

    const auto& audioData = project->getAudioData();
    if (audioData.f0.empty())
        return;

    g.setColour(juce::Colour(COLOR_PITCH_CURVE));

    for (const auto& note : project->getNotes()) {
        if (note.isRest())
            continue;

        juce::Path path;
        bool pathStarted = false;

        int startFrame = note.getStartFrame();
        int endFrame = std::min(note.getEndFrame(), static_cast<int>(audioData.f0.size()));

        for (int i = startFrame; i < endFrame; ++i) {
            float baseMidi = (i < static_cast<int>(audioData.basePitch.size()))
                ? audioData.basePitch[static_cast<size_t>(i)] + note.getPitchOffset()
                : ((i < static_cast<int>(audioData.f0.size()) && audioData.f0[static_cast<size_t>(i)] > 0.0f)
                    ? freqToMidi(audioData.f0[static_cast<size_t>(i)]) + note.getPitchOffset()
                    : 0.0f);

            float deltaMidi = (i < static_cast<int>(audioData.deltaPitch.size()))
                ? audioData.deltaPitch[static_cast<size_t>(i)]
                : 0.0f;

            float finalMidi = baseMidi + deltaMidi + globalPitchOffset;

            if (finalMidi > 0.0f) {
                float x = framesToSeconds(i) * coordMapper->getPixelsPerSecond();
                float y = coordMapper->midiToY(finalMidi) + coordMapper->getPixelsPerSemitone() * 0.5f;

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
}

void PianoRollRenderer::drawCursor(juce::Graphics& g, double cursorTime, int height) {
    if (!coordMapper)
        return;

    float x = coordMapper->timeToX(cursorTime);
    float totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * coordMapper->getPixelsPerSemitone();

    g.setColour(juce::Colours::white);
    g.fillRect(x - 0.5f, 0.0f, 1.0f, totalHeight);
}

void PianoRollRenderer::drawPianoKeys(juce::Graphics& g, int height) {
    if (!coordMapper)
        return;

    constexpr int scrollBarSize = 8;
    auto keyArea = juce::Rectangle<int>(0, CoordinateMapper::timelineHeight,
                                        CoordinateMapper::pianoKeysWidth,
                                        height - CoordinateMapper::timelineHeight - scrollBarSize);

    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRect(keyArea);

    static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    double scrollY = coordMapper->getScrollY();
    float pixelsPerSemitone = coordMapper->getPixelsPerSemitone();

    for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
        float y = coordMapper->midiToY(static_cast<float>(midi)) - static_cast<float>(scrollY) +
                  CoordinateMapper::timelineHeight;
        int noteInOctave = midi % 12;

        bool isBlack = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                        noteInOctave == 8 || noteInOctave == 10);

        if (isBlack)
            g.setColour(juce::Colour(0xFF2D2D37));
        else
            g.setColour(juce::Colour(0xFF3D3D47));

        g.fillRect(0.0f, y, static_cast<float>(CoordinateMapper::pianoKeysWidth - 2), pixelsPerSemitone - 1);

        int octave = midi / 12 - 1;
        juce::String noteName = juce::String(noteNames[noteInOctave]) + juce::String(octave);

        g.setColour(isBlack ? juce::Colour(0xFFAAAAAA) : juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(noteName, CoordinateMapper::pianoKeysWidth - 36, static_cast<int>(y), 32,
                   static_cast<int>(pixelsPerSemitone), juce::Justification::centred);
    }
}

void PianoRollRenderer::updateBasePitchCacheIfNeeded() {
    if (!project) {
        cachedBasePitch.clear();
        cachedNoteCount = 0;
        cachedTotalFrames = 0;
        return;
    }

    const auto& notes = project->getNotes();
    const auto& audioData = project->getAudioData();
    int totalFrames = static_cast<int>(audioData.f0.size());

    size_t currentNoteCount = 0;
    for (const auto& note : notes) {
        if (!note.isRest())
            currentNoteCount++;
    }

    if (cacheInvalidated || cachedNoteCount != currentNoteCount ||
        cachedTotalFrames != totalFrames || cachedBasePitch.empty()) {

        if (currentNoteCount > 0 && totalFrames > 0) {
            std::vector<BasePitchCurve::NoteSegment> noteSegments;
            noteSegments.reserve(currentNoteCount);
            for (const auto& note : notes) {
                if (!note.isRest()) {
                    noteSegments.push_back({note.getStartFrame(), note.getEndFrame(), note.getMidiNote()});
                }
            }

            if (!noteSegments.empty()) {
                cachedBasePitch = BasePitchCurve::generateForNotes(noteSegments, totalFrames);
                cachedNoteCount = currentNoteCount;
                cachedTotalFrames = totalFrames;
                cacheInvalidated = false;
            } else {
                cachedBasePitch.clear();
                cachedNoteCount = 0;
                cachedTotalFrames = 0;
                cacheInvalidated = false;
            }
        } else {
            cachedBasePitch.clear();
            cachedNoteCount = 0;
            cachedTotalFrames = 0;
            cacheInvalidated = false;
        }
    }
}
