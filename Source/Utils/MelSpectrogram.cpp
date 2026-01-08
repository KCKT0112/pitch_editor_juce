#include "MelSpectrogram.h"
#include <cmath>
#include <algorithm>

MelSpectrogram::MelSpectrogram(int sampleRate, int nFft, int hopSize,
                               int numMels, float fMin, float fMax)
    : sampleRate(sampleRate), nFft(nFft), hopSize(hopSize),
      numMels(numMels), fMin(fMin), fMax(fMax),
      fft(static_cast<int>(std::log2(nFft)))
{
    // Create Hann window
    window.resize(nFft);
    for (int i = 0; i < nFft; ++i)
    {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (nFft - 1)));
    }
    
    createMelFilterbank();
}

void MelSpectrogram::createMelFilterbank()
{
    // Convert Hz to Mel (Slaney formula)
    auto hzToMel = [](float hz) {
        return 2595.0f * std::log10(1.0f + hz / 700.0f);
    };
    
    // Convert Mel to Hz
    auto melToHz = [](float mel) {
        return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
    };
    
    float melMin = hzToMel(fMin);
    float melMax = hzToMel(fMax);
    
    // Create mel points (numMels + 2 points for the triangular filters)
    std::vector<float> melPoints(numMels + 2);
    for (int i = 0; i <= numMels + 1; ++i)
    {
        melPoints[i] = melMin + (melMax - melMin) * i / (numMels + 1);
    }
    
    // Convert to Hz
    std::vector<float> hzPoints(numMels + 2);
    for (int i = 0; i <= numMels + 1; ++i)
    {
        hzPoints[i] = melToHz(melPoints[i]);
    }
    
    // Convert to FFT bins
    int numBins = nFft / 2 + 1;
    std::vector<float> binPoints(numMels + 2);
    for (int i = 0; i <= numMels + 1; ++i)
    {
        // Use continuous (fractional) bin numbers like librosa
        binPoints[i] = (nFft + 1) * hzPoints[i] / sampleRate;
    }
    
    // Create filterbank with Slaney normalization (area normalization)
    melFilterbank.resize(numMels);
    for (int m = 0; m < numMels; ++m)
    {
        melFilterbank[m].resize(numBins, 0.0f);
        
        float fLow = hzPoints[m];
        float fCenter = hzPoints[m + 1];
        float fHigh = hzPoints[m + 2];
        
        // Slaney normalization: divide by the width of the mel band
        float enorm = 2.0f / (fHigh - fLow);
        
        for (int k = 0; k < numBins; ++k)
        {
            float freq = static_cast<float>(k) * sampleRate / nFft;
            
            if (freq >= fLow && freq < fCenter)
            {
                // Rising edge
                melFilterbank[m][k] = enorm * (freq - fLow) / (fCenter - fLow);
            }
            else if (freq >= fCenter && freq <= fHigh)
            {
                // Falling edge
                melFilterbank[m][k] = enorm * (fHigh - freq) / (fHigh - fCenter);
            }
        }
    }
}

std::vector<std::vector<float>> MelSpectrogram::compute(const float* audio, int numSamples)
{
    int numFrames = (numSamples - nFft) / hopSize + 1;
    if (numFrames < 1)
    {
        numFrames = 1;
    }
    
    std::vector<std::vector<float>> mel(numFrames);
    int numBins = nFft / 2 + 1;
    
    std::vector<float> frame(nFft * 2, 0.0f);  // Complex FFT buffer
    
    for (int i = 0; i < numFrames; ++i)
    {
        int startSample = i * hopSize;
        
        // Copy and window
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (int j = 0; j < nFft && startSample + j < numSamples; ++j)
        {
            frame[j] = audio[startSample + j] * window[j];
        }
        
        // Perform FFT
        fft.performRealOnlyForwardTransform(frame.data());
        
        // Compute magnitude spectrum
        std::vector<float> mag(numBins);
        for (int k = 0; k < numBins; ++k)
        {
            float real = frame[k * 2];
            float imag = frame[k * 2 + 1];
            mag[k] = std::sqrt(real * real + imag * imag);
        }
        
        // Apply mel filterbank
        mel[i].resize(numMels);
        for (int m = 0; m < numMels; ++m)
        {
            float sum = 0.0f;
            for (int k = 0; k < numBins; ++k)
            {
                sum += mag[k] * melFilterbank[m][k];
            }
            
            // Log scale (natural log for vocoder compatibility)
            mel[i][m] = std::log(std::max(sum, 1e-5f));
        }
    }
    
    return mel;
}
