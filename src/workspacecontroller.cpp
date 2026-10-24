#include "curvy/workspacecontroller.h"
#include "curvy/theme.h"
#include "curvy/dockcontroller.h"
#include "curvy/settingsdialog.h"
#include "curvy/sketchsvg.h"
#include "curvy/welcomeoverlay.h"

#include <newui/application.h>
#include <newui/bundle.h>
#include <newui/dialogs.h>
#include <newui/font.h>
#include <newui/runloop.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>
#include <newui/themedata.h>
#include <newui/uicolormanager.h>
#include <newui/uiinputmanager.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace curvy {

    namespace {
        std::filesystem::path recoveryFolder() {
            const char* base = std::getenv("APPDATA");
            return std::filesystem::path(base != nullptr ? base : ".") / "curvy" / "autosave";
        }
    }

    using namespace newui;

    namespace {

        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(Theme::argb(value)); }
        BLRgba32 card() { return BLRgba32(Theme::argb(Theme::current().bgSurface, 0xF2)); }

        template <typename T>
        T* find(Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }

        RadialRing newRing() {
            return {"New", "Create...", "dialog.new", {
                {"Letter", "1", "new.letter"}, {"A4", "2", "new.a4"}, {"Full HD", "3", "new.fullhd"}, {"Square", "4", "new.square"},
                {"Open", "O", "file.open"}, {"Settings", "S", "dialog.settings"}}};
        }
        RadialRing createRing() {
            return {"Create", "Shape options...", "dialog.shape", {
                {"Rectangle", "R", "tool.rect"}, {"Ellipse", "E", "tool.ellipse"}, {"Polygon", "P", "tool.polygon", "options.polygon"}, {"Star", "S", "tool.star", "options.star"},
                {"Line", "L", "tool.line"}, {"Freehand", "B", "tool.pencil", "options.pencil"}, {"Text", "T", "tool.text"}, {"Select", "V", "tool.select"}}};
        }
        RadialRing documentRing() {
            return {"Document", "Setup...", "dialog.setup", {
                {"New artboard", "A", "doc.newArtboard"}, {"New document", "N", "ring.new"}, {"Open", "O", "file.open"}, {"Save", "S", "file.save"},
                {"Export", "E", "file.exportImage"}, {"Place image", "I", "file.placeImage"}, {"View", "V", "ring.view"}}};
        }
        RadialRing viewRing() {
            return {"View", "Back", "ring.document", {
                {"Toolbar", "T", "view.toolbar"}, {"Properties", "P", "view.properties"}, {"Rulers", "G", "view.toggleRulers"}, {"Fit page", "0", "view.fitPage"},
                {"Theme", "L", "view.theme"}, {"Settings", "S", "dialog.settings"}}};
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
            double width, height;
        };
        const PageSize kPages[] = {
            {"new.letter", "Letter", 612, 792}, {"new.a4", "A4", 595, 842}, {"new.fullhd", "Full HD", 1920, 1080}, {"new.square", "Square 1080", 1080, 1080}};

        struct ToolCommand {
            const char* command;
            SketchTool tool;
        };
        const ToolCommand kToolCommands[] = {
            {"tool.select", SketchTool::Select}, {"tool.rect", SketchTool::Rectangle}, {"tool.ellipse", SketchTool::Ellipse}, {"tool.polygon", SketchTool::Polygon}, {"tool.star", SketchTool::Star},
            {"tool.line", SketchTool::Line}, {"tool.pencil", SketchTool::Pencil}, {"tool.text", SketchTool::Text}};

        // The keys held to draw: R E P S L.
        struct HoldKey {
            const char* key;
            SketchTool tool;
        };
        const HoldKey kHoldKeys[] = {
            {"R", SketchTool::Rectangle}, {"E", SketchTool::Ellipse}, {"P", SketchTool::Polygon}, {"S", SketchTool::Star}, {"L", SketchTool::Line}};

        const char* toolName(SketchTool tool) {
            switch (tool) {
                case SketchTool::Select: return "Select";
                case SketchTool::Rectangle: return "Rectangle";
                case SketchTool::Ellipse: return "Ellipse";
                case SketchTool::Polygon: return "Polygon";
                case SketchTool::Star: return "Star";
                case SketchTool::Line: return "Line";
                case SketchTool::Pencil: return "Freehand";
                case SketchTool::Text: return "Text";
                case SketchTool::Hand: return "Hand";
                case SketchTool::Gradient: return "Gradient";
                default: return "Tool";
            }
        }
    }

    void WorkspaceController::applyTheme(bool dark) {
        // The same route a host takes (cpp_codetools' ThemeChanged): the mode, then the roles the .theme file does not carry. The roles
        // it does carry, and the control parts, come from Resources/Themes/{dark,light}.theme (tools/makeThemes.py).
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
        restyle();
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
        text(find<Label>(frame_, "appLogo"), theme.accent);
        text(find<Label>(frame_, "appName"), theme.textPrimary);
        text(documentName_, theme.textMuted);
        text(hintTool_, theme.highlight);
        text(hintKeys_, theme.textMuted);
        text(hintInfo_, theme.textMuted);
        canvas_->setPasteboardColor(Theme::argb(settings_.canvasFollowsTheme ? theme.bgCanvas : settings_.canvasBackground));
        if (dock_ != nullptr) {
            dock_->restyle();
        }
        if (options_ != nullptr) {
            options_->restyle();
        }
        frame_.rootView().refreshThemes();
        frame_.rootView().style().markDirty();
        canvas_->style().markDirty();
    }

    WorkspaceController::WorkspaceController(Frame& frame, SketchDocument& document) : frame_(frame), document_(document) {
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

        canvas_->setDocument(&document_);
        canvas_->setShowGrid(false);
        restyle();
        files_ = std::make_unique<SketchDocumentController>(document_, frame_);
        commands_ = std::make_unique<SketchCommandController>(document_, *canvas_, *files_);

        menu_ = new RadialMenu();
        canvas_->addChild(menu_);
        bindViews();
        dock_ = std::make_unique<DockController>(frame_, *canvas_, *model_, document_);
        options_ = std::make_unique<ToolOptionsCard>(*canvas_, *canvas_);
        dock_->onToolOptions = [this](SketchTool tool, const Point& at) { showToolOptions(tool, at); };
        dock_->onCardDismissed = [this] { focusCanvas(); };
        if (StylePanel* style = dock_->stylePanel()) {
            // A gradient fill is edited on the shape: the type buttons put the canvas on the Gradient tool, and the stop the panel and the canvas have
            // selected stays the same one.
            style->onGradientChosen += [this](StylePanel&) {
                armTool(SketchTool::Gradient, true, std::string());
                model_->say("Drag the line or a stop on the shape; click the line to add a stop");
                return SyncReturn::Handled;
            };
            style->onStopSelected += [this](StylePanel& panel) {
                canvas_->setSelectedGradientStop(panel.selectedStop());
                return SyncReturn::Handled;
            };
            canvas_->onGradientStopSelected += [this](CanvasView& view) {
                dock_->stylePanel()->setSelectedStop(view.selectedGradientStop());
                return SyncReturn::Handled;
            };
        }
        options_->onDismissed = [this] { focusCanvas(); };
        // Esc closes a card even while one of its controls has the keyboard focus (the canvas's own key filter would never see it then).
        frame_.rootView().onKeyDown += [this](View&, std::uint32_t, int, int, std::uint32_t vk) {
            const bool cardUp = (dock_ != nullptr && dock_->card() != DockController::Card::None) || (options_ != nullptr && options_->isOpen());
            if (vk == vkEscape && cardUp && !menu_->isOpen()) {
                if (dock_ != nullptr) {
                    dock_->closeCard();
                }
                options_->close();
                focusCanvas();
                return SyncReturn::Handled;
            }
            return SyncReturn::Ignored;
        };
        canvas_->reorderChild(menu_, canvas_->childViews().size());   // the ring stays above the docks and cards
    }

    void WorkspaceController::bindViews() {
        canvas_->onSizeChanged += [this](View&, const Size& size) {
            menu_->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
            return SyncReturn::Handled;
        };
        menu_->setBounds(Rect(0.0f, 0.0f, canvas_->bounds().size().width, canvas_->bounds().size().height));
        menu_->onCommand.add(this, &WorkspaceController::handleRingCommand);

        document_.onModifiedChanged += [this](Document&) {
            if (!document_.isModified()) {
                clearRecovery();
            }
            return SyncReturn::Handled;
        };
        frame_.onClosed += [this](Frame&) {
            clearRecovery();
            return SyncReturn::Handled;
        };
        // Closing the window (X, Alt+F4) asks to save a modified document; Cancel keeps it open.
        frame_.onClosing += [this](Frame&, bool& canClose) {
            canClose = files_->canClose();
            return SyncReturn::Handled;
        };
        canvas_->onPressFilter = [this](const Point& pt, std::uint32_t buttons, std::uint32_t keys) { return filterPress(pt, buttons, keys); };
        canvas_->onKeyFilter = [this](std::uint32_t mask, std::uint32_t vk) { return filterKey(mask, vk); };
        canvas_->onToolDefaultsChanged += [this](CanvasView&) { toolDefaultsChanged(); return SyncReturn::Handled; };
        for (const char* name : {"topBar", "toolbarToggle", "propertiesToggle", "hintBar"}) {
            if (View* chrome = frame_.rootView().findView(name)) {
                chrome->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                    dismissWelcome();   // a press anywhere outside the canvas closes the first-run card too
                    return SyncReturn::Ignored;
                };
            }
        }
        canvas_->onPaintTop = [this](BLContext& ctx) { paintTop(ctx); };
        canvas_->onKeyUp.add(this, &WorkspaceController::handleKeyUp);
        canvas_->onMouseMove.add(this, &WorkspaceController::handleMouseMove);
        canvas_->onMouseUp.add(this, &WorkspaceController::handleMouseUp);
        canvas_->onToolChanged.add(this, &WorkspaceController::handleToolChanged);
        document_.onSelectionChanged.add(this, &WorkspaceController::handleSelectionChanged);
        files_->onDocumentReplaced.add(this, &WorkspaceController::handleDocumentReplaced);
    }

    // The autosave timer is not cancelled here: the run loop has already ended and kills its timers itself.
    WorkspaceController::~WorkspaceController() = default;

    void WorkspaceController::startAutosave() {
        autosaveTimer_ = Application::instance().runLoop().postDelayed(std::chrono::seconds(30), [this] {
            autosaveTick();
            return false;
        });
    }

    // The copy is only needed after a crash, so it goes as soon as the document is saved, replaced or the window closes.
    void WorkspaceController::clearRecovery() {
        if (!recoveryFile_.empty()) {
            std::error_code ignored;
            std::filesystem::remove(std::filesystem::u8path(recoveryFile_), ignored);
            recoveryFile_.clear();
        }
    }

    bool WorkspaceController::offerRecovery() {
        std::error_code ignored;
        std::vector<std::filesystem::directory_entry> found;
        for (const auto& entry : std::filesystem::directory_iterator(recoveryFolder(), ignored)) {
            if (entry.is_regular_file(ignored) && entry.path().extension() == ".svg") {
                found.push_back(entry);
            }
        }
        std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) { return a.last_write_time(ignored) > b.last_write_time(ignored); });
        for (const auto& entry : found) {
            const std::string name = entry.path().stem().string();
            const DialogResult answer = Dialog::showMessageBox(frame_.frameHandle(),
                "curvy closed before \"" + name + "\" was saved. Recover the autosaved copy?\n\nYes recovers it, No throws it away, Cancel asks again next time.",
                "curvy", MessageBoxButtons::YesNoCancel, MessageBoxIcon::Question);
            if (answer == DialogResult::Cancel) {
                return false;
            }
            if (answer == DialogResult::Yes && files_->recoverFrom(entry.path().string(), name)) {
                std::filesystem::remove(entry.path(), ignored);
                return true;
            }
            std::filesystem::remove(entry.path(), ignored);
        }
        return false;
    }

    void WorkspaceController::showWelcomeOnce() {
        if (settings_.welcomeShown || frame_.rootView().overlay() != nullptr) {
            return;
        }
        frame_.rootView().setOverlay(std::make_unique<WelcomeOverlay>());
        frame_.rootView().markDirty();
        settings_.welcomeShown = true;
        settings_.save(Settings::defaultPath());
    }

    void WorkspaceController::dismissWelcome() {
        if (frame_.rootView().overlay() != nullptr) {
            frame_.rootView().setOverlay(nullptr);
            frame_.rootView().markDirty();
            focusCanvas();
        }
    }

    // A recovery copy beside the settings; the user's own file is only ever written by Save.
    void WorkspaceController::autosaveTick() {
        const auto now = std::chrono::steady_clock::now();
        if (!settings_.autosave || !model_->documentOpen() || !document_.isModified() ||
            now - lastAutosave_ < std::chrono::minutes(settings_.autosaveMinutes)) {
            return;
        }
        lastAutosave_ = now;
        const std::filesystem::path folder = recoveryFolder();
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        const std::string stem = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).stem().string() : files_->untitledName();
        const std::filesystem::path file = folder / (stem + ".svg");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (out << svgFromDocument(document_)) {
            recoveryFile_ = file.string();
            model_->say("Autosaved " + stem);
        }
    }

    void WorkspaceController::openSettings() {
        Settings edited = settings_;
        edited.toolbarVisible = model_->toolbarVisible();   // these three show how things are now, whatever was saved
        edited.propertiesVisible = model_->propertiesVisible();
        edited.rulersVisible = canvas_->showRulers();
        const Settings before = edited;
        if (SettingsDialog::run(frame_, edited, [this](const Settings& live) { applySettings(live); },
                            [this] { model_->say("Settings cancelled"); })) {
            applySettings(edited);
            if (edited.toolbarVisible != before.toolbarVisible) {
                model_->setToolbarVisible(edited.toolbarVisible);
            }
            if (edited.propertiesVisible != before.propertiesVisible) {
                model_->setPropertiesVisible(edited.propertiesVisible);
            }
            if (edited.rulersVisible != before.rulersVisible) {
                canvas_->setShowRulers(edited.rulersVisible);
            }
            if (edited.toolDefaultsInDocument && model_->documentOpen() && files_->document().toolDefaults().polygonSides <= 0) {
                toolDefaultsChanged();   // switching to the document starts it off with the values in use
            }
            edited.save(Settings::defaultPath());
            showWelcomeOnce();   // when the Welcome button asked for it
            model_->say("Settings saved");
        }
        focusCanvas();
    }

    void WorkspaceController::applySettings(const Settings& settings) {
        const bool themeChanged = settings.darkTheme() != Theme::current().isDark;
        settings_ = settings;
        model_->setScrubAxis(settings_.scrubAxis);
        model_->setHintsFade(settings_.hintsFade);
        if (dock_ != nullptr) {
            dock_->setPropertyTextSize(settings_.propertyTextSize);
        }
        canvas_->setSnapToGrid(settings_.snapGrid);
        canvas_->setGridSpacing(float(settings_.gridSize));
        canvas_->setSmartGuides(settings_.snapGuides);
        if (files_ != nullptr) {
            files_->setMaxRecent(std::size_t(settings_.recentLength));
            files_->setExportFolder(settings_.exportFolder);
        }
        applyToolDefaults();
        if (themeChanged) {
            setDarkTheme(settings_.darkTheme());
        } else {
            restyle();
        }
    }

    // Polygon sides and star points: the document's when the user keeps tool defaults there and it has them, else the per-user ones.
    void WorkspaceController::applyToolDefaults() {
        int sides = settings_.polygonSides;
        int points = settings_.starPoints;
        if (settings_.toolDefaultsInDocument && files_ != nullptr && model_->documentOpen()) {
            const SketchDocument::ToolDefaults& stored = files_->document().toolDefaults();
            sides = stored.polygonSides > 0 ? stored.polygonSides : sides;
            points = stored.starPoints > 0 ? stored.starPoints : points;
        }
        applyingToolDefaults_ = true;
        canvas_->setPolygonSides(sides);
        canvas_->setStarPoints(points);
        canvas_->setCornerRadius(double(settings_.cornerRadius));
        canvas_->setStarInnerPercent(settings_.starInner);
        canvas_->setPencilSmoothing(settings_.pencilSmoothing);
        applyingToolDefaults_ = false;
    }

    void WorkspaceController::toolDefaultsChanged() {
        if (applyingToolDefaults_) {
            return;
        }
        if (settings_.toolDefaultsInDocument && files_ != nullptr && model_->documentOpen()) {
            files_->document().setToolDefaults({canvas_->polygonSides(), canvas_->starPointCount()});
        } else {
            settings_.polygonSides = canvas_->polygonSides();
            settings_.starPoints = canvas_->starPointCount();
        }
        settings_.cornerRadius = int(std::lround(canvas_->cornerRadius()));
        settings_.starInner = canvas_->starInnerPercent();
        settings_.pencilSmoothing = canvas_->pencilSmoothing();
        settings_.save(Settings::defaultPath());
    }

    void WorkspaceController::start(const std::string& path) {
        applySettings(Settings::load(Settings::defaultPath()));
        model_->setToolbarVisible(settings_.toolbarVisible);
        model_->setPropertiesVisible(settings_.propertiesVisible);
        model_->setDocument(false, std::string());
        canvas_->setShowPages(false);
        canvas_->setShowRulers(false);
        if (!path.empty()) {
            files_->open(path);
        } else {
            frame_.setTitle("curvy");
        }
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
        // A label sizes itself when the bar is laid out, so new text needs a layout pass or it is clipped to the old width
        // (the document name started empty and showed as a sliver beside the title).
        std::vector<View*> bars;   // the bars whose layout has to run again
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
            if (bar != nullptr) {
                bar->updateLayout();
            }
        }
        return SyncReturn::Handled;
    }

    // ---- Rings ----

    void WorkspaceController::openRing(bool byPress) {
        const Rect client = canvas_->getClientBounds();
        const Point at = pointerKnown_ ? pointer_ : Point(client.width() * 0.5f, client.height() * 0.5f);
        const BLPoint world = canvas_->toDocument(at);
        RadialRing ring;
        if (!model_->documentOpen()) {
            ring = newRing();
        } else if (SketchObject* hit = document_.hitTest(world, 4.0 / double(canvas_->zoom()))) {
            document_.setSelection({hit->id()});
            ring = objectRing();
        } else if (std::any_of(document_.artboards().begin(), document_.artboards().end(), [&](const Artboard& board) { return board.contains(world); })) {
            ring = createRing();
        } else {
            ring = documentRing();
        }
        model_->setHover(WorkspaceModel::Hover{});
        model_->used("ring");
        if (byPress) {
            model_->used("rightClick");
        }
        menu_->open(ring, at, byPress);
        model_->setRing(ring.title);
    }

    SyncReturn WorkspaceController::handleRingCommand(RadialMenu&, const std::string& command) {
        model_->setRing(std::string());
        runCommand(command);
        focusCanvas();
        return SyncReturn::Handled;
    }

    void WorkspaceController::showToolOptions(SketchTool tool, const Point& at) {
        if (options_ == nullptr || !ToolOptionsCard::hasOptions(tool)) {
            return;
        }
        if (dock_ != nullptr) {
            dock_->closeCard();
        }
        options_->show(tool, at);
        model_->used("toolOptions");
        model_->say("Defaults for the next shape; Esc closes");
    }

    void WorkspaceController::armTool(SketchTool tool, bool sticky, const std::string& heldKey) {
        toolSticky_ = sticky;
        heldToolKey_ = heldKey;
        canvas_->setTool(tool);
        model_->setTool(toolName(tool), heldKey);
    }

    void WorkspaceController::runCommand(const std::string& command) {
        const Rect client = canvas_->getClientBounds();
        const Point at = pointerKnown_ ? pointer_ : Point(client.width() * 0.5f, client.height() * 0.5f);
        for (const PageSize& page : kPages) {
            if (command == page.command) {
                if (files_->newDocument(page.name, page.width, page.height)) {
                    model_->say(std::string("New ") + page.name);
                }
                return;
            }
        }
        for (const ToolCommand& tool : kToolCommands) {
            if (command == tool.command) {
                armTool(tool.tool, true, std::string());
                model_->say(tool.tool == SketchTool::Select ? std::string("Select") : std::string(toolName(tool.tool)) + " tool armed");
                return;
            }
        }
        if (command == "ring.document") { menu_->open(documentRing(), at, false); model_->setRing("Document"); }
        else if (command == "ring.new") { menu_->open(newRing(), at, false); model_->setRing("New"); }
        else if (command == "ring.object") { menu_->open(objectRing(), at, false); model_->setRing("Object"); }
        else if (command == "ring.view") { menu_->open(viewRing(), at, false); model_->setRing("View"); }
        else if (command == "view.toolbar") { model_->setToolbarVisible(!model_->toolbarVisible()); model_->say(model_->toolbarVisible() ? "Toolbar on" : "Toolbar off"); }
        else if (command == "view.properties") { model_->setPropertiesVisible(!model_->propertiesVisible()); model_->say(model_->propertiesVisible() ? "Properties on" : "Properties off"); }
        else if (command == "view.theme") { setDarkTheme(!Theme::current().isDark); model_->say(Theme::current().isDark ? "Dark theme" : "Light theme"); }
        else if (command == "ring.arrange") { menu_->open(arrangeRing(), at, false); model_->setRing("Arrange"); }
        else if (command == "doc.newArtboard") {
            const BLPoint world = canvas_->toDocument(at);
            Artboard board;
            board.x = world.x;
            board.y = world.y;
            board.width = 400.0;
            board.height = 300.0;
            document_.addArtboard(board);
            model_->say("New artboard at the pen");
        }
        else if (SketchTool optionsTool = SketchTool::Select; ToolOptionsCard::toolFromCommand(command, optionsTool)) {
            showToolOptions(optionsTool, Point(at.x + 28.0f, at.y - 40.0f));
        }
        else if (command == "card.color" || command == "card.stroke") {
            if (options_ != nullptr) {
                options_->close();
            }
            if (dock_ != nullptr) {
                Rect selected;
                bool haveSelected = false;
                for (std::size_t id : document_.selection()) {
                    if (const SketchObject* object = document_.findById(id)) {
                        const BLBox box = object->bounds();
                        const BLPoint a = canvas_->toView(BLPoint(box.x0, box.y0)), b = canvas_->toView(BLPoint(box.x1, box.y1));
                        const Rect one(float(std::min(a.x, b.x)), float(std::min(a.y, b.y)), float(std::abs(b.x - a.x)), float(std::abs(b.y - a.y)));
                        selected = haveSelected ? selected.united(one) : one;
                        haveSelected = true;
                    }
                }
                dock_->showCard(command == "card.color" ? DockController::Card::Fill : DockController::Card::Stroke, at, haveSelected ? &selected : nullptr);
                if (command == "card.color" && dock_->stylePanel() != nullptr) {
                    // An open path has nothing to fill, so its colour is the stroke's.
                    const SketchObject* shape = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
                    const bool strokeOnly = shape != nullptr && (shape->fill().isNone() || !shape->isClosed());
                    dock_->stylePanel()->setTarget(strokeOnly ? StylePanel::Target::Stroke : StylePanel::Target::Fill);
                }
                model_->used("appearance");
            }
        }
        else if (command == "dialog.new") { files_->newDocument(); }
        else if (command == "dialog.settings") { openSettings(); }
        else if (command == "dialog.setup") { commands_->run("file.documentSetup"); }
        else if (command.compare(0, 7, "dialog.") == 0) { model_->say("The " + command.substr(7) + " dialog is not built yet"); }
        else if (commands_->has(command)) {
            if (command == "edit.duplicate") { model_->used("duplicate"); }
            if (command == "edit.delete") { model_->used("delete"); }
            commands_->run(command);
        }
        else { model_->say("(" + command + " is not wired yet)"); }
    }

    // ---- Document state ----

    SyncReturn WorkspaceController::handleDocumentReplaced(SketchDocumentController& controller) {
        model_->setDocument(true, controller.displayName());
        canvas_->setShowPages(true);
        canvas_->setShowRulers(settings_.rulersVisible);
        applyToolDefaults();
        frame_.setTitle("curvy - " + controller.displayName());
        canvas_->zoomToFit();
        return SyncReturn::Handled;
    }

    SyncReturn WorkspaceController::handleSelectionChanged(SketchDocument&) {
        model_->setSelectionCount(document_.selection().size());
        return SyncReturn::Handled;
    }

    SyncReturn WorkspaceController::handleToolChanged(CanvasView&) {
        model_->setTool(toolName(canvas_->tool()), heldToolKey_);
        if (dock_ != nullptr) {
            dock_->syncTool();
        }
        return SyncReturn::Handled;
    }

    // ---- Pen and mouse ----

    bool WorkspaceController::filterPress(const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        pointer_ = pt;
        pointerKnown_ = true;
        shiftDown_ = (keys & kmShift) != 0;
        dismissWelcome();
        // The Fill card stays up while the Gradient tool works the shape under it.
        const bool gradientAtWork = dock_ != nullptr && dock_->card() == DockController::Card::Fill && canvas_->tool() == SketchTool::Gradient;
        if (dock_ != nullptr && dock_->card() != DockController::Card::None && !dock_->cardContains(pt) && !gradientAtWork) {
            dock_->closeCard();   // a press anywhere else puts the card away and carries on as usual
        }
        if (options_ != nullptr && options_->isOpen() && !options_->contains(pt)) {
            options_->close();
        }
        const PenState& pen = frame_.rootView().penState();
        if (pen.eraser && (buttons & mbmLeftButton) != 0) {
            if (model_->documentOpen() && !menu_->isOpen()) {
                if (settings_.eraserEnd == Settings::EraserAction::Erase) {
                    if (SketchObject* hit = document_.hitTest(canvas_->toDocument(pt), 4.0 / double(canvas_->zoom()))) {
                        document_.setSelection({hit->id()});
                        model_->used("delete");
                        commands_->run("edit.delete");
                    }
                } else if (settings_.eraserEnd == Settings::EraserAction::Undo) {
                    commands_->run("edit.undo");
                }
            }
            return true;
        }
        if ((buttons & mbmRightButton) != 0) {
            if (pen.barrel && !menu_->isOpen() && settings_.barrelButton != Settings::PenButton::Rings) {
                if (settings_.barrelButton == Settings::PenButton::Pan && model_->documentOpen()) {
                    panning_ = true;
                    panFrom_ = pt;
                    panStartX_ = canvas_->panX();
                    panStartY_ = canvas_->panY();
                    model_->used("pan");
                }
                return true;
            }
            openRing(true);
            return true;
        }
        if ((buttons & mbmLeftButton) == 0) {
            return false;
        }
        if (!model_->documentOpen()) {
            model_->say("Tap Space to start");
            return true;
        }
        if (spaceDown_) {
            spaceUsed_ = true;
            panning_ = true;
            panFrom_ = pt;
            panStartX_ = canvas_->panX();
            panStartY_ = canvas_->panY();
            model_->used("pan");
            return true;
        }
        leftDown_ = true;
        return false;
    }

    SyncReturn WorkspaceController::handleMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        pointer_ = pt;
        pointerKnown_ = true;
        shiftDown_ = (keys & kmShift) != 0;
        if (menu_->isOpen()) {
            menu_->onMouseMove(*menu_, pt, buttons, keys);
            return SyncReturn::Handled;
        }
        if (panning_) {
            canvas_->setPan(panStartX_ + (pt.x - panFrom_.x), panStartY_ + (pt.y - panFrom_.y));
        }
        const BLPoint world = canvas_->toDocument(pt);
        model_->setPointer(true, world.x, world.y, double(canvas_->zoom()) * 100.0);
        if (scrub_.active()) {
            const double value = scrub_.update(document_, scrubTravel(pt), shiftDown_);
            model_->setScrub(true, scrub_.part(), &scrub_.prop(), value, model_->scrubAnchorX(), model_->scrubAnchorY());
        } else {
            updateHover(pt);
        }
        return SyncReturn::Handled;
    }

    SyncReturn WorkspaceController::handleMouseUp(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (menu_->isOpen()) {
            menu_->onMouseUp(*menu_, pt, buttons, keys);
        }
        panning_ = false;
        if (leftDown_) {
            leftDown_ = false;
            if (!toolSticky_ && heldToolKey_.empty() && canvas_->tool() != SketchTool::Select) {
                canvas_->setTool(SketchTool::Select);
            }
        }
        return SyncReturn::Handled;
    }

    void WorkspaceController::updateHover(const Point& pt) {
        WorkspaceModel::Hover hover;
        if (model_->documentOpen() && canvas_->tool() == SketchTool::Select && !leftDown_ && !panning_ && !spaceDown_) {
            const double tolerance = 6.0 / double(canvas_->zoom());
            const BLPoint world = canvas_->toDocument(pt);
            if (const SketchObject* object = document_.hitTest(world, tolerance)) {
                hover.object = object->id();
                hover.part = HoldScrub::partAt(*object, world, tolerance);
            }
        }
        model_->setHover(hover);
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

    bool WorkspaceController::filterKey(std::uint32_t mask, std::uint32_t vk) {
        if (frame_.rootView().overlay() != nullptr) {
            dismissWelcome();   // any key closes it; Space then carries on as usual and opens the ring
            if (vk == vkEscape) {
                return true;
            }
        }
        if ((mask & (kmCtrl | kmAlt)) != 0) {
            return false;
        }
        shiftDown_ = (mask & kmShift) != 0;
        if (scrub_.active()) {
            if (vk == vkEscape) {
                endScrub(false);
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
                menu_->handleKey(keyName(vk), shiftDown_);
            }
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
        if (spaceDown_) {
            spaceUsed_ = true;
        }
        if (!model_->documentOpen()) {
            return false;
        }
        if (vk == vkEscape) {
            if (dock_ != nullptr && dock_->card() != DockController::Card::None) {
                dock_->closeCard();
                return true;
            }
            if (options_ != nullptr && options_->isOpen()) {
                options_->close();
                return true;
            }
            armTool(SketchTool::Select, false, std::string());
            model_->say("Select");
            return false;   // the canvas also clears the selection
        }
        const std::string key = keyName(vk);
        if (key.empty() || leftDown_) {
            return false;
        }
        if (key == heldToolKey_) {
            return true;   // auto-repeat of the key being held
        }
        if (key == "V") {
            armTool(SketchTool::Select, false, std::string());
            model_->say("Select");
            return true;
        }
        if (canvas_->tool() == SketchTool::Select && model_->hover().part != ScrubPart::None && startScrub(key[0])) {
            return true;
        }
        for (const HoldKey& hold : kHoldKeys) {
            if (key == hold.key) {
                armTool(hold.tool, false, key);
                return true;
            }
        }
        return false;
    }

    SyncReturn WorkspaceController::handleKeyUp(View&, std::uint32_t mask, int, int, std::uint32_t vk) {
        shiftDown_ = (mask & kmShift) != 0;
        const std::string key = keyName(vk);
        if (scrub_.active()) {
            if (key == scrubKey_) {
                endScrub(true);
            }
            return SyncReturn::Handled;
        }
        if (vk == vkSpaceBar && spaceDown_) {
            spaceDown_ = false;
            panning_ = false;
            model_->setSpaceDown(false);
            if (!spaceUsed_ && std::chrono::steady_clock::now() - spaceStart_ < std::chrono::milliseconds(settings_.spaceTapMs) && !menu_->isOpen()) {
                openRing(false);
            }
            return SyncReturn::Handled;
        }
        if (!key.empty() && key == heldToolKey_) {
            heldToolKey_.clear();
            if (!leftDown_ && !toolSticky_) {
                canvas_->setTool(SketchTool::Select);
            }
            model_->setTool(toolName(canvas_->tool()), std::string());
        }
        return SyncReturn::Handled;
    }

    // ---- Scrubbing ----

    double WorkspaceController::scrubTravel(const Point& pt) const {
        const double sensitivity = double(settings_.scrubSensitivity) / 100.0;
        return sensitivity * (model_->scrubAxis() == WorkspaceModel::ScrubAxis::Vertical ? -double(pt.y) : double(pt.x));
    }

    bool WorkspaceController::startScrub(char key) {
        const WorkspaceModel::Hover hover = model_->hover();
        if (!scrub_.begin(document_, hover.object, hover.part, key, scrubTravel(pointer_))) {
            return false;
        }
        scrubKey_ = std::string(1, key);
        // Hidden while the card is up; put back where it was so the pointer is not left wherever the scrub ended.
        POINT cursor{};
        cursorHidden_ = ::GetCursorPos(&cursor) != FALSE;
        cursorBeforeX_ = cursor.x;
        cursorBeforeY_ = cursor.y;
        if (cursorHidden_) {
            ::ShowCursor(FALSE);
        }
        model_->setScrub(true, hover.part, &scrub_.prop(), scrub_.value(), double(pointer_.x), double(pointer_.y));
        return true;
    }

    void WorkspaceController::endScrub(bool keep) {
        scrub_.finish(document_, keep);
        if (cursorHidden_) {
            ::SetCursorPos(cursorBeforeX_, cursorBeforeY_);
            ::ShowCursor(TRUE);
            cursorHidden_ = false;
        }
        model_->used("scrub." + scrubKey_);
        model_->setScrub(false, ScrubPart::None, nullptr, 0.0, 0.0, 0.0);
        model_->say(keep ? "Kept" : "Cancelled");
        scrubKey_.clear();
    }

    // ---- Painting over the canvas ----

    void WorkspaceController::paintTop(BLContext& ctx) const {
        if (!model_->documentOpen()) {
            Font font = FontManager::getSystemFont(SystemUIFont::Message);
            font.setSize(13.0f);
            BLFont* blFont = font.blFont();
            const Rect client = canvas_->getClientBounds();
            if (blFont != nullptr && blFont->is_valid()) {
                const std::string text = "Tap Space to start";
                ctx.set_fill_style(rgb(Theme::current().textMuted));
                ctx.fill_utf8_text(BLPoint(double(client.width() - font.measureText(text).width) * 0.5, double(client.height()) * 0.5), *blFont, text.c_str(), text.size());
            }
            return;
        }
        if (model_->scrubbing()) {
            paintScrubCard(ctx);
        } else if (model_->hover().part != ScrubPart::None && !menu_->isOpen() && (dock_ == nullptr || dock_->card() == DockController::Card::None) && (options_ == nullptr || !options_->isOpen())) {
            paintHoverChip(ctx);
        }
    }

    void WorkspaceController::paintScrubCard(BLContext& ctx) const {
        const ScrubProp* prop = model_->scrubProp();
        if (prop == nullptr) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(12.0f);
        font.setBold(true);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        const Rect client = canvas_->getClientBounds();
        const double w = 250.0, h = 58.0;
        const double ax = model_->scrubAnchorX(), ay = model_->scrubAnchorY();
        const double x = std::clamp(ax - w * 0.5, 8.0, std::max(8.0, double(client.width()) - w - 8.0));
        double y = ay - 24.0 - h;
        if (y < 28.0) {
            y = ay + 24.0;
        }
        ctx.set_fill_style(card());
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, 10.0));
        ctx.set_stroke_style(rgb(Theme::current().border));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, 10.0));

        const std::string title = std::string(model_->scrubPart() == ScrubPart::Stroke ? "Outline - " : "Fill - ") + prop->name;
        const std::string value = HoldScrub::format(*prop, model_->scrubValue());
        ctx.set_fill_style(rgb(Theme::current().textPrimary));
        ctx.fill_utf8_text(BLPoint(x + 12.0, y + 22.0), *blFont, title.c_str(), title.size());
        ctx.set_fill_style(rgb(Theme::current().highlight));
        ctx.fill_utf8_text(BLPoint(x + w - 12.0 - double(font.measureText(value).width), y + 22.0), *blFont, value.c_str(), value.size());

        const double fraction = std::clamp((model_->scrubValue() - prop->min) / (prop->max - prop->min), 0.0, 1.0);
        const double tx = x + 16.0, tw = w - 32.0, ty = y + h - 18.0;
        ctx.set_fill_style(rgb(Theme::current().slot));
        ctx.fill_round_rect(BLRoundRect(tx, ty, tw, 6.0, 3.0));
        ctx.set_fill_style(rgb(Theme::current().highlight));
        ctx.fill_round_rect(BLRoundRect(tx, ty, std::max(6.0, tw * fraction), 6.0, 3.0));
        ctx.set_fill_style(rgb(Theme::current().bgSurface));
        ctx.fill_circle(BLCircle(tx + tw * fraction, ty + 3.0, 9.0));
        ctx.set_fill_style(rgb(Theme::current().highlight));
        ctx.fill_circle(BLCircle(tx + tw * fraction, ty + 3.0, 7.0));
    }

    void WorkspaceController::paintHoverChip(BLContext& ctx) const {
        const WorkspaceModel::Hover& hover = model_->hover();
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(12.0f);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid() || !pointerKnown_) {
            return;
        }
        std::string text = hover.part == ScrubPart::Stroke ? "Outline" : "Fill";
        for (const ScrubProp& prop : HoldScrub::props(hover.part)) {
            text += std::string("   ") + prop.key + " " + prop.name;
        }
        const Rect client = canvas_->getClientBounds();
        const double w = double(font.measureText(text).width) + 20.0, h = 28.0;
        double x = double(pointer_.x) - 24.0 - w;
        double y = double(pointer_.y) - 24.0 - h;
        if (x < 24.0) { x = double(pointer_.x) + 24.0; }
        if (y < 24.0) { y = double(pointer_.y) + 24.0; }
        x = std::min(x, double(client.width()) - w - 4.0);
        ctx.set_fill_style(card());
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, 8.0));
        ctx.set_stroke_style(rgb(Theme::current().border));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, 8.0));
        ctx.set_fill_style(rgb(Theme::current().textPrimary));
        ctx.fill_utf8_text(BLPoint(x + 10.0, y + 18.0), *blFont, text.c_str(), text.size());
    }

}
