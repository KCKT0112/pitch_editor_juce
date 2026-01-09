#pragma once

#include "../JuceHeader.h"

#if JucePlugin_Enable_ARA

class MainComponent;

//==============================================================================
class PitchEditorPlaybackRenderer : public juce::ARAPlaybackRenderer
{
public:
    using ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay(double sampleRateIn, int maximumSamplesPerBlockIn,
                      int numChannelsIn, juce::AudioProcessor::ProcessingPrecision,
                      AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;
    bool processBlock(juce::AudioBuffer<float>& buffer,
                     juce::AudioProcessor::Realtime realtime,
                     const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

private:
    std::map<juce::ARAAudioSource*, std::unique_ptr<juce::ARAAudioSourceReader>> audioSourceReaders;
    std::unique_ptr<juce::AudioBuffer<float>> tempBuffer;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int maximumSamplesPerBlock = 512;
    bool useBufferedReader = true;
};

//==============================================================================
class PitchEditorDocumentController : public juce::ARADocumentControllerSpecialisation
{
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    // Called when audio source is added - trigger analysis
    void didAddAudioSourceToDocument(juce::ARADocument* doc, juce::ARAAudioSource* audioSource);

    // Re-analyze: re-read audio from ARA and trigger analysis
    void reanalyze();

    // MainComponent connection for analysis
    void setMainComponent(MainComponent* mc) { mainComponent = mc; }
    MainComponent* getMainComponent() const { return mainComponent; }

protected:
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                    const juce::ARARestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToStream(juce::ARAOutputStream& output,
                               const juce::ARAStoreObjectsFilter* filter) noexcept override;

private:
    MainComponent* mainComponent = nullptr;
    juce::ARAAudioSource* currentAudioSource = nullptr;
};

#endif // JucePlugin_Enable_ARA
