#include "curvy/workspacechecks.h"

#include "curvy/workspacecontroller.h"
#include "curvy/theme.h"

#include <newui/themedata.h>
#include <newui/uicolormanager.h>

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>

#include <cstdio>

namespace curvy {

    namespace {

        class WorkspaceChecks {
        public:
            explicit WorkspaceChecks(WorkspaceController& w) : w_(w), canvas_(&w.canvas()), model_(&w.workspace()), doc_(canvas_->document()) {}

            int run() {
                w_.frame().rootView().updateLayout();
                checkStartAndNewRing();
                checkRingsAndHoldKeys();
                checkScrub();
                checkPanAndBarrel();
                checkDocks();
                checkTheme();
                std::printf("%d failure(s)\n", failures_);
                return failures_;
            }

        private:
            void expect(bool ok, const char* what) {
                std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
                if (!ok) {
                    ++failures_;
                }
            }

            newui::Point at(double x, double y) const {
                const BLPoint p = canvas_->toView(BLPoint(x, y));
                return newui::Point(float(p.x), float(p.y));
            }
            static newui::Point offset(const newui::Point& p, float dx, float dy) { return newui::Point(p.x + dx, p.y + dy); }
            void key(std::uint32_t vk) { canvas_->onKeyDown(*canvas_, 0u, 0, 1, vk); }
            void keyUp(std::uint32_t vk) { canvas_->onKeyUp(*canvas_, 0u, 0, 1, vk); }
            void tapSpace() { key(newui::vkSpaceBar); keyUp(newui::vkSpaceBar); }
            void move(const newui::Point& p) { canvas_->onMouseMove(*canvas_, p, 0u, 0u); }
            void drag(const newui::Point& a, const newui::Point& b) {
                const std::uint32_t left = std::uint32_t(newui::mbmLeftButton);
                canvas_->onMouseMove(*canvas_, a, 0u, 0u);
                canvas_->onMouseDown(*canvas_, a, left, 0u);
                canvas_->onMouseMove(*canvas_, newui::Point((a.x + b.x) / 2, (a.y + b.y) / 2), left, 0u);
                canvas_->onMouseMove(*canvas_, b, left, 0u);
                canvas_->onMouseUp(*canvas_, b, left, 0u);
            }

            void checkStartAndNewRing() {
                w_.start(std::string());
                expect(!model_->documentOpen() && model_->hintTool() == "Nothing open", "starts with nothing open");
                expect(!canvas_->showPages() && !canvas_->showRulers(), "no page or rulers while nothing is open");
                canvas_->onMouseMove(*canvas_, newui::Point(400, 300), 0u, 0u);
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "New", "tap Space opens the New ring");
                key(newui::vkNumber2);
                expect(!w_.menu().isOpen() && model_->documentOpen(), "key 2 in the ring starts a document");
                expect(canvas_->showPages() && canvas_->showRulers(), "the page and rulers appear with the document");
                expect(doc_->pageWidth() == 595.0f && doc_->pageHeight() == 842.0f, "A4 page size");
                expect(model_->hintTool() == "Select", "tool is Select");
            }

            void checkRingsAndHoldKeys() {
                move(at(300, 400));
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "Create", "tap Space over a page opens the Create ring");
                key(newui::vkLetterE);
                expect(canvas_->tool() == SketchTool::Ellipse, "ring key E arms the ellipse");
                drag(at(100, 100), at(200, 200));
                expect(doc_->objectCount() == 1 && canvas_->tool() == SketchTool::Ellipse, "an armed tool stays armed after drawing");
                key(newui::vkEscape);
                expect(canvas_->tool() == SketchTool::Select, "Esc goes back to Select");

                key(newui::vkLetterR);
                expect(canvas_->tool() == SketchTool::Rectangle && model_->heldToolKey() == "R", "holding R arms the rectangle");
                drag(at(300, 100), at(450, 220));
                keyUp(newui::vkLetterR);
                expect(doc_->objectCount() == 2 && canvas_->tool() == SketchTool::Select, "letting go of R is back to Select");

                move(at(375, 160));
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "Object" && doc_->selection().size() == 1, "tap Space over an object opens the Object ring");
                key(newui::vkLetterD);
                expect(doc_->objectCount() == 3, "ring key D duplicates");
            }

            void checkScrub() {
                doc_->clearSelection();
                const SketchObject* rect = doc_->hitTest(BLPoint(300, 150), 2.0);
                expect(rect != nullptr, "found the rectangle");
                if (rect == nullptr) {
                    return;
                }
                const std::size_t id = rect->id();
                const double width = rect->strokeWidth();
                move(at(300, 150));   // on the left edge
                expect(model_->hover().object == id && model_->hover().part == ScrubPart::Stroke, "pointer on the edge hovers the outline");
                key(newui::vkLetterW);
                expect(model_->scrubbing() && w_.scrub().active(), "holding W starts a scrub");
                move(offset(at(300, 150), 0, -40));
                expect(doc_->findById(id)->strokeWidth() > width, "moving up widens the outline");
                keyUp(newui::vkLetterW);
                expect(!model_->scrubbing() && doc_->undoStack().undoDescription() == "Scrub Width", "letting go of W is one undo step");

                move(at(300, 150));
                key(newui::vkLetterO);
                move(offset(at(300, 150), 0, 40));
                key(newui::vkEscape);
                expect(!model_->scrubbing() && doc_->findById(id)->activeItem(AppearanceKind::Stroke).opacity == 1.0 && doc_->undoStack().undoDescription() == "Scrub Width",
                    "Esc cancels a scrub");

                move(at(375, 170));
                expect(model_->hover().part == ScrubPart::Fill, "pointer inside hovers the fill");
                key(newui::vkLetterH);
                expect(w_.scrub().active() && w_.scrub().part() == ScrubPart::Fill, "H on a fill scrubs its hue");
                keyUp(newui::vkLetterH);
            }

            void checkPanAndBarrel() {
                const float panX = canvas_->panX();
                key(newui::vkSpaceBar);
                canvas_->onMouseDown(*canvas_, newui::Point(500, 500), std::uint32_t(newui::mbmLeftButton), 0u);
                canvas_->onMouseMove(*canvas_, newui::Point(540, 520), std::uint32_t(newui::mbmLeftButton), 0u);
                canvas_->onMouseUp(*canvas_, newui::Point(540, 520), std::uint32_t(newui::mbmLeftButton), 0u);
                keyUp(newui::vkSpaceBar);
                expect(canvas_->panX() == panX + 40.0f && !w_.menu().isOpen(), "Space and drag pans and does not open the ring");
                canvas_->onMouseDown(*canvas_, newui::Point(500, 500), std::uint32_t(newui::mbmRightButton), 0u);
                expect(w_.menu().isOpen(), "the barrel button (right press) opens a ring");
                key(newui::vkEscape);
                expect(!w_.menu().isOpen(), "Esc closes the ring");
            }

            void checkDocks() {
                DockController* dock = &w_.dock();
                expect(dock->toolDock() != nullptr && dock->propertyDock() != nullptr, "both dock layouts loaded");
                expect(!dock->toolDock()->isVisible() && !dock->propertyDock()->isVisible(), "both docks are off by default");
                auto* toolbarToggle = dynamic_cast<newui::Button*>(w_.frame().rootView().findView("toolbarToggle"));
                auto* propertiesToggle = dynamic_cast<newui::Button*>(w_.frame().rootView().findView("propertiesToggle"));
                expect(toolbarToggle != nullptr && propertiesToggle != nullptr, "the top bar has both toggles");

                toolbarToggle->onClick(*toolbarToggle);
                expect(model_->toolbarVisible() && dock->toolDock()->isVisible() && !dock->propertyDock()->isVisible(), "the Tools toggle shows the toolbar only");
                const newui::Rect tools = dock->toolDock()->bounds();
                const newui::Rect client = canvas_->getClientBounds();
                expect(tools.left() == canvas_->rulerSize() && tools.top() == canvas_->rulerSize() && tools.height() == client.height() - canvas_->rulerSize(), "the toolbar sits flush left under the ruler, full height");
                expect(dock->toolButton(SketchTool::Select) != nullptr && dock->toolButton(SketchTool::Select)->isChecked(), "Select is the checked tool button");

                dock->toolButton(SketchTool::Star)->onClick(*dock->toolButton(SketchTool::Star));
                expect(canvas_->tool() == SketchTool::Star && dock->toolButton(SketchTool::Star)->isChecked() && !dock->toolButton(SketchTool::Select)->isChecked(), "clicking a tool button picks the tool");
                key(newui::vkEscape);
                expect(canvas_->tool() == SketchTool::Select && dock->toolButton(SketchTool::Select)->isChecked(), "the buttons follow the tool when it changes elsewhere");

                propertiesToggle->onClick(*propertiesToggle);
                const newui::Rect props = dock->propertyDock()->bounds();
                expect(model_->propertiesVisible() && dock->propertyDock()->isVisible() && props.left() + props.width() == client.width(), "the Panels toggle shows the property dock flush right");

                doc_->setSelection({doc_->hitTest(BLPoint(375, 170), 2.0)->id()});
                auto* width = dynamic_cast<newui::TextField*>(w_.frame().rootView().findView("transformWField"));
                expect(width != nullptr && width->text() == L"150", "the Transform box follows the selection");

                w_.runCommand("view.toolbar");
                w_.runCommand("view.properties");
                expect(!dock->toolDock()->isVisible() && !dock->propertyDock()->isVisible() && !toolbarToggle->isChecked(), "the View ring commands turn them off and the toggles follow");
                move(at(-60, 400));
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "Document", "over the pasteboard the Document ring opens");
                key(newui::vkLetterV);
                expect(w_.menu().isOpen() && w_.menu().ring().title == "View", "V in the Document ring opens the View ring");
                key(newui::vkLetterT);
                expect(model_->toolbarVisible() && dock->toolDock()->isVisible(), "T in the View ring turns the toolbar on");
                w_.runCommand("view.toolbar");
            }

            void checkTheme() {
                expect(canvas_->pasteboardColor() == Theme::argb(Theme::dark().bgCanvas), "starts in the dark theme");
                expect(newui::UIColorManager::isDarkMode() && newui::ThemeData::instance().isLoaded(), "newui follows: dark mode and curvy's dark.theme loaded");
                newui::Color window;
                expect(newui::ThemeData::instance().tryColorFor(newui::UIColorRole::WindowBackground, window) && int(window.r * 255.0f + 0.5f) == int(Theme::dark().bgApp >> 16), "WindowBackground comes from the .theme file");
                w_.setDarkTheme(false);
                expect(canvas_->pasteboardColor() == Theme::argb(Theme::light().bgCanvas) && !newui::UIColorManager::isDarkMode(), "switching to light restyles the canvas");
                w_.setDarkTheme(true);
                expect(canvas_->pasteboardColor() == Theme::argb(Theme::dark().bgCanvas), "and back to dark");
            }

            WorkspaceController& w_;
            CanvasView* canvas_;
            WorkspaceModel* model_;
            SketchDocument* doc_;
            int failures_ = 0;
        };

    }

    int runWorkspaceChecks(WorkspaceController& workspace) {
        return WorkspaceChecks(workspace).run();
    }

}
