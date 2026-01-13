#include "PlaybackController.h"

PlaybackController::PlaybackController() = default;

void PlaybackController::setupCallbacks() {
    if (!audioEngine)
        return;

    audioEngine->setPositionCallback([this](double pos) {
        if (onPositionChanged)
            onPositionChanged(pos);
    });

    audioEngine->setFinishCallback([this]() {
        playing = false;
        if (onPlaybackFinished)
            onPlaybackFinished();
    });
}

void PlaybackController::play() {
    if (!audioEngine)
        return;

    audioEngine->play();
    playing = true;
}

void PlaybackController::pause() {
    if (!audioEngine)
        return;

    audioEngine->pause();
    playing = false;
}

void PlaybackController::stop() {
    if (!audioEngine)
        return;

    audioEngine->stop();
    playing = false;
}

void PlaybackController::seek(double timeSeconds) {
    if (!audioEngine)
        return;

    audioEngine->seek(timeSeconds);
}

double PlaybackController::getCurrentTime() const {
    if (!audioEngine)
        return 0.0;

    return audioEngine->getPosition();
}

double PlaybackController::getDuration() const {
    if (!audioEngine)
        return 0.0;

    return audioEngine->getDuration();
}

void PlaybackController::loadWaveform(const juce::AudioBuffer<float>& buffer, int sampleRate) {
    if (!audioEngine)
        return;

    audioEngine->loadWaveform(buffer, sampleRate);
}
