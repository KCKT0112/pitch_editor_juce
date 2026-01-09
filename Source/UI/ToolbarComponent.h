#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"

// Forward declaration - EditMode is defined in PianoRollComponent.h
enum class EditMode;

class ToolbarComponent : public juce::Component,
                         public juce::Button::Listener,
                         public juce::Slider::Listener
{
public:
    ToolbarComponent();
    ~ToolbarComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    
    void setPlaying(bool playing);
    void setCurrentTime(double time);
    void setTotalTime(double time);
    void setEditMode(EditMode mode);
    void setZoom(float pixelsPerSecond);  // Update zoom slider without triggering callback
    bool isFollowPlayback() const { return followPlayback; }

    // Plugin mode
    void setPluginMode(bool isPlugin);

    // Progress bar control
    void showProgress(const juce::String& message);
    void hideProgress();
    void setProgress(float progress);  // 0.0 to 1.0, or -1 for indeterminate

    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void()> onGoToStart;
    std::function<void()> onGoToEnd;
    std::function<void(float)> onZoomChanged;
    std::function<void(EditMode)> onEditModeChanged;

    // Plugin mode callbacks
    std::function<void()> onReanalyze;
    std::function<void()> onRender;
    std::function<void(bool)> onToggleSidebar;  // Called with new visibility state

private:
    void updateTimeDisplay();
    juce::String formatTime(double seconds);

    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton goToStartButton { "|<" };
    juce::TextButton goToEndButton { ">|" };

    // Plugin mode buttons
    juce::TextButton reanalyzeButton { "Re-analyze" };
    juce::TextButton renderButton { "Render" };
    bool pluginMode = false;

    // Edit mode buttons
    juce::TextButton selectModeButton { "Select" };
    juce::TextButton drawModeButton { "Draw" };
    juce::ToggleButton followButton { "Follow" };
    
    juce::Label timeLabel;
    
    juce::Slider zoomSlider;
    juce::Label zoomLabel { {}, "Zoom:" };
    
    // Progress components
    double progressValue = 0.0;  // Must be declared before progressBar
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    bool showingProgress = false;

    // Sidebar toggle
    juce::TextButton sidebarToggleButton { "≡" };
    bool sidebarVisible = false;
    
    double currentTime = 0.0;
    double totalTime = 0.0;
    bool isPlaying = false;
    bool followPlayback = true;
    int currentEditModeInt = 0;  // 0 = Select, 1 = Draw

#if JUCE_MAC
    juce::ComponentDragger dragger;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};
