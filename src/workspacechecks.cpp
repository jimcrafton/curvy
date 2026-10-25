#include "curvy/workspacechecks.h"

#include "curvy/colorwheel.h"
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
                checkGraphDock();
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
                auto* name = dynamic_cast<newui::Label*>(w_.frame().rootView().findView("documentName"));
                w_.frame().rootView().updateLayout();
                expect(name != nullptr && !name->text().empty() && name->bounds().size().width >= 16.0f, "the title bar's document name is wide enough to show");
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

            void armed() {
                w_.runCommand("tool.rect");
                expect(canvas_->tool() == SketchTool::Rectangle, "a ring tool command arms the tool");
                w_.runCommand("tool.select");
                expect(canvas_->tool() == SketchTool::Select, "the ring's Select slot goes back to selecting");
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
                expect(tools.left() == 0.0f && tools.top() == 0.0f && tools.height() == client.height() && canvas_->leftInset() == tools.width(), "the toolbar sits at the extreme left, full height, and the ruler starts to its right");
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

                auto shown = [&](const char* name) { return dock->propertyDock()->findView(name)->isVisible(); };
                expect(shown("transformPanel") && shown("colorPanel") && shown("strokePanel") && shown("opacityPanel"), "a selected shape shows Transform, Colour, Stroke and Opacity");
                doc_->clearSelection();
                expect(!shown("transformPanel") && !shown("colorPanel") && shown("dockCaption"), "Select tool with nothing selected shows only the note");
                key(newui::vkLetterS);
                expect(canvas_->tool() == SketchTool::Star && !shown("transformPanel") && shown("colorPanel") && shown("strokePanel") && shown("opacityPanel"), "an armed drawing tool shows the colour boxes for new shapes");
                keyUp(newui::vkLetterS);
                key(newui::vkLetterE);
                key(newui::vkLetterV);
                expect(canvas_->tool() == SketchTool::Select && model_->heldToolKey().empty(), "V goes back to the Select tool from a held drawing tool");
                keyUp(newui::vkLetterE);
                armed();
                w_.runCommand("view.toolbar");
                w_.runCommand("view.properties");
                expect(!dock->toolDock()->isVisible() && !dock->propertyDock()->isVisible() && !toolbarToggle->isChecked(), "the View ring commands turn them off and the toggles follow");
                checkAppearanceCards();
                checkToolOptions();
                move(at(-60, 400));
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "Document", "over the pasteboard the Document ring opens");
                key(newui::vkLetterV);
                expect(w_.menu().isOpen() && w_.menu().ring().title == "View", "V in the Document ring opens the View ring");
                key(newui::vkLetterT);
                expect(model_->toolbarVisible() && dock->toolDock()->isVisible(), "T in the View ring turns the toolbar on");
                w_.runCommand("view.toolbar");
            }

            // The object ring's Fill and Stroke slots lift the dock's Colour / Stroke box into a card at the pen, with the docks off.
            void checkAppearanceCards() {
                DockController* dock = &w_.dock();
                doc_->setSelection({doc_->hitTest(BLPoint(375, 170), 2.0)->id()});
                auto box = [&](const char* name) { return w_.frame().rootView().findView(name); };
                auto homeless = [&](const char* name) { return box(name) != nullptr && box(name)->parent() != nullptr && box(name)->parent()->name() != "propertyDockBox"; };
                w_.runCommand("card.color");
                expect(dock->card() == DockController::Card::Fill && homeless("colorPanel") && box("colorPanel")->isVisible(), "the Fill slot lifts the Colour box out of the dock into a card");
                expect(!dock->propertyDock()->isVisible(), "and the dock itself stays off");
                expect(dock->stylePanel()->target() == StylePanel::Target::Fill, "a filled closed shape opens the Color card on Fill");
                {
                    SketchObject* shape = doc_->findById(doc_->selection().front());
                    const SketchPaint kept = shape->fill();
                    shape->setFill(SketchPaint::none());
                    w_.runCommand("card.color");
                    expect(dock->stylePanel()->target() == StylePanel::Target::Stroke, "a shape with no fill opens the Color card on Stroke");
                    shape->setFill(kept);
                    w_.runCommand("card.color");
                }
                auto* red = dynamic_cast<newui::Slider*>(box("comp0Slider"));
                auto* hex = dynamic_cast<newui::TextField*>(box("hexField"));
                expect(red != nullptr && hex != nullptr && box("alphaSlider") != nullptr, "the Colour card has the component and alpha sliders");
                red->setValue(0.0f);
                expect(hex->text().compare(0, 3, L"#00") == 0, "moving the R slider changes the fill (the hex follows)");
                auto* mode = dynamic_cast<newui::SegmentedControl*>(box("colorModeControl"));
                expect(mode != nullptr && mode->segments().size() == 4, "the card has the Gray / RGB / CMYK / HSB control");
                mode->setSelectedIndex(3);
                expect(dynamic_cast<newui::Label*>(box("comp0SliderCaption"))->text() == "H", "HSB turns the sliders into H, S, B");
                mode->setSelectedIndex(1);

                auto* host = dynamic_cast<newui::SubView*>(box("colorWheelHost"));
                auto* wheel = host != nullptr && !host->childViews().empty() ? dynamic_cast<ColorWheel*>(host->childViews().front()) : nullptr;
                expect(wheel != nullptr && host->isVisible(), "the Colour card opens on the colour wheel");
                const newui::Rect area = wheel->getClientBounds();
                const float cx = area.left() + area.width() * 0.5f, cy = area.top() + area.height() * 0.5f;
                const float ringRadius = std::min(area.width(), area.height()) * 0.5f - 2.0f - 8.0f;
                const newui::Point onRing(cx + ringRadius * std::cos(2.0944f), cy + ringRadius * std::sin(2.0944f));   // 120 degrees clockwise from red: green
                wheel->onMouseDown(*wheel, onRing, newui::mbmLeftButton, 0);
                wheel->onMouseUp(*wheel, onRing, newui::mbmLeftButton, 0);
                auto channel = [&](std::size_t at) { return int(std::wcstol(hex->text().substr(at, 2).c_str(), nullptr, 16)); };
                expect(std::fabs(wheel->hue() - 1.0 / 3.0) < 0.02 && channel(3) > 0xE0 && channel(1) < 0x20, "pressing on the ring turns the hue: the fill goes green");
                const newui::Point inSquare(cx - area.width() * 0.1f, cy);
                wheel->onMouseDown(*wheel, newui::Point(cx - ringRadius * 0.45f, cy), newui::mbmLeftButton, 0);   // left side of the square: no saturation
                wheel->onMouseUp(*wheel, inSquare, newui::mbmLeftButton, 0);
                expect(wheel->saturation() < 0.1 && std::abs(channel(1) - channel(3)) < 0x20, "a press at the square's left edge desaturates the fill");
                auto* tabs = dynamic_cast<newui::SegmentedControl*>(box("pickerTabs"));
                tabs->setSelectedIndex(1);
                expect(!host->isVisible() && box("pickerSliderArea")->isVisible(), "the Sliders tab swaps the wheel for the sliders");
                tabs->setSelectedIndex(0);
                auto* linear = dynamic_cast<newui::ToolbarButton*>(box("fillLinearButton"));
                linear->setChecked(true);
                expect(box("gradientRow")->isVisible() && canvas_->tool() == SketchTool::Gradient, "choosing Linear shows the stop strip and arms the Gradient tool");
                auto* solid = dynamic_cast<newui::ToolbarButton*>(box("fillSolidButton"));
                solid->setChecked(true);
                expect(!box("gradientRow")->isVisible(), "choosing Solid hides the strip again");
                canvas_->setTool(SketchTool::Select);
                w_.runCommand("card.stroke");
                expect(dock->card() == DockController::Card::Stroke && homeless("strokePanel") && !homeless("colorPanel"), "the Stroke slot swaps the card to the Stroke box and the Colour box goes home");
                dock->closeCard();
                expect(dock->card() == DockController::Card::None && !homeless("strokePanel") && !homeless("colorPanel"), "closing the card puts the box back in the dock");
                w_.runCommand("card.color");
                w_.frame().rootView().setFocusedSubView(dynamic_cast<newui::SubView*>(box("comp1Slider")));
                w_.frame().rootView().onKeyDown(w_.frame().rootView(), 0, 0, 1, newui::vkEscape);   // what RootView::keyEvent does first with any key
                expect(dock->card() == DockController::Card::None, "Esc closes the card while one of its sliders has the focus");
                w_.runCommand("card.stroke");
                auto* x = dynamic_cast<newui::Control*>(box("cardClose"));
                expect(x != nullptr, "the card has a close button");
                x->onClick(*x);
                expect(dock->card() == DockController::Card::None, "the close button puts the card away");
                doc_->clearSelection();
            }

            // The option cards: defaults for the next shape, from a ring slot's option box or Shift + its key, and in step with the canvas.
            void checkToolOptions() {
                ToolOptionsCard& card = w_.toolOptions();
                auto view = [&](const char* name) { return w_.frame().rootView().findView(name); };
                const newui::Point box = RadialLayout::optionBoxCenter(1, 8);
                expect(std::hypot(double(box.x), double(box.y)) > RadialLayout::ringRadius + RadialLayout::slotHeight * 0.5, "a slot's option box sits past the slot's outer end");

                const int pointsBefore = canvas_->starPointCount(), innerBefore = canvas_->starInnerPercent();
                w_.runCommand("options.star");
                expect(canvas_->starPointCount() == pointsBefore && canvas_->starInnerPercent() == innerBefore, "opening the card leaves the defaults as they were");
                expect(card.isOpen() && card.tool() == SketchTool::Star, "the Star card opens");
                expect(card.value(0) == double(canvas_->starPointCount()) && card.value(1) == double(canvas_->starInnerPercent()), "and shows the canvas's points and inner radius");
                expect(view("optionRow1")->isVisible() && view("optionRow2")->isVisible(), "Star has two rows");
                canvas_->setStarPoints(7);
                expect(card.value(0) == 7.0 && dynamic_cast<newui::Slider*>(view("optionSlider1"))->value() == 7.0f, "the arrows while dragging and the card agree");
                dynamic_cast<newui::Slider*>(view("optionSlider2"))->setValue(30.0f);
                expect(canvas_->starInnerPercent() == 30, "moving the inner radius slider changes the canvas default");
                auto* reset = dynamic_cast<newui::Button*>(view("optionsReset"));
                reset->onClick(*reset);
                expect(canvas_->starPointCount() == 5 && canvas_->starInnerPercent() == 50, "Reset puts the defaults back");
                canvas_->setStarPoints(9);
                w_.runCommand("options.polygon");
                expect(card.tool() == SketchTool::Polygon && !view("optionRow2")->isVisible() && canvas_->starPointCount() == 9, "the Polygon card has one row and leaves the star alone");
                key(newui::vkEscape);
                expect(!card.isOpen(), "Esc closes the card");
                canvas_->setStarPoints(5);

                move(at(300, 400));
                tapSpace();
                expect(w_.menu().isOpen() && w_.menu().ring().title == "Create", "the Create ring is up for Shift + key");
                w_.menu().handleKey("S", true);
                expect(!w_.menu().isOpen() && card.isOpen() && card.tool() == SketchTool::Star, "Shift + the slot's key opens its options instead of arming it");
                expect(canvas_->tool() == SketchTool::Select, "and the tool is not armed");
                card.close();
            }

            // The graph dock: the document's layers, groups and nodes as a tree with a box for the selected node's parameters.
            void checkGraphDock() {
                GraphDock& graph = w_.graph();
                expect(graph.dock() != nullptr && graph.tree() != nullptr && !graph.dock()->isVisible(), "the graph dock loads and is off by default");
                auto* toggle = dynamic_cast<newui::Button*>(w_.frame().rootView().findView("graphToggle"));
                expect(toggle != nullptr, "the top bar has the Graph toggle");
                doc_->clearObjects();
                doc_->undoStack().clear();
                SketchObject& rect = doc_->add(SketchObject::makeRect(10, 20, 50, 30));
                rect.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
                rect.setName("Box");
                const std::size_t rectId = rect.id();
                const std::size_t textId = doc_->add(SketchObject::makeText("Hello", "", 20.0, BLPoint(10, 100))).id();
                toggle->onClick(*toggle);
                expect(model_->graphVisible() && graph.dock()->isVisible() && toggle->isChecked(), "the Graph toggle shows the dock");
                w_.frame().rootView().setFocusedSubView(toggle);
                toggle->onClick(*toggle);
                expect(w_.frame().rootView().focusedSubView() != toggle, "a clicked top bar button gives the keyboard back to the canvas");
                toggle->onClick(*toggle);
                const newui::Rect client = canvas_->getClientBounds();
                expect(graph.dock()->bounds().left() + graph.dock()->bounds().width() == client.width(), "it sits flush right while the property dock is off");
                w_.runCommand("view.properties");
                expect(graph.dock()->bounds().left() + graph.dock()->bounds().width() + w_.dock().propertyDock()->bounds().width() == client.width(), "and beside the property dock when that is on");
                w_.runCommand("view.properties");

                graph.refresh();
                auto row = [&](const std::string& text) -> const GraphRow* {
                    for (const GraphRow& r : graph.rows()) {
                        if (r.text == text) {
                            return &r;
                        }
                    }
                    return nullptr;
                };
                expect(graph.rows().size() >= 3 && graph.rows().front().kind == GraphRow::Kind::Layer, "Document view: a layer row, then its objects");
                expect(row("Box") != nullptr && row("Box")->chip && row("Box")->chipColor == 0xFF0000 && row("Box")->icon == "icons/node-path.svg", "a filled shape has a path icon and a colour chip");
                expect(row("\"Hello\"") != nullptr && row("\"Hello\"")->icon == "icons/node-text.svg", "an unnamed text is listed by its words, with the text icon");
                expect(row("Box")->depth == 1 && row("Box")->objectId == rectId, "objects sit one level under their layer and know their object");

                auto lookRow = [&](std::size_t object, const std::string& text) -> const GraphRow* {
                    for (const GraphRow& r : graph.rows()) {
                        if (r.look && r.objectId == object && r.text == text) {
                            return &r;
                        }
                    }
                    return nullptr;
                };
                const GraphRow* fillRow = lookRow(rectId, "Fill");
                const GraphRow* strokeRow = lookRow(rectId, "Stroke");
                expect(fillRow != nullptr && fillRow->chip && fillRow->chipColor == 0xFF0000 && fillRow->depth == 2 && fillRow->icon == "icons/node-fill.svg",
                    "an object's fill is a row under it, with its colour chip and icon");
                expect(strokeRow != nullptr && strokeRow->depth == 2 && strokeRow->icon == "icons/node-stroke.svg", "and so is its stroke");

                doc_->setSelection({rectId});
                expect(graph.detailNode() == rect.graphNode() && graph.title() == "Box", "selecting an object on the canvas shows its node");
                if (strokeRow != nullptr) {
                    const double width = rect.strokeWidth();
                    expect(graph.selectRow(strokeRow->key) && graph.title() == "Stroke" && graph.detailCaption(1) == "Width", "selecting the stroke row shows the stroke node");
                    graph.setDetailValue(1, "6");
                    expect(doc_->findById(rectId)->strokeWidth() == 6.0, "editing the width in the box changes the stroke");
                    expect(graph.title() == "Stroke" && graph.detailValue(1) == "6", "the box stays on the stroke row");
                    doc_->undoStack().undo();
                    expect(doc_->findById(rectId)->strokeWidth() == width, "which is one undo step");
                }
                doc_->setSelection({textId});
                doc_->setSelection({rectId});
                expect(graph.detailCaption(0) == "Curves" && graph.detailValue(0) == "4 anchors, closed, 50 x 30", "a path node shows its curves as a summary, not as text");
                expect(graph.detailLinks(true).empty() && graph.detailLinks(false).size() == 1 && graph.detailLinks(false)[0] == "Fill", "and what feeds it and what uses it, as lists under the values");

                doc_->setSelection({textId});
                expect(graph.detailCaption(0) == "Words" && graph.detailValue(0) == "Hello" && graph.detailShown(2), "a text node shows words, font and size");
                graph.setDetailValue(0, "Bye");
                expect(doc_->findById(textId)->text() == "Bye", "editing the words in the box edits the text");
                expect(doc_->undoStack().canUndo(), "as an undo step");
                doc_->undoStack().undo();
                expect(doc_->findById(textId)->text() == "Hello" && graph.detailValue(0) == "Hello", "which undoes, and the box follows");
                graph.setDetailValue(2, "40");
                expect(doc_->findById(textId)->fontSize() == 40.0, "the size field sets the font size");
                graph.setDetailValue(2, "oops");
                expect(doc_->findById(textId)->fontSize() == 40.0, "a size that is not a number is ignored");

                graph.setFilter("hel");
                expect(graph.rows().size() == 2 && row("\"Hello\"") != nullptr && row("Box") == nullptr && row("\"Hello\"")->dim == false, "the filter keeps matching rows and the layer above them");
                graph.setFilter("");
                expect(row("Box") != nullptr, "an empty filter shows everything again");

                graph.setMode(GraphDock::Mode::Flow);
                std::size_t nodes = 0;
                for (const GraphRow& r : graph.rows()) {
                    nodes += r.kind == GraphRow::Kind::Node ? 1 : 0;
                }
                expect(nodes == doc_->network().nodeIds().size() && graph.rows().front().kind == GraphRow::Kind::Node, "Flow view lists the nodes");
                graph.setMode(GraphDock::Mode::Document);

                doc_->setSelection({});
                expect(graph.detailNode() == 0 && graph.title() == "Nothing selected", "no selection, no node in the box");
                w_.runCommand("view.graph");
                expect(!model_->graphVisible() && !graph.dock()->isVisible() && !toggle->isChecked(), "the View ring's Graph slot hides it and the toggle follows");
                doc_->clearObjects();
                doc_->undoStack().clear();
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
