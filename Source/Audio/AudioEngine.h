#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include <functional>

/**
 * Audio engine for playback and synthesis.
 */
class AudioEngine : public juce::AudioSource,
                    public juce::ChangeListener
{
public:
    AudioEngine();
    ~AudioEngine() override;
    
    // AudioSource interface
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    
    // ChangeListener
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    
    // Playback control
    void setProject(Project* proj) { project = proj; }
    void loadWaveform(const juce::AudioBuffer<float>& buffer, int sampleRate);
    
    void play();
    void pause();
    void stop();
    void seek(double timeSeconds);
    
    bool isPlaying() const { return playing; }
    double getPosition() const;  // Returns position in seconds
    double getDuration() const;
    
    // Callbacks
    using PositionCallback = std::function<void(double)>;
    using FinishCallback = std::function<void()>;
    
    void setPositionCallback(PositionCallback callback) { positionCallback = std::move(callback); }
    void setFinishCallback(FinishCallback callback) { finishCallback = std::move(callback); }
    
    // Audio device management
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    void initializeAudio();
    void shutdownAudio();
    
private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    
    Project* project = nullptr;
    juce::AudioBuffer<float> currentWaveform;
    int waveformSampleRate = 44100;
    
    std::atomic<int64_t> currentPosition { 0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> shouldStop { false };
    
    PositionCallback positionCallback;
    FinishCallback finishCallback;
    
    double currentSampleRate = 44100.0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
