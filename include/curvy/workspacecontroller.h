#pragma once

#include "curvy/backgroundtask.h"
#include "curvy/canvasview.h"
#include "curvy/dockcontroller.h"
#include "curvy/docklayout.h"
#include "curvy/documentpanel.h"
#include "curvy/floatingpanels.h"
#include "curvy/paneldock.h"
#include "curvy/panelsmenu.h"
#include "curvy/graphcrumbs.h"
#include "curvy/nodepanel.h"
#include "curvy/graphview.h"
#include "curvy/newdocumentdialog.h"
#include "curvy/pointtable.h"
#include "curvy/radialmenu.h"
#include "curvy/assetevents.h"
#include "curvy/pythonnodetrust.h"
#include "curvy/assetmanagermodel.h"
#include "curvy/assetmanagerpanel.h"
#include "curvy/assetpropertiessession.h"
#include "curvy/settings.h"
#include "curvy/sketchdocument.h"
#include "curvy/swatchestab.h"
#include "curvy/swatchpalettecontroller.h"
#include "curvy/pyconsolepanel.h"
#include "curvy/scripteditorpanel.h"
#include "curvy/scriptsmenu.h"
#include "curvy/transformpanel.h"
#include "curvy/workspacemodel.h"

#include <newui/action.h>
#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/pen.h>
#include <newui/runloop.h>
#include <newui/toolframe.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace curvy {

    // The controller of the pen-and-keyboard workspace (Resources/workspace.newui: a thin top bar, the canvas, the hint bar). Tap Space
    // (or right-click / the pen's barrel button) opens the ring menu at the pointer; hold Space and release without using it also
    // opens it. A slot is picked by a tap, a flick, or its key. Ring slots only name commands; none does anything yet except moving
    // between rings and switching the theme.
    // @reflect ignore=true
    class WorkspaceController : public newui::FrameController {
    public:
        // `settings` is what the session keeps (theme, window, docks, ...); it is written to `settingsPath` after each change, and not at all
        // when the path is empty. The constructor only stores what it is given: the Frame has no native window yet, so the views are found, the
        // overlays and the tool window built and the blank state shown in frameDidLoad(), when the window is created (Frame::onCreated).
        WorkspaceController(newui::Frame& frame, Settings& settings, const std::string& settingsPath = std::string());
        ~WorkspaceController() override;
        WorkspaceController(const WorkspaceController&) = delete;
        WorkspaceController& operator=(const WorkspaceController&) = delete;

        // Chooses the colour set from the tokens in theme.h; call it before the window is built. setDarkTheme switches a window that is up.
        static void applyTheme(bool dark);
        void setDarkTheme(bool dark);

        Settings& settings() { return settings_; }
        void setSettingsPath(const std::string& path) { settingsPath_ = path; }
        // The Python console the right dock's Python panel runs scripts in; call before the window is created. Without one there is no Python panel.
        // Gives the console the run loop to reach the main thread through, so scripts run in the background.
        void setScriptConsole(ScriptConsole* console);

        // The command line's `--run`: runs a Python file once nothing else is loading, as if Run was pressed in the editor (the console panel
        // shows it, `args` is `arguments`), then prints what it printed to stdout and its error to stderr. With `exitWhenDone` the run loop
        // ends when the script does, so `curvy --run fix.py --exit-after-run -- a.curvy` is a batch command. exitCode(): 0 the script ended
        // normally, 1 it raised, 2 it was cancelled or timed out, 3 the file could not be read, 4 Python is not available.
        void runScriptFile(const std::string& path, std::vector<std::string> arguments, bool exitWhenDone);
        int exitCode() const { return exitCode_; }
        void saveSettings() const;
        // Puts the window where the last session left it (when that is still on a screen), maximized if it was; call before the window is
        // created (it sets the Frame's bounds). The window is saved when it closes.
        void restoreWindow();
        void saveWindow();

        // Starts a blank document with an empty page of this size (points) and shows it; whatever was open is replaced.
        void newDocument(const std::string& name, float width, float height);
        // The New Document dialog, then a blank document with the choices made there; false when cancelled.
        bool newDocumentDialog();
        // The Document Setup dialog on the open document (its name, page size, resolution, color mode and background); false when
        // cancelled or nothing is open.
        bool documentSetup();
        // The Settings dialog: every change shows in the workspace at once; OK keeps and saves them, Cancel puts the old ones back.
        bool settingsDialog();

        // Opens / saves a .curvy file on a background thread: the hint bar shows the progress and Esc cancels. With an empty path the file
        // dialog is asked; save with no path writes to the document's own file, asking only when it has none. Nothing else runs meanwhile.
        void openFile(const std::string& path = std::string());
        void saveFile(const std::string& path = std::string());
        // True when the open document has no unsaved edits, or the user saved or discarded them; false when they cancelled.
        bool confirmDiscard();
        void showTitle();
        newui::DocumentController& documents() { return documents_; }
        // After an open: when the file had damaged layers or groups, asks whether to repair them.
        void offerObjectRepair();
        bool busy() const { return busy_; }

        // The node graph view over the canvas (the Graph button, Tab and the View ring's Graph slot toggle it).
        void showGraph(bool show);
        void toggleGraph();
        bool graphShown() const { return graphOverlay_->isVisible(); }
        // The data popup for the node selected in the graph view (what the property panel's button does).
        // @reflect ignore=true
        PointTable& pointTable() { return *pointTable_; }
        // @reflect ignore=true
        NodePanel& nodePanel() { return *nodePanel_; }
        TransformPanel& transformPanel() { return *transformPanel_; }
        AssetManagerPanel& assetPanel() { return *assetPanel_; }
        AssetManagerModel& assetModel() { return *assetModel_; }
        AssetEvents* assetEvents() { return assetEvents_.get(); }   // null until there is a script console
        PythonNodeTrust* pythonNodeTrust() { return pythonNodeTrust_.get(); }   // likewise: decides whether a python node's code may run
        // onInstalled for the assets registered or re-registered since the last call. The library's change notice queues it on the run loop;
        // this is the part that runs there.
        void flushInstalledEvents();
        // `assets` folder next to the settings file: where a new asset is saved by default and where the editor looks for the user's own.
        std::filesystem::path userAssetsFolder() const;
        // The `+` of the Assets panel: opens the Asset Properties dialog on the selected subnet, or on a blank asset when none is selected.
        void newAsset();
        // The Edit... button: opens it on the selected asset.
        void editAsset();
        SwatchesTab& swatchesTab() { return *swatchesTab_; }
        GraphCrumbs& graphCrumbs() { return *graphCrumbs_; }
        // Runs a command of the node ring ("graph.bypass", ...) on the node selected in the graph view.
        bool runGraphCommandFor(const std::string& command) { return runGraphCommand(command); }
        void openPointTable() { showPointTable(); }
        GraphView& graph() { return *graphView_; }

        SketchDocument& document() { return document_; }
        WorkspaceModel& workspace() { return *model_; }
        CanvasView& canvas() { return *canvas_; }
        RadialMenu& menu() { return *menu_; }
        DockController& docks() { return *docks_; }
        DockLayout& panelLayout() { return panelLayout_; }
        // The lines of the Panels button's menu as they are now (the popup itself blocks, so tests read the lines instead).
        std::vector<PanelMenuEntry> panelsMenuEntries(bool dockShown);
        // The lines of the Scripts button's menu as they are now: the user's scripts folder (next to the settings file), the samples, and
        // "Open scripts folder". A picked script runs like Run in the editor, shown in the console panel.
        std::vector<PanelMenuEntry> scriptsMenuEntries();
        std::filesystem::path userScriptsFolder() const;
        // How a panel menu is shown (screen point); the native popup unless a test replaces it, because that one blocks until the user picks.
        std::function<void(const std::vector<PanelMenuEntry>&, const newui::Point&)> presentMenu;
        PanelDock& rightDock() { return *rightDock_; }
        FloatingPanels& floatingPanels() { return *floatingPanels_; }
        DocumentPanel& documentPanel() { return *documentPanel_; }

        // A press on the canvas as the pen (or mouse) made it; filterPress passes the root view's pen state. The pen's barrel button does what
        // Settings::barrelButton says (open the rings, pan while it is held, or nothing), its eraser end what Settings::eraserEnd says; a
        // mouse's right button always opens the rings. True when the press was used.
        bool pressed(const newui::PenState& pen, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        void openRing(bool byPress);
        void ensureDockShown(DockSide side);
        bool recentColorKey(std::uint32_t vk, bool shift);
        // A left press on the canvas: selects the shape under it (the node that made it) in the selection the graph view shares, or clears
        // the selection on empty canvas; `additive` (Shift or Ctrl) adds to it instead. True when the press did something.
        bool selectAt(const newui::Point& pt, bool additive);
        void runCommand(const std::string& command);

        // One step back or forward in the document's history (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z); the graph panel is shown again so it follows.
        // False when there was nothing to do.
        bool undo() { return stepHistory(false); }
        bool redo() { return stepHistory(true); }

        newui::SyncReturn modelChanged(newui::Model& model) override;

    protected:
        void frameDidLoad() override;
        void frameShouldClose(bool& canClose) override;

    private:
        // The blank state: no document, the canvas focused.
        void start();
        bool filterPress(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        bool filterKey(std::uint32_t keyMask, std::uint32_t vkCode);
        bool stepHistory(bool forward);
        void registerHistoryActions();
        newui::SyncReturn handleKeyUp(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleRingCommand(RadialMenu& sender, const std::string& command);

        // Runs `work` (a load or save of document_) on the worker, ticking the progress into the hint bar; `finish` runs on the UI thread
        // with whether it succeeded.
        void runTask(const std::string& label, std::function<bool()> work, std::function<void(bool)> finish);
        bool tickTask();
        void finishTask(bool ok);
        void showDocument();
        // The canvas and graph view as they are now, for the file's "view" section; applyView puts a saved one back (no canvas part: fitted;
        // no graph part: the canvas shows). Both need a document to be open.
        ViewState captureView() const;
        void applyView(const ViewState& view);
        static std::string fileNameOf(const std::string& path);
        // What a swatch strip's menus ask the workspace for (see SwatchPaletteController): the file dialogs, and the selection.
        void createAssetPanel();
        void openAssetProperties(AssetPropertiesSession& session, bool replaceSubnet);
        // The graph edits behind the Assets panel's buttons; each is one undo step and returns an error message or "".
        std::string addAssetInstance(const std::string& type);
        std::string updateAssetInstances(const std::string& type, std::size_t& count);
        std::string unlockSelectedInstance();
        void fireLoadedEvents();   // onCreated('loaded') for the instances of a document that was just opened
        std::string migrationNotes();     // the same as text ("" when there is nothing); asks to allow assets whose script was not trusted
        void reportMigrationProblems();   // what onVersionUpdated scripts did wrong while a document loaded or instances were updated
        void wirePythonNodeTrust();   // the gate in front of the code of `python` nodes (pythonnodetrust.h)
        void wireAssetEvents();    // the document hooks behind onDeleted, onFrameChanged, onInputChanged and onParamChanged
        void replaceSubnetWithInstance(const NodeRef& subnet, const std::string& type);
        void wireSwatchActions(SwatchPaletteController& swatches);
        void wireSwatchActions(SwatchesTab& swatches);
        // "Use as fill" in the Swatches tab: the selected shape's fill takes the swatch's colour (one undo step).
        void applySwatchToSelection(const Swatch& swatch);
        void importPalette();
        void paletteFromImage();
        void selectColorUsers(const std::vector<NodeRef>& users);

        void startDocument(const NewDocumentModel& settings);
        void bindViews();
        void restyle();
        void focusCanvas();
        void toggleDock(DockSide side);
        void showPanelsMenu(const newui::Point& screen);
        void showScriptsMenu(const newui::Point& screen);
        void runMenuScript(const ScriptFile& file);
        void showPanelMenu(Panel& panel, const newui::Point& screen);
        void buildGraphOverlay();
        void buildRightDock();
        // Settings::panels <-> the layout, the panel views and the floating windows.
        void applyPanelSettings();
        void capturePanelSettings();
        void layoutGraphOverlay();
        newui::SyncReturn handleCanvasSized(newui::View& sender, const newui::Size& size);
        void focusGraph();
        void showPointTable();
        // The ring of a node picked in the graph view, and what its slots and the keys on the graph do.
        static RadialRing nodeRing(const std::string& name);
        bool filterGraphKey(std::uint32_t keyMask, std::uint32_t vkCode);
        bool runGraphCommand(const std::string& command);
        newui::SyncReturn handleGraphMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleGraphMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleGraphMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        void applySettings(const Settings& settings);
        void showRing(const RadialRing& ring, const newui::Point& at, bool byPress);
        newui::Point ringPoint() const;
        void paintTop(BLContext& ctx) const;
        static std::string keyName(std::uint32_t vkCode);

        newui::Frame& frame_;
        Settings& settings_;
        std::string settingsPath_;
        newui::DocumentController documents_;
        SketchDocument& document_;   // owned by documents_
        CanvasView* canvas_ = nullptr;
        GraphView* graphView_ = nullptr;
        newui::SubView* graphOverlay_ = nullptr;
        std::unique_ptr<newui::ToolFrame> pointFrame_;
        std::unique_ptr<PointTable> pointTable_;
        NodePanel* nodePanel_ = nullptr;   // in the right dock, which owns it
        TransformPanel* transformPanel_ = nullptr;   // likewise
        std::shared_ptr<AssetManagerModel> assetModel_;   // shared with the panel, which can outlive this
        AssetManagerPanel* assetPanel_ = nullptr;   // likewise
        ScriptConsole* scriptConsole_ = nullptr;   // the application's, outlives the window
        std::unique_ptr<AssetEvents> assetEvents_;   // made with the console
        std::unique_ptr<PythonNodeTrust> pythonNodeTrust_;   // likewise
        std::optional<std::size_t> deleteMark_;      // the undo depth when onDeleted started, until the delete's step is recorded
        std::string deleteErrors_;
        std::map<std::string, int> installedSeen_;   // asset name -> version already announced with onInstalled
        int libraryListener_ = 0;
        bool installFlushQueued_ = false;
        PyConsolePanel* pyConsolePanel_ = nullptr;   // likewise
        ScriptEditorPanel* scriptEditorPanel_ = nullptr;   // likewise
        std::array<newui::Action, 3> historyActions_;   // Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z; the run loop matches them whichever view has focus
        std::unique_ptr<GraphCrumbs> graphCrumbs_;
        RadialMenu* menu_ = nullptr;
        std::unique_ptr<DockController> docks_;
        float dockWidthBeforeCode_ = 0.0f;   // the right dock's width before the node panel's code section widened it
        DockLayout panelLayout_;   // where every panel is; the dock views read it
        PanelDock* rightDock_ = nullptr;   // owned by the right area's view tree
        DocumentPanel* documentPanel_ = nullptr;   // owned by rightDock_ (or by its floating frame)
        std::unique_ptr<FloatingPanels> floatingPanels_;   // declared before swatchesTab_: that is gone first, then the frames and their panels
        std::unique_ptr<SwatchesTab> swatchesTab_;   // declared after the document: gone before it (its views belong to the Document panel)
        newui::Label* hintTool_ = nullptr;
        newui::Label* hintKeys_ = nullptr;
        newui::Label* hintInfo_ = nullptr;
        newui::Label* documentName_ = nullptr;
        WorkspaceModel* model_ = nullptr;   // owned by the Controller base (setModel)
        newui::Connections connections_;

        BackgroundTask task_;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);   // posts from the worker that arrive after this is gone find false
        newui::RunLoop* loop_ = nullptr;
        newui::RunLoop::TimerHandle timer_ = newui::RunLoop::kInvalidTimerHandle;
        bool busy_ = false;
        // The status bar line of a running script: how long it has run (and its progress), refreshed every few seconds.
        void startScriptStatus();
        void stopScriptStatus();
        std::string scriptStatusText(bool finished = false) const;
        newui::RunLoop::TimerHandle scriptTimer_ = newui::RunLoop::kInvalidTimerHandle;
        bool scriptStatusShown_ = false;
        bool commandLineRun_ = false;
        bool exitWhenScriptDone_ = false;
        bool forcedClose_ = false;
        int exitCode_ = 0;
        void finishCommandLineRun(int code);
        void flushCommandLineOutput(const std::string& text);
        newui::RunLoop::TimerHandle commandLineTimer_ = newui::RunLoop::kInvalidTimerHandle;
        std::size_t commandLinePrinted_ = 0;
        std::chrono::steady_clock::time_point scriptStarted_;
        std::string taskLabel_;
        int taskPercent_ = -1;
        std::function<void(bool)> taskFinish_;

        newui::Point pointer_;
        bool pointerKnown_ = false;
        bool panning_ = false;
        bool zooming_ = false;   // Ctrl+Space and a drag: zoom around the press point
        newui::Point zoomFrom_;
        float zoomStart_ = 1.0f;
        bool spaceDown_ = false, spaceUsed_ = false;
        std::chrono::steady_clock::time_point spaceStart_;
    };

}
