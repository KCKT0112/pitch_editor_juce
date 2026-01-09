#pragma once

#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Generates a smoothed base pitch curve from note MIDI values.
 * Uses cosine-windowed convolution (from ds-editor-lite).
 */
class BasePitchCurve
{
public:
    struct NoteSegment {
        int startFrame;
        int endFrame;
        float midiNote;
    };

    // Generate smoothed base pitch for a single note
    // Returns base pitch in MIDI note values for each frame
    static std::vector<float> generateForNote(int startFrame, int endFrame, float midiNote, int totalFrames);

    // Generate smoothed base pitch for multiple notes
    static std::vector<float> generateForNotes(const std::vector<NoteSegment>& notes, int totalFrames);

    // Calculate delta pitch (actual F0 in MIDI - base pitch)
    static std::vector<float> calculateDeltaPitch(const std::vector<float>& f0Values,
                                                   const std::vector<float>& basePitch,
                                                   int startFrame);

    // Apply base pitch change while preserving delta pitch
    // Returns new F0 values in Hz
    static std::vector<float> applyBasePitchChange(const std::vector<float>& deltaPitch,
                                                    float newBaseMidi,
                                                    int numFrames);

private:
    static constexpr int KERNEL_SIZE = 119;  // ±59ms at 1000Hz sampling
    static constexpr double SMOOTH_WINDOW = 0.12;  // 120ms total window

    static std::vector<double> createCosineKernel();
};
