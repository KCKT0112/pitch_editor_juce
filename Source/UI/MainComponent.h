#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/PitchDetector.h"
#include "../Audio/FCPEPitchDetector.h"
#include "../Audio/SOMEDetector.h"
#include "../Audio/Vocoder.h"
#include "../Utils/UndoManager.h"
#include "CustomTitleBar.h"
#include "ToolbarComponent.h"
#include "PianoRollComponent.h"
#include "ParameterPanel.h"
#include "SettingsComponent.h"

#include <atomic>
#include <thread>

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::KeyListener,
                      public juce::MenuBarModel
{
public:
    explicit MainComponent(bool enableAudioDevice = true);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void timerCallback() override;

    // KeyListener
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // Mouse handling for window dragging on macOS
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Plugin mode
    bool isPluginMode() const { return !enableAudioDeviceFlag; }
    Project* getProject() { return project.get(); }

    // Plugin mode - host audio handling
    void setHostAudio(const juce::AudioBuffer<float>& buffer, double sampleRate);
    void renderProcessedAudio();

    // Plugin mode callbacks
    std::function<void()> onReanalyzeRequested;
    std::function<void(const juce::AudioBuffer<float>&)> onRenderComplete;

private:
    void openFile();
    void exportFile();
    void play();
    void pause();
    void stop();
    void seek(double time);
    void resynthesizeIncremental();  // Incremental synthesis on edit
    void showSettings();
    void applySettings();
    
    void onNoteSelected(Note* note);
    void onPitchEdited();
    void onZoomChanged(float pixelsPerSecond);
    void reinterpolateUV(int startFrame, int endFrame);  // Re-infer UV regions using FCPE
    
    void loadAudioFile(const juce::File& file);
    void analyzeAudio();
    void analyzeAudio(Project& targetProject, const std::function<void(double, const juce::String&)>& onProgress);
    void segmentIntoNotes();
    void segmentIntoNotes(Project& targetProject);
    
    void loadConfig();
    void saveConfig();

    void saveProject();
    
    void undo();
    void redo();
    void setEditMode(EditMode mode);
    
    std::unique_ptr<Project> project;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<PitchDetector> pitchDetector;  // Fallback YIN detector
    std::unique_ptr<FCPEPitchDetector> fcpePitchDetector;  // FCPE neural network detector
    std::unique_ptr<SOMEDetector> someDetector;  // SOME note segmentation detector
    std::unique_ptr<Vocoder> vocoder;
    std::unique_ptr<PitchUndoManager> undoManager;
    
    bool useFCPE = true;  // Use FCPE by default if available

    const bool enableAudioDeviceFlag;

#if !JUCE_MAC
    CustomTitleBar titleBar;
    juce::MenuBarComponent menuBar;
#endif
    ToolbarComponent toolbar;
    PianoRollComponent pianoRoll;
    ParameterPanel parameterPanel;
    
    std::unique_ptr<SettingsDialog> settingsDialog;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    // Original waveform for incremental synthesis
    juce::AudioBuffer<float> originalWaveform;
    bool hasOriginalWaveform = false;
    
    bool isPlaying = false;

    // Sync flag to prevent infinite loops
    bool isSyncingZoom = false;

#if JUCE_MAC
    juce::ComponentDragger dragger;
#endif

    // Async load state
    std::thread loaderThread;
    std::atomic<bool> isLoadingAudio { false };
    std::atomic<bool> cancelLoading { false };
    std::atomic<double> loadingProgress { 0.0 };
    juce::CriticalSection loadingMessageLock;
    juce::String loadingMessage;
    juce::String lastLoadingMessage;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
