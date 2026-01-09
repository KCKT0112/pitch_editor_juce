#include "SettingsComponent.h"
#include "../Utils/Constants.h"
#include "../Utils/Localization.h"
#include <thread>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

//==============================================================================
// SettingsComponent
//==============================================================================

SettingsComponent::SettingsComponent(juce::AudioDeviceManager* audioDeviceManager)
    : deviceManager(audioDeviceManager), pluginMode(audioDeviceManager == nullptr)
{
    // Title
    titleLabel.setText(TR("settings.title"), juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    // Language selection
    languageLabel.setText(TR("settings.language"), juce::dontSendNotification);
    languageLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(languageLabel);

    // Add "Auto" option first
    languageComboBox.addItem(TR("lang.auto"), 1);
    // Add available languages dynamically
    const auto& langs = Localization::getInstance().getAvailableLanguages();
    for (int i = 0; i < static_cast<int>(langs.size()); ++i)
        languageComboBox.addItem(langs[i].nativeName, i + 2);  // IDs start at 2
    languageComboBox.addListener(this);
    addAndMakeVisible(languageComboBox);

    // Device selection
    deviceLabel.setText(TR("settings.device"), juce::dontSendNotification);
    deviceLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(deviceLabel);

    deviceComboBox.addListener(this);
    addAndMakeVisible(deviceComboBox);
    updateDeviceList();

    // GPU device ID selection
    gpuDeviceLabel.setText(TR("settings.gpu_device"), juce::dontSendNotification);
    gpuDeviceLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(gpuDeviceLabel);

    for (int i = 0; i < 8; ++i)
        gpuDeviceComboBox.addItem("GPU " + juce::String(i), i + 1);
    gpuDeviceComboBox.setSelectedId(1, juce::dontSendNotification);
    gpuDeviceComboBox.addListener(this);
    addAndMakeVisible(gpuDeviceComboBox);
    gpuDeviceLabel.setVisible(false);
    gpuDeviceComboBox.setVisible(false);

    // Thread count
    threadsLabel.setText(TR("settings.threads"), juce::dontSendNotification);
    threadsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(threadsLabel);

    threadsSlider.setRange(0, 32, 1);
    threadsSlider.setValue(0);
    threadsSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    threadsSlider.onValueChange = [this]()
    {
        numThreads = static_cast<int>(threadsSlider.getValue());
        if (numThreads == 0)
        {
            int autoThreads = std::thread::hardware_concurrency();
            threadsValueLabel.setText(TR("settings.auto") + " (" + juce::String(autoThreads) + " " + TR("settings.cores") + ")",
                                      juce::dontSendNotification);
        }
        else
        {
            threadsValueLabel.setText(juce::String(numThreads), juce::dontSendNotification);
        }
        saveSettings();
        if (onSettingsChanged)
            onSettingsChanged();
    };
    addAndMakeVisible(threadsSlider);

    threadsValueLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(threadsValueLabel);

    // Info label
    infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
    infoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(infoLabel);

    // Audio device settings (standalone mode only)
    if (!pluginMode && deviceManager != nullptr)
    {
        audioSectionLabel.setText(TR("settings.audio"), juce::dontSendNotification);
        audioSectionLabel.setFont(juce::Font(16.0f, juce::Font::bold));
        audioSectionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(audioSectionLabel);

        // Audio device type (driver)
        audioDeviceTypeLabel.setText(TR("settings.audio_driver"), juce::dontSendNotification);
        audioDeviceTypeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(audioDeviceTypeLabel);
        audioDeviceTypeComboBox.addListener(this);
        addAndMakeVisible(audioDeviceTypeComboBox);

        // Output device
        audioOutputLabel.setText(TR("settings.audio_output"), juce::dontSendNotification);
        audioOutputLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(audioOutputLabel);
        audioOutputComboBox.addListener(this);
        addAndMakeVisible(audioOutputComboBox);

        // Sample rate
        sampleRateLabel.setText(TR("settings.sample_rate"), juce::dontSendNotification);
        sampleRateLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(sampleRateLabel);
        sampleRateComboBox.addListener(this);
        addAndMakeVisible(sampleRateComboBox);

        // Buffer size
        bufferSizeLabel.setText(TR("settings.buffer_size"), juce::dontSendNotification);
        bufferSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(bufferSizeLabel);
        bufferSizeComboBox.addListener(this);
        addAndMakeVisible(bufferSizeComboBox);

        // Output channels
        outputChannelsLabel.setText(TR("settings.output_channels"), juce::dontSendNotification);
        outputChannelsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(outputChannelsLabel);
        outputChannelsComboBox.addItem("Mono", 1);
        outputChannelsComboBox.addItem("Stereo", 2);
        outputChannelsComboBox.setSelectedId(2, juce::dontSendNotification);
        outputChannelsComboBox.addListener(this);
        addAndMakeVisible(outputChannelsComboBox);

        updateAudioDeviceTypes();
    }

    // Load saved settings
    loadSettings();

    // Update UI
    threadsSlider.setValue(numThreads, juce::dontSendNotification);
    if (numThreads == 0)
    {
        int autoThreads = std::thread::hardware_concurrency();
        threadsValueLabel.setText(TR("settings.auto") + " (" + juce::String(autoThreads) + " " + TR("settings.cores") + ")",
                                  juce::dontSendNotification);
    }
    else
    {
        threadsValueLabel.setText(juce::String(numThreads), juce::dontSendNotification);
    }

    // Set size based on mode
    if (pluginMode)
        setSize(400, 280);
    else
        setSize(400, 580);
}

SettingsComponent::~SettingsComponent()
{
}

void SettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(COLOR_BACKGROUND));
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(15);

    // Language selection row
    auto langRow = bounds.removeFromTop(30);
    languageLabel.setBounds(langRow.removeFromLeft(120));
    languageComboBox.setBounds(langRow.reduced(0, 2));
    bounds.removeFromTop(10);

    // Device selection row
    auto deviceRow = bounds.removeFromTop(30);
    deviceLabel.setBounds(deviceRow.removeFromLeft(120));
    deviceComboBox.setBounds(deviceRow.reduced(0, 2));
    bounds.removeFromTop(10);

    // GPU device ID row (only visible when GPU is selected)
    if (gpuDeviceLabel.isVisible())
    {
        auto gpuRow = bounds.removeFromTop(30);
        gpuDeviceLabel.setBounds(gpuRow.removeFromLeft(120));
        gpuDeviceComboBox.setBounds(gpuRow.reduced(0, 2));
        bounds.removeFromTop(10);
    }

    // Threads row
    auto threadsRow = bounds.removeFromTop(30);
    threadsLabel.setBounds(threadsRow.removeFromLeft(120));
    threadsValueLabel.setBounds(threadsRow.removeFromRight(100));
    threadsSlider.setBounds(threadsRow.reduced(0, 2));
    bounds.removeFromTop(15);

    // Info label
    infoLabel.setBounds(bounds.removeFromTop(60));

    // Audio device settings (standalone mode only)
    if (!pluginMode && deviceManager != nullptr)
    {
        bounds.removeFromTop(10);
        audioSectionLabel.setBounds(bounds.removeFromTop(25));
        bounds.removeFromTop(10);

        // Audio driver row
        auto driverRow = bounds.removeFromTop(30);
        audioDeviceTypeLabel.setBounds(driverRow.removeFromLeft(120));
        audioDeviceTypeComboBox.setBounds(driverRow.reduced(0, 2));
        bounds.removeFromTop(10);

        // Output device row
        auto outputRow = bounds.removeFromTop(30);
        audioOutputLabel.setBounds(outputRow.removeFromLeft(120));
        audioOutputComboBox.setBounds(outputRow.reduced(0, 2));
        bounds.removeFromTop(10);

        // Sample rate row
        auto rateRow = bounds.removeFromTop(30);
        sampleRateLabel.setBounds(rateRow.removeFromLeft(120));
        sampleRateComboBox.setBounds(rateRow.reduced(0, 2));
        bounds.removeFromTop(10);

        // Buffer size row
        auto bufferRow = bounds.removeFromTop(30);
        bufferSizeLabel.setBounds(bufferRow.removeFromLeft(120));
        bufferSizeComboBox.setBounds(bufferRow.reduced(0, 2));
        bounds.removeFromTop(10);

        // Output channels row
        auto channelsRow = bounds.removeFromTop(30);
        outputChannelsLabel.setBounds(channelsRow.removeFromLeft(120));
        outputChannelsComboBox.setBounds(channelsRow.reduced(0, 2));
    }
}

void SettingsComponent::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &languageComboBox)
    {
        int selectedId = languageComboBox.getSelectedId();
        if (selectedId == 1)
        {
            // Auto - detect system language
            Localization::detectSystemLanguage();
        }
        else if (selectedId >= 2)
        {
            // Get language code from index
            const auto& langs = Localization::getInstance().getAvailableLanguages();
            int langIndex = selectedId - 2;
            if (langIndex < static_cast<int>(langs.size()))
                Localization::getInstance().setLanguage(langs[langIndex].code);
        }
        saveSettings();

        if (onLanguageChanged)
            onLanguageChanged();
    }
    else if (comboBox == &deviceComboBox)
    {
        currentDevice = deviceComboBox.getText();

        // Show/hide GPU device selector based on device type
        bool isGPU = (currentDevice != "CPU");
        gpuDeviceLabel.setVisible(isGPU);
        gpuDeviceComboBox.setVisible(isGPU);
        resized();

        saveSettings();

        // Update info label
        if (currentDevice == "CPU")
        {
            infoLabel.setText("CPU: Uses your processor for inference.\n"
                              "Most compatible, moderate speed.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "CUDA")
        {
            infoLabel.setText("CUDA: Uses NVIDIA GPU for inference.\n"
                              "Fastest option if you have an NVIDIA GPU.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "DirectML")
        {
            infoLabel.setText("DirectML: Uses GPU via DirectX 12.\n"
                              "Works with most GPUs on Windows.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "CoreML")
        {
            infoLabel.setText("CoreML: Uses Apple Neural Engine or GPU.\n"
                              "Best option on macOS/iOS devices.",
                              juce::dontSendNotification);
        }

        if (onSettingsChanged)
            onSettingsChanged();
    }
    else if (comboBox == &gpuDeviceComboBox)
    {
        gpuDeviceId = gpuDeviceComboBox.getSelectedId() - 1;
        saveSettings();
        if (onSettingsChanged)
            onSettingsChanged();
    }
    else if (comboBox == &audioDeviceTypeComboBox)
    {
        auto& types = deviceManager->getAvailableDeviceTypes();
        int idx = audioDeviceTypeComboBox.getSelectedId() - 1;
        if (idx >= 0 && idx < types.size())
        {
            deviceManager->setCurrentAudioDeviceType(types[idx]->getTypeName(), true);
            updateAudioOutputDevices();
        }
    }
    else if (comboBox == &audioOutputComboBox)
    {
        applyAudioSettings();
        updateSampleRates();
        updateBufferSizes();
    }
    else if (comboBox == &sampleRateComboBox || comboBox == &bufferSizeComboBox ||
             comboBox == &outputChannelsComboBox)
    {
        applyAudioSettings();
    }
}

void SettingsComponent::updateDeviceList()
{
    deviceComboBox.clear();
    
    auto devices = getAvailableDevices();
    int selectedIndex = 0;
    
    for (int i = 0; i < devices.size(); ++i)
    {
        deviceComboBox.addItem(devices[i], i + 1);
        if (devices[i] == currentDevice)
            selectedIndex = i;
    }
    
    deviceComboBox.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
    
    // Update info for initially selected device
    comboBoxChanged(&deviceComboBox);
}

juce::StringArray SettingsComponent::getAvailableDevices()
{
    juce::StringArray devices;
    
    // CPU is always available
    devices.add("CPU");
    
#ifdef HAVE_ONNXRUNTIME
    // Get providers that are compiled into the ONNX Runtime library
    auto availableProviders = Ort::GetAvailableProviders();
    
    // Check which providers are available
    bool hasCuda = false, hasDml = false, hasCoreML = false, hasTensorRT = false;
    
    DBG("Available ONNX Runtime providers:");
    for (const auto& provider : availableProviders)
    {
        DBG("  - " + juce::String(provider));
        
        if (provider == "CUDAExecutionProvider")
            hasCuda = true;
        else if (provider == "DmlExecutionProvider")
            hasDml = true;
        else if (provider == "CoreMLExecutionProvider")
            hasCoreML = true;
        else if (provider == "TensorrtExecutionProvider")
            hasTensorRT = true;
    }
    
    // Add available GPU providers
    if (hasCuda)
        devices.add("CUDA");
    if (hasDml)
        devices.add("DirectML");
    if (hasCoreML)
        devices.add("CoreML");
    if (hasTensorRT)
        devices.add("TensorRT");
    
    // If no GPU providers found, show info about how to enable them
    if (!hasCuda && !hasDml && !hasCoreML && !hasTensorRT)
    {
        DBG("No GPU execution providers available in this ONNX Runtime build.");
        DBG("To enable GPU acceleration:");
        DBG("  - Windows DirectML: Download onnxruntime-directml package");
        DBG("  - NVIDIA CUDA: Download onnxruntime-gpu package");
    }
#endif
    
    return devices;
}

void SettingsComponent::loadSettings()
{
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("PitchEditor")
                            .getChildFile("settings.xml");

    const auto& langs = Localization::getInstance().getAvailableLanguages();

    if (settingsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr)
        {
            currentDevice = xml->getStringAttribute("device", "CPU");
            numThreads = xml->getIntAttribute("threads", 0);
            gpuDeviceId = xml->getIntAttribute("gpuDeviceId", 0);

            // Load language
            juce::String langCode = xml->getStringAttribute("language", "auto");
            if (langCode == "auto")
            {
                Localization::detectSystemLanguage();
                languageComboBox.setSelectedId(1, juce::dontSendNotification);
            }
            else
            {
                Localization::getInstance().setLanguage(langCode);
                // Find combo box index for this language code
                for (int i = 0; i < static_cast<int>(langs.size()); ++i)
                {
                    if (langs[i].code == langCode)
                    {
                        languageComboBox.setSelectedId(i + 2, juce::dontSendNotification);
                        break;
                    }
                }
            }

            DBG("Loaded settings: device=" + currentDevice + ", threads=" + juce::String(numThreads));
        }
    }
    else
    {
        // First run - default to Auto (detect system language)
        Localization::detectSystemLanguage();
        languageComboBox.setSelectedId(1, juce::dontSendNotification);
    }

    // Update the ComboBox selection to match loaded settings
    for (int i = 0; i < deviceComboBox.getNumItems(); ++i)
    {
        if (deviceComboBox.getItemText(i) == currentDevice)
        {
            deviceComboBox.setSelectedItemIndex(i, juce::dontSendNotification);
            break;
        }
    }

    // Update GPU device ID and visibility
    gpuDeviceComboBox.setSelectedId(gpuDeviceId + 1, juce::dontSendNotification);
    bool isGPU = (currentDevice != "CPU");
    gpuDeviceLabel.setVisible(isGPU);
    gpuDeviceComboBox.setVisible(isGPU);
}

void SettingsComponent::saveSettings()
{
    // Don't save if combo box not initialized yet
    if (languageComboBox.getSelectedId() == 0)
        return;

    auto settingsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("PitchEditor");
    settingsDir.createDirectory();

    auto settingsFile = settingsDir.getChildFile("settings.xml");

    juce::XmlElement xml("PitchEditorSettings");
    xml.setAttribute("device", currentDevice);
    xml.setAttribute("threads", numThreads);
    xml.setAttribute("gpuDeviceId", gpuDeviceId);

    // Save language code
    int langId = languageComboBox.getSelectedId();
    juce::String langCode = "auto";
    if (langId >= 2)
    {
        const auto& langs = Localization::getInstance().getAvailableLanguages();
        int langIndex = langId - 2;
        if (langIndex < static_cast<int>(langs.size()))
            langCode = langs[langIndex].code;
    }
    xml.setAttribute("language", langCode);

    xml.writeTo(settingsFile);
}

void SettingsComponent::updateAudioDeviceTypes()
{
    if (deviceManager == nullptr) return;

    audioDeviceTypeComboBox.clear();
    auto& types = deviceManager->getAvailableDeviceTypes();
    for (int i = 0; i < types.size(); ++i)
        audioDeviceTypeComboBox.addItem(types[i]->getTypeName(), i + 1);

    if (auto* currentType = deviceManager->getCurrentDeviceTypeObject())
    {
        for (int i = 0; i < types.size(); ++i)
        {
            if (types[i] == currentType)
            {
                audioDeviceTypeComboBox.setSelectedId(i + 1, juce::dontSendNotification);
                break;
            }
        }
    }
    updateAudioOutputDevices();
}

void SettingsComponent::updateAudioOutputDevices()
{
    if (deviceManager == nullptr) return;

    audioOutputComboBox.clear();
    if (auto* currentType = deviceManager->getCurrentDeviceTypeObject())
    {
        auto devices = currentType->getDeviceNames(false);  // false = output devices
        for (int i = 0; i < devices.size(); ++i)
            audioOutputComboBox.addItem(devices[i], i + 1);

        if (auto* audioDevice = deviceManager->getCurrentAudioDevice())
        {
            auto currentName = audioDevice->getName();
            for (int i = 0; i < devices.size(); ++i)
            {
                if (devices[i] == currentName)
                {
                    audioOutputComboBox.setSelectedId(i + 1, juce::dontSendNotification);
                    break;
                }
            }
        }
    }
    updateSampleRates();
    updateBufferSizes();
}

void SettingsComponent::updateSampleRates()
{
    if (deviceManager == nullptr) return;

    sampleRateComboBox.clear();
    if (auto* device = deviceManager->getCurrentAudioDevice())
    {
        auto rates = device->getAvailableSampleRates();
        double currentRate = device->getCurrentSampleRate();
        for (int i = 0; i < rates.size(); ++i)
        {
            sampleRateComboBox.addItem(juce::String(static_cast<int>(rates[i])) + " Hz", i + 1);
            if (std::abs(rates[i] - currentRate) < 1.0)
                sampleRateComboBox.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }
}

void SettingsComponent::updateBufferSizes()
{
    if (deviceManager == nullptr) return;

    bufferSizeComboBox.clear();
    if (auto* device = deviceManager->getCurrentAudioDevice())
    {
        auto sizes = device->getAvailableBufferSizes();
        int currentSize = device->getCurrentBufferSizeSamples();
        for (int i = 0; i < sizes.size(); ++i)
        {
            bufferSizeComboBox.addItem(juce::String(sizes[i]) + " samples", i + 1);
            if (sizes[i] == currentSize)
                bufferSizeComboBox.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }
}

void SettingsComponent::applyAudioSettings()
{
    if (deviceManager == nullptr) return;

    auto setup = deviceManager->getAudioDeviceSetup();

    // Get selected output device
    if (auto* currentType = deviceManager->getCurrentDeviceTypeObject())
    {
        auto devices = currentType->getDeviceNames(false);
        int outputIdx = audioOutputComboBox.getSelectedId() - 1;
        if (outputIdx >= 0 && outputIdx < devices.size())
            setup.outputDeviceName = devices[outputIdx];
    }

    // Get selected sample rate
    if (auto* device = deviceManager->getCurrentAudioDevice())
    {
        auto rates = device->getAvailableSampleRates();
        int rateIdx = sampleRateComboBox.getSelectedId() - 1;
        if (rateIdx >= 0 && rateIdx < rates.size())
            setup.sampleRate = rates[rateIdx];

        auto sizes = device->getAvailableBufferSizes();
        int sizeIdx = bufferSizeComboBox.getSelectedId() - 1;
        if (sizeIdx >= 0 && sizeIdx < sizes.size())
            setup.bufferSize = sizes[sizeIdx];
    }

    // Output channels
    int channels = outputChannelsComboBox.getSelectedId();
    setup.outputChannels.setRange(0, channels, true);

    deviceManager->setAudioDeviceSetup(setup, true);
}

//==============================================================================
// SettingsDialog
//==============================================================================

SettingsDialog::SettingsDialog(juce::AudioDeviceManager* audioDeviceManager)
    : DialogWindow("Settings", juce::Colour(COLOR_BACKGROUND), true)
{
    settingsComponent = std::make_unique<SettingsComponent>(audioDeviceManager);
    setContentOwned(settingsComponent.get(), false);
    setUsingNativeTitleBar(true);
    setResizable(false, false);

    if (audioDeviceManager != nullptr)
        centreWithSize(400, 580);
    else
        centreWithSize(400, 280);
}

void SettingsDialog::closeButtonPressed()
{
    setVisible(false);
}
