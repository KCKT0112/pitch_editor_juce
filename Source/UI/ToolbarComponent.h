#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"

class ToolbarComponent : public juce::Component,
                         public juce::Button::Listener,
                         public juce::Slider::Listener
{
public:
    ToolbarComponent();
    ~ToolbarComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    
    void setPlaying(bool playing);
    void setCurrentTime(double time);
    void setTotalTime(double time);
    
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void()> onOpenFile;
    std::function<void()> onExportFile;
    std::function<void()> onResynthesize;
    std::function<void(float)> onZoomChanged;
    
private:
    void updateTimeDisplay();
    juce::String formatTime(double seconds);
    
    juce::TextButton openButton { "Open" };
    juce::TextButton exportButton { "Export" };
    
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton resynthButton { "Resynth" };
    
    juce::Label timeLabel;
    
    juce::Slider zoomSlider;
    juce::Label zoomLabel { {}, "Zoom:" };
    
    double currentTime = 0.0;
    double totalTime = 0.0;
    bool isPlaying = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};
