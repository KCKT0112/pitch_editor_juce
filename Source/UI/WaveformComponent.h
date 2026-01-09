#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"

class WaveformComponent : public juce::Component,
                          public juce::ScrollBar::Listener
{
public:
    WaveformComponent();
    ~WaveformComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    
    void setProject(Project* project);
    void setCursorTime(double time);
    void setPixelsPerSecond(float pps);
    void setScrollX(double x);
    
    double getScrollX() const { return scrollX; }
    float getPixelsPerSecond() const { return pixelsPerSecond; }
    
    std::function<void(double)> onSeek;
    std::function<void(float)> onZoomChanged;
    std::function<void(double)> onScrollChanged;
    
private:
    void drawWaveform(juce::Graphics& g);
    void drawCursor(juce::Graphics& g);
    void updateScrollBar();
    void rebuildWaveformCache();
    
    float timeToX(double time) const;
    double xToTime(float x) const;
    
    Project* project = nullptr;
    
    double scrollX = 0.0;
    float pixelsPerSecond = 100.0f;
    double cursorTime = 0.0;
    
    // Waveform cache
    juce::AudioBuffer<float> waveformCache;
    int cacheResolution = 512;  // Samples per pixel at base zoom
    
    juce::ScrollBar horizontalScrollBar { false };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};
