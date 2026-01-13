#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Project.h"
#include "../Vocoder.h"
#include <atomic>
#include <functional>
#include <memory>

/**
 * Handles incremental audio synthesis for edited regions.
 * Uses vocoder to resynthesize only the dirty (modified) portions of audio.
 */
class IncrementalSynthesizer {
public:
    using ProgressCallback = std::function<void(const juce::String& message)>;
    using CompleteCallback = std::function<void(bool success)>;

    IncrementalSynthesizer();
    ~IncrementalSynthesizer();

    void setVocoder(Vocoder* v) { vocoder = v; }
    void setProject(Project* p) { project = p; }

    // Synthesize dirty regions
    void synthesizeDirtyRegion(ProgressCallback onProgress, CompleteCallback onComplete);

    // Cancel ongoing synthesis
    void cancel();

    // Check if synthesis is in progress
    bool isSynthesizing() const { return isBusy.load(); }

    // Get the current job ID (for tracking)
    uint64_t getCurrentJobId() const { return jobId.load(); }

private:
    // Apply crossfade at boundaries
    void applyCrossfade(juce::AudioBuffer<float>& waveform,
                       const std::vector<float>& synthesized,
                       int startSample, int crossfadeSamples);

    Vocoder* vocoder = nullptr;
    Project* project = nullptr;

    std::shared_ptr<std::atomic<bool>> cancelFlag;
    std::atomic<uint64_t> jobId{0};
    std::atomic<bool> isBusy{false};

    // Synthesis parameters
    static constexpr int paddingFrames = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IncrementalSynthesizer)
};
