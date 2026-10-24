#pragma once

#include "curvy/canvasview.h"
#include "curvy/dockcontroller.h"
#include "curvy/graphdock.h"
#include "curvy/holdscrub.h"
#include "curvy/radialmenu.h"
#include "curvy/tooloptionscard.h"
#include "curvy/settings.h"
#include "curvy/sketchcommands.h"
#include "curvy/sketchcontroller.h"
#include "curvy/sketchdocument.h"
#include "curvy/workspacemodel.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/frame.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace curvy {

    // The controller of the pen-and-keyboard workspace (Resources/workspace.newui: a thin top bar, the canvas, the hint bar - no menus,
    // toolbars or panels). It turns the pen, the barrel button and a few held keys into the interaction docs/ui-design.md describes:
    //
    //   - tap Space (or the barrel button / right click) opens the ring for what is under the pointer: the New ring with nothing open,
    //     else the object, create or document ring. A slot is picked by a tap, a flick, or its key.
    //   - hold R E P S L and drag to draw a rectangle, ellipse, polygon, star or line; let go and the Select tool is back.
    //   - hold Space and drag to pan.
    //   - over an outline or a fill, hold W (width), O (opacity), H (hue) or B (brightness) and move the pointer to scrub the number;
    //     letting go of the key is one undo step, Esc puts it back.
    //
    // MVC: the artwork is the SketchDocument; WorkspaceModel (the controller's own Model) holds the shell state and the hint bar text; the
    // views are the canvas, the ring and the labels, which follow the model. The canvas is asked first about presses and keys through its
    // filter hooks, and focus stays with the canvas (UIInputManager decides what a click focuses). Menu items, rings and keys only name
    // commands; SketchCommandController does the work.
    // @reflect ignore=true
    class WorkspaceController : public newui::Controller {
    public:
        WorkspaceController(newui::Frame& frame, SketchDocument& document);
        ~WorkspaceController() override;
        // Checks the document every 30 s and writes a recovery copy when the Autosave interval has passed; call on the loop thread.
        void startAutosave();
        // Autosaved copies left by a session that did not end cleanly: asks whether to recover each, newest first, and
        // stops after the first one recovered (returns true then, false when nothing was recovered). Call on the loop thread, not in the headless checks.
        bool offerRecovery();
        // The first-run overlay: a keyboard with the Space bar lit and how it opens the ring menu. Shown once (Settings::welcomeShown).
        void showWelcomeOnce();
        WorkspaceController(const WorkspaceController&) = delete;
        WorkspaceController& operator=(const WorkspaceController&) = delete;

        // Chooses the colour set: newui's mode and the roles its .theme files leave out (call it before the window is built, as main does),
        // from the tokens in theme.h. setDarkTheme switches a window that is already up.
        static void applyTheme(bool dark);
        void setDarkTheme(bool dark);

        // Opens the file at `path`; with none the workspace starts empty ("Nothing open").
        void start(const std::string& path);

        newui::Frame& frame() { return frame_; }
        WorkspaceModel& workspace() { return *model_; }
        CanvasView& canvas() { return *canvas_; }
        RadialMenu& menu() { return *menu_; }
        DockController& dock() { return *dock_; }
        GraphDock& graph() { return *graph_; }
        ToolOptionsCard& toolOptions() { return *options_; }
        SketchCommandController& commands() { return *commands_; }
        SketchDocumentController& files() { return *files_; }
        const HoldScrub& scrub() const { return scrub_; }

        const Settings& settings() const { return settings_; }
        // Runs the settings dialog; its changes apply live, OK saves them for the next run, Cancel puts them back.
        void openSettings();
        void applySettings(const Settings& settings);
        void applyToolDefaults();
        void toolDefaultsChanged();

        // Opens the ring for what is at the pointer (the canvas centre when the pointer has not been seen).
        void openRing(bool byPress);
        // What a ring slot, a ring hub or a key asks for.
        void runCommand(const std::string& command);

        newui::SyncReturn modelChanged(newui::Model& model) override;

    private:
        bool filterPress(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        bool filterKey(std::uint32_t keyMask, std::uint32_t vkCode);
        newui::SyncReturn handleKeyUp(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleRingCommand(RadialMenu& sender, const std::string& command);
        newui::SyncReturn handleToolChanged(CanvasView& sender);
        newui::SyncReturn handleSelectionChanged(SketchDocument& sender);
        newui::SyncReturn handleDocumentReplaced(SketchDocumentController& sender);

        void bindViews();
        void restyle();
        void focusCanvas();
        void updateHover(const newui::Point& pt);
        double scrubTravel(const newui::Point& pt) const;
        bool startScrub(char key);
        void endScrub(bool keep);
        void armTool(SketchTool tool, bool sticky, const std::string& heldKey);
        void paintTop(BLContext& ctx) const;
        void paintScrubCard(BLContext& ctx) const;
        void paintHoverChip(BLContext& ctx) const;
        static std::string keyName(std::uint32_t vkCode);

        newui::Frame& frame_;
        SketchDocument& document_;
        CanvasView* canvas_ = nullptr;
        RadialMenu* menu_ = nullptr;
        newui::Label* hintTool_ = nullptr;
        newui::Label* hintKeys_ = nullptr;
        newui::Label* hintInfo_ = nullptr;
        newui::Label* documentName_ = nullptr;
        WorkspaceModel* model_ = nullptr;   // owned by the Controller base (setModel)
        std::unique_ptr<SketchDocumentController> files_;
        std::unique_ptr<SketchCommandController> commands_;
        std::unique_ptr<DockController> dock_;
        std::unique_ptr<GraphDock> graph_;
        std::unique_ptr<ToolOptionsCard> options_;
        HoldScrub scrub_;
        void autosaveTick();
        void showToolOptions(SketchTool tool, const newui::Point& at);
        void clearRecovery();
        void dismissWelcome();
        std::string recoveryFile_;
        std::uintptr_t autosaveTimer_ = 0;
        std::chrono::steady_clock::time_point lastAutosave_ = std::chrono::steady_clock::now();
        Settings settings_;
        std::string scrubKey_;
        bool cursorHidden_ = false;
        bool applyingToolDefaults_ = false;
        long cursorBeforeX_ = 0, cursorBeforeY_ = 0;

        newui::Point pointer_;
        bool pointerKnown_ = false;
        bool leftDown_ = false;
        bool shiftDown_ = false;
        bool spaceDown_ = false, spaceUsed_ = false;
        std::chrono::steady_clock::time_point spaceStart_;
        bool panning_ = false;
        newui::Point panFrom_;
        float panStartX_ = 0.0f, panStartY_ = 0.0f;
        bool toolSticky_ = false;
        std::string heldToolKey_;
    };

}
