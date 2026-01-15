#include "ParameterPanel.h"
#include "StyledComponents.h"
#include "../Utils/Localization.h"

ParameterPanel::ParameterPanel()
{
    // Note info
    addAndMakeVisible(noteInfoLabel);
    noteInfoLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    noteInfoLabel.setText(TR("param.no_selection"), juce::dontSendNotification);
    noteInfoLabel.setJustificationType(juce::Justification::centred);

    // Setup sliders
    setupSlider(pitchOffsetSlider, pitchOffsetLabel, TR("param.pitch_offset"), -24.0, 24.0, 0.0);

    // Vibrato
    addAndMakeVisible(vibratoEnableButton);
    vibratoEnableButton.setButtonText(TR("param.vibrato_enable"));
    vibratoEnableButton.addListener(this);
    vibratoEnableButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    vibratoEnableButton.setLookAndFeel(&DarkLookAndFeel::getInstance());
    vibratoEnableButton.setEnabled(false);

    setupSlider(vibratoRateSlider, vibratoRateLabel, TR("param.vibrato_rate"), 0.1, 12.0, 5.0);
    setupSlider(vibratoDepthSlider, vibratoDepthLabel, TR("param.vibrato_depth"), 0.0, 2.0, 0.0);
    vibratoRateSlider.setEnabled(false);
    vibratoDepthSlider.setEnabled(false);
    setupSlider(volumeSlider, volumeLabel, TR("param.volume_label"), -24.0, 12.0, 0.0);
    setupSlider(formantShiftSlider, formantShiftLabel, TR("param.formant_shift"), -12.0, 12.0, 0.0);
    setupSlider(globalPitchSlider, globalPitchLabel, TR("param.global_pitch"), -24.0, 24.0, 0.0);

    // Section labels
    pitchSectionLabel.setText(TR("param.pitch"), juce::dontSendNotification);
    vibratoSectionLabel.setText(TR("param.vibrato"), juce::dontSendNotification);
    volumeSectionLabel.setText(TR("param.volume"), juce::dontSendNotification);
    formantSectionLabel.setText(TR("param.formant"), juce::dontSendNotification);
    globalSectionLabel.setText(TR("param.global"), juce::dontSendNotification);

    for (auto* label : { &pitchSectionLabel, &volumeSectionLabel,
                         &vibratoSectionLabel, &formantSectionLabel, &globalSectionLabel })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, juce::Colour(COLOR_PRIMARY));
        label->setFont(juce::Font(14.0f, juce::Font::bold));
    }

    // Volume and formant sliders disabled (not implemented yet)
    volumeSlider.setEnabled(false);
    formantShiftSlider.setEnabled(false);
    // Global pitch slider is now enabled!
    globalPitchSlider.setEnabled(true);
}

ParameterPanel::~ParameterPanel()
{
    vibratoEnableButton.setLookAndFeel(nullptr);
}

void ParameterPanel::setupSlider(juce::Slider& slider, juce::Label& label,
                                  const juce::String& name, double min, double max, double def)
{
    addAndMakeVisible(slider);
    addAndMakeVisible(label);

    slider.setRange(min, max, 0.01);
    slider.setValue(def);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 22);
    slider.addListener(this);

    // Slider track colors
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2D2D37));
    slider.setColour(juce::Slider::trackColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.6f));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));

    // Text box colors - match dark theme with subtle border
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF252530));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF3D3D47));
    slider.setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.3f));

    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    // Don't fill background - let parent DraggablePanel handle it
    juce::ignoreUnused(g);
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
    bounds.removeFromTop(10);

    // Vibrato section
    vibratoSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    vibratoEnableButton.setBounds(bounds.removeFromTop(22));
    vibratoRateLabel.setBounds(bounds.removeFromTop(20));
    vibratoRateSlider.setBounds(bounds.removeFromTop(24));
    vibratoDepthLabel.setBounds(bounds.removeFromTop(20));
    vibratoDepthSlider.setBounds(bounds.removeFromTop(24));
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
        selectedNote->markDirty();  // Mark as dirty for incremental synthesis
        
        if (onParameterChanged)
            onParameterChanged();
    }
    else if (slider == &vibratoRateSlider && selectedNote)
    {
        selectedNote->setVibratoRateHz(static_cast<float>(slider->getValue()));
        selectedNote->markDirty();

        if (onParameterChanged)
            onParameterChanged();
    }
    else if (slider == &vibratoDepthSlider && selectedNote)
    {
        selectedNote->setVibratoDepthSemitones(static_cast<float>(slider->getValue()));
        selectedNote->markDirty();

        if (onParameterChanged)
            onParameterChanged();
    }
    else if (slider == &globalPitchSlider && project)
    {
        project->setGlobalPitchOffset(static_cast<float>(slider->getValue()));

        // Mark all notes as dirty for full resynthesis
        for (auto& note : project->getNotes())
            note.markDirty();

        if (onGlobalPitchChanged)
            onGlobalPitchChanged();
    }
}

void ParameterPanel::sliderDragEnded(juce::Slider* slider)
{
    if ((slider == &pitchOffsetSlider || slider == &vibratoRateSlider || slider == &vibratoDepthSlider) && selectedNote)
    {
        // Trigger incremental synthesis when slider drag ends
        if (onParameterEditFinished)
            onParameterEditFinished();
    }
    else if (slider == &globalPitchSlider && project)
    {
        // Global pitch changed, need full resynthesis
        if (onParameterEditFinished)
            onParameterEditFinished();
    }
}

void ParameterPanel::buttonClicked(juce::Button* button)
{
    if (isUpdating) return;

    if (button == &vibratoEnableButton && selectedNote)
    {
        selectedNote->setVibratoEnabled(vibratoEnableButton.getToggleState());
        selectedNote->markDirty();

        if (onParameterChanged)
            onParameterChanged();

        if (onParameterEditFinished)
            onParameterEditFinished();
    }
}

void ParameterPanel::setProject(Project* proj)
{
    project = proj;
    updateGlobalSliders();
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

        vibratoEnableButton.setEnabled(true);
        vibratoEnableButton.setToggleState(selectedNote->isVibratoEnabled(), juce::dontSendNotification);
        vibratoRateSlider.setEnabled(true);
        vibratoDepthSlider.setEnabled(true);
        vibratoRateSlider.setValue(selectedNote->getVibratoRateHz(), juce::dontSendNotification);
        vibratoDepthSlider.setValue(selectedNote->getVibratoDepthSemitones(), juce::dontSendNotification);
    }
    else
    {
        noteInfoLabel.setText("No note selected", juce::dontSendNotification);
        pitchOffsetSlider.setValue(0.0);
        pitchOffsetSlider.setEnabled(false);

        vibratoEnableButton.setEnabled(false);
        vibratoEnableButton.setToggleState(false, juce::dontSendNotification);
        vibratoRateSlider.setEnabled(false);
        vibratoDepthSlider.setEnabled(false);
        vibratoRateSlider.setValue(5.0, juce::dontSendNotification);
        vibratoDepthSlider.setValue(0.0, juce::dontSendNotification);
    }
    
    isUpdating = false;
}

void ParameterPanel::updateGlobalSliders()
{
    isUpdating = true;

    if (project)
    {
        globalPitchSlider.setValue(project->getGlobalPitchOffset());
        globalPitchSlider.setEnabled(true);
    }
    else
    {
        globalPitchSlider.setValue(0.0);
        globalPitchSlider.setEnabled(false);
    }

    isUpdating = false;
}
