#include "ToolbarComponent.h"

ToolbarComponent::ToolbarComponent()
{
    // Configure buttons
    addAndMakeVisible(openButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(resynthButton);
    
    openButton.addListener(this);
    exportButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    resynthButton.addListener(this);
    
    // Style buttons
    auto buttonColor = juce::Colour(0xFF3D3D47);
    auto textColor = juce::Colours::white;
    
    for (auto* btn : { &openButton, &exportButton, &playButton, &stopButton, &resynthButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, buttonColor);
        btn->setColour(juce::TextButton::textColourOffId, textColor);
    }
    
    // Time label
    addAndMakeVisible(timeLabel);
    timeLabel.setText("00:00.000 / 00:00.000", juce::dontSendNotification);
    timeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    timeLabel.setJustificationType(juce::Justification::centred);
    
    // Zoom slider
    addAndMakeVisible(zoomLabel);
    addAndMakeVisible(zoomSlider);
    
    zoomLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    
    zoomSlider.setRange(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, 1.0);
    zoomSlider.setValue(100.0);
    zoomSlider.setSkewFactorFromMidPoint(200.0);
    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.addListener(this);
    
    zoomSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xFF3D3D47));
    zoomSlider.setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));
}

ToolbarComponent::~ToolbarComponent()
{
}

void ToolbarComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A24));
    
    // Bottom border
    g.setColour(juce::Colour(0xFF3D3D47));
    g.drawHorizontalLine(getHeight() - 1, 0, static_cast<float>(getWidth()));
}

void ToolbarComponent::resized()
{
    auto bounds = getLocalBounds().reduced(8, 4);
    
    // Left side - file operations
    openButton.setBounds(bounds.removeFromLeft(70));
    bounds.removeFromLeft(4);
    exportButton.setBounds(bounds.removeFromLeft(70));
    bounds.removeFromLeft(20);
    
    // Center - playback controls
    playButton.setBounds(bounds.removeFromLeft(70));
    bounds.removeFromLeft(4);
    stopButton.setBounds(bounds.removeFromLeft(70));
    bounds.removeFromLeft(4);
    resynthButton.setBounds(bounds.removeFromLeft(80));
    bounds.removeFromLeft(20);
    
    // Time display
    timeLabel.setBounds(bounds.removeFromLeft(180));
    bounds.removeFromLeft(20);
    
    // Right side - zoom
    zoomLabel.setBounds(bounds.removeFromRight(50));
    bounds.removeFromRight(4);
    zoomSlider.setBounds(bounds.removeFromRight(150));
}

void ToolbarComponent::buttonClicked(juce::Button* button)
{
    if (button == &openButton && onOpenFile)
        onOpenFile();
    else if (button == &exportButton && onExportFile)
        onExportFile();
    else if (button == &playButton)
    {
        if (isPlaying)
        {
            if (onPause)
                onPause();
        }
        else
        {
            if (onPlay)
                onPlay();
        }
    }
    else if (button == &stopButton && onStop)
        onStop();
    else if (button == &resynthButton && onResynthesize)
        onResynthesize();
}

void ToolbarComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &zoomSlider && onZoomChanged)
        onZoomChanged(static_cast<float>(slider->getValue()));
}

void ToolbarComponent::setPlaying(bool playing)
{
    isPlaying = playing;
    playButton.setButtonText(playing ? "Pause" : "Play");
}

void ToolbarComponent::setCurrentTime(double time)
{
    currentTime = time;
    updateTimeDisplay();
}

void ToolbarComponent::setTotalTime(double time)
{
    totalTime = time;
    updateTimeDisplay();
}

void ToolbarComponent::updateTimeDisplay()
{
    timeLabel.setText(formatTime(currentTime) + " / " + formatTime(totalTime),
                      juce::dontSendNotification);
}

juce::String ToolbarComponent::formatTime(double seconds)
{
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);
    
    return juce::String::formatted("%02d:%02d.%03d", mins, secs, ms);
}
