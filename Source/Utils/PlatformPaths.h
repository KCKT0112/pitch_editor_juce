#pragma once

#include "../JuceHeader.h"

/**
 * Platform-specific path utilities.
 *
 * macOS:
 *   - Models: App.app/Contents/Resources/models/
 *   - Logs: ~/Library/Logs/HachiTune/
 *   - Config: ~/Library/Application Support/HachiTune/
 *
 * Windows:
 *   - Models: <exe_dir>/models/
 *   - Logs: %APPDATA%/HachiTune/Logs/
 *   - Config: %APPDATA%/HachiTune/
 *
 * Linux:
 *   - Models: <exe_dir>/models/
 *   - Logs: ~/.config/HachiTune/logs/
 *   - Config: ~/.config/HachiTune/
 */
namespace PlatformPaths
{
    inline juce::File getModelsDirectory()
    {
    #if JUCE_MAC
        // macOS: Use Resources folder inside app bundle
        auto appBundle = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        return appBundle.getChildFile("Contents/Resources/models");
    #else
        // Windows/Linux: Use models folder next to executable
        return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                   .getParentDirectory()
                   .getChildFile("models");
    #endif
    }

    inline juce::File getLogsDirectory()
    {
    #if JUCE_MAC
        // macOS: ~/Library/Logs/HachiTune/
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile("Library/Logs/HachiTune");
    #elif JUCE_WINDOWS
        // Windows: %APPDATA%/HachiTune/Logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("HachiTune/Logs");
    #else
        // Linux: ~/.config/HachiTune/logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("HachiTune/logs");
    #endif
    }

    inline juce::File getConfigDirectory()
    {
        // All platforms use userApplicationDataDirectory
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("HachiTune");
    }

    inline juce::File getLogFile(const juce::String& name)
    {
        auto logsDir = getLogsDirectory();
        logsDir.createDirectory();
        return logsDir.getChildFile(name);
    }

    inline juce::File getConfigFile(const juce::String& name)
    {
        auto configDir = getConfigDirectory();
        configDir.createDirectory();
        return configDir.getChildFile(name);
    }
}
