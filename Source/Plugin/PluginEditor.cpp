#include "PluginEditor.h"

#if JucePlugin_Enable_ARA
#include "ARADocumentController.h"
#endif

PitchEditorAudioProcessorEditor::PitchEditorAudioProcessorEditor (PitchEditorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
#if JucePlugin_Enable_ARA
    , AudioProcessorEditorARAExtension (&p)
#endif
{
    addAndMakeVisible (mainComponent);

    // Connect processor to mainComponent
    audioProcessor.setMainComponent(&mainComponent);

#if JucePlugin_Enable_ARA
    // Connect ARA document controller to MainComponent via EditorView
    if (auto* editorView = getARAEditorView())
    {
        if (auto* docController = editorView->getDocumentController())
        {
            if (auto* pitchDocController = juce::ARADocumentControllerSpecialisation::
                    getSpecialisedDocumentController<PitchEditorDocumentController>(docController))
            {
                pitchDocController->setMainComponent(&mainComponent);

                // Setup re-analyze callback
                mainComponent.onReanalyzeRequested = [pitchDocController]() {
                    pitchDocController->reanalyze();
                };
            }
        }
    }
#endif

    // Setup render callback
    mainComponent.onRenderComplete = [this](const juce::AudioBuffer<float>& rendered) {
        audioProcessor.setProcessedAudio(rendered);
    };

    setSize (1400, 900);
    setResizable(true, true);
}

PitchEditorAudioProcessorEditor::~PitchEditorAudioProcessorEditor()
{
    // Clear the reference when editor is destroyed
    audioProcessor.setMainComponent(nullptr);
}

void PitchEditorAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void PitchEditorAudioProcessorEditor::resized()
{
    mainComponent.setBounds (getLocalBounds());
}
