#include "WaveformComponent.h"

WaveformComponent::WaveformComponent()
{
    addAndMakeVisible(horizontalScrollBar);
    horizontalScrollBar.addListener(this);
}

WaveformComponent::~WaveformComponent()
{
}

void WaveformComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF16161E));
    
    drawWaveform(g);
    drawCursor(g);
    
    // Border
    g.setColour(juce::Colour(0xFF3D3D47));
    g.drawRect(getLocalBounds());
}

void WaveformComponent::resized()
{
    horizontalScrollBar.setBounds(0, getHeight() - 14, getWidth(), 14);
    updateScrollBar();
}

void WaveformComponent::drawWaveform(juce::Graphics& g)
{
    if (!project) return;
    
    const auto& audioData = project->getAudioData();
    if (audioData.waveform.getNumSamples() == 0) return;
    
    auto bounds = getLocalBounds().withTrimmedBottom(14);
    float centerY = static_cast<float>(bounds.getCentreY());
    float amplitude = bounds.getHeight() * 0.4f;
    
    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();
    
    // Calculate visible range
    double startTime = xToTime(static_cast<float>(scrollX));
    double endTime = xToTime(static_cast<float>(scrollX + bounds.getWidth()));
    
    int startSample = juce::jmax(0, static_cast<int>(startTime * SAMPLE_RATE));
    int endSample = juce::jmin(numSamples - 1, static_cast<int>(endTime * SAMPLE_RATE));
    
    if (startSample >= endSample) return;
    
    // Samples per pixel
    juce::ignoreUnused(startSample, endSample);
    
    g.setColour(juce::Colour(COLOR_WAVEFORM));
    
    for (int x = 0; x < bounds.getWidth(); ++x)
    {
        double time = xToTime(static_cast<float>(scrollX + x));
        int sampleStart = static_cast<int>(time * SAMPLE_RATE);
        int sampleEnd = static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);
        
        sampleStart = juce::jmax(0, sampleStart);
        sampleEnd = juce::jmin(numSamples - 1, sampleEnd);
        
        if (sampleStart >= numSamples || sampleEnd < 0) continue;
        
        // Find min/max in this range
        float minVal = 0.0f, maxVal = 0.0f;
        for (int i = sampleStart; i <= sampleEnd; ++i)
        {
            float s = samples[i];
            if (s < minVal) minVal = s;
            if (s > maxVal) maxVal = s;
        }
        
        float yMin = centerY - maxVal * amplitude;
        float yMax = centerY - minVal * amplitude;
        
        g.drawVerticalLine(x, yMin, yMax);
    }
}

void WaveformComponent::drawCursor(juce::Graphics& g)
{
    float x = timeToX(cursorTime) - static_cast<float>(scrollX);
    
    auto bounds = getLocalBounds().withTrimmedBottom(14);
    
    if (x >= 0 && x < bounds.getWidth())
    {
        g.setColour(juce::Colours::red);
        g.drawVerticalLine(static_cast<int>(x), 0, static_cast<float>(bounds.getHeight()));
    }
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.y >= getHeight() - 14) return;  // Clicked on scrollbar
    
    double time = xToTime(e.x + scrollX);
    cursorTime = std::max(0.0, time);
    
    if (onSeek)
        onSeek(cursorTime);
    
    repaint();
}

void WaveformComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        // Zoom
        float zoomFactor = 1.0f + wheel.deltaY * 0.1f;
        float newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND,
                                     pixelsPerSecond * zoomFactor);
        
        if (newPps != pixelsPerSecond)
        {
            pixelsPerSecond = newPps;
            updateScrollBar();
            
            if (onZoomChanged)
                onZoomChanged(pixelsPerSecond);
            
            repaint();
        }
    }
    else
    {
        // Scroll
        horizontalScrollBar.setCurrentRangeStart(scrollX - wheel.deltaY * 50.0);
    }
}

void WaveformComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    scrollX = newRangeStart;
    
    if (onScrollChanged)
        onScrollChanged(scrollX);
    
    repaint();
}

void WaveformComponent::setProject(Project* proj)
{
    project = proj;
    updateScrollBar();
    repaint();
}

void WaveformComponent::setCursorTime(double time)
{
    cursorTime = time;
    repaint();
}

void WaveformComponent::setPixelsPerSecond(float pps)
{
    pixelsPerSecond = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, pps);
    updateScrollBar();
    repaint();
}

void WaveformComponent::setScrollX(double x)
{
    scrollX = x;
    horizontalScrollBar.setCurrentRangeStart(x);
    repaint();
}

float WaveformComponent::timeToX(double time) const
{
    return static_cast<float>(time * pixelsPerSecond);
}

double WaveformComponent::xToTime(float x) const
{
    return x / pixelsPerSecond;
}

void WaveformComponent::updateScrollBar()
{
    if (project)
    {
        float totalWidth = project->getAudioData().getDuration() * pixelsPerSecond;
        int visibleWidth = getWidth();
        
        horizontalScrollBar.setRangeLimits(0, totalWidth);
        horizontalScrollBar.setCurrentRange(scrollX, visibleWidth);
    }
}
