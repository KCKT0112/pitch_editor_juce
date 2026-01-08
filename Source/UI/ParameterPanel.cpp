#include "ParameterPanel.h"

ParameterPanel::ParameterPanel()
{
    // Note info
    addAndMakeVisible(noteInfoLabel);
    noteInfoLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    noteInfoLabel.setText("No note selected", juce::dontSendNotification);
    noteInfoLabel.setJustificationType(juce::Justification::centred);
    
    // Setup sliders
    setupSlider(pitchOffsetSlider, pitchOffsetLabel, "Pitch Offset", -24.0, 24.0, 0.0);
    setupSlider(volumeSlider, volumeLabel, "Volume", -24.0, 12.0, 0.0);
    setupSlider(formantShiftSlider, formantShiftLabel, "Formant", -12.0, 12.0, 0.0);
    setupSlider(globalPitchSlider, globalPitchLabel, "Global Pitch", -24.0, 24.0, 0.0);
    
    // Section labels
    for (auto* label : { &pitchSectionLabel, &volumeSectionLabel, 
                         &formantSectionLabel, &globalSectionLabel })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, juce::Colour(COLOR_PRIMARY));
        label->setFont(juce::Font(14.0f, juce::Font::bold));
    }
    
    // Volume and formant sliders disabled (not implemented yet)
    volumeSlider.setEnabled(false);
    formantShiftSlider.setEnabled(false);
    globalPitchSlider.setEnabled(false);
}

ParameterPanel::~ParameterPanel()
{
}

void ParameterPanel::setupSlider(juce::Slider& slider, juce::Label& label,
                                  const juce::String& name, double min, double max, double def)
{
    addAndMakeVisible(slider);
    addAndMakeVisible(label);
    
    slider.setRange(min, max, 0.01);
    slider.setValue(def);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    slider.addListener(this);
    
    slider.setColour(juce::Slider::trackColourId, juce::Colour(0xFF3D3D47));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2D2D37));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E28));
    
    // Left border
    g.setColour(juce::Colour(0xFF3D3D47));
    g.drawVerticalLine(0, 0, static_cast<float>(getHeight()));
}

void ParameterPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    // Note info
    noteInfoLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);
    
    // Pitch section
    pitchSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    pitchOffsetLabel.setBounds(bounds.removeFromTop(20));
    pitchOffsetSlider.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(15);
    
    // Volume section
    volumeSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    volumeLabel.setBounds(bounds.removeFromTop(20));
    volumeSlider.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(15);
    
    // Formant section
    formantSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    formantShiftLabel.setBounds(bounds.removeFromTop(20));
    formantShiftSlider.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(30);
    
    // Global section
    globalSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    globalPitchLabel.setBounds(bounds.removeFromTop(20));
    globalPitchSlider.setBounds(bounds.removeFromTop(24));
}

void ParameterPanel::sliderValueChanged(juce::Slider* slider)
{
    if (isUpdating) return;
    
    if (slider == &pitchOffsetSlider && selectedNote)
    {
        selectedNote->setPitchOffset(static_cast<float>(slider->getValue()));
        
        if (onParameterChanged)
            onParameterChanged();
    }
}

void ParameterPanel::setSelectedNote(Note* note)
{
    selectedNote = note;
    updateFromNote();
}

void ParameterPanel::updateFromNote()
{
    isUpdating = true;
    
    if (selectedNote)
    {
        float midi = selectedNote->getAdjustedMidiNote();
        int octave = static_cast<int>(midi / 12) - 1;
        int noteIndex = static_cast<int>(midi) % 12;
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", 
                                           "F#", "G", "G#", "A", "A#", "B" };
        
        juce::String noteInfo = juce::String(noteNames[noteIndex]) + 
                                juce::String(octave) + 
                                " (" + juce::String(midi, 1) + ")";
        noteInfoLabel.setText(noteInfo, juce::dontSendNotification);
        
        pitchOffsetSlider.setValue(selectedNote->getPitchOffset());
        pitchOffsetSlider.setEnabled(true);
    }
    else
    {
        noteInfoLabel.setText("No note selected", juce::dontSendNotification);
        pitchOffsetSlider.setValue(0.0);
        pitchOffsetSlider.setEnabled(false);
    }
    
    isUpdating = false;
}
