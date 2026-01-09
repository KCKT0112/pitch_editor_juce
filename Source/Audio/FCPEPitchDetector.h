#pragma once

#include "../JuceHeader.h"
#include <vector>
#include <array>
#include <memory>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

// GPU execution provider types
enum class GPUProvider
{
    CPU = 0,
    CUDA,      // NVIDIA GPU
    DirectML   // Windows DirectX 12 (AMD/Intel/NVIDIA)
};

/**
 * FCPE (F0 Contour Pitch Estimator) - Deep learning based pitch detector.
 * Uses ONNX Runtime for inference.
 *
 * This implementation matches the PyTorch FCPE model's mel extraction
 * and post-processing to ensure consistent results.
 */
class FCPEPitchDetector
{
public:
    // FCPE configuration constants
    static constexpr float F0_MIN = 32.7f;
    static constexpr float F0_MAX = 1975.5f;
    static constexpr int OUT_DIMS = 360;
    static constexpr int INPUT_CHANNELS = 128;
    
    // Mel extraction constants (must match training)
    static constexpr int FCPE_SAMPLE_RATE = 16000;
    static constexpr int N_MELS = 128;
    static constexpr int N_FFT = 1024;
    static constexpr int WIN_SIZE = 1024;
    static constexpr int HOP_SIZE = 160;
    static constexpr float FMIN = 0.0f;
    static constexpr float FMAX = 8000.0f;
    static constexpr float CLIP_VAL = 1e-5f;
    
    FCPEPitchDetector();
    ~FCPEPitchDetector();
    
    /**
     * Load FCPE model from ONNX file.
     * @param modelPath Path to fcpe.onnx
     * @param melFilterbankPath Path to mel_filterbank.bin (optional)
     * @param centTablePath Path to cent_table.bin (optional)
     * @param provider GPU provider (CPU, CUDA, or DirectML)
     * @param deviceId GPU device ID (0 = first GPU)
     * @return true if successful
     */
    bool loadModel(const juce::File& modelPath,
                   const juce::File& melFilterbankPath = juce::File(),
                   const juce::File& centTablePath = juce::File(),
                   GPUProvider provider = GPUProvider::CPU,
                   int deviceId = 0);
    
    /**
     * Check if model is loaded.
     */
    bool isLoaded() const { return loaded; }
    
    /**
     * Extract F0 from audio buffer.
     * The audio will be resampled to 16kHz internally.
     * 
     * @param audio Audio samples
     * @param numSamples Number of samples
     * @param sampleRate Original sample rate
     * @param threshold Confidence threshold (default 0.05)
     * @return F0 values in Hz (0 for unvoiced frames)
     */
    std::vector<float> extractF0(const float* audio, int numSamples, 
                                  int sampleRate, float threshold = 0.05f);
    
    /**
     * Get the number of F0 frames that will be produced for given audio length.
     */
    int getNumFrames(int numSamples, int sampleRate) const;
    
    /**
     * Get the time in seconds for a given frame index.
     */
    float getTimeForFrame(int frameIndex) const;
    
    /**
     * Get hop size in original sample rate.
     */
    int getHopSizeForSampleRate(int sampleRate) const;
    
private:
    bool loaded = false;
    
    // Mel filterbank matrix [N_MELS x (N_FFT/2+1)]
    std::vector<std::vector<float>> melFilterbank;
    
    // Hann window [WIN_SIZE]
    std::vector<float> hannWindow;
    
    // Cent table for decoding [OUT_DIMS]
    std::vector<float> centTable;
    
    // Initialize mel filterbank (Slaney normalization to match librosa)
    void initMelFilterbank();
    
    // Initialize Hann window
    void initHannWindow();
    
    // Initialize cent table
    void initCentTable();
    
    // Resample audio to 16kHz
    std::vector<float> resampleTo16k(const float* audio, int numSamples, int srcRate);
    
    // Extract mel spectrogram
    std::vector<std::vector<float>> extractMel(const std::vector<float>& audio);
    
    // Decode latent to F0 (local argmax decoder)
    std::vector<float> decodeF0(const std::vector<std::vector<float>>& latent, 
                                 float threshold);
    
    // Convert cent to F0
    static float centToF0(float cent) {
        return 10.0f * std::pow(2.0f, cent / 1200.0f);
    }
    
    // Convert F0 to cent
    static float f0ToCent(float f0) {
        return 1200.0f * std::log2(f0 / 10.0f);
    }
    
#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> onnxEnv;
    std::unique_ptr<Ort::Session> onnxSession;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator;
    
    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<std::string> inputNameStrings;
    std::vector<std::string> outputNameStrings;
#endif
};
