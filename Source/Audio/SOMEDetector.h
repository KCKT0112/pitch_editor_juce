#pragma once

#include "../JuceHeader.h"
#include <vector>
#include <memory>
#include <functional>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

class SOMEDetector
{
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int HOP_SIZE = 512;

    struct NoteEvent {
        int startFrame;
        int endFrame;
        float midiNote;
        bool isRest;
    };

    SOMEDetector();
    ~SOMEDetector();

    bool loadModel(const juce::File& modelPath);
    bool isLoaded() const { return loaded; }

    std::vector<NoteEvent> detectNotes(const float* audio, int numSamples, int sampleRate);
    std::vector<NoteEvent> detectNotesWithProgress(const float* audio, int numSamples,
                                                    int sampleRate,
                                                    std::function<void(double)> progressCallback);

    // Streaming detection - calls noteCallback for each chunk's notes as they're detected
    void detectNotesStreaming(const float* audio, int numSamples, int sampleRate,
                              std::function<void(const std::vector<NoteEvent>&)> noteCallback,
                              std::function<void(double)> progressCallback);

    int getFrameForSample(int sampleIndex) const { return sampleIndex / HOP_SIZE; }
    int getSampleForFrame(int frameIndex) const { return frameIndex * HOP_SIZE; }

private:
    bool loaded = false;

    std::vector<float> resampleTo44k(const float* audio, int numSamples, int srcRate);

    // Slicer
    using MarkerList = std::vector<std::pair<int64_t, int64_t>>;
    MarkerList sliceAudio(const std::vector<float>& samples) const;
    static std::vector<double> getRms(const std::vector<float>& samples, int frameLength, int hopLength);

    // Single chunk inference
    bool inferChunk(const std::vector<float>& chunk, std::vector<float>& midi,
                    std::vector<bool>& rest, std::vector<float>& dur);

#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> onnxEnv;
    std::unique_ptr<Ort::Session> onnxSession;

    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<std::string> inputNameStrings;
    std::vector<std::string> outputNameStrings;
#endif
};
