#pragma once

#include <cstdint>
#include <string>

namespace curvy {

    enum class SettingsPage { Appearance, Workspace, Pen, Drawing, Files };

    // The per-user preferences and the state the app keeps between sessions, in one json5 file (<local app data>/curvy/settings.json5, or
    // the path given with --settings). The window and the dock areas are kept here too, so a session starts as the last one ended. Unknown
    // keys and unparsable or out-of-range values are skipped, so a hand-edited or newer file still loads.
    // @reflect ignore=true
    struct Settings {
        enum class ThemeChoice { Dark, Light, FollowWindows };
        enum class ScrubAxis { Vertical, Horizontal };
        enum class PenButton { Rings, Pan, Nothing };
        enum class EraserAction { Erase, Undo, Nothing };

        // The window as the user left it, in screen pixels (the normal rectangle when maximized); known() is false until a session has
        // saved it. `monitor` is the screen it was on (its device name, such as \\.\DISPLAY2); empty when not known.
        struct WindowPlacement {
            int x = 0, y = 0, width = 0, height = 0;
            bool maximized = false;
            std::string monitor;
            bool known() const { return width > 0 && height > 0; }
        };
        // One dock area (top, left, right, bottom): the size it opens at, and whether it is hidden.
        struct DockArea {
            float size = 240.0f;
            bool collapsed = true;
        };
        static constexpr int dockCount = 4;
        static constexpr float minDockSize = 60.0f;
        static const char* dockName(int index);   // "top", "left", "right", "bottom"

        // Appearance
        ThemeChoice theme = ThemeChoice::Dark;
        std::uint32_t canvasBackground = 0x2A2A2A;
        bool canvasFollowsTheme = true;
        int propertyTextSize = 11;

        // Workspace
        bool rulersVisible = true;
        bool hintsFade = true;
        int recentLength = 10;

        // Pen
        ScrubAxis scrubAxis = ScrubAxis::Vertical;
        int scrubSensitivity = 100;
        int spaceTapMs = 300;
        PenButton barrelButton = PenButton::Rings;
        EraserAction eraserEnd = EraserAction::Erase;

        // Drawing
        bool snapGrid = false;
        double gridSize = 10.0;
        bool snapGuides = true;
        bool toolDefaultsInDocument = false;
        int polygonSides = 5;   // the tool defaults kept per user (in the document instead when toolDefaultsInDocument)
        int starPoints = 5;
        int cornerRadius = 12;      // the other tool-option card values (Rounded Rectangle, Star inner radius %, Pencil smoothing); always per user
        int starInner = 50;
        int pencilSmoothing = 40;

        // Files
        bool autosave = true;
        int autosaveMinutes = 5;
        std::string exportFolder;

        // State
        WindowPlacement window;
        DockArea docks[dockCount];

        Settings();

        // Puts one page back to the defaults.
        void resetPage(SettingsPage page);

        // Whether the UI should be dark under this choice (FollowWindows asks the system).
        bool darkTheme() const;

        std::string toText() const;
        void fromText(const std::string& text);

        static std::string defaultPath();
        static Settings load(const std::string& path);
        bool save(const std::string& path) const;
    };

}
