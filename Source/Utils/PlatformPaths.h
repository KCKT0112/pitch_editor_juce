#pragma once

#include "../JuceHeader.h"

/**
 * Platform-specific path utilities.
 *
 * macOS:
 *   - Models: App.app/Contents/Resources/models/
 *   - Logs: ~/Library/Logs/PitchEditor/
 *   - Config: ~/Library/Application Support/PitchEditor/
 *
 * Windows:
 *   - Models: <exe_dir>/models/
 *   - Logs: %APPDATA%/PitchEditor/Logs/
 *   - Config: %APPDATA%/PitchEditor/
 *
 * Linux:
 *   - Models: <exe_dir>/models/
 *   - Logs: ~/.config/PitchEditor/logs/
 *   - Config: ~/.config/PitchEditor/
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
        // macOS: ~/Library/Logs/PitchEditor/
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile("Library/Logs/PitchEditor");
    #elif JUCE_WINDOWS
        // Windows: %APPDATA%/PitchEditor/Logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("PitchEditor/Logs");
    #else
        // Linux: ~/.config/PitchEditor/logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("PitchEditor/logs");
    #endif
    }

    inline juce::File getConfigDirectory()
    {
        // All platforms use userApplicationDataDirectory
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("PitchEditor");
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
