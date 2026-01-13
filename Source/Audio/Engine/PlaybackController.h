#pragma once

#include "../../JuceHeader.h"
#include "../AudioEngine.h"
#include <functional>

/**
 * Controls audio playback operations.
 * Wraps AudioEngine with a simpler interface and manages playback state.
 */
class PlaybackController {
public:
    PlaybackController();
    ~PlaybackController() = default;

    void setAudioEngine(AudioEngine* engine) { audioEngine = engine; }

    // Playback control
    void play();
    void pause();
    void stop();
    void seek(double timeSeconds);

    // State queries
    bool isPlaying() const { return playing; }
    double getCurrentTime() const;
    double getDuration() const;

    // Load waveform into engine
    void loadWaveform(const juce::AudioBuffer<float>& buffer, int sampleRate);

    // Callbacks
    std::function<void(double)> onPositionChanged;
    std::function<void()> onPlaybackFinished;

    // Setup callbacks on audio engine
    void setupCallbacks();

private:
    AudioEngine* audioEngine = nullptr;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackController)
};
