#include "SettingsComponent.h"
#include "../Utils/Constants.h"
#include "../Utils/Localization.h"

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

//==============================================================================
// SettingsComponent
//==============================================================================

SettingsComponent::SettingsComponent(juce::AudioDeviceManager* audioDeviceManager)
    : deviceManager(audioDeviceManager), pluginMode(audioDeviceManager == nullptr)
{
    // Set component to opaque (required for native title bar)
    setOpaque(true);
    
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

    // GPU device list will be populated dynamically based on available devices
    gpuDeviceComboBox.addListener(this);
    addAndMakeVisible(gpuDeviceComboBox);
    gpuDeviceLabel.setVisible(false);
    gpuDeviceComboBox.setVisible(false);

    // Pitch detector selection
    pitchDetectorLabel.setText(TR("settings.pitch_detector"), juce::dontSendNotification);
    pitchDetectorLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(pitchDetectorLabel);

    pitchDetectorComboBox.addItem("RMVPE", 1);
    pitchDetectorComboBox.addItem("FCPE", 2);
    pitchDetectorComboBox.setSelectedId(1, juce::dontSendNotification);  // Default to RMVPE
    pitchDetectorComboBox.addListener(this);
    addAndMakeVisible(pitchDetectorComboBox);

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

    // Set size based on mode
    if (pluginMode)
        setSize(400, 260);
    else
        setSize(400, 560);
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

    // Pitch detector row
    auto pitchDetectorRow = bounds.removeFromTop(30);
    pitchDetectorLabel.setBounds(pitchDetectorRow.removeFromLeft(120));
    pitchDetectorComboBox.setBounds(pitchDetectorRow.reduced(0, 2));
    bounds.removeFromTop(10);

    bounds.removeFromTop(5);

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
        bool showGpuDeviceList = (currentDevice == "CUDA" || currentDevice == "DirectML");
        if (showGpuDeviceList)
        {
            // Update GPU device list for the selected device type
            updateGPUDeviceList(currentDevice);
        }
        gpuDeviceLabel.setVisible(showGpuDeviceList);
        gpuDeviceComboBox.setVisible(showGpuDeviceList);
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
    else if (comboBox == &pitchDetectorComboBox)
    {
        int selectedId = pitchDetectorComboBox.getSelectedId();
        if (selectedId == 1)
            pitchDetectorType = PitchDetectorType::RMVPE;
        else if (selectedId == 2)
            pitchDetectorType = PitchDetectorType::FCPE;

        saveSettings();

        if (onPitchDetectorChanged)
            onPitchDetectorChanged(pitchDetectorType);
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

    // Auto-select based on compile-time flags
    if (currentDevice == "CPU")
    {
#ifdef USE_DIRECTML
        // If DirectML is compiled in, default to DirectML
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i] == "DirectML")
            {
                selectedIndex = i;
                currentDevice = devices[i];
                DBG("Auto-selecting DirectML (compiled in)");
                break;
            }
        }
#elif defined(USE_CUDA)
        // If CUDA is compiled in, default to CUDA
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i] == "CUDA")
            {
                selectedIndex = i;
                currentDevice = devices[i];
                DBG("Auto-selecting CUDA (compiled in)");
                break;
            }
        }
#else
        // No GPU compiled in, stay on CPU
        DBG("No GPU provider compiled in, using CPU");
#endif
    }

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

void SettingsComponent::updateGPUDeviceList(const juce::String& deviceType)
{
    gpuDeviceComboBox.clear();
    
    if (deviceType == "CPU")
    {
        // No GPU devices for CPU
        return;
    }
    
#ifdef HAVE_ONNXRUNTIME
    if (deviceType == "CUDA")
    {
#ifdef USE_CUDA
        int deviceCount = 0;
        bool devicesDetected = false;

        // Try to load CUDA runtime library to get actual device count and names
#ifdef _WIN32
        const char* cudaDllNames[] = {
            "cudart64_12.dll",  // CUDA 12.x
            "cudart64_11.dll",  // CUDA 11.x
            "cudart64_10.dll",  // CUDA 10.x
            "cudart64.dll"      // Generic
        };

        HMODULE cudaLib = nullptr;
        for (const char* dllName : cudaDllNames)
        {
            cudaLib = LoadLibraryA(dllName);
            if (cudaLib)
            {
                DBG("Loaded CUDA runtime: " + juce::String(dllName));
                break;
            }
        }

        if (cudaLib)
        {
            typedef int (*cudaGetDeviceCountFunc)(int*);
            typedef int (*cudaGetDevicePropertiesFunc)(void*, int);

            auto cudaGetDeviceCount = (cudaGetDeviceCountFunc)GetProcAddress(cudaLib, "cudaGetDeviceCount");

            if (cudaGetDeviceCount)
            {
                int result = cudaGetDeviceCount(&deviceCount);
                if (result == 0 && deviceCount > 0)
                {
                    DBG("CUDA device count: " + juce::String(deviceCount));

                    // Try to get device properties for names
                    auto cudaGetDeviceProperties = (cudaGetDevicePropertiesFunc)GetProcAddress(cudaLib, "cudaGetDeviceProperties");

                    for (int deviceId = 0; deviceId < deviceCount; ++deviceId)
                    {
                        juce::String deviceName = "GPU " + juce::String(deviceId);

                        // Try to get device name from properties
                        if (cudaGetDeviceProperties)
                        {
                            // Allocate full cudaDeviceProp structure (it's large, ~1KB)
                            // We can't use the actual struct without CUDA headers, so allocate enough space
                            char propBuffer[2048];  // Large enough for cudaDeviceProp
                            memset(propBuffer, 0, sizeof(propBuffer));

                            if (cudaGetDeviceProperties(propBuffer, deviceId) == 0)
                            {
                                // Device name is at the start of the structure
                                char* name = propBuffer;
                                if (name[0] != '\0')
                                {
                                    deviceName = juce::String(name);
                                    DBG("CUDA device " + juce::String(deviceId) + ": " + deviceName);
                                }
                            }
                        }

                        gpuDeviceComboBox.addItem(deviceName, deviceId + 1);
                    }
                    devicesDetected = true;
                }
                else
                {
                    DBG("cudaGetDeviceCount failed or returned 0 devices");
                }
            }
            FreeLibrary(cudaLib);
        }
        else
        {
            DBG("Failed to load CUDA runtime library");
        }
#endif

        // If no devices detected, add default
        if (!devicesDetected || gpuDeviceComboBox.getNumItems() == 0)
        {
            gpuDeviceComboBox.addItem("GPU 0 (CUDA)", 1);
            DBG("No CUDA devices detected, using default GPU 0");
        }
#else
        // CUDA not compiled in, but provider is available
        // This shouldn't happen, but add default option
        gpuDeviceComboBox.addItem("GPU 0 (CUDA)", 1);
        DBG("CUDA provider available but USE_CUDA not defined");
#endif
    }
    else if (deviceType == "DirectML")
    {
#ifdef USE_DIRECTML
        // DirectML: Most systems have 1 GPU, but we'll check up to 4
        // DirectML doesn't provide easy enumeration, so we'll use device IDs
        // In practice, DirectML usually uses device 0
        for (int deviceId = 0; deviceId < 4; ++deviceId)
        {
            gpuDeviceComboBox.addItem("GPU " + juce::String(deviceId) + " (DirectML)", deviceId + 1);
        }
#else
        // DirectML not compiled in
        gpuDeviceComboBox.addItem("GPU 0 (DirectML)", 1);
        DBG("DirectML provider available but USE_DIRECTML not defined");
#endif
    }
    else
    {
        // Other GPU providers (CoreML, TensorRT) - use default device
        gpuDeviceComboBox.addItem("Default GPU", 1);
    }
    
    // Set default selection
    if (gpuDeviceComboBox.getNumItems() > 0)
    {
        // Try to restore saved selection, or use first device
        int savedId = gpuDeviceId + 1;
        if (savedId > 0 && savedId <= gpuDeviceComboBox.getNumItems())
            gpuDeviceComboBox.setSelectedId(savedId, juce::dontSendNotification);
        else
            gpuDeviceComboBox.setSelectedId(1, juce::dontSendNotification);
    }
#endif
}

juce::StringArray SettingsComponent::getAvailableDevices()
{
    juce::StringArray devices;
    
    // CPU is always available
    devices.add("CPU");
    
#ifdef HAVE_ONNXRUNTIME
    try {
        // Get providers that are compiled into the ONNX Runtime library
        auto availableProviders = Ort::GetAvailableProviders();
        
        // Check which providers are available
        bool hasCuda = false, hasDml = false, hasCoreML = false, hasTensorRT = false;
        
        DBG("=== ONNX Runtime Provider Detection ===");
        DBG("Total providers found: " + juce::String(availableProviders.size()));
        DBG("Available ONNX Runtime providers:");
        for (const auto& provider : availableProviders)
        {
            juce::String providerStr(provider);
            DBG("  - " + providerStr);
            
            if (providerStr == "CUDAExecutionProvider")
                hasCuda = true;
            else if (providerStr == "DmlExecutionProvider")
                hasDml = true;
            else if (providerStr == "CoreMLExecutionProvider")
                hasCoreML = true;
            else if (providerStr == "TensorrtExecutionProvider")
                hasTensorRT = true;
        }
        
        // Add available GPU providers based on compile-time flags
        // DML and CUDA are mutually exclusive
#ifdef USE_DIRECTML
        if (hasDml)
        {
            devices.add("DirectML");
            DBG("DirectML provider: ENABLED");
        }
#elif defined(USE_CUDA)
        if (hasCuda)
        {
            devices.add("CUDA");
            DBG("CUDA provider: ENABLED");
        }
#else
        // No GPU compiled in, but show available providers for information
        if (hasCuda)
        {
            devices.add("CUDA");
            DBG("CUDA provider: AVAILABLE (not compiled in)");
        }
        if (hasDml)
        {
            devices.add("DirectML");
            DBG("DirectML provider: AVAILABLE (not compiled in)");
        }
#endif
        if (hasCoreML)
        {
            devices.add("CoreML");
            DBG("CoreML provider: ENABLED");
        }
        if (hasTensorRT)
        {
            devices.add("TensorRT");
            DBG("TensorRT provider: ENABLED");
        }
        
        // If no GPU providers found, show info about how to enable them
        if (!hasCuda && !hasDml && !hasCoreML && !hasTensorRT)
        {
            DBG("WARNING: No GPU execution providers available in this ONNX Runtime build.");
            DBG("This appears to be a CPU-only build of ONNX Runtime.");
            DBG("To enable GPU acceleration:");
            DBG("  - Windows DirectML: Use onnxruntime-directml package");
            DBG("  - NVIDIA CUDA: Use onnxruntime-gpu package (requires CUDA toolkit)");
        }
    }
    catch (const std::exception& e)
    {
        DBG("ERROR: Failed to get ONNX Runtime providers: " + juce::String(e.what()));
        DBG("Falling back to CPU-only mode.");
    }
#else
    DBG("WARNING: HAVE_ONNXRUNTIME not defined - only CPU available");
    DBG("To enable GPU support, ensure ONNX Runtime is properly configured in CMakeLists.txt");
#endif
    
    DBG("Final device list: " + devices.joinIntoString(", "));
    return devices;
}

void SettingsComponent::loadSettings()
{
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("HachiTune")
                            .getChildFile("settings.xml");

    const auto& langs = Localization::getInstance().getAvailableLanguages();

    if (settingsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr)
        {
            currentDevice = xml->getStringAttribute("device", "CPU");
            gpuDeviceId = xml->getIntAttribute("gpuDeviceId", 0);

            // Load pitch detector type
            juce::String pitchDetectorStr = xml->getStringAttribute("pitchDetector", "RMVPE");
            pitchDetectorType = stringToPitchDetectorType(pitchDetectorStr);

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

            DBG("Loaded settings: device=" + currentDevice);
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
    bool showGpuDeviceList = (currentDevice == "CUDA" || currentDevice == "DirectML");
    if (showGpuDeviceList)
    {
        // Update GPU device list for the loaded device type
        updateGPUDeviceList(currentDevice);
    }
    gpuDeviceComboBox.setSelectedId(gpuDeviceId + 1, juce::dontSendNotification);
    gpuDeviceLabel.setVisible(showGpuDeviceList);
    gpuDeviceComboBox.setVisible(showGpuDeviceList);

    // Update pitch detector combo box
    if (pitchDetectorType == PitchDetectorType::RMVPE)
        pitchDetectorComboBox.setSelectedId(1, juce::dontSendNotification);
    else if (pitchDetectorType == PitchDetectorType::FCPE)
        pitchDetectorComboBox.setSelectedId(2, juce::dontSendNotification);
}

void SettingsComponent::saveSettings()
{
    // Don't save if combo box not initialized yet
    if (languageComboBox.getSelectedId() == 0)
        return;

    auto settingsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("HachiTune");
    settingsDir.createDirectory();

    auto settingsFile = settingsDir.getChildFile("settings.xml");

    juce::XmlElement xml("HachiTuneSettings");
    xml.setAttribute("device", currentDevice);
    xml.setAttribute("gpuDeviceId", gpuDeviceId);
    xml.setAttribute("pitchDetector", pitchDetectorTypeToString(pitchDetectorType));

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
    // Set opaque before any other operations - this must be done first
    setOpaque(true);

    // Create and configure content component
    settingsComponent = std::make_unique<SettingsComponent>(audioDeviceManager);

    // Ensure content component is also opaque before setting it
    if (settingsComponent)
        settingsComponent->setOpaque(true);

    // Set content before native title bar
    setContentOwned(settingsComponent.get(), false);

    // Now set native title bar after content is set and opaque
    setUsingNativeTitleBar(true);

    // Set window properties
    setResizable(false, false);

    if (audioDeviceManager != nullptr)
        centreWithSize(400, 560);
    else
        centreWithSize(400, 260);
}

void SettingsDialog::closeButtonPressed()
{
    setVisible(false);
}

void SettingsDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(COLOR_BACKGROUND));
}
