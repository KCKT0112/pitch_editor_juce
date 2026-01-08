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
    void deselectAllNotes();
    
    // Global settings
    float getGlobalPitchOffset() const { return globalPitchOffset; }
    void setGlobalPitchOffset(float offset) { globalPitchOffset = offset; }
    
    float getFormantShift() const { return formantShift; }
    void setFormantShift(float shift) { formantShift = shift; }
    
    float getVolume() const { return volume; }
    void setVolume(float vol) { volume = vol; }
    
    // Get adjusted F0 with all modifications applied
    std::vector<float> getAdjustedF0() const;
    
    // Modified state
    bool isModified() const { return modified; }
    void setModified(bool mod) { modified = mod; }
    
private:
    juce::String name = "Untitled";
    juce::File filePath;
    
    AudioData audioData;
    std::vector<Note> notes;
    
    float globalPitchOffset = 0.0f;
    float formantShift = 0.0f;
    float volume = 0.0f;  // dB
    
    bool modified = false;
};
