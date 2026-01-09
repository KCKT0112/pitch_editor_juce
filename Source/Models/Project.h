#pragma once

#include "../JuceHeader.h"
#include "Note.h"
#include <vector>
#include <memory>

/**
 * Container for audio data and extracted features.
 */
struct AudioData
{
    juce::AudioBuffer<float> waveform;
    int sampleRate = 44100;
    
    // Extracted features
    std::vector<std::vector<float>> melSpectrogram;  // [T, NUM_MELS]
    std::vector<float> f0;                            // [T]
    std::vector<bool> voicedMask;                     // [T]
    
    float getDuration() const
    {
        if (waveform.getNumSamples() == 0) return 0.0f;
        return static_cast<float>(waveform.getNumSamples()) / sampleRate;
    }
    
    int getNumFrames() const
    {
        return static_cast<int>(melSpectrogram.size());
    }
};

/**
 * Project data container.
 */
class Project
{
public:
    Project();
    ~Project() = default;
    
    // File operations
    void setFilePath(const juce::File& file) { filePath = file; }
    juce::File getFilePath() const { return filePath; }
    void setProjectFilePath(const juce::File& file) { projectFilePath = file; }
    juce::File getProjectFilePath() const { return projectFilePath; }
    juce::String getName() const { return name; }
    void setName(const juce::String& n) { name = n; }
    
    // Audio data
    AudioData& getAudioData() { return audioData; }
    const AudioData& getAudioData() const { return audioData; }
    
    // Notes
    std::vector<Note>& getNotes() { return notes; }
    const std::vector<Note>& getNotes() const { return notes; }
    void addNote(Note note) { notes.push_back(std::move(note)); }
    void clearNotes() { notes.clear(); }
    
    Note* getNoteAtFrame(int frame);
    std::vector<Note*> getNotesInRange(int startFrame, int endFrame);
    std::vector<Note*> getSelectedNotes();
    std::vector<Note*> getDirtyNotes();
    void deselectAllNotes();
    void clearAllDirty();
    
    // Global settings
    float getGlobalPitchOffset() const { return globalPitchOffset; }
    void setGlobalPitchOffset(float offset) { globalPitchOffset = offset; }
    
    float getFormantShift() const { return formantShift; }
    void setFormantShift(float shift) { formantShift = shift; }
    
    float getVolume() const { return volume; }
    void setVolume(float vol) { volume = vol; }
    
    // Get adjusted F0 with all modifications applied
    std::vector<float> getAdjustedF0() const;
    
    // Get adjusted F0 for a specific frame range
    std::vector<float> getAdjustedF0ForRange(int startFrame, int endFrame) const;
    
    // Get frame range that needs resynthesis (based on dirty notes)
    // Returns {-1, -1} if no dirty notes
    std::pair<int, int> getDirtyFrameRange() const;
    
    // Check if any notes are dirty
    bool hasDirtyNotes() const;
    
    // F0 direct edit dirty tracking (for Draw mode)
    void setF0DirtyRange(int startFrame, int endFrame);
    void clearF0DirtyRange();
    bool hasF0DirtyRange() const;
    std::pair<int, int> getF0DirtyRange() const;
    
    // Modified state
    bool isModified() const { return modified; }
    void setModified(bool mod) { modified = mod; }

    // Persistence
    bool saveToFile(const juce::File& file) const;
    std::unique_ptr<juce::XmlElement> toXml() const;
    bool fromXml(const juce::XmlElement& xml);
    
private:
    juce::String name = "Untitled";
    juce::File filePath;
    juce::File projectFilePath;
    
    AudioData audioData;
    std::vector<Note> notes;
    
    float globalPitchOffset = 0.0f;
    float formantShift = 0.0f;
    float volume = 0.0f;  // dB
    
    // F0 direct edit dirty range
    int f0DirtyStart = -1;
    int f0DirtyEnd = -1;
    
    bool modified = false;
};
