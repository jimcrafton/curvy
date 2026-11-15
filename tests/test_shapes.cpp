#include "curvy/canvasview.h"
#include "curvy/selectionmodel.h"
#include "curvy/shapepaths.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

namespace {

    using jitnodes::AttribClass;
    using jitnodes::AttribType;
    using jitnodes::Geometry;
    using jitnodes::PrimKind;

    // A closed square primitive of half-size `half` around (cx, cy), added as part of the shape `key`.
    void addSquare(Geometry& g, const std::string& key, float cx, float cy, float half) {
        const std::uint32_t start = std::uint32_t(g.pointCount());
        g.resizePoints(start + 4);
        const float xs[4] = {cx - half, cx + half, cx + half, cx - half};
        const float ys[4] = {cy - half, cy - half, cy + half, cy + half};
        for (int i = 0; i < 4; ++i) {
            g.x()[start + i] = xs[i];
            g.y()[start + i] = ys[i];
        }
        g.addShapePrimitive({start, 4, true, PrimKind::polyline}, key);
    }

    void paint(Geometry& g, std::size_t prim, float fillA, float strokeA, float strokeWidth, float evenOdd = 0.0f) {
        g.attrib(AttribClass::primitive, "fill.a", AttribType::float32).floats()[prim] = fillA;
        g.attrib(AttribClass::primitive, "stroke.a", AttribType::float32).floats()[prim] = strokeA;
        g.attrib(AttribClass::primitive, "stroke.width", AttribType::float32).floats()[prim] = strokeWidth;
        g.attrib(AttribClass::primitive, "fill.evenOdd", AttribType::float32).floats()[prim] = evenOdd;
    }

}

TEST(ShapeHitTest, aCompoundShapeKeepsItsHoleAndOnlyTheRingIsHit) {
    Geometry g;
    addSquare(g, "ring", 0, 0, 10);
    addSquare(g, "ring", 0, 0, 4);   // the counter
    paint(g, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    paint(g, 1, 1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(curvy::shapeAt(g, 7.0f, 0.0f, 1.0f).has_value());
    EXPECT_FALSE(curvy::shapeAt(g, 0.0f, 0.0f, 1.0f).has_value());   // in the hole
    EXPECT_FALSE(curvy::shapeAt(g, 20.0f, 0.0f, 1.0f).has_value());
    const auto hit = curvy::shapeAt(g, 7.0f, 0.0f, 1.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->key, "ring");
    EXPECT_EQ(hit->first, 0u);
}

TEST(ShapeHitTest, theShapeDrawnLastWins) {
    Geometry g;
    addSquare(g, "below", 0, 0, 10);
    addSquare(g, "above", 5, 5, 10);
    paint(g, 0, 1.0f, 0.0f, 0.0f);
    paint(g, 1, 1.0f, 0.0f, 0.0f);
    EXPECT_EQ(curvy::shapeAt(g, 6.0f, 6.0f, 1.0f)->key, "above");
    EXPECT_EQ(curvy::shapeAt(g, -8.0f, -8.0f, 1.0f)->key, "below");
}

TEST(ShapeHitTest, aStrokeOnlyShapeIsHitNearItsLineAndNotInside) {
    Geometry g;
    addSquare(g, "outline", 0, 0, 10);
    paint(g, 0, 0.0f, 1.0f, 0.5f);
    EXPECT_FALSE(curvy::shapeAt(g, 0.0f, 0.0f, 1.0f).has_value());
    EXPECT_TRUE(curvy::shapeAt(g, 10.0f, 0.0f, 1.0f).has_value());
    EXPECT_TRUE(curvy::shapeAt(g, 10.8f, 0.0f, 1.0f).has_value());   // thin stroke, but the tolerance widens it
    EXPECT_FALSE(curvy::shapeAt(g, 13.0f, 0.0f, 1.0f).has_value());
}

TEST(ShapeHitTest, aShapeWithNoLookIsNotHit) {
    Geometry g;
    addSquare(g, "bare", 0, 0, 10);
    EXPECT_FALSE(curvy::shapeAt(g, 0.0f, 0.0f, 1.0f).has_value());
}

TEST(ShapeKeyTest, aKeyParsesToTheUidsOfItsPath) {
    using curvy::GraphDocument;
    EXPECT_EQ(GraphDocument::uidsOfShapeKey("1"), (std::vector<std::uint32_t>{1}));
    EXPECT_EQ(GraphDocument::uidsOfShapeKey("a/z/10"), (std::vector<std::uint32_t>{10, 35, 36}));
    EXPECT_EQ(GraphDocument::uidsOfShapeKey("a/b#g5@2"), (std::vector<std::uint32_t>{10, 11}));
    EXPECT_EQ(GraphDocument::uidsOfShapeKey("k@3@1"), (std::vector<std::uint32_t>{20}));
    EXPECT_TRUE(GraphDocument::uidsOfShapeKey("").empty());
    EXPECT_TRUE(GraphDocument::uidsOfShapeKey("a//b").empty());
    EXPECT_TRUE(GraphDocument::uidsOfShapeKey("A").empty());
}

namespace {

    struct OneCircle {
        curvy::SketchDocument document;
        curvy::NodeRef layer, fill;
        OneCircle() {
            layer = document.addLayer();
            fill = document.addCircle(layer, 0.0f, 0.0f, 10.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
        }
        std::string key() {
            const jitnodes::GeometryHandle art = document.artwork();
            return art == nullptr || art->primitiveCount() == 0 ? std::string() : std::string(art->shapes().front().key);
        }
    };

}

TEST(ShapeKeyTest, theCookedArtworkNamesTheNodeThatMadeEachShape) {
    OneCircle s;
    const std::string key = s.key();
    ASSERT_FALSE(key.empty());
    const curvy::NodeRef maker = s.document.nodeForShapeKey(key);
    ASSERT_TRUE(maker.valid());
    EXPECT_EQ(s.document.node(maker).typeName(), "circle");
    EXPECT_EQ(s.document.shapeKeyOf(maker), key);
    EXPECT_NE(maker, s.fill);

    curvy::SelectionModel sel(&s.document);
    EXPECT_EQ(sel.find(curvy::SelectionModel::Role::Fill, maker), s.fill);
}

TEST(ShapeKeyTest, aKeyStillFindsItsNodeAfterUndoRedoAndEdits) {
    OneCircle s;
    const std::string key = s.key();
    s.document.addCircle(s.layer, 50.0f, 0.0f, 5.0f, newui::Color(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(s.key(), key);   // the first shape keeps its key when another is added
    s.document.undoStack().undo();
    s.document.undoStack().redo();
    EXPECT_EQ(s.key(), key);
    EXPECT_EQ(s.document.node(s.document.nodeForShapeKey(key)).typeName(), "circle");

    const curvy::NodeRef maker = s.document.nodeForShapeKey(key);
    s.document.setGraphParam(maker, "radius", 30.0f);
    EXPECT_EQ(s.key(), key);   // a parameter never changes a key
}

TEST(ShapeKeyTest, aKeyForANodeThatIsGoneFindsNothing) {
    OneCircle s;
    EXPECT_FALSE(s.document.nodeForShapeKey("zzzz").valid());
    EXPECT_FALSE(s.document.nodeForShapeKey("").valid());
}

TEST(ShapeKeyTest, theSelectionFollowsAKeyAndTheEnclosingLayer) {
    OneCircle s;
    const std::string key = s.key();
    curvy::SelectionModel sel(&s.document);
    EXPECT_FALSE(sel.isShapeKeySelected(key));
    EXPECT_TRUE(sel.selectShapeKey(key));
    EXPECT_EQ(s.document.node(sel.primary()).typeName(), "circle");
    EXPECT_TRUE(sel.isShapeKeySelected(key));
    EXPECT_FALSE(sel.selectShapeKey(key));   // already selected

    sel.set(s.layer);
    EXPECT_TRUE(sel.isShapeKeySelected(key));   // selecting the layer selects what it holds

    EXPECT_TRUE(sel.selectShapeKey("zzzz"));   // a click on empty canvas
    EXPECT_TRUE(sel.empty());
    EXPECT_FALSE(sel.selectShapeKey("zzzz", true));
}

TEST(ShapeKeyTest, theCanvasHandsBackTheKeyOfTheShapeUnderThePointer) {
    OneCircle s;
    curvy::CanvasView canvas;
    canvas.setDocument(&s.document);
    canvas.setZoom(2.0f);
    canvas.setPan(100.0f, 100.0f);
    const auto inside = canvas.shapeAt(newui::Point(100.0f, 100.0f));   // the circle's centre
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->key, s.key());
    EXPECT_FALSE(canvas.shapeAt(newui::Point(400.0f, 400.0f)).has_value());
}
