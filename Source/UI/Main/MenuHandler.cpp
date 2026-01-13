#include "MenuHandler.h"

MenuHandler::MenuHandler() = default;

juce::StringArray MenuHandler::getMenuBarNames() {
    if (pluginMode)
        return {TRANS("Edit"), TRANS("Settings")};
    return {TRANS("File"), TRANS("Edit"), TRANS("Settings")};
}

juce::PopupMenu MenuHandler::getMenuForIndex(int menuIndex, const juce::String& /*menuName*/) {
    juce::PopupMenu menu;

    if (pluginMode) {
        if (menuIndex == 0) {
            // Edit menu
            bool canUndo = undoManager && undoManager->canUndo();
            bool canRedo = undoManager && undoManager->canRedo();
            menu.addItem(MenuUndo, TRANS("Undo"), canUndo);
            menu.addItem(MenuRedo, TRANS("Redo"), canRedo);
        } else if (menuIndex == 1) {
            // Settings menu
            menu.addItem(MenuSettings, TRANS("Settings..."));
        }
    } else {
        if (menuIndex == 0) {
            // File menu
            menu.addItem(MenuOpen, TRANS("Open..."));
            menu.addItem(MenuSave, TRANS("Save Project"));
            menu.addItem(MenuExport, TRANS("Export..."));
            menu.addSeparator();
            menu.addItem(MenuQuit, TRANS("Quit"));
        } else if (menuIndex == 1) {
            // Edit menu
            bool canUndo = undoManager && undoManager->canUndo();
            bool canRedo = undoManager && undoManager->canRedo();
            menu.addItem(MenuUndo, TRANS("Undo"), canUndo);
            menu.addItem(MenuRedo, TRANS("Redo"), canRedo);
        } else if (menuIndex == 2) {
            // Settings menu
            menu.addItem(MenuSettings, TRANS("Settings..."));
        }
    }

    return menu;
}

void MenuHandler::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/) {
    switch (menuItemID) {
        case MenuOpen:
            if (onOpenFile) onOpenFile();
            break;
        case MenuSave:
            if (onSaveProject) onSaveProject();
            break;
        case MenuExport:
            if (onExportFile) onExportFile();
            break;
        case MenuQuit:
            if (onQuit) onQuit();
            break;
        case MenuUndo:
            if (onUndo) onUndo();
            break;
        case MenuRedo:
            if (onRedo) onRedo();
            break;
        case MenuSettings:
            if (onShowSettings) onShowSettings();
            break;
        default:
            break;
    }
}
