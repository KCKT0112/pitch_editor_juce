#pragma once

#include "../JuceHeader.h"
#include "PluginProcessor.h"
#include "../UI/MainComponent.h"

class PitchEditorAudioProcessorEditor : public juce::AudioProcessorEditor
#if JucePlugin_Enable_ARA
                                       , public juce::AudioProcessorEditorARAExtension
#endif
{
public:
    explicit PitchEditorAudioProcessorEditor (PitchEditorAudioProcessor&);
    ~PitchEditorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PitchEditorAudioProcessor& audioProcessor;
    MainComponent mainComponent { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchEditorAudioProcessorEditor)
};
