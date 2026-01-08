#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * Pitch detector using YIN algorithm.
 * (For production, you'd want to integrate a proper pitch detection library)
 */
class PitchDetector
{
public:
    PitchDetector(int sampleRate = 44100, int hopSize = 512);
    ~PitchDetector() = default;
    
    /**
     * Extract F0 from audio buffer.
     * @param audio Audio samples
     * @param numSamples Number of samples
     * @return Pair of (f0 values, voiced mask)
     */
    std::pair<std::vector<float>, std::vector<bool>> 
    extractF0(const float* audio, int numSamples);
    
    void setSampleRate(int sr) { sampleRate = sr; }
    void setHopSize(int hop) { hopSize = hop; }
    void setF0Range(float min, float max) { f0Min = min; f0Max = max; }
    
private:
    float yinPitchDetect(const float* buffer, int bufferSize);
    float parabolicInterpolation(const std::vector<float>& d, int tau);
    
    int sampleRate;
    int hopSize;
    float f0Min = 50.0f;
    float f0Max = 1000.0f;
    float threshold = 0.1f;  // YIN threshold
    
    int windowSize = 2048;
};
