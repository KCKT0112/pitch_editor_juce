#include "Project.h"
#include "../Utils/Constants.h"
#include <algorithm>
#include <cmath>

Project::Project()
{
}

Note* Project::getNoteAtFrame(int frame)
{
    for (auto& note : notes)
    {
        if (note.containsFrame(frame))
            return &note;
    }
    return nullptr;
}

std::vector<Note*> Project::getNotesInRange(int startFrame, int endFrame)
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.getStartFrame() < endFrame && note.getEndFrame() > startFrame)
            result.push_back(&note);
    }
    return result;
}

std::vector<Note*> Project::getSelectedNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isSelected())
            result.push_back(&note);
    }
    return result;
}

void Project::deselectAllNotes()
{
    for (auto& note : notes)
        note.setSelected(false);
}

std::vector<float> Project::getAdjustedF0() const
{
    if (audioData.f0.empty())
        return {};
    
    // Start with copy of original F0
    std::vector<float> adjustedF0 = audioData.f0;
    
    // Apply global pitch offset
    if (globalPitchOffset != 0.0f)
    {
        float globalRatio = std::pow(2.0f, globalPitchOffset / 12.0f);
        for (auto& f : adjustedF0)
        {
            if (f > 0.0f)
                f *= globalRatio;
        }
    }
    
    // Calculate per-frame ratios from notes
    std::vector<float> frameRatios(adjustedF0.size(), 1.0f);
    
    for (const auto& note : notes)
    {
        if (note.getPitchOffset() != 0.0f)
        {
            float noteRatio = std::pow(2.0f, note.getPitchOffset() / 12.0f);
            int start = note.getStartFrame();
            int end = std::min(note.getEndFrame(), static_cast<int>(adjustedF0.size()));
            
            for (int i = start; i < end; ++i)
                frameRatios[i] = noteRatio;
        }
    }
    
    // Apply smoothing at transitions
    const int smoothFrames = 5;
    
    // Find transition points
    for (size_t i = 1; i < frameRatios.size(); ++i)
    {
        if (std::abs(frameRatios[i] - frameRatios[i-1]) > 0.001f)
        {
            // Smooth transition
            int startIdx = std::max(0, static_cast<int>(i) - smoothFrames / 2);
            int endIdx = std::min(static_cast<int>(frameRatios.size()), 
                                  static_cast<int>(i) + smoothFrames / 2 + 2);
            
            if (endIdx - startIdx > 1)
            {
                float valBefore = frameRatios[startIdx];
                float valAfter = frameRatios[endIdx - 1];
                
                for (int j = startIdx; j < endIdx; ++j)
                {
                    float t = static_cast<float>(j - startIdx) / (endIdx - startIdx - 1);
                    frameRatios[j] = valBefore + t * (valAfter - valBefore);
                }
            }
        }
    }
    
    // Apply ratios only to voiced regions
    for (size_t i = 0; i < adjustedF0.size(); ++i)
    {
        if (i < audioData.voicedMask.size() && audioData.voicedMask[i])
        {
            adjustedF0[i] *= frameRatios[i];
        }
    }
    
    return adjustedF0;
}
