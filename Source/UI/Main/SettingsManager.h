#pragma once

#include "../../JuceHeader.h"
#include "../../Audio/Vocoder.h"
#include "../../Utils/PlatformPaths.h"
#include <functional>

/**
 * Manages application settings and configuration persistence.
 */
class SettingsManager {
public:
    SettingsManager();
    ~SettingsManager() = default;

    void setVocoder(Vocoder* v) { vocoder = v; }

    // Settings (settings.xml - vocoder device/threads)
    void loadSettings();
    void applySettings();
    juce::String getDevice() const { return device; }
    int getThreads() const { return threads; }

    // Config (config.json - window state, last file)
    void loadConfig();
    void saveConfig();
    void setLastFilePath(const juce::File& file) { lastFilePath = file; }
    juce::File getLastFilePath() const { return lastFilePath; }
    void setWindowSize(int w, int h) { windowWidth = w; windowHeight = h; }
    int getWindowWidth() const { return windowWidth; }
    int getWindowHeight() const { return windowHeight; }

    // Callbacks
    std::function<void()> onSettingsChanged;

private:
    static juce::File getSettingsFile();
    static juce::File getConfigFile();

    Vocoder* vocoder = nullptr;

    // Settings
    juce::String device = "CPU";
    int threads = 0;

    // Config
    juce::File lastFilePath;
    int windowWidth = 1200;
    int windowHeight = 800;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsManager)
};
