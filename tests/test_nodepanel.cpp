#include "testsupport.h"

#include "pythonconsole.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>
#include <newui/undostack.h>

#include "curvy/wranglecheck.h"

#include <jitnodes/graph/serialize.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

    class NodePanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Node", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            look_ = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        bool click(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return window().pressed(pen_, newui::Point(float(p.x), float(p.y)), newui::mbmLeftButton, 0);
        }
        std::size_t rowOf(const std::string& caption) {
            for (std::size_t i = 0; i < window().nodePanel().paramCount(); ++i) {
                if (window().nodePanel().paramCaption(i) == caption) {
                    return i;
                }
            }
            return ~std::size_t(0);
        }

        newui::PenState pen_;
        curvy::NodeRef layer_, look_;
    };

}

TEST_F(NodePanelTest, theNodePanelIsInTheRightDockAndNotInTheGraphView) {
    curvy::Panel& panel = window().nodePanel();
    EXPECT_EQ(panel.id(), "node");
    EXPECT_EQ(panel.title(), "Node");
    ASSERT_NE(window().rightDock().panel("node"), nullptr);
    EXPECT_EQ(window().rightDock().panel("node"), &panel);
    EXPECT_NE(window().panelLayout().find("node"), nullptr);
    EXPECT_EQ(window().graph().findView("graphNameField"), nullptr);
    EXPECT_NE(panel.findView("graphNameField"), nullptr);
    EXPECT_NE(panel.findView("graphDataButton"), nullptr);
}

TEST_F(NodePanelTest, nothingIsShownUntilSomethingIsSelected) {
    EXPECT_EQ(window().nodePanel().title(), "Nothing selected");
    EXPECT_EQ(window().nodePanel().paramCount(), 0u);
}

TEST_F(NodePanelTest, clickingAShapeOnTheCanvasShowsItsNodeWithTheGraphHidden) {
    ASSERT_FALSE(window().graphShown());
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_EQ(window().nodePanel().kind(), "circle");
    EXPECT_EQ(window().nodePanel().title(), window().document().node(window().graph().selected()).name());
    EXPECT_NE(rowOf("radius"), ~std::size_t(0));

    EXPECT_TRUE(click(600.0, 700.0));   // empty canvas clears the selection
    EXPECT_EQ(window().nodePanel().title(), "Nothing selected");
}

TEST_F(NodePanelTest, aValueTypedIntoThePanelEditsTheNodeAsOneUndoStep) {
    ASSERT_TRUE(click(100.0, 100.0));
    const curvy::NodeRef circle = window().graph().selected();
    const std::size_t radius = rowOf("radius");
    ASSERT_NE(radius, ~std::size_t(0));
    window().nodePanel().setParam(radius, "12");
    EXPECT_EQ(window().document().node(circle).getFloat("radius"), 12.0f);
    window().document().undoStack().undo();
    EXPECT_EQ(window().document().node(circle).getFloat("radius"), 30.0f);
}

TEST_F(NodePanelTest, aLengthRowShowsAndReadsTheDocumentsUnit) {
    ASSERT_TRUE(click(100.0, 100.0));
    const curvy::NodeRef circle = window().graph().selected();
    window().document().setUnits(curvy::DocumentUnits{curvy::LengthUnit::Inches, curvy::AngleUnit::Degrees});
    window().nodePanel().show(circle);
    const std::size_t radius = rowOf("radius");
    ASSERT_NE(radius, ~std::size_t(0));
    EXPECT_EQ(window().nodePanel().paramValue(radius), "0.416667") << "30 pt is 5/12 in";

    window().nodePanel().setParam(radius, "2");
    EXPECT_NEAR(window().document().node(circle).getFloat("radius"), 144.0f, 1e-3f) << "a bare number is in the document's unit";
    window().nodePanel().setParam(radius, "25.4mm");
    EXPECT_NEAR(window().document().node(circle).getFloat("radius"), 72.0f, 1e-3f) << "a typed suffix wins";
    window().document().undoStack().undo();
    EXPECT_NEAR(window().document().node(circle).getFloat("radius"), 144.0f, 1e-3f);

    window().nodePanel().setParam(radius, "abc");
    EXPECT_NEAR(window().document().node(circle).getFloat("radius"), 144.0f, 1e-3f) << "text that is not a length is refused";
}

TEST_F(NodePanelTest, enterOnUnchangedTextLeavesTheStoredValueAndUndoAlone) {
    ASSERT_TRUE(click(100.0, 100.0));
    const curvy::NodeRef circle = window().graph().selected();
    window().document().setUnits(curvy::DocumentUnits{curvy::LengthUnit::Inches, curvy::AngleUnit::Degrees});
    window().nodePanel().show(circle);
    const std::size_t radius = rowOf("radius");
    ASSERT_NE(radius, ~std::size_t(0));
    const std::string shown = window().nodePanel().paramValue(radius);
    const std::string lastStep = window().document().undoStack().canUndo() ? window().document().undoStack().undoDescription() : "";
    window().nodePanel().setParam(radius, shown);
    EXPECT_EQ(window().document().node(circle).getFloat("radius"), 30.0f) << "0.416667 in must not replace 30 pt";
    EXPECT_EQ(window().document().undoStack().undoDescription(), lastStep) << "no undo step was recorded";
}

TEST_F(NodePanelTest, aPlainNumberRowIgnoresTheDocumentsUnit) {
    ASSERT_TRUE(click(100.0, 100.0));
    const curvy::NodeRef circle = window().graph().selected();
    window().document().setUnits(curvy::DocumentUnits{curvy::LengthUnit::Millimeters, curvy::AngleUnit::Radians});
    window().nodePanel().show(circle);
    const std::size_t count = rowOf("count");
    ASSERT_NE(count, ~std::size_t(0));
    EXPECT_EQ(window().nodePanel().paramValue(count), "96");
}

TEST_F(NodePanelTest, selectingInTheGraphViewShowsTheSameNode) {
    window().toggleGraph();
    window().graph().select(layer_);
    EXPECT_EQ(window().nodePanel().title(), window().document().node(layer_).name());
    EXPECT_EQ(window().nodePanel().kind(), window().document().node(layer_).typeName());
}

TEST_F(NodePanelTest, theDockOpensWhenTheGraphIsShown) {
    ASSERT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    window().toggleGraph();
    EXPECT_TRUE(window().graphShown());
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, theDockOpensWhenAClickSelectsAShape) {
    ASSERT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    EXPECT_FALSE(click(600.0, 700.0));   // empty canvas selects nothing and leaves the dock alone
    EXPECT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, aDockTheUserClosedStaysClosedUntilSomethingNeedsIt) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    window().docks().setCollapsed(curvy::DockSide::Right, true);
    EXPECT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    window().toggleGraph();
    window().toggleGraph();   // hiding the graph does not close the dock again
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, theBodyIsTallEnoughForTheLastLineNotJustMostOfIt) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    ASSERT_TRUE(click(100.0, 100.0));
    curvy::NodePanel& panel = window().nodePanel();
    EXPECT_FALSE(panel.userResized());
    const newui::View* last = panel.findView("graphUsed0");
    ASSERT_NE(last, nullptr);
    EXPECT_GE(last->bounds().height(), last->desiredSize().height) << "the last line is squeezed: the panel's preferred height left something out";
    EXPECT_LE(panel.contentBottom(), panel.body().bounds().height() + 0.5f);
    EXPECT_GE(panel.bodyHeight(), panel.contentHeightFor(curvy::NodePanel::kPreferredParamRows));
}

TEST_F(NodePanelTest, theInputsAndUsersNameTheNodesInsteadOfShowingABareNumber) {
    ASSERT_TRUE(click(100.0, 100.0));
    window().graph().select(look_);   // the fill: one input, fed by the circle
    const auto text = [&](const char* id) {
        const auto* label = dynamic_cast<const newui::Label*>(window().nodePanel().findView(id));
        return label != nullptr ? label->text() : std::string("(no label)");
    };
    EXPECT_EQ(text("graphInput0"), "circle") << "one input: no number, just what feeds it";
    EXPECT_NE(text("graphUsed0").find("merge"), std::string::npos);
    EXPECT_NE(text("graphUsed0"), "0  merge") << "the old form read like a count";

    window().graph().select(layer_);   // the layer feeds the root merge
    EXPECT_NE(text("graphUsed0"), "(not used)");
}

TEST_F(NodePanelTest, aNodeNothingUsesSaysSo) {
    ASSERT_TRUE(click(100.0, 100.0));
    window().graph().select(window().graph().selected());   // keep the circle, then pick the render node, which nothing consumes
    const curvy::NodeRef render = [&]() {
        for (const jitnodes::NodeId id : window().document().network().nodeIds()) {
            if (window().document().network().node(id).typeName() == "curvyRender") {
                return curvy::NodeRef(id);
            }
        }
        return curvy::NodeRef();
    }();
    ASSERT_TRUE(render.valid());
    window().graph().select(render);
    const auto* label = dynamic_cast<const newui::Label*>(window().nodePanel().findView("graphUsed0"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "(not used)");
}

TEST_F(NodePanelTest, thePanelCanBeCollapsedLikeAnyOther) {
    curvy::Panel& panel = window().nodePanel();
    const float open = panel.desiredHeight();
    panel.setCollapsed(true);
    EXPECT_LT(panel.desiredHeight(), open);
}

namespace {

    class WrangleCodeTest : public NodePanelTest {
    protected:
        void SetUp() override {
            NodePanelTest::SetUp();
            window().docks().setCollapsed(curvy::DockSide::Right, false);
            frame_.rootView().updateLayout();
            wrangle_ = window().document().addGraphNode("wrangle");
            const curvy::NodeRef source = window().document().addGraphNode("circle");
            window().document().connectGraphNodes(source, wrangle_);
            window().nodePanel().show(wrangle_);
        }

        curvy::NodeRef wrangle_;
    };

}

TEST_F(WrangleCodeTest, theCodeRowIsOnlyForAWrangleNode) {
    curvy::NodePanel& panel = window().nodePanel();
    EXPECT_TRUE(panel.codeShown());
    panel.show(look_);
    EXPECT_FALSE(panel.codeShown());
    panel.show(wrangle_);
    EXPECT_TRUE(panel.codeShown());
}

TEST_F(WrangleCodeTest, openingTheSectionAsksForAWiderPanelAndClosingGivesItBack) {
    curvy::NodePanel& panel = window().nodePanel();
    std::vector<bool> calls;
    panel.onCodeToggled = [&](bool open) { calls.push_back(open); };
    const float before = panel.contentHeightFor(3);
    panel.setCodeOpen(true);
    EXPECT_TRUE(panel.codeOpen());
    EXPECT_GT(panel.contentHeightFor(3), before);
    panel.setCodeOpen(true);
    panel.setCodeOpen(false);
    EXPECT_EQ(calls, (std::vector<bool>{true, false}));
    EXPECT_FLOAT_EQ(panel.contentHeightFor(3), before);
}

TEST_F(WrangleCodeTest, commitStoresTheTextAsOneUndoStepAndRevertPutsTheNodesTextBack) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    EXPECT_FALSE(panel.codeModified());
    window().document().setGraphString(wrangle_, "code", "y = 1;");
    panel.show(wrangle_);
    EXPECT_EQ(panel.codeText(), "y = 1;");

    panel.codeEditor()->setText("y = 2;");
    EXPECT_TRUE(panel.codeModified());
    panel.revertCode();
    EXPECT_EQ(panel.codeText(), "y = 1;");
    EXPECT_FALSE(panel.codeModified());

    panel.codeEditor()->setText("y = 3;");
    EXPECT_TRUE(panel.codeModified());
    panel.commitCode();
    EXPECT_EQ(window().document().node(wrangle_).getString("code"), "y = 3;");
    EXPECT_FALSE(panel.codeModified());
    window().document().undoStack().undo();
    EXPECT_EQ(window().document().node(wrangle_).getString("code"), "y = 1;");
}

TEST_F(WrangleCodeTest, anEditNotCommittedSurvivesARefreshOfThePanel) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("y = 1;");
    panel.codeEditor()->setText("y = 5;");
    panel.show(wrangle_);
    EXPECT_EQ(panel.codeText(), "y = 5;");
}

TEST_F(WrangleCodeTest, declaredParametersBecomeRowsAndOneWithBothLimitsHasASlider) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 1.5(0, 10), steps(1,) |\ny = y * gain;");
    panel.commitCode();
    const std::size_t gain = rowOf("gain");
    const std::size_t steps = rowOf("steps");
    ASSERT_NE(gain, ~std::size_t(0));
    ASSERT_NE(steps, ~std::size_t(0));
    EXPECT_LT(std::max(gain, steps), rowOf("a"));
    EXPECT_TRUE(panel.paramHasSlider(gain));
    EXPECT_FALSE(panel.paramHasSlider(steps));
    EXPECT_EQ(panel.paramValue(gain), "1.5");
    panel.setParam(gain, "99");
    EXPECT_EQ(panel.paramValue(gain), "10");
    panel.setParam(steps, "-4");
    EXPECT_EQ(panel.paramValue(steps), "1");
}

TEST_F(WrangleCodeTest, aCookErrorShowsUnderTheEditorAndGoesWhenTheCodeIsFixed) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("y = nosuch(y);");
    panel.commitCode();
    EXPECT_FALSE(panel.cookError().empty());
    panel.setCodeText("y = y + 1;");
    panel.commitCode();
    EXPECT_TRUE(panel.cookError().empty());
}

TEST_F(WrangleCodeTest, theClassButtonSwitchesBetweenPointsAndPrimitivesAsOneUndoStep) {
    curvy::NodePanel& panel = window().nodePanel();
    EXPECT_EQ(panel.codeClass(), "point");
    panel.setCodeClass("primitive");
    EXPECT_EQ(window().document().node(wrangle_).getString("class"), "primitive");
    window().document().undoStack().undo();
    EXPECT_EQ(window().document().node(wrangle_).getString("class"), "point");
}

TEST_F(NodePanelTest, theParameterListIsAsTallAsItsRowsSoInputsFollowItWithoutAGap) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    ASSERT_TRUE(click(100.0, 100.0));
    curvy::NodePanel& panel = window().nodePanel();
    const newui::View* list = panel.findView("graphParamScroll");
    const newui::View* inputs = panel.findView("graphInput0");
    ASSERT_NE(list, nullptr);
    ASSERT_NE(inputs, nullptr);
    ASSERT_GT(panel.paramCount(), 0u);
    ASSERT_LT(panel.paramCount(), curvy::NodePanel::kPreferredParamRows);
    EXPECT_NEAR(list->bounds().height(), float(panel.paramCount()) * curvy::NodePanel::kParamRowHeight, 0.5f);
    EXPECT_LT(inputs->bounds().top(), list->bounds().top() + list->bounds().height() + 40.0f);
}

TEST_F(WrangleCodeTest, undoAndRedoTakeTheDeclaredParametersAwayAndBack) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain;");
    panel.commitCode();
    jitnodes::Node& node = window().document().node(wrangle_);
    ASSERT_TRUE(node.hasFloat("gain"));
    window().document().setGraphParam(wrangle_, "gain", 7.0f);

    window().document().undoStack().undo();
    EXPECT_FLOAT_EQ(node.getFloat("gain"), 2.0f);
    window().document().undoStack().undo();
    panel.show(wrangle_);
    EXPECT_FALSE(window().document().node(wrangle_).hasFloat("gain"));
    EXPECT_EQ(rowOf("gain"), ~std::size_t(0));
    EXPECT_EQ(panel.codeText(), "");

    window().document().undoStack().redo();
    window().document().undoStack().redo();
    panel.show(wrangle_);
    EXPECT_FLOAT_EQ(window().document().node(wrangle_).getFloat("gain"), 7.0f);
    EXPECT_NE(rowOf("gain"), ~std::size_t(0));
    EXPECT_TRUE(panel.paramHasSlider(rowOf("gain")));
}

TEST_F(WrangleCodeTest, editingTheCodeKeepsTheValueOfAParameterThatStays) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10), steps = 1 |\ny = y * gain;");
    panel.commitCode();
    panel.setParam(rowOf("gain"), "6");
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain * 2;");
    panel.commitCode();
    EXPECT_FLOAT_EQ(window().document().node(wrangle_).getFloat("gain"), 6.0f);
    EXPECT_EQ(rowOf("steps"), ~std::size_t(0));
}

TEST_F(WrangleCodeTest, aCopyOfTheNodeHasTheDeclaredParametersAndTheirLimits) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain;");
    panel.commitCode();
    window().document().setGraphParam(wrangle_, "gain", 5.0f);

    jitnodes::Network& net = window().document().network();
    const std::vector<jitnodes::NodeId> pasted = jitnodes::pasteNodes(net, {jitnodes::copyNode(net, wrangle_.id)});
    ASSERT_EQ(pasted.size(), 1u);
    const curvy::NodeRef copy(pasted.front());
    ASSERT_TRUE(window().document().hasNode(copy));
    const jitnodes::Node& node = window().document().node(copy);
    EXPECT_TRUE(node.hasFloat("gain"));
    EXPECT_FLOAT_EQ(node.getFloat("gain"), 5.0f);
    float low = -1.0f, high = -1.0f;
    EXPECT_TRUE(node.floatMinimum("gain", low));
    EXPECT_TRUE(node.floatMaximum("gain", high));
    EXPECT_FLOAT_EQ(low, 0.0f);
    EXPECT_FLOAT_EQ(high, 10.0f);

    panel.show(copy);
    EXPECT_EQ(panel.codeText(), "| gain = 2(0, 10) |\ny = y * gain;");
    ASSERT_NE(rowOf("gain"), ~std::size_t(0));
    EXPECT_TRUE(panel.paramHasSlider(rowOf("gain")));
}

TEST_F(WrangleCodeTest, everySampleWrangleParsesDeclaresParametersAndCooks) {
    namespace fs = std::filesystem;
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    std::size_t count = 0;
    ASSERT_TRUE(fs::exists("Resources/samples/wrangle"));
    for (const fs::directory_entry& entry : fs::directory_iterator("Resources/samples/wrangle")) {
        if (entry.path().extension() != ".wrangle") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SCOPED_TRACE(entry.path().string());
        EXPECT_FALSE(curvy::checkWrangle(code).has_value());
        panel.setCodeText(code);
        panel.commitCode();
        EXPECT_EQ(panel.cookError(), "");
        EXPECT_FALSE(window().document().node(wrangle_).spareNames().empty());
        ++count;
    }
    EXPECT_GE(count, 4u);
}

TEST_F(NodePanelTest, aLayerShowsOpacityAndABlendDropdownThatSetsTheBlendIndex) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    curvy::NodePanel& panel = window().nodePanel();
    panel.show(layer_);
    const std::size_t opacity = rowOf("opacity");
    const std::size_t blend = rowOf("blend");
    ASSERT_NE(opacity, ~std::size_t(0));
    ASSERT_NE(blend, ~std::size_t(0));
    EXPECT_TRUE(panel.paramIsChoice(blend));
    EXPECT_FALSE(panel.paramIsChoice(opacity));
    EXPECT_EQ(panel.paramChoice(blend), "Normal");
    panel.chooseParam(blend, 1);
    EXPECT_FLOAT_EQ(window().document().node(layer_).getFloat("blend"), 1.0f);
    EXPECT_EQ(panel.paramChoice(rowOf("blend")), "Multiply");
    window().document().undoStack().undo();
    panel.show(layer_);
    EXPECT_EQ(panel.paramChoice(rowOf("blend")), "Normal");
    panel.setParam(rowOf("opacity"), "3");
    EXPECT_EQ(panel.paramValue(rowOf("opacity")), "1");
    panel.setParam(rowOf("opacity"), "0.25");
    EXPECT_FLOAT_EQ(window().document().node(layer_).getFloat("opacity"), 0.25f);
}

TEST_F(NodePanelTest, aLayerOfTheOpenedSampleShowsOpacityAndBlend) {
    const std::string sample = (std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples" / "layersAndGroups.curvy").u8string();
    ASSERT_TRUE(window().document().load(sample)) << window().document().lastError();
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    const curvy::NodeRef layer = window().document().childrenOf()[0];
    window().nodePanel().show(layer);
    EXPECT_NE(rowOf("opacity"), ~std::size_t(0));
    EXPECT_NE(rowOf("blend"), ~std::size_t(0));
}

TEST_F(WrangleCodeTest, draggingASliderIsOneUndoStepAndUndoPutsTheValueBack) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain;");
    panel.commitCode();
    const std::size_t row = rowOf("gain");
    ASSERT_NE(row, ~std::size_t(0));
    auto* slider = dynamic_cast<newui::Slider*>(panel.findView("nodeSlider" + std::to_string(row)));
    ASSERT_NE(slider, nullptr);
    ASSERT_TRUE(slider->isVisible());

    newui::UndoStack& undo = window().document().undoStack();
    const std::size_t before = undo.depth();
    slider->setValue(4.0f);   // the first change of a gesture, as a click on the track makes before anything else
    slider->setValue(6.0f);
    slider->setValue(8.0f);
    EXPECT_FLOAT_EQ(window().document().node(wrangle_).getFloat("gain"), 8.0f);
    EXPECT_EQ(undo.depth(), before) << "the drag is still live";
    slider->onMouseUp(*slider, newui::Point(0.0f, 0.0f), newui::mbmLeftButton, 0);
    EXPECT_EQ(undo.depth(), before + 1);
    undo.undo();
    EXPECT_FLOAT_EQ(window().document().node(wrangle_).getFloat("gain"), 2.0f);
}

TEST_F(WrangleCodeTest, letGoOfTheThumbEndsTheGestureToo) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain;");
    panel.commitCode();
    auto* slider = dynamic_cast<newui::Slider*>(panel.findView("nodeSlider" + std::to_string(rowOf("gain"))));
    ASSERT_NE(slider, nullptr);
    ASSERT_NE(slider->thumb(), nullptr);
    const std::size_t before = window().document().undoStack().depth();
    slider->setValue(5.0f);
    slider->setValue(7.0f);
    EXPECT_EQ(window().document().undoStack().depth(), before);
    slider->thumb()->onMouseUp(*slider->thumb(), newui::Point(0.0f, 0.0f), newui::mbmLeftButton, 0);
    EXPECT_EQ(window().document().undoStack().depth(), before + 1);
    slider->setValue(9.0f);
    slider->thumb()->onMouseUp(*slider->thumb(), newui::Point(0.0f, 0.0f), newui::mbmLeftButton, 0);
    EXPECT_EQ(window().document().undoStack().depth(), before + 2) << "the next drag is a step of its own";
}

TEST_F(WrangleCodeTest, aGestureStillOpenWhenAnotherNodeIsShownIsRecorded) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.setCodeText("| gain = 2(0, 10) |\ny = y * gain;");
    panel.commitCode();
    auto* slider = dynamic_cast<newui::Slider*>(panel.findView("nodeSlider" + std::to_string(rowOf("gain"))));
    ASSERT_NE(slider, nullptr);
    const std::size_t before = window().document().undoStack().depth();
    slider->setValue(5.0f);
    panel.show(look_);
    EXPECT_EQ(window().document().undoStack().depth(), before + 1);
}

TEST_F(NodePanelTest, choosingInTheBlendDropdownSetsTheObjectsBlend) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    curvy::NodePanel& panel = window().nodePanel();
    panel.show(layer_);
    const std::size_t row = rowOf("blend");
    ASSERT_NE(row, ~std::size_t(0));
    auto* list = dynamic_cast<newui::DropDownList*>(panel.findView("nodeChoice" + std::to_string(row)));
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->isVisible());
    list->setSelectedIndex(2);
    EXPECT_FLOAT_EQ(window().document().node(layer_).getFloat("blend"), 2.0f);
    EXPECT_EQ(panel.paramChoice(rowOf("blend")), "Screen");
}

TEST_F(WrangleCodeTest, openingTheCodeSectionWidensTheRightDockAndClosingRestoresIt) {
    window().docks().setSize(curvy::DockSide::Right, 260.0f);
    window().nodePanel().setCodeOpen(true);
    EXPECT_GE(window().docks().size(curvy::DockSide::Right), curvy::NodePanel::kCodeWidth);
    window().nodePanel().setCodeOpen(false);
    EXPECT_FLOAT_EQ(window().docks().size(curvy::DockSide::Right), 260.0f);
}

TEST_F(WrangleCodeTest, aDockAlreadyWideEnoughIsLeftAlone) {
    window().docks().setSize(curvy::DockSide::Right, 500.0f);
    window().nodePanel().setCodeOpen(true);
    EXPECT_FLOAT_EQ(window().docks().size(curvy::DockSide::Right), 500.0f);
    window().nodePanel().setCodeOpen(false);
    EXPECT_FLOAT_EQ(window().docks().size(curvy::DockSide::Right), 500.0f);
}

namespace {

    class PythonCodeTest : public NodePanelTest {
    protected:
        void SetUp() override {
            NodePanelTest::SetUp();
            curvytest::sharedConsole().setMainThreadPoster([](std::function<void()>) {});
            window().setScriptConsole(&curvytest::sharedConsole());
            window().pythonNodeTrust()->setTrustFile({});
            window().docks().setCollapsed(curvy::DockSide::Right, false);
            frame_.rootView().updateLayout();
            python_ = window().document().addGraphNode("python");
            window().nodePanel().show(python_);
        }

        void TearDown() override {
            NodePanelTest::TearDown();
            curvytest::sharedConsole().setMainThreadPoster(nullptr);
        }

        curvy::NodeRef python_;
    };

}

TEST_F(PythonCodeTest, aPythonNodeGetsTheCodeSectionWithItsOwnEditorAndNoClassButton) {
    curvy::NodePanel& panel = window().nodePanel();
    EXPECT_TRUE(panel.codeShown());
    EXPECT_TRUE(panel.codeIsPython());
    EXPECT_FALSE(panel.findView("wrangleClassButton")->isVisible());

    const curvy::NodeRef wrangle = window().document().addGraphNode("wrangle");
    panel.show(wrangle);
    EXPECT_FALSE(panel.codeIsPython());
    EXPECT_TRUE(panel.findView("wrangleClassButton")->isVisible());
    panel.show(python_);
    EXPECT_TRUE(panel.codeIsPython());
}

TEST_F(PythonCodeTest, committingTrustsTheTypedTextSoTheNodeCooksWithoutAskingAndIsOneUndoStep) {
    curvy::NodePanel& panel = window().nodePanel();
    window().pythonNodeTrust()->setPrompt([](std::size_t) {
        ADD_FAILURE() << "the user's own text must not be asked about";
        return false;
    });
    panel.setCodeOpen(true);
    panel.codeEditor()->setText("geo.resize_points(4)");
    EXPECT_TRUE(panel.codeModified());
    panel.commitCode();

    EXPECT_EQ(window().document().node(python_).getString("code"), "geo.resize_points(4)");
    EXPECT_EQ(window().document().cook(python_)->pointCount(), 4u);
    EXPECT_EQ(panel.cookError(), "");
    EXPECT_EQ(window().pythonNodeTrust()->pendingCount(), 0u);
    window().document().undoStack().undo();
    EXPECT_EQ(window().document().node(python_).getString("code"), "");
}

TEST_F(PythonCodeTest, aFailingScriptShowsTheLastLineOfItsTraceback) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.codeEditor()->setText("x = 1\nraise ValueError('boom')");
    panel.commitCode();
    EXPECT_EQ(panel.cookError(), "ValueError: boom");
}

TEST_F(PythonCodeTest, aSyntaxErrorIsNamedUnderTheEditor) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.codeEditor()->setText("def (");
    panel.codeEditor()->checkSyntax();
    EXPECT_NE(panel.codeProblem(), "");
    panel.codeEditor()->setText("x = 1");
    panel.codeEditor()->checkSyntax();
    EXPECT_EQ(panel.codeProblem(), "");
}

TEST_F(PythonCodeTest, aRefusedNodeShowsAnAllowButtonThatAllowsItsCodeAndCooksIt) {
    curvy::NodePanel& panel = window().nodePanel();
    EXPECT_FALSE(panel.allowShown()) << "an empty node needs no allowance";
    window().document().setGraphString(python_, "code", "geo.resize_points(2)");   // written by something other than the user's typing
    panel.show(python_);

    EXPECT_EQ(panel.cookError().rfind("Python nodes are not allowed here", 0), 0u);
    EXPECT_TRUE(panel.allowShown());

    panel.allowCode();
    EXPECT_EQ(panel.cookError(), "");
    EXPECT_FALSE(panel.allowShown());
    EXPECT_TRUE(window().pythonNodeTrust()->isTrusted("geo.resize_points(2)"));
    EXPECT_EQ(window().document().cook(python_)->pointCount(), 2u);
}

TEST_F(PythonCodeTest, allowWorksAfterTheQuestionWasDeclined) {
    curvy::NodePanel& panel = window().nodePanel();
    window().pythonNodeTrust()->setPrompt([](std::size_t) { return false; });
    window().document().setGraphString(python_, "code", "geo.resize_points(3)");
    panel.show(python_);
    EXPECT_FALSE(window().pythonNodeTrust()->askPending());
    panel.show(python_);
    ASSERT_TRUE(panel.allowShown()) << "declined code stays refused for the session, and the button is the way back";

    panel.allowCode();
    EXPECT_EQ(panel.cookError(), "");
    EXPECT_EQ(window().document().cook(python_)->pointCount(), 3u);
}

TEST_F(PythonCodeTest, theAllowButtonIsOnlyForARefusalNotForOtherErrorsOrOtherNodes) {
    curvy::NodePanel& panel = window().nodePanel();
    panel.setCodeOpen(true);
    panel.codeEditor()->setText("raise ValueError('boom')");
    panel.commitCode();
    EXPECT_EQ(panel.cookError(), "ValueError: boom");
    EXPECT_FALSE(panel.allowShown());

    const curvy::NodeRef wrangle = window().document().addGraphNode("wrangle");
    panel.show(wrangle);
    EXPECT_FALSE(panel.allowShown());
    panel.allowCode();   // nothing to allow: no effect, no crash
    EXPECT_EQ(window().pythonNodeTrust()->pendingCount(), 0u);
}
