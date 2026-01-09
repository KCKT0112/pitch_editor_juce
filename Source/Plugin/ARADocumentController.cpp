#include "ARADocumentController.h"

#if JucePlugin_Enable_ARA

#include "../UI/MainComponent.h"

//==============================================================================
// PitchEditorPlaybackRenderer
//==============================================================================

void PitchEditorPlaybackRenderer::prepareToPlay(double sampleRateIn, int maximumSamplesPerBlockIn,
                                                 int numChannelsIn, juce::AudioProcessor::ProcessingPrecision,
                                                 AlwaysNonRealtime alwaysNonRealtime)
{
    sampleRate = sampleRateIn;
    numChannels = numChannelsIn;
    maximumSamplesPerBlock = maximumSamplesPerBlockIn;
    tempBuffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, maximumSamplesPerBlock);
    useBufferedReader = (alwaysNonRealtime == AlwaysNonRealtime::no);

    // Create readers for all playback regions
    for (const auto playbackRegion : getPlaybackRegions())
    {
        auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
        if (audioSourceReaders.find(audioSource) == audioSourceReaders.end())
        {
            audioSourceReaders.emplace(audioSource,
                std::make_unique<juce::ARAAudioSourceReader>(audioSource));
        }
    }
}

void PitchEditorPlaybackRenderer::releaseResources()
{
    audioSourceReaders.clear();
    tempBuffer.reset();
}

bool PitchEditorPlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::AudioProcessor::Realtime realtime,
                                                const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback(0);
    const auto isPlaying = positionInfo.getIsPlaying();

    bool success = true;
    bool didRenderAnyRegion = false;

    if (isPlaying)
    {
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength(timeInSamples, numSamples);

        for (const auto& playbackRegion : getPlaybackRegions())
        {
            const auto playbackSampleRange = playbackRegion->getSampleRange(sampleRate,
                juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith(playbackSampleRange);

            if (renderRange.isEmpty())
                continue;

            // Get modification sample range
            juce::Range<juce::int64> modificationSampleRange{
                playbackRegion->getStartInAudioModificationSamples(),
                playbackRegion->getEndInAudioModificationSamples()
            };
            const auto modificationSampleOffset = modificationSampleRange.getStart() - playbackSampleRange.getStart();

            renderRange = renderRange.getIntersectionWith(
                modificationSampleRange.movedToStartAt(playbackSampleRange.getStart()));

            if (renderRange.isEmpty())
                continue;

            // Get audio source reader
            const auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();
            const auto readerIt = audioSourceReaders.find(const_cast<juce::ARAAudioSource*>(audioSource));

            if (readerIt == audioSourceReaders.end())
            {
                success = false;
                continue;
            }

            auto& reader = readerIt->second;

            // Calculate buffer offsets
            const int numSamplesToRead = static_cast<int>(renderRange.getLength());
            const int startInBuffer = static_cast<int>(renderRange.getStart() - blockRange.getStart());
            auto startInSource = renderRange.getStart() + modificationSampleOffset;

            // Read samples
            auto& readBuffer = didRenderAnyRegion ? *tempBuffer : buffer;

            if (!reader->read(&readBuffer, startInBuffer, numSamplesToRead, startInSource, true, true))
            {
                success = false;
                continue;
            }

            // Mix if needed
            if (didRenderAnyRegion)
            {
                for (int c = 0; c < numChannels; ++c)
                    buffer.addFrom(c, startInBuffer, *tempBuffer, c, startInBuffer, numSamplesToRead);
            }
            else
            {
                // Clear excess
                if (startInBuffer != 0)
                    buffer.clear(0, startInBuffer);

                const int endInBuffer = startInBuffer + numSamplesToRead;
                const int remainingSamples = numSamples - endInBuffer;
                if (remainingSamples != 0)
                    buffer.clear(endInBuffer, remainingSamples);

                didRenderAnyRegion = true;
            }
        }
    }

    if (!didRenderAnyRegion)
        buffer.clear();

    return success;
}

//==============================================================================
// PitchEditorDocumentController
//==============================================================================

void PitchEditorDocumentController::didAddAudioSourceToDocument(juce::ARADocument* /*doc*/,
                                                                  juce::ARAAudioSource* audioSource)
{
    if (!mainComponent || !audioSource)
        return;

    // Store reference for re-analysis
    currentAudioSource = audioSource;

    // Read audio from ARA source and pass to MainComponent for analysis
    juce::ARAAudioSourceReader reader(audioSource);

    const auto numSamples = static_cast<int>(audioSource->getSampleCount());
    const auto numChannels = audioSource->getChannelCount();
    const auto sourceSampleRate = audioSource->getSampleRate();

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader.read(&buffer, 0, numSamples, 0, true, true);

    // Pass to MainComponent for analysis
    mainComponent->setHostAudio(buffer, sourceSampleRate);
}

void PitchEditorDocumentController::reanalyze()
{
    if (!mainComponent || !currentAudioSource)
        return;

    // Re-read audio from ARA source
    juce::ARAAudioSourceReader reader(currentAudioSource);

    const auto numSamples = static_cast<int>(currentAudioSource->getSampleCount());
    const auto numChannels = currentAudioSource->getChannelCount();
    const auto sourceSampleRate = currentAudioSource->getSampleRate();

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader.read(&buffer, 0, numSamples, 0, true, true);

    // Pass to MainComponent for analysis
    mainComponent->setHostAudio(buffer, sourceSampleRate);
}

juce::ARAPlaybackRenderer* PitchEditorDocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PitchEditorPlaybackRenderer(getDocumentController());
}

bool PitchEditorDocumentController::doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                                                const juce::ARARestoreObjectsFilter* /*filter*/) noexcept
{
    // Read project state
    const auto dataSize = input.readInt64();
    if (dataSize <= 0)
        return true;

    juce::MemoryBlock data;
    data.setSize(static_cast<size_t>(dataSize));
    input.read(data.getData(), static_cast<int>(dataSize));

    if (mainComponent && mainComponent->getProject())
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
        if (xml)
            mainComponent->getProject()->fromXml(*xml);
    }

    return !input.failed();
}

bool PitchEditorDocumentController::doStoreObjectsToStream(juce::ARAOutputStream& output,
                                                            const juce::ARAStoreObjectsFilter* /*filter*/) noexcept
{
    if (!mainComponent || !mainComponent->getProject())
    {
        output.writeInt64(0);
        return true;
    }

    auto xml = mainComponent->getProject()->toXml();
    if (!xml)
    {
        output.writeInt64(0);
        return true;
    }

    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);

    output.writeInt64(static_cast<juce::int64>(data.getSize()));
    return output.write(data.getData(), static_cast<int>(data.getSize()));
}

#endif // JucePlugin_Enable_ARA
