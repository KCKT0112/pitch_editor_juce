#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"
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
    int getNumThreads() const { return numThreads; }
    int getGPUDeviceId() const { return gpuDeviceId; }

    // Plugin mode (disables audio device settings)
    bool isPluginMode() const { return pluginMode; }

    // Callbacks
    std::function<void()> onSettingsChanged;
    std::function<void()> onLanguageChanged;

    // Load/save settings
    void loadSettings();
    void saveSettings();

    // Get available execution providers
    static juce::StringArray getAvailableDevices();

private:
    void updateDeviceList();
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
    juce::Label threadsLabel;
    StyledSlider threadsSlider;
    juce::Label threadsValueLabel;

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
    int numThreads = 0;  // 0 = auto (use all cores)
    int gpuDeviceId = 0;

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

    SettingsComponent* getSettingsComponent() { return settingsComponent.get(); }

private:
    std::unique_ptr<SettingsComponent> settingsComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
