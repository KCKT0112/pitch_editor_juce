#pragma once

#include "../JuceHeader.h"
#include "../Models/Note.h"
#include "../Utils/Constants.h"

class ParameterPanel : public juce::Component,
                       public juce::Slider::Listener
{
public:
    ParameterPanel();
    ~ParameterPanel() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void sliderValueChanged(juce::Slider* slider) override;
    
    void setSelectedNote(Note* note);
    void updateFromNote();
    
    std::function<void()> onParameterChanged;
    
private:
    void setupSlider(juce::Slider& slider, juce::Label& label, 
                    const juce::String& name, double min, double max, double def);
    
    Note* selectedNote = nullptr;
    bool isUpdating = false;  // Prevent feedback loops
    
    // Note info
    juce::Label noteInfoLabel;
    
    // Pitch controls
    juce::Label pitchSectionLabel { {}, "Pitch" };
    juce::Slider pitchOffsetSlider;
    juce::Label pitchOffsetLabel { {}, "Offset (semitones):" };
    
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
