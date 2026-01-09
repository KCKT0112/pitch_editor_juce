#pragma once

#include "../JuceHeader.h"
#include <atomic>

class MainComponent;

class PitchEditorAudioProcessor : public juce::AudioProcessor
#if JucePlugin_Enable_ARA
                                 , public juce::AudioProcessorARAExtension
#endif
{
public:
    PitchEditorAudioProcessor();
    ~PitchEditorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #if ! JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Capture/playback control
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return capturing; }

    juce::AudioBuffer<float>& getCapturedAudio() { return capturedBuffer; }
    void setProcessedAudio(const juce::AudioBuffer<float>& buffer);
    bool hasProcessedAudio() const { return processedReady; }
    void resetPlayback() { playbackPosition = 0; }

    double getHostSampleRate() const { return hostSampleRate; }

    // Editor connection
    void setMainComponent(MainComponent* mc) { mainComponent = mc; }

private:
    // Capture state
    std::atomic<bool> capturing { false };
    juce::AudioBuffer<float> capturedBuffer;
    int capturePosition = 0;
    int maxCaptureLength = 0;

    // Processed audio
    juce::AudioBuffer<float> processedBuffer;
    std::atomic<bool> processedReady { false };
    int playbackPosition = 0;

    double hostSampleRate = 44100.0;
    MainComponent* mainComponent = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchEditorAudioProcessor)
};
