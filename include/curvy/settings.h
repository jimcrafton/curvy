#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace curvy {

    enum class SettingsPage { Appearance, Workspace, Pen, Drawing, Files, ScriptEditor };

    // The per-user preferences and the state the app keeps between sessions, in one json5 file (<local app data>/curvy/settings.json5, or
    // the path given with --settings). The window and the dock areas are kept here too, so a session starts as the last one ended. Unknown
    // keys and unparsable or out-of-range values are skipped, so a hand-edited or newer file still loads.
    // @reflect ignore=true
    struct Settings {
        enum class ThemeChoice { Dark, Light, FollowWindows };
        enum class ScrubAxis { Vertical, Horizontal };
        enum class PenButton { Rings, Pan, Nothing };
        enum class EraserAction { Erase, Undo, Nothing };
        enum class SelectRegion { Rectangle, Ellipse, Freehand, Polygon };
        enum class SelectContainment { Inside, Touches };

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
        // How the user left one dockable panel. The list is in dock order; a panel the app no longer knows is ignored when it is applied.
        struct PanelState {
            std::string id;
            bool collapsed = false;
            bool userHidden = false;
            bool floating = false;
            float floatX = 0.0f, floatY = 0.0f;   // the window's screen position while floating
            float bodyHeight = 0.0f;               // 0: the panel's own default
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
        // The Select tool's marquee. With selectTwoButton the primary button takes what the region fully contains and the secondary button
        // (right mouse, pen barrel when it opens rings) takes what it touches; without it both buttons use selectContainment and the
        // secondary button keeps opening the ring on press.
        SelectRegion selectRegion = SelectRegion::Rectangle;
        bool selectTwoButton = true;
        SelectContainment selectContainment = SelectContainment::Inside;

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

        // Script editor (the Python Script Editor panel)
        std::string editorFont;       // a face as newui names it (JetBrainsMono-Medium); empty: the first installed of the editor's preferred ones
        int editorFontSize = 14;
        int editorTabWidth = 4;       // spaces per indent level: what Tab inserts and Enter adds after a colon
        // Off by default: scripts run until they end or are cancelled. On, a script still running after scriptTimeoutSeconds is stopped
        // the way Cancel stops it.
        bool scriptTimeout = false;
        int scriptTimeoutSeconds = 60;
        // How long a `python` node may run before it is stopped (0: no limit). Unlike a script, a node runs while the canvas draws, so it has a
        // limit by default. Settings > Script editor > "Stop Python nodes after" (file key scriptEditor.pythonNodeSeconds).
        int pythonNodeSeconds = 10;
        // Only the colours the user changed, by syntax style name ("keyword", "string", ...; see pythonSyntaxStyles()), as 0xRRGGBB. Every
        // other style keeps the theme's colour.
        // Besides the syntax styles' names, "text" (the colour of everything not coloured) and "background" can be overridden too.
        std::map<std::string, std::uint32_t> editorColors;

        // State
        // The script the editor had open when the app closed (a full path; empty when it was untitled). Not a preference, so a page reset keeps it.
        std::string editorFile;
        WindowPlacement window;
        DockArea docks[dockCount];
        std::vector<PanelState> panels;

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
