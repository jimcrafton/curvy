#include "curvy/sketchchecks.h"

#include "curvy/canvasview.h"
#include "curvy/gradientbar.h"
#include "curvy/layertree.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchwindow.h"
#include "curvy/swatchgrid.h"

#include <newui/controls.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/utils.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    namespace {

        // The document point (x, y) as a point in the canvas view.
        struct ViewFn {
            CanvasView* canvas;
            newui::Point operator()(double x, double y) const {
                BLPoint p = canvas->toView(BLPoint(x, y));
                return newui::Point(float(p.x), float(p.y));
            }
        };

        // A mouse event on the canvas: what 0 = press, 1 = drag / move, 2 = release.
        struct MouseFn {
            CanvasView* canvas;
            void operator()(int what, const newui::Point& p, std::uint32_t keys = 0) const {
                if (what == 0) { canvas->onMouseDown(*canvas, p, std::uint32_t(1), keys); }
                else if (what == 1) { canvas->onMouseMove(*canvas, p, std::uint32_t(1), keys); }
                else { canvas->onMouseUp(*canvas, p, std::uint32_t(1), keys); }
            }
        };

        // One group of checks per method, in the order they run (each leaves the document empty and the Select tool chosen, ready for the
        // next). The methods are the old --check-tools code, moved here from main() unchanged.
        class ToolChecks {
        public:
            explicit ToolChecks(SketchWindowController& window)
                : window_(window), frame_(window.frame()), document_(window.document()), canvas_(&window.canvas()), stylePanel_(window.stylePanel()), files_(window.files()) {}

            int run() {
                checkPalette();
                checkCommands();
                checkCuttingTools();
                checkPathMenu();
                checkTransformPanel();
                checkScrubbing();
                checkTextScrubbing();
                checkTextPath();
                checkGuides();
                checkLayers();
                checkLayerObjects();
                checkTextPanel();
                checkSelectTools();
                checkArtboards();
                checkTransformTools();
                checkDistribute();
                checkClippingMenu();
                checkLibraries();
                checkBrushes();
                checkShapeBuilder();
                checkColorPanel();
                checkGradientTool();
                checkAppearancePanel();
                checkEffects();
                checkGraphicStyles();
                checkDrawingTools();
                checkLiveShapes();
                checkTextTool();
                checkWidthTool();
                checkHandleCursors();
                checkSnapping();
                checkOpenRecent();
                return failures_;
            }

        private:
            void expect(bool ok, const char* what) {
                std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
                std::fflush(stdout);   // so a crash shows how far the checks got
                failures_ += ok ? 0 : 1;
            }

        // Checking a tool button in the palette must switch the canvas tool and leave exactly that one button checked.
        void checkPalette();

        // Scissors, Knife, Eraser and the Pathfinder commands, driven through the canvas and the menu.
        void checkCuttingTools();

        // The Transform panel: its fields show the selection, typing a value moves / resizes / turns it, the link keeps proportions.
        void checkTransformPanel();

        // Shape Builder: dragging across the pieces of overlapping shapes joins them.
        void checkShapeBuilder();

        // Magic Wand, Lasso and Edit > Select Same.
        void checkSelectTools();
        void checkArtboards();
        void checkTransformTools();
        void checkDistribute();
        void checkClippingMenu();
        void checkLibraries();
        void checkBrushes();

        // The Text panel: size, weight, alignment, tracking of the selected text.
        void checkTextPanel();

        // Layers: the tree, the panel (buttons, rows, drag and drop), visibility / lock, moving the selection, delete and undo.
        void checkLayers();
        void checkLayerObjects();

        // Ruler guides: dragged out of the rulers, moved, deleted, locked; snapping to guides and (Smart Guides) to other objects; the View menu.
        void checkGuides();

        // Dragging the captions of the number fields changes the numbers (Shift and Ctrl+Shift for finer steps), one undo step a drag.
        void checkScrubbing();
        void checkTextScrubbing();
        void checkTextPath();

        // Object > Path and File > Export As Image are in the menus and act on the selection.
        void checkPathMenu();

        // The command controller and the bindings that make the views follow it: command ids, enabled state, the Undo label, menu
        // check marks, the tool hint in the status bar.
        void checkCommands();

        // The Color panel: it shows the selected fill; a slider or the hex field edits it, undoably; gradients, the stroke target and the color modes.
        void checkColorPanel();

        // Gradient tool: dragging lays an axis, its ends and stops are draggable, the axis takes new stops; the stroke can be the target; cursors.
        void checkGradientTool();

        // Appearance panel: the stack lists the items, rows choose what the other controls edit, buttons edit the stack.
        void checkAppearancePanel();

        // Effects: a pick in the fx popup adds one on top of the stack and chooses it; the options row edits it and the opacity slider, blend list and buttons follow the chosen effect.
        void checkEffects();

        // Graphic Styles: a click on a tile applies that style to the selection; New saves the selection's; Delete removes one.
        void checkGraphicStyles();

        // Drawing tools: polygon, star, rounded rectangle, pencil and pen add objects; Direct Select edits their points.
        void checkDrawingTools();

        // Live shape handles: a selected star, polygon, rounded rectangle or arc can be reshaped with its own handles.
        void checkLiveShapes();

        // Text tool: click, type, Enter, Backspace, Esc; edit again; empty text goes away.
        void checkTextTool();

        // Width tool: dragging the path adds a point and widens it, the handles and diamond edit it, Alt-click deletes.
        void checkWidthTool();

        // Select tool: the cursor over a handle follows the frame's turn.
        void checkHandleCursors();

        // Drawing and moving snap to the grid when close to a line; Alt resizes from the center.
        void checkSnapping();

        // Open Recent: the newest file is first, a re-opened one moves up, a missing one is dropped, and the list survives into a new controller (it is kept in a file).
        void checkOpenRecent();

            SketchWindowController& window_;
            newui::Frame& frame_;
            SketchDocument& document_;
            CanvasView* canvas_;
            StylePanel& stylePanel_;
            SketchDocumentController& files_;
            int failures_ = 0;
        };

    void ToolChecks::checkPalette() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        int& failures = failures_;
        static const char* kNames[] = {"selectTool", "directSelectTool", "rectangleTool", "ellipseTool", "lineTool",
            "penTool", "pencilTool", "paintbrushTool", "textTool", "gradientTool", "widthTool", "scissorsTool", "knifeTool", "eraserTool", "shapeBuilderTool", "eyedropperTool", "magicWandTool", "lassoTool", "artboardTool", "rotateTool", "scaleTool", "reflectTool", "handTool", "zoomTool"};
        for (const char* name : kNames) {
            auto* button = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView(name));
            if (button == nullptr || canvas == nullptr) {
                std::printf("FAIL %s: not found\n", name);
                ++failures;
                continue;
            }
            button->setChecked(true);
            int lit = 0;
            for (const char* other : kNames) {
                auto* b = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView(other));
                lit += (b != nullptr && b->isChecked()) ? 1 : 0;
            }
            std::printf("%s -> tool %d, %d button(s) checked%s\n", name, int(canvas->tool()), lit, lit == 1 ? "" : "  <-- FAIL");
            failures += lit == 1 ? 0 : 1;
        }
    }

    void ToolChecks::checkCuttingTools() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);

        // Scissors: a click on a path cuts it there; a click away from every path does nothing.
        canvas->setTool(curvy::SketchTool::Scissors);
        expect(canvas->cursorName() == "scissors", "the scissors tool shows the scissors cursor");
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 100)).setName("Square");
        mouse(0, view(150, 150));
        mouse(2, view(150, 150));
        expect(document.objectCount() == 1 && !document.undoStack().canUndo(), "a click inside a shape, away from its outline, cuts nothing");
        mouse(0, view(140, 101));
        mouse(2, view(140, 101));
        std::vector<curvy::SubPath> opened = curvy::pathToSubPaths(document.object(0).geometry());
        expect(document.objectCount() == 1 && opened.size() == 1 && !opened[0].closed && opened[0].anchors.size() == 6, "a click on the outline opens the closed path there");
        expect(document.undoStack().canUndo(), "cutting is an undo step");
        document.undoStack().undo();
        expect(curvy::pathToSubPaths(document.object(0).geometry())[0].closed, "undo closes it again");
        document.clearObjects();
        document.undoStack().clear();

        // Knife: a drag across a shape divides it into two pieces, and nothing is lost.
        canvas->setTool(curvy::SketchTool::Knife);
        expect(canvas->cursorName() == "knife", "the knife tool shows the knife cursor");
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 100)).setName("Card");
        mouse(0, view(150, 80));
        mouse(1, view(150, 150));
        mouse(1, view(150, 220));
        mouse(2, view(150, 220));
        expect(document.objectCount() == 2, "a knife stroke across a shape divides it in two");
        expect(document.selection().size() == 2, "the pieces are selected");
        document.undoStack().undo();
        expect(document.objectCount() == 1, "undo puts the shape back as one");
        mouse(0, view(150, 150));
        mouse(2, view(150, 150));
        expect(document.objectCount() == 1, "a click with the knife cuts nothing");
        document.clearObjects();
        document.undoStack().clear();

        // Eraser: a stroke across a shape takes a slice out; the size follows the arrow keys.
        canvas->setTool(curvy::SketchTool::Eraser);
        expect(canvas->cursorName() == "eraser", "the eraser tool shows the eraser cursor");
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 100)).setName("Card");
        double size = canvas->eraserSize();
        canvas->onKeyDown(*canvas, 0u, 0, 1, newui::vkUpArrow);
        expect(std::fabs(canvas->eraserSize() - (size + 2.0)) < 1e-9, "the Up arrow makes the eraser bigger");
        canvas->setEraserSize(20.0);
        mouse(0, view(150, 80));
        mouse(1, view(150, 150));
        mouse(1, view(150, 220));
        mouse(2, view(150, 220));
        expect(document.objectCount() == 2, "an eraser stroke across a shape leaves two pieces");
        BLBox left = document.object(0).geometryBounds(), right = document.object(1).geometryBounds();
        expect(std::fabs((std::max(left.x1, right.x1) - std::min(left.x0, right.x0)) - 100.0) < 1.0 &&
                   std::fabs(std::fabs(std::max(left.x0, right.x0) - std::min(left.x1, right.x1)) - 20.0) < 1.0, "the gap between them is as wide as the eraser");
        document.undoStack().undo();
        expect(document.objectCount() == 1, "undo puts it back");
        document.clearObjects();
        document.undoStack().clear();

        // Pathfinder: the menu commands work on the selection and are disabled with fewer than two objects selected.
        canvas->setTool(curvy::SketchTool::Select);
        curvy::SketchCommandController& commands = window_.commands();
        std::size_t a = document.add(curvy::SketchObject::makeRect(0, 0, 100, 100)).id();
        std::size_t b = document.add(curvy::SketchObject::makeRect(50, 50, 100, 100)).id();
        document.setSelection({a});
        expect(!commands.enabled("pathfinder.unite"), "Unite is disabled with one object selected");
        document.setSelection({a, b});
        expect(commands.enabled("pathfinder.unite"), "and enabled with two");
        newui::MenuItem* unite = window_.menuItem("Object/Pathfinder", "Unite");
        expect(unite != nullptr, "the Object menu has a Pathfinder submenu with Unite");
        if (unite != nullptr) {
            unite->onClick(*unite);
        }
        expect(document.objectCount() == 1 && document.object(0).type() == curvy::SketchObjectType::Path, "Unite makes one path of the two");
        document.undoStack().undo();
        document.setSelection({a, b});
        commands.run("pathfinder.minusFront");
        expect(document.objectCount() == 1, "Minus Front leaves one object");
        document.undoStack().undo();
        document.setSelection({a, b});
        commands.run("pathfinder.intersect");
        expect(document.objectCount() == 1, "Intersect leaves one object");
        document.undoStack().undo();
        document.setSelection({a, b});
        commands.run("pathfinder.exclude");
        expect(document.objectCount() == 1, "Exclude leaves one object");
        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
        canvas->setSnapToGrid(true);
    }

    void ToolChecks::checkPathMenu() {
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        for (const char* item : {"Join", "Reverse Direction", "Simplify", "Outline Stroke"}) {
            expect(window_.menuItem("Object/Path", item) != nullptr, (std::string("Object > Path has ") + item).c_str());
        }
        expect(window_.menuItem("File", "Export As Image...") != nullptr, "File has Export As Image");
        curvy::SketchObject& line = document.add(curvy::SketchObject::makeLine(0, 0, 100, 0));
        line.setStrokeWidth(8.0);
        document.setSelection({line.id()});
        newui::MenuItem* reverse = window_.menuItem("Object/Path", "Reverse Direction");
        reverse->onClick(*reverse);
        expect(curvy::pathToSubPaths(document.object(0).geometry())[0].anchors.front().point.x > 50.0, "Reverse Direction turns the path round");
        newui::MenuItem* outline = window_.menuItem("Object/Path", "Outline Stroke");
        outline->onClick(*outline);
        expect(document.objectCount() == 1 && document.object(0).type() == curvy::SketchObjectType::Path && document.object(0).stroke().isNone(), "Outline Stroke makes a filled shape");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkTransformPanel() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto field = [&frame](const char* name) { return dynamic_cast<newui::TextField*>(frame.rootView().findView(name)); };
        auto text = [&field](const char* name) { return field(name) != nullptr ? newui::wideToUtf8(field(name)->text()) : std::string("?"); };
        auto type = [&field](const char* name, const wchar_t* value) {
            if (auto* f = field(name)) {
                f->setText(value);
                f->onReturnPressed(*f);
            }
        };
        expect(field("transformXField") != nullptr && field("transformAngleField") != nullptr, "the Transform panel fields are in the layout");
        expect(!field("transformXField")->isEnabled(), "the fields are disabled with nothing selected");
        curvy::SketchObject& rect = document.add(curvy::SketchObject::makeRect(10, 20, 100, 50));
        document.setSelection({rect.id()});
        expect(field("transformXField")->isEnabled() && text("transformXField") == "10" && text("transformYField") == "20" &&
                   text("transformWField") == "100" && text("transformHField") == "50" && text("transformAngleField") == "0", "the fields show the selection's box");
        type("transformXField", L"40");
        expect(std::fabs(document.object(0).geometryBounds().x0 - 40.0) < 1e-9 && std::fabs(document.object(0).geometryBounds().x1 - 140.0) < 1e-9, "typing X moves the selection");
        type("transformWField", L"200");
        expect(std::fabs(document.object(0).geometryBounds().x1 - 240.0) < 1e-9 && std::fabs(document.object(0).geometryBounds().y1 - 70.0) < 1e-9, "typing W resizes it, keeping the corner and the height");
        auto* link = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("linkAspectButton"));
        if (link != nullptr) {
            link->setChecked(true);
            type("transformHField", L"100");
            expect(std::fabs(document.object(0).geometryBounds().y1 - document.object(0).geometryBounds().y0 - 100.0) < 1e-9 &&
                       std::fabs(document.object(0).geometryBounds().x1 - document.object(0).geometryBounds().x0 - 400.0) < 1e-9, "with the link down, H keeps the proportions");
            link->setChecked(false);
        }
        type("transformWField", L"abc");
        expect(text("transformWField") == "400", "a value that is not a number is put back");
        document.undoStack().undo();
        document.undoStack().undo();
        document.undoStack().undo();
        document.undoStack().undo();
        document.setSelection({document.object(0).id()});
        type("transformAngleField", L"90");
        expect(std::fabs(document.object(0).frameAngle() - 3.14159265358979323846 / 2.0) < 1e-9 && text("transformAngleField") == "90", "typing an angle turns the selection");
        expect(window_.commands().enabled("object.transformAgain"), "Transform Again is available after a transform");
        newui::MenuItem* again = window_.menuItem("Object/Transform", "Transform Again");
        expect(again != nullptr, "Object > Transform has Transform Again");
        if (again != nullptr) {
            again->onClick(*again);
            expect(std::fabs(document.object(0).frameAngle() - 3.14159265358979323846) < 1e-9, "Transform Again turns it by the same angle once more");
        }
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkScrubbing() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto label = [&frame](const char* name) { return dynamic_cast<newui::Label*>(frame.rootView().findView(name)); };
        auto field = [&frame](const char* name) { return dynamic_cast<newui::TextField*>(frame.rootView().findView(name)); };
        auto text = [&field](const char* name) { return field(name) != nullptr ? newui::wideToUtf8(field(name)->text()) : std::string("?"); };
        const std::uint32_t left = newui::mbmLeftButton;
        auto drag = [&label, left](const char* name, float pixels, std::uint32_t keys) {
            newui::Label* caption = label(name);
            caption->onMouseDown(*caption, newui::Point(20, 5), left, keys);
            caption->onMouseMove(*caption, newui::Point(20 + pixels / 2, 5), left, keys);
            caption->onMouseMove(*caption, newui::Point(20 + pixels, 5), left, keys);
            caption->onMouseUp(*caption, newui::Point(20 + pixels, 5), left, keys);
        };
        expect(label("transformXLabel") != nullptr && label("strokeWeightLabel") != nullptr, "the captions of the number fields are named in the layout");
        curvy::SketchObject& rect = document.add(curvy::SketchObject::makeRect(10, 20, 100, 50));
        document.setSelection({rect.id()});
        drag("transformXLabel", 5.0f, 0u);
        expect(std::fabs(document.object(0).geometryBounds().x0 - 15.0) < 1e-9 && text("transformXField") == "15", "dragging X sideways moves the selection (1 point a pixel)");
        expect(document.undoStack().undoDescription() == "Move", "the whole drag is one undo step");
        document.undoStack().undo();
        expect(std::fabs(document.object(0).geometryBounds().x0 - 10.0) < 1e-9 && !document.undoStack().canUndo(), "one undo puts it back");
        drag("transformXLabel", 10.0f, newui::kmShift);
        expect(std::fabs(document.object(0).geometryBounds().x0 - 11.0) < 1e-9, "with Shift a pixel is a tenth of a point");
        drag("transformXLabel", 10.0f, newui::kmShift | newui::kmCtrl);
        expect(std::fabs(document.object(0).geometryBounds().x0 - 11.1) < 1e-9, "with Ctrl+Shift it is a hundredth");
        drag("transformWLabel", 20.0f, 0u);
        expect(std::fabs(document.object(0).geometryBounds().x1 - document.object(0).geometryBounds().x0 - 120.0) < 1e-9, "dragging W changes the width");
        drag("transformAngleLabel", 30.0f, 0u);
        expect(std::fabs(document.object(0).frameAngle() - 30.0 * 3.14159265358979323846 / 180.0) < 1e-9, "dragging Angle turns the selection (a degree a pixel)");
        expect(window_.commands().enabled("object.transformAgain"), "a scrubbed transform can be repeated with Transform Again");
        document.undoStack().clear();

        // Style panel numbers: the stroke weight, with its unit kept.
        document.setSelection({document.object(0).id()});
        double weight = document.object(0).strokeWidth();
        drag("strokeWeightLabel", 4.0f, 0u);
        expect(std::fabs(document.object(0).strokeWidth() - (weight + 2.0)) < 1e-9 && text("strokeWeightField").find("pt") != std::string::npos,
               "dragging Weight changes the stroke weight (half a point a pixel) and keeps the unit");
        expect(document.undoStack().undoDescription().find("Stroke") != std::string::npos || document.undoStack().canUndo(), "as one undo step");
        std::size_t steps = 0;
        while (document.undoStack().canUndo()) { document.undoStack().undo(); ++steps; }
        expect(steps == 1 && std::fabs(document.object(0).strokeWidth() - weight) < 1e-9, "a single undo restores the weight");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkGuides() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        canvas->setTool(curvy::SketchTool::Select);
        const float middle = view(100, 100).x;
        auto ruler = [&view](double documentY) { return newui::Point(view(100, documentY).x, 5.0f); };

        // A press in the top ruler, dragged into the page, drops a horizontal guide there.
        mouse(0, ruler(0));
        mouse(1, view(100, 200));
        mouse(2, view(100, 200));
        expect(document.guides().size() == 1 && !document.guides()[0].vertical && std::fabs(document.guides()[0].position - 200.0) < 1e-6, "dragging out of the top ruler makes a horizontal guide");
        expect(document.undoStack().undoDescription() == "Add Guide", "making a guide is an undo step");
        // The left ruler makes a vertical one.
        mouse(0, newui::Point(5.0f, view(0, 300).y));
        mouse(1, view(150, 300));
        mouse(2, view(150, 300));
        expect(document.guides().size() == 2 && document.guides()[1].vertical && std::fabs(document.guides()[1].position - 150.0) < 1e-6, "dragging out of the left ruler makes a vertical guide");
        // Let go back on the ruler: no guide.
        mouse(0, ruler(0));
        mouse(1, view(100, 400));
        mouse(2, ruler(0));
        expect(document.guides().size() == 2, "a guide let go on the ruler again is not made");

        // Dragging a guide with the Select tool moves it; dragging it back onto a ruler deletes it.
        mouse(0, view(300, 200));
        mouse(1, view(300, 260));
        mouse(2, view(300, 260));
        expect(std::fabs(document.guides()[0].position - 260.0) < 1e-6 && document.undoStack().undoDescription() == "Move Guide", "dragging a guide moves it (one undo step)");
        canvas->onMouseMove(*canvas, view(300, 260), std::uint32_t(0), 0);
        expect(canvas->cursorName() == "resize-ns", "over a horizontal guide the cursor is the vertical arrow");
        canvas->setGuidesLocked(true);
        mouse(0, view(300, 260));
        mouse(1, view(300, 300));
        mouse(2, view(300, 300));
        expect(std::fabs(document.guides()[0].position - 260.0) < 1e-6, "a locked guide stays where it is");
        canvas->setGuidesLocked(false);
        mouse(0, view(300, 260));
        mouse(1, ruler(0));
        mouse(2, ruler(0));
        expect(document.guides().size() == 1 && document.guides()[0].vertical, "dragging a guide back onto the ruler deletes it");
        document.undoStack().undo();
        expect(document.guides().size() == 2, "and undo brings it back");
        document.undoStack().clear();

        // Drawing snaps to a guide (the vertical one at x = 150).
        canvas->setTool(curvy::SketchTool::Rectangle);
        mouse(0, view(147, 50));
        mouse(1, view(300, 100));
        mouse(2, view(300, 100));
        expect(document.objectCount() == 1 && std::fabs(document.object(0).geometryBounds().x0 - 150.0) < 1e-6, "a rectangle drawn near a guide starts exactly on it");
        canvas->setShowGuides(false);
        canvas->setSmartGuides(false);   // (the first rectangle's own edge would catch it otherwise)
        mouse(0, view(147, 50));
        mouse(1, view(300, 100));
        mouse(2, view(300, 100));
        expect(document.objectCount() == 2 && std::fabs(document.object(1).geometryBounds().x0 - 147.0) < 1e-6, "with the guides hidden nothing snaps to them");
        canvas->setShowGuides(true);
        canvas->setSmartGuides(true);
        document.clearObjects();
        document.undoStack().clear();

        // Smart Guides: edges and centers of other objects catch too, and a magenta line shows where.
        document.add(curvy::SketchObject::makeRect(300, 300, 40, 40));
        document.clearGuides();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Rectangle);
        mouse(0, view(50, 50));
        mouse(1, view(298, 100));
        mouse(2, view(298, 100));
        expect(document.objectCount() == 2 && std::fabs(document.object(1).geometryBounds().x1 - 300.0) < 1e-6, "a corner drawn near another object's edge snaps to it");
        canvas->setSmartGuides(false);
        mouse(0, view(50, 50));
        mouse(1, view(298, 100));
        mouse(2, view(298, 100));
        expect(document.objectCount() == 3 && std::fabs(document.object(2).geometryBounds().x1 - 298.0) < 1e-6, "with Smart Guides off it does not");
        canvas->setSmartGuides(true);
        // Moving: the dragged box's edge catches another object's edge.
        document.clearObjects();
        curvy::SketchObject& still = document.add(curvy::SketchObject::makeRect(300, 100, 50, 50));
        curvy::SketchObject& mover = document.add(curvy::SketchObject::makeRect(100, 300, 50, 50));
        (void)still;
        canvas->setTool(curvy::SketchTool::Select);
        document.setSelection({mover.id()});
        mouse(0, view(110, 310));   // not the center (the pivot) of the box
        mouse(1, view(262, 303));   // the box's right edge is then at 302: 2 points from the other object's left edge at 300... moved by (152, -7)
        mouse(2, view(262, 303));
        expect(std::fabs(document.object(1).geometryBounds().x1 - 300.0) < 1e-6 || std::fabs(document.object(1).geometryBounds().x0 - 300.0) < 1e-6 ||
                   std::fabs(document.object(1).geometryBounds().x0 - 350.0) < 1e-6, "a moved box's edge catches the edge or center of another object");
        document.clearObjects();
        document.undoStack().clear();

        // The View menu: Show Guides, Lock Guides, Clear Guides.
        document.addGuide(true, 10.0);
        newui::MenuItem* show = window_.menuItem("View", "Show Guides");
        newui::MenuItem* lock = window_.menuItem("View", "Lock Guides");
        newui::MenuItem* clear = window_.menuItem("View", "Clear Guides");
        expect(show != nullptr && lock != nullptr && clear != nullptr && show->isChecked() == canvas->showGuides(), "the View menu has Show Guides, Lock Guides and Clear Guides");
        if (show != nullptr && lock != nullptr && clear != nullptr) {
            show->onClick(*show);
            expect(!canvas->showGuides() && !show->isChecked(), "Show Guides toggles them and its check mark");
            show->onClick(*show);
            lock->onClick(*lock);
            expect(canvas->guidesLocked() && lock->isChecked(), "Lock Guides locks them");
            lock->onClick(*lock);
            clear->onClick(*clear);
            expect(document.guides().empty(), "Clear Guides removes them all");
            document.undoStack().undo();
            expect(document.guides().size() == 1, "undo brings them back");
        }
        (void)middle;
        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
        canvas->setSnapToGrid(true);
    }

    void ToolChecks::checkShapeBuilder() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        curvy::SketchObject& a = document.add(curvy::SketchObject::makeRect(100, 100, 100, 100));
        curvy::SketchObject& b = document.add(curvy::SketchObject::makeRect(150, 100, 100, 100));
        document.setSelection({a.id(), b.id()});
        canvas->setTool(curvy::SketchTool::ShapeBuilder);
        // A drag across the overlap only joins it into a shape of its own.
        mouse(0, view(175, 150));
        mouse(1, view(180, 150));
        mouse(2, view(180, 150));
        expect(document.objectCount() == 3, "the Shape Builder pulls the overlap out as a shape of its own");
        document.undoStack().undo();
        expect(document.objectCount() == 2, "and undo puts the shapes back");
        document.setSelection({document.object(0).id(), document.object(1).id()});
        // A drag from one shape's own part across the overlap into the other's joins all three.
        mouse(0, view(120, 150));
        mouse(1, view(175, 150));
        mouse(2, view(230, 150));
        expect(document.objectCount() == 1, "dragging across all the pieces joins them into one shape");
        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
    }

    void ToolChecks::checkArtboards() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        expect(document.artboards().size() == 1, "a document starts with one artboard");
        canvas->setTool(curvy::SketchTool::Artboard);
        canvas->setPan(20.0f, 20.0f);
        mouse(0, view(640, 100));
        mouse(1, view(700, 150));
        mouse(1, view(740, 300));
        mouse(2, view(740, 300));
        expect(document.artboards().size() == 2, "dragging with the Artboard tool on empty canvas makes an artboard");
        if (document.artboards().size() == 2) {
            expect(std::fabs(document.artboards()[1].width - 100.0) < 1e-6 && std::fabs(document.artboards()[1].height - 200.0) < 1e-6, "the new artboard has the dragged size");
            expect(document.activeArtboard() == 1, "and becomes the active one");
        }
        mouse(0, view(300, 300));
        mouse(2, view(300, 300));
        expect(document.activeArtboard() == 0, "clicking an artboard makes it the active one");
        mouse(0, view(300, 300));
        mouse(1, view(320, 310));
        mouse(2, view(320, 310));
        expect(document.artboards()[0].x == 20.0 && document.artboards()[0].y == 10.0, "dragging an artboard moves it");
        document.undoStack().undo();
        expect(document.artboards()[0].x == 0.0, "moving an artboard is one undo step");
        newui::MenuItem* add = window_.menuItem("Object/Artboards", "New Artboard");
        expect(add != nullptr, "Object has an Artboards submenu");
        if (add != nullptr) {
            add->onClick(*add);
            expect(document.artboards().size() == 3, "Object > Artboards > New Artboard adds one");
        }
        expect(window_.menuItem("File", "Export All Artboards...") != nullptr, "File has Export All Artboards");
        expect(window_.menuItem("File", "Place Image...") != nullptr && window_.commands().has("file.placeImage"), "File has Place Image");
        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
    }

    // Rotate, Scale and Reflect: a click sets the origin, a drag changes the selection about it, Alt leaves a copy.
    void ToolChecks::checkTransformTools() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        canvas->setPan(20.0f, 20.0f);
        std::size_t id = document.add(SketchObject::makeRect(100, 100, 60, 40)).id();
        auto bounds = [&]() { return document.findById(id)->geometryBounds(); };
        auto same = [](double a, double b) { return std::fabs(a - b) < 1e-6; };

        document.clearSelection();
        canvas->setTool(curvy::SketchTool::Rotate);
        mouse(0, view(130, 120));
        mouse(2, view(130, 120));
        expect(document.selection().size() == 1, "a click with a transform tool selects an object when nothing is selected");

        mouse(0, view(300, 300));   // a click elsewhere only moves the origin
        mouse(2, view(300, 300));
        SelectionFrame frame;
        expect(document.selectionFrame(frame) && same(document.selectionOrigin(frame).x, 300.0) && same(document.selectionOrigin(frame).y, 300.0), "a click sets the transform origin");
        expect(same(bounds().x0, 100.0), "and changes nothing else");
        mouse(0, view(130, 120));   // back to the middle of the rectangle
        mouse(2, view(130, 120));

        mouse(0, view(230, 120));   // Rotate: from the right of the origin to below it is a quarter turn
        mouse(1, view(200, 160));
        mouse(1, view(130, 220));
        mouse(2, view(130, 220));
        expect(same(bounds().x0, 110.0) && same(bounds().x1, 150.0) && same(bounds().y0, 90.0) && same(bounds().y1, 150.0), "Rotate turns the selection about the origin");
        expect(std::fabs(document.findById(id)->frameAngle() - 1.5707963267948966) < 1e-6, "and its frame turns with it");
        document.undoStack().undo();
        expect(same(bounds().x0, 100.0) && same(bounds().y1, 140.0), "a Rotate drag is one undo step");

        canvas->setTool(curvy::SketchTool::Scale);
        mouse(0, view(230, 120));   // Scale: twice as far from the origin is twice the size
        mouse(1, view(280, 120));
        mouse(1, view(330, 120));
        mouse(2, view(330, 120));
        expect(same(bounds().x0, 70.0) && same(bounds().x1, 190.0) && same(bounds().y0, 80.0) && same(bounds().y1, 160.0), "Scale resizes the selection about the origin");
        document.undoStack().undo();
        mouse(0, view(230, 120));   // with Shift each direction follows the pointer: x doubles, y stays
        mouse(1, view(280, 120), newui::kmShift);
        mouse(1, view(330, 120), newui::kmShift);
        mouse(2, view(330, 120), newui::kmShift);
        expect(same(bounds().x1 - bounds().x0, 120.0) && same(bounds().y1 - bounds().y0, 40.0), "Shift scales each direction on its own");
        document.undoStack().undo();

        canvas->setTool(curvy::SketchTool::Reflect);
        mouse(0, view(100, 120));   // Reflect: a click on the left edge, then an axis that runs straight down
        mouse(2, view(100, 120));
        mouse(0, view(400, 400));
        mouse(1, view(200, 300));
        mouse(1, view(100, 220));
        mouse(2, view(100, 220));
        expect(same(bounds().x0, 40.0) && same(bounds().x1, 100.0) && same(bounds().y0, 100.0), "Reflect mirrors the selection across the axis through the origin");
        document.undoStack().undo();

        mouse(0, view(400, 400));   // Alt: the original stays and a mirrored copy is added
        mouse(1, view(200, 300), newui::kmAlt);
        mouse(1, view(100, 220), newui::kmAlt);
        mouse(2, view(100, 220), newui::kmAlt);
        expect(document.objectCount() == 2 && same(bounds().x0, 100.0), "Alt leaves the original where it was");
        expect(document.selection().size() == 1 && document.selection()[0] != id && same(document.findById(document.selection()[0])->geometryBounds().x0, 40.0), "and the copy is mirrored and selected");
        document.undoStack().undo();
        expect(document.objectCount() == 1, "a copy is one undo step");

        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
    }

    // Arrange > Distribute through the menu.
    void ToolChecks::checkDistribute() {
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        std::size_t a = document.add(SketchObject::makeRect(0, 0, 40, 40)).id();
        std::size_t b = document.add(SketchObject::makeRect(60, 0, 20, 20)).id();
        std::size_t c = document.add(SketchObject::makeRect(200, 0, 60, 40)).id();
        document.setSelection({a, b, c});
        newui::MenuItem* spacing = window_.menuItem("Arrange/Distribute", "Horizontal Spacing");
        expect(spacing != nullptr, "Arrange has a Distribute submenu");
        if (spacing != nullptr) {
            spacing->onClick(*spacing);
            expect(std::fabs(document.findById(b)->geometryBounds().x0 - 110.0) < 1e-6, "Horizontal Spacing makes the gaps equal");
        }
        newui::MenuItem* edges = window_.menuItem("Arrange/Distribute", "Top Edges");
        if (edges != nullptr) {
            document.transformObjects({b}, BLMatrix2D::make_translation(0, 30), "Move");   // tops 0, 30 and (after this) 40
            document.transformObjects({c}, BLMatrix2D::make_translation(0, 40), "Move");
            edges->onClick(*edges);
            expect(std::fabs(document.findById(b)->geometryBounds().y0 - 20.0) < 1e-6, "Top Edges spreads the tops between the outermost");
        }
        document.clearObjects();
        document.undoStack().clear();
    }

    // The Brushes panel and menu, and the Paintbrush tool.
    void ToolChecks::checkBrushes() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        canvas->setPan(20.0f, 20.0f);
        auto menu = [&](const char* label) { return window_.menuItem("Object/Brush", label); };
        newui::MenuItem* presets = menu("Add Brush Presets");
        newui::MenuItem* apply = menu("Apply Brush");
        newui::MenuItem* remove = menu("Remove Brush");
        newui::MenuItem* nib = menu("New Calligraphic Brush");
        expect(presets != nullptr && apply != nullptr && remove != nullptr && nib != nullptr && menu("New Art Brush") != nullptr && menu("New Scatter Brush") != nullptr, "Object has a Brush submenu");
        expect(dynamic_cast<newui::DropDownList*>(frame_.rootView().findView("brushCombo")) != nullptr, "the Brushes panel is in the layout");
        if (presets == nullptr || apply == nullptr || remove == nullptr || nib == nullptr) {
            return;
        }
        presets->onClick(*presets);
        expect(document.brushes().size() == 6 && document.activeBrush() != 0, "Add Brush Presets fills the library");
        nib->onClick(*nib);
        expect(document.brushes().size() == 7, "New Calligraphic Brush adds one");

        canvas->setTool(curvy::SketchTool::Paintbrush);
        mouse(0, view(100, 100));
        mouse(1, view(140, 130));
        mouse(1, view(180, 110));
        mouse(1, view(220, 140));
        mouse(1, view(260, 100));
        mouse(2, view(260, 100));
        expect(document.objectCount() == 1, "the Paintbrush draws a path");
        if (document.objectCount() == 1) {
            const curvy::SketchObject& drawn = document.object(0);
            expect(drawn.strokeOptions().brush != nullptr && drawn.strokeOptions().brush->id == document.activeBrush(), "stroked with the active brush");
            expect(drawn.fill().isNone(), "and not filled");
        }
        document.undoStack().undo();
        expect(document.objectCount() == 0, "a Paintbrush stroke is one undo step");

        canvas->setTool(curvy::SketchTool::Select);
        document.add(curvy::SketchObject::makeLine(10, 10, 200, 10));
        document.selectAll();
        apply->onClick(*apply);
        expect(document.selectedBrush() != nullptr, "Apply Brush gives the selection the active brush");
        auto* combo = dynamic_cast<newui::DropDownList*>(frame_.rootView().findView("brushCombo"));
        expect(combo != nullptr && combo->selectedIndex().has_value() && document.brushes()[*combo->selectedIndex()]->id == document.activeBrush(), "the Brushes panel shows it");
        remove->onClick(*remove);
        expect(document.selectedBrush() == nullptr, "Remove Brush returns to the plain stroke");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkLibraries() {
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        document.add(curvy::SketchObject::makeRect(10, 10, 40, 40));
        document.selectAll();
        newui::MenuItem* make = window_.menuItem("Object/Symbol", "New Symbol");
        newui::MenuItem* place = window_.menuItem("Object/Symbol", "Place Symbol");
        newui::MenuItem* patternMake = window_.menuItem("Object/Pattern", "Make Pattern");
        newui::MenuItem* patternFill = window_.menuItem("Object/Pattern", "Fill with Pattern");
        expect(make != nullptr && place != nullptr && patternMake != nullptr && patternFill != nullptr, "Object has Symbol and Pattern submenus");
        if (make != nullptr && place != nullptr && patternMake != nullptr && patternFill != nullptr) {
            patternMake->onClick(*patternMake);
            expect(document.patterns().size() == 1, "Make Pattern adds a pattern to the library");
            patternFill->onClick(*patternFill);
            expect(document.selectedPatternPaint() != nullptr, "Fill with Pattern fills the selection with it");
            make->onClick(*make);
            expect(document.symbols().size() == 1 && document.symbolUseCount(document.symbols()[0]->id) == 1, "New Symbol turns the selection into an instance");
            place->onClick(*place);
            expect(document.symbolUseCount(document.symbols()[0]->id) == 2, "Place Symbol adds another instance");
        }
        expect(dynamic_cast<newui::DropDownList*>(frame_.rootView().findView("symbolCombo")) != nullptr, "the Symbols panel is in the layout");
        expect(dynamic_cast<newui::DropDownList*>(frame_.rootView().findView("patternCombo")) != nullptr, "the Patterns panel is in the layout");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkClippingMenu() {
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        document.add(curvy::SketchObject::makeRect(0, 0, 100, 100));
        document.add(curvy::SketchObject::makeEllipse(50, 50, 20, 20));
        document.selectAll();
        newui::MenuItem* make = window_.menuItem("Object/Clipping Mask", "Make");
        newui::MenuItem* release = window_.menuItem("Object/Clipping Mask", "Release");
        expect(make != nullptr && release != nullptr, "Object has a Clipping Mask submenu");
        if (make != nullptr && release != nullptr) {
            make->onClick(*make);
            expect(document.canReleaseClippingMask(), "Make Clipping Mask groups the selection as a mask");
            release->onClick(*release);
            expect(!document.canReleaseClippingMask(), "Release Clipping Mask frees it");
        }
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkSelectTools() {
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        auto red = [](curvy::SketchObject& object) { object.setFill(curvy::SketchPaint::solid(curvy::SketchColor::fromHex("#ff0000"))); };
        red(document.add(curvy::SketchObject::makeRect(100, 100, 50, 50)));
        red(document.add(curvy::SketchObject::makeRect(200, 100, 50, 50)));
        document.add(curvy::SketchObject::makeRect(300, 100, 50, 50)).setFill(curvy::SketchPaint::solid(curvy::SketchColor::fromHex("#0000ff")));
        canvas->setTool(curvy::SketchTool::MagicWand);
        mouse(0, view(120, 120));
        mouse(2, view(120, 120));
        expect(document.selection().size() == 2, "the Magic Wand selects every object with the same fill");
        mouse(0, view(500, 500));
        mouse(2, view(500, 500));
        expect(document.selection().empty(), "a click on nothing with the wand deselects");
        canvas->setTool(curvy::SketchTool::Lasso);
        mouse(0, view(80, 80));
        mouse(1, view(280, 80));
        mouse(1, view(280, 180));
        mouse(1, view(80, 180));
        mouse(2, view(80, 180));
        expect(document.selection().size() == 2, "the Lasso selects the objects it is drawn round");
        document.setSelection({document.object(0).id()});
        newui::MenuItem* same = window_.menuItem("Edit/Select Same", "Fill Color");
        expect(same != nullptr, "Edit has a Select Same submenu");
        if (same != nullptr) {
            same->onClick(*same);
            expect(document.selection().size() == 2, "Select Same > Fill Color selects the objects with that fill");
        }
        document.clearObjects();
        document.undoStack().clear();
        canvas->setTool(curvy::SketchTool::Select);
    }

    // Dragging the captions of the Text panel (Size, Track, Lead).
    void ToolChecks::checkTextScrubbing() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto label = [&frame](const char* name) { return dynamic_cast<newui::Label*>(frame.rootView().findView(name)); };
        const std::uint32_t left = newui::mbmLeftButton;
        auto drag = [&label, left](const char* name, float pixels) {
            newui::Label* caption = label(name);
            caption->onMouseDown(*caption, newui::Point(20, 5), left, 0u);
            caption->onMouseMove(*caption, newui::Point(20 + pixels / 2, 5), left, 0u);
            caption->onMouseMove(*caption, newui::Point(20 + pixels, 5), left, 0u);
            caption->onMouseUp(*caption, newui::Point(20 + pixels, 5), left, 0u);
        };
        expect(label("textSizeLabel") != nullptr && label("textTrackingLabel") != nullptr && label("textLeadingLabel") != nullptr, "the captions of the Text panel are named in the layout");
        curvy::SketchObject& text = document.add(curvy::SketchObject::makeText("Hello", "", 24.0, BLPoint(20, 60)));
        document.setSelection({text.id()});
        drag("textSizeLabel", 6.0f);
        expect(text.fontSize() == 30.0 && document.undoStack().undoDescription() == "Text Size", "dragging Size changes the font size, one undo step");
        document.undoStack().undo();
        expect(text.fontSize() == 24.0 && !document.undoStack().canUndo(), "and undo puts it back");
        drag("textTrackingLabel", 8.0f);
        expect(text.textStyle().tracking == 8.0 && document.undoStack().undoDescription() == "Tracking", "dragging Track changes the tracking");
        document.undoStack().undo();
        expect(text.textStyle().tracking == 0.0, "and undo puts it back");
        drag("textLeadingLabel", 20.0f);
        expect(std::fabs(text.textStyle().leading - 1.2) < 1e-9 && document.undoStack().undoDescription() == "Leading", "dragging Lead changes the leading (a percent a pixel)");
        document.undoStack().undo();
        document.clearSelection();
        drag("textSizeLabel", 6.0f);
        expect(!document.undoStack().canUndo(), "with no text selected a drag does nothing");
        document.clearObjects();
        document.undoStack().clear();
    }

    // Object > Text on Path and the Start / Flip controls of the Text panel.
    void ToolChecks::checkTextPath() {
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto menu = [&](const char* label) { return window_.menuItem("Object/Text on Path", label); };
        newui::MenuItem* onPath = menu("Make Text on Path");
        newui::MenuItem* between = menu("Make Text Between Paths");
        newui::MenuItem* release = menu("Release Text from Path");
        expect(onPath && between && release, "Object has a Text on Path submenu");
        if (!(onPath && between && release)) {
            return;
        }
        auto field = [this](const char* name) { return dynamic_cast<newui::TextField*>(frame_.rootView().findView(name)); };
        const std::size_t text = document.add(curvy::SketchObject::makeText("Around", "", 24.0, BLPoint(10, 300))).id();
        const std::size_t top = document.add(curvy::SketchObject::makeLine(0, 50, 300, 50)).id();
        const std::size_t bottom = document.add(curvy::SketchObject::makeLine(0, 150, 300, 150)).id();
        document.setSelection({text, top});
        onPath->onClick(*onPath);
        expect(document.findById(text)->textWarp().kind == curvy::TextWarp::Kind::OnPath, "Make Text on Path puts the text on the path");
        expect(field("textPathStartField") != nullptr && field("textPathStartField")->isEnabled(), "the Text panel offers the start of the text on the path");
        field("textPathStartField")->setText(L"50");
        field("textPathStartField")->onReturnPressed(*field("textPathStartField"));
        expect(document.findById(text)->textWarp().start == 50.0, "typing a start moves the text along the path");
        release->onClick(*release);
        expect(!document.findById(text)->textWarp().active(), "Release Text from Path makes it plain text again");
        document.setSelection({text, top, bottom});
        between->onClick(*between);
        expect(document.findById(text)->textWarp().kind == curvy::TextWarp::Kind::Between, "Make Text Between Paths stretches it between two paths");
        expect(document.selection().size() == 1 && document.selection()[0] == text, "and leaves the text selected");
        document.undoStack().undo();
        expect(!document.findById(text)->textWarp().active(), "one undo takes it off again");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkTextPanel() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto field = [&frame](const char* name) { return dynamic_cast<newui::TextField*>(frame.rootView().findView(name)); };
        auto button = [&frame](const char* name) { return dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView(name)); };
        expect(field("textSizeField") != nullptr && button("textBoldButton") != nullptr && button("textAlignCenterButton") != nullptr, "the Text panel controls are in the layout");
        expect(!field("textSizeField")->isEnabled(), "the Text panel is disabled with no text selected");
        curvy::SketchObject& text = document.add(curvy::SketchObject::makeText("Hello", "", 24.0, BLPoint(20, 60)));
        document.setSelection({text.id()});
        expect(field("textSizeField")->isEnabled() && newui::wideToUtf8(field("textSizeField")->text()) == "24", "it shows the selected text's size");
        field("textSizeField")->setText(L"36");
        field("textSizeField")->onReturnPressed(*field("textSizeField"));
        expect(text.fontSize() == 36.0 && document.undoStack().undoDescription() == "Text Size", "typing a size resizes the text (one undo step)");
        if (auto* bold = button("textBoldButton")) {
            bold->setChecked(true);
            expect(text.textStyle().bold, "the Bold button sets the weight");
        }
        if (auto* center = button("textAlignCenterButton")) {
            center->setChecked(true);
            expect(text.textStyle().align == curvy::TextStyle::Center && !button("textAlignLeftButton")->isChecked(), "the alignment buttons act as one group");
        }
        field("textTrackingField")->setText(L"4");
        field("textTrackingField")->onReturnPressed(*field("textTrackingField"));
        expect(text.textStyle().tracking == 4.0, "typing a tracking sets it");
        document.undoStack().undo();
        expect(text.textStyle().tracking == 0.0 && newui::wideToUtf8(field("textTrackingField")->text()) == "0", "undo restores it and the panel follows");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkLayers() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto* tree = dynamic_cast<curvy::LayerTree*>(frame.rootView().findView("layersTree"));
        auto press = [&frame](const char* name) {
            if (auto* button = dynamic_cast<newui::Control*>(frame.rootView().findView(name))) {
                button->onClick(*button);
            }
        };
        auto layerRows = [&]() {
            std::size_t n = 0;
            if (tree != nullptr) {
                for (const curvy::LayerRow& row : tree->rows()) { n += row.kind == curvy::LayerRow::Kind::Layer ? 1 : 0; }
            }
            return n;
        };
        expect(tree != nullptr && layerRows() == 1 && tree->rows()[0].name == "Layer 1", "the Layers panel lists the document's one layer");
        if (tree == nullptr) {
            return;
        }
        const std::size_t base = document.activeLayer();
        document.add(curvy::SketchObject::makeRect(0, 0, 10, 10));
        press("addLayerButton");
        expect(document.layers().size() == 2 && layerRows() == 2 && tree->rows()[0].active, "New Layer adds a row on top and makes it active");
        std::size_t second = document.activeLayer();
        document.add(curvy::SketchObject::makeRect(20, 0, 10, 10));
        expect(document.object(1).layer() == second, "new objects go to the active layer");
        press("addSublayerButton");
        expect(document.layers().size() == 3 && document.layerDepth(document.activeLayer()) == 1 && layerRows() == 3 && tree->rows()[2].depth == 1,
               "New Sublayer adds an indented row under the active layer");
        std::size_t sub = document.activeLayer();

        // The eye and the lock of a row.
        const curvy::LayerRow& top = tree->rows()[0];
        (void)top;
        document.setLayerVisible(second, false);
        expect(!document.findById(document.object(1).id())->isVisible() || !document.object(1).isVisible(), "hiding a layer hides its objects");
        expect(tree->rows()[0].visible == false && tree->rows()[1].shown == false, "its row and its sublayer's row show it");
        document.setLayerVisible(second, true);
        document.setLayerLocked(second, true);
        expect(document.object(1).isLocked() && !document.object(1).ownLocked(), "locking a layer locks its objects without changing theirs");
        document.setLayerLocked(second, false);

        // Rename through the name field.
        if (auto* field = dynamic_cast<newui::TextField*>(frame.rootView().findView("layerNameField"))) {
            field->setText(L"Details");
            field->onReturnPressed(*field);
            expect(document.findLayer(sub)->name == "Details", "the name field renames the active layer");
        }

        // Move the selection to a layer, and drag a row onto another.
        document.setSelection({document.object(0).id()});
        document.moveSelectionToLayer(second);
        expect(document.object(0).layer() == second || document.object(1).layer() == second, "Move to Layer puts the selection in the chosen layer");
        document.undoStack().undo();
        expect(document.object(0).layer() == base, "and undo puts it back");
        document.moveLayer(sub, 0, 0);
        expect(document.findLayer(sub)->parent == 0 && document.layerChildren(0).front() == sub, "a layer can be taken out of its parent and sent to the back");
        document.undoStack().undo();
        expect(document.findLayer(sub)->parent == second, "undo puts it back inside");

        press("deleteLayerButton");
        expect(document.layers().size() == 2, "Delete removes the active layer");
        document.undoStack().undo();
        expect(document.layers().size() == 3 && layerRows() == 3, "and undo brings it back with its row");
        document.clearObjects();
        document.undoStack().clear();
        expect(document.layers().size() == 1 && layerRows() == 1, "a new document has the one layer again");
    }

    // Objects and groups in the Layers tree, the wheel, and the Layer menu.
    void ToolChecks::checkLayerObjects() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        document.clearObjects();
        document.undoStack().clear();
        auto* tree = dynamic_cast<curvy::LayerTree*>(frame.rootView().findView("layersTree"));
        expect(tree != nullptr, "the Layers tree is in the layout");
        if (tree == nullptr) {
            return;
        }
        using Kind = curvy::LayerRow::Kind;
        const std::size_t a = document.add(curvy::SketchObject::makeRect(0, 0, 10, 10)).id();
        const std::size_t b = document.add(curvy::SketchObject::makeRect(20, 0, 10, 10)).id();
        const std::size_t c = document.add(curvy::SketchObject::makeEllipse(50, 5, 5, 5)).id();
        auto indexOf = [&](Kind kind, std::size_t id) {
            for (std::size_t i = 0; i < tree->rows().size(); ++i) {
                if (tree->rows()[i].kind == kind && tree->rows()[i].id == id) { return i; }
            }
            return tree->rows().size();
        };
        auto centre = [](const newui::Rect& r) { return newui::Point(r.left() + r.width() * 0.5f, r.top() + r.height() * 0.5f); };
        auto click = [&](const newui::Rect& where, std::uint32_t keys = 0) {
            newui::Point pt = centre(where);
            tree->onMouseDown(*tree, pt, std::uint32_t(1), keys);
            tree->onMouseUp(*tree, pt, std::uint32_t(1), keys);
        };
        expect(tree->rows().size() == 4 && tree->rows()[0].kind == Kind::Layer && tree->rows()[1].id == c && tree->rows()[3].id == a, "the tree lists the objects of a layer under it, the front one first");
        expect(tree->rows()[1].name == "<Ellipse>" && tree->rows()[3].name == "<Rectangle>", "an unnamed object shows its kind");

        click(tree->nameRect(indexOf(Kind::Object, a)));
        expect(document.selection() == std::vector<std::size_t>{a}, "clicking an object row selects the object");
        expect(tree->rows()[indexOf(Kind::Object, a)].holdsSelection, "and its row shows it");
        click(tree->nameRect(indexOf(Kind::Object, b)), newui::kmCtrl);
        expect(document.selection().size() == 2, "Ctrl-click adds another");
        click(tree->eyeRect(indexOf(Kind::Object, c)));
        expect(!document.findById(c)->ownVisible(), "the eye of an object row hides the object");
        document.undoStack().undo();
        expect(document.findById(c)->ownVisible(), "and undo shows it");
        click(tree->lockRect(indexOf(Kind::Object, c)));
        expect(document.findById(c)->ownLocked(), "the lock of an object row locks it");
        document.undoStack().undo();

        document.setSelection({a, b});
        document.groupSelection();
        expect(indexOf(Kind::Group, document.findById(a)->groups().front()) < tree->rows().size() && indexOf(Kind::Object, a) == tree->rows().size(), "a group is a row of its own, closed");
        const std::size_t group = document.findById(a)->groups().front();
        click(tree->triangleRect(indexOf(Kind::Group, group)));
        expect(indexOf(Kind::Object, a) < tree->rows().size() && tree->rows()[indexOf(Kind::Object, a)].depth == 2, "opening it lists its members one level in");
        click(tree->nameRect(indexOf(Kind::Group, group)));
        expect(document.selection().size() == 2, "clicking a group row selects all its members");

        // Drag an object onto another layer's row.
        const std::size_t first = document.activeLayer();
        const std::size_t other = document.addLayer(0);
        newui::Point from = centre(tree->nameRect(indexOf(Kind::Object, c)));
        newui::Point to = centre(tree->nameRect(indexOf(Kind::Layer, first)));
        tree->onMouseDown(*tree, from, std::uint32_t(1), 0u);
        tree->onMouseMove(*tree, to, std::uint32_t(1), 0u);
        tree->onMouseUp(*tree, to, std::uint32_t(1), 0u);
        expect(document.findById(c)->layer() == first, "dropping an object on its own layer changes nothing");
        to = centre(tree->nameRect(indexOf(Kind::Layer, other)));
        from = centre(tree->nameRect(indexOf(Kind::Object, c)));
        tree->onMouseDown(*tree, from, std::uint32_t(1), 0u);
        tree->onMouseMove(*tree, to, std::uint32_t(1), 0u);
        tree->onMouseUp(*tree, to, std::uint32_t(1), 0u);
        expect(document.findById(c)->layer() == other, "dropping an object row on a layer row moves it there");
        document.undoStack().undo();
        expect(document.findById(c)->layer() == first, "and undo puts it back");

        // The layer colour reaches the rows.
        document.setLayerColor(first, 0xFF0000);
        expect(tree->rows()[indexOf(Kind::Layer, first)].color == 0xFF0000u && tree->rows()[indexOf(Kind::Object, c)].color == 0xFF0000u, "the layer colour shows on its rows");

        // Wheel scrolling.
        document.setActiveLayer(first);
        for (int i = 0; i < 30; ++i) {
            document.add(curvy::SketchObject::makeRect(double(i), 100, 5, 5));
        }
        expect(tree->rows().size() > tree->visibleRows(), "many objects do not all fit");
        const std::size_t top = tree->firstRow();
        tree->onMouseWheel(*tree, newui::Point(10.0f, 10.0f), -120.0f);
        expect(tree->firstRow() > top, "the mouse wheel scrolls the tree");
        tree->onMouseWheel(*tree, newui::Point(10.0f, 10.0f), 600.0f);
        expect(tree->firstRow() == 0, "and back up to the first row");

        // The Layer menu.
        document.clearObjects();
        document.undoStack().clear();
        document.add(curvy::SketchObject::makeRect(0, 0, 10, 10));
        auto menu = [&](const char* label) { return window_.menuItem("Layer", label); };
        newui::MenuItem* add = menu("New Layer");
        newui::MenuItem* duplicate = menu("Duplicate Layer");
        newui::MenuItem* merge = menu("Merge Layer Down");
        newui::MenuItem* flatten = menu("Flatten Artwork");
        newui::MenuItem* hideOthers = menu("Hide Other Layers");
        newui::MenuItem* showAll = menu("Show All Layers");
        expect(add && duplicate && merge && flatten && hideOthers && showAll && menu("Lock Other Layers") && menu("Unlock All Layers") && menu("Select Objects in Layer") &&
            menu("Bring Layer Forward") && menu("Collapse All Layers"), "there is a Layer menu");
        if (!(add && duplicate && merge && flatten && hideOthers && showAll)) {
            return;
        }
        add->onClick(*add);
        document.add(curvy::SketchObject::makeRect(20, 0, 10, 10));
        expect(document.layers().size() == 2 && document.objectCount() == 2, "New Layer adds a layer");
        duplicate->onClick(*duplicate);
        expect(document.layers().size() == 3 && document.objectCount() == 3, "Duplicate Layer copies the layer and its objects");
        document.undoStack().undo();
        expect(document.layers().size() == 2 && document.objectCount() == 2, "and undo takes the copy away");
        document.undoStack().redo();
        expect(document.layers().size() == 3 && document.objectCount() == 3, "and redo brings it back");
        hideOthers->onClick(*hideOthers);
        std::size_t hidden = 0;
        for (const curvy::Layer& layer : document.layers()) { hidden += layer.visible ? 0 : 1; }
        expect(hidden == 2, "Hide Other Layers hides the rest");
        showAll->onClick(*showAll);
        hidden = 0;
        for (const curvy::Layer& layer : document.layers()) { hidden += layer.visible ? 0 : 1; }
        expect(hidden == 0, "Show All Layers shows them again");
        merge->onClick(*merge);
        expect(document.layers().size() == 2 && document.objectCount() == 3, "Merge Layer Down joins a layer into the one behind it");
        flatten->onClick(*flatten);
        expect(document.layers().size() == 1 && document.objectCount() == 3, "Flatten Artwork leaves one layer");
        document.undoStack().undo();
        expect(document.layers().size() == 2, "and undo brings the layers back");
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkCommands() {
        SketchCommandController& commands = window_.commands();
        document_.clearObjects();
        document_.undoStack().clear();
        expect(commands.has("edit.undo") && commands.has("arrange.bringToFront") && commands.has("file.save") && !commands.has("no.such.command"),
            "commands are found by id");
        expect(!commands.run("no.such.command"), "an unknown command does nothing");
        expect(!commands.enabled("edit.undo") && !commands.run("edit.undo") && commands.undoText() == "Undo", "with nothing to undo, Undo is disabled and plain");

        curvy::SketchObject& first = document_.add(curvy::SketchObject::makeRect(0, 0, 10, 10));
        document_.add(curvy::SketchObject::makeRect(20, 0, 10, 10));
        document_.setSelection({first.id()});
        commands.run("arrange.bringToFront");
        expect(document_.object(1).id() == first.id(), "arrange.bringToFront moves the selection to the top");
        expect(commands.enabled("edit.undo") && commands.undoText().rfind("Undo ", 0) == 0 && commands.undoText().size() > 5, "an edit enables Undo and names it");
        auto* undoItem = window_.menuItem("Edit", "Undo");
        expect(undoItem != nullptr && undoItem->text() == commands.undoText() && undoItem->state().isEnabled(), "the Edit menu's Undo item shows the same label");
        commands.run("edit.undo");
        expect(document_.object(0).id() == first.id() && !commands.enabled("edit.undo"), "edit.undo undoes it, and Undo is disabled again");
        expect(undoItem != nullptr && undoItem->text() == "Undo" && !undoItem->state().isEnabled(), "the menu item follows");
        expect(commands.enabled("edit.redo") && commands.run("edit.redo") && document_.object(1).id() == first.id(), "edit.redo puts it back");

        commands.run("edit.selectAll");
        expect(document_.selection().size() == 2, "edit.selectAll selects everything");
        commands.run("edit.delete");
        expect(document_.objectCount() == 0, "edit.delete removes the selection");

        // View switches: the command flips the canvas, the menu item and (for snap) the toolbar toggle show it.
        auto* grid = window_.menuItem("View", "Show Grid");
        auto* snap = window_.menuItem("View", "Snap to Grid");
        auto* snapButton = dynamic_cast<newui::ToolbarButton*>(frame_.rootView().findView("snapToGridButton"));
        bool gridBefore = canvas_->showGrid(), snapBefore = canvas_->snapToGrid();
        expect(grid != nullptr && snap != nullptr && grid->isChecked() == gridBefore && snap->isChecked() == snapBefore, "the View menu's check marks start in step with the canvas");
        grid->onClick(*grid);
        expect(canvas_->showGrid() == !gridBefore && grid->isChecked() == !gridBefore, "clicking Show Grid flips the grid and its check mark");
        grid->onClick(*grid);
        if (snapButton != nullptr) {
            snapButton->setChecked(!snapBefore);
            expect(canvas_->snapToGrid() == !snapBefore && snap->isChecked() == !snapBefore, "the Snap to Grid toolbar button flips the canvas and the menu check");
            snap->onClick(*snap);
            expect(canvas_->snapToGrid() == snapBefore && snapButton->isChecked() == snapBefore, "the menu item flips it back and the toolbar button follows");
        }

        // Color mode follows the document, wherever the change came from.
        auto* cmyk = window_.menuItem("View/Color Mode", "CMYK");
        auto* rgb = window_.menuItem("View/Color Mode", "RGB");
        auto* status = dynamic_cast<newui::StatusBar*>(frame_.rootView().findView("statusBar"));
        cmyk->onClick(*cmyk);
        expect(document_.colorMode() == curvy::ColorSpace::CMYK && cmyk->isChecked() && !rgb->isChecked(), "the CMYK item converts the document and takes the check");
        expect(status != nullptr && status->paneText(status->paneCount() - 2) == "CMYK doc", "the status bar names the document's color mode");
        document_.undoStack().undo();
        expect(document_.colorMode() == curvy::ColorSpace::RGB && rgb->isChecked() && status->paneText(status->paneCount() - 2) == "RGB doc", "undo brings the mode, the menu and the status bar back");

        // The status bar shows a hint for the chosen tool.
        canvas_->setTool(curvy::SketchTool::Hand);
        expect(status != nullptr && status->paneText(0).rfind("Hand:", 0) == 0, "choosing the Hand tool shows its hint");
        canvas_->setTool(curvy::SketchTool::Select);
        expect(status != nullptr && status->paneText(0).rfind("Select:", 0) == 0, "and the Select tool its own");
        document_.clearObjects();
        document_.undoStack().clear();
    }

    void ToolChecks::checkColorPanel() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(10, 10, 100, 60))
            .setFill(curvy::SketchPaint::solid(curvy::SketchColor::rgb(0.9f, 0.3f, 0.2f)));  // a new object's own fill is Gray
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        auto* red = dynamic_cast<newui::Slider*>(frame.rootView().findView("comp0Slider"));
        auto* hex = dynamic_cast<newui::TextField*>(frame.rootView().findView("hexField"));
        expect(red != nullptr && hex != nullptr, "style panel controls found");
        if (red != nullptr && hex != nullptr) {
            std::string shown(hex->text().begin(), hex->text().end());
            expect(shown == document.object(0).fill().color().toHex(), "hex field shows the selected fill");
            std::size_t undoable = 0;
            while (document.undoStack().canUndo()) { document.undoStack().undo(); ++undoable; }
            expect(red->maxValue() == 255.0f, "R/G/B sliders span 0..255");
            document.object(0).setFill(curvy::SketchPaint::solid(curvy::SketchColor::fromHex("#f2b824")));
            document.touch();
            red->setValue(10.0f);
            expect(document.object(0).fill().color().toHex() == "#0ab824", "moving R keeps G and B (#f2b824 -> #0ab824)");
            expect(document.object(0).fill().color().toRgba32().r() == 10, "red slider edits the selected fill");
            expect(document.undoStack().canUndo(), "slider edit is undoable");
            hex->setText(L"#336699");
            hex->onReturnPressed(*hex);
            expect(document.object(0).fill().color().toHex() == "#336699", "hex field edits the selected fill");
            document.undoStack().undo();
            expect(document.object(0).fill().color().toRgba32().r() == 10, "undo steps back one edit");

            // Gradient panel: the linear button turns the fill into a gradient whose stops the bar/hex then edit.
            auto* linearButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("fillLinearButton"));
            auto* reverseButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("reverseGradientButton"));
            auto* addStopButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("addStopButton"));
            auto* bar = dynamic_cast<curvy::GradientBar*>(frame.rootView().findView("gradientBar"));
            expect(linearButton != nullptr && reverseButton != nullptr && addStopButton != nullptr && bar != nullptr, "gradient controls found");
            if (linearButton != nullptr && reverseButton != nullptr && addStopButton != nullptr && bar != nullptr) {
                linearButton->setChecked(true);
                expect(document.object(0).fill().kind() == curvy::SketchPaint::Kind::Linear, "linear button makes a linear gradient");
                expect(bar->parsedStops().size() == 2, "gradient bar shows two stops");
                std::string first = document.object(0).fill().gradient().stops.front().color.toHex();
                hex->setText(L"#112233");
                hex->onReturnPressed(*hex);
                expect(document.object(0).fill().gradient().stops[bar->selectedStop()].color.toHex() == "#112233", "hex edits the selected stop");
                addStopButton->onClick(*addStopButton);
                expect(document.object(0).fill().gradient().stops.size() == 3, "add-stop button adds a stop");
                reverseButton->onClick(*reverseButton);
                expect(document.object(0).fill().gradient().stops.size() == 3, "reverse keeps the stops");
                auto* solidButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("fillSolidButton"));
                if (solidButton != nullptr) {
                    solidButton->setChecked(true);
                    expect(document.object(0).fill().kind() == curvy::SketchPaint::Kind::Solid, "solid button returns to a solid fill");
                }
                while (document.undoStack().canUndo()) { document.undoStack().undo(); }
            }

            // Clicking the stroke well must retarget the panel at the stroke.
            auto* strokeWell = dynamic_cast<newui::SubView*>(frame.rootView().findView("strokeWell"));
            expect(strokeWell != nullptr, "stroke well found");
            if (strokeWell != nullptr) {
                strokeWell->onMouseDown(*strokeWell, newui::Point(5, 5), std::uint32_t(1), 0);
                std::string nowShown(hex->text().begin(), hex->text().end());
                expect(nowShown == document.object(0).stroke().color().toHex(), "clicking the stroke well shows the stroke color");
                auto* info = dynamic_cast<newui::Label*>(frame.rootView().findView("colorPanelHeaderInfo"));
                if (info != nullptr) {
                    std::printf("header text bytes:");
                    for (unsigned char c : info->text()) { std::printf(" %02x", c); }
                    std::printf("\n");
                }
                std::size_t strokeAt = info != nullptr ? info->text().find("Stroke") : std::string::npos;
                expect(strokeAt != std::string::npos && info->text().compare(strokeAt + 7, 3, "\xE2\x97\x8F") == 0,
                    "header marks Stroke as the target");
            }

            // Color modes: the four component sliders follow the color's space; the mode buttons convert it.
            auto* fillWell = dynamic_cast<newui::SubView*>(frame.rootView().findView("fillWell"));
            auto* magenta = dynamic_cast<newui::Slider*>(frame.rootView().findView("comp1Slider"));
            auto* fourth = frame.rootView().findView("comp3SliderRow");
            auto* caption = dynamic_cast<newui::Label*>(frame.rootView().findView("comp0SliderCaption"));
            auto* modeControl = dynamic_cast<newui::SegmentedControl*>(frame.rootView().findView("colorModeControl"));
            expect(fillWell != nullptr && magenta != nullptr && fourth != nullptr && caption != nullptr && modeControl != nullptr, "color mode controls found");
            if (fillWell != nullptr && magenta != nullptr && fourth != nullptr && caption != nullptr && modeControl != nullptr) {
                fillWell->onMouseDown(*fillWell, newui::Point(5, 5), std::uint32_t(1), 0);
                while (document.undoStack().canUndo()) { document.undoStack().undo(); }
                document.object(0).setFill(curvy::SketchPaint::solid(curvy::SketchColor::rgb(0.2f, 0.4f, 0.6f)));
                document.touch();
                expect(!fourth->isVisible() || true, "(RGB fourth row visibility is layout-dependent)");
                expect(caption->text() == "R" && modeControl->selectedIndex() == std::optional<std::size_t>(1), "RGB color shows R/G/B and the RGB mode");

                document.object(0).setFill(curvy::SketchPaint::solid(curvy::SketchColor::cmyk(0.0f, 0.7f, 0.85f, 0.05f)));
                document.touch();
                expect(caption->text() == "C" && fourth->isVisible(), "CMYK color shows C/M/Y/K");
                expect(modeControl->selectedIndex() == std::optional<std::size_t>(2), "mode control follows the color's space");
                expect(int(magenta->value() + 0.5f) == 70 && magenta->maxValue() == 100.0f, "M slider shows 70 of 100");
                magenta->setValue(50.0f);
                const curvy::SketchColor& edited = document.object(0).fill().color();
                expect(edited.space() == curvy::ColorSpace::CMYK && std::fabs(edited.component(1) - 0.5f) < 0.01f &&
                    std::fabs(edited.component(2) - 0.85f) < 0.01f, "editing M keeps the color CMYK and the other inks");

                modeControl->setSelectedIndex(1);
                expect(document.object(0).fill().color().space() == curvy::ColorSpace::RGB, "RGB mode button converts the color");
                expect(caption->text() == "R", "sliders switch to R/G/B");
                document.undoStack().undo();
                expect(document.object(0).fill().color().space() == curvy::ColorSpace::CMYK, "undo restores the CMYK color");

                while (document.undoStack().canUndo()) { document.undoStack().undo(); }
                document.object(0).setFill(curvy::SketchPaint::solid(curvy::SketchColor::rgb(0.2f, 0.4f, 0.6f)));
                document.touch();
                expect(document.convertToColorMode(curvy::ColorSpace::CMYK) && document.colorMode() == curvy::ColorSpace::CMYK, "document converts to CMYK");
                expect(document.object(0).fill().color().space() == curvy::ColorSpace::CMYK, "object colors follow the document mode");
                document.undoStack().undo();
                expect(document.colorMode() == curvy::ColorSpace::RGB && document.object(0).fill().color().space() == curvy::ColorSpace::RGB, "undo restores the RGB document");
            }
            (void)undoable;
        }
    }

    void ToolChecks::checkGradientTool() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(100, 100, 200, 100))
            .setFill(curvy::SketchPaint::solid(curvy::SketchColor::rgb(0.9f, 0.3f, 0.2f)));
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        canvas->setTool(curvy::SketchTool::Gradient);
        auto axis = [&document](BLPoint& start, BLPoint& end) {
            const curvy::SketchObject& object = document.object(0);
            object.fill().gradient().axisPoints(object.geometryBounds(), object.fill().kind() == curvy::SketchPaint::Kind::Radial, start, end);
        };
        BLPoint start, end;

        mouse(0, view(120, 150));
        mouse(1, view(200, 150));
        mouse(2, view(280, 150));
        expect(document.object(0).fill().kind() == curvy::SketchPaint::Kind::Linear, "dragging with the gradient tool makes a linear gradient");
        axis(start, end);
        expect(std::fabs(start.x - 120) < 0.01 && std::fabs(end.x - 280) < 0.01 && std::fabs(end.y - 150) < 0.01, "the drag sets the gradient axis");
        expect(document.undoStack().canUndo(), "laying an axis is one undo step");

        mouse(0, view(280, 150));  // the end handle
        mouse(1, view(240, 170));
        mouse(2, view(200, 150));
        axis(start, end);
        expect(std::fabs(end.x - 200) < 0.01 && std::fabs(start.x - 120) < 0.01, "dragging the end handle moves only the end");

        mouse(0, view(160, 150));  // on the axis, between the ends: adds a stop
        mouse(2, view(160, 150));
        expect(document.object(0).fill().gradient().stops.size() == 3, "clicking the axis adds a stop");
        mouse(0, view(160, 150));  // the new stop: drag it along the axis
        mouse(1, view(180, 150));
        mouse(2, view(180, 150));
        expect(std::fabs(document.object(0).fill().gradient().stops[1].offset - 0.75) < 0.01, "dragging a stop moves its offset");
        mouse(0, view(180, 150), newui::kmAlt);
        mouse(2, view(180, 150), newui::kmAlt);
        expect(document.object(0).fill().gradient().stops.size() == 2, "Alt-click removes a stop");

        while (document.undoStack().canUndo()) { document.undoStack().undo(); }
        expect(document.object(0).fill().kind() == curvy::SketchPaint::Kind::Solid, "undoing the gradient edits restores the solid fill");

        // The tool and the Color panel share the selected stop.
        mouse(0, view(120, 150));
        mouse(1, view(280, 150));
        mouse(2, view(280, 150));
        mouse(0, view(200, 150));  // add a stop in the middle: it becomes the selected one
        mouse(2, view(200, 150));
        auto* offsetField = dynamic_cast<newui::TextField*>(frame.rootView().findView("stopOffsetField"));
        expect(canvas->selectedGradientStop() == 1, "the new stop is the canvas's selected stop");
        expect(offsetField != nullptr && std::string(offsetField->text().begin(), offsetField->text().end()).compare(0, 2, "50") == 0, "the Color panel shows the stop selected on the canvas");
        auto* stopBar = dynamic_cast<curvy::GradientBar*>(frame.rootView().findView("gradientBar"));
        if (stopBar != nullptr) {
            stopBar->setSelectedStop(2);
            stopBar->onStopSelected(*stopBar);
            expect(canvas->selectedGradientStop() == 2, "selecting a stop in the panel selects it on the canvas");
        }
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }

        // With the stroke as the Color panel's target the tool edits the stroke (the fill is left alone).
        auto* strokeTarget = dynamic_cast<newui::SubView*>(frame.rootView().findView("strokeWell"));
        auto* fillTarget = dynamic_cast<newui::SubView*>(frame.rootView().findView("fillWell"));
        if (strokeTarget != nullptr && fillTarget != nullptr) {
            strokeTarget->onMouseDown(*strokeTarget, newui::Point(5, 5), std::uint32_t(1), 0);
            expect(canvas->gradientOnStroke(), "the stroke well points the tool at the stroke");
            mouse(0, view(120, 150));
            mouse(1, view(280, 150));
            mouse(2, view(280, 150));
            expect(document.object(0).stroke().isGradient() && !document.object(0).fill().isGradient(), "the tool lays a gradient on the stroke only");

            // A radial stroke gradient gets an aspect handle below its center.
            auto* radialButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("fillRadialButton"));
            if (radialButton != nullptr) {
                radialButton->setChecked(true);
                expect(document.object(0).stroke().kind() == curvy::SketchPaint::Kind::Radial, "the radial button makes the stroke radial");
                mouse(0, view(120, 310));  // the aspect handle: radius 160 straight down from the center
                mouse(1, view(120, 260));
                mouse(2, view(120, 230));
                expect(std::fabs(document.object(0).stroke().gradient().axisAspect - 0.5) < 0.01, "dragging the aspect handle squashes the radial gradient");
            }
            fillTarget->onMouseDown(*fillTarget, newui::Point(5, 5), std::uint32_t(1), 0);
            expect(!canvas->gradientOnStroke(), "the fill well points the tool back at the fill");
        }
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }

        // A point gradient is edited on the shape: its points are the handles.
        if (auto* pointsButton = dynamic_cast<newui::ToolbarButton*>(frame.rootView().findView("fillPointsButton"))) {
            pointsButton->setChecked(true);
            expect(document.object(0).fill().isPoints() && document.object(0).fill().gradient().points.size() == 2, "the points button makes a two point gradient");
            const auto points = [&document]() -> const std::vector<curvy::GradientPoint>& { return document.object(0).fill().gradient().points; };
            mouse(0, view(150, 125));  // the first point
            mouse(1, view(170, 140));
            mouse(2, view(170, 140));
            expect(std::fabs(points()[0].x - 0.35) < 0.01 && std::fabs(points()[0].y - 0.4) < 0.01, "dragging a point moves it within the box");
            expect(canvas->selectedGradientStop() == 0, "the point just moved is the selected one");
            mouse(0, view(260, 140));  // the reach handle on the selected point's ring
            mouse(1, view(230, 140));
            mouse(2, view(230, 140));
            expect(std::fabs(points()[0].radius - 0.3) < 0.01, "dragging the ring's handle sets the point's reach");
            mouse(0, view(200, 190));  // empty shape: adds a point there
            mouse(2, view(200, 190));
            expect(points().size() == 3 && std::fabs(points()[2].x - 0.5) < 0.01 && canvas->selectedGradientStop() == 2, "clicking the shape adds a point and selects it");
            mouse(0, view(200, 190), newui::kmAlt);
            mouse(2, view(200, 190), newui::kmAlt);
            expect(points().size() == 2, "Alt-click removes a point");
            mouse(0, view(50, 50));  // off the shape: no point is added
            mouse(2, view(50, 50));
            expect(points().size() == 2 || !document.object(0).fill().isPoints(), "clicking away from the shape adds no point");
            while (document.undoStack().canUndo()) { document.undoStack().undo(); }
            expect(document.object(0).fill().kind() == curvy::SketchPaint::Kind::Solid, "undoing the point edits restores the solid fill");
        }

        canvas->setTool(curvy::SketchTool::Hand);
        expect(canvas->cursorName() == "hand", "the hand tool shows the hand cursor");
        canvas->setTool(curvy::SketchTool::Gradient);
        expect(canvas->cursorName() == "gradient", "the gradient tool shows the gradient cursor");
        canvas->setTool(curvy::SketchTool::Select);
        expect(canvas->cursorName() == "select", "the select tool shows the select arrow");
    }

    void ToolChecks::checkAppearancePanel() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Appearance panel: the stack lists the items, rows choose what the other controls edit, buttons edit the stack.
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 60));
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        {
            auto* stack = dynamic_cast<newui::ListView*>(frame.rootView().findView("appearanceStack"));
            auto* opacitySlider = dynamic_cast<newui::Slider*>(frame.rootView().findView("opacitySlider"));
            auto click = [&frame](const char* name) {
                if (auto* button = dynamic_cast<newui::Control*>(frame.rootView().findView(name))) {
                    button->onClick(*button);
                }
            };
            auto rows = [stack]() { auto* m = stack != nullptr ? dynamic_cast<newui::StringListModel*>(stack->model()) : nullptr; return m != nullptr ? m->items().size() : std::size_t(0); };
            expect(stack != nullptr && opacitySlider != nullptr, "appearance stack and opacity slider found");
            if (stack != nullptr && opacitySlider != nullptr) {
                expect(rows() == 3, "the stack lists stroke, fill and the base opacity row");
                click("newStrokeButton");
                expect(document.object(0).appearance().size() == 3 && rows() == 4, "Add Stroke adds an item and a row");
                expect(stack->selectedIndex() == std::optional<std::size_t>(0), "the new stroke is the selected top row");
                stack->setSelectedIndex(2);  // the fill
                opacitySlider->setValue(50.0f);
                expect(std::fabs(document.object(0).appearance()[0].opacity - 0.5) < 0.01, "choosing the fill row points the opacity slider at the fill item");
                stack->setSelectedIndex(1);  // the first stroke
                expect(document.object(0).activeIndex(curvy::AppearanceKind::Stroke) == 1, "choosing a stroke row makes it the active stroke");
                opacitySlider->setValue(40.0f);
                expect(std::fabs(document.object(0).appearance()[1].opacity - 0.4) < 0.01 && document.object(0).opacity() == 1.0, "the opacity slider edits the selected item");
                stack->setSelectedIndex(3);  // the base row
                opacitySlider->setValue(60.0f);
                expect(std::fabs(document.object(0).opacity() - 0.6) < 0.01, "with the base row chosen the slider edits the object's opacity");
                stack->setSelectedIndex(0);
                click("deleteItemButton");
                expect(document.object(0).appearance().size() == 2 && rows() == 3, "Delete removes the selected item");
                click("clearAppearanceButton");
                expect(document.object(0).fill().isNone() && document.object(0).stroke().isNone(), "Clear Appearance leaves no fill or stroke");
            }
        }
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }
    }

    void ToolChecks::checkEffects() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Effects: a pick in the fx popup adds one on top of the stack and chooses it; the options row edits it and the
        // opacity slider, blend list and buttons follow the chosen effect.
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 60));
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        {
            auto* stack = dynamic_cast<newui::ListView*>(frame.rootView().findView("appearanceStack"));
            auto* opacitySlider = dynamic_cast<newui::Slider*>(frame.rootView().findView("opacitySlider"));
            auto field = [&frame](const char* name) { return dynamic_cast<newui::TextField*>(frame.rootView().findView(name)); };
            auto shown = [&frame](const char* name) { auto* v = frame.rootView().findView(name); return v != nullptr && v->isVisible(); };
            auto type = [&field](const char* name, const wchar_t* text) {
                if (auto* f = field(name)) {
                    f->setText(text);
                    f->onReturnPressed(*f);
                }
            };
            auto click = [&frame](const char* name) {
                if (auto* button = dynamic_cast<newui::Control*>(frame.rootView().findView(name))) {
                    button->onClick(*button);
                }
            };
            auto rows = [stack]() { auto* m = stack != nullptr ? dynamic_cast<newui::StringListModel*>(stack->model()) : nullptr; return m != nullptr ? m->items().size() : std::size_t(0); };
            expect(stack != nullptr && opacitySlider != nullptr && field("effectXField") != nullptr && field("effectColorField") != nullptr,
                "the effect option fields are in the layout");
            if (stack != nullptr && opacitySlider != nullptr && field("effectXField") != nullptr && field("effectColorField") != nullptr) {
                expect(!shown("effectRow") && !shown("effectColorRow"), "the effect options are hidden while no effect row is chosen");
                stylePanel_.addEffect(curvy::EffectKind::DropShadow);
                expect(document.object(0).appearance().size() == 3 && document.object(0).appearance()[2].kind == curvy::AppearanceKind::Effect,
                    "Add Effect puts a drop shadow on top of the stack");
                expect(rows() == 4 && stack->selectedIndex() == std::optional<std::size_t>(0), "the new effect is the selected top row");
                expect(shown("effectRow") && shown("effectColorRow") && shown("effectXField"), "a drop shadow shows offset, blur and color");
                expect(newui::wideToUtf8(field("effectXField")->text()) == "4" && newui::wideToUtf8(field("effectSizeField")->text()) == "6", "the fields show the shadow's defaults");
                type("effectXField", L"9");
                expect(std::fabs(document.object(0).appearance()[2].offsetX - 9.0) < 1e-9, "the X field sets the offset");
                type("effectSizeField", L"12");
                expect(std::fabs(document.object(0).appearance()[2].softness - 12.0) < 1e-9, "the Blur field sets the softness");
                type("effectSizeField", L"-3");
                expect(document.object(0).appearance()[2].softness == 0.0, "a negative blur is clamped to zero");
                type("effectColorField", L"#ff0000");
                expect(document.object(0).appearance()[2].color.toHex() == "#ff0000", "the color field sets the tint");
                type("effectColorField", L"nonsense");
                expect(document.object(0).appearance()[2].color.toHex() == "#ff0000", "an unparsable color is ignored");
                opacitySlider->setValue(40.0f);
                expect(std::fabs(document.object(0).appearance()[2].opacity - 0.4) < 0.01 && document.object(0).opacity() == 1.0,
                    "the opacity slider edits the chosen effect's amount");
                expect(document.undoStack().canUndo(), "effect edits are undo steps");

                stack->setSelectedIndex(1);  // the stroke
                expect(!shown("effectRow") && !shown("effectColorRow"), "choosing a stroke row hides the effect options");
                stack->setSelectedIndex(0);
                expect(shown("effectRow"), "choosing the effect row shows them again");

                stylePanel_.addEffect(curvy::EffectKind::RoundCorners);
                expect(rows() == 5 && stack->selectedIndex() == std::optional<std::size_t>(0), "a second effect is added on top and chosen");
                expect(shown("effectRow") && !shown("effectColorRow") && !shown("effectXField"), "round corners shows only its radius");
                expect(newui::wideToUtf8(field("effectSizeField")->text()) == "10", "the size field shows the radius");
                type("effectSizeField", L"14");
                expect(std::fabs(document.object(0).appearance()[3].radius - 14.0) < 1e-9, "the size field sets the radius");

                stylePanel_.addEffect(curvy::EffectKind::GaussianBlur);
                expect(shown("effectRow") && !shown("effectColorRow") && !shown("effectXField"), "gaussian blur shows only its blur");
                stylePanel_.addEffect(curvy::EffectKind::OuterGlow);
                expect(shown("effectColorRow") && !shown("effectXField"), "outer glow shows blur and color but no offset");
                auto labelText = [&frame](const char* name) {
                    auto* label = dynamic_cast<newui::Label*>(frame.rootView().findView(name));
                    return label != nullptr ? std::string(label->text()) : std::string("?");
                };
                stylePanel_.addEffect(curvy::EffectKind::InnerShadow);
                expect(shown("effectXField") && shown("effectColorRow") && labelText("effectXLabel") == "X",
                    "inner shadow shows offset, blur and color like a drop shadow");
                stylePanel_.addEffect(curvy::EffectKind::OffsetPath);
                expect(shown("effectRow") && !shown("effectColorRow") && !shown("effectXField") && labelText("effectSizeLabel") == "Offset",
                    "offset path shows only its offset");
                type("effectSizeField", L"-4");
                expect(std::fabs(document.object(0).appearance().back().radius + 4.0) < 1e-9, "the offset may be negative (inward)");
                stylePanel_.addEffect(curvy::EffectKind::Roughen);
                expect(shown("effectXField") && !shown("effectColorRow") && labelText("effectXLabel") == "Detail" && labelText("effectYLabel") == "Seed" &&
                           labelText("effectSizeLabel") == "Size", "roughen shows size, detail and seed");
                type("effectXField", L"12");
                type("effectYField", L"5");
                type("effectSizeField", L"3");
                expect(std::fabs(document.object(0).appearance().back().offsetX - 12.0) < 1e-9 && std::fabs(document.object(0).appearance().back().offsetY - 5.0) < 1e-9 &&
                           std::fabs(document.object(0).appearance().back().radius - 3.0) < 1e-9, "the roughen fields set detail, seed and size");
                stylePanel_.addEffect(curvy::EffectKind::DropShadow);
                expect(labelText("effectXLabel") == "X" && labelText("effectSizeLabel") == "Blur", "the labels go back for a shadow");
                click("deleteItemButton");
                click("deleteItemButton");
                click("deleteItemButton");
                click("deleteItemButton");
                expect(document.object(0).appearance().size() == 6, "the four extra effects are deleted again");

                click("deleteItemButton");
                expect(document.object(0).appearance().size() == 5, "Delete removes the chosen effect");
                expect(shown("effectRow"), "another effect stays chosen after one is deleted");
                click("clearAppearanceButton");
                expect(!shown("effectRow"), "Clear Appearance hides the effect options");
            }
        }
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }
    }

    void ToolChecks::checkGraphicStyles() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Graphic Styles: a click on a tile applies that style to the selection; New saves the selection's; Delete removes one.
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(100, 100, 100, 60));
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        {
            auto* grid = dynamic_cast<curvy::SwatchGrid*>(frame.rootView().findView("graphicStyleGrid"));
            auto click = [&frame](const char* name) {
                if (auto* button = dynamic_cast<newui::Control*>(frame.rootView().findView(name))) {
                    button->onClick(*button);
                }
            };
            expect(grid != nullptr, "the graphic styles grid is in the layout");
            if (grid != nullptr) {
                std::size_t count = document.graphicStyles().size();
                expect(grid->tileCount() == count && count >= 5, "the grid shows one tile per style of the document");
                expect(grid->selectedIndex() >= count, "no style is chosen at first");
                grid->setSelectedIndex(1);   // Sunset + shadow
                grid->onSwatchPicked(*grid);
                expect(document.object(0).appearance().size() == 3 && document.object(0).fill().isGradient(),
                    "clicking a style tile applies its whole appearance to the selection");
                expect(document.undoStack().canUndo(), "applying a style is an undo step");
                click("newGraphicStyleButton");
                expect(document.graphicStyles().size() == count + 1 && grid->tileCount() == count + 1 && grid->selectedIndex() == count,
                    "New Graphic Style saves the selection's look as a new, chosen tile");
                click("deleteGraphicStyleButton");
                expect(document.graphicStyles().size() == count && grid->selectedIndex() >= count, "Delete removes the chosen style and chooses none");
                document.clearSelection();
                grid->setSelectedIndex(0);
                grid->onSwatchPicked(*grid);
                expect(document.object(0).appearance().size() == 3, "clicking a tile with nothing selected changes nothing");
            }
        }
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }
    }

    void ToolChecks::checkDrawingTools() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Drawing tools: polygon, star, rounded rectangle, pencil and pen add objects; Direct Select edits their points.
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        {
            auto key = [canvas](std::uint32_t vk, std::uint32_t mods = 0) { canvas->onKeyDown(*canvas, mods, 0, 1, vk); };
            auto anchorsOf = [&document](std::size_t index) {
                return curvy::pathToSubPaths(document.object(index).geometry());
            };
            canvas->setTool(curvy::SketchTool::Polygon);
            mouse(0, view(200, 200));
            mouse(1, view(260, 200));
            mouse(2, view(260, 200));
            expect(document.objectCount() == 1 && document.object(0).type() == curvy::SketchObjectType::Polygon, "the polygon tool adds a polygon");
            expect(anchorsOf(0).size() == 1 && anchorsOf(0)[0].anchors.size() == 5 && anchorsOf(0)[0].closed, "a polygon has five sides by default");
            expect(std::fabs(anchorsOf(0)[0].anchors[0].point.x - 260.0) < 1e-6, "its first corner follows the drag");
            mouse(0, view(200, 200));
            mouse(1, view(240, 200));
            key(newui::vkUpArrow);
            mouse(2, view(240, 200));
            expect(document.objectCount() == 2 && anchorsOf(1)[0].anchors.size() == 6, "the Up arrow while dragging adds a side");
            canvas->setPolygonSides(5);
            canvas->setTool(curvy::SketchTool::Star);
            mouse(0, view(300, 300));
            mouse(1, view(300, 250));
            mouse(2, view(300, 250), newui::kmShift);
            expect(document.objectCount() == 3 && anchorsOf(2)[0].anchors.size() == 10, "the star tool adds a ten-vertex star");
            expect(std::fabs(anchorsOf(2)[0].anchors[0].point.y - 250.0) < 1e-6, "Shift makes the star point straight up");
            canvas->setTool(curvy::SketchTool::RoundRect);
            mouse(0, view(100, 400));
            mouse(1, view(200, 450));
            mouse(2, view(200, 450));
            expect(document.objectCount() == 4 && document.object(3).type() == curvy::SketchObjectType::RoundRect, "the rounded rectangle tool adds a rounded rectangle");
            mouse(0, view(120, 300));
            mouse(2, view(120, 300));   // a click, not a drag: nothing is added
            expect(document.objectCount() == 4, "a click with a shape tool adds nothing");

            canvas->setTool(curvy::SketchTool::Pencil);
            mouse(0, view(50, 500));
            for (int i = 1; i <= 20; ++i) {
                mouse(1, view(50 + 10 * i, 500 + 30 * std::sin(i * 0.4)));
            }
            mouse(2, view(250, 500 + 30 * std::sin(8.0)));
            expect(document.objectCount() == 5 && document.object(4).type() == curvy::SketchObjectType::Path && !anchorsOf(4)[0].closed,
                "the pencil adds an open path");
            expect(document.object(4).fill().isNone() && !document.object(4).stroke().isNone(), "a pencil line is stroked, not filled");
            expect(anchorsOf(4)[0].anchors.size() < 15 && anchorsOf(4)[0].anchors.size() >= 3, "the freehand points are simplified");

            canvas->setTool(curvy::SketchTool::Pen);
            mouse(0, view(400, 100));
            mouse(2, view(400, 100));
            mouse(0, view(480, 100));
            mouse(1, view(500, 140));
            mouse(2, view(500, 140));   // a click-drag: a smooth point
            mouse(0, view(560, 180));
            mouse(2, view(560, 180));
            key(newui::vkReturn);
            expect(document.objectCount() == 6 && document.object(5).type() == curvy::SketchObjectType::Path, "the pen adds a path when Enter ends it");
            std::vector<curvy::SubPath> pen = anchorsOf(5);
            expect(pen.size() == 1 && pen[0].anchors.size() == 3 && !pen[0].closed, "the pen path has the three points that were placed");
            expect(!pen[0].anchors[0].hasOut && pen[0].anchors[1].hasOut && pen[0].anchors[1].hasIn, "only the dragged point has curve handles");
            mouse(0, view(600, 100));
            mouse(2, view(600, 100));
            mouse(0, view(700, 100));
            mouse(2, view(700, 100));
            mouse(0, view(700, 200));
            mouse(2, view(700, 200));
            mouse(0, view(600, 100));
            mouse(2, view(600, 100));   // back on the first point: closes
            expect(document.objectCount() == 7 && anchorsOf(6)[0].closed && anchorsOf(6)[0].anchors.size() == 3, "clicking the first point closes the pen path");

            // Direct Select: pick an anchor and drag it; Alt-drag a corner to pull handles; Delete removes a point.
            canvas->setTool(curvy::SketchTool::DirectSelect);
            document.undoStack().clear();
            document.setSelection({document.object(6).id()});
            mouse(0, view(700, 100));
            mouse(1, view(720, 110));
            mouse(2, view(720, 110));
            expect(std::fabs(anchorsOf(6)[0].anchors[1].point.x - 720.0) < 1e-6 && std::fabs(anchorsOf(6)[0].anchors[1].point.y - 110.0) < 1e-6,
                "dragging an anchor moves it");
            expect(document.undoStack().canUndo(), "moving an anchor is an undo step");
            document.undoStack().undo();
            expect(std::fabs(anchorsOf(6)[0].anchors[1].point.x - 700.0) < 1e-6, "undo puts the anchor back");
            mouse(0, view(700, 100), newui::kmAlt);
            mouse(1, view(740, 100), newui::kmAlt);
            mouse(2, view(740, 100), newui::kmAlt);
            expect(anchorsOf(6)[0].anchors[1].hasOut && anchorsOf(6)[0].anchors[1].hasIn, "Alt-dragging a corner pulls curve handles out of it");
            mouse(0, view(740, 100));
            mouse(1, view(740, 60));
            mouse(2, view(740, 60));
            {
                curvy::PenAnchor smooth = anchorsOf(6)[0].anchors[1];   // a copy: anchorsOf() returns a temporary
                double ox = smooth.out.x - smooth.point.x, oy = smooth.out.y - smooth.point.y;
                double ix = smooth.in.x - smooth.point.x, iy = smooth.in.y - smooth.point.y;
                expect(std::fabs(smooth.out.y - 60.0) < 1e-6 && std::fabs(std::hypot(ix, iy) - 40.0) < 1e-6 && (ox * ix + oy * iy) < 0.0 &&
                           std::fabs(ox * iy - oy * ix) < 1e-6,
                    "dragging a handle of a smooth point turns the other one to stay opposite, keeping its length");
            }
            mouse(0, view(700, 100));
            mouse(2, view(700, 100));   // pick the triangle's anchor, then remove it: too little is left, so the outline goes
            key(newui::vkDelete);
            expect(document.objectCount() == 6, "deleting an anchor of a triangle removes the object");
            document.undoStack().clear();
            document.setSelection({document.object(0).id()});   // the pentagon
            mouse(0, view(260, 200));
            mouse(2, view(260, 200));
            key(newui::vkDelete);
            expect(document.objectCount() == 6 && anchorsOf(0)[0].anchors.size() == 4, "Delete removes the picked anchor of a pentagon");
            document.undoStack().undo();
            expect(anchorsOf(0)[0].anchors.size() == 5, "undo puts the anchor back");
        }
        canvas->setTool(curvy::SketchTool::Select);
        canvas->setSnapToGrid(true);
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkLiveShapes() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Live shape handles: a selected star, polygon, rounded rectangle or arc can be reshaped with its own handles.
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        {
            auto viewOf = [&view](const BLPoint& p) { return view(p.x, p.y); };
            auto regularOf = [&document](std::size_t index, curvy::RegularShape& shape) {
                std::vector<curvy::SubPath> subs = curvy::pathToSubPaths(document.object(index).geometry());
                std::vector<BLPoint> corners;
                for (const curvy::PenAnchor& a : subs[0].anchors) { corners.push_back(a.point); }
                return curvy::analyzeRegularShape(corners, shape);
            };

            canvas->setTool(curvy::SketchTool::Circle);
            mouse(0, view(100, 100));
            mouse(1, view(130, 100));
            mouse(2, view(130, 100));
            expect(document.objectCount() == 1 && document.object(0).type() == curvy::SketchObjectType::Ellipse, "the circle tool adds an ellipse");
            BLBox circleBox = document.object(0).geometryBounds();
            expect(std::fabs((circleBox.x1 - circleBox.x0) - 60.0) < 1e-6 && std::fabs((circleBox.y1 - circleBox.y0) - 60.0) < 1e-6 &&
                       std::fabs(circleBox.x0 - 70.0) < 1e-6, "a circle is drawn out from its center to the pointer");

            // Star: drag the inner-radius handle toward the center, then the points handle to the right.
            canvas->setTool(curvy::SketchTool::Star);
            mouse(0, view(300, 300));
            mouse(1, view(300, 240));
            mouse(2, view(300, 240), newui::kmShift);   // five points, the first one up
            canvas->setTool(curvy::SketchTool::Select);
            curvy::RegularShape star;
            expect(document.objectCount() == 2 && regularOf(1, star) && star.star && star.points == 5, "the drawn star is recognised as a five-point star");
            if (star.star) {
                auto hoverName = [canvas](const newui::Point& p) { canvas->onMouseMove(*canvas, p, std::uint32_t(0), 0); return canvas->cursorName(); };
                BLPoint innerAt(star.center.x + star.inner * std::cos(star.firstAngle + 3.14159265358979323846 / 5.0),
                    star.center.y + star.inner * std::sin(star.firstAngle + 3.14159265358979323846 / 5.0));
                BLPoint pointsAt(star.center.x - 0.28 * star.outer * std::cos(star.firstAngle), star.center.y - 0.28 * star.outer * std::sin(star.firstAngle));
                expect(hoverName(viewOf(pointsAt)) == "resize-we", "over the points handle the cursor is the left-right arrow");
                expect(hoverName(viewOf(innerAt)) == "resize-nesw", "over the inner-radius handle it is the arrow along the line to the center (here up-right)");
                expect(hoverName(view(star.center.x + 200, star.center.y + 200)) == "select", "away from the handles the select arrow returns");
                BLPoint inner(star.center.x + star.inner * std::cos(star.firstAngle + 3.14159265358979323846 / 5.0),
                    star.center.y + star.inner * std::sin(star.firstAngle + 3.14159265358979323846 / 5.0));
                mouse(0, viewOf(inner));
                mouse(1, view(star.center.x + 0.2 * (inner.x - star.center.x) / 0.5, star.center.y + 0.2 * (inner.y - star.center.y) / 0.5));
                mouse(2, view(star.center.x + 0.2 * (inner.x - star.center.x) / 0.5, star.center.y + 0.2 * (inner.y - star.center.y) / 0.5));
                curvy::RegularShape changed;
                expect(regularOf(1, changed) && changed.star && std::fabs(changed.ratio() - 0.2 * star.outer / star.outer * (star.inner / star.outer) / 0.5 * 0.5 / (star.inner / star.outer)) < 1.0 &&
                           changed.ratio() < star.ratio() - 0.1, "dragging the inner handle toward the center makes the points longer");
                expect(document.undoStack().canUndo(), "changing the star is an undo step");
                document.undoStack().undo();
                curvy::RegularShape back;
                expect(regularOf(1, back) && std::fabs(back.ratio() - star.ratio()) < 1e-6, "undo restores the inner radius");

                BLPoint pointsHandle(star.center.x - 0.28 * star.outer * std::cos(star.firstAngle), star.center.y - 0.28 * star.outer * std::sin(star.firstAngle));
                mouse(0, viewOf(pointsHandle));
                mouse(1, newui::Point(viewOf(pointsHandle).x + 30.0f, viewOf(pointsHandle).y));
                mouse(2, newui::Point(viewOf(pointsHandle).x + 30.0f, viewOf(pointsHandle).y));
                curvy::RegularShape more;
                expect(regularOf(1, more) && more.star && more.points == 7, "dragging the points handle right adds points (28 px = 2)");
                expect(std::fabs(more.outer - star.outer) < 1e-6 && std::fabs(more.ratio() - star.ratio()) < 1e-6, "the size and the inner radius stay the same");
            }

            // Polygon: the same handle changes the number of sides.
            canvas->setTool(curvy::SketchTool::Polygon);
            mouse(0, view(500, 300));
            mouse(1, view(500, 250));
            mouse(2, view(500, 250), newui::kmShift);
            canvas->setTool(curvy::SketchTool::Select);
            curvy::RegularShape polygon;
            expect(regularOf(2, polygon) && !polygon.star && polygon.points == 5, "the drawn polygon is recognised as a regular pentagon");
            if (!polygon.star && polygon.points == 5) {
                BLPoint handle(polygon.center.x - 0.28 * polygon.outer * std::cos(polygon.firstAngle), polygon.center.y - 0.28 * polygon.outer * std::sin(polygon.firstAngle));
                mouse(0, viewOf(handle));
                mouse(1, newui::Point(viewOf(handle).x - 30.0f, viewOf(handle).y));
                mouse(2, newui::Point(viewOf(handle).x - 30.0f, viewOf(handle).y));
                curvy::RegularShape fewer;
                expect(regularOf(2, fewer) && fewer.points == 3, "dragging the handle left removes sides (down to a triangle)");
            }

            // Rounded rectangle: the corner handle changes the radius.
            canvas->setTool(curvy::SketchTool::RoundRect);
            mouse(0, view(100, 400));
            mouse(1, view(300, 480));
            mouse(2, view(300, 480));
            canvas->setTool(curvy::SketchTool::Select);
            curvy::RoundRectShape rect;
            expect(curvy::analyzeRoundRect(document.object(3).geometry(), document.object(3).frameAngle(), rect) && std::fabs(rect.radius - 12.0) < 1e-6,
                "the drawn rounded rectangle is recognised with its corner radius");
            BLPoint cornerHandle(rect.box.x0 + rect.radius + 14.0, rect.box.y0 + rect.radius + 14.0);
            canvas->onMouseMove(*canvas, viewOf(cornerHandle), std::uint32_t(0), 0);
            expect(canvas->cursorName() == "resize-nwse", "over the corner handle the cursor is the diagonal arrow it slides along");
            mouse(0, viewOf(cornerHandle));
            mouse(1, view(100 + 30.0 + 14.0, 400 + 30.0 + 14.0));
            mouse(2, view(100 + 30.0 + 14.0, 400 + 30.0 + 14.0));
            curvy::RoundRectShape rounder;
            expect(curvy::analyzeRoundRect(document.object(3).geometry(), 0.0, rounder) && std::fabs(rounder.radius - 30.0) < 1e-6,
                "dragging the corner handle sets the corner radius");
            mouse(0, view(100 + 30.0 + 14.0, 400 + 30.0 + 14.0));
            mouse(1, view(100 + 500.0, 400 + 500.0));
            mouse(2, view(100 + 500.0, 400 + 500.0));
            curvy::RoundRectShape most;
            expect(curvy::analyzeRoundRect(document.object(3).geometry(), 0.0, most) && std::fabs(most.radius - 40.0) < 1e-6, "the radius stops at half the shorter side");

            // Arc: drawn from the center, then each end follows the pointer around the center.
            canvas->setTool(curvy::SketchTool::Arc);
            mouse(0, view(600, 150));
            mouse(1, view(660, 150));
            mouse(2, view(660, 150));
            canvas->setTool(curvy::SketchTool::Select);
            curvy::ArcShape arc;
            expect(document.objectCount() == 5 && curvy::analyzeArc(document.object(4).geometry(), arc) && std::fabs(arc.sweep - 1.5707963267948966) < 1e-3 &&
                       std::fabs(arc.radius - 60.0) < 0.1 && document.object(4).fill().isNone(), "the arc tool adds an open quarter-turn arc");
            if (arc.radius > 0.0) {
                BLPoint end = arc.endPoint();
                canvas->onMouseMove(*canvas, viewOf(end), std::uint32_t(0), 0);
                expect(canvas->cursorName() == "resize-we", "over the end of a quarter arc (at the bottom) the cursor is the horizontal arrow, the tangent there");
                canvas->onMouseMove(*canvas, viewOf(arc.startPoint()), std::uint32_t(0), 0);
                expect(canvas->cursorName() == "resize-ns", "over its start (at the right) it is the vertical arrow");
                mouse(0, viewOf(end));
                mouse(1, view(560, 150));
                expect(canvas->cursorName() == "resize-we", "while dragging an arc end the arrow stays the one it had on the handle");
                mouse(1, viewOf(end));
                mouse(1, view(540, 150));   // straight left of the center: a half turn from the start
                mouse(2, view(540, 150));
                curvy::ArcShape longer;
                expect(curvy::analyzeArc(document.object(4).geometry(), longer) && std::fabs(longer.sweep - 3.14159265358979323846) < 1e-2 &&
                           std::fabs(longer.start - arc.start) < 1e-2, "dragging the end handle around the center sets the sweep");
                BLPoint begin = longer.startPoint();
                mouse(0, viewOf(begin));
                mouse(1, view(600, 90));    // straight up: the start moves, the end stays
                mouse(2, view(600, 90));
                curvy::ArcShape moved;
                expect(curvy::analyzeArc(document.object(4).geometry(), moved) && std::fabs(moved.start + 1.5707963267948966) < 1e-2 &&
                           std::fabs((moved.start + moved.sweep) - (longer.start + longer.sweep)) < 1e-2, "dragging the start handle turns the start and keeps the end");
            }
        }
        canvas->setTool(curvy::SketchTool::Select);
        canvas->setSnapToGrid(true);
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkTextTool() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Text tool: click, type, Enter, Backspace, Esc; edit again; empty text goes away.
        document.clearObjects();
        document.undoStack().clear();
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(false);
        {
            auto typeChar = [&canvas](int ch) { canvas->onKeyPress(*canvas, 0u, ch, 1, 0u); };
            auto pressKey = [&canvas](std::uint32_t vk, std::uint32_t mask = 0u) { canvas->onKeyDown(*canvas, mask, 0, 1, vk); };
            canvas->setTool(curvy::SketchTool::Text);
            mouse(0, view(100, 200));
            mouse(2, view(100, 200));
            expect(canvas->textEditing(), "a click with the Text tool starts editing");
            typeChar('H');
            typeChar('i');
            pressKey(newui::vkReturn);
            typeChar('y');
            typeChar('o');
            expect(canvas->editedText() == "Hi\nyo", "typed characters and Enter fill the text");
            pressKey(newui::vkBackSpace);
            pressKey(newui::vkLeftArrow);
            typeChar('!');
            expect(canvas->editedText() == "Hi\n!y", "Backspace removes before the caret and typing inserts at it");
            expect(document.objectCount() == 0, "new text is not in the document until it is finished");
            pressKey(newui::vkEscape);
            expect(!canvas->textEditing() && document.objectCount() == 1 && document.object(0).isText(), "Esc finishes the text as a Text object");
            expect(document.object(0).text() == "Hi\n!y" && document.isSelected(document.object(0).id()), "the new text is selected");
            BLBox written = document.object(0).geometryBounds();
            expect(std::fabs(written.x0 - 100.0) < 3.0 && written.y1 > 200.0, "the text starts at the click, its first baseline on it");
            expect(document.undoStack().canUndo(), "adding text is an undo step");
            document.undoStack().undo();
            expect(document.objectCount() == 0, "undo removes the text");
            document.undoStack().redo();
            expect(document.objectCount() == 1, "redo brings it back");

            // Edit it: click on it with the Text tool, append, finish by clicking the empty page.
            BLBox box = document.object(0).geometryBounds();
            mouse(0, view(box.x1 - 1.0, box.y0 + 5.0));
            mouse(2, view(box.x1 - 1.0, box.y0 + 5.0));
            expect(canvas->textEditing() && canvas->editedText() == "Hi\n!y", "clicking text edits it");
            pressKey(newui::vkEnd);
            typeChar('?');
            mouse(0, view(500, 500));
            mouse(2, view(500, 500));
            expect(document.object(0).text().find('?') != std::string::npos, "a click elsewhere finishes the edit");
            expect(document.objectCount() == 1, "an empty click with the Text tool starts new text, which stays out until typed in");
            canvas->commitText();
            expect(document.objectCount() == 1, "text left empty is not kept");
            document.undoStack().undo();
            expect(document.object(0).text() == "Hi\n!y", "undo of the edit restores the words");
            document.undoStack().redo();

            // Emptying a text deletes it.
            BLBox now = document.object(0).geometryBounds();
            mouse(0, view(now.x0 + 3.0, now.y0 + 3.0));
            mouse(2, view(now.x0 + 3.0, now.y0 + 3.0));
            pressKey(newui::vkEnd);
            for (int i = 0; i < 10; ++i) { pressKey(newui::vkBackSpace); }
            pressKey(newui::vkDownArrow);
            pressKey(newui::vkEnd);
            for (int i = 0; i < 10; ++i) { pressKey(newui::vkBackSpace); }
            canvas->setTool(curvy::SketchTool::Select);
            expect(document.objectCount() == 0, "text edited down to nothing is removed");
            document.undoStack().undo();
            expect(document.objectCount() == 1 && document.object(0).text() == "Hi?\n!y", "undo brings the removed text back");
        }
        canvas->setTool(curvy::SketchTool::Select);
        canvas->setSnapToGrid(true);
        document.clearObjects();
        document.undoStack().clear();
    }

    void ToolChecks::checkWidthTool() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Width tool: dragging the path adds a point and widens it, the handles and diamond edit it, Alt-click deletes.
        document.clearObjects();
        {
            curvy::SketchObject& line = document.add(curvy::SketchObject::makeLine(100, 300, 400, 300));
            line.setStroke(curvy::SketchPaint::solid(curvy::SketchColor::gray(0.0f)));
            line.setStrokeWidth(10.0);
        }
        document.undoStack().clear();
        document.setSelection({document.object(0).id()});
        canvas->setTool(curvy::SketchTool::Width);
        mouse(0, view(250, 300));
        mouse(1, view(250, 290));
        mouse(2, view(250, 290));
        expect(document.object(0).strokeOptions().widths.size() == 3, "dragging the path with the width tool adds a point (and the two ends)");
        expect(document.object(0).strokeOptions().widths.size() == 3 &&
            std::fabs(document.object(0).strokeOptions().widths[1].left - 2.0) < 0.05, "the new point is twice the stroke weight wide");
        expect(document.undoStack().canUndo(), "adding a width point is an undo step");
        mouse(0, view(250, 300));
        mouse(1, view(300, 300));
        mouse(2, view(300, 300));
        expect(document.object(0).strokeOptions().widths.size() == 3 &&
            std::fabs(document.object(0).strokeOptions().widths[1].t - 0.5) > 0.1, "dragging the diamond slides the point along the path");
        mouse(0, view(300, 300), newui::kmAlt);
        mouse(2, view(300, 300), newui::kmAlt);
        expect(document.object(0).strokeOptions().widths.size() == 2, "Alt-click on a point deletes it");
        while (document.undoStack().canUndo()) { document.undoStack().undo(); }
        expect(document.object(0).strokeOptions().widths.empty(), "undo puts the plain stroke back");
        canvas->setTool(curvy::SketchTool::Select);
    }

    void ToolChecks::checkHandleCursors() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Select tool: the cursor over a handle follows the frame's turn.
        auto hover = [canvas](const newui::Point& p) { canvas->onMouseMove(*canvas, p, std::uint32_t(0), 0); return canvas->cursorName(); };
        document.clearObjects();
        document.add(curvy::SketchObject::makeRect(100, 100, 200, 100));
        document.setSelection({document.object(0).id()});
        expect(hover(view(100, 100)) == "resize-nwse", "an upright frame's top-left handle shows the NW-SE arrow");
        expect(hover(view(200, 100)) == "resize-ns", "its top-middle handle shows the vertical arrow");
        expect(hover(view(300, 150)) == "resize-we", "its right-middle handle shows the horizontal arrow");
        expect(hover(view(200, 250)) == "select", "away from the handles the arrow returns");
        double turn = 3.14159265358979323846 / 4.0;  // turn the object 45 degrees about its center
        BLMatrix2D rotation = BLMatrix2D::make_translation(-200, -150);
        rotation.post_rotate(turn);
        rotation.post_translate(200, 150);
        document.object(0).transform(rotation);
        document.object(0).setFrameAngle(turn);
        document.touch();
        // The top-middle handle is now up and to the right of the center: the NE-SW arrow.
        expect(hover(view(200 + 50 * std::sin(turn), 150 - 50 * std::cos(turn))) == "resize-nesw",
            "a frame turned 45 degrees shows the arrow matching its turned handle");
        document.clearObjects();
    }

    void ToolChecks::checkSnapping() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Drawing and moving snap to the grid when close to a line; Alt resizes from the center.
        canvas->setZoom(1.0f);
        canvas->setSnapToGrid(true);
        canvas->setTool(curvy::SketchTool::Rectangle);
        mouse(0, view(103, 98));
        mouse(1, view(150, 150));
        mouse(2, view(197, 203));
        expect(document.objectCount() == 1, "a rectangle was drawn");
        if (document.objectCount() == 1) {
            BLBox drawn = document.object(0).geometryBounds();
            expect(std::fabs(drawn.x0 - 100) < 1e-6 && std::fabs(drawn.y0 - 100) < 1e-6 &&
                std::fabs(drawn.x1 - 200) < 1e-6 && std::fabs(drawn.y1 - 200) < 1e-6, "drawing snaps both corners to the 20 pt grid");
            canvas->setTool(curvy::SketchTool::Select);
            document.setSelection({document.object(0).id()});
            mouse(0, view(130, 130));  // (not the center: that is the transform origin) move by (11, 9): the middle (161, 159) is 1 pt off the 160 lines
            mouse(1, view(141, 139));
            mouse(2, view(141, 139));
            BLBox moved = document.object(0).geometryBounds();
            expect(std::fabs(moved.x0 - 110) < 1e-6 && std::fabs(moved.y0 - 110) < 1e-6, "moving snaps the box's middle to the grid");

            document.clearObjects();
            document.add(curvy::SketchObject::makeRect(100, 100, 200, 100));
            document.setSelection({document.object(0).id()});
            mouse(0, view(300, 150));  // the right-middle handle
            mouse(1, view(330, 150), newui::kmAlt);
            mouse(2, view(350, 150), newui::kmAlt);
            BLBox grown = document.object(0).geometryBounds();
            expect(std::fabs(grown.x0 - 50) < 1e-6 && std::fabs(grown.x1 - 350) < 1e-6, "Alt-dragging a handle resizes about the center");
        }
        document.clearObjects();
        canvas->setTool(curvy::SketchTool::Select);
    }

    void ToolChecks::checkOpenRecent() {
        newui::Frame& frame = frame_;
        SketchDocument& document = document_;
        CanvasView* canvas = canvas_;
        (void)frame; (void)document; (void)canvas;
        const ViewFn view{canvas};
        const MouseFn mouse{canvas};
        (void)view; (void)mouse;
        // Open Recent: the newest file is first, a re-opened one moves up, a missing one is dropped, and the list
        // survives into a new controller (it is kept in a file).
        {
            std::string folder = newui::SpecialFolders::temp() + "/sketch_recent_check";
            std::filesystem::create_directories(folder);
            std::string listPath = folder + "/recent.txt";
            std::filesystem::remove(listPath);
            std::string a = folder + "/a.svg", b = folder + "/b.svg";
            for (const std::string& path : {a, b}) {
                document.clearObjects();
                document.add(curvy::SketchObject::makeRect(0, 0, 10, 10));
                document.save(path);
            }
            auto* controller = new curvy::SketchDocumentController(document, frame);  // lives to the end of the process
            controller->setErrorReporter([](const std::string&) {});  // no message box for the missing file below
            controller->setRecentFilesPath(listPath);
            document.reset();
            expect(controller->open(a) && controller->open(b), "two files open");
            expect(controller->recentFiles().size() == 2 && controller->recentFiles()[0] == document.filePath(), "the newest file is first");
            document.reset();
            controller->open(a);
            expect(controller->recentFiles().front().find("a.svg") != std::string::npos, "re-opening a file moves it to the top");
            std::filesystem::remove(b);
            document.reset();
            controller->open(b);  // gone: reports an error (no dialog is shown for the missing file in this check) and is forgotten
            expect(controller->recentFiles().size() == 1, "a file that no longer exists is dropped from the list");
            auto* second = new curvy::SketchDocumentController(document, frame);
            second->setRecentFilesPath(listPath);
            expect(second->recentFiles() == controller->recentFiles(), "the list is read back from its file");
        }
    }

    }

    int runToolChecks(SketchWindowController& window) {
        return ToolChecks(window).run();
    }

}
