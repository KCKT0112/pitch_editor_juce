#include "IncrementalSynthesizer.h"

IncrementalSynthesizer::IncrementalSynthesizer() = default;

IncrementalSynthesizer::~IncrementalSynthesizer() {
    cancel();
}

void IncrementalSynthesizer::cancel() {
    if (cancelFlag)
        cancelFlag->store(true);
}

void IncrementalSynthesizer::synthesizeDirtyRegion(ProgressCallback onProgress,
                                                   CompleteCallback onComplete) {
    if (!project || !vocoder) {
        if (onComplete) onComplete(false);
        return;
    }

    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
        if (onComplete) onComplete(false);
        return;
    }

    if (!vocoder->isLoaded()) {
        if (onComplete) onComplete(false);
        return;
    }

    // Check for dirty regions
    if (!project->hasDirtyNotes() && !project->hasF0DirtyRange()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
    if (dirtyStart < 0 || dirtyEnd < 0) {
        if (onComplete) onComplete(false);
        return;
    }

    // Add padding for smooth transitions
    int startFrame = std::max(0, dirtyStart - paddingFrames);
    int endFrame = std::min(static_cast<int>(audioData.melSpectrogram.size()),
                            dirtyEnd + paddingFrames);

    // Extract mel spectrogram range
    std::vector<std::vector<float>> melRange(
        audioData.melSpectrogram.begin() + startFrame,
        audioData.melSpectrogram.begin() + endFrame);

    // Get adjusted F0 for range
    std::vector<float> adjustedF0Range = project->getAdjustedF0ForRange(startFrame, endFrame);

    if (melRange.empty() || adjustedF0Range.empty()) {
        if (onComplete) onComplete(false);
        return;
    }

    if (onProgress) onProgress("Synthesizing...");

    // Cancel previous job
    if (cancelFlag)
        cancelFlag->store(true);
    cancelFlag = std::make_shared<std::atomic<bool>>(false);
    uint64_t currentJobId = ++jobId;

    isBusy = true;

    // Calculate sample positions
    int hopSize = vocoder->getHopSize();
    int startSample = startFrame * hopSize;
    int endSample = endFrame * hopSize;

    // Capture for lambda
    auto capturedCancelFlag = cancelFlag;
    auto capturedProject = project;
    int capturedStartSample = startSample;
    int capturedEndSample = endSample;
    int capturedHopSize = hopSize;

    // Run vocoder inference asynchronously
    vocoder->inferAsync(
        melRange, adjustedF0Range,
        [this, capturedCancelFlag, capturedProject, capturedStartSample, capturedEndSample,
         capturedHopSize, currentJobId, onComplete](std::vector<float> synthesizedAudio) {

            // Check if cancelled or superseded
            if (capturedCancelFlag->load() || currentJobId != jobId.load()) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            if (synthesizedAudio.empty()) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            auto& audioData = capturedProject->getAudioData();
            int totalSamples = audioData.waveform.getNumSamples();

            // Verify output length
            int expectedSamples = capturedEndSample - capturedStartSample;
            int actualSamples = static_cast<int>(synthesizedAudio.size());

            if (std::abs(actualSamples - expectedSamples) > capturedHopSize * 2) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            // Apply synthesized audio with crossfade
            int replaceStartSample = capturedStartSample;
            int replaceSamples = std::min(actualSamples, totalSamples - replaceStartSample);

            if (replaceSamples <= 0) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            int numChannels = audioData.waveform.getNumChannels();
            int crossfadeSamples = paddingFrames * capturedHopSize / 2;
            crossfadeSamples = std::min(crossfadeSamples, actualSamples / 4);

            for (int i = 0; i < replaceSamples && (replaceStartSample + i) < totalSamples; ++i) {
                int dstIdx = replaceStartSample + i;
                float srcVal = synthesizedAudio[i];

                // Calculate crossfade factor
                float factor = 1.0f;
                if (i < crossfadeSamples && replaceStartSample > 0) {
                    factor = static_cast<float>(i) / crossfadeSamples;
                } else if (i >= replaceSamples - crossfadeSamples &&
                           (replaceStartSample + replaceSamples) < totalSamples) {
                    factor = static_cast<float>(replaceSamples - 1 - i) / crossfadeSamples;
                }

                for (int ch = 0; ch < numChannels; ++ch) {
                    float* dstCh = audioData.waveform.getWritePointer(ch);
                    if (factor < 1.0f) {
                        dstCh[dstIdx] = dstCh[dstIdx] * (1.0f - factor) + srcVal * factor;
                    } else {
                        dstCh[dstIdx] = srcVal;
                    }
                }
            }

            // Clear dirty flags
            capturedProject->clearAllDirty();

            isBusy = false;
            if (onComplete) onComplete(true);
        });
}

void IncrementalSynthesizer::applyCrossfade(juce::AudioBuffer<float>& waveform,
                                            const std::vector<float>& synthesized,
                                            int startSample, int crossfadeSamples) {
    int numChannels = waveform.getNumChannels();
    int totalSamples = waveform.getNumSamples();
    int synthSize = static_cast<int>(synthesized.size());

    for (int i = 0; i < synthSize && (startSample + i) < totalSamples; ++i) {
        int dstIdx = startSample + i;
        float srcVal = synthesized[i];

        float factor = 1.0f;
        if (i < crossfadeSamples && startSample > 0) {
            factor = static_cast<float>(i) / crossfadeSamples;
        } else if (i >= synthSize - crossfadeSamples && (startSample + synthSize) < totalSamples) {
            factor = static_cast<float>(synthSize - 1 - i) / crossfadeSamples;
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            float* dst = waveform.getWritePointer(ch);
            if (factor < 1.0f) {
                dst[dstIdx] = dst[dstIdx] * (1.0f - factor) + srcVal * factor;
            } else {
                dst[dstIdx] = srcVal;
            }
        }
    }
}
