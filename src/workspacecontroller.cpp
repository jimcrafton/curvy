#include "curvy/workspacecontroller.h"
#include <pyPythonNode.h>
#include "curvy/assetpropertiesdialog.h"
#include "curvy/assetpropertiesdialogsession.h"
#include "curvy/selectionmodel.h"
#include "curvy/settingsdialog.h"
#include "curvy/paletteimagedialog.h"
#include "curvy/paletteimagemodel.h"
#include "curvy/shapepaths.h"
#include "curvy/swatchimport.h"
#include "curvy/theme.h"

#include <jitnodes/graph/assetLibrary.h>
#include <newui/automation.h>
#include <newui/bundle.h>
#include <newui/dialogs.h>
#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>
#include <newui/themedata.h>
#include <newui/uicolormanager.h>
#include <newui/uiinputmanager.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace curvy {

    using namespace newui;

    namespace {

        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(Theme::argb(value)); }

        struct MonitorSearch {
            const std::string* name;
            MONITORINFOEXW* found;
            bool hit;
        };

        BOOL CALLBACK checkMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
            auto* search = reinterpret_cast<MonitorSearch*>(data);
            MONITORINFOEXW info = {};
            info.cbSize = sizeof(info);
            if (!::GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }
            std::string device;
            for (const wchar_t* c = info.szDevice; *c != 0; ++c) {
                device.push_back(char(*c));
            }
            if (device == *search->name) {
                *search->found = info;
                search->hit = true;
                return FALSE;
            }
            return TRUE;
        }

        // The screen with this device name, when it is still attached.
        bool monitorNamed(const std::string& name, MONITORINFOEXW& out) {
            MonitorSearch search{&name, &out, false};
            ::EnumDisplayMonitors(nullptr, nullptr, checkMonitor, reinterpret_cast<LPARAM>(&search));
            return search.hit;
        }

        template <typename T>
        T* find(Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }

        RadialRing newRing() {
            return {"New", "Create...", "dialog.new", {
                {"Letter", "1", "new.letter"}, {"A4", "2", "new.a4"}, {"Full HD", "3", "new.fullhd"}, {"Square", "4", "new.square"},
                {"Open", "O", "file.open"}, {"Import", "I", "file.importPdf"}, {"Settings", "S", "dialog.settings"}}, 3};
        }
        RadialRing createRing() {
            return {"Create", "Shape options...", "dialog.shape", {
                {"Rectangle", "R", "tool.rect"}, {"Ellipse", "E", "tool.ellipse"}, {"Polygon", "P", "tool.polygon", "options.polygon"}, {"Star", "S", "tool.star", "options.star"},
                {"Line", "L", "tool.line"}, {"Freehand", "B", "tool.pencil", "options.pencil"}, {"Text", "T", "tool.text"}, {"Select", "V", "tool.select", "options.select"}}};
        }
        RadialRing documentRing() {
            return {"Document", "Setup...", "dialog.setup", {
                {"New artboard", "A", "doc.newArtboard"}, {"New document", "N", "ring.new"}, {"Open", "O", "file.open"}, {"Save", "S", "file.save"},
                {"Export", "E", "ring.export"}, {"Import", "I", "ring.import"}, {"View", "V", "ring.view"}, {"Select", "L", "ring.select"}}};
        }
        RadialRing selectRing() {
            return {"Select", "Back", "ring.document", {{"By...", "B", "select.by"}, {"Similar", "S", "select.similar"}}};
        }
        RadialRing exportRing() {
            return {"Export", "Back", "ring.document", {
                {"Image", "I", "file.exportImage"}, {"PDF", "P", "file.exportPdf"}, {"EPS", "E", "file.exportEps"}, {"Artboards", "A", "file.exportArtboards"}}};
        }
        RadialRing importRing() {
            return {"Import", "Back", "ring.document", {
                {"Picture", "P", "file.placeImage"}, {"Trace", "T", "file.traceImage"}, {"PDF / AI", "F", "file.importPdf"}}};
        }
        RadialRing viewRing() {
            return {"View", "Back", "ring.document", {
                {"Toolbar", "T", "view.toolbar"}, {"Properties", "P", "view.properties"}, {"Rulers", "G", "view.toggleRulers"}, {"Fit page", "0", "view.fitPage"},
                {"Theme", "L", "view.theme"}, {"Graph", "N", "view.graph"}, {"Timeline", "M", "view.timeline"}, {"Settings", "S", "dialog.settings"}}};
        }
        RadialRing objectRing() {
            return {"Object", "Properties...", "dialog.object", {
                {"Arrange", "1", "ring.arrange"}, {"Group", "2", "object.group"}, {"Color", "3", "card.color"}, {"Stroke", "4", "card.stroke"},
                {"Duplicate", "D", "edit.duplicate"}, {"Delete", "X", "edit.delete"}, {"Flip", "F", "object.flipHorizontal"}, {"Lock", "K", "object.lock"}}};
        }
        RadialRing arrangeRing() {
            return {"Arrange", "Back", "ring.object", {
                {"To front", "F", "arrange.bringToFront"}, {"Forward", "W", "arrange.bringForward"}, {"Backward", "B", "arrange.sendBackward"}, {"To back", "K", "arrange.sendToBack"}}};
        }

        struct PageSize {
            const char* command;
            const char* name;
            float width, height;
        };
        const PageSize kPages[] = {
            {"new.letter", "Letter", 612, 792}, {"new.a4", "A4", 595, 842}, {"new.fullhd", "Full HD", 1920, 1080}, {"new.square", "Square 1080", 1080, 1080}};

        struct RingEntry {
            const char* command;
            RadialRing (*make)();
        };
        const RingEntry kRings[] = {
            {"ring.new", newRing}, {"ring.create", createRing}, {"ring.document", documentRing}, {"ring.export", exportRing},
            {"ring.import", importRing}, {"ring.view", viewRing}, {"ring.object", objectRing}, {"ring.arrange", arrangeRing},
            {"ring.select", selectRing}};
    }

    void WorkspaceController::applyTheme(bool dark) {
        // The mode first, then the roles the .theme file does not carry; the rest comes from Resources/Themes/{dark,light}.theme.
        const Theme& theme = dark ? Theme::dark() : Theme::light();
        Theme::setCurrent(theme);
        UIColorManager::setDarkModeOverride(dark);
        UIColorManager::setColorOverride(UIColorRole::HighlightBackground, Color(theme.accent, false));
        UIColorManager::setColorOverride(UIColorRole::HighlightText, Color(theme.onAccent, false));
        UIColorManager::setColorOverride(UIColorRole::LinkHoverText, Color(theme.accentHover, false));
        ThemeData::instance().reloadForCurrentMode();
    }

    void WorkspaceController::setDarkTheme(bool dark) {
        applyTheme(dark);
        settings_.theme = dark ? Settings::ThemeChoice::Dark : Settings::ThemeChoice::Light;
        saveSettings();
        restyle();
    }

    void WorkspaceController::applySettings(const Settings& settings) {
        const bool dark = settings.darkTheme();
        const bool themeChanged = dark != Theme::current().isDark;
        settings_ = settings;
        if (scriptConsole_ != nullptr) {
            scriptConsole_->setTimeout(std::chrono::seconds(settings_.scriptTimeout ? settings_.scriptTimeoutSeconds : 0));
            jitnodes::python::setPythonNodeTimeLimit(std::chrono::seconds(settings_.pythonNodeSeconds));
        }
        if (themeChanged) {
            applyTheme(dark);
        }
        restyle();
        docks_->refresh();
    }

    bool WorkspaceController::settingsDialog() {
        if (!SettingsDialog::run(frame_, settings_, [this](const Settings& settings) { applySettings(settings); })) {
            return false;
        }
        applySettings(settings_);
        saveSettings();
        model_->say("Settings saved");
        return true;
    }

    void WorkspaceController::saveSettings() const {
        if (!settingsPath_.empty()) {
            settings_.save(settingsPath_);
        }
    }

    void WorkspaceController::restoreWindow() {
        const Settings::WindowPlacement& placed = settings_.window;
        if (!placed.known()) {
            return;
        }
        int x = placed.x, y = placed.y, width = placed.width, height = placed.height;
        const RECT where = {x, y, x + width, y + height};
        MONITORINFOEXW screen = {};
        if (!placed.monitor.empty() && monitorNamed(placed.monitor, screen)) {
            // The screen it was on is still there: the window stays on it, moved in (and shrunk to fit) when that screen has changed since.
            RECT overlap = {};
            if (!::IntersectRect(&overlap, &where, &screen.rcWork)) {
                const int workWidth = screen.rcWork.right - screen.rcWork.left, workHeight = screen.rcWork.bottom - screen.rcWork.top;
                width = width < workWidth ? width : workWidth;
                height = height < workHeight ? height : workHeight;
                x = screen.rcWork.left + (workWidth - width) / 2;
                y = screen.rcWork.top + (workHeight - height) / 2;
            }
        } else if (MonitorFromRect(&where, MONITOR_DEFAULTTONULL) == nullptr) {
            return;   // that screen is gone and nothing else holds the window
        }
        frame_.setBounds(Rect(float(x), float(y), float(width), float(height)));
        if (placed.maximized) {
            loop_->post([this]() {
                if (HWND handle = frame_.frameHandle()) {
                    ::ShowWindow(handle, SW_MAXIMIZE);   // the window exists once the loop runs
                }
            });
        }
    }

    void WorkspaceController::saveWindow() {
        HWND handle = frame_.frameHandle();
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (handle == nullptr || !::GetWindowPlacement(handle, &placement)) {
            return;
        }
        Settings::WindowPlacement placed;
        RECT rect = {};
        if (placement.showCmd == SW_SHOWNORMAL && ::GetWindowRect(handle, &rect)) {
            placed.x = rect.left;
            placed.y = rect.top;
            placed.width = rect.right - rect.left;
            placed.height = rect.bottom - rect.top;
        } else {
            // Maximized or minimized: the normal rectangle is in workspace coordinates, relative to the primary work area.
            RECT work = {};
            ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
            rect = placement.rcNormalPosition;
            placed.x = rect.left + work.left;
            placed.y = rect.top + work.top;
            placed.width = rect.right - rect.left;
            placed.height = rect.bottom - rect.top;
        }
        placed.maximized = placement.showCmd == SW_SHOWMAXIMIZED || (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
        MONITORINFOEXW screen = {};
        screen.cbSize = sizeof(screen);
        if (::GetMonitorInfoW(::MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &screen)) {
            for (const wchar_t* c = screen.szDevice; *c != 0; ++c) {
                placed.monitor.push_back(char(*c));   // device names are plain ASCII ("\\.\DISPLAY1")
            }
        }
        capturePanelSettings();   // the floating windows move without telling anyone
        if (placed.known()) {
            settings_.window = placed;
            saveSettings();
        }
    }

    void WorkspaceController::restyle() {
        if (scriptEditorPanel_ != nullptr) {
            scriptEditorPanel_->applySettings(settings_);   // font, tab width and colours (the dark or light set follows the theme)
        }
        const Theme& theme = Theme::current();
        auto fill = [](View* view, std::uint32_t rgbValue) {
            if (view != nullptr) {
                view->style().setBackgroundColor(Color(Theme::argb(rgbValue), true));
            }
        };
        auto text = [](Label* label, std::uint32_t rgbValue) {
            if (label != nullptr) {
                label->setTextColor(BLRgba32(Theme::argb(rgbValue)));
            }
        };
        fill(&frame_.rootView(), theme.bgApp);
        fill(frame_.rootView().findView("topBar"), theme.bgSurface);
        fill(frame_.rootView().findView("hintBar"), theme.bgSurface);
        for (const char* side : {"top", "left", "right", "bottom"}) {
            const std::string area = std::string(side) + "Area";
            fill(frame_.rootView().findView(area), theme.bgSurface);
            fill(frame_.rootView().findView(area + "HeaderBar"), theme.slot);
            text(find<Label>(frame_, (area + "Header").c_str()), theme.textPrimary);
        }
        text(find<Label>(frame_, "appLogo"), theme.accent);
        text(find<Label>(frame_, "appName"), theme.textPrimary);
        text(documentName_, theme.textMuted);
        text(hintTool_, theme.highlight);
        text(hintKeys_, theme.textMuted);
        text(hintInfo_, theme.textMuted);
        canvas_->setPasteboardColor(Theme::argb(settings_.canvasFollowsTheme ? theme.bgCanvas : settings_.canvasBackground));
        frame_.rootView().refreshThemes();
        frame_.rootView().style().markDirty();
        canvas_->style().markDirty();
    }

    WorkspaceController::WorkspaceController(Frame& frame, Settings& settings, const std::string& settingsPath) : FrameController(frame), frame_(frame), settings_(settings), settingsPath_(settingsPath), document_(*new SketchDocument()) {
        documents_.addDocument(&document_);
        loop_ = &RunLoop::current();
    }

    std::string WorkspaceController::scriptStatusText(bool finished) const {
        const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - scriptStarted_).count();
        const std::string elapsed = seconds >= 60 ? std::to_string(seconds / 60) + " min " + std::to_string(seconds % 60) + " s" : std::to_string(seconds) + " s";
        if (finished) {
            return "Script ended after " + elapsed;
        }
        std::string text = "A script is running";
        if (seconds >= 1) {
            text += " for " + elapsed;
        }
        if (scriptConsole_ != nullptr) {
            const ScriptProgress progress = scriptConsole_->progress();
            if (progress.fraction >= 0.0f) {
                text += " (" + std::to_string(int(progress.fraction * 100.0f + 0.5f)) + "%)";
            }
            if (!progress.message.empty()) {
                text += " - " + progress.message;
            }
        }
        return text + " - Esc cancels";
    }

    void WorkspaceController::startScriptStatus() {
        scriptStarted_ = std::chrono::steady_clock::now();
        scriptStatusShown_ = false;
        stopScriptStatus();
        // A short script leaves the status bar alone (it would replace the hint a script's own selection change just set); only a
        // script that is still going after a moment gets the line, then every few seconds.
        scriptTimer_ = loop_->postDelayed(std::chrono::seconds(2), [this]() {
            scriptStatusShown_ = true;
            model_->say(scriptStatusText());
            scriptTimer_ = loop_->postDelayed(std::chrono::seconds(5), [this]() {
                model_->say(scriptStatusText());
                return false;
            });
            return true;
        });
    }

    void WorkspaceController::stopScriptStatus() {
        if (scriptTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(scriptTimer_);
            scriptTimer_ = RunLoop::kInvalidTimerHandle;
        }
    }

    void WorkspaceController::flushCommandLineOutput(const std::string& text) {
        if (text.size() > commandLinePrinted_) {
            std::fwrite(text.data() + commandLinePrinted_, 1, text.size() - commandLinePrinted_, stdout);
            std::fflush(stdout);
            commandLinePrinted_ = text.size();
        }
    }

    void WorkspaceController::finishCommandLineRun(int code) {
        if (commandLineTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(commandLineTimer_);
            commandLineTimer_ = RunLoop::kInvalidTimerHandle;
        }
        commandLineRun_ = false;
        exitCode_ = code;
        if (scriptConsole_ != nullptr) {
            scriptConsole_->setArguments({});   // the editor's runs later see no arguments
        }
        if (exitWhenScriptDone_) {
            // The window closes the way the X button closes it, so everything is torn down in the usual order, but without the prompts a
            // batch run could never answer (and without saving the window placement: a batch run is not the user rearranging the window).
            forcedClose_ = true;
            ::PostMessageW(frame_.frameHandle(), WM_CLOSE, 0, 0);
        }
    }

    void WorkspaceController::runScriptFile(const std::string& path, std::vector<std::string> arguments, bool exitWhenDone) {
        exitWhenScriptDone_ = exitWhenDone;
        if (scriptConsole_ == nullptr || pyConsolePanel_ == nullptr) {
            std::fprintf(stderr, "curvy: Python is not available, so %s was not run\n", path.c_str());
            finishCommandLineRun(4);
            return;
        }
        if (busy_) {
            // A file given with --open is still loading.
            loop_->postDelayed(std::chrono::milliseconds(50), [this, path, arguments, exitWhenDone]() {
                runScriptFile(path, arguments, exitWhenDone);
                return true;
            });
            return;
        }
        std::string code;
        if (!ScriptsMenu::readScript(std::filesystem::u8path(path), code)) {
            std::fprintf(stderr, "curvy: cannot read the script %s\n", path.c_str());
            finishCommandLineRun(3);
            return;
        }
        if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
            finishCommandLineRun(0);   // nothing to run
            return;
        }
        commandLineRun_ = true;
        commandLinePrinted_ = 0;
        scriptConsole_->setArguments(std::move(arguments));
        pyConsolePanel_->runScript(code, fileNameOf(path));
        // What the script prints reaches stdout as it goes, not only when it ends: a long batch job can be watched or piped.
        if (commandLineRun_ && scriptConsole_->running()) {
            commandLineTimer_ = loop_->postDelayed(std::chrono::milliseconds(100), [this]() {
                flushCommandLineOutput(scriptConsole_->outputSoFar());
                return false;
            });
        }
    }

    void WorkspaceController::setScriptConsole(ScriptConsole* console) {
        if (scriptConsole_ != nullptr && scriptConsole_ != console) {
            scriptConsole_->setDocumentFileHook(nullptr);   // the old console must not call into this window any more
        }
        scriptConsole_ = console;
        assetEvents_.reset();
        jitnodes::python::setPythonNodePolicy(nullptr);
        jitnodes::python::setPythonNodeInterruptCheck(nullptr);
        pythonNodeTrust_.reset();
        if (libraryListener_ != 0) {
            jitnodes::AssetLibrary::instance().removeListener(libraryListener_);
            libraryListener_ = 0;
        }
        if (console != nullptr) {
            assetEvents_ = std::make_unique<AssetEvents>(console);
            assetEvents_->installMigrationHook();
            assetEvents_->remember(document_);
            installedSeen_.clear();
            for (const jitnodes::AssetEntry& entry : jitnodes::AssetLibrary::instance().list()) {
                installedSeen_[entry.name] = entry.version;   // what is registered now was installed before anything could listen
            }
            // The library may change on another thread (a document load): only a task on the main thread looks at what changed.
            libraryListener_ = jitnodes::AssetLibrary::instance().addListener([this, alive = alive_, loop = loop_]() {
                if (installFlushQueued_) {
                    return;
                }
                installFlushQueued_ = true;
                loop->post([this, alive]() {
                    if (*alive) {
                        flushInstalledEvents();
                    }
                });
            });
            wirePythonNodeTrust();
            assetEvents_->addTrustedFolder(executableDirectory() / "Resources" / "assets");
            assetEvents_->addTrustedFolder(userAssetsFolder());
            assetEvents_->setTrustFile(userAssetsFolder().parent_path() / "trustedAssets.txt");
            assetEvents_->setTrustPrompt([this](const std::string& asset, const std::vector<std::string>& scripts) {
                std::string list;
                for (const std::string& name : scripts) {
                    list += (list.empty() ? "" : ", ") + name;
                }
                const DialogResult answer = Dialog::showMessageBox(frame_.frameHandle(),
                    "The asset \"" + asset + "\" comes with Python code (" + list + ") that would run on your document. Only allow it when you trust where it came from.\n\nAllow it to run?",
                    "Run asset scripts?", MessageBoxButtons::YesNo, MessageBoxIcon::Warning);
                return answer == DialogResult::Yes;
            });
            RunLoop* loop = loop_;
            console->setMainThreadPoster([loop](std::function<void()> task) { loop->post(std::move(task)); });
            console->setTimeout(std::chrono::seconds(settings_.scriptTimeout ? settings_.scriptTimeoutSeconds : 0));
            console->setDocumentFileHook([this](bool contentReplaced) {
                if (contentReplaced) {
                    showDocument();   // the canvas, graph view and panels show the new file; its saved view is applied
                    const std::size_t damaged = document_.objectProblems().size();
                    model_->say("A script opened " + fileNameOf(document_.filePath()) +
                                (damaged > 0 ? " (" + std::to_string(damaged) + " damaged object(s), not repaired)" : std::string()));
                    fireLoadedEvents();
                } else {
                    showTitle();
                }
            });
        }
    }

    void WorkspaceController::frameShouldClose(bool& canClose) {
        if (forcedClose_) {
            return;
        }
        if (busy_) {
            model_->say(taskLabel_ + " - wait for it to finish, or press Esc to cancel");
            canClose = false;
            return;
        }
        if (!confirmDiscard()) {
            canClose = false;
            return;
        }
        saveWindow();
    }

    void WorkspaceController::showTitle() {
        const bool open = model_ != nullptr && model_->documentOpen();
        frame_.setTitle(open ? "curvy - " + document_.name() + (document_.isModified() ? " *" : "") : "curvy");
    }

    bool WorkspaceController::confirmDiscard() {
        if (model_ == nullptr || !model_->documentOpen() || !document_.isModified()) {
            return true;
        }
        return documents_.confirmDiscardChanges(document_);
    }

    // The window exists now, so a window that must be owned by it (the data window) can be made.
    void WorkspaceController::frameDidLoad() {
        canvas_ = find<CanvasView>(frame_, "canvas");
        if (canvas_ == nullptr) {
            throw std::runtime_error("WorkspaceController: workspace.newui has no view named \"canvas\"");
        }
        hintTool_ = find<Label>(frame_, "hintTool");
        hintKeys_ = find<Label>(frame_, "hintKeys");
        hintInfo_ = find<Label>(frame_, "hintInfo");
        documentName_ = find<Label>(frame_, "documentName");

        setModel(std::make_unique<WorkspaceModel>());
        model_ = static_cast<WorkspaceModel*>(model());
        model_->addView(canvas_);
        document_.addView(canvas_);   // the canvas shows no page until a document is started (newDocument)
        restyle();

        document_.onModifiedChanged += [this](newui::Document&) {
            if (!busy_) {   // a save or load flips the flag on its worker thread; its completion callback sets the title
                showTitle();
            }
            return SyncReturn::Handled;
        };
        documents_.setUnsavedChangesHandler([this](newui::Document& document) {
            const DialogResult result = Dialog::showMessageBox(frame_.frameHandle(), "Save changes to " + document_.name() + "?", "Unsaved changes", MessageBoxButtons::YesNoCancel, MessageBoxIcon::Warning);
            if (result == DialogResult::Yes) {
                document_.setViewState(captureView());
                return newui::UnsavedChangesChoice::Save;
            }
            return result == DialogResult::No ? newui::UnsavedChangesChoice::Discard : newui::UnsavedChangesChoice::Cancel;
        });
        documents_.setSavePathProvider([this](newui::Document&) {
            FileDialogOptions options;
            options.title = "Save As";
            options.filters.push_back({"curvy documents", "*.curvy"});
            options.defaultExtension = "curvy";
            options.defaultFileName = document_.name() + ".curvy";
            std::string file;
            return Dialog::showSaveFile(frame_.frameHandle(), options, file) ? file : std::string();
        });

        buildGraphOverlay();
        menu_ = new RadialMenu();
        menu_->isEnabled = [](const std::string& command) { return commandWired(command); };
        canvas_->addChild(menu_);
        docks_ = std::make_unique<DockController>(frame_, settings_, [this]() { saveSettings(); });
        buildRightDock();
        bindViews();
        makeTools();
        start();
    }

    void WorkspaceController::buildRightDock() {
        auto* body = dynamic_cast<SubView*>(frame_.rootView().findView("rightAreaBody"));
        if (body == nullptr) {
            return;
        }
        presentMenu = [this](const std::vector<PanelMenuEntry>& entries, const Point& screen) {
            PanelsMenu::popup(frame_.frameHandle(), entries, screen);
        };
        PanelInfo info;
        info.id = "document";
        info.title = "Document";
        info.side = DockSide::Right;
        panelLayout_.add(info);
        PanelInfo nodeInfo;
        nodeInfo.id = "node";
        nodeInfo.title = "Node";
        nodeInfo.side = DockSide::Right;
        panelLayout_.add(nodeInfo);
        PanelInfo transformInfo;
        transformInfo.id = "transform";
        transformInfo.title = "Transform";
        transformInfo.side = DockSide::Right;
        panelLayout_.add(transformInfo);
        PanelInfo selectInfo;
        selectInfo.id = "select";
        selectInfo.title = "Select by";
        selectInfo.side = DockSide::Right;
        selectInfo.collapsed = true;   // used now and then: the title shows until it is opened (the ring's "By..." opens it)
        panelLayout_.add(selectInfo);
        PanelInfo assetsInfo;
        assetsInfo.id = "assets";
        assetsInfo.title = "Assets";
        assetsInfo.side = DockSide::Right;
        assetsInfo.collapsed = true;   // a long panel: the title shows until it is opened
        panelLayout_.add(assetsInfo);
        if (scriptConsole_ != nullptr) {
            PanelInfo pythonInfo;
            pythonInfo.id = "pyconsole";
            pythonInfo.title = "Python Console";
            pythonInfo.side = DockSide::Right;
            panelLayout_.add(pythonInfo);
            PanelInfo editorInfo;
            editorInfo.id = "pyeditor";
            editorInfo.title = "Python Script Editor";
            editorInfo.side = DockSide::Right;
            panelLayout_.add(editorInfo);
        }

        rightDock_ = new PanelDock(panelLayout_, DockSide::Right);
        rightDock_->setLayoutParams(std::make_unique<FlexLayoutParams>(1.0f));
        body->addChild(rightDock_);
        floatingPanels_ = std::make_unique<FloatingPanels>(frame_.frameHandle(), panelLayout_, [this](DockSide side) {
            return side == DockSide::Right ? rightDock_ : nullptr;
        });
        panelLayout_.onChanged = [this]() {
            floatingPanels_->sync();   // panels leave and enter their frames first, then the dock stacks what it has
            rightDock_->sync();
        };

        documentPanel_ = new DocumentPanel(document_);
        rightDock_->addPanel(documentPanel_);
        nodePanel_ = new NodePanel(document_, *canvas_);
        rightDock_->addPanel(nodePanel_);
        nodePanel_->onCommand = [this](const std::string& command) {
            runGraphCommand(command);
            if (graphOverlay_->isVisible()) {
                focusGraph();
            }
        };
        nodePanel_->onRefused = [this](const std::string& message) { model_->say(message); };
        nodePanel_->onDestroyed = [this]() { nodePanel_ = nullptr; };
        nodePanel_->onCodeToggled = [this](bool open) {
            if (open) {
                dockWidthBeforeCode_ = docks_->size(DockSide::Right);
                if (dockWidthBeforeCode_ < NodePanel::kCodeWidth) {
                    docks_->setSize(DockSide::Right, NodePanel::kCodeWidth);
                }
            } else if (dockWidthBeforeCode_ > 0.0f) {
                docks_->setSize(DockSide::Right, dockWidthBeforeCode_);
                dockWidthBeforeCode_ = 0.0f;
            }
        };
        nodePanel_->onEdited = [this]() {
            graphView_->style().markDirty();
            canvas_->style().markDirty();
        };
        nodePanel_->onCheckPython = [this](const std::string& code) -> std::optional<SyntaxProblem> {
            return scriptConsole_ != nullptr ? scriptConsole_->checkSyntax(code) : std::nullopt;
        };
        nodePanel_->onPythonCodeCommitted = [this](const std::string& code) {
            if (pythonNodeTrust_) {
                pythonNodeTrust_->trust(code);
            }
        };
        nodePanel_->onPythonCodeAllowed = nodePanel_->onPythonCodeCommitted;
        if (auto* data = dynamic_cast<Button*>(nodePanel_->findView("graphDataButton"))) {
            data->onClick += [this](Control&) {
                showPointTable();
                return SyncReturn::Handled;
            };
        }
        transformPanel_ = new TransformPanel(document_, graphView_->selectionModel());
        rightDock_->addPanel(transformPanel_);
        transformPanel_->onRefused = [this](const std::string& message) { model_->say(message); };
        transformPanel_->onDestroyed = [this]() { transformPanel_ = nullptr; };
        transformPanel_->onEdited = [this]() {
            graphView_->style().markDirty();
            canvas_->style().markDirty();
        };
        selectPanel_ = new SelectPanel(document_, graphView_->selectionModel());
        rightDock_->addPanel(selectPanel_);
        selectPanel_->onMessage = [this](const std::string& message) { model_->say(message); };
        selectPanel_->onDestroyed = [this]() { selectPanel_ = nullptr; };
        createAssetPanel();
        if (scriptConsole_ != nullptr) {
            pyConsolePanel_ = new PyConsolePanel(*scriptConsole_, &document_);
            pyConsolePanel_->setSelection(&graphView_->selectionModel());
            rightDock_->addPanel(pyConsolePanel_);
            pyConsolePanel_->onDestroyed = [this]() { pyConsolePanel_ = nullptr; };
            pyConsolePanel_->onBeforeRun = [this]() {
                frame_.rootView().repaintNow();
                ::UpdateWindow(frame_.rootView().windowHandle());
            };
            pyConsolePanel_->onFinished = [this](const ScriptResult& result) {
                if (!commandLineRun_) {
                    return;
                }
                flushCommandLineOutput(result.output);   // the rest: what was printed since the last poll
                if (!result.ok) {
                    std::fputs(result.error.c_str(), stderr);
                }
                std::fflush(stdout);
                std::fflush(stderr);
                finishCommandLineRun(result.ok ? 0 : (result.cancelled ? 2 : 1));
            };
            pyConsolePanel_->onRunningChanged = [this](bool running) {
                busy_ = running;
                taskLabel_ = "A script is running";
                if (scriptEditorPanel_ != nullptr) {
                    scriptEditorPanel_->setRunning(running);
                }
                if (running) {
                    startScriptStatus();
                } else {
                    stopScriptStatus();
                    if (scriptStatusShown_) {
                        model_->say(scriptStatusText(true));
                    }
                }
            };
            pyConsolePanel_->onExecuted = [this]() {
                showTitle();   // the modified marker is not refreshed while a script runs
                graphView_->style().markDirty();
                canvas_->style().markDirty();
                if (nodePanel_ != nullptr) {
                    nodePanel_->show(graphView_->selectionModel().primary());
                }
                if (transformPanel_ != nullptr) {
                    transformPanel_->refresh();
                }
                if (selectPanel_ != nullptr) {
                    selectPanel_->refresh();
                }
            };
            scriptEditorPanel_ = new ScriptEditorPanel();
            rightDock_->addPanel(scriptEditorPanel_);
            scriptEditorPanel_->onDestroyed = [this]() { scriptEditorPanel_ = nullptr; };
            scriptEditorPanel_->applySettings(settings_);
            scriptEditorPanel_->onRun = [this](const std::string& code, const std::string& name) {
                if (pyConsolePanel_ != nullptr) {
                    pyConsolePanel_->runScript(code, name);
                }
            };
            scriptEditorPanel_->onStop = [this]() {
                if (pyConsolePanel_ != nullptr) {
                    pyConsolePanel_->cancelRun();
                }
            };
            scriptEditorPanel_->onChooseOpen = [this]() {
                FileDialogOptions options;
                options.title = "Open script";
                options.filters.push_back({"Python scripts", "*.py"});
                options.filters.push_back({"All Files", "*.*"});
                std::string file;
                return Dialog::showOpenFile(frame_.frameHandle(), options, file) ? std::filesystem::path(file) : std::filesystem::path();
            };
            scriptEditorPanel_->onChooseSave = [this](const std::filesystem::path& current) {
                FileDialogOptions options;
                options.title = "Save script";
                options.filters.push_back({"Python scripts", "*.py"});
                options.defaultExtension = "py";
                options.defaultFileName = current.empty() ? std::string("script.py") : current.filename().string();
                std::string file;
                return Dialog::showSaveFile(frame_.frameHandle(), options, file) ? std::filesystem::path(file) : std::filesystem::path();
            };
            scriptEditorPanel_->onMessage = [this](const std::string& message) { model_->say(message); };
            if (!settingsPath_.empty()) {
                // Next to the settings file, so a run with another --settings file keeps its own script.
                scriptEditorPanel_->setScratchFile(std::filesystem::path(settingsPath_).parent_path() / "scratch.py");
            }
            scriptEditorPanel_->onCheckSyntax = [this](const std::string& code) { return scriptConsole_->checkSyntax(code); };
            scriptEditorPanel_->restoreFile(std::filesystem::u8path(settings_.editorFile));
            scriptEditorPanel_->onFileChanged = [this](const std::filesystem::path& file) {
                settings_.editorFile = file.u8string();
                saveSettings();
            };
        }
        // Whatever the selection is, picked in the graph view or on the canvas, is what the panel shows.
        graphView_->selectionModel().onChanged += [this](SelectionModel& selection) {
            if (nodePanel_ != nullptr) {
                nodePanel_->show(selection.primary());
            }
            if (transformPanel_ != nullptr) {
                transformPanel_->refresh();
            }
            if (selectPanel_ != nullptr) {
                selectPanel_->refresh();
            }
            return SyncReturn::Ignored;
        };
        rightDock_->onPanelMenu += [this](PanelDock&, Panel& panel, const Point& at) {
            showPanelMenu(panel, at);
            return SyncReturn::Handled;
        };
        applyPanelSettings();
        // Saving starts after the saved state is applied, so applying it does not write the half-built state back.
        panelLayout_.onChanged = [this]() {
            floatingPanels_->sync();
            rightDock_->sync();
            capturePanelSettings();
            saveSettings();
        };
        rightDock_->onPanelResized += [this](PanelDock&, Panel&) {
            capturePanelSettings();
            saveSettings();
            return SyncReturn::Handled;
        };
        swatchesTab_ = std::make_unique<SwatchesTab>(document_, documentPanel_->swatchPage());
        swatchesTab_->onMessage = [this](const std::string& message) { model_->say(message); };
        swatchesTab_->presentMenu = [this](const std::vector<PanelMenuEntry>& entries, const Point& screen) {
            if (presentMenu) {
                presentMenu(entries, screen);
            }
        };
        wireSwatchActions(*swatchesTab_);
        documentPanel_->layoutSwatches = [this](float width, float height) {
            if (swatchesTab_ != nullptr) {
                swatchesTab_->layoutFor(width, height);
            }
        };
        documentPanel_->swatchesShown = [this]() {
            if (swatchesTab_ != nullptr) {
                swatchesTab_->refresh();   // what is in use changes while shapes are drawn: it is read again when the tab is shown
            }
        };
        documentPanel_->swatchesContentHeight = [this]() { return swatchesTab_ != nullptr ? swatchesTab_->contentHeight() : 0.0f; };
        swatchesTab_->onContentHeightChanged = [this]() {
            if (documentPanel_ != nullptr) {
                documentPanel_->fitHeight();
            }
        };
        documentPanel_->onDestroyed = [this]() { documentPanel_ = nullptr; };
        if (SwatchPaletteController* cardSwatches = nodePanel_->swatches()) {
            wireSwatchActions(*cardSwatches);
        }
    }

    void WorkspaceController::wireSwatchActions(SwatchesTab& swatches) {
        swatches.onImportPalette = [this]() { importPalette(); };
        swatches.onPaletteFromImage = [this]() { paletteFromImage(); };
        swatches.onSelectUsers = [this](const std::vector<NodeRef>& users) { selectColorUsers(users); };
        swatches.onUse = [this](const Swatch& swatch) { applySwatchToSelection(swatch); };
    }

    void WorkspaceController::applySwatchToSelection(const Swatch& swatch) {
        const NodeRef fill = graphView_->selectionModel().fill();
        if (!fill.valid() || !document_.hasNode(fill)) {
            model_->say("Select a shape first, then use the swatch on its fill");
            return;
        }
        try {
            document_.setGraphFloats(fill, {{"r", swatch.r}, {"g", swatch.g}, {"b", swatch.b}, {"alpha", swatch.a}}, "Use Swatch");
            graphView_->style().markDirty();
            canvas_->style().markDirty();
            nodePanel_->show(graphView_->selected());
            model_->say(swatch.name.empty() ? "Fill set from the swatch" : "Fill set to " + swatch.name);
        } catch (const std::exception& problem) {
            model_->say(problem.what());
        }
    }

    void WorkspaceController::wireSwatchActions(SwatchPaletteController& swatches) {
        swatches.onImportPalette = [this]() { importPalette(); };
        swatches.onPaletteFromImage = [this]() { paletteFromImage(); };
        swatches.onSelectUsers = [this](const std::vector<NodeRef>& users) { selectColorUsers(users); };
    }

    void WorkspaceController::importPalette() {
        if (!model_->documentOpen()) {
            model_->say("Nothing is open to import into");
            return;
        }
        FileDialogOptions options;
        options.title = "Import palette";
        options.filters.push_back({"Swatch files", "*.ase;*.aco"});
        options.filters.push_back({"All Files", "*.*"});
        std::string file;
        if (!Dialog::showOpenFile(frame_.frameHandle(), options, file) || file.empty()) {
            return;
        }
        const SwatchImportResult result = SwatchImport::fromFile(file);
        if (!result.error.empty()) {
            model_->say("Could not import " + fileNameOf(file) + ": " + result.error);
            return;
        }
        try {
            document_.importPalettes(result.palettes);
            const std::size_t count = result.colorCount();
            model_->say("Imported " + std::to_string(count) + (count == 1 ? " colour" : " colours") + " from " + fileNameOf(file));
        } catch (const std::runtime_error& problem) {
            model_->say(problem.what());
        }
    }

    void WorkspaceController::paletteFromImage() {
        if (!model_->documentOpen()) {
            model_->say("Nothing is open to add a palette to");
            return;
        }
        FileDialogOptions options;
        options.title = "New palette from image";
        options.filters.push_back({"Images", "*.png;*.jpg;*.jpeg;*.bmp"});
        options.filters.push_back({"All Files", "*.*"});
        std::string file;
        if (!Dialog::showOpenFile(frame_.frameHandle(), options, file) || file.empty()) {
            return;
        }
        PaletteImageModel choices;
        if (!choices.load(file)) {
            model_->say("Could not make a palette from " + fileNameOf(file) + ": " + choices.error());
            return;
        }
        if (!PaletteImageDialog::run(frame_, choices)) {
            return;
        }
        try {
            document_.addPaletteFrom(choices.name(), choices.found());
            model_->say("Made a palette of " + std::to_string(choices.found().size()) + " colours from " + choices.fileName());
        } catch (const std::runtime_error& problem) {
            model_->say(problem.what());
        }
    }

    void WorkspaceController::selectColorUsers(const std::vector<NodeRef>& users) {
        SelectionModel& selection = graphView_->selectionModel();
        std::vector<NodeRef> shapes;
        for (const NodeRef& user : users) {
            const NodeRef shape = selection.find(SelectionModel::Role::Shape, user);
            shapes.push_back(shape.valid() ? shape : user);
        }
        selection.replace(shapes);
        ensureDockShown(DockSide::Right);
    }

    void WorkspaceController::applyPanelSettings() {
        for (const Settings::PanelState& state : settings_.panels) {
            Panel* panel = rightDock_->panel(state.id);
            const PanelInfo* info = panelLayout_.find(state.id);
            if (panel == nullptr || info == nullptr) {
                continue;
            }
            if (state.bodyHeight > 0.0f) {
                panel->restoreBodyHeight((std::max)(Panel::kMinBodyHeight, state.bodyHeight));
            }
            panel->setCollapsed(state.collapsed);
            panelLayout_.setUserHidden(state.id, state.userHidden);
            if (state.floating && info->canTearOff) {
                panelLayout_.tearOff(state.id, state.floatX, state.floatY);
            } else if (!state.userHidden) {
                panelLayout_.move(state.id, DockLayout::kEnd);   // in the saved order, one after another
            }
        }
    }

    void WorkspaceController::capturePanelSettings() {
        if (rightDock_ == nullptr || floatingPanels_ == nullptr) {
            return;
        }
        std::vector<Settings::PanelState> states;
        for (const PanelInfo& info : panelLayout_.panels()) {
            Settings::PanelState state;
            state.id = info.id;
            state.userHidden = info.userHidden;
            state.floating = info.floating;
            state.floatX = info.floatX;
            state.floatY = info.floatY;
            floatingPanels_->screenPosition(info.id, state.floatX, state.floatY);
            if (const Panel* panel = rightDock_->panel(info.id)) {
                state.collapsed = panel->collapsed();
                state.bodyHeight = panel->userResized() ? panel->bodyHeight() : 0.0f;   // a height the panel chose itself is not saved: its content decides
            }
            states.push_back(state);
        }
        settings_.panels = std::move(states);
    }

    // Resources/graphView.newui (from docs/mockups/graphView.html) lies over the canvas, hidden until the Graph button or Tab shows it.
    void WorkspaceController::buildGraphOverlay() {
        View* loaded = Bundle::instance().loadView("graphView");
        graphOverlay_ = dynamic_cast<SubView*>(loaded);
        if (graphOverlay_ == nullptr) {
            delete loaded;
            throw std::runtime_error("WorkspaceController: Resources/graphView.newui did not load");
        }
        graphView_ = dynamic_cast<GraphView*>(graphOverlay_->findView("graphNodes"));
        if (graphView_ == nullptr) {
            throw std::runtime_error("WorkspaceController: graphView.newui has no GraphView named \"graphNodes\"");
        }
        graphOverlay_->setVisible(false);
        canvas_->addChild(graphOverlay_);
        document_.addView(graphView_);
        canvas_->onSizeChanged.add(this, &WorkspaceController::handleCanvasSized);

        graphView_->onSelectionChanged = [this]() {
            model_->say(graphView_->hint());
            graphCrumbs_->show(graphView_->levelNames());
        };
        graphView_->onViewChanged = [this]() {
            if (auto* zoom = find<Label>(frame_, "graphZoom")) {
                zoom->setText(std::to_string(int(std::lround(graphView_->zoom() * 100.0))) + "%");
            }
        };
        graphView_->onCloseRequested = [this]() { showGraph(false); };
        graphView_->onMessage = [this](const std::string& text) { model_->say(text); };
        registerHistoryActions();
        graphView_->onKeyFilter = [this](std::uint32_t mask, std::uint32_t vk) { return filterGraphKey(mask, vk); };
        graphView_->onPasted = [this](std::uint32_t scope, const std::vector<NodeRef>& pasted) {
            if (!assetEvents_) {
                return;
            }
            std::vector<jitnodes::NodeId> ids;
            for (const NodeRef& ref : pasted) {
                ids.push_back(ref.id);
            }
            const std::string errors = assetEvents_->fireCreatedFor(document_, &graphView_->selectionModel(), scope, ids, "pasted");
            if (!errors.empty()) {
                model_->say(errors);
            }
        };
        wireAssetEvents();
        graphView_->onKeyUp.add(this, &WorkspaceController::handleKeyUp);
        graphView_->onMouseDown.add(this, &WorkspaceController::handleGraphMouseDown);
        graphView_->onMouseMove.add(this, &WorkspaceController::handleGraphMouseMove);
        graphView_->onMouseUp.add(this, &WorkspaceController::handleGraphMouseUp);

        auto* crumbBox = dynamic_cast<SubView*>(graphOverlay_->findView("graphCrumbs"));
        auto* crumbCurrent = dynamic_cast<Label*>(graphOverlay_->findView("graphCrumb"));
        if (crumbBox == nullptr || crumbCurrent == nullptr) {
            throw std::runtime_error("WorkspaceController: graphView.newui has no graphCrumbs / graphCrumb");
        }
        graphCrumbs_ = std::make_unique<GraphCrumbs>(*crumbBox, *crumbCurrent);
        graphCrumbs_->onNavigate = [this](std::size_t level) {
            graphView_->goToLevel(level);
            focusGraph();
        };

        View* popup = Bundle::instance().loadView("pointTable");
        auto* popupView = dynamic_cast<SubView*>(popup);
        if (popupView == nullptr) {
            delete popup;
            throw std::runtime_error("WorkspaceController: Resources/pointTable.newui did not load");
        }
        pointFrame_ = std::make_unique<ToolFrame>();
        pointFrame_->setTitle("Data");
        pointFrame_->setBounds(Rect(0.0f, 0.0f, 660.0f, 500.0f));
        pointFrame_->setMinimumSize(Size(380.0f, 260.0f));
        pointFrame_->setActivatesOnClick(true);   // its filter field takes keystrokes
        if (frame_.frameHandle() == nullptr || !pointFrame_->initialize(frame_.frameHandle())) {
            throw std::runtime_error("WorkspaceController: the data window could not be created (the main window does not exist yet)");
        }
#ifdef CURVY_GUI_AUTOMATION
        pointFrame_->rootView().setName("dataRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
        Automation::expose(pointFrame_->rootView());
#endif
        pointFrame_->rootView().addChild(popupView);
        pointFrame_->rootView().onSizeChanged += [popupView](View&, const Size& size) {
            const Rect all(0.0f, 0.0f, size.width, size.height);
            popupView->setBounds(all);
            if (View* box = popupView->findView("pointTableBox")) {
                box->setBounds(all);
            }
            popupView->updateLayout();
            return SyncReturn::Handled;
        };
        pointFrame_->onClosed += [this](ToolFrame&) {
            pointTable_->hide();
            return SyncReturn::Handled;
        };
        pointTable_ = std::make_unique<PointTable>(*popupView, document_);
        pointTable_->onHidden = [this]() {
            pointFrame_->hide();
            focusGraph();
        };
        if (auto* fit = dynamic_cast<Button*>(graphOverlay_->findView("graphFitButton"))) {
            fit->onClick += [this](Control&) {
                graphView_->fit();
                focusGraph();
                return SyncReturn::Handled;
            };
        }
    }

    SyncReturn WorkspaceController::handleCanvasSized(View&, const Size&) {
        layoutGraphOverlay();
        return SyncReturn::Handled;
    }

    void WorkspaceController::layoutGraphOverlay() {
        const Rect client = canvas_->getClientBounds();
        const Rect all(0.0f, 0.0f, client.width(), client.height());
        graphOverlay_->setBounds(all);
        if (View* box = graphOverlay_->findView("graphViewBox")) {
            box->setBounds(all);
        }
        graphOverlay_->updateLayout();
    }

    RadialRing WorkspaceController::nodeRing(const std::string& name) {
        return {name, "Data...", "graph.data", {
            {"Display", "D", "graph.display"}, {"Bypass", "B", "graph.bypass"}, {"Freeze", "F", "graph.freeze"}, {"Rename", "R", "graph.rename"},
            {"Group", "G", "graph.group"}, {"Ungroup", "U", "graph.ungroup"}, {"Enter", "E", "graph.enter"}, {"Exit", "X", "graph.exit"}}};
    }

    // While the Node panel's colour card is open the number keys 1 to 8 apply the recent colours. The card lies over the canvas, so a click in it
    // leaves the keyboard with the canvas as often as with the graph view; both filters ask.
    bool WorkspaceController::recentColorKey(std::uint32_t vk, bool shift) {
        if (shift || vk < vkNumber1 || vk > vkNumber8 || nodePanel_ == nullptr || !nodePanel_->colorPickerOpen()) {
            return false;
        }
        const std::size_t index = vk - vkNumber1;
        if (nodePanel_->useRecentColor(index)) {
            graphView_->style().markDirty();
            canvas_->style().markDirty();
            model_->say("Recent colour " + std::to_string(index + 1));
        } else {
            model_->say("No recent colour " + std::to_string(index + 1));
        }
        return true;
    }

    bool WorkspaceController::filterGraphKey(std::uint32_t mask, std::uint32_t vk) {
        if ((mask & (kmAlt | kmCtrl)) != 0) {
            return false;
        }
        const bool shift = (mask & kmShift) != 0;
        if (menu_->isOpen()) {
            if (vk == vkEscape || vk == vkSpaceBar) {
                menu_->handleEscape();
                model_->setRing(std::string());
                model_->say("Ring closed");
                focusGraph();
            } else {
                menu_->handleKey(keyName(vk), shift);
            }
            return true;
        }
        if (vk == vkTab && !shift) {
            toggleGraph();
            return true;
        }
        if (vk == vkSpaceBar) {
            if (!spaceDown_) {
                spaceDown_ = true;
                spaceUsed_ = false;
                spaceStart_ = std::chrono::steady_clock::now();
                model_->setSpaceDown(true);
            }
            return true;
        }
        if (recentColorKey(vk, shift)) {
            return true;
        }
        const std::string key = keyName(vk);
        if (!key.empty() && !shift) {
            for (const RadialSlot& slot : nodeRing("").slots) {
                if (slot.key == key) {
                    runGraphCommand(slot.command);
                    return true;
                }
            }
        }
        return false;
    }

    SyncReturn WorkspaceController::handleGraphMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        nodePanel_->closeColorPicker();
        if ((buttons & mbmRightButton) != 0) {
            pointer_ = graphView_->mapTo(*canvas_, pt);
            pointerKnown_ = true;
            openRing(true);
        }
        return SyncReturn::Handled;
    }

    SyncReturn WorkspaceController::handleGraphMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        return handleMouseMove(*graphView_, graphView_->mapTo(*canvas_, pt), buttons, keys);
    }

    SyncReturn WorkspaceController::handleGraphMouseUp(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        return handleMouseUp(*graphView_, graphView_->mapTo(*canvas_, pt), buttons, keys);
    }

    bool WorkspaceController::runGraphCommand(const std::string& command) {
        if (command.rfind("graph.", 0) != 0) {
            return false;
        }
        const NodeRef selected = graphView_->selected();
        try {
            if (command == "graph.exit") {
                if (!graphView_->exit()) {
                    model_->say("Already at the root");
                }
                return true;
            }
            if (!selected.valid() || !document_.hasNode(selected)) {
                model_->say("Select a node first");
                return true;
            }
            const std::string name = document_.node(selected).name();
            if (command == "graph.display") {
                document_.setNodeDisplayed(selected);
                model_->say("Showing " + name + " on the canvas");
            } else if (command == "graph.bypass") {
                const bool now = !document_.node(selected).bypassed();
                document_.setNodeBypassed(selected, now);
                model_->say(name + (now ? " bypassed" : " enabled"));
            } else if (command == "graph.freeze") {
                const bool now = !document_.node(selected).frozen();
                document_.setNodeFrozen(selected, now);
                model_->say(name + (now ? " frozen" : " unfrozen"));
            } else if (command == "graph.rename") {
                nodePanel_->focusName();
                model_->say("Type the new name and press Enter");
                return true;
            } else if (command == "graph.group") {
                const NodeRef group = document_.groupNode(selected);
                graphView_->select(NodeRef());
                graphView_->select(group);
                model_->say("Grouped " + name);
            } else if (command == "graph.ungroup") {
                const std::vector<NodeRef> out = document_.ungroup(selected);
                graphView_->select(NodeRef());
                if (!out.empty()) {
                    graphView_->select(out.front());
                }
                model_->say("Ungrouped " + name);
            } else if (command == "graph.enter") {
                if (!graphView_->enter(selected)) {
                    model_->say("Only a layer or group can be entered");
                }
                return true;
            } else if (command == "graph.data") {
                showPointTable();
                return true;
            } else {
                model_->say("(" + command + " is not wired yet)");
                return true;
            }
        } catch (const std::exception& error) {
            model_->say(error.what());
        }
        nodePanel_->show(graphView_->selected());
        transformPanel_->refresh();
        selectPanel_->refresh();
        graphView_->style().markDirty();
        canvas_->style().markDirty();
        return true;
    }

    void WorkspaceController::showPointTable() {
        if (!graphView_->selected().valid()) {
            model_->say("Select a node to see its data");
            return;
        }
        if (!pointFrame_->isVisible()) {
            const Rect screen = canvas_->localToScreen(canvas_->getClientBounds());
            const float w = pointFrame_->getBounds().width(), h = pointFrame_->getBounds().height();
            pointFrame_->moveTo(Rect(screen.left() + (screen.width() - w) * 0.5f, screen.top() + (screen.height() - h) * 0.5f, w, h));
        }
        pointTable_->show(graphView_->selected());
        pointFrame_->show();
        pointTable_->root()->updateLayout();
    }

    void WorkspaceController::focusGraph() {
        if (RootView* root = graphView_->rootView()) {
            root->setFocusedSubView(graphView_);
        }
    }

    void WorkspaceController::showGraph(bool show) {
        if (show && !model_->documentOpen()) {
            model_->say("Open or start a document first");
            return;
        }
        if (graphOverlay_->isVisible() == show) {
            return;
        }
        if (show) {
            ensureDockShown(DockSide::Right);   // before the layout, so the overlay is sized for the narrower canvas
            frame_.rootView().updateLayout();   // the dock changes the canvas size, which the overlay reads next
            layoutGraphOverlay();
        }
        if (!show) {
            pointTable_->hide();
        }
        graphOverlay_->setVisible(show);
        if (show) {
            focusGraph();
            graphView_->onSelectionChanged();
        } else {
            focusCanvas();
        }
        canvas_->style().markDirty();
        model_->say(show ? graphView_->hint() : "Canvas");
    }

    WorkspaceController::~WorkspaceController() {
        *alive_ = false;
        jitnodes::python::setPythonNodePolicy(nullptr);
        jitnodes::python::setPythonNodeInterruptCheck(nullptr);
        if (libraryListener_ != 0) {
            jitnodes::AssetLibrary::instance().removeListener(libraryListener_);
        }
        document_.afterEdit = nullptr;
        document_.beforeRemove = nullptr;
        document_.afterRemove = nullptr;
        document_.onTimeMoved = nullptr;
        for (Action& action : historyActions_) {
            loop_->unregisterAction(&action);
        }
        if (transformPanel_ != nullptr) {
            transformPanel_->onDestroyed = nullptr;
        }
        if (selectPanel_ != nullptr) {
            selectPanel_->onDestroyed = nullptr;
            selectPanel_->onMessage = nullptr;
        }
        if (assetPanel_ != nullptr) {   // its callbacks name this window
            assetPanel_->onDestroyed = nullptr;
            assetPanel_->onNewRequested = nullptr;
            assetPanel_->onEditRequested = nullptr;
            assetPanel_->onSaveToLibraryRequested = nullptr;
            assetPanel_->onLoadFileRequested = nullptr;
            assetPanel_->onLoadFolderRequested = nullptr;
            assetPanel_->onMessage = nullptr;
            assetPanel_->presentMenu = nullptr;
        }
        if (pyConsolePanel_ != nullptr) {
            pyConsolePanel_->onDestroyed = nullptr;
        }
        if (scriptEditorPanel_ != nullptr) {
            scriptEditorPanel_->onDestroyed = nullptr;
        }
        if (documentPanel_ != nullptr) {
            documentPanel_->onDestroyed = nullptr;
            documentPanel_->layoutSwatches = nullptr;
            documentPanel_->swatchesShown = nullptr;
            documentPanel_->swatchesContentHeight = nullptr;
        }
        if (nodePanel_ != nullptr) {   // null when the window's teardown already destroyed the panel
            nodePanel_->onDestroyed = nullptr;
            nodePanel_->releaseDocument();   // the dock destroys the panel after the document is gone
        }
        if (scriptConsole_ != nullptr) {
            scriptConsole_->abort();   // a background script edits the document that is about to go
            scriptConsole_->setDocumentFileHook(nullptr);
        }
        stopScriptStatus();
        task_.cancel();
        task_.waitFor(std::chrono::seconds(30));   // the task's own destructor joins; this only bounds how long a stuck job is waited on first
        if (timer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(timer_);
        }
    }

    // ---- Open and save, on a worker ----

    std::string WorkspaceController::fileNameOf(const std::string& path) {
        return std::filesystem::u8path(path).filename().u8string();
    }

    void WorkspaceController::runTask(const std::string& label, std::function<bool()> work, std::function<void(bool)> finish) {
        busy_ = true;
        taskLabel_ = label;
        taskPercent_ = -1;
        taskFinish_ = std::move(finish);
        document_.setTaskProgress(&task_.progress());
        model_->say(label + " - Esc cancels");
        const bool started = task_.start([this, alive = alive_, work = std::move(work)](TaskProgress&) {
            bool ok = false;
            try {
                ok = work();
            } catch (...) {
            }
            loop_->post([this, alive, ok]() {
                if (*alive) {
                    finishTask(ok);
                }
            });
        });
        if (!started) {
            busy_ = false;
            document_.setTaskProgress(nullptr);
            return;
        }
        timer_ = loop_->postDelayed(std::chrono::milliseconds(33), [this]() { return tickTask(); });
    }

    bool WorkspaceController::tickTask() {
        if (!busy_) {
            timer_ = RunLoop::kInvalidTimerHandle;
            return true;
        }
        const float fraction = task_.progress().fraction();
        const int percent = fraction < 0.0f ? -1 : int(fraction * 100.0f);
        if (percent != taskPercent_) {
            taskPercent_ = percent;
            model_->say(taskLabel_ + (percent < 0 ? std::string() : " " + std::to_string(percent) + "%") + " - Esc cancels");
        }
        return false;
    }

    void WorkspaceController::finishTask(bool ok) {
        task_.waitFor(std::chrono::seconds(10));   // the worker posted this as its last act: it is ending
        if (timer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(timer_);
            timer_ = RunLoop::kInvalidTimerHandle;
        }
        busy_ = false;
        document_.setTaskProgress(nullptr);
        std::function<void(bool)> finish = std::move(taskFinish_);
        taskFinish_ = nullptr;
        if (finish) {
            finish(ok);
        }
    }

    void WorkspaceController::showDocument() {
        canvas_->setDocument(&document_);
        graphView_->setDocument(&document_);
        model_->setDocument(true, document_.name());
        if (assetEvents_) {
            assetEvents_->remember(document_);
        }
        if (documentPanel_ != nullptr) {
            documentPanel_->refresh();
            swatchesTab_->setDocumentOpen(true);   // New and Open change the palettes without telling the tab
        }
        showTitle();
        applyView(document_.viewState());
        canvas_->style().markDirty();
    }

    ViewState WorkspaceController::captureView() const {
        ViewState view;
        if (!model_->documentOpen()) {
            return view;
        }
        if (!canvas_->centered()) {
            view.canvas = CanvasViewState{canvas_->zoom(), canvas_->panX(), canvas_->panY()};
        }
        view.graph = graphView_->state();
        view.graph->open = graphShown();
        return view;
    }

    void WorkspaceController::applyView(const ViewState& view) {
        if (view.canvas) {
            canvas_->setZoom(view.canvas->zoom);
            canvas_->setPan(view.canvas->panX, view.canvas->panY);
        } else {
            canvas_->zoomToFit(1.0f);
        }
        if (view.graph) {
            graphView_->restore(*view.graph);
        }
        showGraph(view.graph && view.graph->open);
    }

    void WorkspaceController::openFile(const std::string& path) {
        if (busy_) {
            model_->say(taskLabel_ + " - wait for it to finish, or press Esc to cancel");
            return;
        }
        std::string file = path;
        if (file.empty()) {
            FileDialogOptions options;
            options.title = "Open";
            options.filters.push_back({"curvy documents", "*.curvy"});
            options.filters.push_back({"All Files", "*.*"});
            if (!Dialog::showOpenFile(frame_.frameHandle(), options, file) || file.empty()) {
                return;
            }
        }
        if (!confirmDiscard()) {
            return;
        }
        // The worker rewrites the document, so the canvas lets go of it until the load is over.
        const bool wasOpen = model_->documentOpen();
        const ViewState previousView = captureView();
        document_.removeView(canvas_);
        document_.removeView(graphView_);
        canvas_->setDocument(nullptr);
        graphView_->setDocument(nullptr);
        runTask("Opening " + fileNameOf(file), [this, file]() { return document_.load(file); }, [this, file, wasOpen, previousView](bool ok) {
            document_.addView(canvas_);
            document_.addView(graphView_);
            if (ok) {
                showDocument();
                model_->say("Opened " + fileNameOf(file));
                reportMigrationProblems();
                fireLoadedEvents();
                offerObjectRepair();
            } else {
                if (wasOpen) {
                    document_.setViewState(previousView);
                    showDocument();   // a refused or cancelled load leaves the document as it was
                }
                model_->say(task_.progress().cancelled() ? std::string("Open cancelled") : "Could not open " + fileNameOf(file) + ": " + document_.lastError());
            }
        });
    }

    void WorkspaceController::offerObjectRepair() {
        const auto& problems = document_.objectProblems();
        if (problems.empty()) {
            return;
        }
        std::string text = std::to_string(problems.size()) + (problems.size() == 1 ? " layer or group in this file is damaged:\n\n" : " layers and groups in this file are damaged:\n\n");
        for (std::size_t i = 0; i < problems.size() && i < 5; ++i) {
            text += problems[i].text + "\n";
        }
        if (problems.size() > 5) {
            text += "and " + std::to_string(problems.size() - 5) + " more\n";
        }
        text += "\nRepair them? Repairing changes their structure, which may undo something that was done on purpose. You can undo it afterwards.";
        if (Dialog::showMessageBox(frame_.frameHandle(), text, "Damaged objects", MessageBoxButtons::YesNo, MessageBoxIcon::Warning) == DialogResult::Yes) {
            document_.repairObjectProblems();
            model_->say("Repaired the damaged objects");
        } else {
            model_->say("Opened " + std::to_string(problems.size()) + " damaged object(s) without repairing them");
        }
    }

    void WorkspaceController::saveFile(const std::string& path) {
        if (busy_) {
            model_->say(taskLabel_ + " - wait for it to finish, or press Esc to cancel");
            return;
        }
        if (!model_->documentOpen()) {
            model_->say("Nothing is open to save");
            return;
        }
        std::string file = path;
        if (file.empty() && document_.hasFilePath()) {
            file = document_.filePath();
        }
        if (file.empty()) {
            FileDialogOptions options;
            options.title = "Save As";
            options.filters.push_back({"curvy documents", "*.curvy"});
            options.defaultExtension = "curvy";
            options.defaultFileName = document_.name() + ".curvy";
            if (!Dialog::showSaveFile(frame_.frameHandle(), options, file) || file.empty()) {
                return;
            }
        }
        document_.setViewState(captureView());
        runTask("Saving " + fileNameOf(file), [this, file]() { return document_.save(file); }, [this, file](bool ok) {
            showTitle();
            model_->say(ok ? "Saved " + fileNameOf(file)
                           : task_.progress().cancelled() ? std::string("Save cancelled") : "Could not save " + fileNameOf(file) + ": " + document_.lastError());
        });
    }

    void WorkspaceController::bindViews() {
        canvas_->onSizeChanged += [this](View&, const Size& size) {
            menu_->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
            return SyncReturn::Handled;
        };
        menu_->setBounds(Rect(0.0f, 0.0f, canvas_->bounds().size().width, canvas_->bounds().size().height));
        menu_->onCommand.add(this, &WorkspaceController::handleRingCommand);

        canvas_->onZoomChanged += [this](CanvasView&) {
            model_->setZoom(double(canvas_->zoom()) * 100.0);
            return SyncReturn::Handled;
        };
        canvas_->onPressFilter = [this](const Point& pt, std::uint32_t buttons, std::uint32_t keys) { return filterPress(pt, buttons, keys); };
        canvas_->onDoubleClickFilter = [this](const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
            const RootView* root = canvas_->rootView();
            return doubleClicked(root != nullptr ? root->penState() : PenState(), pt, buttons, keys);
        };
        canvas_->onKeyFilter = [this](std::uint32_t mask, std::uint32_t vk) { return filterKey(mask, vk); };
        canvas_->onPaintTop = [this](BLContext& ctx) { paintTop(ctx); };
        canvas_->shapeDimmed = [this](const std::string& key) { return graphView_->selectionModel().isShapeKeyDimmed(key); };
        graphView_->selectionModel().onChanged += [this](SelectionModel&) {
            canvas_->style().markDirty();   // the outline of the selected shapes
            return SyncReturn::Ignored;
        };
        canvas_->onKeyUp.add(this, &WorkspaceController::handleKeyUp);
        canvas_->onMouseMove.add(this, &WorkspaceController::handleMouseMove);
        canvas_->onMouseUp.add(this, &WorkspaceController::handleMouseUp);

        // A button that was clicked keeps the keyboard focus and would take the next Space, which belongs to the canvas.
        struct Toggle {
            const char* button;
            DockSide side;
        };
        for (const Toggle& toggle : {Toggle{"toolbarToggle", DockSide::Left}}) {
            if (auto* button = dynamic_cast<Button*>(frame_.rootView().findView(toggle.button))) {
                button->onClick += [this, side = toggle.side](Control&) {
                    toggleDock(side);
                    focusCanvas();
                    return SyncReturn::Handled;
                };
            }
        }
        // The Panels button lists the panels; the dock itself is the menu's last line.
        if (auto* button = dynamic_cast<Button*>(frame_.rootView().findView("propertiesToggle"))) {
            button->onClick += [this, button](Control&) {
                showPanelsMenu(button->localToScreen(Point(0.0f, button->bounds().height())));
                focusCanvas();
                return SyncReturn::Handled;
            };
        }
        if (auto* button = dynamic_cast<Button*>(frame_.rootView().findView("scriptsToggle"))) {
            button->onClick += [this, button](Control&) {
                showScriptsMenu(button->localToScreen(Point(0.0f, button->bounds().height())));
                focusCanvas();
                return SyncReturn::Handled;
            };
        }
        if (auto* button = dynamic_cast<Button*>(frame_.rootView().findView("graphToggle"))) {
            button->onClick += [this](Control&) {
                toggleGraph();
                return SyncReturn::Handled;
            };
        }
    }

    void WorkspaceController::toggleGraph() {
        showGraph(!graphOverlay_->isVisible());
    }

    std::filesystem::path WorkspaceController::userScriptsFolder() const {
        const std::string settingsFile = settingsPath_.empty() ? Settings::defaultPath() : settingsPath_;
        return std::filesystem::u8path(settingsFile).parent_path() / "scripts";
    }

    std::vector<PanelMenuEntry> WorkspaceController::scriptsMenuEntries() {
        return ScriptsMenu::entries(
            userScriptsFolder(), executableDirectory() / "Resources" / "samples" / "scripts",
            [this](const ScriptFile& file) { runMenuScript(file); },
            [this]() {
                const std::filesystem::path folder = userScriptsFolder();
                std::error_code ignored;
                std::filesystem::create_directories(folder, ignored);
                ::ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            });
    }

    void WorkspaceController::showScriptsMenu(const Point& screen) {
        if (scriptConsole_ == nullptr || pyConsolePanel_ == nullptr) {
            model_->say("Python is not available, so scripts cannot run");
            return;
        }
        presentMenu(scriptsMenuEntries(), screen);
    }

    void WorkspaceController::runMenuScript(const ScriptFile& file) {
        if (busy_) {
            model_->say(taskLabel_ + " - wait for it to finish, or press Esc to cancel");
            return;
        }
        std::string code;
        if (!ScriptsMenu::readScript(file.path, code)) {
            model_->say("Could not read " + file.name + ".py");
            return;
        }
        if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
            model_->say(file.name + ".py is empty");
            return;
        }
        pyConsolePanel_->runScript(code, file.name + ".py");
    }

    void WorkspaceController::showPanelsMenu(const Point& screen) {
        const bool shown = !docks_->collapsed(DockSide::Right);
        presentMenu(panelsMenuEntries(shown), screen);
    }

    std::vector<PanelMenuEntry> WorkspaceController::panelsMenuEntries(bool dockShown) {
        return PanelsMenu::forPanels(
            panelLayout_, dockShown,
            [this]() {
                if (docks_->collapsed(DockSide::Right)) {
                    toggleDock(DockSide::Right);
                }
            },
            [this]() { toggleDock(DockSide::Right); });
    }

    void WorkspaceController::showPanelMenu(Panel& panel, const Point& screen) {
        const Point floatAt(screen.x - DockLayout::kTearOffDistance - 200.0f, screen.y);
        presentMenu(PanelsMenu::forPanel(panelLayout_, panel, floatAt), screen);
    }

    // The Node panel is what shows the selection, so whatever selects something or shows the graph makes sure the dock it lives in is open.
    void WorkspaceController::ensureDockShown(DockSide side) {
        if (docks_ != nullptr && docks_->collapsed(side)) {
            docks_->setCollapsed(side, false);
        }
    }

    void WorkspaceController::toggleDock(DockSide side) {
        docks_->toggle(side);
        std::string label = DockController::name(side);
        label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
        model_->say(label + " area " + (docks_->collapsed(side) ? "hidden" : "shown"));
    }

    void WorkspaceController::start() {
        graphOverlay_->setVisible(false);
        canvas_->setDocument(nullptr);
        graphView_->setDocument(nullptr);
        model_->setDocument(false, std::string());
        showTitle();
        focusCanvas();
    }

    void WorkspaceController::focusCanvas() {
        if (RootView* root = canvas_->rootView()) {
            if (SubView* target = UIInputManager::instance().resolveClickFocusTarget(canvas_)) {
                root->setFocusedSubView(target);
            }
        }
    }

    SyncReturn WorkspaceController::modelChanged(Model&) {
        // A label sizes itself when the bar is laid out, so new text needs a layout pass or it is clipped to the old width.
        std::vector<View*> bars;
        auto show = [&bars](Label* label, const std::string& text, bool fitWidth) {
            if (label != nullptr && label->text() != text) {
                label->setText(text);
                if (fitWidth) {   // flex does not measure text, so these two are sized to it here
                    if (auto* style = dynamic_cast<LabelStyle*>(&label->style())) {
                        label->setDesiredSize(Size(std::ceil(style->font().measureText(text).width) + 4.0f, label->bounds().size().height));
                    }
                }
                if (std::find(bars.begin(), bars.end(), label->parent()) == bars.end()) {
                    bars.push_back(label->parent());
                }
            }
        };
        show(hintTool_, model_->hintTool(), true);
        show(hintKeys_, model_->hintKeys(), false);
        show(hintInfo_, model_->hintInfo(), false);
        show(documentName_, model_->documentName(), true);
        for (View* bar : bars) {
            bar->updateLayout();
        }
        return SyncReturn::Handled;
    }

    // ---- Rings ----

    Point WorkspaceController::ringPoint() const {
        const Rect client = canvas_->getClientBounds();
        return pointerKnown_ ? pointer_ : Point(client.width() * 0.5f, client.height() * 0.5f);
    }

    void WorkspaceController::showRing(const RadialRing& ring, const Point& at, bool byPress) {
        menu_->open(ring, at, byPress);
        model_->setRing(ring.title);
    }

    void WorkspaceController::openRing(bool byPress) {
        if (graphOverlay_->isVisible()) {
            const NodeRef selected = graphView_->selected();
            if (!selected.valid() || !document_.hasNode(selected)) {
                model_->say("Select a node first");
                return;
            }
            showRing(nodeRing(document_.node(selected).name()), ringPoint(), byPress);
            return;
        }
        if (!model_->documentOpen()) {
            showRing(newRing(), ringPoint(), byPress);
            return;
        }
        // Over a shape: the object ring (the shape is selected first, unless it already is, so Group and Arrange act on the whole selection).
        // Over the page: the creation ring. Over the grey workspace around it: the document ring.
        const Point at = ringPoint();
        const std::optional<ShapeHit> hit = canvas_->shapeAt(at);
        if (hit.has_value() && !hit->key.empty() && !graphView_->selectionModel().isShapeKeyDimmed(hit->key)) {
            SelectionModel& selection = graphView_->selectionModel();
            if (!selection.isShapeKeySelected(hit->key)) {
                selection.set(selection.pickShapeKey(hit->key));
            }
            showRing(objectRing(), at, byPress);
            return;
        }
        const BLPoint world = canvas_->toDocument(at);
        const bool onPage = world.x >= 0.0 && world.y >= 0.0 && world.x <= double(document_.pageWidth()) && world.y <= double(document_.pageHeight());
        showRing(onPage ? createRing() : documentRing(), at, byPress);
    }

    void WorkspaceController::newDocument(const std::string& name, float width, float height) {
        NewDocumentModel settings;
        settings.loadSetup(double(width), double(height), 72.0, ColorSpace::RGB, PageBackground::White);
        settings.setName(name);
        startDocument(settings);
    }

    void WorkspaceController::startDocument(const NewDocumentModel& settings) {
        if (!confirmDiscard()) {
            return;
        }
        const newui::Color page = settings.background() == PageBackground::Transparent ? newui::Color(1.0f, 1.0f, 1.0f, 0.0f) : newui::Color(1.0f, 1.0f, 1.0f, 1.0f);
        document_.newBlank(settings.name(), float(settings.widthPoints()), float(settings.heightPoints()), float(settings.ppi()), settings.colorMode(), page);
        canvas_->setDocument(&document_);
        graphView_->setDocument(&document_);
        canvas_->zoomToFit(1.0f);
        model_->setDocument(true, settings.name());
        if (documentPanel_ != nullptr) {
            documentPanel_->refresh();
            swatchesTab_->setDocumentOpen(true);   // New changes the palettes without telling the tab
        }
        showTitle();
        canvas_->style().markDirty();
        model_->say("New " + settings.name());
    }

    bool WorkspaceController::newDocumentDialog() {
        NewDocumentModel settings;
        if (!NewDocumentDialog::run(frame_, settings)) {
            return false;
        }
        startDocument(settings);
        return true;
    }

    bool WorkspaceController::documentSetup() {
        if (!model_->documentOpen()) {
            model_->say("Nothing is open to set up");
            return false;
        }
        const newui::Color white(1.0f, 1.0f, 1.0f, 1.0f);
        PageBackground background = PageBackground::White;
        if (document_.pageTransparent()) {
            background = PageBackground::Transparent;
        } else if (!(document_.pageColor() == white)) {
            background = PageBackground::CurrentFill;
        }
        NewDocumentModel settings;
        settings.loadSetup(double(document_.pageWidth()), double(document_.pageHeight()), double(document_.ppi()), document_.colorMode(), background);
        settings.setName(document_.name());
        if (!NewDocumentDialog::runSetup(frame_, settings, true)) {
            return false;
        }
        if (!settings.name().empty()) {
            document_.setName(settings.name());
        }
        document_.setPageSize(float(settings.widthPoints()), float(settings.heightPoints()));
        canvas_->zoomToFit(1.0f);
        document_.setPpi(float(settings.ppi()));
        document_.setColorMode(settings.colorMode());
        switch (settings.background()) {
            case PageBackground::Transparent:
                document_.setPageColor(newui::Color(1.0f, 1.0f, 1.0f, 0.0f));
                break;
            case PageBackground::CurrentFill:
                if (background != PageBackground::CurrentFill) {   // an existing custom color stays when it is unchanged
                    document_.setPageColor(white);
                }
                break;
            default:
                document_.setPageColor(white);
                break;
        }
        model_->setDocument(true, document_.name());
        if (documentPanel_ != nullptr) {
            documentPanel_->refresh();
            swatchesTab_->setDocumentOpen(true);   // New and Open change the palettes without telling the tab
        }
        showTitle();
        canvas_->style().markDirty();
        model_->say("Document setup changed");
        return true;
    }

    SyncReturn WorkspaceController::handleRingCommand(RadialMenu&, const std::string& command) {
        model_->setRing(std::string());
        runCommand(command);
        if (command == "graph.rename") {
            return SyncReturn::Handled;   // the name field keeps the keyboard
        }
        if (graphOverlay_->isVisible()) {
            focusGraph();
        } else {
            focusCanvas();
        }
        return SyncReturn::Handled;
    }

    bool WorkspaceController::commandWired(const std::string& command) {
        static const std::set<std::string> wired = {
            "file.open", "file.save", "dialog.new", "dialog.setup", "dialog.settings", "view.fitPage", "view.toolbar", "view.properties",
            "view.graph", "view.theme", "select.by", "select.similar", "tool.select", "options.select",
            "graph.display", "graph.bypass", "graph.freeze", "graph.rename", "graph.group", "graph.ungroup", "graph.enter", "graph.exit", "graph.data"};
        if (wired.count(command) != 0) {
            return true;
        }
        for (const PageSize& page : kPages) {
            if (command == page.command) {
                return true;
            }
        }
        for (const RingEntry& ring : kRings) {
            if (command == ring.command) {
                return true;
            }
        }
        return false;
    }

    void WorkspaceController::runCommand(const std::string& command) {
        if (busy_ && command != "view.theme" && command.rfind("ring.", 0) != 0) {
            model_->say(taskLabel_ + " - wait for it to finish, or press Esc to cancel");
            return;
        }
        if (runGraphCommand(command)) {
            return;
        }
        if (command == "file.open") {
            openFile();
            return;
        }
        if (command == "file.save") {
            saveFile();
            return;
        }
        for (const PageSize& page : kPages) {
            if (command == page.command) {
                newDocument(page.name, page.width, page.height);
                return;
            }
        }
        for (const RingEntry& ring : kRings) {
            if (command == ring.command) {
                const RadialRing made = ring.make();
                showRing(made, ringPoint(), false);
                return;
            }
        }
        if (command == "dialog.new") {
            newDocumentDialog();
        } else if (command == "dialog.setup") {
            documentSetup();
        } else if (command == "view.fitPage") {
            if (model_->documentOpen()) {
                canvas_->zoomToFit();
                model_->say("Fit page");
            }
        } else if (command == "select.by") {
            showSelectPanel();
        } else if (command == "tool.select") {
            tools_.setCurrent("select");
            model_->say("Select");
        } else if (command.compare(0, 5, "tool.") == 0) {
            model_->say("The " + command.substr(5) + " tool is not built yet");
        } else if (command == "options.select") {
            showToolOptions("select", menu_->optionAnchor());
        } else if (command == "select.similar") {
            if (selectPanel_ != nullptr && !selectPanel_->selectSimilar()) {
                model_->say(selectPanel_->status());
            }
        } else if (command == "dialog.settings") {
            settingsDialog();
        } else if (command == "view.toolbar") {
            toggleDock(DockSide::Left);
        } else if (command == "view.properties") {
            toggleDock(DockSide::Right);
        } else if (command == "view.graph") {
            toggleGraph();
        } else if (command == "view.theme") {
            setDarkTheme(!Theme::current().isDark);
            model_->say(Theme::current().isDark ? "Dark theme" : "Light theme");
        } else {
            model_->say("(" + command + " is not wired yet)");
        }
    }

    void WorkspaceController::showSelectPanel() {
        if (selectPanel_ == nullptr) {
            return;
        }
        ensureDockShown(DockSide::Right);
        selectPanel_->setCollapsed(false);
        selectPanel_->refresh();
        model_->say("Select by: choose a property, then a value");
    }

    // ---- Pen and mouse ----

    bool WorkspaceController::filterPress(const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        const RootView* root = canvas_->rootView();
        return pressed(root != nullptr ? root->penState() : PenState(), pt, buttons, keys);
    }

    bool WorkspaceController::doubleClicked(const PenState& pen, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        const bool plainLeft = (buttons & mbmLeftButton) != 0 && (buttons & mbmRightButton) == 0 && !spaceDown_ && !pen.eraser && model_->documentOpen() &&
                               !graphOverlay_->isVisible() && !menu_->isOpen();
        if (plainLeft && tools_.current()->inputDoubleClick(toolEvent(pt, buttons, keys, false))) {
            pointer_ = pt;
            pointerKnown_ = true;
            return true;
        }
        return pressed(pen, pt, buttons, keys);
    }

    bool WorkspaceController::pressed(const PenState& pen, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        pointer_ = pt;
        pointerKnown_ = true;
        panning_ = false;
        zooming_ = false;
        if (optionsCard_ != nullptr && optionsCard_->isOpen() && !optionsCard_->contains(pt)) {
            optionsCard_->close();
        }
        if (pen.eraser && (buttons & mbmLeftButton) != 0) {
            switch (settings_.eraserEnd) {
                case Settings::EraserAction::Erase:
                    model_->say("(erase is not wired yet)");
                    break;
                case Settings::EraserAction::Undo:
                    if (document_.undoStack().canUndo()) {
                        document_.undoStack().undo();
                        model_->say("Undo");
                    } else {
                        model_->say("Nothing to undo");
                    }
                    break;
                case Settings::EraserAction::Nothing:
                    break;
            }
            return true;
        }
        if ((buttons & mbmRightButton) != 0) {
            const bool barrel = pen.barrel || pen.inRange;   // Windows reports the barrel button as a right click; a mouse is not in range
            if (barrel && settings_.barrelButton == Settings::PenButton::Pan) {
                panning_ = true;
            } else if (!barrel || settings_.barrelButton == Settings::PenButton::Rings) {
                if (!tools_.current()->inputDown(toolEvent(pt, buttons, keys, true))) {
                    openRing(true);
                }
            }
            return true;
        }
        if ((buttons & mbmLeftButton) != 0 && !model_->documentOpen()) {
            model_->say("Tap Space to start");
            return true;
        }
        if ((buttons & mbmLeftButton) != 0 && spaceDown_) {   // Space held, then a press and drag: pan, or with Ctrl zoom around the press
            spaceUsed_ = true;
            if ((keys & kmCtrl) != 0) {
                zooming_ = true;
                zoomFrom_ = pt;
                zoomStart_ = canvas_->zoom();
            } else {
                panning_ = true;
            }
            return true;
        }
        if ((buttons & mbmLeftButton) == 0) {
            return false;
        }
        return tools_.current()->inputDown(toolEvent(pt, buttons, keys, false));
    }

    ToolEvent WorkspaceController::toolEvent(const Point& pt, std::uint32_t buttons, std::uint32_t keys, bool secondary) const {
        ToolEvent event;
        event.view = pt;
        event.document = canvas_->toDocument(pt);
        event.buttons = buttons;
        event.keys = keys;
        event.secondary = secondary;
        const RootView* root = canvas_->rootView();
        event.pen = root != nullptr && root->penState().inRange;
        return event;
    }

    void WorkspaceController::makeTools() {
        ToolContext context{document_, graphView_->selectionModel(), *canvas_, settings_};
        context.say = [this](const std::string& text) { model_->say(text); };
        context.saveSettings = [this]() { saveSettings(); };
        context.openRing = [this]() { openRing(false); };
        context.showSelection = [this]() { ensureDockShown(DockSide::Right); };
        context.documentOpen = [this]() { return model_->documentOpen(); };
        context.graphShown = [this]() { return graphOverlay_->isVisible(); };
        context.menuOpen = [this]() { return menu_->isOpen(); };
        selectTool_ = static_cast<SelectTool*>(&tools_.add(std::make_unique<SelectTool>(context)));
        optionsCard_ = std::make_unique<ToolOptionsCard>(*canvas_);
        optionsCard_->onDismissed = [this]() { focusCanvas(); };
    }

    bool WorkspaceController::showToolOptions(const std::string& toolId, const Point& at) {
        Tool* tool = tools_.find(toolId);
        if (tool == nullptr || optionsCard_ == nullptr) {
            return false;
        }
        return optionsCard_->show(*tool, at);
    }

    SyncReturn WorkspaceController::handleMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (panning_) {
            if ((buttons & (mbmRightButton | mbmLeftButton)) != 0) {
                canvas_->panBy(pt.x - pointer_.x, pt.y - pointer_.y);
            } else {
                panning_ = false;
            }
        }
        if (zooming_) {
            if ((buttons & mbmLeftButton) != 0) {
                // Up or right zooms in, down or left out; 200 pixels is one doubling, and the point pressed stays where it is.
                const float travel = (pt.x - zoomFrom_.x) + (zoomFrom_.y - pt.y);
                canvas_->zoomAt(zoomFrom_, zoomStart_ * std::pow(2.0f, travel / 200.0f));
            } else {
                zooming_ = false;
            }
        }
        if (tools_.current() != nullptr && !panning_ && !zooming_) {
            tools_.current()->inputMove(toolEvent(pt, buttons, keys, false));
        }
        pointer_ = pt;
        pointerKnown_ = true;
        if (menu_->isOpen()) {
            menu_->onMouseMove(*menu_, pt, buttons, keys);
        }
        return SyncReturn::Handled;
    }

    SyncReturn WorkspaceController::handleMouseUp(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        panning_ = false;
        zooming_ = false;
        if (tools_.current() != nullptr) {
            tools_.current()->inputUp(toolEvent(pt, buttons, keys, (buttons & mbmRightButton) != 0));
        }
        if (menu_->isOpen()) {
            menu_->onMouseUp(*menu_, pt, buttons, keys);
        }
        return SyncReturn::Handled;
    }

    // ---- Keys ----

    std::string WorkspaceController::keyName(std::uint32_t vk) {
        if (vk >= vkLetterA && vk <= vkLetterZ) {
            return std::string(1, char('A' + (vk - vkLetterA)));
        }
        if (vk >= vkNumber0 && vk <= vkNumber9) {
            return std::string(1, char('0' + (vk - vkNumber0)));
        }
        return std::string();
    }

    void WorkspaceController::registerHistoryActions() {
        struct Binding {
            VirtualKeyCode key;
            std::uint32_t mask;
            bool forward;
        };
        const Binding bindings[] = {{vkLetterZ, kmCtrl, false}, {vkLetterY, kmCtrl, true}, {vkLetterZ, kmCtrl | kmShift, true}};
        for (std::size_t i = 0; i < historyActions_.size(); ++i) {
            Action& action = historyActions_[i];
            action.text = bindings[i].forward ? "Redo" : "Undo";
            action.setHotkey(bindings[i].key, bindings[i].mask);
            action.onActionPerformed.add([this, forward = bindings[i].forward](Action&) {
                // The hotkey is taken before any view sees the key, so a multi-line editor with the focus undoes its own text here (and
                // the document is left alone). Single-line panel fields keep the document's undo: they commit to the document as they change.
                if (auto* editor = dynamic_cast<TextControl*>(frame_.rootView().focusedSubView())) {
                    if (forward) {
                        editor->redo();
                    } else {
                        editor->undo();
                    }
                    return SyncReturn::Handled;
                }
                stepHistory(forward);
                return SyncReturn::Handled;
            });
            loop_->registerAction(&action);
        }
    }

    bool WorkspaceController::stepHistory(bool forward) {
        newui::UndoStack& stack = document_.undoStack();
        if (!(forward ? stack.canRedo() : stack.canUndo())) {
            model_->say(forward ? "Nothing to redo" : "Nothing to undo");
            return false;
        }
        const std::string what = forward ? stack.redoDescription() : stack.undoDescription();
        std::set<NodeRef> instancesBefore;
        if (assetEvents_) {
            instancesBefore = AssetEvents::instancesOf(document_);
        }
        if (forward) {
            stack.redo();
        } else {
            stack.undo();
        }
        std::string eventErrors;
        if (assetEvents_) {
            // Instances this step brought back (the undo of a delete, or a redo of an add). A script that edits the document makes a new step,
            // which ends the redo history, so this runs inside one merged step only when something was asked of the document.
            const std::size_t mark = stack.depth();
            eventErrors = assetEvents_->fireChanges(document_, &graphView_->selectionModel());   // wiring and parameters the step put back
            const std::string created = assetEvents_->fireCreatedForNew(document_, &graphView_->selectionModel(), instancesBefore, "undo");
            eventErrors += (eventErrors.empty() || created.empty() ? "" : "\n") + created;
            stack.mergeSince(mark, "Asset events");
        }
        // The panel is not a view of the document: it shows the selected node again (a node an undo removed shows as an empty panel).
        nodePanel_->show(graphView_->selected());
        transformPanel_->refresh();
        selectPanel_->refresh();
        graphView_->style().markDirty();
        canvas_->style().markDirty();
        model_->say(eventErrors.empty() ? (forward ? "Redo " : "Undo ") + what : eventErrors);
        return true;
    }

    bool WorkspaceController::filterKey(std::uint32_t mask, std::uint32_t vk) {
        if ((mask & kmCtrl) != 0 && (mask & kmAlt) == 0 && (mask & kmShift) == 0 && vk == vkLetterA && !menu_->isOpen() && model_->documentOpen()) {
            selectTool_->selectAll();   // the canvas has the focus when this filter runs, so a text field keeps its own Ctrl+A
            return true;
        }
        if ((mask & kmAlt) != 0 || ((mask & kmCtrl) != 0 && vk != vkSpaceBar)) {   // Ctrl+Space is the zoom drag
            return false;
        }
        const bool shift = (mask & kmShift) != 0;
        if (busy_ && vk == vkEscape && !menu_->isOpen()) {
            if (scriptConsole_ != nullptr && scriptConsole_->running()) {
                scriptConsole_->cancel();
            } else {
                task_.cancel();
            }
            return true;
        }
        if (menu_->isOpen()) {
            if (vk == vkEscape || vk == vkSpaceBar) {
                menu_->handleEscape();
                model_->setRing(std::string());
                model_->say("Ring closed");
                focusCanvas();
            } else {
                menu_->handleKey(keyName(vk), shift);
            }
            return true;
        }
        if (vk == vkEscape && optionsCard_ != nullptr && optionsCard_->isOpen()) {
            optionsCard_->close();
            return true;
        }
        ToolKeyEvent keyEvent;
        keyEvent.vk = vk;
        keyEvent.mask = mask;
        if (tools_.current()->keyDown(keyEvent)) {
            return true;
        }
        if (recentColorKey(vk, shift)) {
            return true;
        }
        if (vk == vkTab && !graphOverlay_->isVisible() && selectTool_->cycle(shift ? -1 : 1)) {
            return true;
        }
        if (vk == vkTab && !shift) {
            toggleGraph();
            return true;
        }
        if (vk == vkSpaceBar) {
            if (!spaceDown_) {
                spaceDown_ = true;
                spaceUsed_ = (mask & kmCtrl) != 0;   // Ctrl+Space is not a tap for the rings
                spaceStart_ = std::chrono::steady_clock::now();
                model_->setSpaceDown(true);
            }
            return true;
        }
        if (spaceDown_) {
            spaceUsed_ = true;
        }
        return false;
    }

    SyncReturn WorkspaceController::handleKeyUp(View&, std::uint32_t, int, int, std::uint32_t vk) {
        ToolKeyEvent keyEvent;
        keyEvent.vk = vk;
        tools_.current()->keyUp(keyEvent);
        if (vk == vkSpaceBar && spaceDown_) {
            spaceDown_ = false;
            model_->setSpaceDown(false);
            if (!spaceUsed_ && std::chrono::steady_clock::now() - spaceStart_ < std::chrono::milliseconds(settings_.spaceTapMs) && !menu_->isOpen()) {
                openRing(false);
            }
        }
        return SyncReturn::Handled;
    }

    // ---- Painting over the canvas ----

    void WorkspaceController::paintTop(BLContext& ctx) const {
        if (model_->documentOpen()) {
            const SelectionModel& selection = graphView_->selectionModel();
            const jitnodes::GeometryHandle artwork = selection.empty() ? nullptr : document_.artwork();
            if (artwork != nullptr) {
                const BLPoint origin = canvas_->toView(BLPoint(0.0, 0.0));
                ctx.set_stroke_width(2.0);
                for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
                    if (!shape.key.empty() && selection.isShapeKeySelected(std::string(shape.key))) {
                        ctx.stroke_path(shapePath(*artwork, shape, origin.x, origin.y, double(canvas_->zoom())), rgb(Theme::current().accent));
                    }
                }
            }
            if (tools_.current() != nullptr) {
                tools_.current()->paintOverlay(ctx);
            }
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(13.0f);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        const Rect client = canvas_->getClientBounds();
        const std::string text = "Tap Space to start";
        ctx.set_fill_style(rgb(Theme::current().textMuted));
        ctx.fill_utf8_text(BLPoint(double(client.width() - font.measureText(text).width) * 0.5, double(client.height()) * 0.5), *blFont, text.c_str(), text.size());
    }

    // ---- Assets ----

    std::filesystem::path WorkspaceController::userAssetsFolder() const {
        const std::string settingsFile = settingsPath_.empty() ? Settings::defaultPath() : settingsPath_;
        return std::filesystem::u8path(settingsFile).parent_path() / "assets";
    }

    void WorkspaceController::createAssetPanel() {
        AssetManagerHooks hooks;
        hooks.network = [this]() { return &document_.network(); };
        hooks.addInstance = [this](const std::string& type) { return addAssetInstance(type); };
        hooks.updateInstances = [this](const std::string& type, std::size_t& count) { return updateAssetInstances(type, count); };
        hooks.unlockSelected = [this]() { return unlockSelectedInstance(); };
        assetModel_ = std::make_shared<AssetManagerModel>(hooks);

        std::string firstError;
        for (const std::filesystem::path& folder : {executableDirectory() / "Resources" / "assets", userAssetsFolder()}) {
            const std::string error = assetModel_->installFolder(folder.u8string());
            if (firstError.empty()) {
                firstError = error;
            }
        }
        assetModel_->refresh();

        assetPanel_ = new AssetManagerPanel(assetModel_);
        rightDock_->addPanel(assetPanel_);
        assetPanel_->onMessage = [this](const std::string& message) { model_->say(message); };
        assetPanel_->onDestroyed = [this]() { assetPanel_ = nullptr; };
        assetPanel_->onNewRequested = [this]() { newAsset(); };
        assetPanel_->onEditRequested = [this]() { editAsset(); };
        assetPanel_->presentMenu = [this](const std::vector<PanelMenuEntry>& entries, const Point& screen) {
            PanelsMenu::popup(frame_.frameHandle(), entries, screen);
        };
        assetPanel_->onSaveToLibraryRequested = [this]() {
            const std::string name = assetModel_->selected();
            if (name.empty()) {
                return;
            }
            std::error_code ignored;
            std::filesystem::create_directories(userAssetsFolder(), ignored);
            FileDialogOptions options;
            options.title = "Save asset to the library";
            options.filters.push_back({"Asset files", "*.asset.json"});
            options.defaultFileName = name + ".asset.json";
            options.initialDirectory = userAssetsFolder().u8string();
            std::string file;
            if (Dialog::showSaveFile(frame_.frameHandle(), options, file) && !file.empty()) {
                const std::string error = assetModel_->saveToLibrary(file);
                model_->say(error.empty() ? "Saved " + name + " to " + file : error);
            }
        };
        assetPanel_->onLoadFileRequested = [this]() {
            FileDialogOptions options;
            options.title = "Load asset";
            options.filters.push_back({"Asset files", "*.asset.json"});
            options.filters.push_back({"All Files", "*.*"});
            std::string file;
            if (Dialog::showOpenFile(frame_.frameHandle(), options, file) && !file.empty()) {
                const std::string error = assetModel_->installFile(file);
                model_->say(error.empty() ? "Loaded " + file : error);
            }
        };
        assetPanel_->onLoadFolderRequested = [this]() {
            FileDialogOptions options;
            options.title = "Load assets from a folder";
            std::string folder;
            if (Dialog::showBrowseForFolder(frame_.frameHandle(), options, folder) && !folder.empty()) {
                std::size_t loaded = 0;
                const std::string error = assetModel_->installFolder(folder, &loaded);
                model_->say(error.empty() ? "Loaded " + std::to_string(loaded) + " asset(s) from " + folder : error);
            }
        };
        assetPanel_->onCollapsedChanged += [this](Panel&) {
            if (assetModel_ != nullptr) {
                assetModel_->refresh();   // what was skipped while it was collapsed
            }
            return SyncReturn::Handled;
        };
        // The instance counts follow the graph.
        document_.onGraphChanged += [this](GraphDocument&) {
            if (assetModel_ != nullptr && assetPanel_ != nullptr && !assetPanel_->collapsed()) {
                assetModel_->refresh();
            }
            return SyncReturn::Handled;
        };
        if (!firstError.empty()) {
            model_->say(firstError);
        }
    }

    std::string WorkspaceController::addAssetInstance(const std::string& type) {
        try {
            const std::size_t mark = document_.undoStack().depth();
            const NodeRef selected = graphView_->selectionModel().primary();
            const bool wire = selected.valid() && document_.hasNode(selected);
            const NodeRef added = document_.addGraphNode(type, std::string(), wire ? selected.scope : 0u);
            if (wire && document_.node(added).inputCount() > 0) {
                document_.connectGraphNodes(selected, added, 0);
            }
            std::string eventError;
            if (assetEvents_) {
                eventError = assetEvents_->fireCreated(document_, &graphView_->selectionModel(), added, "added");
            }
            document_.undoStack().mergeSince(mark, "Add " + type);
            if (!eventError.empty()) {
                model_->say(eventError);
            }
            graphView_->selectionModel().set(added);
            graphView_->style().markDirty();
            canvas_->style().markDirty();
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    void WorkspaceController::reportMigrationProblems() {
        const std::string text = migrationNotes();
        if (!text.empty()) {
            model_->say(text);
        }
    }

    std::string WorkspaceController::migrationNotes() {
        if (!assetEvents_) {
            return {};
        }
        std::string text = assetEvents_->takeMigrationProblems();
        // The load could not ask, so ask now: an allowed asset runs its script the next time an instance is brought up to date.
        bool allowedNow = false;
        for (const std::string& asset : assetEvents_->takeUntrustedAssets()) {
            allowedNow = assetEvents_->askTrust(asset) || allowedNow;
        }
        if (allowedNow) {
            text += (text.empty() ? "" : "\n") + std::string("Allowed: its migration script runs the next time instances are brought up to date.");
        }
        return text;
    }

    void WorkspaceController::wirePythonNodeTrust() {
        pythonNodeTrust_ = std::make_unique<PythonNodeTrust>();
        PythonNodeTrust* trust = pythonNodeTrust_.get();
        trust->setTrustFile(userAssetsFolder().parent_path() / "trustedPythonNodes.txt");
        trust->setPrompt([this](std::size_t count) {
            const DialogResult answer = Dialog::showMessageBox(frame_.frameHandle(),
                std::to_string(count) + (count == 1 ? " Python node in this document has" : " Python nodes in this document have") +
                    " code that has not been allowed yet. The code can do anything a program can do. Only allow it when you trust where it came from.\n\nAllow it to run?",
                "Run Python node code?", MessageBoxButtons::YesNo, MessageBoxIcon::Warning);
            return answer == DialogResult::Yes;
        });
        // A cook refuses on a worker thread; the question is asked from the run loop, and the views recook once it is allowed.
        trust->setRefusedNotice([this, alive = alive_, loop = loop_]() {
            loop->post([this, alive]() {
                if (!*alive || !pythonNodeTrust_) {
                    return;
                }
                if (pythonNodeTrust_->askPending()) {
                    graphView_->style().markDirty();
                    canvas_->style().markDirty();
                } else {
                    model_->say("Python nodes with code that is not allowed stay disabled");
                }
            });
        });
        jitnodes::python::setPythonNodePolicy([trust](const jitnodes::Node& node) { return trust->allows(node.getString("code")); });
        // A node runs while the canvas draws, on the main thread: Esc (in this application) stops a runaway one, as it stops a script.
        jitnodes::python::setPythonNodeTimeLimit(std::chrono::seconds(settings_.pythonNodeSeconds));
        jitnodes::python::setPythonNodeInterruptCheck([]() {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0) {
                return false;
            }
            DWORD owner = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &owner);
            return owner == GetCurrentProcessId();
        });
    }

    void WorkspaceController::wireAssetEvents() {
        const auto idle = [this]() { return assetEvents_ && scriptConsole_ != nullptr && !scriptConsole_->running(); };
        document_.afterEdit = [this, idle](const std::string& description) {
            if (!idle()) {
                return;
            }
            newui::UndoStack& stack = document_.undoStack();
            const std::size_t mark = stack.depth() - 1;   // the edit's own step: what the scripts change joins it
            const std::string errors = assetEvents_->fireChanges(document_, &graphView_->selectionModel());
            if (stack.depth() > mark + 1) {
                stack.mergeSince(mark, description);
            }
            if (!errors.empty()) {
                model_->say(errors);
            }
        };
        document_.beforeRemove = [this, idle](const std::vector<NodeRef>& nodes) {
            deleteMark_.reset();
            deleteErrors_.clear();
            if (!idle()) {
                return;
            }
            deleteMark_ = document_.undoStack().depth();
            deleteErrors_ = assetEvents_->fireDeletedFor(document_, &graphView_->selectionModel(), nodes);
        };
        document_.afterRemove = [this]() {
            if (deleteMark_ && document_.undoStack().depth() > *deleteMark_ + 1) {
                document_.undoStack().mergeSince(*deleteMark_, "Delete");   // the scripts' steps and the delete are one
            }
            deleteMark_.reset();
            if (!deleteErrors_.empty()) {
                model_->say(deleteErrors_);
                deleteErrors_.clear();
            }
        };
        document_.onTimeMoved = [this, idle]() {
            if (!idle()) {
                return;   // the previous call is still running
            }
            const std::size_t mark = document_.undoStack().depth();
            const int frame = int(std::lround(document_.time() * document_.network().fps()));
            const std::string errors = assetEvents_->fireFrameChanged(document_, &graphView_->selectionModel(), frame, false);
            document_.undoStack().mergeSince(mark, "Asset events");
            if (!errors.empty()) {
                model_->say(errors);
            }
        };
    }

    void WorkspaceController::flushInstalledEvents() {
        installFlushQueued_ = false;
        if (!assetEvents_ || scriptConsole_ == nullptr) {
            return;
        }
        if (scriptConsole_->running()) {
            return;   // asset changes made by a script are not announced to the assets' own scripts
        }
        std::vector<std::string> fresh;
        for (const jitnodes::AssetEntry& entry : jitnodes::AssetLibrary::instance().list()) {
            const auto seen = installedSeen_.find(entry.name);
            if (seen == installedSeen_.end() || seen->second != entry.version) {
                installedSeen_[entry.name] = entry.version;
                fresh.push_back(entry.name);
            }
        }
        if (!model_->documentOpen()) {
            return;
        }
        std::string errors;
        const std::size_t mark = document_.undoStack().depth();
        for (const std::string& name : fresh) {
            const std::string error = assetEvents_->fireInstalled(document_, &graphView_->selectionModel(), name);
            if (!error.empty()) {
                errors += (errors.empty() ? "" : "\n") + error;
            }
        }
        document_.undoStack().mergeSince(mark, "Asset events");
        if (!errors.empty()) {
            model_->say(errors);
        }
    }

    void WorkspaceController::fireLoadedEvents() {
        if (!assetEvents_ || scriptConsole_ == nullptr) {
            return;
        }
        if (scriptConsole_->running()) {
            // A script opened the document and is still running: the events wait for it to end (the interpreter is busy until then).
            loop_->postDelayed(std::chrono::milliseconds(100), [this]() {
                fireLoadedEvents();
                return true;   // true ends the timer (see tickTask)
            });
            return;
        }
        const std::size_t mark = document_.undoStack().depth();
        const std::string errors = assetEvents_->fireCreatedInDocument(document_, &graphView_->selectionModel(), "loaded");
        document_.undoStack().mergeSince(mark, "Asset events");
        assetEvents_->remember(document_);
        if (!errors.empty()) {
            model_->say(errors);
        }
    }

    std::string WorkspaceController::updateAssetInstances(const std::string& type, std::size_t& count) {
        count = 0;
        try {
            const jitnodes::GraphSnapshot before = document_.snapshotGraph();
            count = jitnodes::updateAssetInstances(document_.network(), type);
            if (count > 0) {
                document_.commitGraphEdit(before, "Update assets");
                graphView_->style().markDirty();
                canvas_->style().markDirty();
            }
        } catch (const std::exception& e) {
            return e.what();
        }
        const std::string notes = migrationNotes();
        return notes.empty() ? std::string() : "Updated the instances, but:\n" + notes;
    }

    std::string WorkspaceController::unlockSelectedInstance() {
        const NodeRef selected = graphView_->selectionModel().primary();
        if (!selected.valid() || !document_.hasNode(selected)) {
            return "Select an asset instance in the graph first";
        }
        try {
            jitnodes::Network& network = document_.networkOf(selected.scope);
            if (!network.node(selected.id).isAssetInstance()) {
                return "The selected node is not an asset instance";
            }
            const jitnodes::GraphSnapshot before = document_.snapshotGraph();
            jitnodes::unlockAssetInstance(network, selected.id);
            document_.commitGraphEdit(before, "Unlock asset");
            graphView_->style().markDirty();
            canvas_->style().markDirty();
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    void WorkspaceController::replaceSubnetWithInstance(const NodeRef& subnet, const std::string& type) {
        jitnodes::Network& network = document_.networkOf(subnet.scope);
        const jitnodes::GraphSnapshot before = document_.snapshotGraph();
        const jitnodes::Node& old = network.node(subnet.id);
        std::unique_ptr<jitnodes::Node> fresh = jitnodes::NodeRegistry::instance().create(type);
        for (const std::string& name : old.floatNames()) {
            if (!fresh->hasFloat(name)) {
                continue;
            }
            fresh->setFloat(name, old.baseFloat(name));
            if (!old.expression(name).empty()) {
                fresh->setExpression(name, old.expression(name));
            } else if (!old.keyframes(name).empty()) {
                fresh->setKeyframes(name, old.keyframes(name));
            }
        }
        std::vector<jitnodes::NodeId> inputs;
        for (int i = 0; i < old.inputCount(); ++i) {
            inputs.push_back(network.inputOf(subnet.id, i));
        }
        inputs.resize(std::size_t(fresh->inputCount()), jitnodes::invalidNodeId);
        const bool placed = old.hasPosition();
        const float x = old.posX();
        const float y = old.posY();
        network.replaceNode(subnet.id, std::move(fresh), &inputs);
        if (placed) {
            network.node(subnet.id).setPosition(x, y);
        }
        document_.commitGraphEdit(before, "Create asset " + type);
        graphView_->style().markDirty();
        canvas_->style().markDirty();
    }

    void WorkspaceController::newAsset() {
        const AssetPropertiesSession::NetworkProvider network = [this]() { return &document_.network(); };
        const NodeRef selected = graphView_->selectionModel().primary();
        if (selected.valid() && document_.hasNode(selected)) {
            jitnodes::Node& node = document_.node(selected);
            if (node.childNetwork() != nullptr && !node.isAssetInstance()) {
                std::string name = node.name();
                for (char& c : name) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                        c = '_';
                    }
                }
                if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) {
                    name = "asset" + name;
                }
                try {
                    AssetPropertiesSession session = AssetPropertiesSession::fromSubnet(document_.networkOf(selected.scope), selected.id, name, network);
                    openAssetProperties(session, true);
                } catch (const std::exception& e) {
                    model_->say(e.what());
                }
                return;
            }
        }
        AssetPropertiesSession session = AssetPropertiesSession::blank(network);
        openAssetProperties(session, false);
    }

    void WorkspaceController::editAsset() {
        const std::string name = assetModel_ != nullptr ? assetModel_->selected() : std::string();
        if (name.empty()) {
            model_->say("Select an asset first");
            return;
        }
        try {
            AssetPropertiesSession session = AssetPropertiesSession::forAsset(name, [this]() { return &document_.network(); });
            openAssetProperties(session, false);
        } catch (const std::exception& e) {
            model_->say(e.what());
        }
    }

    void WorkspaceController::openAssetProperties(AssetPropertiesSession& session, bool replaceSubnet) {
        const NodeRef subnet = replaceSubnet ? graphView_->selectionModel().primary() : NodeRef();
        assetpropsdialog::Callbacks callbacks;
        callbacks.chooseSavePath = [this](const std::string& suggestedName) {
            std::error_code ignored;
            std::filesystem::create_directories(userAssetsFolder(), ignored);
            FileDialogOptions options;
            options.title = "Save asset";
            options.filters.push_back({"Asset files", "*.asset.json"});
            options.defaultFileName = suggestedName;
            options.initialDirectory = userAssetsFolder().u8string();
            std::string file;
            return Dialog::showSaveFile(frame_.frameHandle(), options, file) ? file : std::string();
        };
        callbacks.matchSubnet = [this](AssetPropertiesSession& state) -> std::string {
            const NodeRef selected = graphView_->selectionModel().primary();
            if (!selected.valid() || !document_.hasNode(selected)) {
                return "Select a subnet in the graph first";
            }
            return state.matchSubnet(document_.networkOf(selected.scope), selected.id);
        };
        callbacks.editContents = [this](const std::string& name) {
            const std::string error = addAssetInstance(name);
            return error.empty() ? unlockSelectedInstance() : error;
        };
        callbacks.chooseFile = [this](std::string& name, std::string& text) {
            FileDialogOptions options;
            options.title = "Add a file to the asset";
            std::string file;
            if (!Dialog::showOpenFile(frame_.frameHandle(), options, file) || file.empty()) {
                return false;
            }
            std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            name = std::filesystem::u8path(file).filename().string();
            return true;
        };
        callbacks.changed = [this]() {
            assetModel_->refresh();
            model_->say("Asset library updated");
        };
        const bool saved = AssetPropertiesDialog::run(frame_, session, callbacks);
        assetModel_->refresh();
        if (saved && replaceSubnet && subnet.valid() && document_.hasNode(subnet) && session.kind() == AssetPropertiesSession::Kind::asset) {
            try {
                replaceSubnetWithInstance(subnet, session.working().name);
                assetModel_->refresh();
            } catch (const std::exception& e) {
                model_->say(e.what());
            }
        }
    }

}
