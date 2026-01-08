#include "PitchDetector.h"
#include <cmath>
#include <algorithm>

PitchDetector::PitchDetector(int sampleRate, int hopSize)
    : sampleRate(sampleRate), hopSize(hopSize)
{
    windowSize = std::max(2048, static_cast<int>(sampleRate / f0Min) * 2);
}

std::pair<std::vector<float>, std::vector<bool>> 
PitchDetector::extractF0(const float* audio, int numSamples)
{
    int numFrames = (numSamples - windowSize) / hopSize + 1;
    if (numFrames < 1)
    {
        numFrames = numSamples / hopSize;
        if (numFrames < 1) numFrames = 1;
    }
    
    std::vector<float> f0Values(numFrames, 0.0f);
    std::vector<bool> voicedMask(numFrames, false);
    
    for (int i = 0; i < numFrames; ++i)
    {
        int startSample = i * hopSize;
        int availableSamples = numSamples - startSample;
        int frameSamples = std::min(windowSize, availableSamples);
        
        if (frameSamples < 512)  // Too short for pitch detection
        {
            f0Values[i] = 0.0f;
            voicedMask[i] = false;
            continue;
        }
        
        float pitch = yinPitchDetect(audio + startSample, frameSamples);
        
        if (pitch > 0.0f && pitch >= f0Min && pitch <= f0Max)
        {
            f0Values[i] = pitch;
            voicedMask[i] = true;
        }
        else
        {
            f0Values[i] = 0.0f;
            voicedMask[i] = false;
        }
    }
    
    return { f0Values, voicedMask };
}

float PitchDetector::yinPitchDetect(const float* buffer, int bufferSize)
{
    int halfSize = bufferSize / 2;
    if (halfSize < 2) return -1.0f;
    
    std::vector<float> d(halfSize);
    
    // Step 2: Difference function
    for (int tau = 0; tau < halfSize; ++tau)
    {
        d[tau] = 0.0f;
        for (int j = 0; j < halfSize; ++j)
        {
            float diff = buffer[j] - buffer[j + tau];
            d[tau] += diff * diff;
        }
    }
    
    // Step 3: Cumulative mean normalized difference function
    std::vector<float> dPrime(halfSize);
    dPrime[0] = 1.0f;
    float runningSum = 0.0f;
    
    for (int tau = 1; tau < halfSize; ++tau)
    {
        runningSum += d[tau];
        dPrime[tau] = d[tau] * tau / runningSum;
    }
    
    // Step 4: Absolute threshold
    int tauMin = static_cast<int>(sampleRate / f0Max);
    int tauMax = std::min(halfSize - 1, static_cast<int>(sampleRate / f0Min));
    
    int tau = tauMin;
    while (tau < tauMax)
    {
        if (dPrime[tau] < threshold)
        {
            while (tau + 1 < tauMax && dPrime[tau + 1] < dPrime[tau])
                ++tau;
            break;
        }
        ++tau;
    }
    
    if (tau >= tauMax || dPrime[tau] >= threshold)
        return -1.0f;  // No pitch found
    
    // Step 5: Parabolic interpolation
    float betterTau = parabolicInterpolation(dPrime, tau);
    
    if (betterTau > 0.0f)
        return static_cast<float>(sampleRate) / betterTau;
    
    return -1.0f;
}

float PitchDetector::parabolicInterpolation(const std::vector<float>& d, int tau)
{
    if (tau < 1 || tau >= static_cast<int>(d.size()) - 1)
        return static_cast<float>(tau);
    
    float s0 = d[tau - 1];
    float s1 = d[tau];
    float s2 = d[tau + 1];
    
    float adjustment = (s2 - s0) / (2.0f * (2.0f * s1 - s2 - s0));
    
    if (std::abs(adjustment) > 1.0f)
        adjustment = 0.0f;
    
    return tau + adjustment;
}
