#include "curvy/sketchwindow.h"

#include <newui/dialogs.h>
#include <newui/keyboard_constants.h>

#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>

namespace curvy {

    namespace {

        // ---- The tables: which view asks for which command ----

        struct MenuBinding {
            const char* menu;      // "Edit", or "Object/Transform" for an item inside a submenu
            const char* item;      // the item's text
            const char* command;   // SketchCommandController id
        };
        const MenuBinding kMenuBindings[] = {
            {"File", "New", "file.new"},
            {"File", "Open...", "file.open"},
            {"File", "Save", "file.save"},
            {"File", "Save As...", "file.saveAs"},
            {"File", "Document Setup...", "file.documentSetup"},
            {"File", "Export As Image...", "file.exportImage"},
            {"File", "Export All Artboards...", "file.exportArtboards"},
            {"File", "Export As PDF...", "file.exportPdf"},
            {"File", "Trace Image...", "file.traceImage"},
            {"File", "Place Image...", "file.placeImage"},
            {"File", "Exit", "file.exit"},
            {"Edit", "Undo", "edit.undo"},
            {"Edit", "Redo", "edit.redo"},
            {"Edit", "Cut", "edit.cut"},
            {"Edit", "Copy", "edit.copy"},
            {"Edit", "Paste", "edit.paste"},
            {"Edit", "Paste In Place", "edit.pasteInPlace"},
            {"Edit", "Duplicate", "edit.duplicate"},
            {"Edit", "Delete", "edit.delete"},
            {"Edit", "Select All", "edit.selectAll"},
            {"Edit", "Deselect", "edit.deselect"},
            {"Edit/Select Same", "Fill Color", "edit.sameFill"},
            {"Edit/Select Same", "Stroke Color", "edit.sameStroke"},
            {"Edit/Select Same", "Stroke Weight", "edit.sameWeight"},
            {"Edit/Select Same", "Opacity", "edit.sameOpacity"},
            {"Edit/Select Same", "Blend Mode", "edit.sameBlend"},
            {"Edit/Select Same", "Object Type", "edit.sameType"},
            {"Object", "Group", "object.group"},
            {"Object", "Ungroup", "object.ungroup"},
            {"Object/Clipping Mask", "Make", "object.clipMake"},
            {"Object/Clipping Mask", "Release", "object.clipRelease"},
            {"Object/Symbol", "New Symbol", "symbol.new"},
            {"Object/Symbol", "Place Symbol", "symbol.place"},
            {"Object/Symbol", "Redefine Symbol", "symbol.redefine"},
            {"Object/Symbol", "Break Link to Symbol", "symbol.break"},
            {"Object/Brush", "New Calligraphic Brush", "brush.newCalligraphic"},
            {"Object/Brush", "New Art Brush", "brush.newArt"},
            {"Object/Brush", "New Scatter Brush", "brush.newScatter"},
            {"Object/Brush", "Apply Brush", "brush.apply"},
            {"Object/Brush", "Remove Brush", "brush.remove"},
            {"Object/Brush", "Add Brush Presets", "brush.presets"},
            {"Object/Text on Path", "Make Text on Path", "text.onPath"},
            {"Object/Text on Path", "Make Text Between Paths", "text.betweenPaths"},
            {"Object/Text on Path", "Release Text from Path", "text.release"},
            {"Object/Pattern", "Make Pattern", "pattern.make"},
            {"Object/Pattern", "Fill with Pattern", "pattern.fill"},
            {"Object/Pattern", "Stroke with Pattern", "pattern.stroke"},
            {"Object/Artboards", "New Artboard", "artboard.new"},
            {"Object/Artboards", "Delete Artboard", "artboard.delete"},
            {"Object/Artboards", "Previous Artboard", "artboard.previous"},
            {"Object/Artboards", "Next Artboard", "artboard.next"},
            {"Object/Artboards", "Fit Artboard in Window", "artboard.fit"},
            {"Object", "Lock", "object.lock"},
            {"Object", "Unlock All", "object.unlockAll"},
            {"Object", "Hide", "object.hide"},
            {"Object", "Show All", "object.showAll"},
            {"Object/Transform", "Transform Again", "object.transformAgain"},
            {"Object/Transform", "Rotate 90 CW", "object.rotate90cw"},
            {"Object/Transform", "Rotate 90 CCW", "object.rotate90ccw"},
            {"Object/Transform", "Flip Horizontal", "object.flipHorizontal"},
            {"Object/Transform", "Flip Vertical", "object.flipVertical"},
            {"Object/Transform", "Reset Bounding Box", "object.resetBoundingBox"},
            {"Object/Path", "Join", "path.join"},
            {"Object/Path", "Reverse Direction", "path.reverse"},
            {"Object/Path", "Simplify", "path.simplify"},
            {"Object/Path", "Outline Stroke", "path.outlineStroke"},
            {"Object", "Expand Appearance", "object.expandAppearance"},
            {"Object/Live Shape", "New Circle", "liveShape.circle"},
            {"Object/Pathfinder", "Unite", "pathfinder.unite"},
            {"Object/Pathfinder", "Minus Front", "pathfinder.minusFront"},
            {"Object/Pathfinder", "Intersect", "pathfinder.intersect"},
            {"Object/Pathfinder", "Exclude", "pathfinder.exclude"},
            {"Arrange", "Bring to Front", "arrange.bringToFront"},
            {"Arrange", "Bring Forward", "arrange.bringForward"},
            {"Arrange", "Send Backward", "arrange.sendBackward"},
            {"Arrange", "Send to Back", "arrange.sendToBack"},
            {"Arrange", "Align Left", "arrange.alignLeft"},
            {"Arrange", "Align Center", "arrange.alignCenter"},
            {"Arrange", "Align Right", "arrange.alignRight"},
            {"Arrange", "Align Top", "arrange.alignTop"},
            {"Arrange", "Align Middle", "arrange.alignMiddle"},
            {"Arrange", "Align Bottom", "arrange.alignBottom"},
            {"Arrange/Distribute", "Left Edges", "arrange.distributeLeft"},
            {"Arrange/Distribute", "Horizontal Centers", "arrange.distributeCenter"},
            {"Arrange/Distribute", "Right Edges", "arrange.distributeRight"},
            {"Arrange/Distribute", "Top Edges", "arrange.distributeTop"},
            {"Arrange/Distribute", "Vertical Centers", "arrange.distributeMiddle"},
            {"Arrange/Distribute", "Bottom Edges", "arrange.distributeBottom"},
            {"Arrange/Distribute", "Horizontal Spacing", "arrange.distributeSpaceH"},
            {"Arrange/Distribute", "Vertical Spacing", "arrange.distributeSpaceV"},
            {"Layer", "New Layer", "layer.new"},
            {"Layer", "New Sublayer", "layer.newSub"},
            {"Layer", "Duplicate Layer", "layer.duplicate"},
            {"Layer", "Delete Layer", "layer.delete"},
            {"Layer", "Bring Layer Forward", "layer.forward"},
            {"Layer", "Send Layer Backward", "layer.backward"},
            {"Layer", "Merge Layer Down", "layer.mergeDown"},
            {"Layer", "Flatten Artwork", "layer.flatten"},
            {"Layer", "Show All Layers", "layer.showAll"},
            {"Layer", "Hide Other Layers", "layer.hideOthers"},
            {"Layer", "Lock Other Layers", "layer.lockOthers"},
            {"Layer", "Unlock All Layers", "layer.unlockAll"},
            {"Layer", "Select Objects in Layer", "layer.selectObjects"},
            {"Layer", "Expand All Layers", "layer.expandAll"},
            {"Layer", "Collapse All Layers", "layer.collapseAll"},
            {"View", "Zoom In", "view.zoomIn"},
            {"View", "Zoom Out", "view.zoomOut"},
            {"View", "Fit Page", "view.fitPage"},
            {"View", "Actual Size", "view.actualSize"},
            {"View", "Show Grid", "view.toggleGrid"},
            {"View", "Show Rulers", "view.toggleRulers"},
            {"View", "Snap to Grid", "view.toggleSnap"},
            {"View", "Smart Guides", "view.toggleSmartGuides"},
            {"View", "Show Guides", "view.toggleGuides"},
            {"View", "Lock Guides", "view.lockGuides"},
            {"View", "Clear Guides", "view.clearGuides"},
            {"View/Color Mode", "RGB", "color.rgb"},
            {"View/Color Mode", "CMYK", "color.cmyk"},
        };

        struct ButtonBinding {
            const char* button;    // the toolbar button's name in sketch.newui
            const char* command;
        };
        const ButtonBinding kButtonBindings[] = {
            {"newButton", "file.new"},
            {"openButton", "file.open"},
            {"saveButton", "file.save"},
            {"undoButton", "edit.undo"},
            {"redoButton", "edit.redo"},
            {"deleteButton", "edit.delete"},
            {"groupButton", "object.group"},
            {"ungroupButton", "object.ungroup"},
            {"bringToFrontButton", "arrange.bringToFront"},
            {"sendToBackButton", "arrange.sendToBack"},
            {"alignLeftButton", "arrange.alignLeft"},
            {"alignCenterButton", "arrange.alignCenter"},
            {"alignRightButton", "arrange.alignRight"},
        };

        // One row per tool: its palette button, and the hint the status bar shows while it is the tool (so a new tool is one row).
        struct ToolInfo {
            const char* button;
            SketchTool tool;
            const char* hint;
        };
        const ToolInfo kTools[] = {
            {"selectTool", SketchTool::Select, "Select: click, Shift-click or drag a box; drag to move; handles resize (Alt = from center) and rotate"},
            {"directSelectTool", SketchTool::DirectSelect, "Direct Select: click an object to show its points; drag a point or a handle; Alt-drag a corner point to pull out curve handles; Delete removes the point"},
            {"rectangleTool", SketchTool::Rectangle, "Rectangle: drag to draw (Shift = square)"},
            {"ellipseTool", SketchTool::Ellipse, "Ellipse: drag to draw (Shift = circle)"},
            {"circleTool", SketchTool::Circle, "Circle: drag out from the center"},
            {"roundRectTool", SketchTool::RoundRect, "Rounded rectangle: drag to draw (Shift = square); Up / Down arrows change the corner radius"},
            {"polygonTool", SketchTool::Polygon, "Polygon: drag from the center (Shift = upright); Up / Down arrows change the number of sides"},
            {"starTool", SketchTool::Star, "Star: drag from the center (Shift = upright); Up / Down arrows change the number of points"},
            {"arcTool", SketchTool::Arc, "Arc: drag out from the center (Shift = start at the top); Up / Down arrows change the sweep; drag the handles on its ends to change the angles"},
            {"lineTool", SketchTool::Line, "Line: drag to draw (Shift = 45 degrees)"},
            {"penTool", SketchTool::Pen, "Pen: click for a corner point, drag for a smooth one; click the first point to close; Enter or double-click ends; Backspace removes the last point"},
            {"pencilTool", SketchTool::Pencil, "Pencil: drag to draw freehand; the line is smoothed when you let go"},
            {"paintbrushTool", SketchTool::Paintbrush, "Paintbrush: drag to draw freehand with the active brush (choose one in the Brushes panel; Object > Brush > Add Brush Presets adds a few)"},
            {"textTool", SketchTool::Text, "Text: click to type (or click text to edit it); Enter starts a new line; Ctrl+Up / Ctrl+Down change the size; Esc or a click elsewhere finishes"},
            {"gradientTool", SketchTool::Gradient, "Gradient: drag across the object to set the gradient; drag its handles to edit; click the axis to add a stop"},
            {"widthTool", SketchTool::Width, "Width: drag the path to add a width point, drag a handle to widen, drag the diamond to slide, Alt-click a point to delete"},
            {"scissorsTool", SketchTool::Scissors, "Scissors: click a path to cut it there (a closed path opens, an open one divides in two)"},
            {"knifeTool", SketchTool::Knife, "Knife: drag a line across filled shapes to cut them into pieces (all it crosses, or only the selection)"},
            {"eraserTool", SketchTool::Eraser, "Eraser: drag over shapes and lines to rub them out; Up / Down arrows change the size"},
            {"shapeBuilderTool", SketchTool::ShapeBuilder, "Shape Builder: select overlapping shapes, then drag across the pieces to join them into one shape (Alt-drag erases them)"},
            {"eyedropperTool", SketchTool::Eyedropper, "Eyedropper: click an object to copy its fill, stroke and effects onto the selection (with nothing else selected, onto what new shapes are drawn with)"},
            {"magicWandTool", SketchTool::MagicWand, "Magic Wand: click an object to select every object with the same fill (Shift adds to the selection)"},
            {"artboardTool", SketchTool::Artboard, "Artboard: drag to make an artboard, click one to select it, drag it or its handles to move / resize it (Delete removes it)"},
            {"rotateTool", SketchTool::Rotate, "Rotate: click to set the center, then drag to turn the selection about it (Shift = 15 degree steps, Alt = turn a copy)"},
            {"scaleTool", SketchTool::Scale, "Scale: click to set the center, then drag to resize the selection about it (Shift = each direction on its own, Alt = scale a copy)"},
            {"reflectTool", SketchTool::Reflect, "Reflect: click to set a point on the axis, then drag to turn the axis and mirror the selection across it (Shift = 45 degree steps, Alt = mirror a copy)"},
            {"lassoTool", SketchTool::Lasso, "Lasso: drag a loop round objects to select them (Shift adds to the selection)"},
            {"handTool", SketchTool::Hand, "Hand: drag to pan"},
            {"zoomTool", SketchTool::Zoom, "Zoom: click to zoom in, Shift-click to zoom out"},
        };

        // The zoom combo's presets (its last row, "Fit page", is not a factor).
        const float kZoomSteps[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
        constexpr std::size_t kZoomPresets = sizeof(kZoomSteps) / sizeof(kZoomSteps[0]);

        // Status bar panes, counted from the left: 0 the hint / selection message, 1 the pointer, 2 the selection's size; the last two
        // are the document's color mode and the zoom.
        constexpr std::size_t kHintPane = 0, kPointerPane = 1, kSizePane = 2;

        template <class T>
        T* find(newui::Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }
    }

    // ---- Construction ----

    SketchWindowController::SketchWindowController(newui::Frame& frame, SketchDocument& document) : frame_(frame), document_(document) {
        canvas_ = find<CanvasView>(frame_, "canvas");
        if (canvas_ == nullptr) {
            throw std::runtime_error("SketchWindowController: sketch.newui has no view named \"canvas\"");
        }
        status_ = find<newui::StatusBar>(frame_, "statusBar");
        menuBar_ = find<newui::MenuBar>(frame_, "menuBar");
        zoomCombo_ = find<newui::DropDownList>(frame_, "zoomCombo");
        snapButton_ = find<newui::ToolbarButton>(frame_, "snapToGridButton");

        canvas_->setDocument(&document_);
        files_ = std::make_unique<SketchDocumentController>(document_, frame_);
        commands_ = std::make_unique<SketchCommandController>(document_, *canvas_, *files_);
        stylePanel_ = std::make_unique<StylePanel>(frame_.rootView(), document_);   // follows the selection and edits it, undoably
        textPanel_ = std::make_unique<TextPanel>(frame_.rootView(), document_);   // font, size, style, alignment, spacing of text
        layersPanel_ = std::make_unique<LayersPanel>(frame_.rootView(), document_);   // the layer tree
        artboardPanel_ = std::make_unique<ArtboardPanel>(frame_.rootView(), document_);   // the artboard list and the active one
        libraryPanel_ = std::make_unique<LibraryPanel>(frame_.rootView(), document_);   // symbols and patterns
        brushPanel_ = std::make_unique<BrushPanel>(frame_.rootView(), document_);   // brushes
        transformPanel_ = std::make_unique<TransformPanel>(frame_.rootView(), document_);   // X / Y / W / H / angle of the selection

        bindFileEvents();
        bindZoom();
        bindMenus();
        bindToolbarButtons();
        bindViewChecks();
        bindEditState();
        bindColorMode();
        bindStatusBar();
        bindToolPalette();
        bindStylePanel();
        bindShortcuts();
    }

    void SketchWindowController::start(const std::string& path) {
        if (!path.empty()) {
            files_->open(path);
        } else {
            frame_.setTitle("sketch - " + files_->displayName());
        }
    }

    // ---- Helpers ----

    newui::MenuItem* SketchWindowController::menuItem(const std::string& menuPath, const std::string& text) const {
        // Items that were bound are remembered under their original text (the Undo item's text changes to "Undo Move").
        if (auto bound = menuItems_.find(menuPath + "/" + text); bound != menuItems_.end()) {
            return bound->second;
        }
        if (menuBar_ == nullptr) {
            return nullptr;
        }
        const std::vector<newui::MenuItem*>* level = &menuBar_->menus();
        newui::MenuItem* menu = nullptr;
        std::size_t start = 0;
        while (start <= menuPath.size()) {
            std::size_t slash = menuPath.find('/', start);
            std::string part = menuPath.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            menu = nullptr;
            for (newui::MenuItem* candidate : *level) {
                if (candidate->text() == part) {
                    menu = candidate;
                }
            }
            if (menu == nullptr) {
                return nullptr;
            }
            level = &menu->children();
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        for (newui::MenuItem* item : menu->children()) {
            if (item->text() == text) {
                return item;
            }
        }
        return nullptr;
    }

    void SketchWindowController::onMenuItem(const std::string& menuPath, const std::string& text, std::function<void(newui::MenuItem&)> action) {
        if (newui::MenuItem* item = menuItem(menuPath, text)) {
            menuItems_[menuPath + "/" + text] = item;
            item->onClick += [action](newui::MenuItem& clicked) {
                action(clicked);
                return newui::SyncReturn::Handled;
            };
        }
    }

    void SketchWindowController::onButton(const char* name, std::function<void()> action) {
        if (auto* button = find<newui::Control>(frame_, name)) {
            button->onClick += [action](newui::Control&) {
                action();
                return newui::SyncReturn::Handled;
            };
        }
    }

    // ---- File events: what the document controller reports, shown in the window ----

    void SketchWindowController::bindFileEvents() {
        // The controller asks the questions and shows the dialogs; the window only reflects what it reports.
        files_->onStatus += [this](SketchDocumentController&, std::string text) {
            if (status_ != nullptr) {
                status_->setPaneText(kHintPane, text);
            }
            return newui::SyncReturn::Handled;
        };
        files_->onStateChanged += [this](SketchDocumentController& controller) {
            frame_.setTitle("sketch - " + controller.displayName());
            return newui::SyncReturn::Handled;
        };
        files_->onDocumentReplaced += [this](SketchDocumentController&) {
            canvas_->zoomToFit();
            return newui::SyncReturn::Handled;
        };
        // File > Open Recent lists the newest files; the list changes whenever a file is opened or saved.
        onMenuItem("File", "Open Recent", [](newui::MenuItem&) {});
        files_->onRecentChanged += [this](SketchDocumentController&) {
            rebuildRecentMenu();
            return newui::SyncReturn::Handled;
        };
        rebuildRecentMenu();
    }

    void SketchWindowController::rebuildRecentMenu() {
        auto it = menuItems_.find("File/Open Recent");
        if (it == menuItems_.end()) {
            return;
        }
        newui::MenuItem* parent = it->second;
        parkedMenuItems_.clear();
        while (!parent->children().empty()) {
            newui::MenuItem* old = parent->children().back();
            parent->removeChild(old);
            parkedMenuItems_.emplace_back(old);
        }
        const std::vector<std::string>& recent = files_->recentFiles();
        if (recent.empty()) {
            newui::MenuItem* none = new newui::MenuItem("(No recent files)");
            none->state().setEnabled(false);
            parent->addChild(none);
        }
        for (std::size_t i = 0; i < recent.size(); ++i) {
            newui::MenuItem* entry = new newui::MenuItem(std::to_string(i + 1) + "  " + recent[i]);
            std::string path = recent[i];
            entry->onClick += [this, path](newui::MenuItem&) {
                files_->open(path);
                return newui::SyncReturn::Handled;
            };
            parent->addChild(entry);
        }
        if (!recent.empty()) {
            newui::MenuItem* separator = new newui::MenuItem("");
            separator->setSeparator(true);
            parent->addChild(separator);
            newui::MenuItem* clear = new newui::MenuItem("Clear Recent Files");
            clear->onClick += [this](newui::MenuItem&) {
                files_->clearRecentFiles();
                return newui::SyncReturn::Handled;
            };
            parent->addChild(clear);
        }
    }

    // ---- Zoom: combo <-> canvas <-> status bar (last pane) ----

    void SketchWindowController::bindZoom() {
        if (zoomCombo_ != nullptr) {
            zoomCombo_->onSelectionChanged += [this](newui::DropDownList& combo) {
                std::optional<std::size_t> index = combo.selectedIndex();
                if (!index.has_value()) {
                    return newui::SyncReturn::Ignored;
                }
                if (*index < kZoomPresets) {
                    canvas_->setZoom(kZoomSteps[*index]);
                } else {
                    canvas_->zoomToFit();
                }
                return newui::SyncReturn::Handled;
            };
        }
        canvas_->onZoomChanged += [this](CanvasView& view) {
            if (status_ != nullptr) {
                status_->setPaneText(status_->paneCount() - 1, std::to_string(int(view.zoom() * 100.0f + 0.5f)) + "%");
            }
            // Keep the combo on the matching preset (or none, for wheel / fit zooms in between).
            if (zoomCombo_ != nullptr) {
                std::optional<std::size_t> match;
                for (std::size_t i = 0; i < kZoomPresets; ++i) {
                    if (std::fabs(view.zoom() - kZoomSteps[i]) < 1e-4f) {
                        match = i;
                    }
                }
                if (zoomCombo_->selectedIndex() != match) {
                    zoomCombo_->setSelectedIndex(match);
                }
            }
            return newui::SyncReturn::Handled;
        };
    }

    // ---- Menus and toolbar buttons: views that name a command ----

    void SketchWindowController::bindMenus() {
        for (const MenuBinding& binding : kMenuBindings) {
            std::string command = binding.command;
            onMenuItem(binding.menu, binding.item, [this, command](newui::MenuItem&) {
                commands_->run(command);
                syncViewChecks();   // the canvas switches (grid, rulers, snap) show their state in the menu
            });
        }
        onMenuItem("Help", "About sketch", [this](newui::MenuItem&) {
            newui::Dialog::showMessageBox(frame_.frameHandle(),
                "sketch - a small vector editor built with newui.\n\nSVG files, RGB and CMYK documents, gradients, undo/redo.",
                "About sketch", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Information);
        });
    }

    void SketchWindowController::bindToolbarButtons() {
        for (const ButtonBinding& binding : kButtonBindings) {
            std::string command = binding.command;
            onButton(binding.button, [this, command]() { commands_->run(command); });
        }
        // Snap to Grid is a toggle: the toolbar button and the View menu item are one setting.
        if (snapButton_ != nullptr) {
            snapButton_->onCheckedChanged += [this](newui::ToolbarButton& changed) {
                if (changed.isChecked() != canvas_->snapToGrid()) {
                    commands_->run("view.toggleSnap");
                    syncViewChecks();
                }
                return newui::SyncReturn::Handled;
            };
        }
    }

    void SketchWindowController::syncViewChecks() {
        struct Check { const char* path; bool on; };
        const Check checks[] = {{"View/Show Grid", canvas_->showGrid()}, {"View/Show Rulers", canvas_->showRulers()}, {"View/Snap to Grid", canvas_->snapToGrid()},
            {"View/Smart Guides", canvas_->smartGuides()}, {"View/Show Guides", canvas_->showGuides()}, {"View/Lock Guides", canvas_->guidesLocked()}};
        for (const Check& check : checks) {
            if (auto it = menuItems_.find(check.path); it != menuItems_.end() && it->second->isChecked() != check.on) {
                it->second->setChecked(check.on);
            }
        }
        if (snapButton_ != nullptr && snapButton_->isChecked() != canvas_->snapToGrid()) {
            snapButton_->setChecked(canvas_->snapToGrid());
        }
    }

    void SketchWindowController::bindViewChecks() {
        syncViewChecks();
    }

    // ---- Edit > Undo / Redo (and the toolbar buttons) show whether there is anything to undo, and what ----

    void SketchWindowController::bindEditState() {
        auto sync = [this]() {
            const bool canUndo = commands_->enabled("edit.undo"), canRedo = commands_->enabled("edit.redo");
            if (auto it = menuItems_.find("Edit/Undo"); it != menuItems_.end()) {
                it->second->setText(commands_->undoText());
                it->second->state().setEnabled(canUndo);
            }
            if (auto it = menuItems_.find("Edit/Redo"); it != menuItems_.end()) {
                it->second->setText(commands_->redoText());
                it->second->state().setEnabled(canRedo);
            }
            if (auto* button = find<newui::Control>(frame_, "undoButton")) { button->setEnabled(canUndo); }
            if (auto* button = find<newui::Control>(frame_, "redoButton")) { button->setEnabled(canRedo); }
        };
        document_.onChanged += [sync](SketchDocument&) { sync(); return newui::SyncReturn::Handled; };
        document_.undoStack().onActionPushed += [sync](newui::UndoStack&, const newui::UndoableAction&) { sync(); return newui::SyncReturn::Handled; };
        sync();
    }

    // ---- Document color mode: the status bar's document pane and the menu checks follow it, including after undo / open ----

    void SketchWindowController::bindColorMode() {
        auto sync = [this]() {
            bool cmyk = document_.colorMode() == ColorSpace::CMYK;
            if (status_ != nullptr && status_->paneCount() >= 2) {
                status_->setPaneText(status_->paneCount() - 2, cmyk ? "CMYK doc" : "RGB doc");
            }
            if (auto it = menuItems_.find("View/Color Mode/RGB"); it != menuItems_.end()) { it->second->setChecked(!cmyk); }
            if (auto it = menuItems_.find("View/Color Mode/CMYK"); it != menuItems_.end()) { it->second->setChecked(cmyk); }
        };
        document_.onChanged += [sync](SketchDocument&) { sync(); return newui::SyncReturn::Handled; };
        sync();
    }

    // ---- Status bar: a hint for the tool, the pointer, the size and number of what is selected ----

    void SketchWindowController::bindStatusBar() {
        if (status_ == nullptr) {
            return;
        }
        document_.onSelectionChanged += [this](SketchDocument& changed) {
            std::size_t count = changed.selection().size();
            status_->setPaneText(kHintPane, count == 0 ? "Ready" : std::to_string(count) + (count == 1 ? " object selected" : " objects selected"));
            return newui::SyncReturn::Handled;
        };
        if (status_->paneCount() < 4) {
            return;
        }
        canvas_->onPointerMoved += [this](CanvasView& view) {
            char text[64];
            std::snprintf(text, sizeof(text), "x %.1f  y %.1f pt", view.pointerPosition().x, view.pointerPosition().y);
            status_->setPaneText(kPointerPane, text);
            return newui::SyncReturn::Handled;
        };
        auto syncSize = [this]() {
            BLBox box;
            char text[64] = "";
            if (document_.selectionBounds(box)) {
                std::snprintf(text, sizeof(text), "W %.1f  H %.1f", box.x1 - box.x0, box.y1 - box.y0);
            }
            status_->setPaneText(kSizePane, text);
        };
        document_.onChanged += [syncSize](SketchDocument&) { syncSize(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [syncSize](SketchDocument&) { syncSize(); return newui::SyncReturn::Handled; };
        syncSize();
        canvas_->onToolChanged += [this](CanvasView& view) {
            for (const ToolInfo& info : kTools) {
                if (info.tool == view.tool()) {
                    status_->setPaneText(kHintPane, info.hint);
                }
            }
            return newui::SyncReturn::Handled;
        };
    }

    // ---- Tool palette: radio behavior over the toggle buttons; the canvas tool and the buttons follow each other ----

    void SketchWindowController::bindToolPalette() {
        for (const ToolInfo& info : kTools) {
            if (auto* button = find<newui::ToolbarButton>(frame_, info.button)) {
                SketchTool tool = info.tool;
                button->onCheckedChanged += [this, tool](newui::ToolbarButton& changed) {
                    if (changed.isChecked()) {
                        canvas_->setTool(tool);
                    } else if (canvas_->tool() == tool) {
                        changed.setChecked(true);   // a tool stays on until another replaces it
                    }
                    return newui::SyncReturn::Handled;
                };
            }
        }
        canvas_->onToolChanged += [this](CanvasView& view) {
            for (const ToolInfo& info : kTools) {
                if (auto* button = find<newui::ToolbarButton>(frame_, info.button)) {
                    bool on = info.tool == view.tool();
                    if (button->isChecked() != on) {
                        button->setChecked(on);
                    }
                }
            }
            return newui::SyncReturn::Handled;
        };
    }

    // ---- The Gradient tool edits what the Color panel targets (fill or stroke), and the stop selected on the canvas is the stop the
    // panel edits - and the other way round ----

    void SketchWindowController::bindStylePanel() {
        canvas_->setGradientOnStroke(stylePanel_->target() == StylePanel::Target::Stroke);
        stylePanel_->onTargetChanged += [this](StylePanel& panel) {
            canvas_->setGradientOnStroke(panel.target() == StylePanel::Target::Stroke);
            return newui::SyncReturn::Handled;
        };
        canvas_->onGradientStopSelected += [this](CanvasView& view) {
            stylePanel_->setSelectedStop(view.selectedGradientStop());
            return newui::SyncReturn::Handled;
        };
        stylePanel_->onStopSelected += [this](StylePanel& panel) {
            canvas_->setSelectedGradientStop(panel.selectedStop());
            return newui::SyncReturn::Handled;
        };
    }

    // ---- Keyboard: the File shortcuts inside the canvas, and closing the window ----

    void SketchWindowController::bindShortcuts() {
        canvas_->onControlKey = [this](std::uint32_t vk, bool shift) {
            if (vk == newui::vkLetterN) { return commands_->run("file.new"); }
            if (vk == newui::vkLetterO) { return commands_->run("file.open"); }
            if (vk == newui::vkLetterS) { return commands_->run(shift ? "file.saveAs" : "file.save"); }
            return false;
        };
        // Closing the window (X, Alt+F4) asks the same question as File > Exit; Cancel keeps it open.
        frame_.onClosing += [this](newui::Frame&, bool& canClose) {
            canClose = files_->canClose();
            return newui::SyncReturn::Handled;
        };
    }

}
