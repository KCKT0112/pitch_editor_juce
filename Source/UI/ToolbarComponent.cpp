#include "ToolbarComponent.h"
#include "PianoRollComponent.h"  // For EditMode enum
#include "StyledComponents.h"
#include "../Utils/Localization.h"
#include "../Utils/SvgUtils.h"
#include "BinaryData.h"

ToolbarComponent::ToolbarComponent()
{
    // Load SVG icons with white tint
    auto playIcon = SvgUtils::loadSvg(BinaryData::playline_svg, BinaryData::playline_svgSize, juce::Colours::white);
    auto pauseIcon = SvgUtils::loadSvg(BinaryData::pauseline_svg, BinaryData::pauseline_svgSize, juce::Colours::white);
    auto stopIcon = SvgUtils::loadSvg(BinaryData::stopline_svg, BinaryData::stopline_svgSize, juce::Colours::white);
    auto startIcon = SvgUtils::loadSvg(BinaryData::movestartline_svg, BinaryData::movestartline_svgSize, juce::Colours::white);
    auto endIcon = SvgUtils::loadSvg(BinaryData::moveendline_svg, BinaryData::moveendline_svgSize, juce::Colours::white);
    auto cursorIcon = SvgUtils::loadSvg(BinaryData::cursor_24_filled_svg, BinaryData::cursor_24_filled_svgSize, juce::Colours::white);
    auto pitchEditIcon = SvgUtils::loadSvg(BinaryData::pitch_edit_24_filled_svg, BinaryData::pitch_edit_24_filled_svgSize, juce::Colours::white);
    auto scissorsIcon = SvgUtils::loadSvg(BinaryData::scissors_24_filled_svg, BinaryData::scissors_24_filled_svgSize, juce::Colours::white);
    auto followIcon = SvgUtils::loadSvg(BinaryData::follow24filled_svg, BinaryData::follow24filled_svgSize, juce::Colours::white);

    playButton.setImages(playIcon.get());
    stopButton.setImages(stopIcon.get());
    goToStartButton.setImages(startIcon.get());
    goToEndButton.setImages(endIcon.get());
    selectModeButton.setImages(cursorIcon.get());
    drawModeButton.setImages(pitchEditIcon.get());
    splitModeButton.setImages(scissorsIcon.get());
    followButton.setImages(followIcon.get());

    // Set edge indent for icon padding (makes icons smaller within button bounds)
    goToStartButton.setEdgeIndent(4);
    playButton.setEdgeIndent(6);
    stopButton.setEdgeIndent(6);
    goToEndButton.setEdgeIndent(4);
    selectModeButton.setEdgeIndent(6);
    drawModeButton.setEdgeIndent(6);
    splitModeButton.setEdgeIndent(6);
    followButton.setEdgeIndent(6);

    // Store pause icon for later use
    pauseDrawable = std::move(pauseIcon);
    playDrawable = SvgUtils::loadSvg(BinaryData::playline_svg, BinaryData::playline_svgSize, juce::Colours::white);

    // Configure buttons
    addAndMakeVisible(goToStartButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(goToEndButton);
    addAndMakeVisible(selectModeButton);
    addAndMakeVisible(drawModeButton);
    addAndMakeVisible(splitModeButton);
    addAndMakeVisible(followButton);

    // Plugin mode buttons (hidden by default)
    addChildComponent(reanalyzeButton);
    addChildComponent(araModeLabel);

    // ARA mode label style (background drawn in paint() for rounded corners)
    araModeLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    araModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    araModeLabel.setJustificationType(juce::Justification::centred);
    araModeLabel.setFont(juce::Font(11.0f, juce::Font::bold));

    goToStartButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    goToEndButton.addListener(this);
    selectModeButton.addListener(this);
    drawModeButton.addListener(this);
    splitModeButton.addListener(this);
    followButton.addListener(this);
    reanalyzeButton.addListener(this);

    // Set localized text (tooltips for icon buttons)
    selectModeButton.setTooltip(TR("toolbar.select"));
    drawModeButton.setTooltip(TR("toolbar.draw"));
    splitModeButton.setTooltip(TR("toolbar.split"));
    followButton.setTooltip(TR("toolbar.follow"));
    reanalyzeButton.setButtonText(TR("toolbar.reanalyze"));
    zoomLabel.setText(TR("toolbar.zoom"), juce::dontSendNotification);

    // Style reanalyze button
    reanalyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3D3D47));
    reanalyzeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    // Set default active states
    selectModeButton.setActive(true);
    followButton.setActive(true);  // Follow is on by default

    // Time label with monospace font for stable width
    addAndMakeVisible(timeLabel);
    timeLabel.setText("00:00.000 / 00:00.000", juce::dontSendNotification);
    timeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFCCCCCC));
    timeLabel.setJustificationType(juce::Justification::centred);
    timeLabel.setFont(juce::FontOptions().withName("Menlo").withHeight(14.0f).withStyle("Bold"));

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
    
    // Status label (hidden by default)
    addChildComponent(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(11.0f));
}

ToolbarComponent::~ToolbarComponent() = default;

void ToolbarComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A24));

    // Draw rounded background for tool buttons container
    if (!toolContainerBounds.isEmpty())
    {
        g.setColour(juce::Colour(0xFF2D2D37));
        g.fillRoundedRectangle(toolContainerBounds.toFloat(), 6.0f);
    }

    // Draw rounded background for time label
    if (timeLabel.isVisible())
    {
        g.setColour(juce::Colour(0xFF2D2D37));
        g.fillRoundedRectangle(timeLabel.getBounds().toFloat(), 6.0f);
    }

    // Draw rounded background for ARA mode label
    if (pluginMode && araModeLabel.isVisible())
    {
        g.setColour(juce::Colour(COLOR_PRIMARY));
        g.fillRoundedRectangle(araModeLabel.getBounds().toFloat(), 8.0f);
    }
}

void ToolbarComponent::resized()
{
    auto bounds = getLocalBounds().reduced(8, 4);

    // Calculate center section width for centering
    const int toolButtonSize = 32;
    const int toolContainerPadding = 4;
    const int numToolButtons = pluginMode ? 3 : 4;
    const int toolContainerWidth = toolButtonSize * numToolButtons + toolContainerPadding * 2;
    const int playbackWidth = pluginMode ? 200 : 120;
    const int timeWidth = 160;
    const int centerGap = 16;
    const int centerTotalWidth = playbackWidth + centerGap + toolContainerWidth + centerGap + timeWidth;

    // Right side - status/progress
    auto rightBounds = bounds.removeFromRight(200);
    if (showingStatus && !showingProgress)
    {
        statusLabel.setBounds(rightBounds.removeFromLeft(120));
    }
    if (showingProgress)
    {
        auto progressArea = rightBounds.withWidth(std::min(180, rightBounds.getWidth()));
        const int progressBarHeight = progressArea.getHeight() / 2;
        progressLabel.setBounds(progressArea.removeFromTop(progressArea.getHeight() - progressBarHeight));
        progressBar.setBounds(progressArea.withHeight(progressBarHeight));
    }

    // Hide zoom controls
    zoomLabel.setVisible(false);
    zoomSlider.setVisible(false);

    // Center section - calculate starting X for centering
    int centerStartX = (getWidth() - centerTotalWidth) / 2;
    int currentX = centerStartX;

    // Playback controls (or plugin mode buttons) - centered
    if (pluginMode)
    {
        araModeLabel.setBounds(currentX, bounds.getY(), 90, bounds.getHeight());
        currentX += 98;
        reanalyzeButton.setBounds(currentX, bounds.getY(), 100, bounds.getHeight());
        currentX += 100;
    }
    else
    {
        goToStartButton.setBounds(currentX, bounds.getY() + 4, 28, bounds.getHeight() - 8);
        currentX += 32;
        playButton.setBounds(currentX, bounds.getY() + 4, 28, bounds.getHeight() - 8);
        currentX += 32;
        stopButton.setBounds(currentX, bounds.getY() + 4, 28, bounds.getHeight() - 8);
        currentX += 32;
        goToEndButton.setBounds(currentX, bounds.getY() + 4, 28, bounds.getHeight() - 8);
        currentX += 28;
    }
    currentX += centerGap;

    // Edit mode buttons in a container - centered
    toolContainerBounds = juce::Rectangle<int>(currentX, bounds.getY() + 2, toolContainerWidth, bounds.getHeight() - 4);
    auto toolArea = toolContainerBounds.reduced(toolContainerPadding, toolContainerPadding);
    int toolX = toolArea.getX();
    selectModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    drawModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    splitModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    if (!pluginMode)
        followButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());

    currentX += toolContainerWidth + centerGap;

    // Time display - centered
    timeLabel.setBounds(currentX, bounds.getY() + 2, timeWidth, bounds.getHeight() - 4);
}

void ToolbarComponent::buttonClicked(juce::Button* button)
{
    if (button == &goToStartButton && onGoToStart)
        onGoToStart();
    else if (button == &goToEndButton && onGoToEnd)
        onGoToEnd();
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
    else if (button == &reanalyzeButton && onReanalyze)
        onReanalyze();
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
    else if (button == &splitModeButton)
    {
        setEditMode(EditMode::Split);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Split);
    }
    else if (button == &followButton)
    {
        followPlayback = !followPlayback;
        followButton.setActive(followPlayback);
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
    playButton.setImages(playing ? pauseDrawable.get() : playDrawable.get());
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
    currentEditModeInt = static_cast<int>(mode);
    selectModeButton.setActive(mode == EditMode::Select);
    drawModeButton.setActive(mode == EditMode::Draw);
    splitModeButton.setActive(mode == EditMode::Split);
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

void ToolbarComponent::setStatusMessage(const juce::String& message)
{
    if (message.isEmpty())
    {
        showingStatus = false;
        statusLabel.setVisible(false);
    }
    else
    {
        showingStatus = true;
        statusLabel.setText(message, juce::dontSendNotification);
        statusLabel.setVisible(true);
    }
    resized();
    repaint();
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
    juce::ignoreUnused(e);
}

void ToolbarComponent::setPluginMode(bool isPlugin)
{
    pluginMode = isPlugin;

    goToStartButton.setVisible(!isPlugin);
    playButton.setVisible(!isPlugin);
    stopButton.setVisible(!isPlugin);
    goToEndButton.setVisible(!isPlugin);
    reanalyzeButton.setVisible(isPlugin);
    araModeLabel.setVisible(isPlugin);

    // In plugin mode, hide follow button (host controls playback)
    followButton.setVisible(!isPlugin);

    resized();
}

void ToolbarComponent::setARAMode(bool isARA)
{
    araMode = isARA;
    araModeLabel.setText(isARA ? "ARA Mode" : "Non-ARA", juce::dontSendNotification);
}
