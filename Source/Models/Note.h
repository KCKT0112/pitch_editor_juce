#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * Represents a single note/pitch segment.
 */
class Note
{
public:
    Note() = default;
    Note(int startFrame, int endFrame, float midiNote);
    
    // Frame range
    int getStartFrame() const { return startFrame; }
    int getEndFrame() const { return endFrame; }
    void setStartFrame(int frame) { startFrame = frame; }
    void setEndFrame(int frame) { endFrame = frame; }
    int getDurationFrames() const { return endFrame - startFrame; }
    
    // Pitch
    float getMidiNote() const { return midiNote; }
    void setMidiNote(float note) { midiNote = note; }
    float getPitchOffset() const { return pitchOffset; }
    void setPitchOffset(float offset) { pitchOffset = offset; }
    float getAdjustedMidiNote() const { return midiNote + pitchOffset; }
    
    // F0 values
    const std::vector<float>& getF0Values() const { return f0Values; }
    void setF0Values(std::vector<float> values) { f0Values = std::move(values); }
    std::vector<float> getAdjustedF0() const;
    
    // Selection
    bool isSelected() const { return selected; }
    void setSelected(bool sel) { selected = sel; }
    
    // Color
    juce::Colour getColor() const { return color; }
    void setColor(juce::Colour c) { color = c; }
    
    // Check if frame is within note
    bool containsFrame(int frame) const;
    
private:
    int startFrame = 0;
    int endFrame = 0;
    float midiNote = 60.0f;
    float pitchOffset = 0.0f;
    std::vector<float> f0Values;
    bool selected = false;
    juce::Colour color = juce::Colour(0xFF9B59B6);
};
