#include "ToolbarComponent.h"
#include "PianoRollComponent.h"  // For EditMode enum
#include "StyledComponents.h"
#include "../Utils/Localization.h"

#if JUCE_MAC
#include <Cocoa/Cocoa.h>
#endif

ToolbarComponent::ToolbarComponent()
{
    // Configure buttons
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(selectModeButton);
    addAndMakeVisible(drawModeButton);
    addAndMakeVisible(followButton);

    // Plugin mode buttons (hidden by default)
    addChildComponent(reanalyzeButton);
    addChildComponent(renderButton);

    playButton.addListener(this);
    stopButton.addListener(this);
    selectModeButton.addListener(this);
    drawModeButton.addListener(this);
    followButton.addListener(this);
    reanalyzeButton.addListener(this);
    renderButton.addListener(this);

    // Set localized text
    playButton.setButtonText(TR("toolbar.play"));
    stopButton.setButtonText(TR("toolbar.stop"));
    selectModeButton.setButtonText(TR("toolbar.select"));
    drawModeButton.setButtonText(TR("toolbar.draw"));
    followButton.setButtonText(TR("toolbar.follow"));
    reanalyzeButton.setButtonText(TR("toolbar.reanalyze"));
    renderButton.setButtonText(TR("toolbar.render"));
    zoomLabel.setText(TR("toolbar.zoom"), juce::dontSendNotification);

    // Style buttons
    auto buttonColor = juce::Colour(0xFF3D3D47);
    auto textColor = juce::Colours::white;

    for (auto* btn : { &playButton, &stopButton, &selectModeButton, &drawModeButton, &reanalyzeButton, &renderButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, buttonColor);
        btn->setColour(juce::TextButton::textColourOffId, textColor);
    }

    // Follow button style
    followButton.setToggleState(true, juce::dontSendNotification);
    followButton.setColour(juce::ToggleButton::textColourId, textColor);
    followButton.setLookAndFeel(&DarkLookAndFeel::getInstance());

    // Highlight select mode as default active
    selectModeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(COLOR_PRIMARY));

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

    zoomSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2D2D37));
    zoomSlider.setColour(juce::Slider::trackColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.6f));
    zoomSlider.setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));

    // Progress bar (hidden by default)
    addChildComponent(progressBar);
    addChildComponent(progressLabel);

    progressLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    progressLabel.setJustificationType(juce::Justification::centredLeft);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(COLOR_PRIMARY));
    progressBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xFF2D2D37));
}

ToolbarComponent::~ToolbarComponent()
{
    followButton.setLookAndFeel(nullptr);
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

    // Playback controls (or plugin mode buttons)
    if (pluginMode)
    {
        reanalyzeButton.setBounds(bounds.removeFromLeft(100));
        bounds.removeFromLeft(4);
        renderButton.setBounds(bounds.removeFromLeft(80));
    }
    else
    {
        playButton.setBounds(bounds.removeFromLeft(70));
        bounds.removeFromLeft(4);
        stopButton.setBounds(bounds.removeFromLeft(70));
    }
    bounds.removeFromLeft(20);

    // Edit mode buttons
    selectModeButton.setBounds(bounds.removeFromLeft(60));
    bounds.removeFromLeft(4);
    drawModeButton.setBounds(bounds.removeFromLeft(60));
    bounds.removeFromLeft(10);
    followButton.setBounds(bounds.removeFromLeft(70));
    bounds.removeFromLeft(20);

    // Time display
    timeLabel.setBounds(bounds.removeFromLeft(180));
    bounds.removeFromLeft(20);

    // Right side - zoom (slider on right, label before it)
    zoomSlider.setBounds(bounds.removeFromRight(150));
    bounds.removeFromRight(4);
    zoomLabel.setBounds(bounds.removeFromRight(50));

    // Progress bar (use the remaining middle area so it won't cover buttons)
    if (showingProgress)
    {
        auto progressArea = bounds;
        if (progressArea.getWidth() < 220)
            progressArea = getLocalBounds().reduced(200, 6);

        const int labelWidth = std::min(160, std::max(80, progressArea.getWidth() / 4));
        progressLabel.setBounds(progressArea.removeFromLeft(labelWidth));
        progressBar.setBounds(progressArea);
    }
}

void ToolbarComponent::buttonClicked(juce::Button* button)
{
    if (button == &playButton)
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
    else if (button == &reanalyzeButton && onReanalyze)
        onReanalyze();
    else if (button == &renderButton && onRender)
        onRender();
    else if (button == &selectModeButton)
    {
        setEditMode(EditMode::Select);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Select);
    }
    else if (button == &drawModeButton)
    {
        setEditMode(EditMode::Draw);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Draw);
    }
    else if (button == &followButton)
    {
        followPlayback = followButton.getToggleState();
    }
}

void ToolbarComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &zoomSlider && onZoomChanged)
        onZoomChanged(static_cast<float>(slider->getValue()));
}

void ToolbarComponent::setPlaying(bool playing)
{
    isPlaying = playing;
    playButton.setButtonText(playing ? TR("toolbar.pause") : TR("toolbar.play"));
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

void ToolbarComponent::setEditMode(EditMode mode)
{
    currentEditModeInt = (mode == EditMode::Draw) ? 1 : 0;
    
    auto buttonColor = juce::Colour(0xFF3D3D47);
    auto activeColor = juce::Colour(COLOR_PRIMARY);
    
    selectModeButton.setColour(juce::TextButton::buttonColourId, 
        mode == EditMode::Select ? activeColor : buttonColor);
    drawModeButton.setColour(juce::TextButton::buttonColourId, 
        mode == EditMode::Draw ? activeColor : buttonColor);
    
    repaint();
}

void ToolbarComponent::setZoom(float pixelsPerSecond)
{
    // Update slider without triggering callback
    zoomSlider.setValue(pixelsPerSecond, juce::dontSendNotification);
}

void ToolbarComponent::showProgress(const juce::String& message)
{
    showingProgress = true;
    progressLabel.setText(message, juce::dontSendNotification);
    progressLabel.setVisible(true);
    progressBar.setVisible(true);
    progressValue = -1.0;  // Indeterminate
    resized();
    repaint();
}

void ToolbarComponent::hideProgress()
{
    showingProgress = false;
    progressLabel.setVisible(false);
    progressBar.setVisible(false);
    resized();
    repaint();
}

void ToolbarComponent::setProgress(float progress)
{
    if (progress < 0)
        progressValue = -1.0;  // Indeterminate
    else
        progressValue = static_cast<double>(juce::jlimit(0.0f, 1.0f, progress));
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

void ToolbarComponent::mouseDown(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (auto* window = getTopLevelComponent())
        dragger.startDraggingComponent(window, e.getEventRelativeTo(window));
#else
    juce::ignoreUnused(e);
#endif
}

void ToolbarComponent::mouseDrag(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (auto* window = getTopLevelComponent())
        dragger.dragComponent(window, e.getEventRelativeTo(window), nullptr);
#else
    juce::ignoreUnused(e);
#endif
}

void ToolbarComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
#if JUCE_MAC
    juce::ignoreUnused(e);
    // Use native zoom (maximize to screen) instead of fullscreen
    if (auto* peer = getTopLevelComponent()->getPeer())
    {
        if (auto* nsView = (NSView*)peer->getNativeHandle())
        {
            if (auto* nsWindow = [nsView window])
                [nsWindow zoom:nil];
        }
    }
#else
    juce::ignoreUnused(e);
#endif
}

void ToolbarComponent::setPluginMode(bool isPlugin)
{
    pluginMode = isPlugin;

    playButton.setVisible(!isPlugin);
    stopButton.setVisible(!isPlugin);
    reanalyzeButton.setVisible(isPlugin);
    renderButton.setVisible(isPlugin);

    // In plugin mode, hide follow button (host controls playback)
    followButton.setVisible(!isPlugin);

    resized();
}
