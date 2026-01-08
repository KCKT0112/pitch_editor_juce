#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/PitchDetector.h"
#include "../Audio/FCPEPitchDetector.h"
#include "../Audio/Vocoder.h"
#include "ToolbarComponent.h"
#include "PianoRollComponent.h"
#include "WaveformComponent.h"
#include "ParameterPanel.h"
#include "SettingsComponent.h"

class MainComponent : public juce::Component,
                      public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void timerCallback() override;
    
private:
    void openFile();
    void exportFile();
    void play();
    void pause();
    void stop();
    void seek(double time);
    void resynthesize();
    void showSettings();
    void applySettings();
    
    void onNoteSelected(Note* note);
    void onPitchEdited();
    void onZoomChanged(float pixelsPerSecond);
    void onScrollChanged(double scrollX);
    
    void loadAudioFile(const juce::File& file);
    void analyzeAudio();
    void segmentIntoNotes();
    
    std::unique_ptr<Project> project;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<PitchDetector> pitchDetector;  // Fallback YIN detector
    std::unique_ptr<FCPEPitchDetector> fcpePitchDetector;  // FCPE neural network detector
    std::unique_ptr<Vocoder> vocoder;
    
    bool useFCPE = true;  // Use FCPE by default if available
    
    ToolbarComponent toolbar;
    PianoRollComponent pianoRoll;
    WaveformComponent waveform;
    ParameterPanel parameterPanel;
    
    std::unique_ptr<SettingsDialog> settingsDialog;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    bool isPlaying = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
