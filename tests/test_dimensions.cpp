#include "curvy/dimensionoverlay.h"
#include "curvy/dimensions.h"
#include "curvy/selecttool.h"

#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/pen.h>

#include <cmath>

namespace {

    using Kind = curvy::DimensionItem::Kind;

    // A layer with a circle of radius 30 at (100, 100) and one of radius 10 at (300, 200).
    struct Shapes {
        curvy::SketchDocument document;
        curvy::SelectionModel selection{&document};
        curvy::DimensionModel model{document, selection};
        curvy::NodeRef layer, first, second;

        Shapes() {
            document.newBlank("Dimensions", 612.0f, 792.0f);
            layer = document.addLayer();
            first = document.addCircle(layer, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            second = document.addCircle(layer, 300.0f, 200.0f, 10.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
        }

        // addCircle returns the circle's fill node; a click selects the node that made the shape (the circle), so the tests pick it the same way.
        void select(const curvy::NodeRef& node) {
            const jitnodes::GeometryHandle artwork = document.artwork();
            for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
                const std::string key(shape.key);
                if (document.nodeForShapeKey(key) == shapeOf(node)) {
                    selection.set(selection.pickShapeKey(key));
                }
            }
        }
        void addToSelection(const curvy::NodeRef& node) {
            const jitnodes::GeometryHandle artwork = document.artwork();
            for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
                const std::string key(shape.key);
                if (document.nodeForShapeKey(key) == shapeOf(node)) {
                    selection.add(selection.pickShapeKey(key));
                }
            }
        }
        curvy::NodeRef shapeOf(const curvy::NodeRef& look) { return selection.find(curvy::SelectionModel::Role::Shape, look); }

        std::optional<curvy::DimensionItem> item(Kind kind) {
            for (const curvy::DimensionItem& i : model.compute()) {
                if (i.kind == kind) {
                    return i;
                }
            }
            return std::nullopt;
        }
        double value(Kind kind) {
            const auto found = item(kind);
            return found.has_value() ? found->value : -1.0;
        }
        BLBox box() {
            BLBox b;
            model.bounds(b);
            return b;
        }
    };

}

TEST(Dimensions, aSelectedCircleIsMeasuredByBoxAndRadius) {
    Shapes s;
    EXPECT_TRUE(s.model.compute().empty()) << "nothing selected";
    s.select(s.first);
    EXPECT_NEAR(s.value(Kind::Width), 60.0, 1e-3);
    EXPECT_NEAR(s.value(Kind::Height), 60.0, 1e-3);
    EXPECT_NEAR(s.value(Kind::Radius), 30.0, 1e-3);
    EXPECT_FALSE(s.item(Kind::CurveLength).has_value());
    const BLBox box = s.box();
    EXPECT_NEAR(box.x0, 70.0, 1e-3);
    EXPECT_NEAR(box.y1, 130.0, 1e-3);
    EXPECT_NEAR(s.item(Kind::Width)->anchor.x, 100.0, 1e-3);
    EXPECT_NEAR(s.item(Kind::Width)->anchor.y, 130.0, 1e-3);
}

TEST(Dimensions, twoShapesAreMeasuredAsOneBoxAndHaveNoRadius) {
    Shapes s;
    s.select(s.first);
    s.addToSelection(s.second);
    EXPECT_NEAR(s.value(Kind::Width), 240.0, 1e-3) << "70 .. 310";
    EXPECT_NEAR(s.value(Kind::Height), 140.0, 1e-3) << "70 .. 210";
    EXPECT_FALSE(s.item(Kind::Radius).has_value());
}

TEST(Dimensions, aWidthIsSetAboutTheLeftEdgeInOneUndoStep) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Width, 120.0));
    EXPECT_NEAR(s.value(Kind::Width), 120.0, 1e-3);
    EXPECT_NEAR(s.value(Kind::Height), 60.0, 1e-3);
    EXPECT_NEAR(s.box().x0, 70.0, 1e-3) << "the left edge stays";
    EXPECT_NEAR(s.box().y0, 70.0, 1e-3);
    EXPECT_FALSE(s.item(Kind::Radius).has_value()) << "no longer a circle";
    EXPECT_EQ(s.document.undoStack().undoDescription(), "Change width");
    s.document.undoStack().undo();
    EXPECT_NEAR(s.value(Kind::Width), 60.0, 1e-3);
    s.document.undoStack().redo();
    EXPECT_NEAR(s.value(Kind::Width), 120.0, 1e-3);
}

TEST(Dimensions, aHeightIsSetAboutTheTopEdge) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Height, 30.0));
    EXPECT_NEAR(s.value(Kind::Height), 30.0, 1e-3);
    EXPECT_NEAR(s.value(Kind::Width), 60.0, 1e-3);
    EXPECT_NEAR(s.box().y0, 70.0, 1e-3);
}

TEST(Dimensions, aRadiusIsSetAboutTheCentre) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Radius, 15.0));
    EXPECT_NEAR(s.value(Kind::Radius), 15.0, 1e-3);
    EXPECT_NEAR(s.box().x0, 85.0, 1e-3);
    EXPECT_NEAR(s.box().y1, 115.0, 1e-3);
}

TEST(Dimensions, aNewShapeTransformHasItsPivotAtTheCentreOfTheShape) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Width, 120.0));
    const std::map<std::string, float> xform = s.document.shapeTransform(s.shapeOf(s.first));
    EXPECT_NEAR(xform.at("px"), 100.0f, 1e-3f);
    EXPECT_NEAR(xform.at("py"), 100.0f, 1e-3f);
}

TEST(Dimensions, aWidthSetAboutThePivotKeepsTheShapeCentred) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Width, 120.0, true));
    EXPECT_NEAR(s.value(Kind::Width), 120.0, 1e-3);
    EXPECT_NEAR(s.box().x0, 40.0, 1e-3);
    EXPECT_NEAR(s.box().x1, 160.0, 1e-3);
    EXPECT_NEAR(s.box().y0, 70.0, 1e-3) << "the height is untouched";
}

TEST(Dimensions, aLayerPivotIsAtTheCentreOfItsContentsOnceTheLayerIsTransformed) {
    Shapes s;
    s.document.setObjectTransform(s.layer, {{"rot", 90.0f}});
    const std::map<std::string, float> xform = s.document.objectTransform(s.layer);
    EXPECT_NEAR(xform.at("px"), 190.0f, 1e-3f) << "the box of both circles is 70 .. 310";
    EXPECT_NEAR(xform.at("py"), 140.0f, 1e-3f);
}

TEST(Dimensions, aSelectedShapeShowsItsOwnPivotAndItsLayersOnTheCanvas) {
    Shapes s;
    s.select(s.first);
    const std::vector<curvy::DimensionModel::PivotMark> marks = s.model.pivots();
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_EQ(marks[0].level, curvy::DimensionModel::PivotMark::Level::Shape);
    EXPECT_NEAR(marks[0].at.x, 100.0, 1e-3);
    EXPECT_NEAR(marks[0].at.y, 100.0, 1e-3);
    EXPECT_EQ(marks[1].level, curvy::DimensionModel::PivotMark::Level::Object);
    EXPECT_NEAR(marks[1].at.x, 190.0, 1e-3);
    EXPECT_NEAR(marks[1].at.y, 140.0, 1e-3);
}

TEST(Dimensions, movingAPivotMovesNothingOnScreenAndIsOneUndoStep) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Width, 120.0));
    const BLBox before = s.box();
    const std::vector<curvy::DimensionModel::PivotMark> marks = s.model.pivots();
    ASSERT_FALSE(marks.empty());
    ASSERT_TRUE(s.model.beginPivotMove(marks[0]));
    ASSERT_TRUE(s.model.previewPivotMove(BLPoint(50.0, 60.0)));
    ASSERT_FALSE(s.model.pivots().empty());
    EXPECT_NEAR(s.model.pivots()[0].at.x, 50.0, 1e-3) << "mid-drag the mark is already where the cursor is";
    ASSERT_TRUE(s.model.commit());
    const BLBox after = s.box();
    EXPECT_NEAR(after.x0, before.x0, 1e-3);
    EXPECT_NEAR(after.x1, before.x1, 1e-3);
    EXPECT_NEAR(after.y0, before.y0, 1e-3);
    const std::vector<curvy::DimensionModel::PivotMark> moved = s.model.pivots();
    ASSERT_FALSE(moved.empty());
    EXPECT_NEAR(moved[0].at.x, 50.0, 1e-3) << "the mark is where it was dropped, on the shape as it is shown";
    EXPECT_NEAR(moved[0].at.y, 60.0, 1e-3);
    EXPECT_EQ(s.document.undoStack().undoDescription(), "Move Pivot");
    s.document.undoStack().undo();
    EXPECT_NEAR(s.model.pivots()[0].at.x, 130.0, 1e-3);
}

TEST(Dimensions, aPivotAtTheCentreStaysAtTheCentreWhenTheShapeIsResized) {
    Shapes s;
    s.select(s.first);
    ASSERT_TRUE(s.model.apply(Kind::Width, 120.0));
    const std::vector<curvy::DimensionModel::PivotMark> marks = s.model.pivots();
    ASSERT_FALSE(marks.empty());
    EXPECT_NEAR(marks[0].at.x, 130.0, 1e-3) << "the box is now 70 .. 190";
    EXPECT_NEAR(marks[0].at.y, 100.0, 1e-3);
}

TEST(Dimensions, aPreviewMakesNoUndoStepAndCancelPutsTheGraphBack) {
    Shapes s;
    s.select(s.first);
    const std::string before = s.document.undoStack().undoDescription();
    const std::size_t nodes = s.document.network().nodeCount();
    ASSERT_TRUE(s.model.begin(Kind::Width));
    EXPECT_TRUE(s.model.editing());
    ASSERT_TRUE(s.model.preview(90.0));
    EXPECT_NEAR(s.value(Kind::Width), 90.0, 1e-3);
    ASSERT_TRUE(s.model.preview(75.0));
    EXPECT_NEAR(s.value(Kind::Width), 75.0, 1e-3) << "a preview starts again from the original, not from the last preview";
    EXPECT_EQ(s.document.undoStack().undoDescription(), before);
    s.model.cancel();
    EXPECT_FALSE(s.model.editing());
    EXPECT_NEAR(s.value(Kind::Width), 60.0, 1e-3);
    EXPECT_EQ(s.document.network().nodeCount(), nodes);
    EXPECT_EQ(s.document.undoStack().undoDescription(), before);
}

TEST(Dimensions, aSessionWithoutAPreviewLeavesNothingBehind) {
    Shapes s;
    s.select(s.first);
    const std::size_t nodes = s.document.network().nodeCount();
    ASSERT_TRUE(s.model.begin(Kind::Width));
    EXPECT_FALSE(s.model.commit());
    EXPECT_EQ(s.document.network().nodeCount(), nodes);
}

TEST(Dimensions, ZeroNegativeAndNothingSelectedAreRefused) {
    Shapes s;
    EXPECT_FALSE(s.model.apply(Kind::Width, 50.0)) << "nothing selected";
    s.select(s.first);
    EXPECT_FALSE(s.model.apply(Kind::Width, 0.0));
    EXPECT_FALSE(s.model.apply(Kind::Width, -5.0));
    EXPECT_FALSE(s.model.message().empty());
    EXPECT_NEAR(s.value(Kind::Width), 60.0, 1e-3);
    EXPECT_FALSE(s.model.apply(Kind::CurveLength, 10.0));
}

TEST(Dimensions, aWidthIsInDocumentUnitsThroughAScaledAndRotatedLayer) {
    Shapes s;
    s.document.setObjectTransform(s.layer, {{"sx", 2.0f}, {"sy", 2.0f}, {"tx", 10.0f}, {"rot", 90.0f}});
    s.select(s.first);
    EXPECT_NEAR(s.value(Kind::Width), 120.0, 1e-2) << "the layer doubles it";
    ASSERT_TRUE(s.model.apply(Kind::Width, 50.0));
    EXPECT_NEAR(s.value(Kind::Width), 50.0, 1e-2);
    EXPECT_NEAR(s.value(Kind::Height), 120.0, 1e-2) << "the other axis is untouched";
}

TEST(Dimensions, twoShapesScaleTogetherAboutTheBoxCorner) {
    Shapes s;
    s.select(s.first);
    s.addToSelection(s.second);
    const BLBox before = s.box();
    ASSERT_TRUE(s.model.apply(Kind::Width, (before.x1 - before.x0) * 2.0));
    const BLBox after = s.box();
    EXPECT_NEAR(after.x0, before.x0, 1e-3);
    EXPECT_NEAR(after.x1 - after.x0, (before.x1 - before.x0) * 2.0, 1e-2);
    EXPECT_EQ(s.document.undoStack().undoDescription(), "Change width");
    s.document.undoStack().undo();
    EXPECT_NEAR(s.box().x1, before.x1, 1e-3) << "both shapes came back in one step";
}

TEST(Dimensions, aGroupAndItsMemberSelectedTogetherAreScaledOnce) {
    Shapes s;
    const curvy::NodeRef group = s.document.addGroup(s.layer, "Group");
    const curvy::NodeRef inside = s.document.addCircle(group, 500.0f, 500.0f, 20.0f, newui::Color(0.0f, 1.0f, 0.0f, 1.0f));
    s.selection.set(group);
    s.selection.add(s.shapeOf(inside));
    const BLBox before = s.box();
    ASSERT_TRUE(s.model.apply(Kind::Width, (before.x1 - before.x0) * 3.0));
    EXPECT_NEAR(s.box().x1 - s.box().x0, (before.x1 - before.x0) * 3.0, 1e-2);
}

TEST(Dimensions, kindsHaveStableNamesForScripts) {
    for (Kind kind : {Kind::Width, Kind::Height, Kind::Radius, Kind::CurveLength}) {
        EXPECT_EQ(curvy::DimensionModel::kindFromName(curvy::DimensionModel::name(kind)), kind);
    }
    EXPECT_FALSE(curvy::DimensionModel::kindFromName("depth").has_value());
}

namespace {

    class DimensionOverlayTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Dimensions", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            window().document().addCircle(layer_, 200.0f, 200.0f, 60.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            window().pressed(pen_, at(200.0, 200.0), newui::mbmLeftButton, 0);
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        curvy::DimensionOverlay& overlay() { return static_cast<curvy::SelectTool&>(*window().tools().find("select")).dimensions(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        newui::Point centreOf(const newui::Rect& r) { return newui::Point(r.left() + r.width() * 0.5f, r.top() + r.height() * 0.5f); }
        double width() {
            curvy::DimensionModel model(window().document(), selection());
            for (const curvy::DimensionItem& i : model.compute()) {
                if (i.kind == Kind::Width) {
                    return i.value;
                }
            }
            return -1.0;
        }
        const curvy::DimensionOverlay::Label* label(Kind kind) {
            for (const curvy::DimensionOverlay::Label& l : overlay().labels()) {
                if (l.item.kind == kind) {
                    return &l;
                }
            }
            return nullptr;
        }

        newui::PenState pen_;
        curvy::NodeRef layer_;
    };

}

TEST_F(DimensionOverlayTest, theSelectionGetsWidthHeightAndRadiusLabelsInTheDocumentUnit) {
    ASSERT_EQ(overlay().labels().size(), 3u);
    ASSERT_NE(label(Kind::Width), nullptr);
    EXPECT_EQ(label(Kind::Width)->text, "120 px");
    EXPECT_EQ(label(Kind::Radius)->text, "r 60 px");
    window().document().setUnits({curvy::LengthUnit::Millimeters, curvy::AngleUnit::Degrees});
    EXPECT_EQ(label(Kind::Width)->text, "42.33 mm");
}

TEST_F(DimensionOverlayTest, labelsHideWithTheSettingAndWhenNothingIsSelected) {
    window().settings().showDimensions = false;
    EXPECT_TRUE(overlay().labels().empty());
    window().settings().showDimensions = true;
    EXPECT_EQ(overlay().labels().size(), 3u);
    selection().clear();
    EXPECT_TRUE(overlay().labels().empty());
}

TEST_F(DimensionOverlayTest, aSmallSelectionOnScreenShowsNoLabels) {
    canvas().setZoom(0.1f);
    EXPECT_TRUE(overlay().labels().empty()) << "a 12 px box cannot hold a label";
}

TEST_F(DimensionOverlayTest, draggingALabelScrubsTheValueAndIsOneUndoStep) {
    const std::string before = window().document().undoStack().undoDescription();
    const newui::Point start = centreOf(label(Kind::Width)->rect);
    ASSERT_TRUE(window().pressed(pen_, start, newui::mbmLeftButton, 0));
    EXPECT_TRUE(overlay().scrubbing());
    canvas().onMouseMove(canvas(), newui::Point(start.x + 10.0f, start.y), newui::mbmLeftButton, 0);
    EXPECT_NEAR(width(), 130.0, 1e-2) << "a pixel is a unit in a pixel document";
    EXPECT_EQ(window().document().undoStack().undoDescription(), before) << "no step while dragging";
    canvas().onMouseUp(canvas(), newui::Point(start.x + 10.0f, start.y), newui::mbmLeftButton, 0);
    EXPECT_FALSE(overlay().scrubbing());
    EXPECT_EQ(window().document().undoStack().undoDescription(), "Change width");
    window().document().undoStack().undo();
    EXPECT_NEAR(width(), 120.0, 1e-2);
}

TEST_F(DimensionOverlayTest, aPressOnALabelWithoutMovingChangesNothing) {
    const std::string before = window().document().undoStack().undoDescription();
    const newui::Point start = centreOf(label(Kind::Width)->rect);
    window().pressed(pen_, start, newui::mbmLeftButton, 0);
    canvas().onMouseUp(canvas(), start, newui::mbmLeftButton, 0);
    EXPECT_EQ(window().document().undoStack().undoDescription(), before);
    EXPECT_NEAR(width(), 120.0, 1e-3);
    EXPECT_EQ(selection().items().size(), 1u) << "the press belonged to the label, not the canvas";
}

TEST_F(DimensionOverlayTest, escapeWhileScrubbingPutsTheSizeBack) {
    const newui::Point start = centreOf(label(Kind::Width)->rect);
    window().pressed(pen_, start, newui::mbmLeftButton, 0);
    canvas().onMouseMove(canvas(), newui::Point(start.x + 25.0f, start.y), newui::mbmLeftButton, 0);
    ASSERT_NEAR(width(), 145.0, 1e-2);
    overlay().cancel();
    EXPECT_FALSE(overlay().scrubbing());
    EXPECT_NEAR(width(), 120.0, 1e-2);
}

TEST_F(DimensionOverlayTest, aDoubleClickOpensAFieldAndTypedTextWithAUnitIsApplied) {
    const newui::Point on = centreOf(label(Kind::Width)->rect);
    ASSERT_TRUE(window().doubleClicked(pen_, on, newui::mbmLeftButton, 0));
    EXPECT_TRUE(overlay().editing());
    EXPECT_EQ(overlay().editText(), "120 px");
    ASSERT_TRUE(overlay().commitEdit("1 in"));
    EXPECT_FALSE(overlay().editing());
    EXPECT_NEAR(width(), 72.0, 1e-2) << "1 in is 72 points";
    EXPECT_EQ(window().document().undoStack().undoDescription(), "Change width");
}

TEST_F(DimensionOverlayTest, textThatIsNotALengthLeavesTheSizeAlone) {
    window().doubleClicked(pen_, centreOf(label(Kind::Width)->rect), newui::mbmLeftButton, 0);
    EXPECT_FALSE(overlay().commitEdit("wide"));
    EXPECT_FALSE(overlay().editing());
    EXPECT_NEAR(width(), 120.0, 1e-3);
}

TEST_F(DimensionOverlayTest, cancellingTheEditorKeepsTheSize) {
    window().doubleClicked(pen_, centreOf(label(Kind::Width)->rect), newui::mbmLeftButton, 0);
    overlay().cancelEdit();
    EXPECT_FALSE(overlay().editing());
    EXPECT_NEAR(width(), 120.0, 1e-3);
}

TEST(Dimensions, aSelectedNodeCutOffFromItsShapeHasNoPivotAndPaintingItDoesNotThrow) {
    Shapes s;
    jitnodes::Network& network = s.document.networkOf(s.first.scope);
    network.disconnect(s.first.id, 0);   // the fill no longer hangs on its circle: what unwiring it on the graph does
    s.selection.selectNodes({s.first}, curvy::SelectionModel::Combine::Replace);
    EXPECT_NO_THROW(s.model.pivots());
}
