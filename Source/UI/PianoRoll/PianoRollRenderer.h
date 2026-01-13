#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Project.h"
#include "../../Utils/Constants.h"
#include "../../Utils/BasePitchCurve.h"
#include "CoordinateMapper.h"
#include <vector>

/**
 * Handles all rendering for the piano roll component.
 * Manages caches for waveform and base pitch curve.
 */
class PianoRollRenderer {
public:
    PianoRollRenderer();
    ~PianoRollRenderer() = default;

    void setCoordinateMapper(CoordinateMapper* mapper) { coordMapper = mapper; }
    void setProject(Project* proj) { project = proj; }

    // Main drawing methods
    void drawBackgroundWaveform(juce::Graphics& g, const juce::Rectangle<int>& area);
    void drawGrid(juce::Graphics& g, int width, int height);
    void drawTimeline(juce::Graphics& g, int width);
    void drawNotes(juce::Graphics& g, double visibleStartTime, double visibleEndTime);
    void drawPitchCurves(juce::Graphics& g, float globalPitchOffset);
    void drawCursor(juce::Graphics& g, double cursorTime, int height);
    void drawPianoKeys(juce::Graphics& g, int height);

    // Cache management
    void invalidateWaveformCache();
    void invalidateBasePitchCache();
    void updateBasePitchCacheIfNeeded();

    // Debug option
    static constexpr bool ENABLE_BASE_PITCH_DEBUG = true;

private:
    // Helper for Catmull-Rom spline interpolation
    static float catmullRom(float t, float p0, float p1, float p2, float p3);

    // Draw note waveform with smooth curves
    void drawNoteWaveform(juce::Graphics& g, const Note& note, float x, float y, float w, float h,
                         const float* samples, int totalSamples, int sampleRate);

    CoordinateMapper* coordMapper = nullptr;
    Project* project = nullptr;

    // Waveform cache
    juce::Image waveformCache;
    double cachedScrollX = -1.0;
    float cachedPixelsPerSecond = -1.0f;
    int cachedWidth = 0;
    int cachedHeight = 0;

    // Base pitch cache
    std::vector<float> cachedBasePitch;
    size_t cachedNoteCount = 0;
    int cachedTotalFrames = 0;
    bool cacheInvalidated = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollRenderer)
};
