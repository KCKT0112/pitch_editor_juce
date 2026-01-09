#include "BasePitchCurve.h"
#include <algorithm>

// Local constants (to avoid JUCE dependency from Constants.h)
namespace {
    constexpr int SAMPLE_RATE = 44100;
    constexpr int HOP_SIZE = 512;
    constexpr int MIDI_A4 = 69;
    constexpr float FREQ_A4 = 440.0f;

    inline float midiToFreq(float midi) {
        return FREQ_A4 * std::pow(2.0f, (midi - MIDI_A4) / 12.0f);
    }

    inline float freqToMidi(float freq) {
        if (freq <= 0.0f) return 0.0f;
        return 12.0f * std::log2(freq / FREQ_A4) + MIDI_A4;
    }
}

std::vector<double> BasePitchCurve::createCosineKernel()
{
    std::vector<double> kernel(KERNEL_SIZE);
    double sum = 0.0;

    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        double time = 0.001 * (i - KERNEL_SIZE / 2);  // Time offset in seconds
        kernel[i] = std::cos(M_PI * time / SMOOTH_WINDOW);
        sum += kernel[i];
    }

    // Normalize
    for (auto& value : kernel)
        value /= sum;

    return kernel;
}

std::vector<float> BasePitchCurve::generateForNote(int startFrame, int endFrame, float midiNote, int totalFrames)
{
    std::vector<NoteSegment> notes = {{startFrame, endFrame, midiNote}};
    return generateForNotes(notes, totalFrames);
}

std::vector<float> BasePitchCurve::generateForNotes(const std::vector<NoteSegment>& notes, int totalFrames)
{
    if (notes.empty() || totalFrames <= 0)
        return {};

    // Convert frames to milliseconds (at ~86 fps, each frame is ~11.6ms)
    // We'll work at 1ms resolution for smoothing, then resample
    double msPerFrame = 1000.0 * HOP_SIZE / SAMPLE_RATE;  // ~11.6ms
    int totalMs = static_cast<int>(totalFrames * msPerFrame) + 200;  // Add padding

    // Create initial values at 1ms resolution
    std::vector<double> initValues(totalMs, 0.0);
    size_t noteIndex = 0;

    for (int ms = 0; ms < totalMs; ++ms)
    {
        // Find which note this millisecond belongs to
        double framePos = ms / msPerFrame;

        // Find the note containing this position
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (framePos >= notes[i].startFrame && framePos < notes[i].endFrame)
            {
                initValues[ms] = notes[i].midiNote;
                break;
            }
            // Between notes - use midpoint transition
            if (i < notes.size() - 1 &&
                framePos >= notes[i].endFrame && framePos < notes[i + 1].startFrame)
            {
                double midpoint = 0.5 * (notes[i].endFrame + notes[i + 1].startFrame);
                initValues[ms] = (framePos < midpoint) ? notes[i].midiNote : notes[i + 1].midiNote;
                break;
            }
        }

        // Before first note or after last note
        if (initValues[ms] == 0.0)
        {
            if (!notes.empty())
            {
                if (framePos < notes.front().startFrame)
                    initValues[ms] = notes.front().midiNote;
                else if (framePos >= notes.back().endFrame)
                    initValues[ms] = notes.back().midiNote;
            }
        }
    }

    // Apply cosine kernel convolution
    auto kernel = createCosineKernel();
    std::vector<double> smoothedMs(totalMs, 0.0);

    for (int i = 0; i < totalMs; ++i)
    {
        for (int j = 0; j < KERNEL_SIZE; ++j)
        {
            int srcIdx = std::clamp(i - KERNEL_SIZE / 2 + j, 0, totalMs - 1);
            smoothedMs[i] += initValues[srcIdx] * kernel[j];
        }
    }

    // Resample back to frame resolution
    std::vector<float> result(totalFrames);
    for (int frame = 0; frame < totalFrames; ++frame)
    {
        double ms = frame * msPerFrame;
        int msIdx = static_cast<int>(ms);
        double frac = ms - msIdx;

        if (msIdx + 1 < totalMs)
            result[frame] = static_cast<float>(smoothedMs[msIdx] * (1.0 - frac) + smoothedMs[msIdx + 1] * frac);
        else if (msIdx < totalMs)
            result[frame] = static_cast<float>(smoothedMs[msIdx]);
        else
            result[frame] = static_cast<float>(smoothedMs.back());
    }

    return result;
}

std::vector<float> BasePitchCurve::calculateDeltaPitch(const std::vector<float>& f0Values,
                                                        const std::vector<float>& basePitch,
                                                        int startFrame)
{
    std::vector<float> deltaPitch(f0Values.size(), 0.0f);

    for (size_t i = 0; i < f0Values.size(); ++i)
    {
        int globalFrame = startFrame + static_cast<int>(i);
        if (globalFrame < 0 || globalFrame >= static_cast<int>(basePitch.size()))
            continue;

        float f0 = f0Values[i];
        if (f0 > 0.0f)
        {
            // Convert F0 to MIDI
            float f0Midi = freqToMidi(f0);
            // Delta = actual - base
            deltaPitch[i] = f0Midi - basePitch[globalFrame];
        }
    }

    return deltaPitch;
}

std::vector<float> BasePitchCurve::applyBasePitchChange(const std::vector<float>& deltaPitch,
                                                         float newBaseMidi,
                                                         int numFrames)
{
    std::vector<float> newF0(numFrames, 0.0f);

    for (int i = 0; i < numFrames && i < static_cast<int>(deltaPitch.size()); ++i)
    {
        // New MIDI = new base + delta
        float newMidi = newBaseMidi + deltaPitch[i];
        // Convert back to Hz
        newF0[i] = midiToFreq(newMidi);
    }

    return newF0;
}
