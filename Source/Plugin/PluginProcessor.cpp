#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../UI/MainComponent.h"

PitchEditorAudioProcessor::PitchEditorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

PitchEditorAudioProcessor::~PitchEditorAudioProcessor() = default;

const juce::String PitchEditorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PitchEditorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PitchEditorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PitchEditorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PitchEditorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PitchEditorAudioProcessor::getNumPrograms()
{
    return 1;
}

int PitchEditorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PitchEditorAudioProcessor::setCurrentProgram (int /*index*/)
{
}

const juce::String PitchEditorAudioProcessor::getProgramName (int /*index*/)
{
    return {};
}

void PitchEditorAudioProcessor::changeProgramName (int /*index*/, const juce::String& /*newName*/)
{
}

void PitchEditorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;

#if JucePlugin_Enable_ARA
    // ARA mode: let ARA handle audio
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#else
    juce::ignoreUnused(samplesPerBlock);
#endif

    // Pre-allocate capture buffer for 5 minutes (non-ARA mode)
    maxCaptureLength = static_cast<int>(sampleRate * 300);
    capturedBuffer.setSize(2, maxCaptureLength);
    capturedBuffer.clear();
    capturePosition = 0;
}

void PitchEditorAudioProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

#if ! JucePlugin_PreferredChannelConfigurations
bool PitchEditorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}
#endif

void PitchEditorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

#if JucePlugin_Enable_ARA
    // ARA mode: let ARA renderer handle audio
    if (processBlockForARA(buffer, isRealtime(), getPlayHead()))
        return;
#endif

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Capture mode: accumulate input audio
    if (capturing)
    {
        for (int ch = 0; ch < numChannels && ch < capturedBuffer.getNumChannels(); ++ch)
        {
            if (capturePosition + numSamples <= capturedBuffer.getNumSamples())
                capturedBuffer.copyFrom(ch, capturePosition, buffer, ch, 0, numSamples);
        }
        capturePosition += numSamples;

        // Stop capturing if buffer is full
        if (capturePosition >= maxCaptureLength)
            capturing = false;
    }

    // Playback mode: output processed audio instead of input
    if (processedReady && playbackPosition < processedBuffer.getNumSamples())
    {
        const int samplesToPlay = std::min(numSamples,
            processedBuffer.getNumSamples() - playbackPosition);

        for (int ch = 0; ch < numChannels && ch < processedBuffer.getNumChannels(); ++ch)
        {
            buffer.copyFrom(ch, 0, processedBuffer, ch, playbackPosition, samplesToPlay);
            // Clear remaining samples if processed audio is shorter
            if (samplesToPlay < numSamples)
                buffer.clear(ch, samplesToPlay, numSamples - samplesToPlay);
        }

        playbackPosition += numSamples;
    }
    // else: passthrough (input already in buffer)
}

bool PitchEditorAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PitchEditorAudioProcessor::createEditor()
{
    return new PitchEditorAudioProcessorEditor (*this);
}

void PitchEditorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (mainComponent && mainComponent->getProject())
    {
        auto xml = mainComponent->getProject()->toXml();
        if (xml)
            copyXmlToBinary(*xml, destData);
    }
}

void PitchEditorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && mainComponent && mainComponent->getProject())
        mainComponent->getProject()->fromXml(*xml);
}

void PitchEditorAudioProcessor::startCapture()
{
    capturedBuffer.clear();
    capturePosition = 0;
    processedReady = false;
    playbackPosition = 0;
    capturing = true;
}

void PitchEditorAudioProcessor::stopCapture()
{
    capturing = false;
    // Trim buffer to actual captured length
    if (capturePosition > 0 && capturePosition < capturedBuffer.getNumSamples())
        capturedBuffer.setSize(capturedBuffer.getNumChannels(), capturePosition, true);
}

void PitchEditorAudioProcessor::setProcessedAudio(const juce::AudioBuffer<float>& buffer)
{
    processedBuffer = buffer;
    playbackPosition = 0;
    processedReady = true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchEditorAudioProcessor();
}

#if JucePlugin_Enable_ARA
#include "ARADocumentController.h"

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<PitchEditorDocumentController>();
}
#endif
