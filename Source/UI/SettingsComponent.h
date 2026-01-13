#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"
#include "../Audio/PitchDetectorType.h"
#include "StyledComponents.h"
#include <functional>

enum class Language;  // Forward declaration

/**
 * Settings dialog for application configuration.
 * Includes device selection for ONNX inference.
 */
class SettingsComponent : public juce::Component,
                          public juce::ComboBox::Listener
{
public:
    SettingsComponent(juce::AudioDeviceManager* audioDeviceManager = nullptr);
    ~SettingsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ComboBox::Listener
    void comboBoxChanged(juce::ComboBox* comboBox) override;

    // Get current settings
    juce::String getSelectedDevice() const { return currentDevice; }
    int getGPUDeviceId() const { return gpuDeviceId; }
    PitchDetectorType getPitchDetectorType() const { return pitchDetectorType; }

    // Plugin mode (disables audio device settings)
    bool isPluginMode() const { return pluginMode; }

    // Callbacks
    std::function<void()> onSettingsChanged;
    std::function<void()> onLanguageChanged;
    std::function<void(PitchDetectorType)> onPitchDetectorChanged;

    // Load/save settings
    void loadSettings();
    void saveSettings();

    // Get available execution providers
    static juce::StringArray getAvailableDevices();

private:
    void updateDeviceList();
    void updateGPUDeviceList(const juce::String& deviceType);
    void updateAudioDeviceTypes();
    void updateAudioOutputDevices();
    void updateSampleRates();
    void updateBufferSizes();
    void applyAudioSettings();

    bool pluginMode = false;
    juce::AudioDeviceManager* deviceManager = nullptr;

    juce::Label titleLabel;

    juce::Label languageLabel;
    StyledComboBox languageComboBox;

    juce::Label deviceLabel;
    StyledComboBox deviceComboBox;
    juce::Label gpuDeviceLabel;
    StyledComboBox gpuDeviceComboBox;

    juce::Label pitchDetectorLabel;
    StyledComboBox pitchDetectorComboBox;

    juce::Label infoLabel;

    // Audio device settings (standalone mode only)
    juce::Label audioSectionLabel;
    juce::Label audioDeviceTypeLabel;
    StyledComboBox audioDeviceTypeComboBox;
    juce::Label audioOutputLabel;
    StyledComboBox audioOutputComboBox;
    juce::Label sampleRateLabel;
    StyledComboBox sampleRateComboBox;
    juce::Label bufferSizeLabel;
    StyledComboBox bufferSizeComboBox;
    juce::Label outputChannelsLabel;
    StyledComboBox outputChannelsComboBox;

    juce::String currentDevice = "CPU";
    int gpuDeviceId = 0;
    PitchDetectorType pitchDetectorType = PitchDetectorType::RMVPE;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

/**
 * Settings dialog window.
 */
class SettingsDialog : public juce::DialogWindow
{
public:
    SettingsDialog(juce::AudioDeviceManager* audioDeviceManager = nullptr);
    ~SettingsDialog() override = default;

    void closeButtonPressed() override;
    void paint(juce::Graphics& g) override;

    SettingsComponent* getSettingsComponent() { return settingsComponent.get(); }

private:
    std::unique_ptr<SettingsComponent> settingsComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
