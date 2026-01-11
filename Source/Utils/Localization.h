#pragma once

#include "../JuceHeader.h"
#include <map>
#include <vector>

class Localization
{
public:
    static Localization& getInstance()
    {
        static Localization instance;
        return instance;
    }

    struct LangInfo
    {
        juce::String code;
        juce::String nativeName;
    };

    void setLanguage(const juce::String& langCode)
    {
        if (languages.count(langCode))
        {
            currentLang = langCode;
            loadLanguageFile(langCode);
        }
    }

    juce::String getLanguage() const { return currentLang; }

    juce::String get(const juce::String& key) const
    {
        auto it = strings.find(key);
        if (it != strings.end())
            return it->second;
        return key;
    }

    const std::vector<LangInfo>& getAvailableLanguages() const
    {
        return availableLanguages;
    }

    static void detectSystemLanguage()
    {
        auto& inst = getInstance();
        auto locale = juce::SystemStats::getUserLanguage();

        juce::String langCode = "en";
        if (locale.startsWith("zh"))
            langCode = "zh";
        else if (locale.startsWith("ja"))
            langCode = "ja";

        inst.setLanguage(langCode);
    }

    // Load language from saved settings (call before UI creation)
    static void loadFromSettings()
    {
        auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("HachiTune")
                                .getChildFile("settings.xml");

        if (settingsFile.existsAsFile())
        {
            auto xml = juce::XmlDocument::parse(settingsFile);
            if (xml != nullptr)
            {
                juce::String langCode = xml->getStringAttribute("language", "auto");
                if (langCode == "auto")
                    detectSystemLanguage();
                else
                    getInstance().setLanguage(langCode);
                return;
            }
        }
        // No settings file - use system language
        detectSystemLanguage();
    }

    void scanAvailableLanguages()
    {
        availableLanguages.clear();
        std::vector<juce::String> knownCodes = { "en", "zh", "zh-TW", "ja" };

        for (const auto& code : knownCodes)
        {
            auto langFile = findLanguageFile(code);
            if (langFile.existsAsFile())
            {
                auto jsonText = langFile.loadFileAsString();
                auto json = juce::JSON::parse(jsonText);

                juce::String nativeName = code;
                if (auto* obj = json.getDynamicObject())
                {
                    auto nameKey = "lang." + code;
                    if (obj->hasProperty(nameKey))
                        nativeName = obj->getProperty(nameKey).toString();
                }

                availableLanguages.push_back({ code, nativeName });
                languages[code] = nativeName;
            }
        }
    }

private:
    Localization()
    {
        scanAvailableLanguages();
        if (!availableLanguages.empty())
            loadLanguageFile(availableLanguages[0].code);
    }

    void loadLanguageFile(const juce::String& langCode)
    {
        strings.clear();

        auto langFile = findLanguageFile(langCode);
        if (!langFile.existsAsFile())
            return;

        auto jsonText = langFile.loadFileAsString();
        auto json = juce::JSON::parse(jsonText);

        if (auto* obj = json.getDynamicObject())
        {
            for (const auto& prop : obj->getProperties())
                strings[prop.name.toString()] = prop.value.toString();
        }

        currentLang = langCode;
    }

    juce::File findLanguageFile(const juce::String& langCode)
    {
        auto fileName = langCode + ".json";

#if JUCE_MAC
        auto bundleDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        auto resourceFile = bundleDir.getChildFile("Contents/Resources/lang").getChildFile(fileName);
        if (resourceFile.existsAsFile())
            return resourceFile;
#endif

        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto exeFile = exeDir.getChildFile("lang").getChildFile(fileName);
        if (exeFile.existsAsFile())
            return exeFile;

        auto cwdFile = juce::File::getCurrentWorkingDirectory().getChildFile("Resources/lang").getChildFile(fileName);
        if (cwdFile.existsAsFile())
            return cwdFile;

        return {};
    }

    juce::String currentLang = "en";
    std::map<juce::String, juce::String> strings;
    std::map<juce::String, juce::String> languages;
    std::vector<LangInfo> availableLanguages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Localization)
};

#define TR(key) Localization::getInstance().get(key)
