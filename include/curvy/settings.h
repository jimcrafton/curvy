#pragma once

#include "curvy/workspacemodel.h"

#include <cstdint>
#include <string>

namespace curvy {

    enum class SettingsPage { Appearance, Workspace, Pen, Drawing, Files };

    // The per-user preferences behind the settings dialog (docs/ui-design.md "Settings / Preferences dialog"). Plain values; the
    // dialog edits a copy and the workspace applies it. Stored as key=value lines.
    // @reflect ignore=true
    struct Settings {
        enum class ThemeChoice { Dark, Light, FollowWindows };
        enum class PenButton { Rings, Pan, Nothing };
        enum class EraserAction { Erase, Undo, Nothing };

        ThemeChoice theme = ThemeChoice::Dark;
        std::uint32_t canvasBackground = 0x2A2A2A;
        bool canvasFollowsTheme = true;
        int propertyTextSize = 11;
        bool blurPanels = false;

        bool toolbarVisible = false;
        bool propertiesVisible = false;
        bool rulersVisible = true;
        bool hintsFade = true;
        int recentLength = 10;

        WorkspaceModel::ScrubAxis scrubAxis = WorkspaceModel::ScrubAxis::Vertical;
        int scrubSensitivity = 100;
        int holdDelayMs = 400;
        int spaceTapMs = 300;
        PenButton barrelButton = PenButton::Rings;
        EraserAction eraserEnd = EraserAction::Erase;

        bool snapGrid = false;
        double gridSize = 10.0;
        bool snapGuides = true;
        bool toolDefaultsInDocument = false;

        bool welcomeShown = false;   // the first-run overlay has been shown
        bool autosave = true;
        int autosaveMinutes = 5;
        std::string exportFolder;

        // Puts one page back to the defaults.
        void resetPage(SettingsPage page);

        // Whether the UI should be dark under this choice (FollowWindows asks the system).
        bool darkTheme() const;

        std::string toText() const;
        // Unknown keys and unparsable or out-of-range values are skipped, so a hand-edited or newer file still loads.
        void fromText(const std::string& text);

        static std::string defaultPath();  // %APPDATA%\curvy\settings.ini
        static Settings load(const std::string& path);
        bool save(const std::string& path) const;
    };

}
