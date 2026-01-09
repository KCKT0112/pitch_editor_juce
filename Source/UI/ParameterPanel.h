#pragma once

#include "../JuceHeader.h"
#include "../Models/Note.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"

class DarkLookAndFeel;  // Forward declaration

class ParameterPanel : public juce::Component,
                       public juce::Slider::Listener,
                       public juce::Button::Listener,
                       public juce::Timer
{
public:
    ParameterPanel();
    ~ParameterPanel() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;
    
    void setProject(Project* proj);
    void setSelectedNote(Note* note);
    void updateFromNote();
    void updateGlobalSliders();
    
    // Loading status with progress bar
    void setLoadingStatus(const juce::String& status);
    void clearLoadingStatus();
    
    std::function<void()> onParameterChanged;
    std::function<void()> onParameterEditFinished;  // Called when slider drag ends
    std::function<void()> onGlobalPitchChanged;
    
private:
    void setupSlider(juce::Slider& slider, juce::Label& label, 
                    const juce::String& name, double min, double max, double def);
    
    Project* project = nullptr;
    Note* selectedNote = nullptr;
    bool isUpdating = false;  // Prevent feedback loops
    
    // Loading status with indeterminate progress
    juce::Label loadingStatusLabel;
    double progressValue = -1.0;  // -1 for indeterminate - must be before progressBar
    juce::ProgressBar progressBar { progressValue };
    bool isLoading = false;
    
    // Note info
    juce::Label noteInfoLabel;
    
    // Pitch controls
    juce::Label pitchSectionLabel { {}, "Pitch" };
    juce::Slider pitchOffsetSlider;
    juce::Label pitchOffsetLabel { {}, "Offset (semitones):" };

    // Vibrato controls
    juce::Label vibratoSectionLabel { {}, "Vibrato" };
    juce::ToggleButton vibratoEnableButton { "Enable" };
    juce::Slider vibratoRateSlider;
    juce::Label vibratoRateLabel { {}, "Rate (Hz):" };
    juce::Slider vibratoDepthSlider;
    juce::Label vibratoDepthLabel { {}, "Depth (semitones):" };
    
    // Future parameters
    juce::Label volumeSectionLabel { {}, "Volume" };
    juce::Slider volumeSlider;
    juce::Label volumeLabel { {}, "Gain (dB):" };
    
    juce::Label formantSectionLabel { {}, "Formant" };
    juce::Slider formantShiftSlider;
    juce::Label formantShiftLabel { {}, "Shift (semitones):" };
    
    // Global settings
    juce::Label globalSectionLabel { {}, "Global Settings" };
    juce::Slider globalPitchSlider;
    juce::Label globalPitchLabel { {}, "Global Pitch:" };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterPanel)
};
