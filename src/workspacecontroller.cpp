#include "curvy/workspacecontroller.h"
#include "curvy/settingsdialog.h"
#include "curvy/theme.h"

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

#include <algorithm>
#include <cmath>
#include <filesystem>
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
                {"Line", "L", "tool.line"}, {"Freehand", "B", "tool.pencil", "options.pencil"}, {"Text", "T", "tool.text"}, {"Select", "V", "tool.select"}}};
        }
        RadialRing documentRing() {
            return {"Document", "Setup...", "dialog.setup", {
                {"New artboard", "A", "doc.newArtboard"}, {"New document", "N", "ring.new"}, {"Open", "O", "file.open"}, {"Save", "S", "file.save"},
                {"Export", "E", "ring.export"}, {"Import", "I", "ring.import"}, {"View", "V", "ring.view"}}};
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
            {"ring.import", importRing}, {"ring.view", viewRing}, {"ring.object", objectRing}, {"ring.arrange", arrangeRing}};
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
        if (placed.known()) {
            settings_.window = placed;
            saveSettings();
        }
    }

    void WorkspaceController::restyle() {
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

    void WorkspaceController::frameShouldClose(bool& canClose) {
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
        canvas_->addChild(menu_);
        docks_ = std::make_unique<DockController>(frame_, settings_, [this]() { saveSettings(); });
        bindViews();
        start();
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
            graphPanel_->show(graphView_->selected());
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

        graphPanel_ = std::make_unique<GraphPanel>(*graphOverlay_, document_);
        graphPanel_->onCommand = [this](const std::string& command) {
            runGraphCommand(command);
            focusGraph();
        };
        graphPanel_->onRefused = [this](const std::string& message) { model_->say(message); };
        graphPanel_->onEdited =[this]() { graphView_->style().markDirty(); };

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
        if (auto* data = dynamic_cast<Button*>(graphOverlay_->findView("graphDataButton"))) {
            data->onClick += [this](Control&) {
                showPointTable();
                return SyncReturn::Handled;
            };
        }
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
        if (!shift && vk >= vkNumber1 && vk <= vkNumber8 && graphPanel_->colorPickerOpen()) {
            const std::size_t index = vk - vkNumber1;
            if (graphPanel_->useRecentColor(index)) {
                graphView_->style().markDirty();
                canvas_->style().markDirty();
                model_->say("Recent colour " + std::to_string(index + 1));
            } else {
                model_->say("No recent colour " + std::to_string(index + 1));
            }
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
        graphPanel_->closeColorPicker();
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
                graphPanel_->focusName();
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
        graphPanel_->show(graphView_->selected());
        graphView_->style().markDirty();
        canvas_->style().markDirty();
        return true;
    }

    void WorkspaceController::showPointTable() {
        if (!graphOverlay_->isVisible()) {
            return;
        }
        if (!graphView_->selected().valid()) {
            model_->say("Select a node to see its data");
            return;
        }
        if (!pointFrame_->isVisible()) {
            const Rect screen = graphOverlay_->localToScreen(graphOverlay_->getClientBounds());
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
        for (Action& action : historyActions_) {
            loop_->unregisterAction(&action);
        }
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
        canvas_->onKeyFilter = [this](std::uint32_t mask, std::uint32_t vk) { return filterKey(mask, vk); };
        canvas_->onPaintTop = [this](BLContext& ctx) { paintTop(ctx); };
        canvas_->onKeyUp.add(this, &WorkspaceController::handleKeyUp);
        canvas_->onMouseMove.add(this, &WorkspaceController::handleMouseMove);
        canvas_->onMouseUp.add(this, &WorkspaceController::handleMouseUp);

        // A button that was clicked keeps the keyboard focus and would take the next Space, which belongs to the canvas.
        struct Toggle {
            const char* button;
            DockSide side;
        };
        for (const Toggle& toggle : {Toggle{"toolbarToggle", DockSide::Left}, Toggle{"propertiesToggle", DockSide::Right}}) {
            if (auto* button = dynamic_cast<Button*>(frame_.rootView().findView(toggle.button))) {
                button->onClick += [this, side = toggle.side](Control&) {
                    toggleDock(side);
                    focusCanvas();
                    return SyncReturn::Handled;
                };
            }
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
        showRing(model_->documentOpen() ? documentRing() : newRing(), ringPoint(), byPress);
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

    // ---- Pen and mouse ----

    bool WorkspaceController::filterPress(const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        const RootView* root = canvas_->rootView();
        return pressed(root != nullptr ? root->penState() : PenState(), pt, buttons, keys);
    }

    bool WorkspaceController::pressed(const PenState& pen, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        pointer_ = pt;
        pointerKnown_ = true;
        panning_ = false;
        zooming_ = false;
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
                openRing(true);
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
        return false;
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
        if (forward) {
            stack.redo();
        } else {
            stack.undo();
        }
        // The panel is not a view of the document: it shows the selected node again (a node an undo removed shows as an empty panel).
        graphPanel_->show(graphView_->selected());
        graphView_->style().markDirty();
        canvas_->style().markDirty();
        model_->say((forward ? "Redo " : "Undo ") + what);
        return true;
    }

    bool WorkspaceController::filterKey(std::uint32_t mask, std::uint32_t vk) {
        if ((mask & kmAlt) != 0 || ((mask & kmCtrl) != 0 && vk != vkSpaceBar)) {   // Ctrl+Space is the zoom drag
            return false;
        }
        const bool shift = (mask & kmShift) != 0;
        if (busy_ && vk == vkEscape && !menu_->isOpen()) {
            task_.cancel();
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

}
