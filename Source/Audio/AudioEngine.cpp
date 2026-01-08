#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
}

AudioEngine::~AudioEngine()
{
    shutdownAudio();
}

void AudioEngine::initializeAudio()
{
    // Initialize audio device
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);  // No input, stereo output
    
    if (result.isNotEmpty())
    {
        DBG("Audio device initialization error: " + result);
    }
    else
    {
        DBG("Audio device initialized successfully");
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device)
        {
            DBG("Device name: " + device->getName());
            DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
            DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
        }
    }
    
    deviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
}

void AudioEngine::shutdownAudio()
{
    audioSourcePlayer.setSource(nullptr);
    deviceManager.removeAudioCallback(&audioSourcePlayer);
    deviceManager.closeAudioDevice();
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
}

void AudioEngine::releaseResources()
{
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (!playing || currentWaveform.getNumSamples() == 0)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    auto* outputBuffer = bufferToFill.buffer;
    auto numSamples = bufferToFill.numSamples;
    auto startSample = bufferToFill.startSample;
    
    int64_t pos = currentPosition.load();
    int64_t waveformLength = currentWaveform.getNumSamples();
    
    int samplesToProcess = numSamples;
    
    if (pos >= waveformLength)
    {
        bufferToFill.clearActiveBufferRegion();
        playing = false;
        
        // Schedule callback on message thread
        if (finishCallback)
        {
            juce::MessageManager::callAsync([this]() {
                if (finishCallback)
                    finishCallback();
            });
        }
        return;
    }
    
    // Check if we'll reach the end
    if (pos + samplesToProcess > waveformLength)
    {
        samplesToProcess = static_cast<int>(waveformLength - pos);
    }
    
    // Copy samples to output buffer
    for (int ch = 0; ch < outputBuffer->getNumChannels(); ++ch)
    {
        // Copy from mono source to all channels
        outputBuffer->copyFrom(ch, startSample,
                               currentWaveform.getReadPointer(0, static_cast<int>(pos)),
                               samplesToProcess);
        
        // Clear remaining samples if any
        if (samplesToProcess < numSamples)
        {
            outputBuffer->clear(ch, startSample + samplesToProcess, 
                               numSamples - samplesToProcess);
        }
    }
    
    currentPosition.store(pos + samplesToProcess);
    
    // Update position callback
    if (positionCallback)
    {
        double posSeconds = static_cast<double>(currentPosition.load()) / waveformSampleRate;
        juce::MessageManager::callAsync([this, posSeconds]() {
            if (positionCallback)
                positionCallback(posSeconds);
        });
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
}

void AudioEngine::loadWaveform(const juce::AudioBuffer<float>& buffer, int sampleRate)
{
    stop();
    currentWaveform = buffer;
    waveformSampleRate = sampleRate;
    currentPosition.store(0);
    
    DBG("Loaded waveform: " + juce::String(buffer.getNumSamples()) + " samples at " + 
        juce::String(sampleRate) + " Hz");
}

void AudioEngine::play()
{
    if (currentWaveform.getNumSamples() == 0)
    {
        DBG("Cannot play: no waveform loaded");
        return;
    }
    
    DBG("Starting playback from position: " + juce::String(currentPosition.load()));
    playing = true;
}

void AudioEngine::pause()
{
    playing = false;
}

void AudioEngine::stop()
{
    playing = false;
    currentPosition.store(0);
}

void AudioEngine::seek(double timeSeconds)
{
    int64_t newPos = static_cast<int64_t>(timeSeconds * waveformSampleRate);
    newPos = juce::jlimit<int64_t>(0, currentWaveform.getNumSamples(), newPos);
    currentPosition.store(newPos);
}

double AudioEngine::getPosition() const
{
    return static_cast<double>(currentPosition.load()) / waveformSampleRate;
}

double AudioEngine::getDuration() const
{
    if (currentWaveform.getNumSamples() == 0)
        return 0.0;
    return static_cast<double>(currentWaveform.getNumSamples()) / waveformSampleRate;
}
