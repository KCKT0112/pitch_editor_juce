#include "JuceHeader.h"
#include "UI/MainComponent.h"
#include "Utils/Constants.h"
#include "Utils/Localization.h"

#if JUCE_MAC
#include <Cocoa/Cocoa.h>
#endif

#if JUCE_WINDOWS
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

class PitchEditorApplication : public juce::JUCEApplication
{
public:
    PitchEditorApplication() {}

    const juce::String getApplicationName() override
    {
        return "Pitch Editor";
    }

    const juce::String getApplicationVersion() override
    {
        return "1.0.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        // Load language from settings BEFORE creating UI
        Localization::loadFromSettings();
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                            juce::Colour(COLOR_BACKGROUND),  // Match content background
#if JUCE_MAC
                            DocumentWindow::allButtons)  // Need buttons for traffic lights
        {
            setUsingNativeTitleBar(true);  // Must be true for traffic lights
#else
                            0)  // No JUCE buttons on Windows/Linux - we use custom title bar
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
#endif
            setContentOwned(new MainComponent(), true);

            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());

            setVisible(true);

#if JUCE_MAC
            // Make title bar transparent and extend content into it
            if (auto* peer = getPeer())
            {
                if (auto* nsView = (NSView*)peer->getNativeHandle())
                {
                    if (auto* nsWindow = [nsView window])
                    {
                        nsWindow.titlebarAppearsTransparent = YES;
                        nsWindow.titleVisibility = NSWindowTitleHidden;
                        nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;

                        // Disable fullscreen mode - green button will zoom instead
                        nsWindow.collectionBehavior = (nsWindow.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenPrimary) | NSWindowCollectionBehaviorFullScreenAuxiliary;

                        // Set window background color to match toolbar
                        nsWindow.backgroundColor = [NSColor colorWithRed:0x1A/255.0
                                                                   green:0x1A/255.0
                                                                    blue:0x24/255.0
                                                                   alpha:1.0];
                    }
                }
            }
#elif JUCE_WINDOWS
            // Enable rounded corners on Windows 11+
            if (auto* peer = getPeer())
            {
                if (auto hwnd = (HWND)peer->getNativeHandle())
                {
                    DWORD preference = 2;  // DWMWCP_ROUND
                    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                                         &preference, sizeof(preference));
                }
            }
#endif
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PitchEditorApplication)
