#include "curvy/sketchdocument.h"
#include "curvy/snapper.h"

#include <gtest/gtest.h>

using curvy::SnapResult;
using curvy::Snapper;
using curvy::TraceItem;
using Kind = curvy::SnapResult::Kind;

namespace {

    class SnapperTest : public ::testing::Test {
    protected:
        void SetUp() override {
            document_.newBlank("Snap", 612.0f, 792.0f);
            document_.addTraceItem(TraceItem::rect(100.0, 100.0, 200.0, 180.0));
        }

        SnapResult snap(double x, double y, double zoom = 1.0, const std::vector<std::size_t>& exclude = {}) {
            return Snapper(document_).snap(BLPoint(x, y), zoom, exclude);
        }

        curvy::SketchDocument document_;
    };

}

TEST_F(SnapperTest, aCornerWithinReachPullsThePointOntoIt) {
    const SnapResult result = snap(103.0, 98.0);
    EXPECT_EQ(Kind::Corner, result.kind);
    EXPECT_EQ(100.0, result.point.x);
    EXPECT_EQ(100.0, result.point.y);
}

TEST_F(SnapperTest, theCrossingOfTwoShapesEdgesIsAPointTarget) {
    document_.addTraceItem(TraceItem::line(150.0, 50.0, 150.0, 250.0));
    const SnapResult result = snap(152.0, 103.0);
    EXPECT_EQ(Kind::Intersection, result.kind) << "the line crosses the rectangle's top edge at (150, 100)";
    EXPECT_EQ(150.0, result.point.x);
    EXPECT_EQ(100.0, result.point.y);
    EXPECT_STREQ("intersection", SnapResult::kindName(result.kind));
    EXPECT_EQ(Kind::Edge, snap(152.0, 120.0).kind) << "away from the crossing it is just an edge";
}

TEST_F(SnapperTest, midpointsAndTheCentreSnapToo) {
    SnapResult result = snap(150.0, 104.0);
    EXPECT_EQ(Kind::Midpoint, result.kind) << "a midpoint beats the edge it is on";
    EXPECT_EQ(150.0, result.point.x);
    EXPECT_EQ(100.0, result.point.y);

    result = snap(153.0, 138.0);
    EXPECT_EQ(Kind::Center, result.kind);
    EXPECT_EQ(150.0, result.point.x);
    EXPECT_EQ(140.0, result.point.y);
}

TEST_F(SnapperTest, anEdgeTakesThePointOntoItsNearestPlace) {
    const SnapResult result = snap(130.0, 104.0);
    EXPECT_EQ(Kind::Edge, result.kind);
    EXPECT_EQ(130.0, result.point.x);
    EXPECT_EQ(100.0, result.point.y);
}

TEST_F(SnapperTest, aPointOutOfReachIsLeftAlone) {
    const SnapResult result = snap(120.0, 140.0);
    EXPECT_EQ(Kind::None, result.kind);
    EXPECT_FALSE(result.snapped());
    EXPECT_EQ(120.0, result.point.x);
    EXPECT_EQ(140.0, result.point.y);
}

TEST_F(SnapperTest, reachIsInScreenPixelsSoItShrinksWhenZoomedIn) {
    EXPECT_EQ(Kind::Corner, snap(106.0, 100.0, 1.0).kind);
    EXPECT_EQ(Kind::Edge, snap(106.0, 100.0, 2.0).kind) << "6 points is 12 pixels away from the corner now, but the edge is under it";
    EXPECT_EQ(Kind::None, snap(106.0, 103.0, 8.0).kind) << "3 points is 24 pixels";
    const SnapResult zoomedOut = snap(130.0, 100.0, 0.1);
    EXPECT_EQ(Kind::Midpoint, zoomedOut.kind) << "zoomed out the reach is 80 points, and the nearest point target (the midpoint, 20 away) wins";
    EXPECT_EQ(150.0, zoomedOut.point.x);
    EXPECT_EQ(Kind::None, snap(103.0, 98.0, 0.0).kind) << "a zoom that is not positive snaps nothing";
}

TEST_F(SnapperTest, excludedShapesDoNotAttract) {
    EXPECT_EQ(Kind::None, snap(103.0, 98.0, 1.0, {0}).kind);
}

TEST_F(SnapperTest, aHiddenTraceLayerDoesNotSnap) {
    document_.setTraceVisible(false);
    EXPECT_EQ(Kind::None, snap(103.0, 98.0).kind);
}

TEST_F(SnapperTest, guidesSetTheirOwnAxisAndHiddenGuidesDoNot) {
    document_.addGuide(true, 300.0);
    document_.addGuide(false, 400.0);
    SnapResult result = snap(304.0, 50.0);
    EXPECT_EQ(Kind::Guide, result.kind);
    EXPECT_EQ(300.0, result.point.x);
    EXPECT_EQ(50.0, result.point.y) << "a vertical guide pulls x only";

    result = snap(304.0, 397.0);
    EXPECT_EQ(300.0, result.point.x);
    EXPECT_EQ(400.0, result.point.y);

    document_.setGuidesVisible(false);
    EXPECT_EQ(Kind::None, snap(304.0, 50.0).kind);
}

TEST_F(SnapperTest, aGuideWinsOverAnEdgeOnItsOwnAxis) {
    document_.addGuide(true, 130.0);
    const SnapResult result = snap(133.0, 104.0);
    EXPECT_EQ(Kind::Guide, result.kind);
    EXPECT_EQ(130.0, result.point.x);
    EXPECT_EQ(100.0, result.point.y) << "and the edge still sets y";
}

TEST_F(SnapperTest, aDraggedShapeIsPulledByTheSmallestPullOfAnyOfItsPoints) {
    document_.addTraceItem(TraceItem::rect(300.0, 300.0, 400.0, 360.0));
    std::vector<BLPoint> points;
    for (const curvy::TraceSnapPoint& point : document_.trace().items()[0].snapPoints()) {
        points.push_back(point.point);
    }
    Snapper::MoveResult result = Snapper(document_).snapMove(points, BLPoint(197.0, 203.0), 1.0, {0});
    EXPECT_TRUE(result.hit.snapped());
    EXPECT_NEAR(200.0, result.delta.x, 1e-9);
    EXPECT_NEAR(200.0, result.delta.y, 1e-9);

    result = Snapper(document_).snapMove(points, BLPoint(500.0, 500.0), 1.0, {0});
    EXPECT_FALSE(result.hit.snapped());
    EXPECT_EQ(500.0, result.delta.x);
    EXPECT_EQ(500.0, result.delta.y);
}

TEST(TraceItemGeometry, aRectHasNineSnapPointsAndFourEdges) {
    const TraceItem item = TraceItem::rect(0.0, 0.0, 10.0, 20.0);
    const std::vector<curvy::TraceSnapPoint> points = item.snapPoints();
    ASSERT_EQ(9u, points.size());
    int corners = 0, midpoints = 0, centres = 0;
    for (const curvy::TraceSnapPoint& point : points) {
        corners += point.kind == curvy::TraceSnapPoint::Kind::Corner ? 1 : 0;
        midpoints += point.kind == curvy::TraceSnapPoint::Kind::Midpoint ? 1 : 0;
        centres += point.kind == curvy::TraceSnapPoint::Kind::Center ? 1 : 0;
    }
    EXPECT_EQ(4, corners);
    EXPECT_EQ(4, midpoints);
    EXPECT_EQ(1, centres);
    EXPECT_EQ(4u, item.segments().size());
}
