#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * Mel spectrogram computation.
 */
class MelSpectrogram
{
public:
    MelSpectrogram(int sampleRate = 44100, int nFft = 2048, int hopSize = 512,
                   int numMels = 128, float fMin = 40.0f, float fMax = 16000.0f);
    ~MelSpectrogram() = default;
    
    /**
     * Compute mel spectrogram from audio.
     * @param audio Audio samples
     * @param numSamples Number of samples
     * @return Mel spectrogram [T, numMels] in log scale
     */
    std::vector<std::vector<float>> compute(const float* audio, int numSamples);
    
private:
    void createMelFilterbank();
    void applyWindow(std::vector<float>& frame);
    std::vector<float> computeFFT(const std::vector<float>& frame);
    
    int sampleRate;
    int nFft;
    int hopSize;
    int numMels;
    float fMin;
    float fMax;
    
    std::vector<float> window;  // Hann window
    std::vector<std::vector<float>> melFilterbank;
    
    juce::dsp::FFT fft;
};
