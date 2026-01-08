#pragma once

#include "../JuceHeader.h"
#include <vector>
#include <functional>
#include <memory>
#include <fstream>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

/**
 * PC-NSF-HiFiGAN Vocoder wrapper using ONNX Runtime.
 * Converts mel spectrogram + F0 to waveform with pitch control.
 */
class Vocoder
{
public:
    Vocoder();
    ~Vocoder();
    
    /**
     * Load vocoder model from ONNX file.
     * @param modelPath Path to .onnx model file
     * @return true if successful
     */
    bool loadModel(const juce::File& modelPath);
    
    /**
     * Check if model is loaded.
     */
    bool isLoaded() const { return loaded; }
    
    /**
     * Check if ONNX Runtime is available.
     */
    static bool isOnnxRuntimeAvailable();
    
    /**
     * Synthesize waveform from mel spectrogram and F0.
     * @param mel Mel spectrogram [T, NUM_MELS] (T frames, each with NUM_MELS values)
     * @param f0 F0 values [T] (fundamental frequency per frame)
     * @return Synthesized waveform, or empty vector on failure
     */
    std::vector<float> infer(const std::vector<std::vector<float>>& mel,
                              const std::vector<float>& f0);
    
    /**
     * Synthesize with pitch shift.
     * @param mel Mel spectrogram
     * @param f0 F0 values
     * @param pitchShiftSemitones Pitch shift in semitones (+12 = one octave up)
     * @return Synthesized waveform
     */
    std::vector<float> inferWithPitchShift(const std::vector<std::vector<float>>& mel,
                                            const std::vector<float>& f0,
                                            float pitchShiftSemitones);
    
    /**
     * Asynchronous inference with callback.
     * @param mel Mel spectrogram
     * @param f0 F0 values
     * @param callback Called with result on completion
     */
    void inferAsync(const std::vector<std::vector<float>>& mel,
                    const std::vector<float>& f0,
                    std::function<void(std::vector<float>)> callback);
    
    // Model parameters
    int getSampleRate() const { return sampleRate; }
    int getHopSize() const { return hopSize; }
    int getNumMels() const { return numMels; }
    bool isPitchControllable() const { return pitchControllable; }
    
private:
    bool loaded = false;
    int sampleRate = 44100;
    int hopSize = 512;
    int numMels = 128;
    bool pitchControllable = true;
    
    juce::File modelFile;
    std::unique_ptr<std::ofstream> logFile;
    
    void log(const std::string& message);
    
#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> onnxEnv;
    std::unique_ptr<Ort::Session> onnxSession;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator;
    
    // Input/output names (cached)
    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<std::string> inputNameStrings;
    std::vector<std::string> outputNameStrings;
#endif
    
    /**
     * Generate simple sine wave fallback when ONNX is not available.
     */
    std::vector<float> generateSineFallback(const std::vector<float>& f0);
};
