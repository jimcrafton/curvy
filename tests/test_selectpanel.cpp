#include "testsupport.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>

namespace {

    // A red and a blue circle in one layer; the layer carries a user tag, and the red circle's node another.
    class SelectPanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Select", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            window().canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            window().document().addCircle(layer_, 300.0f, 100.0f, 30.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
            window().document().node(layer_).addTag("backdrop");
            clickCircle(100.0, 100.0);
            window().document().node(selection().primary()).addTag("hero");
            selection().clear();
            panel().refresh();
        }

        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        curvy::SelectPanel& panel() { return window().selectPanel(); }
        void clickCircle(double x, double y) {
            const BLPoint p = window().canvas().toView(BLPoint(x, y));
            window().pressed(pen_, newui::Point(float(p.x), float(p.y)), newui::mbmLeftButton, 0);
        }
        std::vector<std::string> texts() {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < panel().rowCount(); ++i) {
                out.push_back(panel().row(i)->text);
            }
            return out;
        }
        std::size_t countOf(const std::string& text) {
            for (std::size_t i = 0; i < panel().rowCount(); ++i) {
                if (panel().row(i)->text == text) {
                    return panel().row(i)->count;
                }
            }
            return 0;
        }

        newui::PenState pen_;
        curvy::NodeRef layer_;
    };

}

TEST_F(SelectPanelTest, describeShapesReadsTheLookTheTagsAndTheLayer) {
    const jitnodes::GeometryHandle artwork = window().document().artwork();
    ASSERT_NE(artwork, nullptr);
    const std::vector<curvy::ShapeValues> shapes = curvy::describeShapes(window().document(), *artwork);
    ASSERT_EQ(shapes.size(), 2u);
    std::vector<std::string> fills = {shapes[0].fill, shapes[1].fill};
    std::sort(fills.begin(), fills.end());
    EXPECT_EQ(fills, (std::vector<std::string>{"#0000FF", "#FF0000"}));
    for (const curvy::ShapeValues& shape : shapes) {
        EXPECT_EQ(shape.stroke, curvy::kNoValue);
        EXPECT_EQ(shape.width, curvy::kNoValue);
        EXPECT_EQ(shape.type, "circle");
        EXPECT_EQ(shape.layer, window().document().node(layer_).name());
        EXPECT_NE(std::find(shape.tags.begin(), shape.tags.end(), "backdrop"), shape.tags.end()) << "the layer's tag reaches the shapes inside it";
        EXPECT_EQ(std::find(shape.tags.begin(), shape.tags.end(), "layer"), shape.tags.end()) << "the document's own tags are not shown";
    }
    EXPECT_EQ(std::count_if(shapes.begin(), shapes.end(), [](const curvy::ShapeValues& s) { return std::find(s.tags.begin(), s.tags.end(), "hero") != s.tags.end(); }), 1);
}

TEST_F(SelectPanelTest, valuesAreCountedAndFoundByValue) {
    const jitnodes::GeometryHandle artwork = window().document().artwork();
    const std::vector<curvy::ShapeValues> shapes = curvy::describeShapes(window().document(), *artwork);
    const std::vector<curvy::ValueCount> tags = curvy::countValues(shapes, curvy::ShapeProperty::Tag);
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_EQ(tags[0].text, "backdrop") << "the most common first";
    EXPECT_EQ(tags[0].count, 2u);
    EXPECT_EQ(curvy::shapesWithValue(shapes, curvy::ShapeProperty::Tag, "hero").size(), 1u);
    EXPECT_EQ(curvy::shapesWithValue(shapes, curvy::ShapeProperty::Fill, "#FF0000").size(), 1u);
    EXPECT_TRUE(curvy::shapesWithValue(shapes, curvy::ShapeProperty::Fill, "#123456").empty());
}

TEST_F(SelectPanelTest, thePanelListsTheValuesOfTheChosenProperty) {
    EXPECT_EQ(panel().property(), curvy::ShapeProperty::Fill);
    EXPECT_EQ(panel().rowCount(), 2u);
    EXPECT_EQ(countOf("#FF0000"), 1u);
    EXPECT_TRUE(panel().row(0)->colour);
    panel().setProperty(curvy::ShapeProperty::Tag);
    EXPECT_EQ(countOf("backdrop"), 2u);
    EXPECT_EQ(countOf("hero"), 1u);
    panel().setProperty(curvy::ShapeProperty::Layer);
    EXPECT_EQ(panel().rowCount(), 1u);
    EXPECT_FALSE(panel().row(0)->colour);
}

TEST_F(SelectPanelTest, aValueSelectsEveryShapeThatHasItAndAddAndSubtractCombine) {
    EXPECT_TRUE(panel().choose("#FF0000", curvy::SelectionModel::Combine::Replace));
    EXPECT_EQ(selection().items().size(), 1u);
    EXPECT_TRUE(panel().choose("#0000FF", curvy::SelectionModel::Combine::Add));
    EXPECT_EQ(selection().items().size(), 2u);
    EXPECT_TRUE(panel().choose("#FF0000", curvy::SelectionModel::Combine::Subtract));
    EXPECT_EQ(selection().items().size(), 1u);
    EXPECT_FALSE(panel().choose("#123456", curvy::SelectionModel::Combine::Replace)) << "no shape has it";
    EXPECT_EQ(selection().items().size(), 1u);
    panel().setProperty(curvy::ShapeProperty::Tag);
    EXPECT_TRUE(panel().choose("backdrop", curvy::SelectionModel::Combine::Replace));
    EXPECT_EQ(selection().items().size(), 2u);
}

TEST_F(SelectPanelTest, theAddAndSubtractButtonsApplyTheLastValueClicked) {
    EXPECT_FALSE(panel().applyChosen(curvy::SelectionModel::Combine::Add)) << "nothing was chosen yet";
    panel().choose("#FF0000", curvy::SelectionModel::Combine::Replace);
    panel().choose("#0000FF", curvy::SelectionModel::Combine::Replace);
    EXPECT_EQ(panel().chosen(), "#0000FF");
    EXPECT_EQ(selection().items().size(), 1u);
    panel().setProperty(curvy::ShapeProperty::Fill);
    panel().choose("#FF0000", curvy::SelectionModel::Combine::Add);
    EXPECT_EQ(selection().items().size(), 2u);
    EXPECT_TRUE(panel().applyChosen(curvy::SelectionModel::Combine::Subtract));
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(SelectPanelTest, withinTheSelectionNarrowsTheValues) {
    panel().choose("#FF0000", curvy::SelectionModel::Combine::Replace);
    panel().setProperty(curvy::ShapeProperty::Tag);
    panel().setWithin(curvy::SelectPanel::Within::Selection);
    EXPECT_EQ(countOf("backdrop"), 1u);
    EXPECT_EQ(countOf("hero"), 1u);
    panel().setWithin(curvy::SelectPanel::Within::Document);
    EXPECT_EQ(countOf("backdrop"), 2u);
}

TEST_F(SelectPanelTest, similarSelectsWhatSharesTheSelectedShapesValue) {
    EXPECT_FALSE(panel().selectSimilar()) << "nothing is selected";
    clickCircle(100.0, 100.0);
    panel().refresh();
    panel().setProperty(curvy::ShapeProperty::Layer);
    EXPECT_TRUE(panel().row(0)->own) << "the row the selected shape has is marked";
    EXPECT_TRUE(panel().selectSimilar());
    EXPECT_EQ(selection().items().size(), 2u) << "both circles are in the one layer";
    EXPECT_FALSE(panel().selectSimilar()) << "two shapes are selected: there is no single shape's value to follow";
}

TEST_F(SelectPanelTest, theRingsBySlotShowsThePanel) {
    window().runCommand("select.by");
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
    EXPECT_FALSE(panel().collapsed());
}
