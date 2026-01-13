#include "SettingsManager.h"
#include "../../Utils/AppLogger.h"

SettingsManager::SettingsManager() {
    loadSettings();
    loadConfig();
}

juce::File SettingsManager::getSettingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HachiTune")
        .getChildFile("settings.xml");
}

juce::File SettingsManager::getConfigFile() {
    return PlatformPaths::getConfigFile("config.json");
}

void SettingsManager::loadSettings() {
    auto settingsFile = getSettingsFile();

    if (settingsFile.existsAsFile()) {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr) {
            device = xml->getStringAttribute("device", "CPU");
            threads = xml->getIntAttribute("threads", 0);

            // Load pitch detector type
            juce::String pitchDetectorStr = xml->getStringAttribute("pitchDetector", "RMVPE");
            pitchDetectorType = stringToPitchDetectorType(pitchDetectorStr);
            LOG("SettingsManager: Loaded pitchDetector = " + pitchDetectorStr);
        }
    } else {
        LOG("SettingsManager: Settings file not found, using defaults (RMVPE)");
    }
}

void SettingsManager::applySettings() {
    loadSettings();

    if (vocoder) {
        vocoder->setExecutionDevice(device);
        if (vocoder->isLoaded())
            vocoder->reloadModel();
    }

    if (onSettingsChanged)
        onSettingsChanged();
}

void SettingsManager::loadConfig() {
    auto configFile = getConfigFile();

    if (configFile.existsAsFile()) {
        auto configText = configFile.loadFileAsString();
        auto config = juce::JSON::parse(configText);

        if (config.isObject()) {
            auto configObj = config.getDynamicObject();
            if (configObj) {
                auto lastFile = configObj->getProperty("lastFile").toString();
                if (lastFile.isNotEmpty())
                    lastFilePath = juce::File(lastFile);

                if (configObj->hasProperty("windowWidth"))
                    windowWidth = static_cast<int>(configObj->getProperty("windowWidth"));
                if (configObj->hasProperty("windowHeight"))
                    windowHeight = static_cast<int>(configObj->getProperty("windowHeight"));
            }
        }
    }
}

void SettingsManager::saveConfig() {
    auto configFile = getConfigFile();

    juce::DynamicObject::Ptr config = new juce::DynamicObject();

    if (lastFilePath.existsAsFile())
        config->setProperty("lastFile", lastFilePath.getFullPathName());

    config->setProperty("windowWidth", windowWidth);
    config->setProperty("windowHeight", windowHeight);

    juce::String jsonText = juce::JSON::toString(juce::var(config.get()));
    configFile.replaceWithText(jsonText);
}
