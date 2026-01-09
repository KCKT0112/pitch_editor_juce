#include "Note.h"
#include "../Utils/Constants.h"

Note::Note(int startFrame, int endFrame, float midiNote)
    : startFrame(startFrame), endFrame(endFrame), midiNote(midiNote)
{
}

std::vector<float> Note::getAdjustedF0() const
{
    if (f0Values.empty() || pitchOffset == 0.0f)
        return f0Values;

    // Convert semitone offset to frequency ratio
    float ratio = std::pow(2.0f, pitchOffset / 12.0f);

    std::vector<float> adjusted;
    adjusted.reserve(f0Values.size());

    for (float f0 : f0Values)
    {
        if (f0 > 0.0f)
            adjusted.push_back(f0 * ratio);
        else
            adjusted.push_back(0.0f);
    }

    return adjusted;
}

std::vector<float> Note::computeF0FromDelta() const
{
    int numFrames = endFrame - startFrame;
    std::vector<float> result(numFrames, 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        // Get delta pitch for this frame (or 0 if not available)
        float delta = (i < static_cast<int>(deltaPitch.size())) ? deltaPitch[i] : 0.0f;

        // Actual MIDI = base + offset + delta
        float actualMidi = midiNote + pitchOffset + delta;

        // Convert to Hz
        result[i] = midiToFreq(actualMidi);
    }

    return result;
}

bool Note::containsFrame(int frame) const
{
    return frame >= startFrame && frame < endFrame;
}
