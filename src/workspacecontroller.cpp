#include "curvy/workspacecontroller.h"
#include "curvy/theme.h"
#include "curvy/dockcontroller.h"

#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>
#include <newui/themedata.h>
#include <newui/uicolormanager.h>
#include <newui/uiinputmanager.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr auto kTapLimit = std::chrono::milliseconds(300);

        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(Theme::argb(value)); }
        BLRgba32 card() { return BLRgba32(Theme::argb(Theme::current().bgSurface, 0xF2)); }

        template <typename T>
        T* find(Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }

        RadialRing newRing() {
            return {"New", "Create...", "dialog.new", {
                {"Letter", "1", "new.letter"}, {"A4", "2", "new.a4"}, {"Full HD", "3", "new.fullhd"}, {"Square", "4", "new.square"},
                {"Open", "O", "file.open"}}};
        }
        RadialRing createRing() {
            return {"Create", "Shape options...", "dialog.shape", {
                {"Rectangle", "R", "tool.rect"}, {"Ellipse", "E", "tool.ellipse"}, {"Polygon", "P", "tool.polygon"}, {"Star", "S", "tool.star"},
                {"Line", "L", "tool.line"}, {"Freehand", "B", "tool.pencil"}, {"Text", "T", "tool.text"}, {"File", "F", "ring.document"}}};
        }
        RadialRing documentRing() {
            return {"Document", "Setup...", "dialog.setup", {
                {"New artboard", "A", "doc.newArtboard"}, {"New document", "N", "ring.new"}, {"Open", "O", "file.open"}, {"Save", "S", "file.save"},
                {"Export", "E", "file.exportImage"}, {"Place image", "I", "file.placeImage"}, {"View", "V", "ring.view"}}};
        }
        RadialRing viewRing() {
            return {"View", "Back", "ring.document", {
                {"Toolbar", "T", "view.toolbar"}, {"Properties", "P", "view.properties"}, {"Rulers", "G", "view.toggleRulers"}, {"Fit page", "0", "view.fitPage"},
                {"Theme", "L", "view.theme"}}};
        }
        RadialRing objectRing() {
            return {"Object", "Properties...", "dialog.object", {
                {"Arrange", "1", "ring.arrange"}, {"Group", "2", "object.group"}, {"Duplicate", "D", "edit.duplicate"}, {"Delete", "X", "edit.delete"},
                {"Lock", "K", "object.lock"}, {"Flip", "F", "object.flipHorizontal"}}};
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
            {"tool.rect", SketchTool::Rectangle}, {"tool.ellipse", SketchTool::Ellipse}, {"tool.polygon", SketchTool::Polygon}, {"tool.star", SketchTool::Star},
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
        canvas_->setPasteboardColor(Theme::argb(theme.bgCanvas));
        if (dock_ != nullptr) {
            dock_->restyle();
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
    }

    void WorkspaceController::bindViews() {
        canvas_->onSizeChanged += [this](View&, const Size& size) {
            menu_->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
            return SyncReturn::Handled;
        };
        menu_->setBounds(Rect(0.0f, 0.0f, canvas_->bounds().size().width, canvas_->bounds().size().height));
        menu_->onCommand.add(this, &WorkspaceController::handleRingCommand);

        canvas_->onPressFilter = [this](const Point& pt, std::uint32_t buttons, std::uint32_t keys) { return filterPress(pt, buttons, keys); };
        canvas_->onKeyFilter = [this](std::uint32_t mask, std::uint32_t vk) { return filterKey(mask, vk); };
        canvas_->onPaintTop = [this](BLContext& ctx) { paintTop(ctx); };
        canvas_->onKeyUp.add(this, &WorkspaceController::handleKeyUp);
        canvas_->onMouseMove.add(this, &WorkspaceController::handleMouseMove);
        canvas_->onMouseUp.add(this, &WorkspaceController::handleMouseUp);
        canvas_->onToolChanged.add(this, &WorkspaceController::handleToolChanged);
        document_.onSelectionChanged.add(this, &WorkspaceController::handleSelectionChanged);
        files_->onDocumentReplaced.add(this, &WorkspaceController::handleDocumentReplaced);
    }

    WorkspaceController::~WorkspaceController() = default;

    void WorkspaceController::start(const std::string& path) {
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
        if (hintTool_ != nullptr) { hintTool_->setText(model_->hintTool()); }
        if (hintKeys_ != nullptr) { hintKeys_->setText(model_->hintKeys()); }
        if (hintInfo_ != nullptr) { hintInfo_->setText(model_->hintInfo()); }
        if (documentName_ != nullptr) { documentName_->setText(model_->documentName()); }
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
                model_->say(std::string(toolName(tool.tool)) + " tool armed");
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
        else if (command == "dialog.new") { files_->newDocument(); }
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
        canvas_->setShowRulers(true);
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
        if ((buttons & mbmRightButton) != 0) {
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
                menu_->handleKey(keyName(vk));
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
            if (!spaceUsed_ && std::chrono::steady_clock::now() - spaceStart_ < kTapLimit && !menu_->isOpen()) {
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
        return model_->scrubAxis() == WorkspaceModel::ScrubAxis::Vertical ? -double(pt.y) : double(pt.x);
    }

    bool WorkspaceController::startScrub(char key) {
        const WorkspaceModel::Hover hover = model_->hover();
        if (!scrub_.begin(document_, hover.object, hover.part, key, scrubTravel(pointer_))) {
            return false;
        }
        scrubKey_ = std::string(1, key);
        model_->setScrub(true, hover.part, &scrub_.prop(), scrub_.value(), double(pointer_.x), double(pointer_.y));
        return true;
    }

    void WorkspaceController::endScrub(bool keep) {
        scrub_.finish(document_, keep);
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
        } else if (model_->hover().part != ScrubPart::None && !menu_->isOpen()) {
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
