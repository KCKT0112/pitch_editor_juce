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

bool Note::containsFrame(int frame) const
{
    return frame >= startFrame && frame < endFrame;
}
