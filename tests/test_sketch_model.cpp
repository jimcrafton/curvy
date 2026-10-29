#include "curvy/sketchcolor.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchobject.h"
#include "curvy/cursorset.h"
#include "curvy/shapetools.h"
#include "curvy/pathops.h"
#include "curvy/imagetrace.h"
#include "curvy/imagedata.h"
#include "curvy/pdfexport.h"
#include "curvy/gzip.h"
#include "curvy/sketchsvg.h"
#include "curvy/gzip.h"
#include "curvy/sketchexport.h"
#include "curvy/numberscrubber.h"
#include "curvy/expressionfield.h"
#include "curvy/radialmenu.h"
#include "curvy/holdscrub.h"
#include "curvy/workspacemodel.h"
#include "curvy/theme.h"
#include "curvy/dockcontext.h"
#include "curvy/settings.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include "curvy/sketchbin.h"
#include "curvy/pathnode.h"
#include "curvy/graphtree.h"
#include <jitnodes/geo/topology.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

// Headless coverage of sketch's model layer: SketchColor, SketchObject (geometry, bounds, hit
// testing) and SketchDocument (ids, ordering, hit testing). Painting needs a live window.

using namespace curvy;

namespace {
    // Carries an object that is already in `document` by `matrix`, the way a drag does: through the document, which writes its nodes.
    void placeInDocument(SketchDocument& document, SketchObject& object, const BLMatrix2D& matrix) {
        GeometryState state;
        state.id = object.id();
        state.path = object.geometry();
        state.angle = object.frameAngle();
        state.textMatrix = object.textMatrix();
        state.paintMatrix = object.paintToPage();
        state.paintWarp = object.paintWarp();
        document.placeObjects({state}, matrix);
    }

    void setWords(SketchDocument& document, std::size_t id, const std::string& words) {
        TextState state = SketchDocument::textStateOf(*document.findById(id));
        state.text = words;
        document.setObjectText(id, state);
    }

    void transformPrototype(SketchObject& object, const BLMatrix2D& matrix) {
        SketchObject::Prototype::transform(object, matrix);
    }
}

TEST(SketchColor, RgbRoundTripsThroughHex) {
    SketchColor color = SketchColor::fromHex("#336699");
    EXPECT_EQ(color.space(), ColorSpace::RGB);
    EXPECT_EQ(color.toHex(), "#336699");
    BLRgba32 rgba = color.toRgba32();
    EXPECT_EQ(rgba.r(), 0x33u);
    EXPECT_EQ(rgba.g(), 0x66u);
    EXPECT_EQ(rgba.b(), 0x99u);
    EXPECT_EQ(rgba.a(), 0xFFu);
}

TEST(SketchColor, HexAcceptsOptionalHashAndAlpha) {
    SketchColor color = SketchColor::fromHex("ff000080");
    EXPECT_EQ(color.toHex(), "#ff0000");
    EXPECT_NEAR(color.alpha(), 128.0f / 255.0f, 1e-4f);
}

TEST(SketchColor, MalformedHexThrows) {
    EXPECT_THROW(SketchColor::fromHex("#12345"), std::runtime_error);
    EXPECT_THROW(SketchColor::fromHex("#gg0000"), std::runtime_error);
    EXPECT_THROW(SketchColor::fromHex(""), std::runtime_error);
}

TEST(SketchColor, CmykKeepsItsSpaceAndConvertsNaively) {
    SketchColor color = SketchColor::cmyk(0.0f, 1.0f, 1.0f, 0.0f);
    EXPECT_EQ(color.space(), ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(color.component(1), 1.0f);
    EXPECT_EQ(color.toHex(), "#ff0000");
    EXPECT_EQ(SketchColor::cmyk(0, 0, 0, 1).toHex(), "#000000");
    EXPECT_EQ(SketchColor::cmyk(0, 0, 0, 0).toHex(), "#ffffff");
}

TEST(SketchColor, GrayAndHsb) {
    EXPECT_EQ(SketchColor::gray(1.0f).toHex(), "#ffffff");
    EXPECT_EQ(SketchColor::gray(0.0f).toHex(), "#000000");
    EXPECT_EQ(SketchColor::hsb(0.0f, 1.0f, 1.0f).toHex(), "#ff0000");
    EXPECT_EQ(SketchColor::hsb(1.0f / 3.0f, 1.0f, 1.0f).toHex(), "#00ff00");
    // Hue wraps: a full turn is the same color.
    EXPECT_EQ(SketchColor::hsb(1.0f, 1.0f, 1.0f).toHex(), "#ff0000");
}

TEST(SketchColor, ComponentsAndAlphaClamp) {
    SketchColor color = SketchColor::rgb(2.0f, -1.0f, 0.5f, 7.0f);
    EXPECT_FLOAT_EQ(color.component(0), 1.0f);
    EXPECT_FLOAT_EQ(color.component(1), 0.0f);
    EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
    color.setAlpha(-3.0f);
    EXPECT_FLOAT_EQ(color.alpha(), 0.0f);
}

TEST(SketchColor, EqualityComparesSpaceAndComponents) {
    EXPECT_EQ(SketchColor::rgb(1, 0, 0), SketchColor::rgb(1, 0, 0));
    EXPECT_NE(SketchColor::rgb(1, 0, 0), SketchColor::rgb(0, 1, 0));
    // Same display color, different authoring space: not equal.
    EXPECT_NE(SketchColor::rgb(1, 0, 0), SketchColor::cmyk(0, 1, 1, 0));
}

TEST(SketchPaint, NoneAndSolid) {
    EXPECT_TRUE(SketchPaint::none().isNone());
    SketchPaint paint = SketchPaint::solid(SketchColor::gray(0.5f));
    EXPECT_FALSE(paint.isNone());
    EXPECT_EQ(paint.kind(), SketchPaint::Kind::Solid);
    EXPECT_EQ(paint.color(), SketchColor::gray(0.5f));
}

TEST(SketchObject, FactoriesSetTypeAndClosedness) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    EXPECT_EQ(rect->type(), SketchObjectType::Rect);
    EXPECT_TRUE(rect->isClosed());

    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 0, 10, 10));
    EXPECT_EQ(line->type(), SketchObjectType::Line);
    EXPECT_FALSE(line->isClosed());

    std::unique_ptr<SketchObject> poly(SketchObject::makePolyline({BLPoint(0, 0), BLPoint(5, 5), BLPoint(10, 0)}));
    EXPECT_FALSE(poly->isClosed());
    std::unique_ptr<SketchObject> gon(SketchObject::makePolygon({BLPoint(0, 0), BLPoint(5, 5), BLPoint(10, 0)}));
    EXPECT_TRUE(gon->isClosed());
}

TEST(SketchObject, BoundsIncludeHalfTheStroke) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(10, 20, 100, 50));
    rect->setStrokeWidth(4.0);
    BLBox box = rect->bounds();
    EXPECT_NEAR(box.x0, 8.0, 1e-6);
    EXPECT_NEAR(box.y0, 18.0, 1e-6);
    EXPECT_NEAR(box.x1, 112.0, 1e-6);
    EXPECT_NEAR(box.y1, 72.0, 1e-6);
}

TEST(SketchObject, EllipseBounds) {
    std::unique_ptr<SketchObject> ellipse(SketchObject::makeEllipse(50, 50, 30, 20));
    ellipse->setStroke(SketchPaint::none());
    BLBox box = ellipse->bounds();
    EXPECT_NEAR(box.x0, 20.0, 1e-3);
    EXPECT_NEAR(box.x1, 80.0, 1e-3);
    EXPECT_NEAR(box.y0, 30.0, 1e-3);
    EXPECT_NEAR(box.y1, 70.0, 1e-3);
}

TEST(SketchObject, HitTestFillInteriorAndMiss) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    EXPECT_TRUE(rect->hitTest(BLPoint(50, 50), 2.0));
    EXPECT_FALSE(rect->hitTest(BLPoint(150, 50), 2.0));
}

TEST(SketchObject, HitTestUnfilledShapeOnlyHitsTheStroke) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setFill(SketchPaint::none());
    EXPECT_FALSE(rect->hitTest(BLPoint(50, 50), 2.0));
    EXPECT_TRUE(rect->hitTest(BLPoint(50, 1), 2.0));
}

TEST(SketchObject, HitTestLineUsesTolerance) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 0, 100, 0));
    EXPECT_TRUE(line->hitTest(BLPoint(50, 2), 3.0));
    EXPECT_FALSE(line->hitTest(BLPoint(50, 20), 3.0));
}

TEST(SketchObject, SettersRejectOutOfRange) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 1, 1));
    rect->setOpacity(0.5);
    EXPECT_DOUBLE_EQ(rect->opacity(), 0.5);
    EXPECT_THROW(rect->setOpacity(3.0), std::runtime_error);
    EXPECT_THROW(rect->setOpacity(-1.0), std::runtime_error);
    EXPECT_DOUBLE_EQ(rect->opacity(), 0.5);
    EXPECT_THROW(rect->setStrokeWidth(-1.0), std::runtime_error);
}

TEST(SketchDocument, AddAssignsIncreasingIdsAndKeepsOrder) {
    SketchDocument doc;
    EXPECT_TRUE(doc.empty());
    SketchObject& a = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    SketchObject& b = doc.add(SketchObject::makeRect(5, 5, 10, 10));
    EXPECT_NE(a.id(), 0u);
    EXPECT_LT(a.id(), b.id());
    EXPECT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(&doc.object(0), &a);
    EXPECT_EQ(&doc.object(1), &b);
    EXPECT_THROW(doc.object(2), std::runtime_error);
}

TEST(SketchDocument, FindAndRemoveById) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    EXPECT_NE(doc.findById(id), nullptr);
    EXPECT_EQ(doc.findById(id + 100), nullptr);
    EXPECT_TRUE(doc.remove(id));
    EXPECT_FALSE(doc.remove(id));
    EXPECT_TRUE(doc.empty());
}

TEST(SketchDocument, IdsAreNotReusedAfterRemove) {
    SketchDocument doc;
    std::size_t first = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    doc.remove(first);
    EXPECT_GT(doc.add(SketchObject::makeRect(0, 0, 1, 1)).id(), first);
}

TEST(SketchDocument, HitTestPicksTopmostVisibleUnlocked) {
    SketchDocument doc;
    SketchObject& bottom = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchObject& top = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), &top);
    top.setLocked(true);
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), &bottom);
    bottom.setVisible(false);
    EXPECT_EQ(doc.hitTest(BLPoint(50, 50), 2.0), nullptr);
}

TEST(SketchDocument, ChangesMarkItModified) {
    SketchDocument doc;
    EXPECT_FALSE(doc.isModified());
    doc.add(SketchObject::makeRect(0, 0, 1, 1));
    EXPECT_TRUE(doc.isModified());
}

TEST(SketchDocument, ClearObjectsKeepsPageSettings) {
    SketchDocument doc;
    doc.setPageSize(300, 200);
    doc.setPpi(96);
    doc.add(SketchObject::makeRect(0, 0, 10, 10));
    doc.add(SketchObject::makeEllipse(5, 5, 3, 3));
    EXPECT_FALSE(doc.empty());
    doc.clearObjects();
    EXPECT_TRUE(doc.empty());
    EXPECT_FLOAT_EQ(doc.pageWidth(), 300.0f);
    EXPECT_FLOAT_EQ(doc.pageHeight(), 200.0f);
    EXPECT_FLOAT_EQ(doc.ppi(), 96.0f);
}

TEST(SketchDocument, BadPageSizeThrows) {
    SketchDocument doc;
    EXPECT_THROW(doc.setPageSize(0, 10), std::runtime_error);
    EXPECT_THROW(doc.setPpi(-1), std::runtime_error);
}

TEST(SketchDocument, SvgIoIsNotYetAvailable) {
    SketchDocument doc;
    EXPECT_FALSE(doc.load("nonexistent.svg"));
}

TEST(SketchObject, TransformBakesIntoGeometry) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    transformPrototype(*rect, BLMatrix2D::make_translation(5, 7));
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 5.0, 1e-9);
    EXPECT_NEAR(box.y1, 17.0, 1e-9);
    EXPECT_DOUBLE_EQ(rect->strokeWidth(), 1.0);
}

TEST(SketchObject, CloneCopiesAppearanceButNotId) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->setId(9);
    rect->setStrokeWidth(3.0);
    rect->setName("r");
    std::unique_ptr<SketchObject> copy(rect->clone());
    EXPECT_EQ(copy->id(), 0u);
    EXPECT_EQ(copy->name(), "r");
    EXPECT_DOUBLE_EQ(copy->strokeWidth(), 3.0);
    EXPECT_EQ(copy->type(), SketchObjectType::Rect);
}

TEST(SketchSelection, SetFiltersUnknownAndDuplicateIds) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a, a, 999});
    ASSERT_EQ(doc.selection().size(), 1u);
    EXPECT_TRUE(doc.isSelected(a));
}

TEST(SketchSelection, RemovedObjectLeavesSelection) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    doc.remove(a);
    EXPECT_TRUE(doc.selection().empty());
}

TEST(SketchSelection, BoundsAreTheUnionOfGeometry) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 30, 10, 10)).id();
    BLBox box;
    EXPECT_FALSE(doc.selectionBounds(box));
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.selectionBounds(box));
    EXPECT_NEAR(box.x0, 0.0, 1e-9);
    EXPECT_NEAR(box.x1, 30.0, 1e-9);
    EXPECT_NEAR(box.y1, 40.0, 1e-9);
}

TEST(SketchSelection, FiresChangedOnlyWhenItChanges) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    int fired = 0;
    doc.onSelectionChanged += [&fired](SketchDocument&) { ++fired; return newui::SyncReturn::Handled; };
    doc.setSelection({a});
    doc.setSelection({a});
    EXPECT_EQ(fired, 1);
}

TEST(SketchUndo, AddUndoableUndoRedo) {
    SketchDocument doc;
    std::size_t id = doc.addUndoable(SketchObject::makeRect(0, 0, 10, 10), "Draw").id();
    EXPECT_EQ(doc.objectCount(), 1u);
    EXPECT_TRUE(doc.isSelected(id));
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 0u);
    EXPECT_TRUE(doc.selection().empty());
    doc.undoStack().redo();
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.object(0).id(), id);
    EXPECT_TRUE(doc.isSelected(id));
}

TEST(SketchUndo, DeleteSelectionRestoresStackOrder) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    std::size_t b = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    std::size_t c = doc.add(SketchObject::makeRect(0, 0, 1, 1)).id();
    doc.setSelection({a, c});
    doc.deleteSelection();
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.object(0).id(), b);
    doc.undoStack().undo();
    ASSERT_EQ(doc.objectCount(), 3u);
    EXPECT_EQ(doc.object(0).id(), a);
    EXPECT_EQ(doc.object(1).id(), b);
    EXPECT_EQ(doc.object(2).id(), c);
    EXPECT_TRUE(doc.isSelected(a));
    EXPECT_TRUE(doc.isSelected(c));
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 1u);
}

TEST(SketchUndo, DuplicateOffsetsAndSelectsCopies) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    doc.duplicateSelection(5, 5);
    ASSERT_EQ(doc.objectCount(), 2u);
    ASSERT_EQ(doc.selection().size(), 1u);
    EXPECT_NE(doc.selection()[0], a);
    EXPECT_NEAR(doc.object(1).geometryBounds().x0, 5.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 1u);
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 2u);
}

TEST(SketchUndo, TransformObjectsUndoRestoresOutline) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.transformObjects({a}, BLMatrix2D::make_translation(100, 0), "Move");
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 100.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 0.0, 1e-9);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 100.0, 1e-9);
}

TEST(SketchUndo, CommitGeometryRecordsALiveDrag) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotSelection();
    doc.placeObjects(before, BLMatrix2D::make_scaling(2.0));  // the live drag edit
    doc.commitPlacement(before, "Resize");
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 20.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 10.0, 1e-9);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 20.0, 1e-9);
}

TEST(SketchObject, RotateAboutCenterStaysPut) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 50));
    BLMatrix2D m = BLMatrix2D::make_translation(-50, -25);  // 90 degrees about the center (50, 25)
    m.post_rotate(1.5707963267948966);
    m.post_translate(50, 25);
    transformPrototype(*rect, m);
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 25.0, 1e-6);
    EXPECT_NEAR(box.x1, 75.0, 1e-6);
    EXPECT_NEAR(box.y0, -25.0, 1e-6);
    EXPECT_NEAR(box.y1, 75.0, 1e-6);
}

TEST(SketchFrame, UprightWhenNotRotated) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 20, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(frame.angle, 0.0, 1e-12);
    EXPECT_NEAR(frame.local.x0, 10.0, 1e-9);
    EXPECT_NEAR(frame.local.y1, 70.0, 1e-9);
}

TEST(SketchFrame, FollowsTheObjectsRotation) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    const double angle = 0.5;
    SketchObject* object = doc.findById(a);
    BLMatrix2D m = BLMatrix2D::make_translation(-50, -25);
    m.post_rotate(angle);
    m.post_translate(50, 25);
    transformPrototype(*object, m);
    object->setFrameAngle(angle);

    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(frame.angle, angle, 1e-12);
    // In the rotated frame the box is again 100 x 50, and its center is the rotation center.
    EXPECT_NEAR(frame.local.x1 - frame.local.x0, 100.0, 1e-6);
    EXPECT_NEAR(frame.local.y1 - frame.local.y0, 50.0, 1e-6);
    EXPECT_NEAR(frame.center().x, 50.0, 1e-6);
    EXPECT_NEAR(frame.center().y, 25.0, 1e-6);
    // toLocal inverts toWorld.
    BLPoint world = frame.toWorld(12.0, 34.0);
    BLPoint back = frame.toLocal(world);
    EXPECT_NEAR(back.x, 12.0, 1e-9);
    EXPECT_NEAR(back.y, 34.0, 1e-9);
}

TEST(SketchFrame, MixedAnglesFallBackToUpright) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);
}

TEST(SketchFrame, UndoRestoresTheFrameAngle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotSelection();
    doc.placeObjects(before, BLMatrix2D::make_rotation(0.3), 1.0, 0.3);
    doc.commitPlacement(before, "Rotate");
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.0);
    doc.undoStack().redo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.3);
}

TEST(SketchFrame, CloneKeepsTheFrameAngle) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    rect->setFrameAngle(1.1);
    std::unique_ptr<SketchObject> copy(rect->clone());
    EXPECT_DOUBLE_EQ(copy->frameAngle(), 1.1);
}

TEST(SketchObject, WarpMapsEveryVertex) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 10, 10));
    SketchObject::Prototype::warp(*rect, [](const BLPoint& p) { return BLPoint(p.x * 2.0 + 1.0, p.y + 5.0); });
    BLBox box = rect->geometryBounds();
    EXPECT_NEAR(box.x0, 1.0, 1e-9);
    EXPECT_NEAR(box.x1, 21.0, 1e-9);
    EXPECT_NEAR(box.y0, 5.0, 1e-9);
    EXPECT_NEAR(box.y1, 15.0, 1e-9);
    EXPECT_TRUE(rect->isClosed());
}

TEST(SketchObject, WarpKeepsCurvesAndClosure) {
    std::unique_ptr<SketchObject> ellipse(SketchObject::makeEllipse(0, 0, 10, 10));
    std::size_t before = ellipse->geometry().size();
    SketchObject::Prototype::warp(*ellipse, [](const BLPoint& p) { return BLPoint(p.x + 100.0, p.y); });
    EXPECT_EQ(ellipse->geometry().size(), before);
    EXPECT_NEAR(ellipse->geometryBounds().x0, 90.0, 1e-6);
}

TEST(SketchOrigin, DefaultsToTheCenterAndRidesWithTheFrame) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    BLPoint origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 50.0, 1e-9);
    EXPECT_NEAR(origin.y, 25.0, 1e-9);

    doc.setSelectionOrigin(frame, BLPoint(0, 0));  // move the pivot to the top-left corner
    origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 0.0, 1e-9);
    EXPECT_NEAR(origin.y, 0.0, 1e-9);

    // Scale the object 2x about the corner: the origin fraction keeps pointing at the same corner.
    doc.placeObjects(doc.snapshotSelection(), BLMatrix2D::make_scaling(2.0));
    doc.selectionFrame(frame);
    origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 0.0, 1e-9);
    doc.placeObjects(doc.snapshotSelection(), BLMatrix2D::make_translation(30, 0));
    doc.selectionFrame(frame);
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 30.0, 1e-9);
}

TEST(SketchOrigin, ResetsWhenTheSelectionChanges) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    std::size_t b = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    doc.selectionFrame(frame);
    doc.setSelectionOrigin(frame, BLPoint(0, 0));
    doc.setSelection({b});
    doc.selectionFrame(frame);
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 50.0, 1e-9);
}

TEST(SketchFrame, RotatedGroupKeepsItsOwnAngleWhenMembersDiffer) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);

    doc.setGroupAngle(0.4);
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.4);

    // A shared member angle wins over the stored group angle (they agree after a group rotation).
    doc.findById(a)->setFrameAngle(0.7);
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.7);

    // Changing the selection drops the group angle.
    doc.findById(a)->setFrameAngle(0.0);
    doc.setSelection({b, a});
    EXPECT_FALSE(doc.groupAngle().has_value());
}

TEST(SketchFrame, UndoRestoresTheGroupAngle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.findById(b)->setFrameAngle(0.7);
    doc.setSelection({a, b});
    auto before = doc.snapshotSelection();
    FrameState groupBefore = doc.frameState();
    doc.findById(a)->setFrameAngle(0.3);
    doc.findById(b)->setFrameAngle(1.0);
    doc.setGroupAngle(0.3);
    doc.commitGeometry(before, "Rotate", groupBefore);
    doc.undoStack().undo();
    EXPECT_FALSE(doc.groupAngle().has_value());
    EXPECT_DOUBLE_EQ(doc.findById(b)->frameAngle(), 0.7);
    doc.undoStack().redo();
    ASSERT_TRUE(doc.groupAngle().has_value());
    EXPECT_DOUBLE_EQ(*doc.groupAngle(), 0.3);
}

TEST(SketchQuad, MapAndUnmapRoundTrip) {
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(100, 10);
    quad.p[2] = BLPoint(80, 90);
    quad.p[3] = BLPoint(-10, 60);
    EXPECT_NEAR(quad.map(0, 0).x, 0.0, 1e-9);
    EXPECT_NEAR(quad.map(1, 0).x, 100.0, 1e-9);
    EXPECT_NEAR(quad.map(1, 1).y, 90.0, 1e-9);
    EXPECT_NEAR(quad.map(0, 1).x, -10.0, 1e-9);
    for (double u : {0.0, 0.25, 0.5, 1.0}) {
        for (double v : {0.0, 0.3, 0.5, 1.0}) {
            double ru = -1, rv = -1;
            ASSERT_TRUE(quad.unmap(quad.map(u, v), ru, rv));
            EXPECT_NEAR(ru, u, 1e-9);
            EXPECT_NEAR(rv, v, 1e-9);
        }
    }
}

TEST(SketchQuad, RectangleFrameAsQuadMatchesItsBox) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 20, 100, 50)).id();
    doc.setSelection({a});
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    QuadFrame quad = frame.asQuad();
    EXPECT_NEAR(quad.p[0].x, 10.0, 1e-9);
    EXPECT_NEAR(quad.p[2].y, 70.0, 1e-9);
    BLPoint mid = frame.pointAt(0.5, 0.5);
    EXPECT_NEAR(mid.x, 60.0, 1e-9);
    EXPECT_NEAR(mid.y, 45.0, 1e-9);
}

TEST(SketchQuad, FrameQuadSurvivesTransformsAndUndo) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(12, 1);
    quad.p[2] = BLPoint(10, 10);
    quad.p[3] = BLPoint(-1, 9);
    doc.setQuadFrame(quad);
    doc.transformObjects({a}, BLMatrix2D::make_translation(100, 0), "Move");
    ASSERT_TRUE(doc.quadFrame().has_value());
    EXPECT_NEAR(doc.quadFrame()->p[1].x, 112.0, 1e-9);
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    ASSERT_TRUE(frame.quad.has_value());
    doc.undoStack().undo();
    EXPECT_NEAR(doc.quadFrame()->p[1].x, 12.0, 1e-9);
    doc.setSelection({});
    EXPECT_FALSE(doc.quadFrame().has_value());
}

TEST(SketchQuad, OriginFollowsTheQuad) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(20, 0);
    quad.p[2] = BLPoint(20, 20);
    quad.p[3] = BLPoint(0, 20);
    doc.setQuadFrame(quad);
    SelectionFrame frame;
    doc.selectionFrame(frame);
    doc.setSelectionOrigin(frame, BLPoint(20, 20));
    doc.transformObjects({a}, BLMatrix2D::make_translation(5, 5), "Move");
    doc.selectionFrame(frame);
    BLPoint origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 25.0, 1e-9);
    EXPECT_NEAR(origin.y, 25.0, 1e-9);
}

TEST(SketchQuad, ResetBoundingBoxStraightensTheFrameAndUndoes) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    EXPECT_FALSE(doc.resetBoundingBox());  // nothing selected
    doc.setSelection({a, b});
    EXPECT_FALSE(doc.resetBoundingBox());  // already upright
    doc.findById(a)->setFrameAngle(0.5);
    doc.setGroupAngle(0.2);
    QuadFrame quad;
    quad.p[0] = BLPoint(0, 0);
    quad.p[1] = BLPoint(30, 2);
    quad.p[2] = BLPoint(30, 10);
    quad.p[3] = BLPoint(0, 10);
    doc.setQuadFrame(quad);

    EXPECT_TRUE(doc.resetBoundingBox());
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.0);
    EXPECT_FALSE(doc.groupAngle().has_value());
    EXPECT_FALSE(doc.quadFrame().has_value());
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_DOUBLE_EQ(frame.angle, 0.0);
    EXPECT_FALSE(frame.quad.has_value());

    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->frameAngle(), 0.5);
    ASSERT_TRUE(doc.groupAngle().has_value());
    EXPECT_TRUE(doc.quadFrame().has_value());
}

TEST(SketchStyle, LiveEditsThenOneUndoStep) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    SketchPaint original = doc.findById(a)->fill();
    auto before = doc.snapshotStyle();
    doc.setSelectionFill(SketchPaint::solid(SketchColor::fromHex("#112233")));
    doc.setSelectionFill(SketchPaint::solid(SketchColor::fromHex("#445566")));  // a drag: many live changes
    EXPECT_TRUE(doc.commitStyle(before, "Change Fill"));
    EXPECT_EQ(doc.findById(a)->fill().color().toHex(), "#445566");
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(a)->fill(), original);
    EXPECT_FALSE(doc.undoStack().canUndo());
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(a)->fill().color().toHex(), "#445566");
}

TEST(SketchStyle, CommitWithNoDifferenceRecordsNothing) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    EXPECT_FALSE(doc.commitStyle(before, "Nothing"));
    EXPECT_FALSE(doc.undoStack().canUndo());
}

TEST(SketchStyle, StrokeWidthAndOpacityUndo) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    doc.setSelectionStrokeWidth(6.0);
    doc.setSelectionOpacity(0.25);
    doc.commitStyle(before, "Style");
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->strokeWidth(), 1.0);
    EXPECT_DOUBLE_EQ(doc.findById(a)->opacity(), 1.0);
}

TEST(SketchStyle, DefaultsApplyToNewShapesAndFireChanged) {
    SketchDocument doc;
    int changes = 0;
    doc.onChanged += [&changes](SketchDocument&) { ++changes; return newui::SyncReturn::Handled; };
    doc.setDefaultFill(SketchPaint::none());
    doc.setDefaultStrokeWidth(5.0);
    EXPECT_TRUE(doc.defaultFill().isNone());
    EXPECT_DOUBLE_EQ(doc.defaultStrokeWidth(), 5.0);
    EXPECT_EQ(changes, 2);
}

TEST(SketchStyle, PaintEquality) {
    EXPECT_EQ(SketchPaint::none(), SketchPaint::none());
    EXPECT_EQ(SketchPaint::solid(SketchColor::gray(0.5f)), SketchPaint::solid(SketchColor::gray(0.5f)));
    EXPECT_NE(SketchPaint::none(), SketchPaint::solid(SketchColor::gray(0.5f)));
}

namespace {
    // Renders one object onto a transparent w x h image and returns its pixels (0xAARRGGBB, premultiplied).
    std::vector<std::uint32_t> render(const SketchObject& object, int w, int h) {
        BLImage image(w, h, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        std::vector<std::uint32_t> pixels(std::size_t(w) * std::size_t(h));
        for (int y = 0; y < h; ++y) {
            const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
            for (int x = 0; x < w; ++x) {
                pixels[std::size_t(y) * std::size_t(w) + std::size_t(x)] = row[x];
            }
        }
        return pixels;
    }

    SketchGradient redToBlue() {
        SketchGradient gradient;
        gradient.stops.push_back({0.0, SketchColor::rgb(1, 0, 0)});
        gradient.stops.push_back({1.0, SketchColor::rgb(0, 0, 1)});
        return gradient;
    }
}

TEST(SketchGradient, ColorAtInterpolatesAndClamps) {
    SketchGradient gradient = redToBlue();
    EXPECT_EQ(gradient.colorAt(-1.0).toHex(), "#ff0000");
    EXPECT_EQ(gradient.colorAt(2.0).toHex(), "#0000ff");
    SketchColor mid = gradient.colorAt(0.5);
    EXPECT_NEAR(double(mid.toRgba32().r()), 128.0, 1.5);
    EXPECT_NEAR(double(mid.toRgba32().b()), 128.0, 1.5);
}

TEST(SketchGradient, ReverseAndSort) {
    SketchGradient gradient = redToBlue();
    gradient.stops.push_back({0.25, SketchColor::rgb(0, 1, 0)});
    gradient.sortStops();
    EXPECT_DOUBLE_EQ(gradient.stops[1].offset, 0.25);
    gradient.reverse();
    EXPECT_EQ(gradient.stops.front().color.toHex(), "#0000ff");
    EXPECT_DOUBLE_EQ(gradient.stops[1].offset, 0.75);
}

TEST(SketchGradient, PaintKindsAndEquality) {
    SketchPaint linear = SketchPaint::linear(redToBlue());
    SketchPaint radial = SketchPaint::radial(redToBlue());
    EXPECT_TRUE(linear.isGradient());
    EXPECT_FALSE(linear.isNone());
    EXPECT_EQ(linear.color().toHex(), "#ff0000");  // first stop stands in for color-only callers
    EXPECT_NE(linear, radial);
    EXPECT_EQ(linear, SketchPaint::linear(redToBlue()));
    SketchGradient other = redToBlue();
    other.angleDegrees = 45.0;
    EXPECT_NE(linear, SketchPaint::linear(other));
}

TEST(SketchGradient, LinearGradientRendersLeftToRight) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::linear(redToBlue()));
    std::vector<std::uint32_t> px = render(*rect, 100, 40);
    std::uint32_t left = px[20 * 100 + 3], right = px[20 * 100 + 96];
    EXPECT_GT((left >> 16) & 0xFF, 200u);   // red end
    EXPECT_LT(left & 0xFF, 40u);
    EXPECT_GT(right & 0xFF, 200u);          // blue end
    EXPECT_LT((right >> 16) & 0xFF, 40u);
}

TEST(SketchGradient, AngleTurnsTheGradient) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 40, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.angleDegrees = 90.0;  // top to bottom
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 40, 100);
    std::uint32_t top = px[3 * 40 + 20], bottom = px[96 * 40 + 20];
    EXPECT_GT((top >> 16) & 0xFF, 200u);
    EXPECT_GT(bottom & 0xFF, 200u);
}

TEST(SketchGradient, RadialGradientRunsFromCenterOut) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::radial(redToBlue()));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t center = px[50 * 100 + 50], edge = px[50 * 100 + 98];
    EXPECT_GT((center >> 16) & 0xFF, 200u);
    EXPECT_GT(edge & 0xFF, 200u);
}

TEST(SketchGradient, GradientFollowsTheObjectWhenItMoves) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::linear(redToBlue()));
    transformPrototype(*rect, BLMatrix2D::make_translation(100, 0));
    std::vector<std::uint32_t> px = render(*rect, 200, 40);
    EXPECT_GT((px[20 * 200 + 103] >> 16) & 0xFF, 200u);  // still red at its (new) left edge
    EXPECT_GT(px[20 * 200 + 196] & 0xFF, 200u);
}

TEST(SketchGradient, GradientEditsUndoLikeAnyStyle) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    auto before = doc.snapshotStyle();
    doc.setSelectionFill(SketchPaint::linear(redToBlue()));
    EXPECT_TRUE(doc.commitStyle(before, "Gradient"));
    EXPECT_TRUE(doc.findById(a)->fill().isGradient());
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(a)->fill().isGradient());
}

namespace {
    SketchGradient redAndBluePoints() {
        SketchGradient gradient;
        gradient.points.push_back({0.1, 0.5, SketchColor::fromHex("#ff0000"), 0.3});
        gradient.points.push_back({0.9, 0.5, SketchColor::fromHex("#0000ff"), 0.3});
        return gradient;
    }
}

TEST(SketchGradient, PointsBlendTowardTheNearestColour) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::points(redAndBluePoints()));
    ASSERT_TRUE(rect->fill().isPoints());
    EXPECT_FALSE(rect->fill().isGradient());
    std::vector<std::uint32_t> px = render(*rect, 100, 40);
    const std::uint32_t red = px[20 * 100 + 10], middle = px[20 * 100 + 50], blue = px[20 * 100 + 90];
    EXPECT_GT((red >> 16) & 0xFF, 220u);
    EXPECT_LT(red & 0xFF, 40u);
    EXPECT_GT(blue & 0xFF, 220u);
    EXPECT_LT((blue >> 16) & 0xFF, 40u);
    EXPECT_GT((middle >> 16) & 0xFF, 80u) << "halfway both colours show";
    EXPECT_GT(middle & 0xFF, 80u);
}

TEST(SketchGradient, PointsFollowTheObjectAndHaveEquality) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    rect->setFill(SketchPaint::points(redAndBluePoints()));
    transformPrototype(*rect, BLMatrix2D::make_translation(100, 0));
    std::vector<std::uint32_t> px = render(*rect, 200, 40);
    EXPECT_GT((px[20 * 200 + 110] >> 16) & 0xFF, 220u);
    EXPECT_GT(px[20 * 200 + 190] & 0xFF, 220u);
    SketchGradient moved = redAndBluePoints();
    EXPECT_EQ(SketchPaint::points(redAndBluePoints()), SketchPaint::points(moved));
    moved.points[0].x = 0.2;
    EXPECT_NE(SketchPaint::points(redAndBluePoints()), SketchPaint::points(moved));
    EXPECT_NE(SketchPaint::points(redAndBluePoints()), SketchPaint::linear(redAndBluePoints()));
}

TEST(SketchGradient, ExplicitAxisIsKeptRelativeToTheBox) {
    SketchGradient gradient = redToBlue();
    BLBox box(10, 20, 110, 70);  // 100 x 50
    BLPoint start, end;
    gradient.axisPoints(box, false, start, end);  // derived: edge to edge through the center
    EXPECT_NEAR(start.x, 10.0, 1e-9);
    EXPECT_NEAR(end.x, 110.0, 1e-9);
    EXPECT_NEAR(start.y, 45.0, 1e-9);

    gradient.setAxis(box, BLPoint(30, 30), BLPoint(60, 70));
    EXPECT_TRUE(gradient.hasAxis);
    EXPECT_NEAR(gradient.angleDegrees, std::atan2(40.0, 30.0) * 180.0 / 3.14159265358979323846, 1e-9);
    // The same fractions over a moved and doubled box.
    gradient.axisPoints(BLBox(0, 0, 200, 100), false, start, end);
    EXPECT_NEAR(start.x, 40.0, 1e-9);
    EXPECT_NEAR(start.y, 20.0, 1e-9);
    EXPECT_NEAR(end.x, 100.0, 1e-9);
    EXPECT_NEAR(end.y, 100.0, 1e-9);

    // Turning the angle of an explicit axis keeps its midpoint and length.
    gradient.axisPoints(box, false, start, end);
    double length = std::hypot(end.x - start.x, end.y - start.y);
    gradient.setAngle(box, 0.0);
    BLPoint s2, e2;
    gradient.axisPoints(box, false, s2, e2);
    EXPECT_NEAR(s2.y, e2.y, 1e-9);
    EXPECT_NEAR(e2.x - s2.x, length, 1e-9);
    EXPECT_NEAR((s2.x + e2.x) * 0.5, (start.x + end.x) * 0.5, 1e-9);
}

TEST(SketchGradient, AxisChangesTheRenderedGradient) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 40));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.setAxis(rect->geometryBounds(), BLPoint(60, 20), BLPoint(100, 20));  // red until x = 60, then fades to blue
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 40);
    EXPECT_GT((px[20 * 100 + 30] >> 16) & 0xFF, 240u);  // padded red before the start
    EXPECT_LT(px[20 * 100 + 30] & 0xFF, 15u);
    EXPECT_GT(px[20 * 100 + 97] & 0xFF, 200u);
}

TEST(SketchGradient, RadialAxisSetsCenterAndRadius) {
    SketchGradient gradient = redToBlue();
    BLBox box(0, 0, 100, 100);
    gradient.setAxis(box, BLPoint(25, 25), BLPoint(25, 75));
    BLPoint center, edge;
    gradient.axisPoints(box, true, center, edge);
    EXPECT_NEAR(center.x, 25.0, 1e-9);
    EXPECT_NEAR(std::hypot(edge.x - center.x, edge.y - center.y), 50.0, 1e-9);
}

TEST(SketchGradient, AspectSquashesARadialGradientIntoAnEllipse) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    // Center in the middle, radius 40 along x, half of that across (y).
    gradient.setAxis(rect->geometryBounds(), BLPoint(50, 50), BLPoint(90, 50));
    gradient.axisAspect = 0.5;
    rect->setFill(SketchPaint::radial(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    // 10 px out along x (a quarter of the way) is mostly red; 25 px out along y is past the 20 px ellipse
    // edge, so fully blue - a circle of radius 40 would still be purple there.
    EXPECT_GT((px[50 * 100 + 60] >> 16) & 0xFF, px[50 * 100 + 60] & 0xFF);
    EXPECT_GT(px[75 * 100 + 50] & 0xFF, 240u);
    EXPECT_LT((px[75 * 100 + 50] >> 16) & 0xFF, 20u);

    BLMatrix2D m = gradient.axisMatrix(BLPoint(50, 50), BLPoint(90, 50), true);
    BLPoint p = m.map_point(90, 50);
    EXPECT_NEAR(p.x, 90.0, 1e-9);
    BLPoint q = m.map_point(50, 90);
    EXPECT_NEAR(q.y, 70.0, 1e-9);
}

TEST(SketchGradient, SkewLeansTheLinesOfEqualColor) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(0, 0, 100, 100));
    rect->setStroke(SketchPaint::none());
    SketchGradient gradient = redToBlue();
    gradient.setAxis(rect->geometryBounds(), BLPoint(0, 50), BLPoint(100, 50));
    gradient.axisSkew = 1.0;  // t = (x - (y - 50)) / 100
    rect->setFill(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    EXPECT_GT((px[95 * 100 + 50] >> 16) & 0xFF, 200u);  // lower middle: t = 0.05, red
    EXPECT_GT(px[5 * 100 + 50] & 0xFF, 200u);            // upper middle: t = 0.95, blue
}

TEST(SketchGradient, AlongStrokeFollowsThePathNotTheBox) {
    std::vector<BLPoint> corner = {BLPoint(10, 10), BLPoint(10, 90), BLPoint(90, 90)};
    std::unique_ptr<SketchObject> line(SketchObject::makePolyline(corner));
    line->setFill(SketchPaint::none());
    line->setStrokeWidth(10.0);
    SketchGradient gradient = redToBlue();
    gradient.alongStroke = true;
    line->setStroke(SketchPaint::linear(gradient));
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    std::uint32_t start = px[15 * 100 + 10], bend = px[88 * 100 + 10], end = px[90 * 100 + 88];
    EXPECT_GT((start >> 16) & 0xFF, 200u);   // the first end is red
    EXPECT_GT(end & 0xFF, 200u);              // the last end is blue
    // Halfway along the path (at the bend) both colors are present; a box gradient would still be pure red here.
    EXPECT_GT(bend & 0xFF, 80u);
    EXPECT_GT((bend >> 16) & 0xFF, 80u);
    // The same stroke with the plain box gradient is red along the whole left leg (a left-to-right gradient).
    gradient.alongStroke = false;
    line->setStroke(SketchPaint::linear(gradient));
    px = render(*line, 100, 100);
    EXPECT_LT(px[88 * 100 + 10] & 0xFF, 60u);
}

// ---- Arrange / align / transform / lock / hide ---------------------------------------------------

namespace {
    // Three squares a, b, c (back to front) at x = 0, 100, 200.
    struct Row {
        SketchDocument doc;
        std::size_t a = 0, b = 0, c = 0;
        Row() {
            a = doc.add(SketchObject::makeRect(0, 10, 40, 40)).id();
            b = doc.add(SketchObject::makeRect(100, 20, 20, 20)).id();
            c = doc.add(SketchObject::makeRect(200, 0, 60, 80)).id();
        }
        std::vector<std::size_t> order() const {
            std::vector<std::size_t> ids;
            for (std::size_t i = 0; i < doc.objectCount(); ++i) { ids.push_back(doc.object(i).id()); }
            return ids;
        }
    };
}

TEST(SketchArrange, BringToFrontAndSendToBackUndo) {
    Row row;
    row.doc.setSelection({row.a});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringToFront));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.b, row.c, row.a}));
    row.doc.undoStack().undo();
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.b, row.c}));
    row.doc.undoStack().redo();
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.b, row.c, row.a}));
    row.doc.setSelection({row.c});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::SendToBack));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.c, row.b, row.a}));
}

TEST(SketchArrange, ForwardAndBackwardStepOneNeighborAndDoNothingAtTheEnds) {
    Row row;
    row.doc.setSelection({row.b});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.c, row.b}));
    EXPECT_FALSE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));  // already in front
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::SendBackward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.a, row.b, row.c}));
    // Two together keep their relative order and each steps past one unselected neighbor.
    row.doc.setSelection({row.a, row.b});
    EXPECT_TRUE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(row.order(), (std::vector<std::size_t>{row.c, row.a, row.b}));
    EXPECT_FALSE(row.doc.arrangeSelection(SketchDocument::ArrangeOp::BringToFront));
}

TEST(SketchAlign, ObjectsLineUpOnTheirCombinedBoundsAndUndo) {
    Row row;
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Top));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().y0, 0.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().y0, 0.0, 1e-9);
    EXPECT_FALSE(row.doc.alignSelection(SketchDocument::AlignMode::Top));  // nothing left to move
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Right));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x1, 260.0, 1e-9);
    row.doc.undoStack().undo();
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x0, 0.0, 1e-9);
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.alignSelection(SketchDocument::AlignMode::Middle));
    EXPECT_NEAR((row.doc.findById(row.b)->geometryBounds().y0 + row.doc.findById(row.b)->geometryBounds().y1) * 0.5, 40.0, 1e-9);
}

TEST(SketchDistribute, EdgesAndCentersGoEvenlyBetweenTheOutermost) {
    Row row;
    row.doc.transformObjects({row.b}, BLMatrix2D::make_translation(-70, 0), "Move");   // b at x = 30
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.distributeSelection(SketchDocument::AlignMode::Left));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x0, 0.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 100.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.c)->geometryBounds().x0, 200.0, 1e-9);
    EXPECT_FALSE(row.doc.distributeSelection(SketchDocument::AlignMode::Left));   // already even
    row.doc.undoStack().undo();
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 30.0, 1e-9);
    // Centers: a at 20 and c at 230, so b's center goes to 125.
    EXPECT_TRUE(row.doc.distributeSelection(SketchDocument::AlignMode::Center));
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 115.0, 1e-9);
    // Vertically the tops are 10, 20 and 0: in order c, a, b - a is already halfway between the other two.
    EXPECT_FALSE(row.doc.distributeSelection(SketchDocument::AlignMode::Top));
    row.doc.transformObjects({row.a}, BLMatrix2D::make_translation(0, 4), "Move");   // tops 14, 20, 0: a is no longer halfway
    EXPECT_TRUE(row.doc.distributeSelection(SketchDocument::AlignMode::Top));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().y0, 10.0, 1e-9);
}

TEST(SketchDistribute, NeedsThreeObjects) {
    Row row;
    row.doc.setSelection({row.a, row.b});
    EXPECT_FALSE(row.doc.distributeSelection(SketchDocument::AlignMode::Left));
    EXPECT_FALSE(row.doc.distributeSpacing(true));
}

TEST(SketchDistribute, SpacingMakesTheGapsEqual) {
    Row row;   // widths 40, 20 and 60 across 0 .. 260: the gaps come out at 70
    row.doc.setSelection({row.a, row.b, row.c});
    EXPECT_TRUE(row.doc.distributeSpacing(true));
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().x0, 0.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 110.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.c)->geometryBounds().x1, 260.0, 1e-9);
    EXPECT_FALSE(row.doc.distributeSpacing(true));
    row.doc.undoStack().undo();
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 100.0, 1e-9);
    // A given gap: the first stays and the others follow it at that distance.
    EXPECT_TRUE(row.doc.distributeSpacing(true, 10.0));
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().x0, 50.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.c)->geometryBounds().x0, 80.0, 1e-9);
    // Down the page: heights 40, 20 and 80, tops 10, 20 and 0 -> in order c, a, b.
    EXPECT_TRUE(row.doc.distributeSpacing(false, 5.0));
    EXPECT_NEAR(row.doc.findById(row.c)->geometryBounds().y0, 0.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.a)->geometryBounds().y0, 85.0, 1e-9);
    EXPECT_NEAR(row.doc.findById(row.b)->geometryBounds().y0, 130.0, 1e-9);
}

TEST(SketchDistribute, AGroupMovesAsOnePiece) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 10, 40, 40)).id();
    std::size_t b = doc.add(SketchObject::makeRect(100, 20, 20, 20)).id();
    std::size_t c = doc.add(SketchObject::makeRect(200, 0, 60, 80)).id();
    std::size_t d = doc.add(SketchObject::makeRect(300, 0, 10, 10)).id();
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    doc.setSelection({a, b, c, d});
    EXPECT_TRUE(doc.distributeSpacing(true));   // pieces 0..120, 200..260 and 300..310: the gaps are 60
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 0.0, 1e-9);
    EXPECT_NEAR(doc.findById(b)->geometryBounds().x0, 100.0, 1e-9);   // the group's members keep their places
    EXPECT_NEAR(doc.findById(c)->geometryBounds().x0, 180.0, 1e-9);
    EXPECT_NEAR(doc.findById(d)->geometryBounds().x0, 300.0, 1e-9);
}

TEST(SketchTransformTools, MatricesPivotAboutTheOrigin) {
    BLPoint r = SketchDocument::rotationAbout(BLPoint(10, 0), 1.5707963267948966).map_point(BLPoint(20, 0));
    EXPECT_NEAR(r.x, 10.0, 1e-9);
    EXPECT_NEAR(r.y, 10.0, 1e-9);
    BLPoint s = SketchDocument::scalingAbout(BLPoint(10, 10), 2.0, 3.0).map_point(BLPoint(20, 20));
    EXPECT_NEAR(s.x, 30.0, 1e-9);
    EXPECT_NEAR(s.y, 40.0, 1e-9);
    // Across a horizontal line through (10, 10), a vertical one, and the diagonal through the origin.
    BLPoint h = SketchDocument::reflectionAcross(BLPoint(10, 10), 0.0).map_point(BLPoint(15, 20));
    EXPECT_NEAR(h.x, 15.0, 1e-9);
    EXPECT_NEAR(h.y, 0.0, 1e-9);
    BLPoint v = SketchDocument::reflectionAcross(BLPoint(10, 10), 1.5707963267948966).map_point(BLPoint(15, 20));
    EXPECT_NEAR(v.x, 5.0, 1e-9);
    EXPECT_NEAR(v.y, 20.0, 1e-9);
    BLPoint d = SketchDocument::reflectionAcross(BLPoint(0, 0), 0.7853981633974483).map_point(BLPoint(1, 0));
    EXPECT_NEAR(d.x, 0.0, 1e-9);
    EXPECT_NEAR(d.y, 1.0, 1e-9);
    BLPoint on = SketchDocument::reflectionAcross(BLPoint(10, 10), 0.3).map_point(BLPoint(10, 10));   // the origin stays
    EXPECT_NEAR(on.x, 10.0, 1e-9);
    EXPECT_NEAR(on.y, 10.0, 1e-9);
}

TEST(SketchTransformTools, ATransformedCopyLeavesTheOriginalAndIsOneUndoStep) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(10, 10, 20, 20)).id();
    doc.setSelection({id});
    doc.duplicateTransformed(SketchDocument::reflectionAcross(BLPoint(50, 0), 1.5707963267948966), -1.0, 3.14159265358979323846, "Reflect Copy");
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 10.0, 1e-9);   // the original has not moved
    ASSERT_EQ(doc.selection().size(), 1u);
    const SketchObject* copy = doc.findById(doc.selection()[0]);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->id(), id);
    EXPECT_NEAR(copy->geometryBounds().x0, 70.0, 1e-9);   // mirrored across x = 50
    EXPECT_NEAR(copy->geometryBounds().x1, 90.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 1u);
}

TEST(SketchAlign, OneObjectAlignsToThePage) {
    SketchDocument doc;
    doc.setPageSize(300, 200);
    std::size_t id = doc.add(SketchObject::makeRect(10, 10, 100, 50)).id();
    doc.setSelection({id});
    EXPECT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Center));
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 100.0, 1e-9);
    EXPECT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Bottom));
    EXPECT_NEAR(doc.findById(id)->geometryBounds().y1, 200.0, 1e-9);
}

TEST(SketchTransform, Rotate90AndFlipTurnTheFrameToo) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 100, 20)).id();
    doc.setSelection({id});
    EXPECT_TRUE(doc.rotateSelection90(true));
    BLBox box = doc.findById(id)->geometryBounds();
    EXPECT_NEAR(box.x1 - box.x0, 20.0, 1e-6);
    EXPECT_NEAR(box.y1 - box.y0, 100.0, 1e-6);
    EXPECT_NEAR((box.x0 + box.x1) * 0.5, 50.0, 1e-6);  // about its center
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 3.14159265358979323846 * 0.5, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1, 100.0, 1e-6);
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 0.0, 1e-12);

    doc.add(SketchObject::makeRect(300, 0, 10, 10));
    doc.setSelection({id, doc.object(1).id()});
    EXPECT_TRUE(doc.flipSelection(true));  // mirrored about x = 155
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 210.0, 1e-6);
    EXPECT_NEAR(doc.object(1).geometryBounds().x1, 10.0, 1e-6);
    EXPECT_TRUE(doc.flipSelection(false));
    EXPECT_EQ(doc.undoStack().undoDescription(), "Flip Vertical");
}

TEST(SketchLockHide, HiddenAndLockedObjectsLeaveTheSelectionAndComeBack) {
    Row row;
    row.doc.setSelection({row.a, row.b});
    EXPECT_TRUE(row.doc.hideSelection());
    EXPECT_FALSE(row.doc.findById(row.a)->isVisible());
    EXPECT_TRUE(row.doc.selection().empty());
    EXPECT_EQ(row.doc.hitTest(BLPoint(20, 30), 1.0), nullptr);
    row.doc.setSelection({row.c});
    EXPECT_TRUE(row.doc.lockSelection());
    EXPECT_TRUE(row.doc.findById(row.c)->isLocked());
    EXPECT_TRUE(row.doc.selection().empty());
    EXPECT_TRUE(row.doc.showAll());
    EXPECT_TRUE(row.doc.findById(row.a)->isVisible());
    EXPECT_FALSE(row.doc.showAll());  // nothing hidden any more
    EXPECT_TRUE(row.doc.unlockAll());
    EXPECT_FALSE(row.doc.findById(row.c)->isLocked());
    row.doc.undoStack().undo();  // back to locked
    EXPECT_TRUE(row.doc.findById(row.c)->isLocked());
}

TEST(SketchClipboard, CopySelectionAsSvgAndPasteIsOneUndoStep) {
    SketchDocument doc;
    EXPECT_TRUE(doc.selectionToSvg().empty());
    std::size_t a = doc.add(SketchObject::makeRect(10, 10, 40, 30)).id();
    std::size_t b = doc.add(SketchObject::makeEllipse(100, 100, 20, 20)).id();
    doc.add(SketchObject::makeLine(0, 0, 5, 5));
    doc.setSelection({a, b});
    std::string svg = doc.selectionToSvg();
    ASSERT_FALSE(svg.empty());

    EXPECT_FALSE(doc.pasteSvg("not svg at all", 0, 0));
    ASSERT_TRUE(doc.pasteSvg(svg, 12, 12));
    ASSERT_EQ(doc.objectCount(), 5u);
    EXPECT_EQ(doc.selection().size(), 2u);  // the pasted objects are selected
    BLBox pasted = doc.object(3).bounds();
    BLBox original = doc.object(0).bounds();
    EXPECT_NEAR(pasted.x0 - original.x0, 12.0, 1e-6);
    EXPECT_NEAR(pasted.y0 - original.y0, 12.0, 1e-6);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 3u);
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 5u);
}

TEST(SketchStrokeOptions, WidthProfileTapersRendersAndRoundTrips) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 50, 100, 50));
    line->setStrokeWidth(10.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions taper;
    taper.widths = {WidthPoint{0.0, 1.0, 1.0}, WidthPoint{1.0, 0.1, 0.1}};
    line->setStrokeOptions(taper);
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_GT(px[46 * 100 + 5] >> 24, 200u);   // wide near the start (half width ~4.5)
    EXPECT_EQ(px[46 * 100 + 95] >> 24, 0u);    // thin near the end
    EXPECT_GT(px[50 * 100 + 95] >> 24, 100u);  // still drawn on the centerline

    BLPoint p, n;
    ASSERT_TRUE(line->pathSample(0.25, p, n));
    EXPECT_NEAR(p.x, 25.0, 1e-6);
    EXPECT_NEAR(std::fabs(n.y), 1.0, 1e-6);
    double t = 0.0, d = 0.0;
    ASSERT_TRUE(line->nearestOnPath(BLPoint(60, 53), t, d));
    EXPECT_NEAR(t, 0.6, 1e-6);
    EXPECT_NEAR(d, 3.0, 1e-6);
    EXPECT_NEAR(widthScaleAt(taper.widths, 0.5, true), 0.55, 1e-6);  // the middle of the ease

    SketchDocument doc;
    doc.add(line->clone());
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    ASSERT_EQ(copy.object(0).strokeOptions().widths.size(), 2u);
    EXPECT_NEAR(copy.object(0).strokeOptions().widths[1].left, 0.1, 1e-9);
}

TEST(SketchBlend, ModesBlendWithWhatIsBelowUndoAndSave) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(10, 10, 50, 50)).id();
    doc.findById(id)->setFill(SketchPaint::solid(SketchColor::fromHex("#ffffff")));
    doc.findById(id)->setStroke(SketchPaint::none());
    doc.setSelection({id});

    auto pixelOverWhite = [&](const SketchObject& object) {
        BLImage image(80, 80, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.fill_all(BLRgba32(0xFFFFFFFFu));
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        return reinterpret_cast<const std::uint32_t*>(data.pixel_data)[30 * (data.stride / 4) + 30] & 0x00FFFFFFu;
    };
    EXPECT_EQ(pixelOverWhite(*doc.findById(id)), 0x00FFFFFFu);  // Normal: white over white

    auto before = doc.snapshotStyle();
    doc.setSelectionBlendMode(BlendMode::Difference);
    EXPECT_TRUE(doc.commitStyle(before, "Blend Mode"));
    EXPECT_EQ(pixelOverWhite(*doc.findById(id)), 0x00000000u);  // white difference white = black
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->blendMode(), BlendMode::Normal);
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(id)->blendMode(), BlendMode::Difference);
    std::unique_ptr<SketchObject> copyOf(doc.findById(id)->clone());
    EXPECT_EQ(copyOf->blendMode(), BlendMode::Difference);

    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    EXPECT_EQ(loaded.object(0).blendMode(), BlendMode::Difference);
    EXPECT_STREQ(blendModeName(BlendMode::ColorDodge), "Color Dodge");
    EXPECT_EQ(blendModeFromCss("soft-light"), BlendMode::SoftLight);
}

TEST(SketchGroups, GroupSelectUngroupArrangeAlignDuplicateAndSave) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t b = doc.add(SketchObject::makeRect(100, 0, 10, 10)).id();
    std::size_t c = doc.add(SketchObject::makeRect(200, 0, 10, 10)).id();
    doc.setSelection({a});
    EXPECT_FALSE(doc.groupSelection());  // needs two objects
    doc.setSelection({a, c});
    EXPECT_FALSE(doc.canUngroup());
    ASSERT_TRUE(doc.groupSelection());
    // The members sit together where the topmost one was: b, a, c.
    EXPECT_EQ(doc.object(0).id(), b);
    EXPECT_EQ(doc.object(1).id(), a);
    EXPECT_EQ(doc.object(2).id(), c);
    ASSERT_EQ(doc.findById(a)->groups().size(), 1u);
    EXPECT_EQ(doc.findById(a)->groups(), doc.findById(c)->groups());
    EXPECT_EQ(doc.withGroupMembers({a}), (std::vector<std::size_t>{a, c}));
    EXPECT_EQ(doc.withGroupMembers({b}), (std::vector<std::size_t>{b}));

    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    EXPECT_EQ(doc.object(0).id(), a);
    doc.undoStack().redo();
    EXPECT_FALSE(doc.findById(c)->groups().empty());

    // Sending the loose object forward steps past the whole group, not into it.
    doc.setSelection({b});
    ASSERT_TRUE(doc.arrangeSelection(SketchDocument::ArrangeOp::BringForward));
    EXPECT_EQ(doc.object(0).id(), a);
    EXPECT_EQ(doc.object(1).id(), c);
    EXPECT_EQ(doc.object(2).id(), b);

    // Aligning a group and an object treats the group as one piece.
    doc.setSelection({a, c, b});
    ASSERT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Left));
    EXPECT_NEAR(doc.findById(c)->geometryBounds().x0, 200.0, 1e-9);  // c stayed with a
    EXPECT_NEAR(doc.findById(b)->geometryBounds().x0, 0.0, 1e-9);

    // The file keeps the group as a <g>.
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 3u);
    EXPECT_FALSE(loaded.object(0).groups().empty());
    EXPECT_EQ(loaded.object(0).groups(), loaded.object(1).groups());
    EXPECT_TRUE(loaded.object(2).groups().empty());

    // A duplicate is a group of its own.
    doc.setSelection({a, c});
    doc.duplicateSelection(5, 5);
    ASSERT_EQ(doc.objectCount(), 5u);
    EXPECT_FALSE(doc.object(3).groups().empty());
    EXPECT_NE(doc.object(3).groups(), doc.object(0).groups());
    EXPECT_EQ(doc.object(3).groups(), doc.object(4).groups());

    doc.setSelection({a, c});
    ASSERT_TRUE(doc.ungroupSelection());
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    EXPECT_FALSE(doc.canUngroup());
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(a)->groups().empty());
}

TEST(SketchAppearance, StackPaintsInOrderHitTestsAndSaves) {
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
    rect->setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    rect->setStroke(SketchPaint::none());
    // A second fill on top (blue, half the rectangle's width would show nothing different - so use opacity): 50% blue.
    std::vector<AppearanceItem> items = rect->appearance();
    AppearanceItem blue = AppearanceItem::makeFill(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    blue.opacity = 0.5;
    items.push_back(blue);
    // And a thick stroke below everything: it only shows outside the fills.
    AppearanceItem wide = AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::fromHex("#00ff00")), 20.0);
    items.insert(items.begin(), wide);
    rect->setAppearance(items);
    ASSERT_EQ(rect->appearance().size(), 4u);  // wide stroke, red fill, no-paint stroke, blue fill

    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t center = px[40 * 100 + 40];
    EXPECT_GT((center >> 16) & 0xFFu, 100u);   // red shows through half-transparent blue
    EXPECT_GT(center & 0xFFu, 100u);           // and so does the blue
    std::uint32_t outside = px[40 * 100 + 15]; // inside the stroke's reach, left of the rect
    EXPECT_GT((outside >> 8) & 0xFFu, 200u);   // green
    EXPECT_EQ(outside & 0xFFu, 0u);

    // Hiding the item drops it from painting and the hit test; its bounds shrink too.
    EXPECT_TRUE(rect->hitTest(BLPoint(15, 40), 0.5));  // on the wide stroke
    items = rect->appearance();
    items[0].visible = false;
    rect->setAppearance(items);
    EXPECT_FALSE(rect->hitTest(BLPoint(15, 40), 0.5));
    EXPECT_LT(rect->bounds().x1 - rect->bounds().x0, 41.0);

    // The primary accessors still mean the first fill / first stroke.
    EXPECT_EQ(rect->fill().color().toHex(), "#ff0000");
    EXPECT_EQ(rect->stroke().color().toHex(), "#00ff00");  // the first stroke is the wide one
    EXPECT_DOUBLE_EQ(rect->strokeWidth(), 20.0);

    // Undo goes through StyleState; the file keeps the whole stack.
    SketchDocument doc;
    std::size_t id = doc.add(rect->clone()).id();
    doc.setSelection({id});
    auto before = doc.snapshotStyle();
    std::vector<AppearanceItem> edited = doc.findById(id)->appearance();
    edited.pop_back();
    doc.findById(id)->setAppearance(edited);
    EXPECT_TRUE(doc.commitStyle(before, "Delete Item"));
    EXPECT_EQ(doc.findById(id)->appearance().size(), 3u);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->appearance().size(), 4u);

    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 1u);
    const std::vector<AppearanceItem>& back = loaded.object(0).appearance();
    ASSERT_EQ(back.size(), 4u);
    EXPECT_EQ(back[0].kind, AppearanceKind::Stroke);
    EXPECT_FALSE(back[0].visible);
    EXPECT_DOUBLE_EQ(back[0].strokeWidth, 20.0);
    EXPECT_EQ(back[3].kind, AppearanceKind::Fill);
    EXPECT_DOUBLE_EQ(back[3].opacity, 0.5);
}

TEST(SketchAppearance, ActiveItemEditsAddDuplicateDeleteMoveAndClear) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 50, 50)).id();
    doc.setSelection({id});
    SketchObject* object = doc.findById(id);
    ASSERT_EQ(object->appearance().size(), 2u);  // fill, stroke

    ASSERT_TRUE(doc.addAppearanceItem(AppearanceKind::Stroke));
    ASSERT_EQ(object->appearance().size(), 3u);
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 2u);
    // The panels' accessors follow the active item: this edit lands on the new stroke, not the first.
    object->setStrokeWidth(9.0);
    EXPECT_DOUBLE_EQ(object->appearance()[2].strokeWidth, 9.0);
    EXPECT_DOUBLE_EQ(object->appearance()[1].strokeWidth, 1.0);

    doc.setActiveItem(AppearanceKind::Stroke, 1);  // choose the first stroke again - not an undo step
    EXPECT_DOUBLE_EQ(object->strokeWidth(), 1.0);

    doc.setActiveItem(AppearanceKind::Stroke, 2);
    ASSERT_TRUE(doc.duplicateActiveItem(AppearanceKind::Stroke));
    ASSERT_EQ(object->appearance().size(), 4u);
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 3u);
    EXPECT_DOUBLE_EQ(object->appearance()[3].strokeWidth, 9.0);
    object->setStrokeWidth(5.0);  // make the copy differ so moving it is visible

    ASSERT_TRUE(doc.moveActiveItem(AppearanceKind::Stroke, -1));
    EXPECT_EQ(object->activeIndex(AppearanceKind::Stroke), 2u);
    EXPECT_FALSE(doc.moveActiveItem(AppearanceKind::Stroke, 1) && doc.moveActiveItem(AppearanceKind::Stroke, 1) && doc.moveActiveItem(AppearanceKind::Stroke, 1));  // runs off the top

    // Live per-item opacity / blend / visibility, committed as one step.
    auto before = doc.snapshotStyle();
    doc.setSelectionItemOpacity(AppearanceKind::Stroke, 0.25);
    doc.setSelectionItemBlendMode(AppearanceKind::Stroke, BlendMode::Multiply);
    doc.setSelectionItemVisible(AppearanceKind::Stroke, false);
    EXPECT_TRUE(doc.commitStyle(before, "Item"));
    const AppearanceItem& edited = object->activeItem(AppearanceKind::Stroke);
    EXPECT_DOUBLE_EQ(edited.opacity, 0.25);
    EXPECT_EQ(edited.blendMode, BlendMode::Multiply);
    EXPECT_FALSE(edited.visible);
    doc.undoStack().undo();
    EXPECT_TRUE(object->activeItem(AppearanceKind::Stroke).visible);

    // Deleting walks back to a neighbour; the last item of a kind turns None instead of vanishing.
    std::size_t before_count = object->appearance().size();
    while (object->appearance().size() > 2) {
        ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Stroke));
    }
    EXPECT_EQ(object->appearance().size(), 2u);
    EXPECT_GT(before_count, 2u);
    ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Stroke));  // the last stroke
    EXPECT_EQ(object->appearance().size(), 2u);
    EXPECT_TRUE(object->stroke().isNone());
    doc.undoStack().undo();
    EXPECT_FALSE(object->stroke().isNone());

    object->setOpacity(0.5);
    ASSERT_TRUE(doc.clearAppearance());
    EXPECT_TRUE(object->fill().isNone());
    EXPECT_TRUE(object->stroke().isNone());
    EXPECT_DOUBLE_EQ(object->opacity(), 1.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(object->opacity(), 0.5);
    EXPECT_FALSE(object->fill().isNone());
}

TEST(SketchLayers, ObjectAndGroupOpacityBlendAsOnePiece) {
    // A black fill with a black stroke overlapping it, at 50% object opacity: where they overlap the result is still
    // 50% - not 75%, as it would be if each were drawn at 50% on its own.
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
    rect->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
    rect->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    rect->setStrokeWidth(20.0);
    rect->setOpacity(0.5);
    std::vector<std::uint32_t> px = render(*rect, 100, 100);
    std::uint32_t overlap = px[30 * 100 + 30] >> 24;   // inside the fill and within the stroke's inner half
    std::uint32_t strokeOnly = px[15 * 100 + 40] >> 24;  // outside the rect, stroke only
    EXPECT_NEAR(double(overlap), 128.0, 3.0);
    EXPECT_NEAR(double(strokeOnly), 128.0, 3.0);

    // Under a user transform with a fractional offset and a scale the layer still lines up with the direct drawing.
    BLImage image(100, 100, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    ctx.translate(3.25, 1.5);
    ctx.scale(0.5);
    SketchObject* half = rect->clone();
    half->setOpacity(0.5);  // goes through a layer under a fractional, scaled transform
    half->paint(ctx);
    ctx.end();
    delete half;

    // A group with opacity: its members blend as one, undo restores it with the objects.
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(10, 10, 40, 40)).id();
    std::size_t b = doc.add(SketchObject::makeRect(30, 30, 40, 40)).id();
    for (std::size_t id : {a, b}) {
        doc.findById(id)->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
        doc.findById(id)->setStroke(SketchPaint::none());
    }
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    EXPECT_EQ(doc.selectedGroup(), doc.findById(a)->groups().front());
    auto before = doc.snapshotStyle();
    doc.setSelectionGroupOpacity(0.5);
    EXPECT_TRUE(doc.commitStyle(before, "Group Opacity"));
    BLImage canvas(100, 100, BL_FORMAT_PRGB32);
    {
        BLContext c2(canvas);
        c2.clear_all();
        doc.paint(c2);
        c2.end();
    }
    BLImageData data;
    canvas.get_data(&data);
    const std::uint32_t* pixels = reinterpret_cast<const std::uint32_t*>(data.pixel_data);
    std::size_t stride = std::size_t(data.stride) / 4;
    EXPECT_NEAR(double(pixels[40 * stride + 40] >> 24), 128.0, 3.0);  // the squares overlap here - still 50%
    EXPECT_NEAR(double(pixels[20 * stride + 20] >> 24), 128.0, 3.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.groupProps(doc.findById(a)->groups().front()).opacity, 1.0);

    // Saved and read back.
    doc.undoStack().redo();
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 2u);
    EXPECT_DOUBLE_EQ(loaded.groupProps(loaded.object(0).groups().front()).opacity, 0.5);
}

TEST(SketchEffects, ShadowBlurRoundCornersAndSave) {
    auto make = []() {
        std::unique_ptr<SketchObject> rect(SketchObject::makeRect(20, 20, 40, 40));
        rect->setFill(SketchPaint::solid(SketchColor::fromHex("#000000")));
        rect->setStroke(SketchPaint::none());
        return rect;
    };
    auto alphaAt = [](const std::vector<std::uint32_t>& px, int x, int y) { return px[std::size_t(y) * 100 + std::size_t(x)] >> 24; };

    // Drop shadow: cast behind, offset down-right (4, 4), so it shows just outside the bottom-right corner only.
    std::unique_ptr<SketchObject> shadowed = make();
    std::vector<AppearanceItem> items = shadowed->appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::DropShadow));
    shadowed->setAppearance(items);
    EXPECT_GT(shadowed->effectPad(), 18.0);  // 3 x softness + offset
    std::vector<std::uint32_t> px = render(*shadowed, 100, 100);
    EXPECT_GT(alphaAt(px, 63, 63), 20u);
    EXPECT_EQ(alphaAt(px, 6, 6), 0u);          // far up-left of the rect: no shadow there
    EXPECT_GT(alphaAt(px, 40, 40), 200u);      // the rect itself is still on top
    items[2].visible = false;
    shadowed->setAppearance(items);
    px = render(*shadowed, 100, 100);
    EXPECT_EQ(alphaAt(px, 63, 63), 0u);

    // Gaussian blur softens the edges of what is below it.
    std::unique_ptr<SketchObject> blurred = make();
    items = blurred->appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::GaussianBlur));
    blurred->setAppearance(items);
    px = render(*blurred, 100, 100);
    EXPECT_GT(alphaAt(px, 62, 40), 0u);        // bled past the edge
    EXPECT_LT(alphaAt(px, 58, 40), 255u);      // and thinned inside it
    EXPECT_GT(alphaAt(px, 40, 40), 200u);

    // Round corners cut the corner of a rectangle but not the middle of its sides.
    std::unique_ptr<SketchObject> rounded = make();
    items = rounded->appearance();
    AppearanceItem round = AppearanceItem::makeEffect(EffectKind::RoundCorners);
    round.radius = 12.0;
    items.push_back(round);
    rounded->setAppearance(items);
    px = render(*rounded, 100, 100);
    EXPECT_EQ(alphaAt(px, 21, 21), 0u);
    EXPECT_GT(alphaAt(px, 40, 21), 200u);
    EXPECT_GT(alphaAt(px, 40, 40), 200u);

    // Effects survive undo snapshots and the file.
    SketchDocument doc;
    std::size_t id = doc.add(shadowed->clone()).id();
    doc.setSelection({id});
    ASSERT_TRUE(doc.addEffect(EffectKind::RoundCorners));
    EXPECT_EQ(doc.findById(id)->appearance().size(), 4u);
    ASSERT_TRUE(doc.changeActiveEffect([](AppearanceItem& e) { e.radius = 3.0; }, "Round Corners"));
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    const std::vector<AppearanceItem>& back = loaded.object(0).appearance();
    ASSERT_EQ(back.size(), 4u);
    EXPECT_EQ(back[2].kind, AppearanceKind::Effect);
    EXPECT_EQ(back[2].effect, EffectKind::DropShadow);
    EXPECT_FALSE(back[2].visible);
    EXPECT_DOUBLE_EQ(back[3].radius, 3.0);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(id)->appearance()[3].radius, 10.0);
    ASSERT_TRUE(doc.deleteActiveItem(AppearanceKind::Effect));  // effects can all be removed
    EXPECT_EQ(doc.findById(id)->appearance().size(), 3u);
}

TEST(SketchStrokeOptions, DashesRenderAndRoundTrip) {
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(0, 50, 100, 50));
    line->setStrokeWidth(10.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions dashed;
    dashed.dashes = {20.0, 20.0};
    line->setStrokeOptions(dashed);
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_GT(px[50 * 100 + 10] >> 24, 200u);  // inside the first dash
    EXPECT_EQ(px[50 * 100 + 30] >> 24, 0u);    // inside the first gap
    EXPECT_GT(px[50 * 100 + 50] >> 24, 200u);  // second dash

    SketchDocument doc;
    doc.add(line->clone());
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.object(0).strokeOptions().dashes, (std::vector<double>{20.0, 20.0}));
}

TEST(SketchStrokeOptions, CapsJoinsAndMiterRenderUndoAndSave) {
    // A round cap reaches half a stroke width past the end of the line; a butt cap does not.
    std::unique_ptr<SketchObject> line(SketchObject::makeLine(30, 50, 70, 50));
    line->setStrokeWidth(20.0);
    line->setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_EQ(px[50 * 100 + 25] >> 24, 0u);  // butt: nothing left of x = 30
    StrokeOptions round;
    round.cap = StrokeCap::Round;
    line->setStrokeOptions(round);
    px = render(*line, 100, 100);
    EXPECT_GT(px[50 * 100 + 25] >> 24, 200u);
    EXPECT_EQ(px[50 * 100 + 15] >> 24, 0u);
    EXPECT_TRUE(line->hitTest(BLPoint(22, 50), 0.5));  // hit testing sees the cap too
    line->setStrokeOptions(StrokeOptions());
    EXPECT_FALSE(line->hitTest(BLPoint(22, 50), 0.5));

    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makePolyline({BLPoint(0, 0), BLPoint(50, 50), BLPoint(100, 0)})).id();
    doc.setSelection({id});
    auto before = doc.snapshotStyle();
    doc.changeSelectionStrokeOptions([](StrokeOptions& o) { o.join = StrokeJoin::Round; o.miterLimit = 3.0; });
    EXPECT_TRUE(doc.commitStyle(before, "Round Join"));
    EXPECT_EQ(doc.findById(id)->strokeOptions().join, StrokeJoin::Round);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->strokeOptions().join, StrokeJoin::Miter);
    doc.undoStack().redo();

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.object(0).strokeOptions().join, StrokeJoin::Round);
    EXPECT_DOUBLE_EQ(copy.object(0).strokeOptions().miterLimit, 3.0);
    EXPECT_EQ(copy.object(0).strokeOptions().cap, StrokeCap::Butt);
}

// ---- SVG round trip -------------------------------------------------------------------------

namespace {
    SketchDocument roundTrip(const SketchDocument& source) {
        SketchDocument copy;
        EXPECT_TRUE(documentFromSvg(svgFromDocument(source), copy));
        return copy;
    }
}

TEST(SketchSvg, PageAndObjectsRoundTrip) {
    SketchDocument doc;
    doc.setPageSize(400, 300);
    doc.setPpi(144);
    SketchObject& rect = doc.add(SketchObject::makeRoundRect(10, 20, 100, 60, 8));
    rect.setName("Card & <co>");
    rect.setFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    rect.setStroke(SketchPaint::none());
    rect.setOpacity(0.5);
    rect.setLocked(true);
    SketchObject& line = doc.add(SketchObject::makeLine(0, 0, 50, 50));
    line.setVisible(false);
    line.setStrokeWidth(3.0);

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_FLOAT_EQ(copy.pageWidth(), 400.0f);
    EXPECT_FLOAT_EQ(copy.pageHeight(), 300.0f);
    EXPECT_FLOAT_EQ(copy.ppi(), 144.0f);
    ASSERT_EQ(copy.objectCount(), 2u);
    const SketchObject& a = copy.object(0);
    EXPECT_EQ(a.type(), SketchObjectType::RoundRect);
    EXPECT_EQ(a.name(), "Card & <co>");
    EXPECT_EQ(a.fill().color().toHex(), "#336699");
    EXPECT_TRUE(a.stroke().isNone());
    EXPECT_DOUBLE_EQ(a.opacity(), 0.5);
    EXPECT_TRUE(a.isLocked());
    EXPECT_NEAR(a.geometryBounds().x0, 10.0, 1e-3);
    EXPECT_NEAR(a.geometryBounds().x1, 110.0, 1e-3);
    const SketchObject& b = copy.object(1);
    EXPECT_EQ(b.type(), SketchObjectType::Line);
    EXPECT_FALSE(b.isVisible());
    EXPECT_DOUBLE_EQ(b.strokeWidth(), 3.0);
}

TEST(SketchSvg, CmykColorsSurviveTheRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    o.setFill(SketchPaint::solid(SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f, 0.75f)));
    o.setStroke(SketchPaint::solid(SketchColor::gray(0.25f)));
    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    const SketchColor& fill = copy.object(0).fill().color();
    EXPECT_EQ(fill.space(), ColorSpace::CMYK);
    EXPECT_NEAR(fill.component(0), 0.1f, 1e-4);
    EXPECT_NEAR(fill.component(3), 0.4f, 1e-4);
    EXPECT_NEAR(fill.alpha(), 0.75f, 1e-4);
    EXPECT_EQ(copy.object(0).stroke().color().space(), ColorSpace::Gray);
}

TEST(SketchSvg, GradientsRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    SketchGradient g;
    g.stops.push_back({0.0, SketchColor::fromHex("#ff0000")});
    g.stops.push_back({0.4, SketchColor::cmyk(1, 0, 0, 0)});
    g.stops.push_back({1.0, SketchColor::fromHex("#0000ff80")});
    g.angleDegrees = 35.0;
    g.spread = GradientSpread::Reflect;
    o.setFill(SketchPaint::linear(g));
    o.setStroke(SketchPaint::radial(g));

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    const SketchPaint& fill = copy.object(0).fill();
    ASSERT_EQ(fill.kind(), SketchPaint::Kind::Linear);
    EXPECT_NEAR(fill.gradient().angleDegrees, 35.0, 1e-3);
    EXPECT_EQ(fill.gradient().spread, GradientSpread::Reflect);
    ASSERT_EQ(fill.gradient().stops.size(), 3u);
    EXPECT_NEAR(fill.gradient().stops[1].offset, 0.4, 1e-5);
    EXPECT_EQ(fill.gradient().stops[1].color.space(), ColorSpace::CMYK);
    EXPECT_NEAR(fill.gradient().stops[2].color.alpha(), 0.5f, 0.01f);
    EXPECT_EQ(copy.object(0).stroke().kind(), SketchPaint::Kind::Radial);
}

TEST(SketchSvg, PointGradientsSurviveSvgAndBinary) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    SketchGradient g = redAndBluePoints();
    g.points.push_back({0.5, 0.25, SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f, 0.5f), 0.2});
    o.setFill(SketchPaint::points(g));
    o.setStroke(SketchPaint::points(redAndBluePoints()));

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    ASSERT_TRUE(copy.object(0).fill().isPoints());
    ASSERT_EQ(copy.object(0).fill().gradient().points.size(), 3u);
    EXPECT_NEAR(copy.object(0).fill().gradient().points[2].x, 0.5, 1e-5);
    EXPECT_NEAR(copy.object(0).fill().gradient().points[2].radius, 0.2, 1e-5);
    EXPECT_EQ(copy.object(0).fill().gradient().points[2].color.space(), ColorSpace::CMYK);
    EXPECT_NEAR(copy.object(0).fill().gradient().points[2].color.alpha(), 0.5f, 0.01f);
    EXPECT_TRUE(copy.object(0).stroke().isPoints());

    SketchDocument fromBinary;
    fromBinary.add(SketchObject::makeRect(0, 0, 1, 1));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), fromBinary));
    ASSERT_EQ(fromBinary.objectCount(), 1u);
    EXPECT_EQ(fromBinary.object(0).fill(), doc.object(0).fill());
    EXPECT_EQ(fromBinary.object(0).stroke(), doc.object(0).stroke());
}

TEST(SketchObject, PaintsFollowEveryTransformAndSurviveFiles) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    o.setFill(SketchPaint::points(redAndBluePoints()));
    placeInDocument(doc, o, BLMatrix2D(1.0, 0.0, 0.5, 1.0, 0.0, 0.0));   // a shear
    placeInDocument(doc, o, BLMatrix2D::make_translation(30.0, 5.0));

    const BLBox frame = o.paintFrameBounds();
    EXPECT_NEAR(frame.x0, 10.0, 1e-9);
    EXPECT_NEAR(frame.x1, 110.0, 1e-9);
    EXPECT_NEAR(frame.y0, 10.0, 1e-9);
    EXPECT_NEAR(frame.y1, 60.0, 1e-9);
    EXPECT_NEAR(o.paintToPage().m10, 0.5, 1e-12);

    SketchObject* copy = o.clone();
    EXPECT_NEAR(copy->paintToPage().m20, 30.0, 1e-12);
    delete copy;

    SketchDocument viaSvg = roundTrip(doc);
    ASSERT_EQ(viaSvg.objectCount(), 1u);
    EXPECT_NEAR(viaSvg.object(0).paintToPage().m10, 0.5, 1e-5);
    EXPECT_NEAR(viaSvg.object(0).paintToPage().m21, 5.0, 1e-5);

    SketchDocument viaBinary;
    viaBinary.add(SketchObject::makeRect(0, 0, 1, 1));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), viaBinary));
    ASSERT_EQ(viaBinary.objectCount(), 1u);
    EXPECT_NEAR(viaBinary.object(0).paintToPage().m10, 0.5, 1e-12);
    EXPECT_NEAR(viaBinary.object(0).paintToPage().m20, 30.0, 1e-12);
}

TEST(SketchSvg, GradientAxisRoundTripsAndFollowsTheObject) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(30, 20), BLPoint(90, 50));
    o.setFill(SketchPaint::linear(g));
    SketchObject& c = doc.add(SketchObject::makeEllipse(200, 100, 40, 40));
    SketchGradient r = SketchGradient::fromColor(SketchColor::fromHex("#00ff00"));
    r.setAxis(c.geometryBounds(), BLPoint(190, 90), BLPoint(190, 130));
    c.setFill(SketchPaint::radial(r));

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const SketchObject& was = doc.object(i);
        const SketchObject& now = copy.object(i);
        bool radial = i == 1;
        EXPECT_EQ(now.fill().kind(), was.fill().kind());
        ASSERT_TRUE(now.fill().gradient().hasAxis);
        BLPoint s0, e0, s1, e1;
        was.fill().gradient().axisPoints(was.geometryBounds(), radial, s0, e0);
        now.fill().gradient().axisPoints(now.geometryBounds(), radial, s1, e1);
        EXPECT_NEAR(s0.x, s1.x, 1e-3);
        EXPECT_NEAR(s0.y, s1.y, 1e-3);
        EXPECT_NEAR(e0.x, e1.x, 1e-3);
        EXPECT_NEAR(e0.y, e1.y, 1e-3);
    }
    // The axis is relative to the box, so moving the object moves it along.
    placeInDocument(copy, copy.object(0), BLMatrix2D::make_translation(50, 0));
    BLPoint moved, ignored;
    copy.object(0).fill().gradient().axisPoints(copy.object(0).geometryBounds(), false, moved, ignored);
    EXPECT_NEAR(moved.x, 80.0, 1e-3);
}

TEST(SketchSvg, ForeignGradientVectorsAreKept) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><linearGradient id='a' gradientUnits='userSpaceOnUse' x1='20' y1='10' x2='60' y2='10'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='b' cx='0.25' cy='0.5' r='0.25'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect x='10' y='0' width='100' height='20' fill='url(#a)'/>"
        "<rect x='0' y='0' width='40' height='40' fill='url(#b)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 2u);
    BLPoint start, end;
    doc.object(0).fill().gradient().axisPoints(doc.object(0).geometryBounds(), false, start, end);
    EXPECT_NEAR(start.x, 20.0, 1e-6);
    EXPECT_NEAR(end.x, 60.0, 1e-6);
    EXPECT_NEAR(start.y, 10.0, 1e-6);
    doc.object(1).fill().gradient().axisPoints(doc.object(1).geometryBounds(), true, start, end);
    EXPECT_NEAR(start.x, 10.0, 1e-6);   // 0.25 of 40
    EXPECT_NEAR(start.y, 20.0, 1e-6);
    EXPECT_NEAR(end.x - start.x, 10.0, 1e-6);  // r = 0.25 of 40
}

TEST(SketchSvg, EllipticalRadialAspectRoundTrips) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(50, 50), BLPoint(80, 80));
    g.axisAspect = 0.4;
    o.setFill(SketchPaint::radial(g));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("gradientTransform"), std::string::npos);  // other viewers see the ellipse too
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_NEAR(copy.object(0).fill().gradient().axisAspect, 0.4, 1e-4);
}

TEST(SketchSvg, ForeignGradientTransformBecomesAxisAndAspect) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><radialGradient id='b' gradientUnits='userSpaceOnUse' cx='50' cy='50' r='20' gradientTransform='translate(0 0) rotate(0 50 50) scale(1 1)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient>"
        "<linearGradient id='a' gradientUnits='userSpaceOnUse' x1='0' y1='0' x2='10' y2='0' gradientTransform='translate(20 30) scale(2)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='c' gradientUnits='userSpaceOnUse' cx='0' cy='0' r='10' gradientTransform='translate(50 50) scale(3 1.5)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect width='100' height='100' fill='url(#b)'/><rect width='100' height='100' fill='url(#a)'/><rect width='100' height='100' fill='url(#c)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 3u);
    BLPoint start, end;
    doc.object(1).fill().gradient().axisPoints(doc.object(1).geometryBounds(), false, start, end);
    EXPECT_NEAR(start.x, 20.0, 1e-6);
    EXPECT_NEAR(start.y, 30.0, 1e-6);
    EXPECT_NEAR(end.x, 40.0, 1e-6);  // 10 * 2 + 20
    doc.object(2).fill().gradient().axisPoints(doc.object(2).geometryBounds(), true, start, end);
    EXPECT_NEAR(start.x, 50.0, 1e-6);
    EXPECT_NEAR(end.x - start.x, 30.0, 1e-6);  // r = 10 scaled 3x along x
    EXPECT_NEAR(doc.object(2).fill().gradient().axisAspect, 0.5, 1e-6);  // 1.5 / 3
}

TEST(SketchSvg, SkewAndAlongStrokeRoundTrip) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    g.setAxis(o.geometryBounds(), BLPoint(0, 50), BLPoint(100, 50));
    g.axisSkew = 0.75;
    o.setFill(SketchPaint::linear(g));
    SketchGradient along = SketchGradient::fromColor(SketchColor::fromHex("#00ff00"));
    along.alongStroke = true;
    o.setStroke(SketchPaint::linear(along));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("gradientTransform"), std::string::npos);
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_NEAR(copy.object(0).fill().gradient().axisSkew, 0.75, 1e-4);
    EXPECT_TRUE(copy.object(0).stroke().gradient().alongStroke);
    EXPECT_FALSE(copy.object(0).fill().gradient().alongStroke);
}

TEST(SketchSvg, ForeignSkewIsKept) {
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<defs><linearGradient id='a' gradientUnits='userSpaceOnUse' x1='0' y1='0' x2='100' y2='0' gradientTransform='skewX(45)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient>"
        "<radialGradient id='b' gradientUnits='userSpaceOnUse' cx='50' cy='50' r='20' gradientTransform='rotate(0 50 50) translate(50 50) skewX(30) translate(-50 -50)'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></radialGradient></defs>"
        "<rect width='100' height='100' fill='url(#a)'/><rect width='100' height='100' fill='url(#b)'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_NEAR(doc.object(0).fill().gradient().axisSkew, 1.0, 1e-6);
    EXPECT_NEAR(doc.object(1).fill().gradient().axisSkew, std::tan(30.0 * 3.14159265358979323846 / 180.0), 1e-6);
    EXPECT_NEAR(doc.object(1).fill().gradient().axisAspect, 1.0, 1e-6);
}

TEST(SketchSvg, RotatedObjectsKeepTheirFrameAngle) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 40, 20));
    placeInDocument(doc, o, BLMatrix2D::make_rotation(0.5, 0.0, 0.0));
    o.setFrameAngle(0.5);
    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_NEAR(copy.object(0).frameAngle(), 0.5, 1e-5);
    EXPECT_NEAR(copy.object(0).geometryBounds().x1, o.geometryBounds().x1, 1e-3);
}

TEST(SketchSvg, ReadsForeignSvgShapesGroupsAndTransforms) {
    const char* svg =
        "<?xml version='1.0'?><!-- hi --><svg xmlns='http://www.w3.org/2000/svg' width='200' height='100' viewBox='0 0 100 50'>"
        "<defs><linearGradient id='g'><stop offset='0' stop-color='red'/><stop offset='100%' style='stop-color:#00f'/></linearGradient></defs>"
        "<g fill='#0f0' stroke='black' transform='translate(10,5)'>"
        "<rect x='0' y='0' width='20' height='10' rx='2'/>"
        "<circle cx='50' cy='20' r='5' fill='url(#g)'/>"
        "<path d='M0 0 h10 v10 h-10 z m20 0 l5 5' style='fill:rgb(255,0,0);stroke:none'/>"
        "<polygon points='0,0 10,0 5,8'/>"
        "<text>skipped</text>"
        "</g></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    EXPECT_FLOAT_EQ(doc.pageWidth(), 200.0f);
    ASSERT_EQ(doc.objectCount(), 4u);
    EXPECT_EQ(doc.object(0).type(), SketchObjectType::RoundRect);
    // viewBox scale 2 and translate(10,5): the rect's left edge lands at x = 20.
    EXPECT_NEAR(doc.object(0).geometryBounds().x0, 20.0, 1e-3);
    EXPECT_EQ(doc.object(0).fill().color().toHex(), "#00ff00");
    EXPECT_EQ(doc.object(1).type(), SketchObjectType::Ellipse);
    EXPECT_EQ(doc.object(1).fill().kind(), SketchPaint::Kind::Linear);
    EXPECT_EQ(doc.object(1).fill().gradient().stops.size(), 2u);
    EXPECT_EQ(doc.object(2).fill().color().toHex(), "#ff0000");
    EXPECT_TRUE(doc.object(2).stroke().isNone());
    EXPECT_EQ(doc.object(3).type(), SketchObjectType::Polygon);
}

TEST(SketchSvg, PathCommandsAndArcsParse) {
    const char* svg = "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
        "<path d='M10,10 C 20,0 30,0 40,10 S 60,20 70,10 Q 80,0 90,10 T 95,15 A 5 5 0 0 1 90 20 L 10 20 Z'/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 1u);
    BLBox b = doc.object(0).geometryBounds();
    EXPECT_NEAR(b.x0, 10.0, 1e-3);
    EXPECT_GE(b.x1, 94.9);
    EXPECT_GE(b.y1, 19.9);
}

TEST(SketchSvg, MalformedInputIsRejectedAndLeavesTheDocumentAlone) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(0, 0, 5, 5));
    EXPECT_FALSE(documentFromSvg("<svg><g></svg>", doc));
    EXPECT_FALSE(documentFromSvg("not xml at all", doc));
    EXPECT_FALSE(documentFromSvg("<html></html>", doc));
    EXPECT_EQ(doc.objectCount(), 1u);
}

TEST(SketchSvg, SavesAndLoadsThroughTheDocumentFileApi) {
    std::string path = (std::filesystem::temp_directory_path() / "sketch_test_roundtrip.svg").string();
    {
        SketchDocument doc;
        doc.add(SketchObject::makeEllipse(50, 50, 20, 10));
        ASSERT_TRUE(doc.save(path));
        EXPECT_FALSE(doc.isModified());
    }
    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(loaded.objectCount(), 1u);
    EXPECT_EQ(loaded.filePath(), path);
    EXPECT_FALSE(loaded.isModified());
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");
    EXPECT_FALSE(loaded.load("Z:/definitely/not/here.svg"));
}

// ---- Color conversion and the document color mode ---------------------------------------------

TEST(SketchColorConvert, RgbToCmykAndBackIsExact) {
    SketchColor rgb = SketchColor::rgb(0.2f, 0.4f, 0.6f, 0.5f);
    SketchColor cmyk = rgb.convertedTo(ColorSpace::CMYK);
    EXPECT_EQ(cmyk.space(), ColorSpace::CMYK);
    EXPECT_NEAR(cmyk.component(3), 0.4f, 1e-5);  // K = 1 - max
    EXPECT_FLOAT_EQ(cmyk.alpha(), 0.5f);
    SketchColor back = cmyk.convertedTo(ColorSpace::RGB);
    EXPECT_NEAR(back.component(0), 0.2f, 1e-5);
    EXPECT_NEAR(back.component(1), 0.4f, 1e-5);
    EXPECT_NEAR(back.component(2), 0.6f, 1e-5);
}

TEST(SketchColorConvert, BlackAndWhiteInCmyk) {
    SketchColor black = SketchColor::rgb(0, 0, 0).convertedTo(ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(black.component(3), 1.0f);
    EXPECT_FLOAT_EQ(black.component(0), 0.0f);
    SketchColor white = SketchColor::rgb(1, 1, 1).convertedTo(ColorSpace::CMYK);
    EXPECT_FLOAT_EQ(white.component(3), 0.0f);
}

TEST(SketchColorConvert, HsbAndGrayRoundTripThroughRgb) {
    SketchColor red = SketchColor::rgb(1, 0, 0).convertedTo(ColorSpace::HSB);
    EXPECT_NEAR(red.component(0), 0.0f, 1e-5);
    EXPECT_NEAR(red.component(1), 1.0f, 1e-5);
    SketchColor teal = SketchColor::rgb(0.1f, 0.6f, 0.5f);
    SketchColor back = teal.convertedTo(ColorSpace::HSB).convertedTo(ColorSpace::RGB);
    EXPECT_NEAR(back.component(0), 0.1f, 1e-4);
    EXPECT_NEAR(back.component(1), 0.6f, 1e-4);
    EXPECT_NEAR(back.component(2), 0.5f, 1e-4);
    EXPECT_NEAR(SketchColor::rgb(0.5f, 0.5f, 0.5f).convertedTo(ColorSpace::Gray).component(0), 0.5f, 1e-5);
    EXPECT_EQ(SketchColor::gray(0.3f).convertedTo(ColorSpace::Gray), SketchColor::gray(0.3f));
}

TEST(SketchDocument, ColorModeConversionIsOneUndoStep) {
    SketchDocument doc;
    SketchObject& o = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    SketchGradient g;
    g.stops.push_back({0.0, SketchColor::rgb(1, 0, 0)});
    g.stops.push_back({1.0, SketchColor::rgb(0, 0, 1)});
    o.setFill(SketchPaint::linear(g));
    o.setStroke(SketchPaint::solid(SketchColor::rgb(0.2f, 0.4f, 0.6f)));
    std::size_t id = o.id();
    doc.undoStack().clear();

    EXPECT_FALSE(doc.convertToColorMode(ColorSpace::RGB));  // already RGB
    EXPECT_TRUE(doc.convertToColorMode(ColorSpace::CMYK));
    EXPECT_EQ(doc.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(doc.findById(id)->stroke().color().space(), ColorSpace::CMYK);
    EXPECT_EQ(doc.findById(id)->fill().gradient().stops[1].color.space(), ColorSpace::CMYK);
    EXPECT_EQ(doc.defaultFill().color().space(), ColorSpace::CMYK);

    doc.undoStack().undo();
    EXPECT_EQ(doc.colorMode(), ColorSpace::RGB);
    EXPECT_EQ(doc.findById(id)->stroke().color().space(), ColorSpace::RGB);
    EXPECT_EQ(doc.defaultFill().color().space(), ColorSpace::RGB);
    EXPECT_FALSE(doc.undoStack().canUndo());
    doc.undoStack().redo();
    EXPECT_EQ(doc.colorMode(), ColorSpace::CMYK);
}

TEST(SketchDocument, OnlyRgbAndCmykAreDocumentModes) {
    SketchDocument doc;
    EXPECT_THROW(doc.setColorMode(ColorSpace::Gray), std::runtime_error);
    EXPECT_THROW(doc.convertToColorMode(ColorSpace::HSB), std::runtime_error);
}

TEST(SketchSvg, ColorModeRoundTrips) {
    SketchDocument doc;
    doc.setColorMode(ColorSpace::CMYK);
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(copy.defaultFill().color().space(), ColorSpace::CMYK);
    // A file without the attribute is an RGB document, even when loaded over a CMYK one.
    ASSERT_TRUE(documentFromSvg("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'/>", copy));
    EXPECT_EQ(copy.colorMode(), ColorSpace::RGB);
}

// ---- New Document model --------------------------------------------------------------------------

#include "curvy/newdocumentmodel.h"

TEST(NewDocumentModel, StartsAsLetterCmyk) {
    NewDocumentModel model;
    EXPECT_EQ(model.name(), "Untitled-1");
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
    EXPECT_NEAR(model.heightPoints(), 792.0, 1e-6);
    EXPECT_EQ(model.colorMode(), ColorSpace::CMYK);
    EXPECT_EQ(model.pixelWidth(), 2550);
    EXPECT_EQ(model.pixelHeight(), 3300);
    EXPECT_FALSE(model.isLandscape());
}

TEST(NewDocumentModel, LoadSetupShowsAnExistingPageInPoints) {
    NewDocumentModel model;
    model.loadSetup(595.0, 842.0, 150.0, ColorSpace::RGB, PageBackground::Transparent);
    EXPECT_EQ(model.unit(), LengthUnit::Points);
    EXPECT_DOUBLE_EQ(model.widthPoints(), 595.0);
    EXPECT_DOUBLE_EQ(model.heightPoints(), 842.0);
    EXPECT_DOUBLE_EQ(model.ppi(), 150.0);
    EXPECT_EQ(model.colorMode(), ColorSpace::RGB);
    EXPECT_EQ(model.background(), PageBackground::Transparent);
    model.setUnit(LengthUnit::Inches);  // converting keeps the page the same physical size
    EXPECT_NEAR(model.width(), 595.0 / 72.0, 1e-9);
    EXPECT_NEAR(model.widthPoints(), 595.0, 1e-9);
}

TEST(NewDocumentModel, UnitChangeKeepsThePhysicalSize) {
    NewDocumentModel model;
    model.setUnit(LengthUnit::Millimeters);
    EXPECT_NEAR(model.width(), 215.9, 0.01);
    EXPECT_NEAR(model.height(), 279.4, 0.01);
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
    model.setUnit(LengthUnit::Pixels);  // 300 ppi
    EXPECT_NEAR(model.width(), 2550.0, 1e-6);
    EXPECT_NEAR(model.widthPoints(), 612.0, 1e-6);
}

TEST(NewDocumentModel, PixelUnitsFollowThePpi) {
    NewDocumentModel model;
    model.loadPreset(NewDocumentModel::presets()[10]);  // Full HD, px @ 72
    EXPECT_NEAR(model.widthPoints(), 1920.0, 1e-6);
    model.setPpi(144.0);  // same pixel count, half the physical size
    EXPECT_NEAR(model.widthPoints(), 960.0, 1e-6);
    EXPECT_EQ(model.pixelWidth(), 1920);
    model.setPpi(-5.0);  // ignored
    EXPECT_NEAR(model.ppi(), 144.0, 1e-9);
}

TEST(NewDocumentModel, OrientationSwapsOnlyWhenNeeded) {
    NewDocumentModel model;  // 8.5 x 11 portrait
    model.setLandscape(false);
    EXPECT_NEAR(model.width(), 8.5, 1e-9);
    model.setLandscape(true);
    EXPECT_NEAR(model.width(), 11.0, 1e-9);
    EXPECT_NEAR(model.height(), 8.5, 1e-9);
    EXPECT_TRUE(model.isLandscape());
    model.setLandscape(true);
    EXPECT_NEAR(model.width(), 11.0, 1e-9);
}

TEST(NewDocumentModel, KeepProportionsScalesTheOtherSide) {
    NewDocumentModel model;
    model.loadPreset(NewDocumentModel::presets()[19]);  // App icon would be square; use A4 instead
    model.loadPreset(NewDocumentModel::presets()[4]);   // A4 210 x 297 mm
    model.setKeepProportions(true);
    model.setWidth(105.0);
    EXPECT_NEAR(model.height(), 148.5, 0.01);
    model.setHeight(594.0);
    EXPECT_NEAR(model.width(), 420.0, 0.01);
    model.setKeepProportions(false);
    model.setWidth(100.0);
    EXPECT_NEAR(model.height(), 594.0, 1e-9);
    model.setWidth(0.0);  // ignored
    EXPECT_NEAR(model.width(), 100.0, 1e-9);
}

TEST(NewDocumentModel, PresetsAndCategories) {
    std::vector<std::string> categories = NewDocumentModel::categories();
    ASSERT_EQ(categories.size(), 4u);
    EXPECT_EQ(categories[0], "Print");
    EXPECT_EQ(categories[3], "Art");
    EXPECT_EQ(NewDocumentModel::presetsIn("Print").size(), 10u);
    EXPECT_EQ(NewDocumentModel::presetsIn("Web").size(), 6u);
    EXPECT_EQ(NewDocumentModel::rowText(NewDocumentModel::presets()[0]), "Letter   8.5 x 11 in   CMYK, 300 ppi");
}

TEST(NewDocumentModel, SummaryAndLabels) {
    NewDocumentModel model;
    EXPECT_EQ(model.widthLabel(), "8.5 in");
    EXPECT_EQ(model.heightLabel(), "11 in");
    EXPECT_EQ(model.summary(), "612 x 792 pt  =  8.5 x 11 in  =  215.9 x 279.4 mm\n2550 x 3300 px at 300 ppi  -  CMYK  -  Portrait");
}

TEST(NewDocumentModel, OnlyRgbAndCmykColorModes) {
    NewDocumentModel model;
    model.setColorMode(ColorSpace::HSB);
    EXPECT_EQ(model.colorMode(), ColorSpace::RGB);
    model.setColorMode(ColorSpace::CMYK);
    EXPECT_EQ(model.colorMode(), ColorSpace::CMYK);
}

TEST(SketchSvg, PageBackgroundRoundTrips) {
    SketchDocument doc;
    EXPECT_TRUE(doc.pageBackground().has_value());  // white by default
    doc.add(SketchObject::makeRect(0, 0, 5, 5));
    std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("id=\"pageBackground\""), std::string::npos);  // a real rect for other viewers

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    EXPECT_EQ(copy.objectCount(), 1u);  // the background rect is not an object
    ASSERT_TRUE(copy.pageBackground().has_value());
    EXPECT_EQ(copy.pageBackground()->toHex(), "#ffffff");

    doc.setPageBackground(SketchColor::cmyk(0.1f, 0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_EQ(copy.pageBackground()->space(), ColorSpace::CMYK);

    doc.setPageBackground(std::nullopt);
    std::string transparent = svgFromDocument(doc);
    EXPECT_EQ(transparent.find("id=\"pageBackground\""), std::string::npos);
    ASSERT_TRUE(documentFromSvg(transparent, copy));
    EXPECT_FALSE(copy.pageBackground().has_value());
}

// ---- Graphic Styles --------------------------------------------------------------------------------------------------

namespace {
    // A document with one selected rectangle wearing a distinctive look: blue fill, red 3 pt stroke, a drop shadow, 60% opacity.
    SketchObject& addStyledRect(SketchDocument& doc) {
        SketchObject& rect = doc.add(SketchObject::makeRect(10, 10, 80, 40));
        std::vector<AppearanceItem> items = {AppearanceItem::makeFill(SketchPaint::solid(SketchColor::fromHex("#0000ff"))),
            AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::fromHex("#ff0000")), 3.0), AppearanceItem::makeEffect(EffectKind::DropShadow)};
        rect.setAppearance(items);
        rect.setOpacity(0.6);
        rect.setBlendMode(BlendMode::Multiply);
        doc.setSelection({rect.id()});
        return rect;
    }
}

TEST(SketchGraphicStyles, ANewDocumentStartsWithTheStarterStyles) {
    SketchDocument doc;
    EXPECT_EQ(doc.graphicStyles().size(), defaultGraphicStyles().size());
    ASSERT_GE(doc.graphicStyles().size(), 5u);
    EXPECT_EQ(doc.graphicStyles()[0].name, "Default");
    EXPECT_EQ(doc.graphicStyles()[1].items.back().kind, AppearanceKind::Effect);   // Sunset + shadow
    EXPECT_NEAR(doc.graphicStyles().back().opacity, 0.55, 1e-9);                    // Ghost
}

TEST(SketchGraphicStyles, SavingTheSelectionAddsANamedStyleAndIsOneUndoStep) {
    SketchDocument doc;
    std::size_t before = doc.graphicStyles().size();
    EXPECT_FALSE(doc.addGraphicStyleFromSelection());   // nothing selected
    addStyledRect(doc);
    doc.undoStack().clear();
    ASSERT_TRUE(doc.addGraphicStyleFromSelection());
    ASSERT_EQ(doc.graphicStyles().size(), before + 1);
    const GraphicStyle& saved = doc.graphicStyles().back();
    EXPECT_EQ(saved.items.size(), 3u);
    EXPECT_EQ(saved.blendMode, BlendMode::Multiply);
    EXPECT_NEAR(saved.opacity, 0.6, 1e-9);
    EXPECT_EQ(saved.name, "Style " + std::to_string(before + 1));
    ASSERT_TRUE(doc.addGraphicStyleFromSelection());
    EXPECT_NE(doc.graphicStyles()[before].name, doc.graphicStyles()[before + 1].name);   // names stay unique
    doc.undoStack().undo();
    EXPECT_EQ(doc.graphicStyles().size(), before + 1);
    doc.undoStack().undo();
    EXPECT_EQ(doc.graphicStyles().size(), before);
    doc.undoStack().redo();
    EXPECT_EQ(doc.graphicStyles().size(), before + 1);
}

TEST(SketchGraphicStyles, ApplyingReplacesTheSelectionsWholeAppearanceAndUndoes) {
    SketchDocument doc;
    addStyledRect(doc);
    ASSERT_TRUE(doc.addGraphicStyleFromSelection());
    std::size_t index = doc.graphicStyles().size() - 1;

    SketchObject& other = doc.add(SketchObject::makeEllipse(100, 100, 30, 30));
    other.setFill(SketchPaint::solid(SketchColor::fromHex("#00ff00")));
    doc.setSelection({other.id()});
    doc.undoStack().clear();
    ASSERT_TRUE(doc.applyGraphicStyle(index));
    EXPECT_EQ(other.appearance().size(), 3u);
    EXPECT_EQ(other.fill().color().toHex(), "#0000ff");
    EXPECT_EQ(other.stroke().color().toHex(), "#ff0000");
    EXPECT_NEAR(other.opacity(), 0.6, 1e-9);
    EXPECT_EQ(other.blendMode(), BlendMode::Multiply);
    EXPECT_FALSE(doc.applyGraphicStyle(index));   // already wearing it: nothing changes, no step
    doc.undoStack().undo();
    EXPECT_EQ(other.appearance().size(), 2u);
    EXPECT_EQ(other.fill().color().toHex(), "#00ff00");
    EXPECT_EQ(other.opacity(), 1.0);

    doc.clearSelection();
    EXPECT_FALSE(doc.applyGraphicStyle(index));    // nothing selected
    doc.setSelection({other.id()});
    EXPECT_FALSE(doc.applyGraphicStyle(9999));     // no such style
}

TEST(SketchGraphicStyles, DeletingRemovesOneStyleAndUndoes) {
    SketchDocument doc;
    std::size_t count = doc.graphicStyles().size();
    std::string second = doc.graphicStyles()[1].name;
    EXPECT_FALSE(doc.deleteGraphicStyle(count));
    ASSERT_TRUE(doc.deleteGraphicStyle(0));
    EXPECT_EQ(doc.graphicStyles().size(), count - 1);
    EXPECT_EQ(doc.graphicStyles()[0].name, second);
    doc.undoStack().undo();
    EXPECT_EQ(doc.graphicStyles()[0].name, "Default");
    EXPECT_EQ(doc.graphicStyles().size(), count);
}

TEST(SketchGraphicStyles, StylesRoundTripThroughSvg) {
    SketchDocument doc;
    addStyledRect(doc);
    ASSERT_TRUE(doc.addGraphicStyleFromSelection());
    std::string svg = svgFromDocument(doc);
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    ASSERT_EQ(copy.graphicStyles().size(), doc.graphicStyles().size());
    for (std::size_t i = 0; i < doc.graphicStyles().size(); ++i) {
        const GraphicStyle& a = doc.graphicStyles()[i];
        const GraphicStyle& b = copy.graphicStyles()[i];
        EXPECT_EQ(a.name, b.name) << i;
        EXPECT_EQ(a.items.size(), b.items.size()) << a.name;
        EXPECT_NEAR(a.opacity, b.opacity, 1e-6) << a.name;
        EXPECT_EQ(a.blendMode, b.blendMode) << a.name;
    }
    const GraphicStyle& saved = copy.graphicStyles().back();
    EXPECT_EQ(saved.items[1].paint.color().toHex(), "#ff0000");
    EXPECT_NEAR(saved.items[1].strokeWidth, 3.0, 1e-6);
    EXPECT_EQ(saved.items[2].kind, AppearanceKind::Effect);
    // The styles are metadata, not drawing: the file still holds just the one real object.
    EXPECT_EQ(copy.objectCount(), 1u);
    // Gradient and dashed starter styles keep their look.
    EXPECT_TRUE(copy.graphicStyles()[1].items[0].paint.isGradient());   // Sunset + shadow
    EXPECT_FALSE(copy.graphicStyles()[4].items[1].strokeOptions.dashes.empty());   // Dashed red
}

TEST(SketchGraphicStyles, AnEmptyListSurvivesAndAFileWithoutStylesGetsTheStarterSet) {
    SketchDocument doc;
    doc.setGraphicStyles({});
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    EXPECT_TRUE(copy.graphicStyles().empty());   // the user deleted them all: they stay deleted

    SketchDocument plain;
    plain.setGraphicStyles({});
    ASSERT_TRUE(documentFromSvg("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'/></svg>", plain));
    EXPECT_EQ(plain.graphicStyles().size(), defaultGraphicStyles().size());   // a foreign SVG has none of its own
}

TEST(SketchGraphicStyles, ColorModeConversionConvertsTheStylesToo) {
    SketchDocument doc;
    ASSERT_TRUE(doc.convertToColorMode(ColorSpace::CMYK));
    EXPECT_EQ(doc.graphicStyles()[2].items[0].paint.color().space(), ColorSpace::CMYK);   // Blue card's fill
    doc.undoStack().undo();
    EXPECT_EQ(doc.graphicStyles()[2].items[0].paint.color().space(), ColorSpace::RGB);
}

// ---- Drawing tool geometry (shapetools.h) ----------------------------------------------------------------------------

TEST(SketchShapeTools, RegularPolygonsAndStars) {
    std::vector<BLPoint> hexagon = regularPolygonPoints(BLPoint(50, 50), 10.0, 6, -1.5707963267948966);
    ASSERT_EQ(hexagon.size(), 6u);
    EXPECT_NEAR(hexagon[0].x, 50.0, 1e-9);   // the first corner points straight up (y grows downward)
    EXPECT_NEAR(hexagon[0].y, 40.0, 1e-9);
    for (const BLPoint& p : hexagon) {
        EXPECT_NEAR(std::hypot(p.x - 50.0, p.y - 50.0), 10.0, 1e-9);
    }
    EXPECT_EQ(regularPolygonPoints(BLPoint(0, 0), 5.0, 1, 0.0).size(), 3u);   // fewer than three sides clamps to a triangle

    std::vector<BLPoint> star = starPoints(BLPoint(0, 0), 10.0, 0.5, 5, -1.5707963267948966);
    ASSERT_EQ(star.size(), 10u);
    EXPECT_NEAR(std::hypot(star[0].x, star[0].y), 10.0, 1e-9);   // outer corner
    EXPECT_NEAR(std::hypot(star[1].x, star[1].y), 5.0, 1e-9);    // inner corner
}

TEST(SketchShapeTools, SimplifyKeepsTheShapeWithinTolerance) {
    std::vector<BLPoint> line;
    for (int i = 0; i <= 100; ++i) {
        line.push_back(BLPoint(double(i), 0.01 * double(i % 2)));   // a straight line with tiny noise
    }
    std::vector<BLPoint> simple = simplifyPolyline(line, 0.5);
    EXPECT_EQ(simple.size(), 2u);
    EXPECT_NEAR(simple.front().x, 0.0, 1e-9);
    EXPECT_NEAR(simple.back().x, 100.0, 1e-9);

    std::vector<BLPoint> corner = {BLPoint(0, 0), BLPoint(5, 0), BLPoint(10, 0), BLPoint(10, 5), BLPoint(10, 10)};
    std::vector<BLPoint> kept = simplifyPolyline(corner, 0.5);
    ASSERT_EQ(kept.size(), 3u);                       // the corner survives
    EXPECT_NEAR(kept[1].x, 10.0, 1e-9);
    EXPECT_NEAR(kept[1].y, 0.0, 1e-9);
    EXPECT_EQ(simplifyPolyline({BLPoint(1, 1), BLPoint(2, 2)}, 1.0).size(), 2u);
}

TEST(SketchShapeTools, SmoothPathPassesThroughEveryPoint) {
    std::vector<BLPoint> points = {BLPoint(0, 0), BLPoint(10, 10), BLPoint(20, 0), BLPoint(30, 10)};
    BLPath path = smoothPath(points);
    EXPECT_EQ(path.size(), 1u + 3u * 3u);            // a move and three cubics of three vertices each
    const BLPoint* v = path.vertex_data();
    EXPECT_NEAR(v[3].x, 10.0, 1e-9);                 // each cubic ends on the next point
    EXPECT_NEAR(v[6].x, 20.0, 1e-9);
    EXPECT_NEAR(v[9].x, 30.0, 1e-9);
    EXPECT_EQ(smoothPath({BLPoint(0, 0), BLPoint(4, 4)}).size(), 2u);   // two points: a plain line
    EXPECT_EQ(smoothPath({}).size(), 0u);
}

TEST(SketchShapeTools, PenPathUsesLinesForCornersAndCubicsForHandles) {
    PenAnchor a, b, c;
    a.point = BLPoint(0, 0);
    b.point = BLPoint(10, 0);
    c.point = BLPoint(10, 10);
    BLPath corners = penPath({a, b, c}, false);
    EXPECT_EQ(corners.size(), 3u);                    // move, line, line
    b.out = BLPoint(15, 0);
    b.hasOut = true;
    BLPath curved = penPath({a, b, c}, false);
    EXPECT_EQ(curved.size(), 1u + 1u + 3u);           // move, line a-b, cubic b-c
    BLPath closed = penPath({a, b, c}, true);
    SketchObject* shape = SketchObject::makePath(closed);
    EXPECT_TRUE(shape->isClosed());
    delete shape;
    EXPECT_EQ(penPath({}, false).size(), 0u);
}

// ---- Cursor hotspots (cursorset.h) -----------------------------------------------------------------------------------

TEST(SketchCursors, HotspotsAreReadFromTheJsonFile) {
    std::map<std::string, CursorHotspot> spots = parseCursorHotspots(
        R"({ "select": { "x": 7, "y": 3, "size": 32 },
  "pen": { "x": 6, "y": 27, "size": 32 } })");
    ASSERT_EQ(spots.size(), 2u);
    EXPECT_EQ(spots["select"].x, 7);
    EXPECT_EQ(spots["select"].y, 3);
    EXPECT_EQ(spots["pen"].x, 6);
    EXPECT_EQ(spots["pen"].y, 27);
    EXPECT_EQ(spots["pen"].size, 32);
    EXPECT_TRUE(parseCursorHotspots("").empty());
    EXPECT_TRUE(parseCursorHotspots("not json").empty());
    // An entry that cannot be read ends the scan but keeps what came before it.
    EXPECT_EQ(parseCursorHotspots("{ \"a\": { \"x\": 1 }, \"b\": { \"x\": oops } }").size(), 1u);
}

TEST(SketchShapeTools, PathsSplitIntoAnchorsAndBackWithoutChange) {
    // A rectangle: one closed outline of four corners, no handles (the closing line is folded into the first anchor).
    std::unique_ptr<SketchObject> rect(SketchObject::makeRect(10, 20, 100, 50));
    std::vector<SubPath> subs = pathToSubPaths(rect->geometry());
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_TRUE(subs[0].closed);
    EXPECT_EQ(subs[0].anchors.size(), 4u);
    for (const PenAnchor& a : subs[0].anchors) {
        EXPECT_FALSE(a.hasIn || a.hasOut);
    }
    BLBox before, after;
    rect->geometry().get_bounding_box(&before);
    subPathsToPath(subs).get_bounding_box(&after);
    EXPECT_NEAR(before.x0, after.x0, 1e-9);
    EXPECT_NEAR(before.y1, after.y1, 1e-9);

    // An ellipse: four smooth anchors with handles on both sides.
    std::unique_ptr<SketchObject> oval(SketchObject::makeEllipse(50, 50, 30, 20));
    std::vector<SubPath> ovalSubs = pathToSubPaths(oval->geometry());
    ASSERT_EQ(ovalSubs.size(), 1u);
    EXPECT_EQ(ovalSubs[0].anchors.size(), 4u);
    for (const PenAnchor& a : ovalSubs[0].anchors) {
        EXPECT_TRUE(a.hasIn && a.hasOut);
    }

    // A pen path round-trips exactly, and a corner anchor keeps no phantom handle.
    PenAnchor a, b, c;
    a.point = BLPoint(0, 0);
    b.point = BLPoint(10, 0);
    b.out = BLPoint(15, 5);
    b.hasOut = true;
    c.point = BLPoint(20, 10);
    BLPath path = penPath({a, b, c}, false);
    std::vector<SubPath> penSubs = pathToSubPaths(path);
    ASSERT_EQ(penSubs.size(), 1u);
    ASSERT_EQ(penSubs[0].anchors.size(), 3u);
    EXPECT_FALSE(penSubs[0].closed);
    EXPECT_FALSE(penSubs[0].anchors[0].hasOut);   // the line a-b has no handles
    EXPECT_TRUE(penSubs[0].anchors[1].hasOut);
    EXPECT_FALSE(penSubs[0].anchors[1].hasIn);    // the cubic b-c starts with b's handle and ends with c's own (none: on the anchor)
    EXPECT_FALSE(penSubs[0].anchors[2].hasIn);
    EXPECT_EQ(subPathsToPath(penSubs).size(), path.size());
    EXPECT_TRUE(pathToSubPaths(BLPath()).empty());
}

// ---- Cursor images: the SVG must SCALE to the requested size, not be cropped to it ------------------------------------

#include <newui/svgimage.h>

namespace {
    // The box of pixels with any alpha in an image: {x0, y0, x1, y1}, or all -1 when empty.
    std::array<int, 4> opaqueBox(const BLImage& image) {
        BLImageData data;
        const_cast<BLImage&>(image).get_data(&data);
        int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
        for (int y = 0; y < data.size.h; ++y) {
            const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::size_t(y) * std::size_t(data.stride));
            for (int x = 0; x < data.size.w; ++x) {
                if ((row[x] >> 24) != 0) {
                    if (x < x0) { x0 = x; } if (y < y0) { y0 = y; } if (x > x1) { x1 = x; } if (y > y1) { y1 = y; }
                }
            }
        }
        return x1 < 0 ? std::array<int, 4>{-1, -1, -1, -1} : std::array<int, 4>{x0, y0, x1, y1};
    }
}

TEST(SketchCursors, ASvgCursorScalesToTheSizeItIsRenderedAt) {
    std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "cursors" / "select.svg";
    ASSERT_TRUE(std::filesystem::exists(file)) << file;
    BLImage fullImage, halfImage;
    ASSERT_TRUE(newui::renderSvgFile(file.string(), 32, 32, fullImage));
    ASSERT_TRUE(newui::renderSvgFile(file.string(), 16, 16, halfImage));
    std::array<int, 4> a = opaqueBox(fullImage), b = opaqueBox(halfImage);
    // The arrow spans about x 5..25, y 1..30 of the 32 px drawing. At 16 px it must span about half of that - all of it,
    // not just the corner that happens to fit (a cropped image reaches the 16 px edge and is as big as before).
    EXPECT_LT(b[2], 15) << "the arrow runs off the right edge: the drawing was cropped, not scaled";
    EXPECT_LT(b[3], 15) << "the arrow runs off the bottom edge: the drawing was cropped, not scaled";
    EXPECT_NEAR(double(b[3] - b[1]) * 2.0, double(a[3] - a[1]), 4.0);
    EXPECT_NEAR(double(b[2] - b[0]) * 2.0, double(a[2] - a[0]), 4.0);
}

TEST(SketchShapeTools, RegularPolygonsAndStarsAreRecognisedFromTheirCorners) {
    RegularShape shape;
    std::vector<BLPoint> pentagon = regularPolygonPoints(BLPoint(100, 80), 40.0, 5, -1.2);
    ASSERT_TRUE(analyzeRegularShape(pentagon, shape));
    EXPECT_FALSE(shape.star);
    EXPECT_EQ(shape.points, 5);
    EXPECT_NEAR(shape.center.x, 100.0, 1e-9);
    EXPECT_NEAR(shape.outer, 40.0, 1e-9);
    EXPECT_NEAR(shape.firstAngle, -1.2, 1e-9);

    std::vector<BLPoint> star = starPoints(BLPoint(10, 20), 50.0, 0.4, 7, 0.3);
    ASSERT_TRUE(analyzeRegularShape(star, shape));
    EXPECT_TRUE(shape.star);
    EXPECT_EQ(shape.points, 7);
    EXPECT_NEAR(shape.outer, 50.0, 1e-9);
    EXPECT_NEAR(shape.ratio(), 0.4, 1e-9);
    EXPECT_NEAR(shape.firstAngle, 0.3, 1e-9);

    // The shape that comes back from its own numbers is the shape it was (the first corner is where it was).
    BLPath again = regularShapePath(shape);
    EXPECT_EQ(again.size(), star.size() + 1);   // the corners and a close
    EXPECT_NEAR(again.vertex_data()[0].x, star[0].x, 1e-9);

    // Not regular: squashed, edited, or too few corners.
    std::vector<BLPoint> squashed = pentagon;
    for (BLPoint& p : squashed) { p.y = 80.0 + (p.y - 80.0) * 0.5; }
    EXPECT_FALSE(analyzeRegularShape(squashed, shape));
    std::vector<BLPoint> moved = pentagon;
    moved[2].x += 5.0;
    EXPECT_FALSE(analyzeRegularShape(moved, shape));
    EXPECT_FALSE(analyzeRegularShape({BLPoint(0, 0), BLPoint(1, 1)}, shape));

    // A star whose first listed corner is an inner one is still a star (the outer corners are the other parity).
    std::vector<BLPoint> shifted(star.begin() + 1, star.end());
    shifted.push_back(star.front());
    ASSERT_TRUE(analyzeRegularShape(shifted, shape));
    EXPECT_TRUE(shape.star);
    EXPECT_EQ(shape.points, 7);
}

TEST(SketchShapeTools, RoundedRectanglesAreRecognisedWithTheirRadiusAndRebuilt) {
    std::unique_ptr<SketchObject> plain(SketchObject::makeRect(10, 20, 100, 50));
    RoundRectShape shape;
    ASSERT_TRUE(analyzeRoundRect(plain->geometry(), 0.0, shape));
    EXPECT_NEAR(shape.radius, 0.0, 1e-9);
    EXPECT_NEAR(shape.box.x1, 110.0, 1e-9);

    std::unique_ptr<SketchObject> round(SketchObject::makeRoundRect(10, 20, 100, 50, 12.0));
    ASSERT_TRUE(analyzeRoundRect(round->geometry(), 0.0, shape));
    EXPECT_NEAR(shape.radius, 12.0, 1e-6);
    EXPECT_NEAR(shape.box.x0, 10.0, 1e-9);
    EXPECT_NEAR(shape.box.y1, 70.0, 1e-9);

    shape.radius = 20.0;
    std::unique_ptr<SketchObject> bigger(SketchObject::makePath(roundRectPath(shape)));
    RoundRectShape again;
    ASSERT_TRUE(analyzeRoundRect(bigger->geometry(), 0.0, again));
    EXPECT_NEAR(again.radius, 20.0, 1e-6);
    shape.radius = 999.0;   // clamped to half the shorter side
    RoundRectShape clamped;
    ASSERT_TRUE(analyzeRoundRect(roundRectPath(shape), 0.0, clamped));
    EXPECT_NEAR(clamped.radius, 25.0, 1e-6);

    // Turned by the frame angle: the same box and radius come out.
    BLPath turned = round->geometry();
    turned.transform(BLMatrix2D::make_rotation(0.5));
    RoundRectShape rotated;
    ASSERT_TRUE(analyzeRoundRect(turned, 0.5, rotated));
    EXPECT_NEAR(rotated.radius, 12.0, 1e-6);
    EXPECT_NEAR(rotated.box.x1 - rotated.box.x0, 100.0, 1e-6);

    // An ellipse or a triangle is no rounded rectangle.
    std::unique_ptr<SketchObject> oval(SketchObject::makeEllipse(50, 50, 30, 20));
    EXPECT_FALSE(analyzeRoundRect(oval->geometry(), 0.0, shape));
    std::unique_ptr<SketchObject> triangle(SketchObject::makePolygon({BLPoint(0, 0), BLPoint(10, 0), BLPoint(5, 8)}));
    EXPECT_FALSE(analyzeRoundRect(triangle->geometry(), 0.0, shape));
}

TEST(SketchShapeTools, ArcsAreRebuiltAndRecognisedFromTheirOutline) {
    ArcShape arc;
    arc.center = BLPoint(100, 80);
    arc.radius = 40.0;
    arc.start = -0.5;
    arc.sweep = 2.4;   // more than a quarter turn: several segments
    BLPath path = arcPath(arc);
    EXPECT_GT(path.size(), 0u);
    ArcShape found;
    ASSERT_TRUE(analyzeArc(path, found));
    EXPECT_NEAR(found.center.x, 100.0, 1e-3);
    EXPECT_NEAR(found.center.y, 80.0, 1e-3);
    EXPECT_NEAR(found.radius, 40.0, 0.05);
    EXPECT_NEAR(found.start, -0.5, 1e-3);
    EXPECT_NEAR(found.sweep, 2.4, 1e-3);
    EXPECT_NEAR(arc.endPoint().x, 100.0 + 40.0 * std::cos(1.9), 1e-9);

    // A short arc (one segment), a negative sweep and a nearly full circle.
    arc.sweep = 0.9;
    ASSERT_TRUE(analyzeArc(arcPath(arc), found));
    EXPECT_NEAR(found.sweep, 0.9, 1e-3);
    arc.sweep = -2.0;
    ASSERT_TRUE(analyzeArc(arcPath(arc), found));
    EXPECT_NEAR(found.sweep, -2.0, 1e-3);
    arc.sweep = 6.0;
    ASSERT_TRUE(analyzeArc(arcPath(arc), found));
    EXPECT_NEAR(found.sweep, 6.0, 1e-3);

    // Not arcs: a straight line, a closed outline, a wavy curve.
    BLPath line;
    line.move_to(0, 0);
    line.line_to(10, 10);
    EXPECT_FALSE(analyzeArc(line, found));
    std::unique_ptr<SketchObject> oval(SketchObject::makeEllipse(50, 50, 30, 30));
    EXPECT_FALSE(analyzeArc(oval->geometry(), found));
    EXPECT_FALSE(analyzeArc(smoothPath({BLPoint(0, 0), BLPoint(10, 10), BLPoint(20, 0), BLPoint(30, 10)}), found));
    EXPECT_EQ(arcPath(ArcShape()).size(), 0u);
}

namespace {
    // The largest distance from any of `points` to the nearest sample of the curve through `anchors` (the curve sampled finely).
    double deviation(const std::vector<BLPoint>& points, const std::vector<PenAnchor>& anchors) {
        std::vector<BLPoint> curve;
        for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
            BLPoint p0 = anchors[i].point, p3 = anchors[i + 1].point;
            BLPoint p1 = anchors[i].hasOut ? anchors[i].out : p0, p2 = anchors[i + 1].hasIn ? anchors[i + 1].in : p3;
            for (int k = 0; k <= 200; ++k) {
                double t = k / 200.0, s = 1.0 - t;
                curve.push_back(BLPoint(s * s * s * p0.x + 3 * s * s * t * p1.x + 3 * s * t * t * p2.x + t * t * t * p3.x,
                    s * s * s * p0.y + 3 * s * s * t * p1.y + 3 * s * t * t * p2.y + t * t * t * p3.y));
            }
        }
        double worst = 0.0;
        for (const BLPoint& p : points) {
            double nearest = 1e18;
            for (const BLPoint& c : curve) {
                nearest = std::min(nearest, std::hypot(p.x - c.x, p.y - c.y));
            }
            worst = std::max(worst, nearest);
        }
        return worst;
    }
}

TEST(SketchFitCurve, AStraightStrokeNeedsTwoPoints) {
    std::vector<BLPoint> points;
    for (int i = 0; i <= 60; ++i) {
        points.push_back(BLPoint(i * 2.0, 10.0 + 0.3 * ((i * 7) % 5 - 2)));   // a line with a little jitter
    }
    std::vector<PenAnchor> anchors = fitCurve(points, 2.0);
    EXPECT_EQ(anchors.size(), 2u);
    EXPECT_LE(deviation(points, anchors), 2.0);
}

TEST(SketchFitCurve, ASmoothArcNeedsOneOrTwoSegmentsAndStaysClose) {
    std::vector<BLPoint> points;
    for (int i = 0; i <= 100; ++i) {
        double a = 3.14159265358979 * i / 100.0;   // a half circle of radius 80
        points.push_back(BLPoint(100.0 + 80.0 * std::cos(a), 100.0 + 80.0 * std::sin(a)));
    }
    std::vector<PenAnchor> anchors = fitCurve(points, 2.0);
    EXPECT_LE(anchors.size(), 4u);
    EXPECT_GE(anchors.size(), 2u);
    EXPECT_LE(deviation(points, anchors), 2.5);
    EXPECT_NEAR(anchors.front().point.x, 180.0, 1e-9);   // the ends are the first and last sample
    EXPECT_NEAR(anchors.back().point.x, 20.0, 1e-9);
    EXPECT_TRUE(anchors.front().hasOut && anchors.back().hasIn);
}

TEST(SketchFitCurve, AWavyLineKeepsOnlyAFewPoints) {
    std::vector<BLPoint> points;
    for (int i = 0; i <= 200; ++i) {
        points.push_back(BLPoint(i * 2.0, 100.0 + 40.0 * std::sin(i * 2.0 / 60.0)));   // about 1.3 periods
    }
    std::vector<PenAnchor> anchors = fitCurve(points, 2.0);
    EXPECT_LE(anchors.size(), 8u);
    EXPECT_LE(deviation(points, anchors), 2.5);
}

TEST(SketchFitCurve, ASharpCornerStaysSharp) {
    std::vector<BLPoint> points;
    for (int i = 0; i <= 40; ++i) { points.push_back(BLPoint(i * 3.0, 0.0)); }          // right...
    for (int i = 1; i <= 40; ++i) { points.push_back(BLPoint(120.0, i * 3.0)); }        // ...then down: a 90 degree corner
    std::vector<PenAnchor> anchors = fitCurve(points, 2.0);
    EXPECT_EQ(anchors.size(), 3u);   // start, the corner, end
    EXPECT_NEAR(anchors[1].point.x, 120.0, 4.0);
    EXPECT_NEAR(anchors[1].point.y, 0.0, 4.0);
    EXPECT_LE(deviation(points, anchors), 2.5);
}

TEST(SketchFitCurve, DegenerateInput) {
    EXPECT_TRUE(fitCurve({}, 2.0).empty());
    EXPECT_EQ(fitCurve({BLPoint(1, 1)}, 2.0).size(), 1u);
    EXPECT_EQ(fitCurve({BLPoint(1, 1), BLPoint(1, 1), BLPoint(1, 1)}, 2.0).size(), 1u);   // repeats collapse
    EXPECT_EQ(fitCurve({BLPoint(0, 0), BLPoint(5, 5)}, 2.0).size(), 2u);
}

namespace {
    // The farthest any point of the fitted curve is from the drawn stroke (the stroke sampled densely enough that the nearest sample will do).
    double overshoot(const std::vector<BLPoint>& stroke, const std::vector<PenAnchor>& anchors) {
        double worst = 0.0;
        for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
            BLPoint p0 = anchors[i].point, p3 = anchors[i + 1].point;
            BLPoint p1 = anchors[i].hasOut ? anchors[i].out : p0, p2 = anchors[i + 1].hasIn ? anchors[i + 1].in : p3;
            for (int k = 0; k <= 100; ++k) {
                double t = k / 100.0, s = 1.0 - t;
                BLPoint c(s * s * s * p0.x + 3 * s * s * t * p1.x + 3 * s * t * t * p2.x + t * t * t * p3.x,
                    s * s * s * p0.y + 3 * s * s * t * p1.y + 3 * s * t * t * p2.y + t * t * t * p3.y);
                double nearest = 1e18;
                for (const BLPoint& p : stroke) {
                    nearest = std::min(nearest, std::hypot(p.x - c.x, p.y - c.y));
                }
                worst = std::max(worst, nearest);
            }
        }
        return worst;
    }
}

TEST(SketchFitCurve, AnAcuteShakyCornerHasNoHook) {
    // Down and to the left, then a sharp turn back toward the right (a turn of over 100 degrees), drawn with a shaky hand.
    std::vector<BLPoint> stroke;
    for (int i = 0; i <= 60; ++i) {
        double t = i / 60.0;
        stroke.push_back(BLPoint(100.0 - 60.0 * t + 0.8 * ((i * 7) % 5 - 2), 0.0 + 120.0 * t + 0.8 * ((i * 3) % 4 - 1.5)));
    }
    for (int i = 1; i <= 60; ++i) {
        double t = i / 60.0;
        stroke.push_back(BLPoint(40.0 + 200.0 * t + 0.8 * ((i * 5) % 5 - 2), 120.0 + 25.0 * t + 0.8 * ((i * 3) % 4 - 1.5)));
    }
    std::vector<PenAnchor> anchors = fitCurve(smoothPolyline(stroke, 2), 2.5);
    EXPECT_LE(overshoot(stroke, anchors), 3.5) << "the fitted curve strays from the stroke (a hook at the corner)";
    EXPECT_LE(deviation(stroke, anchors), 4.0);
    // The corner is one anchor, at the tip of the turn.
    bool atTip = false;
    for (const PenAnchor& a : anchors) {
        atTip = atTip || (std::fabs(a.point.x - 40.0) < 5.0 && std::fabs(a.point.y - 120.0) < 5.0);
    }
    EXPECT_TRUE(atTip);
    EXPECT_LE(anchors.size(), 6u);
}

TEST(SketchFitCurve, HandlesNeverRunLongerThanTheirChord) {
    std::vector<BLPoint> stroke;
    for (int i = 0; i <= 80; ++i) {
        double a = 3.14159265358979 * 1.8 * i / 80.0;   // most of a circle
        stroke.push_back(BLPoint(100.0 + 50.0 * std::cos(a), 100.0 + 50.0 * std::sin(a)));
    }
    std::vector<PenAnchor> anchors = fitCurve(stroke, 1.5);
    for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
        double chord = std::hypot(anchors[i + 1].point.x - anchors[i].point.x, anchors[i + 1].point.y - anchors[i].point.y);
        EXPECT_LE(std::hypot(anchors[i].out.x - anchors[i].point.x, anchors[i].out.y - anchors[i].point.y), chord * 1.0001);
        EXPECT_LE(std::hypot(anchors[i + 1].in.x - anchors[i + 1].point.x, anchors[i + 1].in.y - anchors[i + 1].point.y), chord * 1.0001);
    }
    EXPECT_LE(overshoot(stroke, anchors), 2.5);
}

// ---- Text objects ----

TEST(SketchText, OutlineSitsOnTheBaselineAndGrowsWithTheWordsAndSize) {
    std::unique_ptr<SketchObject> text(SketchObject::makeText("Hi", "", 24.0, BLPoint(100.0, 50.0)));
    ASSERT_TRUE(text->isText());
    BLBox box = text->geometryBounds();
    EXPECT_GE(box.x0, 99.0);
    EXPECT_LE(box.y1, 50.5) << "H and i stand on the baseline";
    EXPECT_LT(box.y0, 50.0 - 10.0) << "the capitals rise well above it";
    double width = box.x1 - box.x0;
    SketchObject::Prototype::setText(*text, "Hi there");
    EXPECT_GT(text->geometryBounds().x1 - text->geometryBounds().x0, width * 2.0);
    SketchObject::Prototype::setText(*text, "Hi\nthere");
    EXPECT_GT(text->geometryBounds().y1, 50.0 + 10.0) << "a second line sits below the first";
    SketchObject::Prototype::setFontSize(*text, 48.0);
    EXPECT_GT(text->geometryBounds().y1 - text->geometryBounds().y0, 40.0);
}

TEST(SketchText, MovingKeepsLaterEditsWhereTheTextWas) {
    SketchDocument doc;
    SketchObject& text = doc.add(SketchObject::makeText("Hi", "", 24.0, BLPoint(100.0, 50.0)));
    std::size_t id = text.id();
    doc.transformObjects({id}, BLMatrix2D::make_translation(30.0, 20.0), "Move");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 130.0, 3.0);
    setWords(doc, id, "Hi!");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 130.0, 3.0) << "editing the words does not send the text back";
    EXPECT_LE(doc.findById(id)->geometryBounds().y1, 70.5);
    setWords(doc, id, "Hi");
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 100.0, 3.0);
    setWords(doc, id, "Hi!");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 100.0, 3.0) << "after undoing the move the placement is the old one too";
    EXPECT_LE(doc.findById(id)->geometryBounds().y1, 50.5);
}

TEST(SketchSvg, TextKeepsItsWordsFontAndPlacement) {
    SketchDocument doc;
    SketchObject& plain = doc.add(SketchObject::makeText("Line one\nLine \"two\" & <more>", "", 30.0, BLPoint(40.0, 80.0)));
    plain.setFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    placeInDocument(doc, plain, BLMatrix2D::make_scaling(1.5, 1.5));
    SketchObject& styled = doc.add(SketchObject::makeText("Fancy", "", 20.0, BLPoint(10.0, 200.0)));
    AppearanceItem extra = AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::gray(0.0f)), 2.0);
    std::vector<AppearanceItem> items = styled.appearance();
    items.push_back(extra);
    styled.setAppearance(items);

    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    ASSERT_EQ(copy.objectCount(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const SketchObject& a = doc.object(i);
        SketchObject& b = copy.object(i);
        ASSERT_TRUE(b.isText()) << i;
        EXPECT_EQ(b.text(), a.text());
        EXPECT_DOUBLE_EQ(b.fontSize(), a.fontSize());
        EXPECT_NEAR(b.textMatrix().m00, a.textMatrix().m00, 1e-4);
        EXPECT_NEAR(b.textMatrix().m20, a.textMatrix().m20, 1e-3);
        EXPECT_NEAR(b.geometryBounds().x0, a.geometryBounds().x0, 1e-2);
        EXPECT_NEAR(b.geometryBounds().y1, a.geometryBounds().y1, 1e-2);
        std::string words = b.text();
        setWords(copy, b.id(), words + "!");   // the restored placement puts an edit in the same spot
        EXPECT_NEAR(b.geometryBounds().x0, a.geometryBounds().x0, 1e-2);
    }
}

// ---- gzip and .svgz ----

TEST(SketchGzip, RoundTripsAndRejectsDamagedData) {
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text += "<path d=\"M" + std::to_string(i) + " 0 L10 10\"/>\n";
    }
    std::string packed = gzipCompress(text);
    EXPECT_TRUE(isGzip(packed));
    EXPECT_FALSE(isGzip(text));
    EXPECT_LT(packed.size(), text.size() / 4) << "repetitive text shrinks a lot";
    std::string back;
    ASSERT_TRUE(gzipDecompress(packed, back));
    EXPECT_EQ(back, text);

    std::string empty;
    ASSERT_TRUE(gzipDecompress(gzipCompress(""), empty));
    EXPECT_TRUE(empty.empty());

    std::string cut = packed.substr(0, packed.size() / 2);
    EXPECT_FALSE(gzipDecompress(cut, back)) << "a cut-short stream is refused";
    std::string damaged = packed;
    damaged[damaged.size() / 2] = char(damaged[damaged.size() / 2] ^ 0x55);
    EXPECT_FALSE(gzipDecompress(damaged, back));
    EXPECT_FALSE(gzipDecompress("plain text, not gzip", back));
}

TEST(SketchSvg, SvgzFilesSaveAndLoadAndShrink) {
    SketchDocument doc;
    for (int i = 0; i < 200; ++i) {
        doc.add(SketchObject::makeRect(i, i * 2, 30, 20)).setName("Box " + std::to_string(i));
    }
    doc.add(SketchObject::makeText("Fine & dandy", "", 18.0, BLPoint(5.0, 50.0)));
    std::filesystem::path folder = std::filesystem::temp_directory_path();
    std::string plain = (folder / "sketch_test_a.svg").string();
    std::string packed = (folder / "sketch_test_b.SVGZ").string();
    ASSERT_TRUE(doc.save(plain));
    ASSERT_TRUE(doc.save(packed));
    EXPECT_LT(std::filesystem::file_size(packed), std::filesystem::file_size(plain) / 3);

    SketchDocument fromPacked, fromPlain;
    ASSERT_TRUE(fromPacked.load(packed));
    ASSERT_TRUE(fromPlain.load(plain));
    EXPECT_EQ(fromPacked.objectCount(), 201u);
    EXPECT_EQ(svgFromDocument(fromPacked), svgFromDocument(fromPlain));
    EXPECT_TRUE(fromPacked.object(200).isText());

    // A gzip stream is recognised by its bytes, whatever the file is called.
    std::string renamed = (folder / "sketch_test_c.svg").string();
    std::filesystem::copy_file(packed, renamed, std::filesystem::copy_options::overwrite_existing);
    SketchDocument fromRenamed;
    EXPECT_TRUE(fromRenamed.load(renamed));
    std::filesystem::remove(plain);
    std::filesystem::remove(packed);
    std::filesystem::remove(renamed);
}

// ---- how long a big document takes (SKETCH_PERF_SHAPES = shape count, default 5000) ----

TEST(SketchPerf, LargeDocumentSaveAndLoadTimes) {
    std::size_t count = 5000;
    if (const char* text = std::getenv("SKETCH_PERF_SHAPES")) {
        count = std::max<std::size_t>(1, std::strtoull(text, nullptr, 10));
    }
    using Clock = std::chrono::steady_clock;
    auto since = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };

    auto start = Clock::now();
    SketchDocument doc;
    doc.setPageSize(20000, 20000);
    for (std::size_t i = 0; i < count; ++i) {
        double x = double(i % 500) * 36.0, y = double(i / 500) * 36.0;
        SketchObject* object = nullptr;
        switch (i % 5) {
            case 0: object = SketchObject::makeRect(x, y, 30, 30); break;
            case 1: object = SketchObject::makeEllipse(x + 15, y + 15, 15, 12); break;
            case 2: object = SketchObject::makePolygon({BLPoint(x, y), BLPoint(x + 30, y + 5), BLPoint(x + 15, y + 30)}); break;
            case 3: {
                BLPath path;
                path.move_to(x, y);
                path.cubic_to(x + 10, y - 10, x + 20, y + 40, x + 30, y + 30);
                object = SketchObject::makePath(path);
                break;
            }
            default: object = SketchObject::makeLine(x, y, x + 30, y + 30); break;
        }
        SketchObject& added = doc.add(object);
        added.setFill(SketchPaint::solid(SketchColor::gray(float(i % 7) / 7.0f)));
    }
    double build = since(start);

    start = Clock::now();
    std::string svg = svgFromDocument(doc);
    double write = since(start);
    start = Clock::now();
    std::string packed = gzipCompress(svg);
    double compress = since(start);
    start = Clock::now();
    std::string inflated;
    ASSERT_TRUE(gzipDecompress(packed, inflated));
    double decompress = since(start);
    start = Clock::now();
    SketchDocument copy;
    ASSERT_TRUE(documentFromSvg(svg, copy));
    double parse = since(start);

    // The same document as .skb, raw and gzipped.
    start = Clock::now();
    std::string raw = binaryFromDocument(doc, false);
    double binWrite = since(start);
    start = Clock::now();
    std::string binPacked = binaryFromDocument(doc, true);
    double binPackedWrite = since(start);
    SketchDocument fromRaw, fromPacked;
    start = Clock::now();
    ASSERT_TRUE(documentFromBinary(raw, fromRaw));
    double binRead = since(start);
    start = Clock::now();
    ASSERT_TRUE(documentFromBinary(binPacked, fromPacked));
    double binPackedRead = since(start);
    EXPECT_EQ(fromRaw.objectCount(), count);
    EXPECT_EQ(fromPacked.objectCount(), count);
    std::printf("PERF %zu shapes as .skb: raw %.1f MB write %.0f ms read %.0f ms (%.2f us/shape) | gzipped %.1f MB write %.0f ms read %.0f ms\n",
        count, double(raw.size()) / 1048576.0, binWrite, binRead, binRead * 1000.0 / double(count), double(binPacked.size()) / 1048576.0,
        binPackedWrite, binPackedRead);

    EXPECT_EQ(copy.objectCount(), count);
    EXPECT_EQ(inflated, svg);
    EXPECT_LT(packed.size(), svg.size());
    std::printf("PERF %zu shapes: build %.0f ms | svg %.1f MB write %.0f ms | gzip %.1f MB (%.1fx) %.0f ms, inflate %.0f ms | read+parse %.0f ms (%.2f us/shape)\n",
        count, build, double(svg.size()) / 1048576.0, write, double(packed.size()) / 1048576.0, double(svg.size()) / double(packed.size()),
        compress, decompress, parse, parse * 1000.0 / double(count));
}

// A real, large drawing: examples/peacockspider.svg (skipped when the file is not there).
TEST(SketchPerf, PeacockSpiderExample) {
    std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "examples" / "peacockspider.svg";
    if (!std::filesystem::exists(file)) {
        GTEST_SKIP() << file.string() << " not found";
    }
    using Clock = std::chrono::steady_clock;
    auto since = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    auto start = Clock::now();
    SketchDocument doc;
    bool loaded = doc.load(file.string());
    double load = since(start);
    ASSERT_TRUE(loaded);
    start = Clock::now();
    std::string svg = svgFromDocument(doc);
    double write = since(start);
    std::string packed = gzipCompress(svg);
    std::printf("PEACOCK %.1f MB file: %zu objects, load %.0f ms, rewrite as sketch SVG %.1f MB in %.0f ms (gzip %.1f MB)\n",
        double(std::filesystem::file_size(file)) / 1048576.0, doc.objectCount(), load, double(svg.size()) / 1048576.0, write,
        double(packed.size()) / 1048576.0);
    EXPECT_GT(doc.objectCount(), 100u);
    std::string raw = binaryFromDocument(doc, false), binPacked = binaryFromDocument(doc, true);
    SketchDocument back;
    start = Clock::now();
    ASSERT_TRUE(documentFromBinary(binPacked, back));
    std::printf("PEACOCK as .skb: raw %.1f MB, gzipped %.1f MB, read %.0f ms\n", double(raw.size()) / 1048576.0, double(binPacked.size()) / 1048576.0, since(start));
    EXPECT_EQ(back.objectCount(), doc.objectCount());
}

// ---- the binary format (.skb) ----

namespace {
    // "" when equal, else a short description of the first place two long strings differ (a failed EXPECT_EQ would print megabytes).
    std::string firstDifference(const std::string& a, const std::string& b) {
        std::size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) {
            ++i;
        }
        if (i == a.size() && i == b.size()) {
            return std::string();
        }
        auto around = [i](const std::string& s) { return s.substr(i > 60 ? i - 60 : 0, 140); };
        return "differ at byte " + std::to_string(i) + ":\n  first : " + around(a) + "\n  second: " + around(b);
    }

    // Like firstDifference, but a number may differ by `tolerance`: an outline rebuilt from a node's float curve is the saved one to within a float step.
    std::string firstDifferenceNear(const std::string& a, const std::string& b, double tolerance) {
        auto numeric = [](char c) { return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == '+'; };
        std::size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) {
                ++i;
                ++j;
                continue;
            }
            std::size_t ei = i, ej = j;
            while (ei < a.size() && numeric(a[ei])) { ++ei; }
            while (ej < b.size() && numeric(b[ej])) { ++ej; }
            std::size_t si = i, sj = j;
            while (si > 0 && numeric(a[si - 1])) { --si; }
            while (sj > 0 && numeric(b[sj - 1])) { --sj; }
            const double x = std::atof(a.substr(si, ei - si).c_str()), y = std::atof(b.substr(sj, ej - sj).c_str());
            if ((ei == i && ej == j) || std::fabs(x - y) > tolerance * (1.0 + std::fabs(x))) {
                return "differ at byte " + std::to_string(i) + ":\n  first : " + a.substr(i > 60 ? i - 60 : 0, 140) + "\n  second: " + b.substr(j > 60 ? j - 60 : 0, 140);
            }
            i = ei;
            j = ej;
        }
        return i == a.size() && j == b.size() ? std::string() : "one ends early, at byte " + std::to_string(i);
    }

    // The SVG without its embedded graph, so only the drawing is compared (the graph text carries the node ids and the order they were made in).
    std::string withoutGraph(std::string svg) {
        const std::size_t start = svg.find("<metadata>");
        const std::size_t end = svg.find("</metadata>");
        if (start != std::string::npos && end != std::string::npos) {
            svg.erase(start, end + 11 - start);
        }
        // The ids of the nodes depend on how many the document had when each object was added.
        for (std::size_t at = svg.find(" data-graph-node=\""); at != std::string::npos; at = svg.find(" data-graph-node=\"", at)) {
            svg.erase(at, svg.find('"', at + 18) + 1 - at);
        }
        return svg;
    }

    // A document that uses most of what the format carries.
    void fillRichDocument(SketchDocument& doc) {
        doc.setPageSize(500, 400);
        doc.setPpi(144);
        doc.setPageBackground(SketchColor::rgb(0.9f, 0.95f, 1.0f));
        doc.setColorMode(ColorSpace::CMYK);
        SketchObject& card = doc.add(SketchObject::makeRoundRect(10, 20, 100, 60, 8));
        card.setName("Card & <co>");
        card.setOpacity(0.5);
        card.setBlendMode(BlendMode::Multiply);
        card.setLocked(true);
        SketchGradient gradient = SketchGradient::fromColor(SketchColor::cmyk(0.2f, 0.4f, 0.6f, 0.1f));
        gradient.setAxis(card.geometryBounds(), BLPoint(20, 30), BLPoint(90, 70));
        card.setFill(SketchPaint::linear(gradient));
        StrokeOptions options;
        options.cap = StrokeCap::Round;
        options.dashes = {4.0, 2.0};
        options.widths = {{0.0, 0.5, 0.5}, {1.0, 1.5, 1.0}};
        card.setStroke(SketchPaint::solid(SketchColor::gray(0.2f, 0.8f)));
        card.setStrokeWidth(3.0);
        card.setStrokeOptions(options);
        std::vector<AppearanceItem> items = card.appearance();
        AppearanceItem shadow = AppearanceItem::makeEffect(EffectKind::DropShadow);
        shadow.offsetX = 7.5;
        items.push_back(shadow);
        items.push_back(AppearanceItem::makeFill(SketchPaint::radial(SketchGradient::fromColor(SketchColor::rgb(1, 0, 0)))));
        card.setAppearance(items);
        card.setActiveIndices(0, 1, 2);
        placeInDocument(doc, card, BLMatrix2D::make_rotation(0.3));
        card.setFrameAngle(0.3);

        SketchObject& a = doc.add(SketchObject::makeEllipse(200, 100, 40, 30));
        SketchObject& b = doc.add(SketchObject::makeLine(0, 0, 50, 60));
        std::size_t outer = doc.newGroupId(), inner = doc.newGroupId();
        a.setGroups({outer, inner});
        b.setGroups({outer});
        GroupProps props;
        props.opacity = 0.4;
        props.blendMode = BlendMode::Screen;
        doc.setGroupProps(outer, props);
        BLPath curve;
        curve.move_to(5, 5);
        curve.cubic_to(15, 85.0 / 3.0, 30, 85.0 / 3.0, 50, 5);   // a node's curve holds cubics only, so a quadratic would come back as this
        curve.cubic_to(60, 0, 70, 30, 90, 20);
        curve.line_to(5, 5);   // a node's closed curve holds its closing edge as a segment
        curve.close();
        doc.add(SketchObject::makePath(curve)).setVisible(false);
        doc.add(SketchObject::makeText("Fine\nand \"dandy\" \xC3\xA9", "", 22.0, BLPoint(40, 300)));
        doc.setGraphicStyles({GraphicStyle{"Mine", {AppearanceItem::makeFill(SketchPaint::solid(SketchColor::gray(0.5f))),
            AppearanceItem::makeStroke(SketchPaint::none(), 2.0)}, 0.7, BlendMode::Overlay}});
    }
}

TEST(SketchBinary, ADocumentRoundTripsWithNothingLost) {
    SketchDocument doc;
    fillRichDocument(doc);
    for (bool compress : {true, false}) {
        std::string file = binaryFromDocument(doc, compress);
        ASSERT_TRUE(isBinaryDocument(file));
        SketchDocument copy;
        copy.add(SketchObject::makeRect(0, 0, 1, 1));   // replaced by the load
        ASSERT_TRUE(documentFromBinary(file, copy)) << "compress " << compress;
        ASSERT_EQ(copy.objectCount(), doc.objectCount());
        // The SVG writer reads every part of the model, so equal SVG means equal documents.
        EXPECT_EQ(firstDifferenceNear(withoutGraph(svgFromDocument(copy)), withoutGraph(svgFromDocument(doc)), 1e-4), "");
        EXPECT_TRUE(copy.object(4).isText());
        EXPECT_EQ(copy.object(4).text(), doc.object(4).text());
        EXPECT_EQ(copy.object(0).activeEffectIndex(), 2u);
        EXPECT_EQ(copy.object(1).groups().size(), 2u);
        EXPECT_EQ(copy.object(1).groups()[0], copy.object(2).groups()[0]) << "members of a group share it";
        EXPECT_DOUBLE_EQ(copy.groupProps(copy.object(1).groups()[0]).opacity, 0.4);
        EXPECT_EQ(copy.graphicStyles().size(), 1u);
        EXPECT_EQ(copy.colorMode(), ColorSpace::CMYK);
        EXPECT_FALSE(copy.undoStack().canUndo());
        // Geometry comes back to within a 32-bit float step (the outlines are stored as float steps).
        BLBox x = doc.object(0).geometryBounds(), y = copy.object(0).geometryBounds();
        EXPECT_NEAR(x.x0, y.x0, 1e-4);
        EXPECT_NEAR(x.y1, y.y1, 1e-4);
    }
}

TEST(SketchBinary, SharedAppearancesAreWrittenOnce) {
    SketchDocument one, many;
    one.add(SketchObject::makeRect(0, 0, 10, 10));
    for (int i = 0; i < 1000; ++i) {
        many.add(SketchObject::makeRect(i, 0, 10, 10));
    }
    std::size_t base = binaryFromDocument(one, false).size();
    std::size_t big = binaryFromDocument(many, false).size();
    EXPECT_LT(big, base + 1000 * 700) << "each extra rectangle costs its nodes (the outline lives there) and a few bytes, not another appearance";
}

// DG-03 S5: the file stores the graph and each object's shape node; an outline the node holds is not saved but built from the node on load.
TEST(SketchBinary, TheFileStoresTheGraphAndObjectsAreBuiltFromItsNodes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(20, 20, 100, 60)).id();
    const std::size_t b = doc.add(SketchObject::makeEllipse(200, 100, 40, 30)).id();
    doc.transformObjects({a}, SketchDocument::rotationAbout(BLPoint(70, 50), 0.7), "Rotate");
    ASSERT_TRUE(doc.setParent(b, a));
    ASSERT_TRUE(doc.pathHeldByNode(*doc.findById(a)));

    SketchDocument loaded;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 2u);
    EXPECT_EQ(loaded.graphToText(), doc.graphToText()) << "the graph comes back as it was saved";
    EXPECT_EQ(loaded.object(0).graphNode(), doc.object(0).graphNode());
    EXPECT_EQ(loaded.object(1).graphNode(), doc.object(1).graphNode());
    EXPECT_EQ(loaded.object(1).parent(), loaded.object(0).id()) << "the parent link is in the graph";
    EXPECT_TRUE(loaded.pathHeldByNode(loaded.object(0))) << "the object is in step with its node, nothing was rewritten";
    const BLBox x = doc.object(0).geometryBounds(), y = loaded.object(0).geometryBounds();
    EXPECT_NEAR(x.x0, y.x0, 1e-3);
    EXPECT_NEAR(x.y0, y.y0, 1e-3);
    EXPECT_NEAR(x.x1, y.x1, 1e-3);
    EXPECT_NEAR(x.y1, y.y1, 1e-3);
}

TEST(SketchBinary, AnOlderFileWithoutAGraphStillLoads) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(20, 20, 100, 60));
    doc.add(SketchObject::makeText("Hi", "", 24.0, BLPoint(10, 90)));
    SketchDocument loaded;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true, 3), loaded));
    ASSERT_EQ(loaded.objectCount(), 2u);
    EXPECT_NEAR(loaded.object(0).geometryBounds().x1, 120.0, 1e-3);
    EXPECT_TRUE(loaded.object(1).isText());
    EXPECT_NE(loaded.object(0).graphNode(), 0u) << "the nodes are made from the objects, as before";
}

TEST(SketchBinary, AGraphThatCannotBeReadRefusesTheFileAndLeavesTheDocument) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(20, 20, 100, 60));
    std::string file = binaryFromDocument(doc, false);
    const std::size_t at = file.find("jitnodes");
    ASSERT_NE(at, std::string::npos) << "the graph text is in the file";
    file[at] = '#';
    SketchDocument keep;
    keep.add(SketchObject::makeRect(1, 2, 3, 4));
    EXPECT_FALSE(documentFromBinary(file, keep));
    EXPECT_EQ(keep.objectCount(), 1u);
}

TEST(SketchBinary, DamagedFilesAreRefusedWithoutHarm) {
    SketchDocument doc;
    fillRichDocument(doc);
    std::string file = binaryFromDocument(doc, false);
    SketchDocument keep;
    keep.add(SketchObject::makeRect(1, 2, 3, 4));
    EXPECT_FALSE(documentFromBinary("", keep));
    EXPECT_FALSE(documentFromBinary("SKB1", keep));
    EXPECT_FALSE(documentFromBinary("not a sketch file at all", keep));
    // Every cut-short prefix is refused (and the document stays as it was).
    for (std::size_t length = 0; length < file.size(); length += 7) {
        EXPECT_FALSE(documentFromBinary(file.substr(0, length), keep)) << "prefix of " << length;
    }
    EXPECT_EQ(keep.objectCount(), 1u);
    // Flipped bytes anywhere must never crash; they may load (a changed number) or be refused.
    for (std::size_t at = 8; at < file.size(); at += 3) {
        std::string damaged = file;
        damaged[at] = char(damaged[at] ^ 0xA5);
        SketchDocument target;
        documentFromBinary(damaged, target);
    }
    // A newer version than this reader knows is refused.
    std::string future = file;
    future[4] = 9;
    EXPECT_FALSE(documentFromBinary(future, keep));
    // An unknown chunk is skipped.
    std::string extra = file;
    std::string chunk = "ZZZZ";
    std::uint64_t size = 3;
    chunk.append(reinterpret_cast<const char*>(&size), 8);
    chunk += "abc";
    extra.insert(8, chunk);
    SketchDocument skipped;
    EXPECT_TRUE(documentFromBinary(extra, skipped));
    EXPECT_EQ(skipped.objectCount(), doc.objectCount());
}

TEST(SketchBinary, FilesSaveAndLoadByExtensionAndByContent) {
    SketchDocument doc;
    fillRichDocument(doc);
    std::filesystem::path folder = std::filesystem::temp_directory_path();
    std::string path = (folder / "sketch_test_d.skb").string();
    ASSERT_TRUE(doc.save(path));
    std::ifstream in(path, std::ios::binary);
    char magic[4] = {};
    in.read(magic, 4);
    EXPECT_EQ(std::string(magic, 4), "SKB1");
    in.close();
    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(firstDifferenceNear(withoutGraph(svgFromDocument(loaded)), withoutGraph(svgFromDocument(doc)), 1e-4), "");
    std::string renamed = (folder / "sketch_test_e.svg").string();
    std::filesystem::copy_file(path, renamed, std::filesystem::copy_options::overwrite_existing);
    SketchDocument fromRenamed;
    EXPECT_TRUE(fromRenamed.load(renamed)) << "a binary file is recognised by its bytes";
    std::filesystem::remove(path);
    std::filesystem::remove(renamed);
}

// ---- Inner Shadow, Offset Path, Roughen ----

namespace {
    // The bounds of what an object paints, found by painting it into an image and looking for non-transparent pixels.
    bool paintedBox(const SketchObject& object, int size, BLBox& box) {
        BLImage image(size, size, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        int x0 = size, y0 = size, x1 = -1, y1 = -1;
        for (int y = 0; y < size; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
            for (int x = 0; x < size; ++x) {
                if ((row[x] >> 24) > 8) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
            }
        }
        box = BLBox(x0, y0, x1 + 1, y1 + 1);
        return x1 >= 0;
    }

    uint32_t pixelAt(const SketchObject& object, int size, int px, int py) {
        BLImage image(size, size, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        object.paint(ctx);
        ctx.end();
        BLImageData data;
        image.get_data(&data);
        return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data.pixel_data) + std::ptrdiff_t(py) * data.stride)[px];
    }

    void addEffect(SketchObject& object, const AppearanceItem& effect) {
        std::vector<AppearanceItem> items = object.appearance();
        items.push_back(effect);
        object.setAppearance(items);
    }
}

TEST(SketchEffects, OffsetPathGrowsAndShrinksTheOutline) {
    std::unique_ptr<SketchObject> grown(SketchObject::makeRect(40, 40, 60, 40));
    grown->setStroke(SketchPaint::none());
    AppearanceItem offset = AppearanceItem::makeEffect(EffectKind::OffsetPath);
    offset.radius = 10.0;
    addEffect(*grown, offset);
    BLBox box;
    ASSERT_TRUE(paintedBox(*grown, 200, box));
    EXPECT_NEAR(box.x0, 30.0, 1.5);
    EXPECT_NEAR(box.x1, 110.0, 1.5);
    EXPECT_NEAR(box.y0, 30.0, 1.5);
    EXPECT_NEAR(box.y1, 90.0, 1.5);
    EXPECT_LE(grown->paintBounds().x0, 40.0 - 10.0) << "the paint bounds cover the grown outline";
    EXPECT_NEAR(grown->geometryBounds().x0, 40.0, 1e-9) << "the outline of the object itself is unchanged";

    std::unique_ptr<SketchObject> shrunk(SketchObject::makeRect(40, 40, 60, 40));
    shrunk->setStroke(SketchPaint::none());
    offset.radius = -10.0;
    addEffect(*shrunk, offset);
    ASSERT_TRUE(paintedBox(*shrunk, 200, box));
    EXPECT_NEAR(box.x0, 50.0, 1.5);
    EXPECT_NEAR(box.x1, 90.0, 1.5);
    EXPECT_NEAR(box.y0, 50.0, 1.5);
    EXPECT_NEAR(box.y1, 70.0, 1.5);

    // Orientation does not matter: the same rectangle drawn the other way round grows the same way.
    std::unique_ptr<SketchObject> reversed(SketchObject::makePolygon({BLPoint(40, 40), BLPoint(40, 80), BLPoint(100, 80), BLPoint(100, 40)}));
    reversed->setStroke(SketchPaint::none());
    offset.radius = 10.0;
    addEffect(*reversed, offset);
    ASSERT_TRUE(paintedBox(*reversed, 200, box));
    EXPECT_NEAR(box.x0, 30.0, 1.5);
    EXPECT_NEAR(box.x1, 110.0, 1.5);
}

TEST(SketchEffects, RoughenIsRepeatableAndStaysNearTheOutline) {
    std::unique_ptr<SketchObject> plain(SketchObject::makeRect(40, 40, 100, 100));
    plain->setStroke(SketchPaint::none());
    std::unique_ptr<SketchObject> rough(plain->clone());
    AppearanceItem effect = AppearanceItem::makeEffect(EffectKind::Roughen);
    effect.radius = 4.0;
    effect.offsetX = 10.0;
    addEffect(*rough, effect);
    BLBox a, b;
    ASSERT_TRUE(paintedBox(*plain, 200, a));
    ASSERT_TRUE(paintedBox(*rough, 200, b));
    EXPECT_NE(a.x0, b.x0) << "the outline moved";
    EXPECT_GE(b.x0, a.x0 - 5.0) << "by no more than the size";
    EXPECT_LE(b.x1, a.x1 + 5.0);
    // The same seed gives the same shape; another seed a different one.
    std::unique_ptr<SketchObject> again(rough->clone());
    EXPECT_EQ(pixelAt(*rough, 200, 90, 38), pixelAt(*again, 200, 90, 38));
    std::vector<AppearanceItem> items = again->appearance();
    items.back().offsetY = 7.0;
    again->setAppearance(items);
    bool differs = false;
    for (int x = 40; x < 140 && !differs; x += 3) {
        differs = pixelAt(*rough, 200, x, 40) != pixelAt(*again, 200, x, 40);
    }
    EXPECT_TRUE(differs);
    // Moving the object moves the wobble with it, unchanged.
    std::unique_ptr<SketchObject> moved(rough->clone());
    transformPrototype(*moved, BLMatrix2D::make_translation(20, 0));
    EXPECT_EQ(pixelAt(*rough, 200, 90, 38), pixelAt(*moved, 200, 110, 38));
    // Size 0 does nothing.
    std::vector<AppearanceItem> zero = rough->appearance();
    zero.back().radius = 0.0;
    rough->setAppearance(zero);
    BLBox d;
    ASSERT_TRUE(paintedBox(*rough, 200, d));
    EXPECT_EQ(d.x0, a.x0);
}

TEST(SketchEffects, InnerShadowStaysInsideAndDarkensTheEdgesItIsCastFrom) {
    std::unique_ptr<SketchObject> shape(SketchObject::makeRect(50, 50, 100, 100));
    shape->setStroke(SketchPaint::none());
    shape->setFill(SketchPaint::solid(SketchColor::rgb(1, 1, 1)));
    AppearanceItem shadow = AppearanceItem::makeEffect(EffectKind::InnerShadow);
    shadow.offsetX = 12.0;
    shadow.offsetY = 12.0;
    shadow.softness = 4.0;
    shadow.opacity = 1.0;
    shadow.blendMode = BlendMode::Normal;
    addEffect(*shape, shadow);
    BLBox box;
    ASSERT_TRUE(paintedBox(*shape, 200, box));
    EXPECT_NEAR(box.x0, 50.0, 1.5) << "nothing is cast outside the shape";
    EXPECT_NEAR(box.x1, 150.0, 1.5);
    EXPECT_NEAR(shape->paintBounds().x0, 50.0, 1e-9) << "an inner shadow has no reach";
    auto green = [&](int x, int y) { return (pixelAt(*shape, 200, x, y) >> 8) & 255u; };
    EXPECT_LT(green(54, 54), 110u) << "next to the top-left edges, where the offset pushes the shade in, it is dark";
    EXPECT_GT(green(120, 120), 230u) << "far from them the shape stays white";
    EXPECT_GT(green(146, 146), 230u) << "and the edges away from the light stay clear";
}

TEST(SketchEffects, NewEffectsSurviveSvgAndBinaryAndHaveNames) {
    SketchDocument doc;
    SketchObject& object = doc.add(SketchObject::makeRect(0, 0, 50, 50));
    std::vector<AppearanceItem> items = object.appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::InnerShadow));
    AppearanceItem offset = AppearanceItem::makeEffect(EffectKind::OffsetPath);
    offset.radius = -3.5;
    items.push_back(offset);
    AppearanceItem rough = AppearanceItem::makeEffect(EffectKind::Roughen);
    rough.radius = 1.25;
    rough.offsetX = 6.0;
    rough.offsetY = 9.0;
    items.push_back(rough);
    object.setAppearance(items);
    EXPECT_STREQ(effectName(EffectKind::InnerShadow), "Inner Shadow");
    EXPECT_STREQ(effectName(EffectKind::OffsetPath), "Offset Path");
    EXPECT_STREQ(effectName(EffectKind::Roughen), "Roughen");
    EXPECT_TRUE(isGeometryEffect(EffectKind::Roughen));
    EXPECT_FALSE(isGeometryEffect(EffectKind::InnerShadow));
    EXPECT_TRUE(isTintedEffect(EffectKind::InnerShadow));

    SketchDocument fromSvg, fromBinary;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), fromBinary));
    for (SketchDocument* copy : {&fromSvg, &fromBinary}) {
        const std::vector<AppearanceItem>& back = copy->object(0).appearance();
        ASSERT_EQ(back.size(), 5u);
        EXPECT_EQ(back[2].effect, EffectKind::InnerShadow);
        EXPECT_EQ(back[3].effect, EffectKind::OffsetPath);
        EXPECT_DOUBLE_EQ(back[3].radius, -3.5);
        EXPECT_EQ(back[4].effect, EffectKind::Roughen);
        EXPECT_DOUBLE_EQ(back[4].radius, 1.25);
        EXPECT_DOUBLE_EQ(back[4].offsetX, 6.0);
        EXPECT_DOUBLE_EQ(back[4].offsetY, 9.0);
    }
}

// ---- path operations (cutting tools and Pathfinder) ----

namespace {
    Loop box(double x0, double y0, double x1, double y1) { return Loop{BLPoint(x0, y0), BLPoint(x1, y0), BLPoint(x1, y1), BLPoint(x0, y1)}; }
    // The area a set of outlines fills: outer outlines count positive, holes negative (as combineLoops produces them).
    double filled(const Loops& loops) {
        double total = 0.0;
        for (const Loop& loop : loops) { total += signedArea(loop); }
        return total;
    }
}

TEST(SketchPathOps, BooleanOperationsOnOverlappingRectangles) {
    Loops a{box(0, 0, 100, 100)}, b{box(50, 50, 150, 150)};
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Union)), 10000 + 10000 - 2500, 1e-3);
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Intersect)), 2500, 1e-3);
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Subtract)), 7500, 1e-3);
    EXPECT_NEAR(filled(combineLoops(b, a, BoolOp::Subtract)), 7500, 1e-3);
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Exclude)), 15000, 1e-3);
    // One outline for the union, shaped like two squares joined; the subtraction is an L with six corners.
    EXPECT_EQ(combineLoops(a, b, BoolOp::Union).size(), 1u);
    ASSERT_EQ(combineLoops(a, b, BoolOp::Subtract).size(), 1u);
    EXPECT_EQ(combineLoops(a, b, BoolOp::Subtract)[0].size(), 6u);
    EXPECT_EQ(combineLoops(a, b, BoolOp::Exclude).size(), 2u);
}

TEST(SketchPathOps, HolesDisjointAndTouchingShapes) {
    Loops big{box(0, 0, 100, 100)}, inner{box(30, 30, 70, 70)};
    Loops ring = combineLoops(big, inner, BoolOp::Subtract);
    ASSERT_EQ(ring.size(), 2u);
    EXPECT_NEAR(filled(ring), 10000 - 1600, 1e-3);
    std::vector<Loops> regions = splitIntoRegions(ring);
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_EQ(regions[0].size(), 2u) << "the hole belongs to the outline around it";
    EXPECT_TRUE(pointInLoops(BLPoint(10, 10), ring));
    EXPECT_FALSE(pointInLoops(BLPoint(50, 50), ring));

    Loops apart{box(200, 0, 300, 100)};
    EXPECT_EQ(combineLoops(big, apart, BoolOp::Union).size(), 2u);
    EXPECT_TRUE(combineLoops(big, apart, BoolOp::Intersect).empty());
    EXPECT_NEAR(filled(combineLoops(big, apart, BoolOp::Subtract)), 10000, 1e-3);

    // Shapes that share an edge: the seam disappears.
    Loops next{box(100, 0, 200, 100)};
    Loops joined = combineLoops(big, next, BoolOp::Union);
    ASSERT_EQ(joined.size(), 1u);
    EXPECT_NEAR(filled(joined), 20000, 1e-3);
    EXPECT_EQ(joined[0].size(), 4u);
    EXPECT_TRUE(combineLoops(big, next, BoolOp::Intersect).empty());
    // The same shape twice.
    EXPECT_NEAR(filled(combineLoops(big, big, BoolOp::Union)), 10000, 1e-3);
    EXPECT_NEAR(filled(combineLoops(big, big, BoolOp::Intersect)), 10000, 1e-3);
    EXPECT_TRUE(combineLoops(big, big, BoolOp::Subtract).empty());
    EXPECT_TRUE(combineLoops(big, big, BoolOp::Exclude).empty());
    // An outline that touches another only at a corner.
    Loops corner{box(100, 100, 150, 150)};
    EXPECT_NEAR(filled(combineLoops(big, corner, BoolOp::Union)), 12500, 1e-3);
}

TEST(SketchPathOps, CurvesAreFlattenedAndCombined) {
    BLPath circleA, circleB;
    circleA.add_circle(BLCircle(100, 100, 50));
    circleB.add_circle(BLCircle(150, 100, 50));
    Loops a = flattenToLoops(circleA, 0.01), b = flattenToLoops(circleB, 0.01);
    const double pi = 3.14159265358979;
    EXPECT_NEAR(std::fabs(filled(a)), pi * 2500, 6.0);
    double lens = 2 * 2500 * std::acos(0.5) - 25 * std::sqrt(3.0) * 50;   // overlap of two circles of radius 50 whose centers are 50 apart
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Intersect)), lens, 12.0);
    EXPECT_NEAR(filled(combineLoops(a, b, BoolOp::Union)), 2 * pi * 2500 - lens, 20.0);
    BLPath out = loopsToPath(combineLoops(a, b, BoolOp::Union));
    BLBox bounds;
    out.get_bounding_box(&bounds);
    EXPECT_NEAR(bounds.x0, 50.0, 0.1);
    EXPECT_NEAR(bounds.x1, 200.0, 0.1);
}

TEST(SketchPathOps, AnOutlineThatCrossesItselfIsUnderstood) {
    // A stroke polygon from a path that loops back over itself overlaps itself.
    std::vector<BLPoint> scribble{BLPoint(10, 50), BLPoint(90, 50), BLPoint(90, 60), BLPoint(50, 60), BLPoint(50, 30), BLPoint(50, 90)};
    Loops pen = strokeToLoops(scribble, 8.0);
    ASSERT_FALSE(pen.empty());
    Loops sheet{box(0, 0, 100, 100)};
    Loops left = combineLoops(sheet, pen, BoolOp::Subtract);
    EXPECT_FALSE(pointInLoops(BLPoint(50, 50), left)) << "inside the stroke";
    EXPECT_TRUE(pointInLoops(BLPoint(20, 20), left));
    EXPECT_FALSE(pointInLoops(BLPoint(30, 50), left));
    double removed = 10000 - filled(left);
    EXPECT_GT(removed, 80 * 8 * 0.8);
    EXPECT_LT(removed, 80 * 8 + 40 * 8 + 60 * 8 + 120);
    for (const Loop& loop : left) {
        EXPECT_NE(signedArea(loop), 0.0);
    }
}

TEST(SketchPathOps, AKnifeLineDividesAShapeWithoutGapOrLoss) {
    Loops sheet{box(0, 0, 100, 100)};
    std::vector<Loops> pieces = cutAlongLine(sheet, {BLPoint(50, -20), BLPoint(50, 120)});
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_NEAR(filled(pieces[0]) + filled(pieces[1]), 10000, 1e-3);
    EXPECT_NEAR(filled(pieces[0]), 5000, 1e-3);
    // A slanting, bent cut.
    pieces = cutAlongLine(sheet, {BLPoint(-10, 30), BLPoint(60, 50), BLPoint(110, 20)});
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_NEAR(filled(pieces[0]) + filled(pieces[1]), 10000, 1e-2);
    // A line that misses the shape divides nothing.
    EXPECT_TRUE(cutAlongLine(sheet, {BLPoint(200, -20), BLPoint(200, 120)}).empty());
    // A shape with a hole cut across it comes out as two pieces, one of them carrying half the hole's edge.
    Loops donut = combineLoops(sheet, Loops{box(40, 40, 60, 60)}, BoolOp::Subtract);
    pieces = cutAlongLine(donut, {BLPoint(50, -20), BLPoint(50, 120)});
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_NEAR(filled(pieces[0]) + filled(pieces[1]), 10000 - 400, 1e-2);
    // A cut that stops inside still divides the shape along its extension (documented).
    EXPECT_EQ(cutAlongLine(sheet, {BLPoint(50, -20), BLPoint(50, 60)}).size(), 2u);
}

TEST(SketchPathOps, ClippingALineLeavesTheOutsideParts) {
    Loops hole{box(40, 0, 60, 100)};
    bool touched = false;
    std::vector<std::vector<BLPoint>> parts = clipLineOutside({BLPoint(0, 50), BLPoint(100, 50)}, false, hole, touched);
    EXPECT_TRUE(touched);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_NEAR(parts[0].back().x, 40.0, 1e-9);
    EXPECT_NEAR(parts[1].front().x, 60.0, 1e-9);
    parts = clipLineOutside({BLPoint(0, 200), BLPoint(100, 200)}, false, hole, touched);
    EXPECT_FALSE(touched);
    EXPECT_TRUE(parts.empty());
    // A closed outline cut open once comes back as one open piece (the seam is not a break).
    std::vector<std::vector<BLPoint>> ring = clipLineOutside({BLPoint(0, 0), BLPoint(100, 0), BLPoint(100, 100), BLPoint(0, 100)}, true,
        Loops{box(90, -10, 110, 10)}, touched);
    EXPECT_TRUE(touched);
    ASSERT_EQ(ring.size(), 1u);
    EXPECT_NEAR(ring[0].front().x, 100.0, 1e-9);
    EXPECT_NEAR(ring[0].front().y, 10.0, 1e-9);
    EXPECT_NEAR(ring[0].back().x, 90.0, 1e-9);
    EXPECT_NEAR(ring[0].back().y, 0.0, 1e-9);
}

TEST(SketchPathOps, ScissorsCutAPathKeepingItsCurves) {
    SubPath square;
    square.closed = true;
    for (BLPoint p : {BLPoint(0, 0), BLPoint(100, 0), BLPoint(100, 100), BLPoint(0, 100)}) {
        PenAnchor a;
        a.point = p;
        square.anchors.push_back(a);
    }
    std::size_t segment = 0;
    double t = 0.0, distance = 0.0;
    ASSERT_TRUE(nearestOnSubPath(square, BLPoint(40, 3), segment, t, distance));
    EXPECT_EQ(segment, 0u);
    EXPECT_NEAR(t, 0.4, 1e-9);
    EXPECT_NEAR(distance, 3.0, 1e-9);
    std::vector<SubPath> pieces;
    ASSERT_TRUE(splitSubPath(square, segment, t, pieces));
    ASSERT_EQ(pieces.size(), 1u) << "a closed outline opens into one path";
    EXPECT_FALSE(pieces[0].closed);
    ASSERT_EQ(pieces[0].anchors.size(), 6u);
    EXPECT_NEAR(pieces[0].anchors.front().point.x, 40.0, 1e-9);
    EXPECT_NEAR(pieces[0].anchors.back().point.x, 40.0, 1e-9);
    EXPECT_NEAR(pieces[0].anchors[1].point.x, 100.0, 1e-9);

    // A curve: the two halves still follow the original.
    SubPath arc;
    PenAnchor a0, a1;
    a0.point = BLPoint(0, 0);
    a0.out = BLPoint(0, 60);
    a0.hasOut = true;
    a1.point = BLPoint(100, 0);
    a1.in = BLPoint(100, 60);
    a1.hasIn = true;
    arc.anchors = {a0, a1};
    ASSERT_TRUE(splitSubPath(arc, 0, 0.5, pieces));
    ASSERT_EQ(pieces.size(), 2u);
    BLPoint mid(0.125 * 0 + 0.375 * 0 + 0.375 * 100 + 0.125 * 100, 0.125 * 0 + 0.375 * 60 + 0.375 * 60 + 0.125 * 0);
    EXPECT_NEAR(pieces[0].anchors.back().point.x, mid.x, 1e-9);
    EXPECT_NEAR(pieces[0].anchors.back().point.y, mid.y, 1e-9);
    EXPECT_TRUE(pieces[0].anchors.back().hasIn);
    EXPECT_FALSE(pieces[0].anchors.back().hasOut);
    EXPECT_FALSE(pieces[1].anchors.front().hasIn);
    EXPECT_TRUE(pieces[1].anchors.front().hasOut);
    BLPath whole = subPathsToPath({arc}), first = subPathsToPath({pieces[0]}), second = subPathsToPath({pieces[1]});
    BLBox wb, fb, sb;
    whole.get_bounding_box(&wb);
    first.get_bounding_box(&fb);
    second.get_bounding_box(&sb);
    EXPECT_NEAR(std::min(fb.y0, sb.y0), wb.y0, 1e-6);
    EXPECT_NEAR(std::max(fb.y1, sb.y1), wb.y1, 1e-6);

    // An open path: cutting an end does nothing; a middle anchor splits it in two.
    SubPath line;
    for (BLPoint p : {BLPoint(0, 0), BLPoint(50, 0), BLPoint(100, 0)}) {
        PenAnchor a;
        a.point = p;
        line.anchors.push_back(a);
    }
    EXPECT_FALSE(splitSubPath(line, 0, 0.0, pieces));
    ASSERT_TRUE(splitSubPath(line, 0, 1.0, pieces));
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].anchors.size(), 2u);
    EXPECT_EQ(pieces[1].anchors.size(), 2u);
}

// ---- the document's cutting and combining commands ----

namespace {
    // The area the object's outline encloses (non-zero rule), from its flattened outline.
    double areaOf(const SketchObject& object) {
        Loops flat = flattenToLoops(object.geometry(), 0.01);
        Loops cleaned = combineLoops(flat, Loops{}, BoolOp::Union);
        double total = 0.0;
        for (const Loop& loop : cleaned) { total += signedArea(loop); }
        return total;
    }
}

TEST(SketchCutting, PathfinderCombinesTheSelectionIntoOnePath) {
    SketchDocument doc;
    SketchObject& a = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchObject& b = doc.add(SketchObject::makeRect(50, 50, 100, 100));
    a.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    b.setFill(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    std::size_t idA = a.id(), idB = b.id();
    doc.setSelection({idA, idB});
    ASSERT_TRUE(doc.combineSelection(BoolOp::Union));
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.object(0).type(), SketchObjectType::Path);
    EXPECT_NEAR(areaOf(doc.object(0)), 17500.0, 1.0);
    EXPECT_EQ(doc.object(0).fill().color().toHex(), "#0000ff") << "Unite wears the look of the top object";
    EXPECT_EQ(doc.selection().size(), 1u);
    doc.undoStack().undo();
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(doc.object(0).id(), idA);
    EXPECT_EQ(doc.object(1).id(), idB);
    EXPECT_EQ(doc.selection().size(), 2u) << "undo selects the originals again";
    doc.undoStack().redo();
    EXPECT_EQ(doc.objectCount(), 1u);
    doc.undoStack().undo();

    doc.setSelection({idA, idB});
    ASSERT_TRUE(doc.combineSelection(BoolOp::Subtract));
    EXPECT_NEAR(areaOf(doc.object(0)), 7500.0, 1.0);
    EXPECT_EQ(doc.object(0).fill().color().toHex(), "#ff0000") << "Minus Front keeps the look of the lowest object";
    doc.undoStack().undo();
    doc.setSelection({idA, idB});
    ASSERT_TRUE(doc.combineSelection(BoolOp::Intersect));
    EXPECT_NEAR(areaOf(doc.object(0)), 2500.0, 1.0);
    doc.undoStack().undo();
    doc.setSelection({idA, idB});
    ASSERT_TRUE(doc.combineSelection(BoolOp::Exclude));
    EXPECT_NEAR(areaOf(doc.object(0)), 15000.0, 1.0);
    doc.undoStack().undo();

    // Not enough selected, or nothing left: nothing changes.
    doc.setSelection({idA});
    EXPECT_FALSE(doc.combineSelection(BoolOp::Union));
    SketchObject& distant = doc.add(SketchObject::makeRect(500, 500, 10, 10));
    doc.setSelection({idA, distant.id()});
    EXPECT_FALSE(doc.combineSelection(BoolOp::Intersect));
    EXPECT_EQ(doc.objectCount(), 3u);
}

TEST(SketchCutting, ResultsKeepTheirPlaceInTheStack) {
    SketchDocument doc;
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 100)).id();
    std::size_t b = doc.add(SketchObject::makeRect(300, 300, 10, 10)).id();
    std::size_t c = doc.add(SketchObject::makeRect(50, 50, 100, 100)).id();
    doc.setSelection({a, c});
    ASSERT_TRUE(doc.combineSelection(BoolOp::Union));
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(doc.object(0).id(), b);
    EXPECT_NE(doc.object(1).id(), a) << "the united shape is on top, where the top object was";
    doc.undoStack().undo();
    ASSERT_EQ(doc.objectCount(), 3u);
    EXPECT_EQ(doc.object(0).id(), a);
    EXPECT_EQ(doc.object(1).id(), b);
    EXPECT_EQ(doc.object(2).id(), c);
}

TEST(SketchCutting, TheKnifeDividesFilledShapesAndSkipsTheRest) {
    SketchDocument doc;
    SketchObject& card = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    SketchObject& locked = doc.add(SketchObject::makeRect(0, 200, 100, 100));
    locked.setLocked(true);
    SketchObject& aside = doc.add(SketchObject::makeRect(300, 0, 100, 100));
    card.setName("Card");
    (void)aside;
    // The line as drawn crosses only the first shape (the locked one is never touched; the last is out of its way).
    ASSERT_TRUE(doc.knifeCut({BLPoint(50, -10), BLPoint(50, 110)}));
    ASSERT_EQ(doc.objectCount(), 4u);
    double total = 0.0;
    int pieces = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        if (doc.object(i).name() == "Card") {
            total += areaOf(doc.object(i));
            ++pieces;
            EXPECT_EQ(doc.object(i).type(), SketchObjectType::Path);
        }
    }
    EXPECT_EQ(pieces, 2);
    EXPECT_NEAR(total, 10000.0, 1.0) << "the pieces together are the shape, with no gap";
    EXPECT_EQ(doc.selection().size(), 2u) << "the pieces are selected";
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 3u);
    // A line that misses everything changes nothing.
    EXPECT_FALSE(doc.knifeCut({BLPoint(200, -10), BLPoint(200, 110)}));
    // A line across two shapes cuts both - unless some of them are selected, and then only those.
    std::size_t asideId = doc.object(2).id();
    doc.clearSelection();
    ASSERT_TRUE(doc.knifeCut({BLPoint(50, 50), BLPoint(350, 50)}));
    EXPECT_EQ(doc.objectCount(), 5u) << "both shapes were cut (2 pieces each) and the locked one was left alone";
    doc.undoStack().undo();
    doc.setSelection({asideId});
    ASSERT_TRUE(doc.knifeCut({BLPoint(50, 50), BLPoint(350, 50)}));
    EXPECT_EQ(doc.objectCount(), 4u) << "only the selected shape was cut";
    EXPECT_NE(doc.findById(card.id()), nullptr) << "the card is still whole";
}

TEST(SketchCutting, TheEraserRemovesWhatItTouches) {
    SketchDocument doc;
    SketchObject& card = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    card.setName("Card");
    std::size_t line = doc.add(SketchObject::makeLine(0, 200, 100, 200)).id();
    // A stroke across the middle of the card, and of the line: the card falls into two pieces, the line into two lines.
    ASSERT_TRUE(doc.erase({BLPoint(50, -20), BLPoint(50, 220)}, 20.0));
    int cardPieces = 0, lines = 0;
    double total = 0.0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        if (doc.object(i).name() == "Card") {
            ++cardPieces;
            total += areaOf(doc.object(i));
        } else {
            ++lines;
            EXPECT_NE(doc.object(i).id(), line);
        }
    }
    EXPECT_EQ(cardPieces, 2);
    EXPECT_NEAR(total, 100.0 * 80.0, 3.0) << "a 20 point wide stroke took a 20 wide slice";
    EXPECT_EQ(lines, 2);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(doc.object(1).id(), line);
    // Rubbing out a whole small shape removes it; a stroke that touches nothing does nothing.
    EXPECT_FALSE(doc.erase({BLPoint(300, 300), BLPoint(320, 300)}, 10.0));
    doc.clearSelection();   // (undo selected the objects it brought back, and a selection limits the eraser to it)
    SketchObject& dot = doc.add(SketchObject::makeRect(500, 500, 6, 6));
    std::size_t dotId = dot.id();
    EXPECT_TRUE(doc.erase({BLPoint(503, 503)}, 30.0));
    EXPECT_EQ(doc.findById(dotId), nullptr);
    doc.undoStack().undo();
    EXPECT_NE(doc.findById(dotId), nullptr);
}

TEST(SketchCutting, TheScissorsOpenAClosedPathAndDivideAnOpenOne) {
    SketchDocument doc;
    SketchObject& square = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    square.setName("Square");
    std::size_t squareId = square.id();
    EXPECT_FALSE(doc.scissorsCut(BLPoint(50, 50), 5.0)) << "the middle of a shape is not on its path";
    ASSERT_TRUE(doc.scissorsCut(BLPoint(40, 3), 5.0));
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_NE(doc.object(0).id(), squareId);
    EXPECT_EQ(doc.object(0).name(), "Square");
    std::vector<SubPath> subs = pathToSubPaths(doc.object(0).geometry());
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_FALSE(subs[0].closed);
    EXPECT_EQ(subs[0].anchors.size(), 6u);
    EXPECT_NEAR(subs[0].anchors.front().point.x, 40.0, 1e-6);
    doc.undoStack().undo();
    EXPECT_EQ(doc.object(0).id(), squareId);

    doc.clearObjects();
    doc.add(SketchObject::makeLine(0, 0, 100, 0));
    ASSERT_TRUE(doc.scissorsCut(BLPoint(30, 2), 5.0));
    ASSERT_EQ(doc.objectCount(), 2u);
    BLBox first = doc.object(0).geometryBounds(), second = doc.object(1).geometryBounds();
    EXPECT_NEAR(first.x0, 0.0, 1e-6);
    EXPECT_NEAR(first.x1, 30.0, 1e-6);
    EXPECT_NEAR(second.x0, 30.0, 1e-6);
    EXPECT_NEAR(second.x1, 100.0, 1e-6);
    EXPECT_FALSE(doc.scissorsCut(BLPoint(0, 0), 5.0)) << "cutting at the end of an open path changes nothing";
    EXPECT_EQ(doc.objectCount(), 2u);
}

// ---- image export and Object > Path ----

TEST(SketchExport, RendersThePageAtTheRequestedScale) {
    SketchDocument doc;
    doc.setPageSize(100, 50);
    doc.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
    SketchObject& square = doc.add(SketchObject::makeRect(10, 10, 20, 20));
    square.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    square.setStroke(SketchPaint::none());
    BLImage image;
    ASSERT_TRUE(renderPageImage(doc, 2.0, image));
    EXPECT_EQ(image.width(), 200);
    EXPECT_EQ(image.height(), 100);
    BLImageData data;
    image.get_data(&data);
    auto pixel = [&](int x, int y) { return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride)[x]; };
    EXPECT_EQ(pixel(40, 40) & 0xFFFFFFu, 0xFF0000u) << "inside the square (scaled by 2)";
    EXPECT_EQ(pixel(150, 80) & 0xFFFFFFu, 0xFFFFFFu) << "the page color elsewhere";
    // No page color: transparent.
    doc.setPageBackground(std::nullopt);
    ASSERT_TRUE(renderPageImage(doc, 1.0, image));
    image.get_data(&data);
    EXPECT_EQ(reinterpret_cast<const uint32_t*>(data.pixel_data)[0] >> 24, 0u);
    // Silly sizes are refused.
    EXPECT_FALSE(renderPageImage(doc, 0.0, image));
    EXPECT_FALSE(renderPageImage(doc, 1.0e6, image));
}

TEST(SketchExport, WritesPngAndBmpFilesByExtension) {
    SketchDocument doc;
    doc.setPageSize(64, 32);
    doc.setPageBackground(std::nullopt);
    doc.add(SketchObject::makeRect(0, 0, 32, 32));
    std::filesystem::path folder = std::filesystem::temp_directory_path();
    std::string png = (folder / "sketch_export_test.PNG").string(), jpg = (folder / "sketch_export_test.bmp").string();
    ASSERT_TRUE(exportPageImage(doc, png, 1.0));
    ASSERT_TRUE(exportPageImage(doc, jpg, 2.0));
    EXPECT_FALSE(exportPageImage(doc, (folder / "sketch_export_test.jpg").string(), 1.0)) << "no JPEG writer";
    BLImage back;
    ASSERT_EQ(back.read_from_file(png.c_str()), BL_SUCCESS);
    EXPECT_EQ(back.width(), 64);
    EXPECT_EQ(back.height(), 32);
    ASSERT_EQ(back.read_from_file(jpg.c_str()), BL_SUCCESS);
    EXPECT_EQ(back.width(), 128);
    EXPECT_FALSE(exportPageImage(doc, "Z:/no/such/folder/x.png", 1.0));
    EXPECT_NEAR(exportScaleFor(doc), double(doc.ppi()) / 72.0, 1e-9);
    std::filesystem::remove(png);
    std::filesystem::remove(jpg);
}

TEST(SketchPathCommands, ReverseJoinAndSimplify) {
    SketchDocument doc;
    SketchObject& line = doc.add(SketchObject::makeLine(0, 0, 10, 0));
    std::size_t lineId = line.id();
    doc.setSelection({lineId});
    ASSERT_TRUE(doc.reverseSelection());
    std::vector<SubPath> subs = pathToSubPaths(doc.findById(lineId)->geometry());
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_NEAR(subs[0].anchors.front().point.x, 10.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(pathToSubPaths(doc.findById(lineId)->geometry())[0].anchors.front().point.x, 0.0, 1e-9);

    // One open path: join closes it.
    BLPath triangle;
    triangle.move_to(0, 0);
    triangle.line_to(100, 0);
    triangle.line_to(50, 80);
    SketchObject& open = doc.add(SketchObject::makePath(triangle));
    doc.setSelection({open.id()});
    ASSERT_TRUE(doc.joinSelection());
    EXPECT_TRUE(pathToSubPaths(doc.findById(open.id())->geometry())[0].closed);
    doc.undoStack().undo();
    EXPECT_FALSE(pathToSubPaths(doc.findById(open.id())->geometry())[0].closed);

    // Two open paths whose ends are near each other are chained (the second one turned round when needed).
    doc.clearObjects();
    std::size_t a = doc.add(SketchObject::makeLine(0, 0, 50, 0)).id();
    std::size_t b = doc.add(SketchObject::makeLine(100, 0, 50, 0)).id();   // runs toward the first
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.joinSelection());
    ASSERT_EQ(doc.objectCount(), 1u);
    subs = pathToSubPaths(doc.object(0).geometry());
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].anchors.size(), 3u) << "meeting ends become one anchor";
    EXPECT_FALSE(subs[0].closed);
    EXPECT_NEAR(subs[0].anchors.back().point.x, 100.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 2u);
    doc.setSelection({});
    EXPECT_FALSE(doc.joinSelection());

    // Simplify: a hundred points along a gentle wave become a few anchors that stay on it.
    doc.clearObjects();
    std::vector<BLPoint> wave;
    for (int i = 0; i <= 100; ++i) { wave.push_back(BLPoint(i * 2.0, 20.0 * std::sin(i * 0.06))); }
    SketchObject& dense = doc.add(SketchObject::makePolyline(wave));
    doc.setSelection({dense.id()});
    ASSERT_TRUE(doc.simplifySelection(0.5));
    std::vector<SubPath> simple = pathToSubPaths(doc.object(0).geometry());
    ASSERT_EQ(simple.size(), 1u);
    EXPECT_LT(simple[0].anchors.size(), 12u);
    EXPECT_GE(simple[0].anchors.size(), 2u);
    BLBox box = doc.object(0).geometryBounds();
    EXPECT_NEAR(box.x1, 200.0, 0.5);
    EXPECT_NEAR(box.y0, -20.0 * std::sin(1.57 / 0.06 * 0.06) * 1.0 + 0.0, 40.0) << "still the same wave, give or take";
    doc.undoStack().undo();
    EXPECT_EQ(pathToSubPaths(doc.object(0).geometry())[0].anchors.size(), 101u);
}

TEST(SketchPathCommands, OutlineStrokeTurnsTheStrokeIntoAShape) {
    SketchDocument doc;
    SketchObject& line = doc.add(SketchObject::makeLine(0, 0, 100, 0));
    line.setStroke(SketchPaint::solid(SketchColor::fromHex("#00aa00")));
    line.setStrokeWidth(10.0);
    doc.setSelection({line.id()});
    ASSERT_TRUE(doc.outlineStroke());
    ASSERT_EQ(doc.objectCount(), 1u) << "a line has no fill: only the outlined stroke remains";
    const SketchObject& ring = doc.object(0);
    EXPECT_EQ(ring.fill().color().toHex(), "#00aa00") << "it wears the stroke's color as a fill";
    EXPECT_TRUE(ring.stroke().isNone());
    BLBox box = ring.geometryBounds();
    EXPECT_NEAR(box.y1 - box.y0, 10.0, 1e-6);
    EXPECT_NEAR(box.x1 - box.x0, 100.0, 1e-6) << "butt caps add nothing";
    doc.undoStack().undo();
    EXPECT_EQ(doc.object(0).type(), SketchObjectType::Line);

    // A filled, stroked rectangle: the fill stays, the stroke becomes a ring above it.
    doc.clearObjects();
    SketchObject& card = doc.add(SketchObject::makeRect(0, 0, 100, 60));
    card.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    card.setStroke(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    card.setStrokeWidth(4.0);
    doc.setSelection({card.id()});
    ASSERT_TRUE(doc.outlineStroke());
    ASSERT_EQ(doc.objectCount(), 2u);
    EXPECT_EQ(doc.object(0).fill().color().toHex(), "#ff0000");
    EXPECT_TRUE(doc.object(0).stroke().isNone());
    EXPECT_EQ(doc.object(1).fill().color().toHex(), "#0000ff");
    BLBox outer = doc.object(1).geometryBounds();
    EXPECT_NEAR(outer.x1 - outer.x0, 104.0, 0.1) << "the ring reaches half the weight past the outline";
    // Nothing to outline: no stroke.
    doc.setSelection({doc.object(0).id()});
    EXPECT_FALSE(doc.outlineStroke());
}

// ---- numeric transforms and Transform Again ----

TEST(SketchTransform, SetBoxMovesAndResizesTheSelection) {
    SketchDocument doc;
    SketchObject& a = doc.add(SketchObject::makeRect(10, 20, 100, 50));
    doc.setSelection({a.id()});
    ASSERT_TRUE(doc.setSelectionBox(BLBox(30, 40, 230, 140)));   // twice as wide and tall, moved
    BLBox box = doc.object(0).geometryBounds();
    EXPECT_NEAR(box.x0, 30.0, 1e-9);
    EXPECT_NEAR(box.y0, 40.0, 1e-9);
    EXPECT_NEAR(box.x1, 230.0, 1e-9);
    EXPECT_NEAR(box.y1, 140.0, 1e-9);
    EXPECT_EQ(doc.undoStack().undoDescription(), "Resize");
    doc.undoStack().undo();
    EXPECT_NEAR(doc.object(0).geometryBounds().x1, 110.0, 1e-9);
    ASSERT_TRUE(doc.setSelectionBox(BLBox(0, 0, 100, 50)));   // the same size: only a move
    EXPECT_EQ(doc.undoStack().undoDescription(), "Move");
    EXPECT_FALSE(doc.setSelectionBox(BLBox(0, 0, 100, 50))) << "nothing to change";
    EXPECT_FALSE(doc.setSelectionBox(BLBox(0, 0, -5, 50))) << "a negative size is refused";
    // A straight line has no height: it can move and change its length, nothing else.
    doc.clearObjects();
    SketchObject& line = doc.add(SketchObject::makeLine(0, 0, 100, 0));
    doc.setSelection({line.id()});
    ASSERT_TRUE(doc.setSelectionBox(BLBox(10, 5, 60, 5)));
    EXPECT_NEAR(doc.object(0).geometryBounds().x0, 10.0, 1e-9);
    EXPECT_NEAR(doc.object(0).geometryBounds().x1, 60.0, 1e-9);
    EXPECT_NEAR(doc.object(0).geometryBounds().y0, 5.0, 1e-9);
    // Nothing selected: nothing happens.
    doc.clearSelection();
    EXPECT_FALSE(doc.setSelectionBox(BLBox(0, 0, 10, 10)));
}

TEST(SketchTransform, RotateByAnAngleTurnsTheFrameToo) {
    SketchDocument doc;
    SketchObject& a = doc.add(SketchObject::makeRect(0, 0, 100, 20));
    doc.setSelection({a.id()});
    const double pi = 3.14159265358979323846;
    ASSERT_TRUE(doc.rotateSelectionBy(pi / 2.0));
    BLBox box = doc.object(0).geometryBounds();
    EXPECT_NEAR(box.x1 - box.x0, 20.0, 1e-6) << "a quarter turn swaps the sides";
    EXPECT_NEAR(box.y1 - box.y0, 100.0, 1e-6);
    EXPECT_NEAR((box.x0 + box.x1) / 2.0, 50.0, 1e-6) << "about the center";
    EXPECT_NEAR(doc.object(0).frameAngle(), pi / 2.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.object(0).frameAngle(), 0.0, 1e-9);
    EXPECT_FALSE(doc.rotateSelectionBy(0.0));
}

TEST(SketchTransform, TransformAgainRepeatsTheLastMoveOnTheSelection) {
    SketchDocument doc;
    EXPECT_FALSE(doc.canTransformAgain());
    SketchObject& a = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    SketchObject& b = doc.add(SketchObject::makeRect(100, 0, 10, 10));
    std::size_t idB = b.id();
    doc.setSelection({a.id()});
    EXPECT_FALSE(doc.transformAgain());
    doc.transformObjects({a.id()}, BLMatrix2D::make_translation(15, 5), "Move");
    EXPECT_TRUE(doc.canTransformAgain());
    ASSERT_TRUE(doc.transformAgain());
    EXPECT_NEAR(doc.object(0).geometryBounds().x0, 30.0, 1e-9) << "moved by (15, 5) once more";
    EXPECT_NEAR(doc.object(0).geometryBounds().y0, 10.0, 1e-9);
    EXPECT_EQ(doc.undoStack().undoDescription(), "Transform Again");
    doc.setSelection({idB});
    ASSERT_TRUE(doc.transformAgain());
    EXPECT_NEAR(doc.findById(idB)->geometryBounds().x0, 115.0, 1e-9) << "and on whatever is selected now";
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(idB)->geometryBounds().x0, 100.0, 1e-9);
    // A rotation is repeated about the same point, and the frames turn again.
    doc.setSelection({a.id()});
    const double pi = 3.14159265358979323846;
    doc.rotateSelectionBy(pi / 6.0);
    double first = doc.object(0).frameAngle();
    ASSERT_TRUE(doc.transformAgain());
    EXPECT_NEAR(doc.object(0).frameAngle(), first + pi / 6.0, 1e-9);
}

// ---- scrubbing numbers by dragging their captions ----

TEST(SketchScrub, StepsFollowTheModifierKeys) {
    EXPECT_DOUBLE_EQ(NumberScrubber::stepFor(1.0, 0), 1.0);
    EXPECT_DOUBLE_EQ(NumberScrubber::stepFor(1.0, newui::kmShift), 0.1);
    EXPECT_DOUBLE_EQ(NumberScrubber::stepFor(1.0, newui::kmShift | newui::kmCtrl), 0.01);
    EXPECT_DOUBLE_EQ(NumberScrubber::stepFor(0.5, newui::kmShift), 0.05) << "relative to the field's own unit";
    EXPECT_DOUBLE_EQ(NumberScrubber::stepFor(1.0, newui::kmCtrl), 1.0) << "Ctrl alone changes nothing";
    EXPECT_DOUBLE_EQ(NumberScrubber::roundToStep(12.34, 1.0), 12.0);
    EXPECT_DOUBLE_EQ(NumberScrubber::roundToStep(12.36, 0.1), 12.4);
    EXPECT_DOUBLE_EQ(NumberScrubber::roundToStep(0.1 + 0.2, 0.01), 0.3) << "no binary noise";
    EXPECT_DOUBLE_EQ(NumberScrubber::roundToStep(5.0, 0.0), 5.0) << "no step: unchanged";
}

TEST(SketchScrub, ADragOnALabelReportsValuesThenEnds) {
    newui::Label label;
    std::vector<double> seen;
    bool began = false, ended = false, endedChanged = false;
    auto scrubber = NumberScrubber::attach(label, 1.0, 0.0, 100.0);
    scrubber->onBegin += [&](NumberScrubber&, double& value) { value = 10.0; began = true; return newui::SyncReturn::Handled; };
    scrubber->onChange += [&](NumberScrubber&, double value) { seen.push_back(value); return newui::SyncReturn::Handled; };
    scrubber->onEnd += [&](NumberScrubber&, bool changed) { ended = true; endedChanged = changed; return newui::SyncReturn::Handled; };
    label.onMouseDown(label, newui::Point(50, 5), std::uint32_t(newui::mbmLeftButton), 0u);
    EXPECT_TRUE(began);
    label.onMouseMove(label, newui::Point(53, 5), std::uint32_t(newui::mbmLeftButton), 0u);   // 3 px: 3 units
    label.onMouseMove(label, newui::Point(53, 40), std::uint32_t(newui::mbmLeftButton), 0u);  // only sideways counts
    label.onMouseMove(label, newui::Point(63, 5), std::uint32_t(newui::mbmLeftButton), std::uint32_t(newui::kmShift));   // 10 px with Shift: +1.0
    label.onMouseMove(label, newui::Point(73, 5), std::uint32_t(newui::mbmLeftButton), std::uint32_t(newui::kmShift | newui::kmCtrl));   // +0.1
    label.onMouseMove(label, newui::Point(-500, 5), std::uint32_t(newui::mbmLeftButton), 0u);   // far left: clamped at the minimum
    label.onMouseUp(label, newui::Point(-500, 5), std::uint32_t(newui::mbmLeftButton), 0u);
    ASSERT_EQ(seen.size(), 4u);
    EXPECT_DOUBLE_EQ(seen[0], 13.0);
    EXPECT_DOUBLE_EQ(seen[1], 14.0);
    EXPECT_DOUBLE_EQ(seen[2], 14.1);
    EXPECT_DOUBLE_EQ(seen[3], 0.0);
    EXPECT_TRUE(ended);
    EXPECT_TRUE(endedChanged);
    // A press with the right button, or a begin that says no, starts nothing.
    seen.clear();
    label.onMouseDown(label, newui::Point(0, 0), std::uint32_t(newui::mbmRightButton), 0u);
    label.onMouseMove(label, newui::Point(20, 0), std::uint32_t(0), 0u);
    EXPECT_TRUE(seen.empty());
}

// ---- ruler guides ----

TEST(SketchGuides, AreUndoableAndSurviveSvgAndBinary) {
    SketchDocument doc;
    EXPECT_TRUE(doc.guides().empty());
    std::size_t a = doc.addGuide(true, 100.0);
    std::size_t b = doc.addGuide(false, 50.5);
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    ASSERT_EQ(doc.guides().size(), 2u);
    EXPECT_TRUE(doc.guides()[0].vertical);
    EXPECT_DOUBLE_EQ(doc.guides()[1].position, 50.5);
    EXPECT_TRUE(doc.isModified());

    EXPECT_TRUE(doc.moveGuide(0, 120.0));
    EXPECT_DOUBLE_EQ(doc.guides()[0].position, 120.0);
    EXPECT_FALSE(doc.moveGuide(0, 120.0)) << "no change";
    EXPECT_FALSE(doc.moveGuide(9, 1.0)) << "bad index";
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.guides()[0].position, 100.0);
    doc.undoStack().redo();

    SketchDocument copy, packed;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), copy));
    ASSERT_EQ(copy.guides().size(), 2u);
    EXPECT_TRUE(copy.guides()[0].vertical);
    EXPECT_DOUBLE_EQ(copy.guides()[0].position, 120.0);
    EXPECT_FALSE(copy.guides()[1].vertical);
    EXPECT_DOUBLE_EQ(copy.guides()[1].position, 50.5);
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), packed));
    ASSERT_EQ(packed.guides().size(), 2u);
    EXPECT_DOUBLE_EQ(packed.guides()[1].position, 50.5);

    EXPECT_TRUE(doc.removeGuide(0));
    ASSERT_EQ(doc.guides().size(), 1u);
    EXPECT_FALSE(doc.guides()[0].vertical);
    doc.undoStack().undo();
    ASSERT_EQ(doc.guides().size(), 2u);
    EXPECT_TRUE(doc.guides()[0].vertical) << "a deleted guide comes back in its place";
    EXPECT_TRUE(doc.clearGuides());
    EXPECT_TRUE(doc.guides().empty());
    EXPECT_FALSE(doc.clearGuides());
    doc.undoStack().undo();
    EXPECT_EQ(doc.guides().size(), 2u);
    doc.clearObjects();
    EXPECT_TRUE(doc.guides().empty()) << "a new document starts without guides";
    // A document with none writes no attribute, and one read from a file without it has none.
    SketchDocument plain;
    EXPECT_EQ(svgFromDocument(plain).find("data-guides"), std::string::npos);
    ASSERT_TRUE(documentFromSvg("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10' data-guides='v1 h2 x3 v'/>", plain));
    EXPECT_EQ(plain.guides().size(), 2u) << "bad entries are skipped";
}

// ---- layers ----

TEST(SketchLayers, TreeStackingVisibilityAndUndo) {
    SketchDocument doc;
    ASSERT_EQ(doc.layers().size(), 1u);
    std::size_t base = doc.activeLayer();
    std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    std::size_t top = doc.addLayer(0, "Top");
    EXPECT_EQ(doc.activeLayer(), top);
    std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    std::size_t sub = doc.addLayer(base, "Sub");
    std::size_t c = doc.add(SketchObject::makeRect(40, 0, 10, 10)).id();
    EXPECT_EQ(doc.layerDepth(sub), 1u);
    EXPECT_TRUE(doc.layerContains(base, sub));
    // Stacking: base's sublayer, then base's own objects, then the Top layer.
    ASSERT_EQ(doc.objectCount(), 3u);
    EXPECT_EQ(doc.object(0).id(), c);
    EXPECT_EQ(doc.object(1).id(), a);
    EXPECT_EQ(doc.object(2).id(), b);

    // Hiding a layer hides what is in it and its sublayers; own flags stay untouched.
    EXPECT_TRUE(doc.setLayerVisible(base, false));
    EXPECT_FALSE(doc.findById(a)->isVisible());
    EXPECT_FALSE(doc.findById(c)->isVisible());
    EXPECT_TRUE(doc.findById(a)->ownVisible());
    EXPECT_TRUE(doc.findById(b)->isVisible());
    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(a)->isVisible());
    EXPECT_TRUE(doc.setLayerLocked(top, true));
    EXPECT_TRUE(doc.findById(b)->isLocked());
    EXPECT_FALSE(doc.findById(b)->ownLocked());
    doc.setLayerLocked(top, false);

    // Moving the selection to another layer puts it on top of that layer; undo restores the order.
    doc.setSelection({a});
    EXPECT_TRUE(doc.moveSelectionToLayer(top));
    EXPECT_EQ(doc.findById(a)->layer(), top);
    EXPECT_EQ(doc.object(2).id(), a);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(a)->layer(), base);
    EXPECT_EQ(doc.object(1).id(), a);

    // Reparenting, and no moving a layer into itself.
    EXPECT_FALSE(doc.moveLayer(base, sub, 0));
    EXPECT_TRUE(doc.moveLayer(top, base, 0));
    EXPECT_EQ(doc.findLayer(top)->parent, base);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findLayer(top)->parent, 0u);

    // Saved in both formats.
    SketchDocument svg, bin;
    doc.renameLayer(top, "Top & <more>");
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), svg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), bin));
    for (SketchDocument* d : {&svg, &bin}) {
        ASSERT_EQ(d->layers().size(), 3u);
        EXPECT_EQ(d->findLayer(top)->name, "Top & <more>");
        EXPECT_EQ(d->findLayer(sub)->parent, base);
        EXPECT_EQ(d->layerObjectCount(base, true), 2u);
        EXPECT_EQ(d->layerObjectCount(top, false), 1u);
    }

    // Deleting a layer removes its sublayers and objects; undo brings them back in place; the last layer stays.
    EXPECT_TRUE(doc.deleteLayer(base));
    EXPECT_EQ(doc.objectCount(), 1u);
    EXPECT_EQ(doc.layers().size(), 1u);
    EXPECT_FALSE(doc.deleteLayer(top));
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 3u);
    EXPECT_EQ(doc.layers().size(), 3u);
    EXPECT_EQ(doc.object(0).id(), c);
    // Arrange stays inside the layer.
    doc.setSelection({a});
    EXPECT_FALSE(doc.arrangeSelection(SketchDocument::ArrangeOp::BringToFront)) << "a is already the topmost of its layer";
}

// ---- Shape Builder ----

TEST(SketchShapeBuilder, CellsAreJoinedOrErasedAndTheRestStaysWhole) {
    auto make = [](SketchDocument& doc) {
        std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 100)).id();
        std::size_t b = doc.add(SketchObject::makeRect(50, 0, 100, 100)).id();
        doc.setSelection({a, b});
    };
    SketchDocument doc;
    make(doc);
    SketchDocument::ShapeBuilderModel model = doc.shapeBuilderModel();
    ASSERT_EQ(model.objects.size(), 2u);
    ASSERT_EQ(model.cells.size(), 3u) << "A alone, the overlap, B alone";
    std::size_t overlap = model.cells.size();
    for (std::size_t i = 0; i < model.cells.size(); ++i) {
        if (model.cells[i].members.size() == 2) {
            overlap = i;
        }
    }
    ASSERT_LT(overlap, model.cells.size());

    // Joining the overlap on its own makes it a shape; both rectangles lose it.
    ASSERT_TRUE(doc.shapeBuild({overlap}, false));
    EXPECT_EQ(doc.objectCount(), 3u);
    double total = 0.0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        BLBox box = doc.object(i).geometryBounds();
        total += (box.x1 - box.x0) * (box.y1 - box.y0);
    }
    EXPECT_NEAR(total, 5000.0 * 3, 1e-6) << "A without the overlap, the overlap, B without it: three 50 x 100 pieces";
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 2u);

    // Erasing it leaves the two crescents.
    ASSERT_TRUE(doc.shapeBuild({overlap}, true));
    EXPECT_EQ(doc.objectCount(), 2u);
    EXPECT_NEAR(doc.object(0).geometryBounds().x1, 50.0, 1e-6);
    doc.undoStack().undo();

    // Joining every cell is the union: one shape.
    ASSERT_TRUE(doc.shapeBuild({0, 1, 2}, false));
    EXPECT_EQ(doc.objectCount(), 1u);
    EXPECT_NEAR(doc.object(0).geometryBounds().x1, 150.0, 1e-6);
    EXPECT_FALSE(doc.shapeBuild({}, false));
    EXPECT_FALSE(doc.shapeBuild({9}, false));
}

// ---- swatches and the eyedropper ----

TEST(SketchSwatches, AreUndoableAndSavedAndTheEyedropperCopiesALook) {
    SketchDocument doc;
    EXPECT_EQ(doc.swatches(), defaultSwatches());
    EXPECT_TRUE(doc.addSwatch("#AABBCC"));
    EXPECT_EQ(doc.swatches().back(), "#aabbcc");
    EXPECT_FALSE(doc.addSwatch("#aabbcc")) << "no duplicates";
    EXPECT_FALSE(doc.addSwatch("red")) << "hex only";
    SketchDocument svg, bin;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), svg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), bin));
    EXPECT_EQ(svg.swatches(), doc.swatches());
    EXPECT_EQ(bin.swatches(), doc.swatches());
    EXPECT_TRUE(doc.deleteSwatch(0));
    EXPECT_EQ(doc.swatches().front(), "#555555");
    doc.undoStack().undo();
    EXPECT_EQ(doc.swatches().front(), "#000000");
    EXPECT_FALSE(doc.deleteSwatch(99));
    while (doc.swatches().size() < 24) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#0000%02x", int(doc.swatches().size()) + 100);
        ASSERT_TRUE(doc.addSwatch(hex));
    }
    EXPECT_FALSE(doc.addSwatch("#fefefe")) << "at most 24";

    SketchObject& source = doc.add(SketchObject::makeRect(0, 0, 50, 50));
    source.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    source.setOpacity(0.5);
    SketchObject& target = doc.add(SketchObject::makeRect(100, 0, 50, 50));
    doc.setSelection({target.id()});
    ASSERT_TRUE(doc.sampleLook(BLPoint(25, 25), 2.0));
    EXPECT_EQ(target.fill().color().toHex(), "#ff0000");
    EXPECT_DOUBLE_EQ(target.opacity(), 0.5);
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(target.opacity(), 1.0);
    EXPECT_FALSE(doc.sampleLook(BLPoint(500, 500), 2.0));
    // With only the source selected the look becomes the default for new shapes.
    doc.setSelection({source.id()});
    ASSERT_TRUE(doc.sampleLook(BLPoint(25, 25), 2.0));
    EXPECT_EQ(doc.defaultFill().color().toHex(), "#ff0000");
}

// ---- Select Same, Magic Wand, Lasso ----

TEST(SketchSelect, SameWandAndLasso) {
    SketchDocument doc;
    auto rect = [&doc](double x, const char* fill) -> SketchObject& {
        SketchObject& r = doc.add(SketchObject::makeRect(x, 0, 40, 40));
        r.setFill(SketchPaint::solid(SketchColor::fromHex(fill)));
        return r;
    };
    SketchObject& a = rect(0, "#ff0000");
    SketchObject& b = rect(100, "#ff0000");
    SketchObject& c = rect(200, "#00ff00");
    SketchObject& d = rect(300, "#fe0202");   // nearly red
    c.setOpacity(0.5);
    EXPECT_FALSE(doc.selectSame(SketchDocument::SameKind::FillColor)) << "nothing selected";
    doc.setSelection({a.id()});
    ASSERT_TRUE(doc.selectSame(SketchDocument::SameKind::FillColor));
    EXPECT_EQ(doc.selection().size(), 2u);
    EXPECT_TRUE(doc.isSelected(b.id()));
    doc.setSelection({c.id()});
    doc.selectSame(SketchDocument::SameKind::Opacity);
    EXPECT_EQ(doc.selection().size(), 1u);

    ASSERT_TRUE(doc.selectSimilar(a.id(), false, 0.08));
    EXPECT_EQ(doc.selection().size(), 3u) << "the wand tolerates a little difference (a, b and d)";
    EXPECT_FALSE(doc.isSelected(c.id()));
    ASSERT_TRUE(doc.selectSimilar(c.id(), true, 0.0));
    EXPECT_EQ(doc.selection().size(), 4u) << "adding keeps the selection";
    doc.setSelection({});
    doc.selectSimilar(a.id(), false, 0.0);
    EXPECT_FALSE(doc.isSelected(d.id())) << "with no tolerance only the exact color";

    // The lasso takes what it touches.
    doc.selectInPolygon({BLPoint(-10, -10), BLPoint(150, -10), BLPoint(150, 60), BLPoint(-10, 60)}, false);
    EXPECT_EQ(doc.selection().size(), 2u);
    doc.selectInPolygon({BLPoint(195, 20), BLPoint(205, 20), BLPoint(205, 25)}, true);
    EXPECT_EQ(doc.selection().size(), 3u) << "a loop wholly inside a shape touches it";
    doc.selectInPolygon({BLPoint(500, 500), BLPoint(510, 500), BLPoint(510, 510)}, false);
    EXPECT_TRUE(doc.selection().empty());
}

TEST(SketchSelect, MarqueeTouchesStrokesAndFillsNotBoundingBoxes) {
    SketchDocument doc;
    SketchObject& ring = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    ring.setFill(SketchPaint::none());
    ring.setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    ring.setStrokeWidth(2.0);
    SketchObject& disc = doc.add(SketchObject::makeRect(300, 0, 100, 100));
    disc.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));

    EXPECT_FALSE(doc.selectInRect(BLBox(40, 40, 60, 60), false)) << "inside an unfilled outline's box, touching nothing";
    EXPECT_TRUE(doc.selectInRect(BLBox(-10, 40, 10, 60), false)) << "crosses the stroke";
    EXPECT_TRUE(doc.isSelected(ring.id()));
    EXPECT_FALSE(doc.isSelected(disc.id()));
    EXPECT_TRUE(doc.selectInRect(BLBox(340, 40, 360, 60), false)) << "wholly inside a fill";
    EXPECT_TRUE(doc.isSelected(disc.id()));
    EXPECT_TRUE(doc.selectInRect(BLBox(-10, -10, 500, 500), false));
    EXPECT_EQ(doc.selection().size(), 2u) << "a rectangle round everything";
}

// ---- text style ----

TEST(SketchText, StyleChangesTheOutlineAndSurvivesUndoAndFiles) {
    SketchDocument doc;
    SketchObject& text = doc.add(SketchObject::makeText("Hello\nWorld wide", "", 24.0, BLPoint(10, 40)));
    doc.setSelection({text.id()});
    BLBox plain = text.geometryBounds();
    ASSERT_GT(plain.x1, plain.x0);
    EXPECT_TRUE(text.textStyle().isDefault());

    // Tracking widens it; undo brings the old outline back.
    ASSERT_TRUE(doc.changeSelectionText([](TextState& s) { s.style.tracking = 5.0; }, "Tracking"));
    EXPECT_GT(text.geometryBounds().x1 - text.geometryBounds().x0, plain.x1 - plain.x0 + 10.0);
    doc.undoStack().undo();
    EXPECT_NEAR(text.geometryBounds().x1, plain.x1, 1e-6);
    doc.undoStack().redo();

    // Leading spreads the lines; alignment moves the shorter line.
    TextStyle style;
    style.leading = 2.0;
    ASSERT_TRUE(doc.changeSelectionText([&style](TextState& s) { s.style = style; }, "Leading"));
    EXPECT_GT(text.geometryBounds().y1 - text.geometryBounds().y0, (plain.y1 - plain.y0) * 1.3);
    EXPECT_NEAR(textLineHeight("", 24.0, style), textLineHeight("", 24.0, TextStyle()) * 2.0, 1e-9);
    TextStyle right;
    right.align = TextStyle::Right;
    EXPECT_GT(textLineOffset("Hi\nA much longer line", 0, "", 24.0, right), 10.0);
    EXPECT_NEAR(textLineOffset("Hi\nA much longer line", 1, "", 24.0, right), 0.0, 1e-9);
    EXPECT_NEAR(textLineOffset("Hi\nA much longer line", 0, "", 24.0, TextStyle()), 0.0, 1e-9);
    EXPECT_FALSE(doc.changeSelectionText([&style](TextState& s) { s.style = style; }, "Again")) << "no change, no step";

    // Both file formats keep the style.
    style.bold = true;
    style.italic = true;
    style.align = TextStyle::Center;
    style.tracking = 1.5;
    doc.changeSelectionText([&style](TextState& s) { s.style = style; }, "All");
    SketchDocument svg, bin;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), svg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), bin));
    for (SketchDocument* d : {&svg, &bin}) {
        ASSERT_EQ(d->objectCount(), 1u);
        EXPECT_TRUE(d->object(0).textStyle() == style);
    }
    // Not text: nothing to change.
    SketchDocument other;
    other.setSelection({other.add(SketchObject::makeRect(0, 0, 10, 10)).id()});
    EXPECT_FALSE(other.changeSelectionText([](TextState& s) { s.size = 10.0; }, "x"));
}

// ---- Image Trace ----

TEST(SketchTrace, ABlockPictureBecomesStackedShapes) {
    // 20 x 20: white background, a red 8 x 8 square with a white 2 x 2 hole... and a transparent corner.
    const int w = 20, h = 20;
    std::vector<std::uint32_t> pixels(std::size_t(w * h), 0xFFFFFFFFu);
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) {
            pixels[std::size_t(y * w + x)] = 0xFFFF0000u;
        }
    }
    for (int y = 7; y < 9; ++y) {
        for (int x = 7; x < 9; ++x) {
            pixels[std::size_t(y * w + x)] = 0xFFFFFFFFu;
        }
    }
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            pixels[std::size_t(y * w + x)] = 0x00000000u;   // transparent
        }
    }
    TraceOptions options;
    options.colors = 2;
    options.minArea = 1.0;
    TraceResult trace = traceRgba(pixels, w, h, options);
    EXPECT_EQ(trace.width, w);
    ASSERT_EQ(trace.shapes.size(), 2u) << "the white background below, the red above";
    EXPECT_EQ(trace.shapes[0].rgb, 0xFFFFFFu) << "the largest color is the bottom layer";
    EXPECT_EQ(trace.shapes[1].rgb, 0xFF0000u);
    BLBox red;
    trace.shapes[1].path.get_bounding_box(&red);
    EXPECT_NEAR(red.x0, 4.0, 1.5);
    EXPECT_NEAR(red.x1, 12.0, 1.5);
    BLBox base;
    trace.shapes[0].path.get_bounding_box(&base);
    EXPECT_GT(base.x1 - base.x0, 15.0);
    // The red layer covers its own pixels only, and the hole in it is empty (the white shows through from below).
    BLPath hole = trace.shapes[1].path;
    EXPECT_TRUE(hole.hit_test(BLPoint(5.5, 5.5), BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN);
    EXPECT_FALSE(hole.hit_test(BLPoint(8.0, 8.0), BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN);

    SketchDocument doc;
    doc.setPageSize(200, 100);
    std::vector<std::size_t> ids = doc.addTrace(trace);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(doc.objectCount(), 2u);
    EXPECT_FALSE(doc.object(0).groups().empty()) << "the layers are one group";
    BLBox all;
    doc.selectionBounds(all);
    EXPECT_GE(all.x0, 0.0);
    EXPECT_LE(all.x1, 200.0);
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 0u);
    EXPECT_TRUE(doc.addTrace(TraceResult()).empty());
    EXPECT_TRUE(traceRgba({}, 0, 0).shapes.empty());
}


TEST(SketchTrace, ARenderedPictureCanBeLoadedAndTraced) {
    SketchDocument source;
    source.setPageSize(120, 120);
    SketchObject& disc = source.add(SketchObject::makeEllipse(60, 60, 40, 40));
    disc.setFill(SketchPaint::solid(SketchColor::fromHex("#d02020")));
    disc.setStroke(SketchPaint::none());
    SketchObject& dot = source.add(SketchObject::makeEllipse(60, 60, 10, 10));
    dot.setFill(SketchPaint::solid(SketchColor::fromHex("#2020d0")));
    dot.setStroke(SketchPaint::none());
    std::string path = (std::filesystem::temp_directory_path() / "sketch_trace_test.png").string();
    ASSERT_TRUE(exportPageImage(source, path, 2.0));
    std::vector<std::uint32_t> pixels;
    int w = 0, h = 0;
    ASSERT_TRUE(loadImagePixels(path, pixels, w, h));
    EXPECT_EQ(w, 240);
    EXPECT_EQ(h, 240);
    TraceOptions options;
    options.colors = 3;
    TraceResult trace = traceRgba(pixels, w, h, options);
    EXPECT_EQ(trace.shapes.size(), 3u) << "white page, red disc, blue dot";
    SketchDocument doc;
    EXPECT_FALSE(doc.addTrace(trace).empty());
    std::filesystem::remove(path);
    EXPECT_FALSE(loadImagePixels((std::filesystem::temp_directory_path() / "no_such_picture.png").string(), pixels, w, h));
}


// ---- clipping masks ----

namespace {
    // A 100 x 100 page: a big red square, then a small circle on top as the would-be mask.
    std::pair<std::size_t, std::size_t> clipFixture(SketchDocument& doc) {
        doc.setPageSize(100, 100);
        SketchObject& square = doc.add(SketchObject::makeRect(0, 0, 100, 100));
        square.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
        square.setStroke(SketchPaint::none());
        std::size_t a = square.id();
        SketchObject& circle = doc.add(SketchObject::makeEllipse(50, 50, 20, 20));
        circle.setFill(SketchPaint::solid(SketchColor::fromHex("#00ff00")));
        circle.setStroke(SketchPaint::none());
        return {a, circle.id()};
    }

    bool isRed(std::uint32_t argb) { return ((argb >> 16) & 255u) > 200u && ((argb >> 8) & 255u) < 60u && (argb & 255u) < 60u; }
}

TEST(SketchClip, MakeCutsTheGroupToTheTopmostObjectAndUndoes) {
    SketchDocument doc;
    auto [square, circle] = clipFixture(doc);
    doc.setSelection({square, circle});
    EXPECT_TRUE(doc.canMakeClippingMask());
    ASSERT_TRUE(doc.makeClippingMask());
    std::size_t group = doc.findById(square)->groups().front();
    EXPECT_TRUE(doc.groupProps(group).clip);
    EXPECT_EQ(doc.clipMaskOf(group), doc.findById(circle));
    EXPECT_TRUE(doc.canReleaseClippingMask());
    EXPECT_FALSE(doc.canMakeClippingMask());

    std::string path = (std::filesystem::temp_directory_path() / "sketch_clip_test.png").string();
    std::vector<std::uint32_t> pixels;
    int w = 0, h = 0;
    ASSERT_TRUE(exportPageImage(doc, path, 1.0));
    ASSERT_TRUE(loadImagePixels(path, pixels, w, h));
    // The mask itself is not drawn (so no green), the square shows only inside the circle.
    EXPECT_TRUE(isRed(pixels[50 * w + 50])) << "inside the mask the square shows";
    EXPECT_FALSE(isRed(pixels[5 * w + 5])) << "outside the mask nothing shows";
    EXPECT_FALSE(isRed(pixels[50 * w + 90]));

    EXPECT_EQ(doc.hitTest(BLPoint(5, 5), 0.0), nullptr) << "clipped away parts cannot be picked";
    EXPECT_NE(doc.hitTest(BLPoint(50, 50), 0.0), nullptr);

    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(square)->groups().empty());
    ASSERT_TRUE(exportPageImage(doc, path, 1.0));
    ASSERT_TRUE(loadImagePixels(path, pixels, w, h));
    EXPECT_TRUE(isRed(pixels[5 * w + 5])) << "undone: the square shows fully";
    doc.undoStack().redo();
    EXPECT_TRUE(doc.groupProps(doc.findById(square)->groups().front()).clip);
    std::filesystem::remove(path);
}

TEST(SketchClip, ReleaseKeepsTheGroupAndUndoes) {
    SketchDocument doc;
    auto [square, circle] = clipFixture(doc);
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.makeClippingMask());
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.releaseClippingMask());
    std::size_t group = doc.findById(square)->groups().front();
    EXPECT_FALSE(doc.groupProps(group).clip);
    EXPECT_EQ(doc.clipMaskOf(group), nullptr);
    EXPECT_NE(doc.hitTest(BLPoint(5, 5), 0.0), nullptr);
    doc.undoStack().undo();
    EXPECT_TRUE(doc.groupProps(group).clip);
    // An existing plain group can be turned into a mask too.
    doc.undoStack().undo();
    doc.undoStack().undo();
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.groupSelection());
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.makeClippingMask());
    EXPECT_TRUE(doc.groupProps(doc.findById(square)->groups().front()).clip);
}

TEST(SketchClip, SurvivesSvgAndBinaryRoundTrips) {
    SketchDocument doc;
    auto [square, circle] = clipFixture(doc);
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.makeClippingMask());

    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_EQ(fromSvg.objectCount(), 2u);
    std::size_t g1 = fromSvg.object(0).groups().front();
    EXPECT_TRUE(fromSvg.groupProps(g1).clip);

    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), fromBin));
    ASSERT_EQ(fromBin.objectCount(), 2u);
    std::size_t g2 = fromBin.object(0).groups().front();
    EXPECT_TRUE(fromBin.groupProps(g2).clip);
    EXPECT_NE(fromBin.clipMaskOf(g2), nullptr);
}

TEST(SketchClip, SelectionBoxIsTheVisiblePart) {
    SketchDocument doc;
    auto [square, circle] = clipFixture(doc);
    doc.setSelection({square, circle});
    ASSERT_TRUE(doc.makeClippingMask());
    doc.setSelection({square});
    BLBox box;
    ASSERT_TRUE(doc.selectionBounds(box));
    EXPECT_NEAR(box.x0, 30.0, 1e-6);
    EXPECT_NEAR(box.x1, 70.0, 1e-6);
    EXPECT_NEAR(box.y0, 30.0, 1e-6);
    EXPECT_NEAR(box.y1, 70.0, 1e-6);
}

TEST(SketchClip, AnOpenPathMasksByItsClosedShapeButALineCannot) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    SketchObject& square = doc.add(SketchObject::makeRect(0, 0, 100, 100));
    square.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    square.setStroke(SketchPaint::none());
    std::size_t a = square.id();
    BLPath triangle;
    triangle.move_to(10, 10);
    triangle.line_to(90, 10);
    triangle.line_to(50, 90);
    std::size_t t = doc.add(SketchObject::makePath(triangle)).id();   // open: no close
    doc.setSelection({a, t});
    ASSERT_TRUE(doc.makeClippingMask());
    EXPECT_NE(doc.hitTest(BLPoint(50, 30), 0.0), nullptr) << "inside the implied triangle";
    EXPECT_EQ(doc.hitTest(BLPoint(10, 80), 0.0), nullptr) << "outside it";

    SketchDocument other;
    other.setPageSize(100, 100);
    std::size_t b = other.add(SketchObject::makeRect(0, 0, 100, 100)).id();
    BLPath line;
    line.move_to(10, 50);
    line.line_to(90, 50);
    std::size_t l = other.add(SketchObject::makePath(line)).id();
    other.setSelection({b, l});
    EXPECT_FALSE(other.canMakeClippingMask()) << "a straight line encloses nothing";
    EXPECT_FALSE(other.makeClippingMask());
}

// ---- artboards ----

TEST(SketchArtboards, AdditionDeletionAndUndo) {
    SketchDocument doc;
    ASSERT_EQ(doc.artboards().size(), 1u);
    EXPECT_EQ(doc.artboards()[0].name, "Artboard 1");
    EXPECT_FALSE(doc.deleteArtboard(0)) << "the last artboard stays";
    std::size_t second = doc.addArtboardBesideLast();
    EXPECT_EQ(second, 1u);
    EXPECT_EQ(doc.activeArtboard(), 1u);
    EXPECT_EQ(doc.artboards()[1].name, "Artboard 2");
    EXPECT_NEAR(doc.artboards()[1].x, 612.0 + 40.0, 1e-9) << "beside the others, with a gap";
    EXPECT_EQ(doc.artboardAt(BLPoint(700, 100)), 1);
    EXPECT_EQ(doc.artboardAt(BLPoint(5, 5)), 0);
    EXPECT_EQ(doc.artboardAt(BLPoint(630, 100)), -1);
    BLBox all = doc.artboardsBounds();
    EXPECT_NEAR(all.x1, 612.0 + 40.0 + 612.0, 1e-9);

    Artboard moved = doc.artboards()[1];
    moved.x = 800;
    moved.width = 100;
    moved.name = "Cover";
    EXPECT_TRUE(doc.setArtboard(1, moved));
    EXPECT_FALSE(doc.setArtboard(1, moved)) << "no change, no step";
    EXPECT_EQ(doc.artboards()[1].name, "Cover");
    EXPECT_FLOAT_EQ(doc.pageWidth(), 100.0f) << "the active artboard is the page";
    doc.undoStack().undo();
    EXPECT_EQ(doc.artboards()[1].name, "Artboard 2");
    doc.undoStack().undo();
    EXPECT_EQ(doc.artboards().size(), 1u);
    doc.undoStack().redo();
    ASSERT_EQ(doc.artboards().size(), 2u);
    EXPECT_TRUE(doc.deleteArtboard(0));
    EXPECT_EQ(doc.artboards().size(), 1u);
    EXPECT_EQ(doc.activeArtboard(), 0u);
    doc.undoStack().undo();
    EXPECT_EQ(doc.artboards().size(), 2u);
}

TEST(SketchArtboards, AlignAndExportFollowTheActiveArtboard) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    doc.addArtboard(Artboard{"Right", 300, 50, 200, 100});
    SketchObject& box = doc.add(SketchObject::makeRect(0, 0, 20, 20));
    box.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    box.setStroke(SketchPaint::none());
    std::size_t id = box.id();
    doc.setSelection({id});
    ASSERT_TRUE(doc.alignSelection(SketchDocument::AlignMode::Center));
    BLBox b = doc.findById(id)->geometryBounds();
    EXPECT_NEAR((b.x0 + b.x1) * 0.5, 400.0, 1e-6) << "centred on the active artboard";

    BLImage image;
    ASSERT_TRUE(renderArtboardImage(doc, 1, 1.0, image));
    EXPECT_EQ(image.width(), 200);
    EXPECT_EQ(image.height(), 100);
    BLImageData data;
    image.get_data(&data);
    auto at = [&data](int x, int y) { return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride)[x]; };
    EXPECT_NE(at(100, 50) & 0x00FF0000u, 0u) << "the square, in the artboard's own pixels";
    BLImage first;
    ASSERT_TRUE(renderArtboardImage(doc, 0, 1.0, first));
    EXPECT_EQ(first.width(), 100);
    EXPECT_FALSE(renderArtboardImage(doc, 2, 1.0, first));
    EXPECT_EQ(artboardFilePath("C:/out/poster.png", "Artboard 2", 2), "C:/out/poster_2_Artboard-2.png");
    EXPECT_EQ(artboardFilePath("poster", "", 1), "poster_1");
}

TEST(SketchArtboards, SurviveSvgAndBinaryRoundTrips) {
    SketchDocument doc;
    doc.addArtboard(Artboard{"Back Cover", 700, 20, 300, 400});
    SketchObject& dot = doc.add(SketchObject::makeRect(710, 30, 10, 10));
    dot.setFill(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    doc.setActiveArtboard(0);

    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_EQ(fromSvg.artboards().size(), 2u);
    EXPECT_EQ(fromSvg.artboards()[1].name, "Back Cover");
    EXPECT_NEAR(fromSvg.artboards()[1].x, 700.0, 1e-6);
    EXPECT_NEAR(fromSvg.artboards()[1].height, 400.0, 1e-6);
    EXPECT_EQ(fromSvg.activeArtboard(), 0u);
    ASSERT_EQ(fromSvg.objectCount(), 1u);
    EXPECT_NEAR(fromSvg.object(0).geometryBounds().x0, 710.0, 1e-6) << "objects keep their canvas position";

    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), fromBin));
    ASSERT_EQ(fromBin.artboards().size(), 2u);
    EXPECT_EQ(fromBin.artboards()[1].name, "Back Cover");
    EXPECT_NEAR(fromBin.artboards()[1].width, 300.0, 1e-9);

    // A plain old file with no artboard list reads as one artboard of its page size.
    SketchDocument plain;
    ASSERT_TRUE(documentFromSvg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200pt\" height=\"100pt\" viewBox=\"0 0 200 100\"></svg>", plain));
    ASSERT_EQ(plain.artboards().size(), 1u);
    EXPECT_NEAR(plain.artboards()[0].width, 200.0, 1e-6);
    EXPECT_NEAR(plain.artboards()[0].height, 100.0, 1e-6);
}

TEST(SketchArtboards, ATraceLandsOnTheActiveArtboard) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    doc.addArtboard(Artboard{"Far", 1000, 500, 100, 100});
    TraceResult trace;
    trace.width = 10;
    trace.height = 10;
    TracedShape shape;
    shape.rgb = 0x336699;
    shape.path.add_rect(0, 0, 10, 10);
    trace.shapes.push_back(shape);
    ASSERT_FALSE(doc.addTrace(trace).empty());
    BLBox b = doc.object(0).geometryBounds();
    EXPECT_GE(b.x0, 1000.0);
    EXPECT_LE(b.x1, 1100.0);
    EXPECT_GE(b.y0, 500.0);
    EXPECT_LE(b.y1, 600.0);
}

// ---- symbols ----

namespace {
    std::uint32_t pixelAt(const SketchDocument& doc, int x, int y) {
        BLImage image;
        if (!renderPageImage(doc, 1.0, image)) {
            return 0;
        }
        BLImageData data;
        image.get_data(&data);
        return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride)[x];
    }

    // A 100 x 100 page with one red 20 x 20 square at (10, 10), selected.
    std::size_t symbolFixture(SketchDocument& doc) {
        doc.setPageSize(100, 100);
        SketchObject& square = doc.add(SketchObject::makeRect(10, 10, 20, 20));
        square.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
        square.setStroke(SketchPaint::none());
        doc.setSelection({square.id()});
        return square.id();
    }
}

TEST(SketchSymbols, MakeReplacesTheSelectionWithAnInstanceAndUndoes) {
    SketchDocument doc;
    symbolFixture(doc);
    std::size_t id = doc.makeSymbol("Square");
    ASSERT_NE(id, 0u);
    ASSERT_EQ(doc.symbols().size(), 1u);
    EXPECT_EQ(doc.symbols()[0]->name, "Square");
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_TRUE(doc.object(0).isSymbol());
    EXPECT_EQ(doc.symbolUseCount(id), 1u);
    EXPECT_TRUE(isRed(pixelAt(doc, 20, 20))) << "the instance draws the artwork where it was";
    EXPECT_FALSE(isRed(pixelAt(doc, 60, 60)));
    EXPECT_NE(doc.hitTest(BLPoint(20, 20), 0.0), nullptr);
    doc.undoStack().undo();
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_FALSE(doc.object(0).isSymbol());
    doc.undoStack().redo();
    EXPECT_TRUE(doc.object(0).isSymbol());
    EXPECT_EQ(doc.symbolUseCount(id), 1u);
}

TEST(SketchSymbols, InstancesFollowTransformsAndRedefinition) {
    SketchDocument doc;
    symbolFixture(doc);
    std::size_t id = doc.makeSymbol("");
    EXPECT_EQ(doc.symbols()[0]->name, "Symbol 1");
    std::size_t second = doc.placeSymbol(id, BLPoint(70, 70));
    ASSERT_NE(second, 0u);
    EXPECT_EQ(doc.symbolUseCount(id), 2u);
    EXPECT_TRUE(isRed(pixelAt(doc, 70, 70))) << "centred where it was placed";

    // Transforms act on the instance like on any object (here a scale about its centre to 2 x).
    SketchObject* instance = doc.findById(second);
    BLBox before = instance->geometryBounds();
    BLMatrix2D scale = BLMatrix2D::make_translation(-70, -70);
    scale.post_scale(2.0, 2.0);
    scale.post_translate(70, 70);
    doc.transformObjects({second}, scale, "Scale");
    BLBox after = doc.findById(second)->geometryBounds();
    EXPECT_NEAR(after.x1 - after.x0, 2.0 * (before.x1 - before.x0), 1e-6);
    EXPECT_TRUE(isRed(pixelAt(doc, 85, 70))) << "the artwork is carried along with the instance";

    // Redefine: the selection (a blue circle) becomes the artwork, both instances follow and keep their place.
    SketchObject& blue = doc.add(SketchObject::makeEllipse(60, 20, 10, 10));
    blue.setFill(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    blue.setStroke(SketchPaint::none());
    doc.setSelection({blue.id()});
    ASSERT_TRUE(doc.redefineSymbol(id));
    EXPECT_EQ(doc.symbolUseCount(id), 2u);
    EXPECT_FALSE(isRed(pixelAt(doc, 20, 20))) << "the first instance now shows the new artwork";
    EXPECT_FALSE(isRed(pixelAt(doc, 70, 70)));
    doc.undoStack().undo();
    EXPECT_TRUE(isRed(pixelAt(doc, 70, 70))) << "undone: the old artwork is back";
    doc.setSelection({doc.object(0).id()});
    EXPECT_FALSE(doc.redefineSymbol(id)) << "a symbol cannot be redefined from its own instance";
}

TEST(SketchSymbols, BreakLinkAndLibraryHousekeeping) {
    SketchDocument doc;
    symbolFixture(doc);
    std::size_t id = doc.makeSymbol("Box");
    doc.setSelection({doc.object(0).id()});
    EXPECT_TRUE(doc.canBreakSymbolLink());
    EXPECT_FALSE(doc.deleteSymbol(id)) << "a symbol in use stays";
    ASSERT_TRUE(doc.breakSymbolLink());
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_FALSE(doc.object(0).isSymbol());
    BLBox b = doc.object(0).geometryBounds();
    EXPECT_NEAR(b.x0, 10.0, 1e-6);
    EXPECT_NEAR(b.x1, 30.0, 1e-6);
    EXPECT_TRUE(isRed(pixelAt(doc, 20, 20)));
    EXPECT_EQ(doc.symbolUseCount(id), 0u);
    EXPECT_TRUE(doc.renameSymbol(id, "Crate"));
    EXPECT_EQ(doc.findSymbol(id)->name, "Crate");
    doc.undoStack().undo();
    EXPECT_EQ(doc.findSymbol(id)->name, "Box");
    ASSERT_TRUE(doc.deleteSymbol(id));
    EXPECT_TRUE(doc.symbols().empty());
    doc.undoStack().undo();
    EXPECT_EQ(doc.symbols().size(), 1u);
}

TEST(SketchSymbols, SurviveSvgBinaryAndClipboardRoundTrips) {
    SketchDocument doc;
    symbolFixture(doc);
    std::size_t id = doc.makeSymbol("Box");
    std::size_t second = doc.placeSymbol(id, BLPoint(70, 70));
    ASSERT_NE(second, 0u);

    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_EQ(fromSvg.symbols().size(), 1u);
    EXPECT_EQ(fromSvg.symbols()[0]->name, "Box");
    EXPECT_EQ(fromSvg.symbolUseCount(fromSvg.symbols()[0]->id), 2u);
    EXPECT_TRUE(isRed(pixelAt(fromSvg, 70, 70)));
    EXPECT_TRUE(isRed(pixelAt(fromSvg, 20, 20)));

    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), fromBin));
    ASSERT_EQ(fromBin.symbols().size(), 1u);
    EXPECT_EQ(fromBin.symbolUseCount(fromBin.symbols()[0]->id), 2u);
    EXPECT_TRUE(isRed(pixelAt(fromBin, 70, 70)));

    // Copy and paste into another document brings the artwork along, once.
    doc.setSelection({second});
    std::string clip = doc.selectionToSvg();
    SketchDocument other;
    other.setPageSize(100, 100);
    ASSERT_TRUE(other.pasteSvg(clip, 0, 0));
    ASSERT_EQ(other.symbols().size(), 1u);
    EXPECT_TRUE(isRed(pixelAt(other, 70, 70)));
    ASSERT_TRUE(other.pasteSvg(clip, 5, 5));
    EXPECT_EQ(other.symbols().size(), 1u) << "the same artwork is not added twice";
    EXPECT_EQ(other.symbolUseCount(other.symbols()[0]->id), 2u);
}

// ---- patterns ----

TEST(SketchPatterns, MakeApplyAndTile) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    // The tile: a 10 x 10 box with a red 5 x 5 square in its top left corner.
    doc.add(SketchObject::makeRect(0, 0, 10, 10)).setFill(SketchPaint::none());
    SketchObject& dot = doc.add(SketchObject::makeRect(0, 0, 5, 5));
    dot.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    dot.setStroke(SketchPaint::none());
    doc.object(0).setStroke(SketchPaint::none());
    doc.selectAll();
    std::size_t id = doc.makePattern("Dots");
    ASSERT_NE(id, 0u);
    EXPECT_EQ(doc.patterns().size(), 1u);
    EXPECT_EQ(doc.objectCount(), 2u) << "making a pattern leaves the artwork";
    doc.undoStack().undo();
    EXPECT_TRUE(doc.patterns().empty());
    doc.undoStack().redo();
    ASSERT_EQ(doc.patterns().size(), 1u);

    doc.clearSelection();
    SketchObject& area = doc.add(SketchObject::makeRect(20, 20, 60, 60));
    area.setStroke(SketchPaint::none());
    std::size_t areaId = area.id();
    doc.setSelection({areaId});
    ASSERT_TRUE(doc.applyPattern(id, false));
    EXPECT_TRUE(doc.findById(areaId)->fill().isPattern());
    EXPECT_EQ(doc.patternUseCount(id), 1u);
    EXPECT_FALSE(doc.deletePattern(id)) << "in use";
    // The red square repeats every 10 points from the origin: (21, 21) is inside one, (26, 21) is not.
    EXPECT_TRUE(isRed(pixelAt(doc, 22, 22)));
    EXPECT_FALSE(isRed(pixelAt(doc, 27, 22)));
    EXPECT_TRUE(isRed(pixelAt(doc, 32, 22)));

    ASSERT_TRUE(doc.setPatternTransform(2.0, 0.0));
    EXPECT_DOUBLE_EQ(doc.findById(areaId)->fill().patternScale(), 2.0);
    EXPECT_TRUE(isRed(pixelAt(doc, 22, 22)));
    EXPECT_TRUE(isRed(pixelAt(doc, 27, 22))) << "at twice the size the square is 10 points wide";
    EXPECT_FALSE(isRed(pixelAt(doc, 32, 22)));
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(areaId)->fill().patternScale(), 1.0);
    EXPECT_NE(doc.selectedPatternPaint(), nullptr);
}

TEST(SketchPatterns, SurviveSvgBinaryAndClipboardRoundTrips) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    SketchObject& dot = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    dot.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    dot.setStroke(SketchPaint::none());
    doc.selectAll();
    std::size_t id = doc.makePattern("Red");
    SketchObject& area = doc.add(SketchObject::makeRect(20, 20, 60, 60));
    area.setStroke(SketchPaint::none());
    doc.setSelection({area.id()});
    ASSERT_TRUE(doc.applyPattern(id, false));
    doc.setPatternTransform(1.5, 0.0);

    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_EQ(fromSvg.patterns().size(), 1u);
    EXPECT_EQ(fromSvg.patterns()[0]->name, "Red");
    ASSERT_EQ(fromSvg.objectCount(), 2u);
    EXPECT_TRUE(fromSvg.object(1).fill().isPattern());
    EXPECT_DOUBLE_EQ(fromSvg.object(1).fill().patternScale(), 1.5);
    EXPECT_TRUE(fromSvg.object(1).fill().patternDef() != nullptr);
    EXPECT_TRUE(isRed(pixelAt(fromSvg, 60, 60)));

    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), fromBin));
    ASSERT_EQ(fromBin.patterns().size(), 1u);
    ASSERT_EQ(fromBin.objectCount(), 2u);
    EXPECT_TRUE(fromBin.object(1).fill().isPattern());
    EXPECT_TRUE(fromBin.object(1).fill().patternDef() != nullptr);
    EXPECT_TRUE(isRed(pixelAt(fromBin, 60, 60)));

    doc.setSelection({doc.object(1).id()});
    std::string clip = doc.selectionToSvg();
    SketchDocument other;
    other.setPageSize(100, 100);
    ASSERT_TRUE(other.pasteSvg(clip, 0, 0));
    ASSERT_TRUE(other.pasteSvg(clip, 3, 3));
    EXPECT_EQ(other.patterns().size(), 1u);
    EXPECT_TRUE(other.object(0).fill().isPattern());
    EXPECT_EQ(other.object(0).fill().patternDef(), other.object(1).fill().patternDef());
}

TEST(SketchSymbols, ASymbolCanHoldAPatternFill) {
    SketchDocument doc;
    doc.setPageSize(100, 100);
    SketchObject& tile = doc.add(SketchObject::makeRect(10, 10, 40, 40));
    tile.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    doc.selectAll();
    std::size_t pattern = doc.makePattern("");
    ASSERT_TRUE(doc.applyPattern(pattern, false));
    ASSERT_NE(doc.makeSymbol(""), 0u);
    EXPECT_TRUE(isRed(pixelAt(doc, 20, 20)));
}

// ---- Brushes ------------------------------------------------------------------------------------------

namespace {
    std::shared_ptr<BrushDef> calligraphicBrush(double angle, double roundness) {
        auto brush = std::make_shared<BrushDef>();
        brush->id = 1;
        brush->name = "Nib";
        brush->angle = angle;
        brush->roundness = roundness;
        return brush;
    }

    // A brush whose artwork is one black rectangle of the given size.
    std::shared_ptr<BrushDef> rectangleBrush(BrushKind kind, double w, double h) {
        auto art = std::make_shared<SymbolDef>();
        art->box = BLBox(0, 0, w, h);
        art->content = std::make_shared<SketchDocument>();
        SketchObject* rect = SketchObject::makeRect(0, 0, w, h);
        rect->setFill(SketchPaint::solid(SketchColor::gray(0.0f)));
        rect->setStroke(SketchPaint::none());
        art->content->add(rect);
        auto brush = std::make_shared<BrushDef>();
        brush->id = 2;
        brush->name = "Block";
        brush->kind = kind;
        brush->art = art;
        return brush;
    }

    // A horizontal line from x = 10 to 90 at y = 50, stroked with `brush` at `weight`.
    std::unique_ptr<SketchObject> brushedLine(const std::shared_ptr<const BrushDef>& brush, double weight) {
        std::unique_ptr<SketchObject> line(SketchObject::makeLine(10, 50, 90, 50));
        line->setStroke(SketchPaint::solid(SketchColor::gray(0.0f)));
        line->setStrokeWidth(weight);
        StrokeOptions options;
        options.brush = brush;
        line->setStrokeOptions(options);
        return line;
    }

    bool inked(const std::vector<std::uint32_t>& pixels, int x, int y) { return (pixels[std::size_t(y) * 100 + std::size_t(x)] >> 24) > 128u; }

    BLBox boxOfPath(const BLPath& path) {
        BLBox box(0, 0, 0, 0);
        path.get_bounding_box(&box);
        return box;
    }
}

TEST(SketchBrushes, ANibSweepsOutAnOval) {
    BLPath line;
    line.move_to(10, 50);
    line.line_to(90, 50);
    // A flat nib 10 across and 1 thick, then the same turned upright.
    BLBox flat = boxOfPath(calligraphicBrush(0.0, 10.0)->nibOutline(line, 10.0));
    EXPECT_NEAR(flat.x0, 5.0, 1e-6);
    EXPECT_NEAR(flat.x1, 95.0, 1e-6);
    EXPECT_NEAR(flat.y1 - flat.y0, 1.0, 0.05);
    BLBox upright = boxOfPath(calligraphicBrush(90.0, 10.0)->nibOutline(line, 10.0));
    EXPECT_NEAR(upright.y1 - upright.y0, 10.0, 1e-6);
    EXPECT_NEAR(upright.x1 - upright.x0, 81.0, 0.05);
}

TEST(SketchBrushes, ACalligraphicStrokeIsThickOrThinWithTheDirection) {
    std::unique_ptr<SketchObject> line = brushedLine(calligraphicBrush(90.0, 10.0), 20.0);   // an upright nib drawing a horizontal line
    std::vector<std::uint32_t> px = render(*line, 100, 100);
    EXPECT_TRUE(inked(px, 50, 50));
    EXPECT_TRUE(inked(px, 50, 58));    // 20 tall at the middle
    EXPECT_FALSE(inked(px, 50, 65));
    BLBox box = line->bounds();
    EXPECT_NEAR(box.y0, 40.0, 1e-6);   // the bounds allow for the nib
    EXPECT_NEAR(box.y1, 60.0, 1e-6);
    std::unique_ptr<SketchObject> thin = brushedLine(calligraphicBrush(0.0, 10.0), 20.0);   // a flat nib: the same line comes out thin
    px = render(*thin, 100, 100);
    EXPECT_TRUE(inked(px, 50, 50));
    EXPECT_FALSE(inked(px, 50, 56));
}

TEST(SketchBrushes, AnArtBrushStretchesItsArtworkAlongThePath) {
    std::shared_ptr<BrushDef> brush = rectangleBrush(BrushKind::Art, 100, 6);
    std::vector<std::uint32_t> px = render(*brushedLine(brush, 1.0), 100, 100);   // the 100 x 6 block squeezed onto 80 long
    EXPECT_TRUE(inked(px, 50, 50));
    EXPECT_TRUE(inked(px, 50, 52));
    EXPECT_FALSE(inked(px, 50, 56));
    EXPECT_TRUE(inked(px, 12, 50));
    EXPECT_FALSE(inked(px, 5, 50));    // it ends where the path ends
    px = render(*brushedLine(brush, 2.0), 100, 100);   // weight 2: twice as thick
    EXPECT_TRUE(inked(px, 50, 55));
    EXPECT_FALSE(inked(px, 50, 58));
    EXPECT_NEAR(brushedLine(brush, 2.0)->bounds().y1, 56.0, 1e-6);
}

TEST(SketchBrushes, AnArtBrushBendsWithACurve) {
    std::shared_ptr<BrushDef> brush = rectangleBrush(BrushKind::Art, 100, 6);
    std::unique_ptr<SketchObject> arc(SketchObject::makeLine(10, 90, 90, 90));
    BLPath path;
    path.move_to(10, 90);
    path.quad_to(50, 10, 90, 90);   // up and over: it peaks at y = 50
    SketchObject::Prototype::setOutline(*arc, path);
    arc->setStroke(SketchPaint::solid(SketchColor::gray(0.0f)));
    arc->setStrokeWidth(1.0);
    StrokeOptions options;
    options.brush = brush;
    arc->setStrokeOptions(options);
    std::vector<std::uint32_t> px = render(*arc, 100, 100);
    EXPECT_TRUE(inked(px, 50, 50));
    EXPECT_FALSE(inked(px, 50, 70));   // a straight block would have been drawn across the middle
    EXPECT_TRUE(inked(px, 20, 70));    // the start of the curve, still low down
}

TEST(SketchBrushes, AScatterBrushStandsCopiesAlongThePathAndStaysPut) {
    std::shared_ptr<BrushDef> brush = rectangleBrush(BrushKind::Scatter, 4, 4);
    brush->spacing = 250.0;   // 10 apart
    brush->followPath = false;
    BLPath path;
    path.move_to(10, 50);
    path.line_to(90, 50);
    std::vector<BLMatrix2D> first = brush->placements(path, 1.0);
    EXPECT_EQ(first.size(), 9u);
    std::vector<BLMatrix2D> again = brush->placements(path, 1.0);
    ASSERT_EQ(again.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_DOUBLE_EQ(first[i].m20, again[i].m20);   // the same places every time
    }
    std::vector<std::uint32_t> px = render(*brushedLine(brush, 1.0), 100, 100);
    EXPECT_TRUE(inked(px, 10, 50));    // a copy centred on the start
    EXPECT_TRUE(inked(px, 20, 50));
    EXPECT_FALSE(inked(px, 15, 50));   // and gaps between
    EXPECT_FALSE(inked(px, 50, 60));
    brush->scatter = 100.0;   // strays up to the artwork height from the path
    brush->sizeVariation = 50.0;
    std::vector<BLMatrix2D> strewn = brush->placements(path, 1.0);
    bool moved = false;
    for (const BLMatrix2D& m : strewn) {
        moved = moved || std::fabs(m.m21 - 48.0) > 0.01;   // the centre of a copy that stayed on the line would sit at y = 50 (m21 = 50 - 2)
    }
    EXPECT_TRUE(moved);
}

TEST(SketchBrushes, TheNumbersAreKeptInRange) {
    BrushDef brush;
    brush.setParams({1000.0, -4.0, 0.0, 9999.0, 150.0, 400.0, 0.0});
    EXPECT_DOUBLE_EQ(brush.angle, 180.0);
    EXPECT_DOUBLE_EQ(brush.roundness, 1.0);
    EXPECT_DOUBLE_EQ(brush.spacing, 5.0);
    EXPECT_DOUBLE_EQ(brush.scatter, 500.0);
    EXPECT_DOUBLE_EQ(brush.sizeVariation, 100.0);
    EXPECT_DOUBLE_EQ(brush.rotation, 180.0);
    EXPECT_FALSE(brush.followPath);
}

TEST(SketchBrushes, TheLibraryMakesAppliesAndEditsBrushesAsUndoSteps) {
    SketchDocument doc;
    std::size_t id = doc.add(SketchObject::makeLine(0, 0, 100, 0)).id();
    doc.setSelection({id});
    std::size_t brush = doc.makeBrush(BrushKind::Calligraphic, "");
    ASSERT_NE(brush, 0u);
    EXPECT_EQ(doc.brushes().size(), 1u);
    EXPECT_EQ(doc.activeBrush(), brush);
    EXPECT_TRUE(doc.applyBrush(brush));
    ASSERT_TRUE(doc.findById(id)->strokeOptions().brush != nullptr);
    EXPECT_EQ(doc.findById(id)->strokeOptions().brush->id, brush);
    EXPECT_EQ(doc.brushUseCount(brush), 1u);
    EXPECT_FALSE(doc.deleteBrush(brush));   // in use
    EXPECT_FALSE(doc.applyBrush(brush));    // nothing changes
    BrushDef edited = *doc.findBrush(brush);
    edited.name = "Chisel";
    edited.angle = 10.0;
    EXPECT_TRUE(doc.changeBrush(brush, edited));
    EXPECT_EQ(doc.findById(id)->strokeOptions().brush->name, "Chisel");   // the stroke follows the edit
    EXPECT_DOUBLE_EQ(doc.findById(id)->strokeOptions().brush->angle, 10.0);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->strokeOptions().brush->name, "Calligraphic Brush 1");
    EXPECT_TRUE(doc.applyBrush(0));   // back to the plain stroke
    EXPECT_TRUE(doc.findById(id)->strokeOptions().brush == nullptr);
    EXPECT_TRUE(doc.deleteBrush(brush));
    EXPECT_TRUE(doc.brushes().empty());
    doc.undoStack().undo();
    EXPECT_EQ(doc.brushes().size(), 1u);
}

TEST(SketchBrushes, AnArtBrushTakesTheSelectionAsItsArtwork) {
    SketchDocument doc;
    EXPECT_EQ(doc.makeBrush(BrushKind::Art, ""), 0u);   // nothing selected
    std::size_t id = doc.add(SketchObject::makeRect(0, 0, 60, 10)).id();
    doc.setSelection({id});
    std::size_t brush = doc.makeBrush(BrushKind::Art, "Bar");
    ASSERT_NE(brush, 0u);
    std::shared_ptr<const BrushDef> def = doc.findBrush(brush);
    ASSERT_TRUE(def && def->art && def->art->content);
    EXPECT_EQ(def->art->content->objectCount(), 1u);
    EXPECT_NEAR(def->art->box.x1 - def->art->box.x0, 60.0, 2.0);   // the box of the artwork (its stroke counts too)
    EXPECT_EQ(doc.objectCount(), 1u);                              // the selection stays
}

TEST(SketchBrushes, PresetsAreAddedOnceByName) {
    SketchDocument doc;
    EXPECT_EQ(doc.addBrushPresets(), 6u);
    EXPECT_EQ(doc.brushes().size(), 6u);
    EXPECT_EQ(doc.addBrushPresets(), 0u);
    bool art = false, scatter = false, nib = false;
    for (const std::shared_ptr<const BrushDef>& brush : doc.brushes()) {
        art = art || (brush->kind == BrushKind::Art && brush->art != nullptr);
        scatter = scatter || (brush->kind == BrushKind::Scatter && brush->art != nullptr);
        nib = nib || brush->kind == BrushKind::Calligraphic;
    }
    EXPECT_TRUE(art && scatter && nib);
    doc.undoStack().undo();
    EXPECT_TRUE(doc.brushes().empty());
}

TEST(SketchBrushes, SurviveSvgBinaryAndClipboardRoundTrips) {
    SketchDocument doc;
    std::size_t bar = doc.add(SketchObject::makeRect(0, 0, 60, 10)).id();
    std::size_t line = doc.add(SketchObject::makeLine(0, 50, 100, 50)).id();
    doc.setSelection({bar});
    std::size_t art = doc.makeBrush(BrushKind::Scatter, "Bars");
    doc.setSelection({line});
    std::size_t nib = doc.makeBrush(BrushKind::Calligraphic, "Nib");
    BrushDef edited = *doc.findBrush(nib);
    edited.angle = 30.0;
    edited.roundness = 40.0;
    ASSERT_TRUE(doc.changeBrush(nib, edited));
    ASSERT_TRUE(doc.applyBrush(nib));
    ASSERT_NE(art, 0u);

    auto check = [&](const SketchDocument& d, const char* how) {
        SCOPED_TRACE(how);
        ASSERT_EQ(d.brushes().size(), 2u);
        const SketchObject* stroked = nullptr;
        for (std::size_t i = 0; i < d.objectCount(); ++i) {
            if (d.object(i).strokeOptions().brush) {
                stroked = &d.object(i);
            }
        }
        ASSERT_NE(stroked, nullptr);
        EXPECT_EQ(stroked->strokeOptions().brush->name, "Nib");
        EXPECT_DOUBLE_EQ(stroked->strokeOptions().brush->angle, 30.0);
        EXPECT_DOUBLE_EQ(stroked->strokeOptions().brush->roundness, 40.0);
        std::shared_ptr<const BrushDef> bars;
        for (const std::shared_ptr<const BrushDef>& brush : d.brushes()) {
            if (brush->kind == BrushKind::Scatter) {
                bars = brush;
            }
        }
        ASSERT_TRUE(bars && bars->art && bars->art->content);
        EXPECT_EQ(bars->name, "Bars");
        EXPECT_EQ(bars->art->content->objectCount(), 1u);
        EXPECT_DOUBLE_EQ(bars->spacing, 150.0);
    };
    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    check(fromSvg, "svg");
    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), fromBin));
    check(fromBin, "binary");

    // Copy and paste: the brush comes along, once however often it is pasted.
    doc.setSelection({line});
    std::string clip = doc.selectionToSvg();
    SketchDocument other;
    ASSERT_TRUE(other.pasteSvg(clip, 5, 5));
    ASSERT_TRUE(other.pasteSvg(clip, 10, 10));
    EXPECT_EQ(other.brushes().size(), 1u);
    EXPECT_EQ(other.brushUseCount(other.brushes().front()->id), 2u);
}

TEST(SketchBrushes, ABrushedStrokeOfAnArtBrushCanBePartOfBrushArtwork) {
    // The artwork of a brush that holds brushed strokes must carry those brushes with it, or saving it would name brushes it does not have.
    SketchDocument doc;
    std::size_t line = doc.add(SketchObject::makeLine(0, 0, 100, 0)).id();
    doc.setSelection({line});
    ASSERT_NE(doc.makeBrush(BrushKind::Calligraphic, ""), 0u);
    ASSERT_TRUE(doc.applyBrush(doc.activeBrush()));
    ASSERT_NE(doc.makeBrush(BrushKind::Scatter, "Nested"), 0u);
    SketchDocument back;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), back));
    ASSERT_EQ(back.brushes().size(), 2u);
    for (const std::shared_ptr<const BrushDef>& brush : back.brushes()) {
        if (brush->kind == BrushKind::Scatter) {
            ASSERT_TRUE(brush->art && brush->art->content);
            ASSERT_EQ(brush->art->content->objectCount(), 1u);
            EXPECT_TRUE(brush->art->content->object(0).strokeOptions().brush != nullptr);
        }
    }
}

// ---- Layers: colour, duplicate, merge, flatten, objects in the tree ------------------------------------

TEST(SketchLayers, ColourIsAutomaticUntilSetAndSurvivesSaving) {
    SketchDocument doc;
    const std::size_t first = doc.activeLayer();
    const std::size_t second = doc.addLayer(0);
    EXPECT_NE(layerColor(*doc.findLayer(first)), layerColor(*doc.findLayer(second)));   // each layer gets one of the nine in turn
    EXPECT_EQ(doc.findLayer(first)->color, 0u);
    EXPECT_TRUE(doc.setLayerColor(second, 0x112233));
    EXPECT_FALSE(doc.setLayerColor(second, 0x112233));
    EXPECT_EQ(layerColor(*doc.findLayer(second)), 0x112233u);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findLayer(second)->color, 0u);
    doc.undoStack().redo();
    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_EQ(fromSvg.layers().size(), 2u);
    EXPECT_EQ(fromSvg.layers()[1].color, 0x112233u);
    EXPECT_EQ(fromSvg.layers()[0].color, 0u);
    SketchDocument fromBin;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), fromBin));
    ASSERT_EQ(fromBin.layers().size(), 2u);
    EXPECT_EQ(fromBin.layers()[1].color, 0x112233u);
    EXPECT_EQ(fromBin.layers()[0].color, 0u);
}

TEST(SketchLayers, DuplicateCopiesTheLayerItsSublayersAndObjectsWithGroupsOfTheirOwn) {
    SketchDocument doc;
    const std::size_t base = doc.activeLayer();
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    const std::size_t sub = doc.addLayer(base, "Sub");
    const std::size_t dot = doc.add(SketchObject::makeEllipse(5, 5, 3, 3)).id();
    ASSERT_EQ(doc.findById(dot)->layer(), sub);
    const std::size_t copy = doc.duplicateLayer(base);
    ASSERT_NE(copy, 0u);
    EXPECT_EQ(doc.layers().size(), 4u);
    EXPECT_EQ(doc.findLayer(copy)->name, "Layer 1 copy");
    EXPECT_EQ(doc.activeLayer(), copy);
    EXPECT_EQ(doc.layerObjectCount(copy, true), 3u);
    EXPECT_EQ(doc.layerObjectCount(base, true), 3u);
    EXPECT_EQ(doc.layerChildren(copy).size(), 1u);   // the sublayer came along
    // The copy's group is not the original's.
    std::size_t original = doc.findById(a)->groups().front(), copied = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        if (doc.object(i).layer() == copy && !doc.object(i).groups().empty()) { copied = doc.object(i).groups().front(); }
    }
    EXPECT_NE(copied, 0u);
    EXPECT_NE(copied, original);
    EXPECT_EQ(doc.groupMembers(original).size(), 2u);
    doc.undoStack().undo();
    EXPECT_EQ(doc.layers().size(), 2u);
    EXPECT_EQ(doc.objectCount(), 3u);
}

TEST(SketchLayers, MergeFlattenAndTheShowHideEditsAreUndoSteps) {
    SketchDocument doc;
    const std::size_t one = doc.activeLayer();
    doc.add(SketchObject::makeRect(0, 0, 10, 10));
    const std::size_t two = doc.addLayer(0);
    doc.add(SketchObject::makeRect(20, 0, 10, 10));
    const std::size_t three = doc.addLayer(0);
    doc.add(SketchObject::makeRect(40, 0, 10, 10));
    EXPECT_FALSE(doc.mergeLayerDown(one));   // nothing behind it
    EXPECT_TRUE(doc.hideOtherLayers(three));
    EXPECT_TRUE(!doc.findLayer(one)->visible && !doc.findLayer(two)->visible && doc.findLayer(three)->visible);
    EXPECT_TRUE(doc.showAllLayers());
    EXPECT_FALSE(doc.showAllLayers());
    EXPECT_TRUE(doc.lockOtherLayers(two));
    EXPECT_TRUE(doc.findLayer(one)->locked && doc.findLayer(three)->locked && !doc.findLayer(two)->locked);
    EXPECT_TRUE(doc.unlockAllLayers());
    EXPECT_TRUE(doc.mergeLayerDown(three));
    EXPECT_EQ(doc.layers().size(), 2u);
    EXPECT_EQ(doc.layerObjectCount(two, false), 2u);
    EXPECT_EQ(doc.activeLayer(), two);
    doc.undoStack().undo();
    EXPECT_EQ(doc.layers().size(), 3u);
    EXPECT_EQ(doc.layerObjectCount(three, false), 1u);
    EXPECT_TRUE(doc.flattenArtwork());
    EXPECT_EQ(doc.layers().size(), 1u);
    EXPECT_EQ(doc.layerObjectCount(one, false), 3u);
    EXPECT_FALSE(doc.flattenArtwork());
    doc.undoStack().undo();
    EXPECT_EQ(doc.layers().size(), 3u);
}

TEST(SketchLayers, LayersMoveOneStepAmongTheirSiblings) {
    SketchDocument doc;
    const std::size_t one = doc.activeLayer();
    const std::size_t two = doc.addLayer(0);
    const std::size_t three = doc.addLayer(0);
    EXPECT_EQ(doc.layerChildren(0), (std::vector<std::size_t>{one, two, three}));
    EXPECT_FALSE(doc.moveLayerStep(three, true));
    EXPECT_TRUE(doc.moveLayerStep(one, true));
    EXPECT_EQ(doc.layerChildren(0), (std::vector<std::size_t>{two, one, three}));
    EXPECT_TRUE(doc.moveLayerStep(three, false));
    EXPECT_EQ(doc.layerChildren(0), (std::vector<std::size_t>{two, three, one}));
    EXPECT_FALSE(doc.moveLayerStep(two, false));
}

TEST(SketchLayers, ObjectsMoveBetweenLayersAndNextToOthersAndLeaveTheirGroups) {
    SketchDocument doc;
    const std::size_t one = doc.activeLayer();
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    const std::size_t c = doc.add(SketchObject::makeRect(40, 0, 10, 10)).id();
    const std::size_t two = doc.addLayer(0);
    auto order = [&]() {
        std::vector<std::size_t> ids;
        for (std::size_t i = 0; i < doc.objectCount(); ++i) { ids.push_back(doc.object(i).id()); }
        return ids;
    };
    // Behind `c`: a goes after b ... in front of c and behind nothing: a lands on top of its layer.
    EXPECT_TRUE(doc.moveObjects({a}, 0, {c}, true));
    EXPECT_EQ(order(), (std::vector<std::size_t>{b, c, a}));
    EXPECT_FALSE(doc.moveObjects({a}, 0, {a}, true));   // onto itself
    EXPECT_TRUE(doc.moveObjects({a}, 0, {b}, false));
    EXPECT_EQ(order(), (std::vector<std::size_t>{a, b, c}));
    // Into another layer: on top of it, and the order follows the layers.
    EXPECT_TRUE(doc.moveObjects({b}, two, {}, false));
    EXPECT_EQ(doc.findById(b)->layer(), two);
    EXPECT_EQ(order().back(), b);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(b)->layer(), one);
    EXPECT_EQ(order(), (std::vector<std::size_t>{a, b, c}));
    // Taking a member out of its group.
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    EXPECT_FALSE(doc.findById(a)->groups().empty());
    EXPECT_TRUE(doc.moveObjects({a}, two, {}, false, std::vector<std::size_t>{}, std::numeric_limits<std::size_t>::max()));
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    EXPECT_FALSE(doc.findById(b)->groups().empty());
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(a)->groups().empty());
    EXPECT_EQ(doc.findById(a)->layer(), one);
}

TEST(SketchLayers, ObjectsHaveTheirOwnEyeLockAndName) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    EXPECT_TRUE(doc.setObjectsVisible({a}, false));
    EXPECT_FALSE(doc.findById(a)->ownVisible());
    EXPECT_TRUE(doc.selection().empty());   // a hidden object leaves the selection
    EXPECT_FALSE(doc.setObjectsVisible({a}, false));
    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(a)->ownVisible());
    EXPECT_TRUE(doc.setObjectsLocked({a}, true));
    EXPECT_TRUE(doc.findById(a)->ownLocked());
    EXPECT_TRUE(doc.renameObject(a, "Sun"));
    EXPECT_EQ(doc.findById(a)->name(), "Sun");
    EXPECT_FALSE(doc.renameObject(a, "Sun"));
    doc.undoStack().undo();
    EXPECT_NE(doc.findById(a)->name(), "Sun");
}

TEST(SketchBinary, OutlinesAreStoredAsFloatStepsInHalfTheSpaceAndOldFilesStillRead) {
    SketchDocument doc;
    BLPath wobble;
    wobble.move_to(10.123456789, 20.987654321);
    for (int i = 1; i < 200; ++i) {
        wobble.cubic_to(10.0 + i * 3.1, 20.0 + std::sin(i) * 40.0, 12.0 + i * 3.1, 80.0 + std::cos(i) * 40.0, 14.0 + i * 3.1, 50.0 + std::sin(i * 0.3) * 30.0);
    }
    wobble.close();
    doc.add(SketchObject::makePath(wobble));
    doc.add(SketchObject::makeRect(0.25, 0.5, 100.0, 50.0));   // values a float holds exactly come back exactly
    doc.add(SketchObject::makeEllipse(300, 300, 40, 25));
    std::string v1 = binaryFromDocument(doc, false, 1), v2 = binaryFromDocument(doc, false, 2);
    EXPECT_LT(v2.size(), v1.size() * 6 / 10) << "the vertices take half";
    SketchDocument fromV1, fromV2;
    ASSERT_TRUE(documentFromBinary(v1, fromV1));
    ASSERT_TRUE(documentFromBinary(v2, fromV2));
    ASSERT_EQ(fromV1.objectCount(), 3u);
    ASSERT_EQ(fromV2.objectCount(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        BLPathView original = doc.object(i).geometry().view(), old = fromV1.object(i).geometry().view(), now = fromV2.object(i).geometry().view();
        ASSERT_EQ(old.size, original.size);
        ASSERT_EQ(now.size, original.size);
        for (std::size_t k = 0; k < original.size; ++k) {
            ASSERT_EQ(now.command_data[k], original.command_data[k]);
            if (original.command_data[k] == BL_PATH_CMD_CLOSE) { continue; }
            EXPECT_EQ(old.vertex_data[k].x, original.vertex_data[k].x) << "version 1 is exact";
            EXPECT_NEAR(now.vertex_data[k].x, original.vertex_data[k].x, 2e-5);
            EXPECT_NEAR(now.vertex_data[k].y, original.vertex_data[k].y, 2e-5);
        }
    }
    BLBox box = fromV2.object(1).geometryBounds();   // the rectangle
    EXPECT_EQ(box.x0, 0.25);
    EXPECT_EQ(box.y1, 50.5);
}

// ---- SVG import: <use>, <symbol>, nested <svg>, clip-path, filter ----------------------------------------

namespace {
    const char* kForeignSvg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="200" height="200" viewBox="0 0 200 200">
<defs>
<rect id="box" width="20" height="10" fill="#ff0000"/>
<symbol id="sym" viewBox="0 0 10 10"><rect width="10" height="10" fill="#00ff00"/></symbol>
<clipPath id="c"><circle cx="150" cy="50" r="20"/></clipPath>
<filter id="b"><feGaussianBlur stdDeviation="3"/></filter>
<filter id="s"><feDropShadow dx="4" dy="5" stdDeviation="2" flood-color="#000000" flood-opacity="0.5"/></filter>
</defs>
<use xlink:href="#box" x="10" y="20"/>
<use xlink:href="#sym" x="100" y="100" width="40" height="20"/>
<svg x="50" y="150" width="40" height="40" viewBox="0 0 10 10"><rect width="5" height="5" fill="#0000ff"/></svg>
<rect x="120" y="30" width="60" height="40" fill="#ff00ff" clip-path="url(#c)"/>
<rect x="10" y="100" width="20" height="20" filter="url(#b)"/>
<rect x="60" y="100" width="20" height="20" style="filter:url(#s)"/>
</svg>)SVG";

    const SketchObject* objectAtX(const SketchDocument& doc, double x0) {
        for (std::size_t i = 0; i < doc.objectCount(); ++i) {
            if (std::fabs(doc.object(i).geometryBounds().x0 - x0) < 1e-6 && doc.object(i).name() != "Clipping Path") {
                return &doc.object(i);
            }
        }
        return nullptr;
    }
}

TEST(SketchSvgImport, UseSymbolsAndNestedViewportsAreDrawnWhereTheyBelong) {
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(kForeignSvg, doc));
    EXPECT_EQ(doc.objectCount(), 7u);   // the shapes, one clip mask
    const SketchObject* used = objectAtX(doc, 10.0);   // <use> of a rect, moved by x / y
    ASSERT_NE(used, nullptr);
    EXPECT_NEAR(used->geometryBounds().y0, 20.0, 1e-6);
    EXPECT_NEAR(used->geometryBounds().x1, 30.0, 1e-6);
    const SketchObject* symbol = objectAtX(doc, 110.0);   // a 10 x 10 viewBox in 40 x 20 is scaled by 2 and centred
    ASSERT_NE(symbol, nullptr);
    EXPECT_NEAR(symbol->geometryBounds().x1, 130.0, 1e-6);
    EXPECT_NEAR(symbol->geometryBounds().y0, 100.0, 1e-6);
    EXPECT_NEAR(symbol->geometryBounds().y1, 120.0, 1e-6);
    const SketchObject* nested = objectAtX(doc, 50.0);   // a nested <svg> scales its content by 4
    ASSERT_NE(nested, nullptr);
    EXPECT_NEAR(nested->geometryBounds().x1, 70.0, 1e-6);
    EXPECT_NEAR(nested->geometryBounds().y1, 170.0, 1e-6);
}

TEST(SketchSvgImport, ClipPathBecomesAClippingGroup) {
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(kForeignSvg, doc));
    const SketchObject* clipped = objectAtX(doc, 120.0);
    ASSERT_NE(clipped, nullptr);
    ASSERT_FALSE(clipped->groups().empty());
    EXPECT_TRUE(doc.groupProps(clipped->groups().back()).clip);
    const SketchObject* mask = doc.clipMaskOf(clipped->groups().back());
    ASSERT_NE(mask, nullptr);
    EXPECT_NEAR(mask->geometryBounds().x0, 130.0, 1e-6);   // the circle
    auto isMagenta = [](std::uint32_t argb) { return ((argb >> 16) & 255u) > 200u && ((argb >> 8) & 255u) < 60u && (argb & 255u) > 200u; };
    EXPECT_TRUE(isMagenta(pixelAt(doc, 150, 50)));    // inside the circle
    EXPECT_FALSE(isMagenta(pixelAt(doc, 125, 35)));   // in the rectangle but outside the circle
}

TEST(SketchSvgImport, SimpleFiltersBecomeEffects) {
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(kForeignSvg, doc));
    auto effectOf = [](const SketchObject* object, EffectKind kind) -> const AppearanceItem* {
        if (object == nullptr) { return nullptr; }
        for (const AppearanceItem& item : object->appearance()) {
            if (item.kind == AppearanceKind::Effect && item.effect == kind) { return &item; }
        }
        return nullptr;
    };
    const AppearanceItem* blur = nullptr;   // the blurred rectangle is the one at (10, 100); the <use> is at (10, 20)
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        if (std::fabs(doc.object(i).geometryBounds().y0 - 100.0) < 1e-6 && std::fabs(doc.object(i).geometryBounds().x0 - 10.0) < 1e-6) {
            blur = effectOf(&doc.object(i), EffectKind::GaussianBlur);
        }
    }
    ASSERT_NE(blur, nullptr);
    EXPECT_NEAR(blur->softness, 3.0, 1e-6);
    const AppearanceItem* shadow = effectOf(objectAtX(doc, 60.0), EffectKind::DropShadow);
    ASSERT_NE(shadow, nullptr);
    EXPECT_NEAR(shadow->offsetX, 4.0, 1e-6);
    EXPECT_NEAR(shadow->offsetY, 5.0, 1e-6);
    EXPECT_NEAR(shadow->softness, 2.0, 1e-6);
    EXPECT_NEAR(shadow->opacity, 0.5, 1e-6);
}

TEST(SketchSvgImport, AReferenceThatLeadsBackToItselfDoesNotLoopForever) {
    SketchDocument doc;
    const char* loop = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="50" height="50">
<defs><g id="a"><rect width="5" height="5"/><use xlink:href="#a"/></g></defs><use xlink:href="#a"/></svg>)SVG";
    ASSERT_TRUE(documentFromSvg(loop, doc));
    EXPECT_GE(doc.objectCount(), 1u);
}

TEST(SketchSvgImport, TheBigExampleFileLoadsInReasonableTime) {
    std::ifstream in("D:/code/newui-demos/examples/peacockspider.svg", std::ios::binary);
    if (!in) {
        GTEST_SKIP() << "example file not here";
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto start = std::chrono::steady_clock::now();
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(text.str(), doc));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("peacockspider: %zu objects in %.2f s\n", doc.objectCount(), seconds);
    EXPECT_LT(seconds, 20.0);
    start = std::chrono::steady_clock::now();
    BLImage image;
    ASSERT_TRUE(renderPageImage(doc, 0.5, image));
    std::printf("peacockspider: drawn in %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

// ---- Image Trace options -------------------------------------------------------------------------

namespace {
    // A 60 x 60 white picture with a black disc.
    std::vector<std::uint32_t> blackDiscOnWhite() {
        std::vector<std::uint32_t> pixels(60 * 60, 0xFFFFFFFFu);
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 60; ++x) {
                if (std::hypot(x - 30.0, y - 30.0) < 18.0) { pixels[std::size_t(y * 60 + x)] = 0xFF000000u; }
            }
        }
        return pixels;
    }

    std::size_t verticesOf(const TraceResult& trace) {
        std::size_t n = 0;
        for (const TracedShape& shape : trace.shapes) { n += shape.path.size(); }
        return n;
    }
}

TEST(SketchTrace, TheWhiteBackgroundCanBeLeftOut) {
    TraceOptions options;
    options.colors = 2;
    TraceResult with = traceRgba(blackDiscOnWhite(), 60, 60, options);
    options.ignoreWhite = true;
    TraceResult without = traceRgba(blackDiscOnWhite(), 60, 60, options);
    EXPECT_EQ(with.shapes.size(), 2u);        // the white layer and the black one
    ASSERT_EQ(without.shapes.size(), 1u);     // only the disc
    EXPECT_EQ(without.shapes[0].rgb, 0x000000u);
}

TEST(SketchTrace, ATighterFitFollowsTheOutlineWithMorePoints) {
    TraceOptions tight, smooth;
    tight.colors = smooth.colors = 2;
    tight.ignoreWhite = smooth.ignoreWhite = true;
    tight.fitTolerance = 0.2;
    smooth.fitTolerance = 2.0;
    const std::size_t many = verticesOf(traceRgba(blackDiscOnWhite(), 60, 60, tight));
    const std::size_t few = verticesOf(traceRgba(blackDiscOnWhite(), 60, 60, smooth));
    EXPECT_GT(many, 0u);
    EXPECT_GT(few, 0u);
    EXPECT_GE(many, few);
}

// ---- Text on a path / between two paths ------------------------------------------------------------

namespace {
    // A text and a straight path (0, y) .. (length, y) in a document; returns the text id, selects both.
    std::size_t textAndLine(SketchDocument& doc, double y, double length, std::size_t& lineId, const char* words = "Hello") {
        std::size_t text = doc.add(SketchObject::makeText(words, "", 24.0, BLPoint(10, 300))).id();
        lineId = doc.add(SketchObject::makeLine(0, y, length, y)).id();
        doc.setSelection({text, lineId});
        return text;
    }
}

TEST(SketchTextPath, TextFollowsAStraightPathAndStartsWhereAsked) {
    SketchDocument doc;
    std::size_t line = 0;
    const std::size_t text = textAndLine(doc, 100, 400, line);
    EXPECT_TRUE(doc.canMakeTextOnPath());
    EXPECT_FALSE(doc.canMakeTextBetweenPaths());
    ASSERT_TRUE(doc.makeTextOnPath());
    const SketchObject* t = doc.findById(text);
    ASSERT_TRUE(t->textWarp().kind == TextWarp::Kind::OnPath);
    BLBox box = t->geometryBounds();
    EXPECT_NEAR(box.x0, 0.0, 3.0);          // starts at the start of the path
    EXPECT_GT(box.x1, 40.0);
    EXPECT_LT(box.y0, 100.0);               // stands on the path ...
    EXPECT_NEAR(box.y1, 100.0, 8.0);        // ... with only descenders below it
    EXPECT_EQ(doc.objectCount(), 2u);       // the path itself is still there
    EXPECT_TRUE(doc.setTextPathOptions(50.0, false));
    EXPECT_NEAR(doc.findById(text)->geometryBounds().x0, 200.0, 3.0);
    EXPECT_TRUE(doc.setTextPathOptions(0.0, true));   // the other side: upside down below the path
    EXPECT_GT(doc.findById(text)->geometryBounds().y0, 99.0);
    doc.undoStack().undo();
    doc.undoStack().undo();
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(text)->textWarp().active());
    EXPECT_NEAR(doc.findById(text)->geometryBounds().y1, 300.0, 10.0);   // back where it was typed
}

TEST(SketchTextPath, TextBendsAroundACurve) {
    SketchDocument doc;
    std::size_t text = doc.add(SketchObject::makeText("Curved text on an arc", "", 24.0, BLPoint(0, 300))).id();
    BLPath arc;
    arc.move_to(0, 200);
    arc.quad_to(150, 0, 300, 200);   // peaks at y = 100
    SketchObject* curve = SketchObject::makePath(arc);
    std::size_t curveId = doc.add(curve).id();
    doc.setSelection({text, curveId});
    ASSERT_TRUE(doc.makeTextOnPath());
    BLBox box = doc.findById(text)->geometryBounds();
    EXPECT_GT(box.y1 - box.y0, 60.0);   // the text follows the rise and the fall, so it is taller than a line of 24-point text
    EXPECT_LT(box.y0, 150.0);
}

TEST(SketchTextPath, TextIsStretchedBetweenTwoPaths) {
    SketchDocument doc;
    std::size_t text = doc.add(SketchObject::makeText("Banner", "", 24.0, BLPoint(10, 300))).id();
    std::size_t bottom = doc.add(SketchObject::makeLine(0, 150, 300, 150)).id();   // added first: the top is found by position, not order
    std::size_t top = doc.add(SketchObject::makeLine(0, 50, 300, 50)).id();
    doc.setSelection({text, bottom, top});
    EXPECT_TRUE(doc.canMakeTextBetweenPaths());
    ASSERT_TRUE(doc.makeTextBetweenPaths());
    BLBox box = doc.findById(text)->geometryBounds();
    EXPECT_NEAR(box.x0, 0.0, 1.0);
    EXPECT_NEAR(box.x1, 300.0, 1.0);        // the whole length
    EXPECT_LT(box.y0, 70.0);                // from near the top path ...
    EXPECT_GT(box.y1, 120.0);               // ... to the baseline, just above the bottom one
    EXPECT_LT(box.y1, 151.0);
    // Typing keeps the shape between the paths.
    setWords(doc, text, "Banner day");
    BLBox again = doc.findById(text)->geometryBounds();
    EXPECT_NEAR(again.x1, 300.0, 1.0);
    // Release: plain text again, starting about where the warped text started.
    doc.setSelection({text});
    EXPECT_TRUE(doc.canReleaseTextWarp());
    ASSERT_TRUE(doc.releaseTextWarp());
    EXPECT_FALSE(doc.findById(text)->textWarp().active());
    EXPECT_LT(doc.findById(text)->geometryBounds().x1 - doc.findById(text)->geometryBounds().x0, 200.0);
}

TEST(SketchTextPath, PathsRunningTheOtherWayAreTurned) {
    SketchDocument doc;
    std::size_t text = doc.add(SketchObject::makeText("Backwards", "", 24.0, BLPoint(10, 300))).id();
    std::size_t top = doc.add(SketchObject::makeLine(300, 50, 0, 50)).id();   // right to left
    std::size_t bottom = doc.add(SketchObject::makeLine(0, 150, 300, 150)).id();
    doc.setSelection({text, top, bottom});
    ASSERT_TRUE(doc.makeTextBetweenPaths());
    const TextWarp& warp = doc.findById(text)->textWarp();
    auto firstX = [](const BLPath& p) { BLPathView v = p.view(); return v.vertex_data[0].x; };
    EXPECT_LT(firstX(warp.first), 1.0);
    EXPECT_LT(firstX(warp.second), 1.0);
}

TEST(SketchTextPath, TheWarpSurvivesSavingMovingAndCopying) {
    SketchDocument doc;
    std::size_t line = 0;
    const std::size_t text = textAndLine(doc, 100, 400, line);
    ASSERT_TRUE(doc.makeTextOnPath());
    ASSERT_TRUE(doc.setTextPathOptions(25.0, false));
    const BLBox box = doc.findById(text)->geometryBounds();
    SketchDocument fromSvg, fromBin;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), fromBin));
    for (const SketchDocument* copy : {&fromSvg, &fromBin}) {
        const SketchObject* t = nullptr;
        for (std::size_t i = 0; i < copy->objectCount(); ++i) {
            if (copy->object(i).isText()) { t = &copy->object(i); }
        }
        ASSERT_NE(t, nullptr);
        EXPECT_TRUE(t->textWarp().kind == TextWarp::Kind::OnPath);
        EXPECT_NEAR(t->textWarp().start, 25.0, 1e-6);
        EXPECT_NEAR(t->geometryBounds().x0, box.x0, 1e-3);
        EXPECT_NEAR(t->geometryBounds().y1, box.y1, 1e-3);
    }
    // Moving the text moves what it follows with it.
    doc.setSelection({text});
    doc.transformObjects({text}, BLMatrix2D::make_translation(50, 20), "Move");
    EXPECT_NEAR(doc.findById(text)->geometryBounds().x0, box.x0 + 50.0, 1e-6);
    setWords(doc, text, "Hi");   // typing afterwards: still on the (moved) path
    EXPECT_NEAR(doc.findById(text)->geometryBounds().x0, box.x0 + 50.0, 3.0);
    // A copy keeps it.
    doc.duplicateSelection(0, 0);
    EXPECT_TRUE(doc.findById(doc.selection().front())->textWarp().active());
}

// ---- Images ---------------------------------------------------------------------------------------

namespace {
    // A w x h PNG: red on the left half, blue on the right.
    std::string twoColorPng(int w, int h) {
        BLImage image(w, h, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0xFFFF0000u));
        ctx.fill_rect(BLRect(0, 0, w / 2.0, h));
        ctx.set_fill_style(BLRgba32(0xFF0000FFu));
        ctx.fill_rect(BLRect(w / 2.0, 0, w / 2.0, h));
        ctx.end();
        BLImageCodec codec;
        codec.find_by_name("PNG");
        BLArray<uint8_t> buffer;
        image.write_to_data(buffer, codec);
        return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }
}

TEST(SketchImages, Base64AndDataUrisRoundTrip) {
    for (std::string text : {std::string(), std::string("a"), std::string("ab"), std::string("abc"), std::string("hello world, \x01\xff\x80 bytes")}) {
        std::string back;
        ASSERT_TRUE(base64Decode(base64Encode(text), back));
        EXPECT_EQ(back, text);
    }
    std::string bytes;
    ASSERT_TRUE(bytesFromDataUri("data:text/plain;base64,aGk=", bytes));
    EXPECT_EQ(bytes, "hi");
    ASSERT_TRUE(bytesFromDataUri("data:text/plain,a%20b", bytes));
    EXPECT_EQ(bytes, "a b");
    EXPECT_FALSE(bytesFromDataUri("file.png", bytes));
    std::string png = twoColorPng(8, 4);
    EXPECT_EQ(imageMimeType(png), "image/png");
    std::shared_ptr<const ImageData> image = decodeImage(png, "two.png");
    ASSERT_TRUE(image);
    EXPECT_EQ(image->width, 8);
    EXPECT_EQ(image->height, 4);
    EXPECT_EQ(decodeImage("not a picture", "x"), nullptr);
}

TEST(SketchImages, APlacedPictureIsDrawnMovedAndHitLikeAnyObject) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    auto image = decodeImage(twoColorPng(40, 20), "two.png");
    ASSERT_TRUE(image);
    const std::size_t id = doc.placeImage(image, BLPoint(100, 100));
    ASSERT_NE(id, 0u);
    EXPECT_EQ(doc.selection(), (std::vector<std::size_t>{id}));
    const SketchObject* object = doc.findById(id);
    EXPECT_TRUE(object->isImage());
    BLBox box = object->geometryBounds();
    EXPECT_NEAR(box.x0, 80.0, 1e-6);   // centred, natural size (a point a pixel)
    EXPECT_NEAR(box.x1, 120.0, 1e-6);
    auto isRed = [](std::uint32_t p) { return ((p >> 16) & 255u) > 200u && (p & 255u) < 60u; };
    auto isBlue = [](std::uint32_t p) { return ((p >> 16) & 255u) < 60u && (p & 255u) > 200u; };
    EXPECT_TRUE(isRed(pixelAt(doc, 85, 100)));
    EXPECT_TRUE(isBlue(pixelAt(doc, 115, 100)));
    EXPECT_NE(doc.hitTest(BLPoint(100, 100), 2.0), nullptr);
    EXPECT_EQ(doc.hitTest(BLPoint(10, 10), 2.0), nullptr);
    doc.transformObjects({id}, BLMatrix2D::make_translation(30, 0), "Move");   // it moves like any object
    EXPECT_TRUE(isRed(pixelAt(doc, 115, 100)));
    BLMatrix2D scale = BLMatrix2D::make_scaling(2.0, 2.0);
    doc.transformObjects({id}, scale, "Scale");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1 - doc.findById(id)->geometryBounds().x0, 80.0, 1e-6);
    doc.findById(id)->setOpacity(0.5);   // opacity works
    const std::uint32_t half = pixelAt(doc, 190, 100);
    EXPECT_GT(((half >> 8) & 255u), 100u);   // red over white at half strength is pinkish
    doc.undoStack().undo();
}

TEST(SketchImages, ABigPictureIsScaledToFitTheArtboard) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    auto image = decodeImage(twoColorPng(800, 400), "big.png");
    ASSERT_TRUE(image);
    const std::size_t id = doc.placeImage(image, BLPoint(100, 100));
    BLBox box = doc.findById(id)->geometryBounds();
    EXPECT_LE(box.x1 - box.x0, 160.0 + 1e-6);
    EXPECT_NEAR((box.x1 - box.x0) / (box.y1 - box.y0), 2.0, 1e-6);   // proportions kept
}

TEST(SketchImages, PicturesSurviveSvgBinaryAndClipboardRoundTrips) {
    SketchDocument doc;
    auto image = decodeImage(twoColorPng(40, 20), "two.png");
    const std::size_t a = doc.placeImage(image, BLPoint(100, 100));
    const std::size_t b = doc.placeImage(image, BLPoint(300, 100));   // the same picture twice
    ASSERT_NE(a, 0u);
    ASSERT_NE(b, 0u);
    doc.findById(b)->setOpacity(0.5);
    doc.findById(b)->setName("Second");
    doc.transformObjects({b}, BLMatrix2D::make_rotation(0.3), "Rotate");
    const std::string svg = svgFromDocument(doc);
    EXPECT_NE(svg.find("<image"), std::string::npos);   // a real <image> that other viewers show
    EXPECT_NE(svg.find("data:image/png;base64,"), std::string::npos);
    const std::string bin = binaryFromDocument(doc, false);
    EXPECT_LT(bin.size(), image->bytes.size() * 3 / 2 + 4000) << "the picture is stored once however often it is used";
    SketchDocument fromSvg, fromBin;
    ASSERT_TRUE(documentFromSvg(svg, fromSvg));
    ASSERT_TRUE(documentFromBinary(bin, fromBin));
    for (const SketchDocument* copy : {&fromSvg, &fromBin}) {
        ASSERT_EQ(copy->objectCount(), 2u);
        for (std::size_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(copy->object(i).isImage());
            ASSERT_TRUE(copy->object(i).image());
            EXPECT_EQ(copy->object(i).image()->width, 40);
        }
        EXPECT_NEAR(copy->object(0).geometryBounds().x0, doc.object(0).geometryBounds().x0, 1e-3);
        EXPECT_NEAR(copy->object(1).geometryBounds().y1, doc.object(1).geometryBounds().y1, 1e-3);
        EXPECT_NEAR(copy->object(1).opacity(), 0.5, 1e-9);
        EXPECT_EQ(copy->object(1).name(), "Second");
    }
    doc.setSelection({a});
    SketchDocument other;
    ASSERT_TRUE(other.pasteSvg(doc.selectionToSvg(), 10, 10));
    ASSERT_EQ(other.objectCount(), 1u);
    EXPECT_TRUE(other.object(0).isImage());
}

TEST(SketchImages, ForeignSvgImagesKeepTheirProportionsInTheirBox) {
    const std::string png = twoColorPng(40, 20);
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\"><image x=\"10\" y=\"20\" width=\"100\" height=\"100\" href=\"data:image/png;base64," +
        base64Encode(png) + "\"/><image x=\"0\" y=\"150\" width=\"50\" height=\"50\" preserveAspectRatio=\"none\" href=\"data:image/png;base64," + base64Encode(png) + "\"/></svg>";
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(svg, doc));
    ASSERT_EQ(doc.objectCount(), 2u);
    BLBox meet = doc.object(0).geometryBounds();   // 2:1 picture in a square: fitted and centred
    EXPECT_NEAR(meet.x0, 10.0, 1e-6);
    EXPECT_NEAR(meet.x1, 110.0, 1e-6);
    EXPECT_NEAR(meet.y1 - meet.y0, 50.0, 1e-6);
    EXPECT_NEAR((meet.y0 + meet.y1) * 0.5, 70.0, 1e-6);
    BLBox stretched = doc.object(1).geometryBounds();
    EXPECT_NEAR(stretched.y1 - stretched.y0, 50.0, 1e-6);
}

TEST(SketchImages, APhotoThatCrashedBlend2dLoadsAndABrokenFileJustFails) {
    EXPECT_EQ(loadImageFile("D:/does/not/exist.jpg"), nullptr);
    EXPECT_EQ(decodeImage(std::string("\xFF\xD8\xFF\xE0 truncated jpeg"), "bad.jpg"), nullptr);   // no crash, no assertion
    auto photo = loadImageFile("C:/Users/jim/Downloads/IMG_3476 (1).jpeg");
    if (!photo) {
        GTEST_SKIP() << "the sample photo is not here";
    }
    EXPECT_GT(photo->width, 1000);
    double extra = 1.0;
    const BLImage& thumb = photo->levelFor(0.1, extra);
    EXPECT_LT(thumb.width(), photo->width / 2);
    EXPECT_GT(extra, 2.0);
    SketchDocument doc;
    EXPECT_NE(doc.placeImage(photo, BLPoint(100, 100)), 0u);
    std::printf("photo %dx%d, level %d wide (x%.0f)\n", photo->width, photo->height, thumb.width(), extra);
}

TEST(SketchImages, ThePhotoLoadsAndDrawsInReasonableTime) {
    auto t0 = std::chrono::steady_clock::now();
    auto lap = [&t0](const char* what) {
        auto now = std::chrono::steady_clock::now();
        std::printf("photo: %s %.2f s\n", what, std::chrono::duration<double>(now - t0).count());
        t0 = now;
    };
    auto photo = loadImageFile("C:/Users/jim/Downloads/IMG_3476 (1).jpeg");
    if (!photo) {
        GTEST_SKIP() << "the sample photo is not here";
    }
    lap("decode");
    SketchDocument doc;
    doc.placeImage(photo, BLPoint(306, 396));
    lap("place");
    BLImage image;
    ASSERT_TRUE(renderPageImage(doc, 1.0, image));
    lap("first draw");
    ASSERT_TRUE(renderPageImage(doc, 1.0, image));
    lap("second draw");
    const std::string svg = svgFromDocument(doc);
    lap("svg");
    const std::string bin = binaryFromDocument(doc, true);
    lap("skb");
}

// ---- Expand Appearance -----------------------------------------------------------------------------

TEST(SketchExpand, AFillAndAStrokeBecomeTwoObjectsInAGroup) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(10, 10, 100, 50));
    rect.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    rect.setStroke(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    rect.setStrokeWidth(4.0);
    doc.setSelection({rect.id()});
    ASSERT_TRUE(doc.expandAppearance());
    ASSERT_EQ(doc.objectCount(), 2u);
    const SketchObject& fill = doc.object(0);
    const SketchObject& ring = doc.object(1);
    EXPECT_NEAR(fill.geometryBounds().x0, 10.0, 1e-6);          // the fill keeps the outline
    EXPECT_FALSE(fill.fill().isNone());
    EXPECT_TRUE(fill.stroke().isNone());
    EXPECT_NEAR(ring.geometryBounds().x0, 8.0, 1e-3);           // the stroke is an outline 2 either side
    EXPECT_NEAR(ring.geometryBounds().x1, 112.0, 1e-3);
    EXPECT_TRUE(ring.stroke().isNone());
    EXPECT_FALSE(ring.fill().isNone());                          // wearing the stroke's paint
    EXPECT_EQ(ring.fill().color().toRgba32().value, SketchColor::fromHex("#0000ff").toRgba32().value);
    EXPECT_FALSE(fill.groups().empty());
    EXPECT_EQ(fill.groups(), ring.groups());
    doc.undoStack().undo();
    EXPECT_EQ(doc.objectCount(), 1u);
    EXPECT_FALSE(doc.object(0).stroke().isNone());
}

TEST(SketchExpand, EveryFillAndStrokeIsKeptAndWholeObjectOpacityGoesToTheGroup) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(0, 0, 60, 40));
    std::vector<AppearanceItem> items = rect.appearance();
    items.push_back(AppearanceItem::makeFill(SketchPaint::solid(SketchColor::fromHex("#00ff00"))));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::fromHex("#222222")), 2.0));
    items[1].strokeWidth = 6.0;
    rect.setAppearance(items);
    rect.setOpacity(0.5);
    doc.setSelection({rect.id()});
    ASSERT_TRUE(doc.expandAppearance());
    EXPECT_EQ(doc.objectCount(), 4u);   // two fills, two strokes
    ASSERT_FALSE(doc.object(0).groups().empty());
    EXPECT_NEAR(doc.groupProps(doc.object(0).groups().back()).opacity, 0.5, 1e-9);
    EXPECT_NEAR(doc.object(0).opacity(), 1.0, 1e-9);
}

TEST(SketchExpand, GeometryEffectsAreBakedIntoTheOutline) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(0, 0, 100, 60));
    rect.setStroke(SketchPaint::none());
    std::vector<AppearanceItem> items = rect.appearance();
    AppearanceItem round = AppearanceItem::makeEffect(EffectKind::RoundCorners);
    round.radius = 20.0;
    items.push_back(round);
    rect.setAppearance(items);
    doc.setSelection({rect.id()});
    const std::size_t before = rect.geometry().size();
    ASSERT_TRUE(doc.expandAppearance());
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_GT(doc.object(0).geometry().size(), before);   // curves where the corners were
    for (const AppearanceItem& item : doc.object(0).appearance()) {
        EXPECT_NE(item.kind, AppearanceKind::Effect);
    }
    // the corner point itself is no longer on the outline
    EXPECT_NE(doc.object(0).geometry().hit_test(BLPoint(0.5, 0.5), BL_FILL_RULE_NON_ZERO), BL_HIT_TEST_IN);
}

TEST(SketchExpand, APixelEffectBecomesAPicture) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    SketchObject& rect = doc.add(SketchObject::makeRect(50, 50, 60, 40));
    rect.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    std::vector<AppearanceItem> items = rect.appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::DropShadow));
    rect.setAppearance(items);
    doc.setSelection({rect.id()});
    ASSERT_TRUE(doc.expandAppearance());
    ASSERT_EQ(doc.objectCount(), 1u);
    ASSERT_TRUE(doc.object(0).isImage());
    EXPECT_LT(doc.object(0).geometryBounds().x0, 50.0);    // the shadow makes the picture larger than the shape
    EXPECT_GT(doc.object(0).geometryBounds().x1, 110.0);
    auto isRed = [](std::uint32_t p) { return ((p >> 16) & 255u) > 200u && ((p >> 8) & 255u) < 60u && (p & 255u) < 60u; };
    EXPECT_TRUE(isRed(pixelAt(doc, 70, 60)));              // it still looks the same
}

TEST(SketchExpand, BrushStrokesBecomeTheirArtwork) {
    SketchDocument doc;
    SketchObject& line = doc.add(SketchObject::makeLine(10, 50, 190, 50));
    line.setFill(SketchPaint::none());
    doc.setSelection({line.id()});
    ASSERT_NE(doc.makeBrush(BrushKind::Calligraphic, ""), 0u);
    ASSERT_TRUE(doc.applyBrush(doc.activeBrush()));
    line.setStrokeWidth(10.0);
    ASSERT_TRUE(doc.expandAppearance());
    ASSERT_EQ(doc.objectCount(), 1u);
    EXPECT_TRUE(doc.object(0).stroke().isNone());
    EXPECT_TRUE(doc.object(0).strokeOptions().brush == nullptr);
    EXPECT_GT(doc.object(0).geometryBounds().y1 - doc.object(0).geometryBounds().y0, 3.0);   // a nib-wide band, not a line
    // a scatter brush: one object per copy
    SketchDocument other;
    SketchObject& dot = other.add(SketchObject::makeEllipse(0, 0, 4, 4));
    other.setSelection({dot.id()});
    ASSERT_NE(other.makeBrush(BrushKind::Scatter, ""), 0u);
    SketchObject& path = other.add(SketchObject::makeLine(0, 100, 200, 100));
    other.setSelection({path.id()});
    ASSERT_TRUE(other.applyBrush(other.activeBrush()));
    path.setStrokeWidth(1.0);
    const std::size_t before = other.objectCount();
    ASSERT_TRUE(other.expandAppearance());
    EXPECT_GT(other.objectCount(), before + 3);
}

TEST(SketchExpand, NothingToExpandForTextSymbolsAndEmptySelections) {
    SketchDocument doc;
    EXPECT_FALSE(doc.expandAppearance());
    SketchObject& text = doc.add(SketchObject::makeText("Hi", "", 24.0, BLPoint(10, 40)));
    doc.setSelection({text.id()});
    EXPECT_FALSE(doc.expandAppearance());
}

// ---- PDF export -------------------------------------------------------------------------------------

namespace {
    // The text of every content stream of the PDF (inflated), in file order, and the whole file as is.
    struct PdfView {
        std::string file;
        std::vector<std::string> streams;
        int pages = 0;
        bool structureOk = false;
    };

    PdfView inspectPdf(const std::string& pdf) {
        PdfView view;
        view.file = pdf;
        if (pdf.compare(0, 8, "%PDF-1.4") != 0 || pdf.find("%%EOF") == std::string::npos) {
            return view;
        }
        // startxref -> the xref table -> every offset must be the start of "N 0 obj"
        const std::size_t sx = pdf.rfind("startxref");
        const std::size_t xref = std::size_t(std::strtoull(pdf.c_str() + sx + 9, nullptr, 10));
        if (pdf.compare(xref, 4, "xref") != 0) {
            return view;
        }
        std::size_t count = std::size_t(std::strtoull(pdf.c_str() + xref + 7, nullptr, 10));   // "xref\n0 N"
        std::size_t entry = pdf.find('\n', pdf.find('\n', xref) + 1) + 1;                      // after "0 N"
        bool ok = true;
        for (std::size_t i = 0; i < count; ++i, entry += 20) {
            if (i == 0) { continue; }   // the free entry
            const std::size_t offset = std::size_t(std::strtoull(pdf.c_str() + entry, nullptr, 10));
            const std::string expect = std::to_string(i) + " 0 obj";
            ok = ok && pdf.compare(offset, expect.size(), expect) == 0;
        }
        view.structureOk = ok;
        for (std::size_t at = 0; (at = pdf.find("/Type /Page ", at)) != std::string::npos; at += 12) { ++view.pages; }
        // every stream, flate-decoded when it says so
        for (std::size_t at = 0; (at = pdf.find("stream\n", at)) != std::string::npos;) {
            if (at >= 3 && pdf.compare(at - 3, 3, "end") == 0) { at += 7; continue; }
            const std::size_t begin = at + 7, end = pdf.find("\nendstream", begin);
            const std::size_t dict = pdf.rfind("<<", at);
            const bool flate = pdf.find("/FlateDecode", dict) < at;
            std::string data = pdf.substr(begin, end - begin), text;
            if (flate && gzipDecompress(data, text)) { data = text; }
            view.streams.push_back(data);
            at = end + 10;
        }
        return view;
    }

    bool anyStreamHas(const PdfView& v, const std::string& needle) {
        for (const std::string& s : v.streams) {
            if (s.find(needle) != std::string::npos) { return true; }
        }
        return false;
    }
}

TEST(SketchPdf, ADocumentBecomesAWellFormedFileWithAPagePerArtboard) {
    SketchDocument doc;
    doc.setPageSize(200, 100);
    Artboard second;
    second.name = "Two";
    second.x = 300;
    second.width = 50;
    second.height = 50;
    doc.addArtboard(second);
    SketchObject& rect = doc.add(SketchObject::makeRect(10, 10, 80, 40));
    rect.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    rect.setStroke(SketchPaint::solid(SketchColor::fromHex("#0000ff")));
    rect.setStrokeWidth(3.0);
    PdfView pdf = inspectPdf(pdfFromDocument(doc));
    EXPECT_TRUE(pdf.structureOk);
    EXPECT_EQ(pdf.pages, 2);
    EXPECT_NE(pdf.file.find("/MediaBox [0 0 200 100]"), std::string::npos);
    EXPECT_NE(pdf.file.find("/MediaBox [0 0 50 50]"), std::string::npos);
    EXPECT_TRUE(anyStreamHas(pdf, "1 0 0 rg"));      // the fill color
    EXPECT_TRUE(anyStreamHas(pdf, "0 0 1 RG"));      // the stroke color
    EXPECT_TRUE(anyStreamHas(pdf, "3 w"));           // its weight
    EXPECT_TRUE(anyStreamHas(pdf, "10 10 m"));       // the outline
    EXPECT_TRUE(anyStreamHas(pdf, "1 0 0 -1 0 100 cm"));   // the y flip of the first page (document y runs down)
    const std::string path = (std::filesystem::temp_directory_path() / "sketch_test.pdf").string();
    EXPECT_TRUE(writePdf(doc, path));
    EXPECT_GT(std::filesystem::file_size(path), 200u);
    std::filesystem::remove(path);
}

TEST(SketchPdf, GradientsDashesCmykOpacityAndClippingStayVector) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    SketchObject& a = doc.add(SketchObject::makeRect(10, 10, 80, 40));
    SketchGradient g = SketchGradient::fromColor(SketchColor::fromHex("#ff0000"));
    GradientStop middle;
    middle.offset = 0.5;
    middle.color = SketchColor::fromHex("#00ff00");
    g.stops.push_back(middle);
    g.sortStops();
    a.setFill(SketchPaint::linear(g));
    SketchObject& b = doc.add(SketchObject::makeLine(10, 100, 190, 100));
    b.setStroke(SketchPaint::solid(SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f, 1.0f)));
    StrokeOptions dashed;
    dashed.dashes = {6.0, 3.0};
    dashed.cap = StrokeCap::Round;
    b.setStrokeOptions(dashed);
    b.setStrokeWidth(5.0);
    SketchObject& c = doc.add(SketchObject::makeEllipse(150, 150, 30, 30));
    c.setOpacity(0.4);
    c.setBlendMode(BlendMode::Multiply);
    SketchObject& d = doc.add(SketchObject::makeRect(20, 130, 60, 60));
    SketchObject& mask = doc.add(SketchObject::makeEllipse(50, 160, 25, 25));
    doc.setSelection({d.id(), mask.id()});
    ASSERT_TRUE(doc.makeClippingMask());
    PdfView pdf = inspectPdf(pdfFromDocument(doc));
    EXPECT_TRUE(pdf.structureOk);
    EXPECT_NE(pdf.file.find("/ShadingType 2"), std::string::npos);                 // the gradient is a shading
    EXPECT_NE(pdf.file.find("/FunctionType 3"), std::string::npos);                // three stops: stitched
    EXPECT_TRUE(anyStreamHas(pdf, "/Pattern cs"));
    EXPECT_TRUE(anyStreamHas(pdf, "[6 3 ] 0 d"));                                   // the dashes
    EXPECT_TRUE(anyStreamHas(pdf, "1 J"));                                          // round caps
    EXPECT_TRUE(anyStreamHas(pdf, "0.1 0.2 0.3 0.4 K"));                            // CMYK stays CMYK
    EXPECT_NE(pdf.file.find("/BM /Multiply"), std::string::npos);                   // blend mode
    EXPECT_NE(pdf.file.find("/ca 0.4"), std::string::npos);                         // opacity
    EXPECT_NE(pdf.file.find("/Subtype /Form"), std::string::npos);                  // in a transparency group
    EXPECT_TRUE(anyStreamHas(pdf, "W n"));                                          // the clipping mask
}

TEST(SketchPdf, PicturesPatternsSymbolsAndPixelEffectsAreEmbedded) {
    SketchDocument doc;
    doc.setPageSize(300, 300);
    doc.placeImage(decodeImage(twoColorPng(20, 10), "p.png"), BLPoint(60, 40));
    SketchObject& tile = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    tile.setFill(SketchPaint::solid(SketchColor::fromHex("#00aa00")));
    doc.setSelection({tile.id()});
    ASSERT_NE(doc.makePattern(""), 0u);
    SketchObject& filled = doc.add(SketchObject::makeRect(100, 100, 80, 60));
    doc.setSelection({filled.id()});
    ASSERT_TRUE(doc.applyPattern(doc.activePattern(), false));
    SketchObject& star = doc.add(SketchObject::makeRect(200, 20, 30, 30));
    doc.setSelection({star.id()});
    ASSERT_NE(doc.makeSymbol(""), 0u);
    SketchObject& shadowed = doc.add(SketchObject::makeRect(200, 200, 40, 40));
    std::vector<AppearanceItem> items = shadowed.appearance();
    items.push_back(AppearanceItem::makeEffect(EffectKind::DropShadow));
    shadowed.setAppearance(items);
    PdfView pdf = inspectPdf(pdfFromDocument(doc));
    EXPECT_TRUE(pdf.structureOk);
    EXPECT_NE(pdf.file.find("/Subtype /Image"), std::string::npos);
    EXPECT_NE(pdf.file.find("/PatternType 1"), std::string::npos);                  // the tile
    EXPECT_NE(pdf.file.find("/SMask"), std::string::npos);                          // the shadow picture has transparency
    EXPECT_TRUE(anyStreamHas(pdf, " Do"));
}

TEST(SketchPdf, BrushStrokesAndTextAreWrittenAsOutlines) {
    SketchDocument doc;
    doc.setPageSize(300, 200);
    SketchObject& line = doc.add(SketchObject::makeLine(10, 50, 250, 50));
    doc.setSelection({line.id()});
    ASSERT_NE(doc.makeBrush(BrushKind::Calligraphic, ""), 0u);
    ASSERT_TRUE(doc.applyBrush(doc.activeBrush()));
    line.setStrokeWidth(12.0);
    doc.add(SketchObject::makeText("PDF", "", 40.0, BLPoint(20, 150)));
    PdfView pdf = inspectPdf(pdfFromDocument(doc));
    EXPECT_TRUE(pdf.structureOk);
    EXPECT_FALSE(anyStreamHas(pdf, " Tf"));    // no fonts: the letters are paths
    EXPECT_GT(pdf.streams.front().size(), 400u);
    EXPECT_TRUE(anyStreamHas(pdf, " c\n"));    // curves in the glyph outlines
}

TEST(SketchPdf, TheShowcaseExportsWithoutTrouble) {
    std::ifstream in("D:/code/newui-demos/demos/sketch/docs/examples/showcase.svg", std::ios::binary);
    if (!in) {
        GTEST_SKIP() << "the showcase file is not here";
    }
    std::ostringstream text;
    text << in.rdbuf();
    SketchDocument doc;
    ASSERT_TRUE(documentFromSvg(text.str(), doc));
    const std::string pdf = pdfFromDocument(doc);
    EXPECT_GT(pdf.size(), 2000u);
    if (const char* out = std::getenv("SKETCH_PDF_OUT")) {
        std::ofstream file(out, std::ios::binary);
        file.write(pdf.data(), std::streamsize(pdf.size()));
    }
}

// ---- Stroke align and arrows ------------------------------------------------------------------------

namespace {
    BLBox strokeBounds(const SketchObject& object) {
        BLPath outline;
        BLBox box(0, 0, 0, 0);
        for (std::size_t i = 0; i < object.appearance().size(); ++i) {
            if (object.appearance()[i].kind == AppearanceKind::Stroke && object.strokeOutline(i, outline)) {
                outline.get_bounding_box(&box);
            }
        }
        return box;
    }
}

TEST(SketchStrokeDecor, InsideAndOutsideKeepOneSideOfAClosedOutline) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(10, 10, 80, 40));
    rect.setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    rect.setStrokeWidth(10.0);
    StrokeOptions options;
    options.align = StrokeAlign::Inside;
    rect.setStrokeOptions(options);
    BLBox inside = strokeBounds(rect);
    EXPECT_NEAR(inside.x0, 10.0, 0.1);
    EXPECT_NEAR(inside.x1, 90.0, 0.1);
    options.align = StrokeAlign::Outside;
    rect.setStrokeOptions(options);
    BLBox outside = strokeBounds(rect);
    EXPECT_NEAR(outside.x0, 0.0, 0.1);
    EXPECT_NEAR(outside.x1, 100.0, 0.1);
    EXPECT_NEAR(rect.bounds().x0, 0.0, 0.1);   // the object's bounds take the outside half in
}

TEST(SketchStrokeDecor, ArrowheadsGrowTheEndsOfAnOpenPath) {
    SketchDocument doc;
    SketchObject& line = doc.add(SketchObject::makeLine(10, 50, 110, 50));
    line.setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    line.setStrokeWidth(2.0);
    StrokeOptions options;
    options.arrowEnd = ArrowStyle::Filled;
    line.setStrokeOptions(options);
    BLBox box = strokeBounds(line);
    EXPECT_NEAR(box.x1, 110.0, 0.1);              // the tip is on the end
    EXPECT_NEAR(box.y1 - box.y0, 9.6, 0.2);       // a head 8 long and 0.6 * 8 either side
    options.arrowEnd = ArrowStyle::Dot;
    options.arrowScale = 200.0;
    line.setStrokeOptions(options);
    EXPECT_GT(strokeBounds(line).y1 - strokeBounds(line).y0, 10.0);
    options.arrowStart = ArrowStyle::Simple;
    line.setStrokeOptions(options);
    EXPECT_LT(strokeBounds(line).x0, 10.0 + 0.1);
}

TEST(SketchStrokeDecor, AlignAndArrowsSurviveSavingAndOpening) {
    SketchDocument doc;
    SketchObject& a = doc.add(SketchObject::makeRect(10, 10, 80, 40));
    a.setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions options;
    options.align = StrokeAlign::Outside;
    a.setStrokeOptions(options);
    SketchObject& b = doc.add(SketchObject::makeLine(10, 100, 110, 100));
    b.setStroke(SketchPaint::solid(SketchColor::fromHex("#000000")));
    StrokeOptions arrows;
    arrows.arrowStart = ArrowStyle::Dot;
    arrows.arrowEnd = ArrowStyle::Simple;
    arrows.arrowScale = 150.0;
    b.setStrokeOptions(arrows);
    SketchDocument viaSvg, viaBin;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), viaSvg));
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), viaBin));
    for (const SketchDocument* loaded : {&viaSvg, &viaBin}) {
        ASSERT_EQ(loaded->objectCount(), 2u);
        EXPECT_EQ(loaded->object(0).strokeOptions().align, StrokeAlign::Outside);
        EXPECT_EQ(loaded->object(1).strokeOptions().arrowStart, ArrowStyle::Dot);
        EXPECT_EQ(loaded->object(1).strokeOptions().arrowEnd, ArrowStyle::Simple);
        EXPECT_DOUBLE_EQ(loaded->object(1).strokeOptions().arrowScale, 150.0);
    }
    EXPECT_EQ(pdfFromDocument(doc).compare(0, 8, "%PDF-1.4"), 0);
}

TEST(GraphDocument, ScopesAddressNodesInsideSubnets) {
    GraphDocument doc;
    const NodeRef group = doc.addGraphNode("subnet", "group");
    const std::uint32_t scope = doc.scopeOfSubnet(group);
    EXPECT_NE(scope, 0u);
    EXPECT_EQ(doc.scopeOfSubnet(group), scope) << "the same path is the same scope";
    EXPECT_EQ(doc.parentOfScope(scope), group);
    EXPECT_FALSE(doc.parentOfScope(0).valid());

    const NodeRef circle = doc.addGraphNode("circle", "inner", scope);
    EXPECT_EQ(circle.scope, scope);
    EXPECT_TRUE(doc.hasNode(circle));
    EXPECT_FALSE(doc.hasNode(NodeRef(scope, circle.id + 50)));
    doc.setGraphParam(circle, "radius", 7.0f);
    EXPECT_EQ(doc.node(circle).getFloat("radius"), 7.0f);
    EXPECT_NE(circle, NodeRef(circle.id)) << "the same id in the root is another node";

    // A node one subnet deeper gets a scope of its own.
    const NodeRef inner = doc.addGraphNode("subnet", "inner", scope);
    const std::uint32_t deeper = doc.scopeOfSubnet(inner);
    EXPECT_NE(deeper, scope);
    EXPECT_EQ(doc.parentOfScope(deeper), inner);
    const NodeRef leaf = doc.addGraphNode("circle", "leaf", deeper);
    EXPECT_TRUE(doc.hasNode(leaf));

    // Undo removes the edit inside the subnet; redo brings it back under the same reference.
    doc.setGraphParam(leaf, "radius", 3.0f);
    doc.undoStack().undo();
    EXPECT_NE(doc.node(leaf).getFloat("radius"), 3.0f);
    doc.undoStack().redo();
    EXPECT_EQ(doc.node(leaf).getFloat("radius"), 3.0f);

    // Undoing the subnet's creation makes its scope unreachable; redo makes it reachable again.
    while (doc.undoStack().canUndo()) {
        doc.undoStack().undo();
    }
    EXPECT_FALSE(doc.hasNode(circle));
    EXPECT_EQ(doc.findNetwork(scope), nullptr);
    while (doc.undoStack().canRedo()) {
        doc.undoStack().redo();
    }
    EXPECT_TRUE(doc.hasNode(circle));
    EXPECT_TRUE(doc.hasNode(leaf));
    EXPECT_EQ(doc.node(circle).getFloat("radius"), 7.0f);
    EXPECT_EQ(doc.scopeOfSubnet(group), scope);

    EXPECT_THROW(doc.connectGraphNodes(group, circle), std::runtime_error);
}

TEST(SketchDocumentScopes, ObjectInsideAHandBuiltGroupFollowsTheGroup) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    const std::size_t id = rect.id();
    const NodeRef group = doc.addGraphNode(kSubnetNodeType, "group");
    const std::uint32_t scope = doc.scopeOfSubnet(group);
    ASSERT_NE(scope, 0u);

    // Moving the object's nodes into the group's scope: the next sync builds them there and drops the root ones.
    rect.setGraphRef(NodeRef(scope, jitnodes::invalidNodeId));
    doc.add(SketchObject::makeRect(50, 50, 5, 5));
    SketchObject* member = doc.findById(id);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->graphScope(), scope);
    EXPECT_TRUE(doc.hasNode(member->graphRef()));
    const NodeRef place = doc.transformRefOf(id);
    EXPECT_EQ(place.scope, scope) << "the transform node is made beside the shape node";
    EXPECT_EQ(doc.transformNodeOf(id), jitnodes::invalidNodeId) << "the root-scope shortcut does not answer for a group member";
    EXPECT_EQ(doc.objectOfNode(place), id);
    int rootShapes = 0;
    for (jitnodes::NodeId nodeId : doc.network().nodeIds()) {
        rootShapes += isShapeNodeType(doc.network().node(nodeId).typeName()) ? 1 : 0;
    }
    EXPECT_EQ(rootShapes, 1) << "only the second rectangle keeps a root shape node; the moved object's is gone";
    EXPECT_NEAR(doc.outlineBounds(*member).x0, 0.0, 1e-3);

    // The group's own placement carries its members: the object follows when the group moves, and again on undo and redo.
    doc.setGraphParam(group, "translateX", 100.0f);
    member = doc.findById(id);
    EXPECT_NEAR(doc.outlineBounds(*member).x0, 100.0, 1e-3);
    EXPECT_NEAR(doc.scopeMatrix(scope).m20, 100.0, 1e-6);
    EXPECT_NEAR(doc.worldOf(doc.transformRefOf(id)).m20, 100.0, 1e-6);

    // Moving the object by hand writes the placement relative to the group, so the page position stays what the user made.
    doc.transformObjects({id}, BLMatrix2D::make_translation(20, 0), "Move");
    member = doc.findById(id);
    EXPECT_NEAR(doc.outlineBounds(*member).x0, 120.0, 1e-3);
    EXPECT_NEAR(doc.node(doc.transformRefOf(id)).getFloat("translateX"), 20.0, 1e-3);

    doc.undoStack().undo();
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(id)).x0, 100.0, 1e-3);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(id)).x0, 0.0, 1e-3);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(id)).x0, 100.0, 1e-3);
}

TEST(SketchDocumentScopes, AppearanceNodesLiveBesideTheShapeInTheGroup) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    const std::size_t id = rect.id();
    const NodeRef group = doc.addGraphNode(kSubnetNodeType, "group");
    const std::uint32_t scope = doc.scopeOfSubnet(group);
    rect.setGraphRef(NodeRef(scope, jitnodes::invalidNodeId));
    doc.add(SketchObject::makeRect(50, 50, 5, 5));

    const jitnodes::Network& inside = doc.networkOf(scope);
    int appearance = 0;
    for (jitnodes::NodeId nodeId : inside.nodeIds()) {
        appearance += isAppearanceNodeType(inside.node(nodeId).typeName()) ? 1 : 0;
    }
    EXPECT_EQ(appearance, int(doc.findById(id)->appearance().size()));
    int rootAppearance = 0;
    for (jitnodes::NodeId nodeId : doc.network().nodeIds()) {
        rootAppearance += isAppearanceNodeType(doc.network().node(nodeId).typeName()) ? 1 : 0;
    }
    EXPECT_EQ(rootAppearance, int(doc.object(1).appearance().size())) << "the root keeps the second rectangle's appearance nodes only";
}

namespace {
    int groupSubnetCount(const SketchDocument& doc) {
        int count = 0;
        for (jitnodes::NodeId id : doc.network().nodeIds()) {
            count += doc.network().node(id).typeName() == kSubnetNodeType ? 1 : 0;
        }
        return count;
    }
}

TEST(SketchDocumentScopes, GroupingMovesTheMembersIntoASubnet) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    const std::size_t c = doc.add(SketchObject::makeRect(40, 0, 10, 10)).id();
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());

    const std::uint32_t scope = doc.findById(a)->graphScope();
    EXPECT_NE(scope, 0u);
    EXPECT_EQ(doc.findById(b)->graphScope(), scope);
    EXPECT_EQ(doc.findById(c)->graphScope(), 0u);
    EXPECT_EQ(groupSubnetCount(doc), 1);
    EXPECT_EQ(doc.groupIdOfScope(scope), doc.outermostGroupOf(*doc.findById(a)));
    EXPECT_TRUE(doc.hasNode(doc.findById(a)->graphRef()));
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(b)).x0, 20.0, 1e-3);

    // The group's subnet carries the members: moving the group subnet moves both.
    const NodeRef subnet = doc.parentOfScope(scope);
    doc.setGraphParam(subnet, "translateX", 100.0f);
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(a)).x0, 100.0, 1e-3);
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(b)).x0, 120.0, 1e-3);
    doc.undoStack().undo();

    doc.undoStack().undo();   // the grouping
    EXPECT_EQ(doc.findById(a)->graphScope(), 0u);
    EXPECT_EQ(groupSubnetCount(doc), 0) << "the empty group's subnet goes";
    EXPECT_NEAR(doc.outlineBounds(*doc.findById(b)).x0, 20.0, 1e-3);
    doc.undoStack().redo();
    EXPECT_NE(doc.findById(a)->graphScope(), 0u);
    EXPECT_EQ(groupSubnetCount(doc), 1);

    ASSERT_TRUE(doc.ungroupSelection());
    EXPECT_EQ(doc.findById(a)->graphScope(), 0u);
    EXPECT_EQ(groupSubnetCount(doc), 0);
}

TEST(SketchDocumentScopes, GroupedObjectsKeepTheirNodesThroughAMove) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.setGraphExpression(doc.transformRefOf(a), "translateX", "1 + 2");
    const std::string held = doc.node(doc.transformRefOf(a)).expression("translateX");
    ASSERT_FALSE(held.empty());
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    const NodeRef place = doc.transformRefOf(a);
    EXPECT_NE(place.scope, 0u);
    EXPECT_EQ(doc.node(place).expression("translateX"), held);
}

TEST(SketchDocumentScopes, GroupsSurviveSvgAndBinaryRoundTrips) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(20, 0, 10, 10)).id();
    doc.add(SketchObject::makeRect(40, 0, 10, 10));
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());

    auto check = [](const SketchDocument& loaded) {
        ASSERT_EQ(loaded.objectCount(), 3u);
        const SketchObject& first = loaded.object(0);
        const SketchObject& second = loaded.object(1);
        EXPECT_TRUE(loaded.isGrouped(first));
        EXPECT_EQ(loaded.outermostGroupOf(first), loaded.outermostGroupOf(second));
        EXPECT_NE(first.graphScope(), 0u);
        EXPECT_EQ(first.graphScope(), second.graphScope());
        EXPECT_EQ(loaded.object(2).graphScope(), 0u);
        EXPECT_TRUE(loaded.hasNode(first.graphRef()));
        EXPECT_EQ(groupSubnetCount(loaded), 1);
        EXPECT_NEAR(loaded.outlineBounds(second).x0, 20.0, 1e-3);
    };
    SketchDocument fromSvg;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), fromSvg));
    check(fromSvg);
    SketchDocument fromBinary;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc, true), fromBinary));
    check(fromBinary);
}

TEST(GraphDocument, EditsAreUndoableSteps) {
    GraphDocument doc;
    const jitnodes::NodeId circle = doc.addGraphNode("circle").id;
    doc.setGraphParam(circle, "radius", 5.0f);
    EXPECT_EQ(doc.network().node(circle).getFloat("radius"), 5.0f);
    doc.undoStack().undo();
    EXPECT_EQ(doc.network().node(circle).getFloat("radius"), 1.0f);
    doc.undoStack().undo();
    EXPECT_TRUE(doc.network().nodeIds().empty());
    doc.undoStack().redo();
    doc.undoStack().redo();
    EXPECT_EQ(doc.network().node(circle).getFloat("radius"), 5.0f);
}

TEST(GraphDocument, FailedLoadKeepsTheGraph) {
    GraphDocument doc;
    doc.addGraphNode("circle");
    const std::string text = doc.graphToText();
    EXPECT_FALSE(doc.graphFromText("not a graph"));
    EXPECT_EQ(doc.graphToText(), text);
    GraphDocument other;
    EXPECT_TRUE(other.graphFromText(text));
    EXPECT_EQ(other.graphToText(), text);
}

TEST(LiveShape, OutlineFollowsTheNodeAndUndo) {
    SketchDocument doc;
    SketchObject* shape = doc.addLiveShape("circle", {{"count", 16.0f}, {"radius", 10.0f}}, "Circle");
    ASSERT_NE(shape, nullptr);
    const std::size_t id = shape->id();
    EXPECT_NEAR(shape->geometryBounds().x1 - shape->geometryBounds().x0, 20.0, 0.01);
    EXPECT_TRUE(doc.setLiveShapeParam(id, "radius", 30.0f));
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1 - doc.findById(id)->geometryBounds().x0, 60.0, 0.01);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1 - doc.findById(id)->geometryBounds().x0, 20.0, 0.01);
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id), nullptr);
    for (auto nid : doc.network().nodeIds()) { std::printf("left: %u %s\n", nid, doc.network().node(nid).typeName().c_str()); }
    EXPECT_TRUE(doc.network().nodeIds().empty());
    doc.undoStack().redo();
    ASSERT_NE(doc.findById(id), nullptr);
    EXPECT_EQ(doc.network().nodeIds().size(), 3u) << "the shape, its fill and its stroke";
}

TEST(LiveShape, SurvivesSvgRoundTrip) {
    SketchDocument doc;
    const std::size_t id = doc.addLiveShape("circle", {{"count", 16.0f}, {"radius", 10.0f}}, "Circle")->id();
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    ASSERT_EQ(loaded.objectCount(), 1u);
    const SketchObject& object = loaded.object(0);
    EXPECT_NE(object.graphNode(), 0u);
    EXPECT_TRUE(loaded.setLiveShapeParam(object.id(), "radius", 30.0f));
    EXPECT_NEAR(loaded.findById(object.id())->geometryBounds().x1 - loaded.findById(object.id())->geometryBounds().x0, 60.0, 0.01);
    (void)id;
}

namespace {
// True when every object has exactly one curvyPath node, holding its outline, and no other curvyPath node exists.
BLPath drawn(const SketchObject& object) {
    BLMatrix2D back = object.paintToPage();
    back.invert();
    BLPath path = object.geometry();
    path.transform(back);
    return path;
}

bool pathNodesMatch(const SketchDocument& doc) {
    std::size_t nodes = 0;
    for (jitnodes::NodeId id : doc.network().nodeIds()) {
        if (isShapeNodeType(doc.network().node(id).typeName())) {
            ++nodes;
        }
    }
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& object = doc.object(i);
        if (object.graphNode() == 0 || doc.network().node(object.graphNode()).typeName() != shapeNodeType(object) ||
            !sameCurveShape(*doc.network().node(object.graphNode()).geometryParam("curve"), *geometryFromPath(drawn(object)), 1e-4)) {
            return false;
        }
    }
    return nodes == doc.objectCount();
}
}

TEST(PathNode, TextRoundTripsCurves) {
    BLPath path;
    path.move_to(1.5, 2.25);
    path.line_to(10, 20);
    path.quad_to(12, 22, 14, 18);
    path.cubic_to(15, 1, 16, 2, 17, 3);
    path.close();
    path.move_to(-4, -5);
    path.line_to(-6, -7);
    BLPath back;
    ASSERT_TRUE(pathFromText(pathToText(path), back));
    EXPECT_EQ(pathToText(back), pathToText(path));
    EXPECT_EQ(back.size(), path.size());
    EXPECT_FALSE(pathFromText("M 1 2 X 3 4", back));
}

TEST(PathNode, CooksCubicPrimitivesAndFlattensOnRequest) {
    SketchDocument doc;
    doc.add(SketchObject::makeEllipse(50, 50, 20, 10));
    const jitnodes::GeometryHandle geometry = doc.cook(doc.object(0).graphNode());
    ASSERT_EQ(geometry->primitiveCount(), 1u);
    const jitnodes::Primitive& prim = geometry->primitives().front();
    EXPECT_EQ(prim.kind, jitnodes::PrimKind::cubic);
    EXPECT_TRUE(prim.closed);
    EXPECT_EQ(jitnodes::cubicSegments(prim), 4u);
    EXPECT_EQ(geometry->pointCount(), 12u);   // four anchors, each with two handles

    const jitnodes::Geometry flat = jitnodes::flattenCurves(*geometry, 0.05f);
    EXPECT_FALSE(jitnodes::hasCurves(flat));
    EXPECT_GT(flat.pointCount(), 12u);
    float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
    for (std::size_t i = 0; i < flat.pointCount(); ++i) {
        left = std::min(left, flat.x()[i]);
        right = std::max(right, flat.x()[i]);
        top = std::min(top, flat.y()[i]);
        bottom = std::max(bottom, flat.y()[i]);
    }
    EXPECT_NEAR(left, 30.0f, 0.1f);
    EXPECT_NEAR(right, 70.0f, 0.1f);
    EXPECT_NEAR(top, 40.0f, 0.1f);
    EXPECT_NEAR(bottom, 60.0f, 0.1f);
}

TEST(PathNode, LinesQuadsAndOpenFiguresBecomeCubics) {
    SketchDocument doc;
    BLPath path;
    path.move_to(0, 0);
    path.line_to(30, 0);
    path.quad_to(45, 15, 30, 30);
    doc.add(SketchObject::makePath(path));
    const jitnodes::GeometryHandle geometry = doc.cook(doc.object(0).graphNode());
    ASSERT_EQ(geometry->primitiveCount(), 1u);
    const jitnodes::Primitive& prim = geometry->primitives().front();
    EXPECT_FALSE(prim.closed);
    EXPECT_EQ(jitnodes::cubicSegments(prim), 2u);
    EXPECT_NEAR(geometry->x()[3], 30.0f, 1e-4f);   // the anchor after the line
    EXPECT_NEAR(geometry->x()[1], 0.0f, 1e-4f);    // a line's handles sit on its anchors
    EXPECT_NEAR(geometry->x()[2], 30.0f, 1e-4f);
    EXPECT_NEAR(geometry->x()[4], 40.0f, 1e-4f);   // 30 + 2/3 * (45 - 30): the quad raised to a cubic
    EXPECT_NEAR(geometry->y()[4], 10.0f, 1e-4f);
}

TEST(PathNode, EveryObjectHasANodeThroughEdits) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.add(SketchObject::makeEllipse(40, 40, 8, 8));
    EXPECT_TRUE(pathNodesMatch(doc));

    doc.setSelection({a});
    const auto before = doc.snapshotSelection();
    doc.placeObjects(before, BLMatrix2D::make_scaling(2.0));
    doc.commitPlacement(before, "Resize");
    EXPECT_TRUE(pathNodesMatch(doc));
    doc.undoStack().undo();
    EXPECT_TRUE(pathNodesMatch(doc));
    doc.undoStack().redo();
    EXPECT_TRUE(pathNodesMatch(doc));

    const jitnodes::NodeId node = doc.findById(a)->graphNode();
    doc.deleteSelection();
    EXPECT_TRUE(pathNodesMatch(doc));
    EXPECT_EQ(doc.network().nodeIds().size(), 4u) << "the other object's shape, transform, fill and stroke";
    doc.undoStack().undo();
    ASSERT_NE(doc.findById(a), nullptr);
    EXPECT_EQ(doc.findById(a)->graphNode(), node);
    EXPECT_TRUE(pathNodesMatch(doc));

    doc.setSelection({a});
    doc.duplicateSelection(5, 5);
    EXPECT_EQ(doc.objectCount(), 3u);
    EXPECT_TRUE(pathNodesMatch(doc));
}

TEST(PathNode, EditingTheNodeMovesTheObjectAndUndoes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::string original = pathToText(doc.findById(a)->geometry());
    BLPath wide;
    wide.move_to(0, 0);
    wide.line_to(40, 0);
    wide.line_to(40, 20);
    wide.line_to(0, 20);
    wide.close();
    doc.setGraphGeometry(doc.findById(a)->graphNode(), "curve", geometryFromPath(wide));
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 40.0, 1e-6);
    EXPECT_TRUE(pathNodesMatch(doc));
    doc.undoStack().undo();
    EXPECT_EQ(pathToText(doc.findById(a)->geometry()), original);
    EXPECT_TRUE(pathNodesMatch(doc));
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x1, 40.0, 1e-6);
    EXPECT_TRUE(pathNodesMatch(doc));
}

TEST(PathNode, UndoAndRedoKeepTheExactCoordinates) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeEllipse(1234.56789123, 987.654321987, 321.123456789, 55.5555555)).id();
    doc.setSelection({a});
    const BLPath start = doc.findById(a)->geometry();
    const auto before = doc.snapshotSelection();
    doc.placeObjects(before, BLMatrix2D::make_rotation(0.7, 100.1, 200.2));
    doc.commitPlacement(before, "Rotate");
    const BLPath moved = doc.findById(a)->geometry();
    for (int i = 0; i < 3; ++i) {
        doc.undoStack().undo();
        EXPECT_TRUE(doc.findById(a)->geometry().equals(start));
        doc.undoStack().redo();
        EXPECT_TRUE(doc.findById(a)->geometry().equals(moved));
    }
    EXPECT_TRUE(pathNodesMatch(doc));
}

TEST(PathNode, TextSymbolAndImageObjectsHaveNodesToo) {
    SketchDocument doc;
    const std::size_t text = doc.add(SketchObject::makeText("Hello\nthere", "", 24.0, BLPoint(10, 40))).id();
    doc.add(SketchObject::makeRect(0, 0, 10, 10)).setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    doc.setSelection({doc.object(1).id()});
    const std::size_t symbol = doc.makeSymbol("red");
    ASSERT_NE(symbol, 0u);
    const std::size_t instance = doc.placeSymbol(symbol, BLPoint(100, 100));
    const std::size_t picture = doc.placeImage(decodeImage(twoColorPng(40, 20), "two.png"), BLPoint(50, 50));
    ASSERT_NE(instance, 0u);
    ASSERT_NE(picture, 0u);
    EXPECT_TRUE(pathNodesMatch(doc));
    const jitnodes::Network& net = doc.network();
    EXPECT_EQ(net.node(doc.findById(text)->graphNode()).typeName(), std::string(kTextNodeType));
    EXPECT_EQ(net.node(doc.findById(text)->graphNode()).getString("text"), "Hello\nthere");
    EXPECT_EQ(net.node(doc.findById(instance)->graphNode()).getString("symbol"), std::to_string(symbol));
    EXPECT_EQ(net.node(doc.findById(picture)->graphNode()).getString("image"), "two.png");

    SketchObject* object = doc.findById(text);
    doc.setSelection({text});
    setWords(doc, text, "Changed");
    doc.touch();
    EXPECT_EQ(net.node(object->graphNode()).getString("text"), "Changed");
    EXPECT_TRUE(pathNodesMatch(doc));
}

TEST(PathNode, EditingATextNodeRebuildsTheText) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeText("One", "", 20.0, BLPoint(5, 30))).id();
    const jitnodes::NodeId node = doc.findById(id)->graphNode();
    const BLBox start = doc.findById(id)->geometryBounds();
    doc.setGraphString(node, "text", "One Two Three");
    EXPECT_EQ(doc.findById(id)->text(), "One Two Three");
    EXPECT_GT(doc.findById(id)->geometryBounds().x1, start.x1 + 10.0);
    EXPECT_TRUE(pathNodesMatch(doc));
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(id)->text(), "One");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1, start.x1, 1e-6);
    EXPECT_TRUE(pathNodesMatch(doc));
}

TEST(PathNode, AttributesOnAnchorsSurviveMovesAndUndo) {
    SketchDocument doc;
    BLPath curve;
    curve.move_to(0, 0);
    curve.cubic_to(10, 20, 30, 20, 40, 0);
    const std::size_t id = doc.add(SketchObject::makePath(curve)).id();
    const jitnodes::NodeId node = doc.findById(id)->graphNode();
    auto drawn = std::make_shared<jitnodes::Geometry>(*doc.network().node(node).geometryParam("curve"));
    drawn->attrib(jitnodes::AttribClass::point, "pressure").floats()[0] = 0.25f;
    drawn->attrib(jitnodes::AttribClass::point, "pressure").floats()[3] = 0.75f;
    doc.setGraphGeometry(node, "curve", drawn);
    auto pressure = [&](std::size_t i) { return doc.network().node(node).geometryParam("curve")->findAttrib(jitnodes::AttribClass::point, "pressure")->at(i); };
    EXPECT_NEAR(pressure(3), 0.75f, 1e-6f);

    doc.setSelection({id});
    const auto before = doc.snapshotSelection();
    doc.placeObjects(before, BLMatrix2D::make_translation(100, 5));
    doc.commitPlacement(before, "Move");
    ASSERT_NE(doc.network().node(node).geometryParam("curve")->findAttrib(jitnodes::AttribClass::point, "pressure"), nullptr);
    EXPECT_NEAR(pressure(0), 0.25f, 1e-6f);
    EXPECT_NEAR(doc.network().node(node).geometryParam("curve")->x()[0], 0.0f, 1e-4f);   // the curve stays as drawn; the transform node holds the move
    EXPECT_NEAR(doc.findById(id)->paintToPage().m20, 100.0, 1e-9);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.network().node(node).geometryParam("curve")->x()[0], 0.0f, 1e-4f);
    EXPECT_NEAR(pressure(3), 0.75f, 1e-6f);
}

TEST(PathNode, SurviveSaveAndLoad) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(0, 0, 10, 10));
    doc.add(SketchObject::makeEllipse(40, 40, 8, 8));
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    EXPECT_EQ(loaded.objectCount(), 2u);
    EXPECT_TRUE(pathNodesMatch(loaded));
    EXPECT_EQ(loaded.graphToText(), doc.graphToText());
}

TEST(PathNode, AFileWithoutAGraphGetsNodesOnLoad) {
    SketchDocument doc;
    doc.add(SketchObject::makeRect(0, 0, 10, 10));
    std::string svg = svgFromDocument(doc);
    const std::size_t start = svg.find("<metadata>");
    const std::size_t end = svg.find("</metadata>");
    ASSERT_NE(start, std::string::npos);
    svg.erase(start, end + 11 - start);
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svg, loaded));
    EXPECT_TRUE(pathNodesMatch(loaded));
}

TEST(TransformNode, HoldsWhereTheObjectSitsAndEditsBothWays) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 100, 50)).id();
    const jitnodes::NodeId shape = doc.findById(id)->graphNode();
    auto placeOf = [&]() {
        for (jitnodes::NodeId n : doc.network().nodeIds()) {
            if (doc.network().node(n).typeName() == kTransformNodeType && doc.network().inputOf(n, 0) == shape) {
                return n;
            }
        }
        return jitnodes::invalidNodeId;
    };
    const jitnodes::NodeId place = placeOf();
    ASSERT_NE(place, jitnodes::invalidNodeId);

    doc.setSelection({id});
    doc.transformObjects({id}, BLMatrix2D(1.0, 0.0, 0.5, 1.0, 40.0, 7.0), "Shear");
    const BLMatrix2D held = transformOf(doc.network().node(place));
    EXPECT_NEAR(held.m10, 0.5, 1e-6);
    EXPECT_NEAR(held.m20, 40.0, 1e-4);
    EXPECT_NEAR(doc.network().node(shape).geometryParam("curve")->x()[0], 10.0f, 1e-4f) << "the shape node keeps the outline as drawn";
    const jitnodes::GeometryHandle out = doc.cook(place);
    BLBox box = doc.findById(id)->geometryBounds();
    float minX = out->x()[0], maxX = out->x()[0];
    for (std::size_t i = 0; i < out->pointCount(); ++i) {
        minX = std::min(minX, out->x()[i]);
        maxX = std::max(maxX, out->x()[i]);
    }
    EXPECT_NEAR(minX, box.x0, 1e-3);
    EXPECT_NEAR(maxX, box.x1, 1e-3);

    const float pivotMoved = doc.network().node(place).baseFloat("translateX");   // translate moves the pivot, so it is not the matrix's e
    EXPECT_NEAR(doc.network().node(place).baseFloat("shear"), std::atan(0.5) * 180.0 / 3.14159265358979, 1e-3);
    EXPECT_NEAR(doc.network().node(place).baseFloat("pivotX"), 60.0f, 1e-3f) << "a new node pivots about the middle of the shape";
    EXPECT_NEAR(doc.network().node(place).baseFloat("pivotY"), 35.0f, 1e-3f);
    doc.setGraphParam(place, "translateX", pivotMoved + 60.0f);
    EXPECT_NEAR(doc.findById(id)->paintToPage().m20, 100.0, 1e-4);
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, box.x0 + 60.0, 1e-3) << "editing the matrix in the graph moves the object";
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, box.x0, 1e-3);
    EXPECT_NEAR(doc.findById(id)->paintToPage().m20, 40.0, 1e-4);

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_NEAR(copy.object(0).paintToPage().m10, 0.5, 1e-5);
    EXPECT_NEAR(copy.object(0).geometryBounds().x0, box.x0, 1e-3);
}

TEST(TransformNode, ANewShapeStartsWithNothingToTransform) {
    SketchDocument doc;
    std::vector<BLPoint> corners;
    for (int i = 0; i < 5; ++i) {
        const double a = 6.283185307179586 * i / 5.0;
        corners.push_back(BLPoint(400.0 + 90.0 * std::cos(a), 300.0 + 90.0 * std::sin(a)));
    }
    const std::size_t addedId = doc.addUndoable(SketchObject::makePolygon(corners), "Draw Polygon").id();
    const jitnodes::NodeId place = doc.transformNodeOf(addedId);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    const jitnodes::Node& node = doc.network().node(place);
    for (const char* name : {"translateX", "translateY", "rotation", "shear"}) {
        EXPECT_NEAR(node.baseFloat(name), 0.0f, 1e-4f) << name;
    }
    EXPECT_NEAR(node.baseFloat("scaleX"), 1.0f, 1e-5f);
    EXPECT_NEAR(node.baseFloat("pivotX"), 400.0f, 90.0f);
}

TEST(TransformNode, ThePivotIsTheCanvasOriginAndSurvivesReselecting) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 100, 50)).id();
    const std::size_t other = doc.add(SketchObject::makeRect(300, 10, 20, 20)).id();
    doc.setSelection({id});
    doc.transformObjects({id}, BLMatrix2D::make_translation(40.0, 0.0), "Move");
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    const BLBox before = doc.findById(id)->geometryBounds();

    doc.setSelectionOrigin(frame, BLPoint(200.0, 90.0));
    const BLPoint origin = doc.selectionOrigin(frame);
    EXPECT_NEAR(origin.x, 200.0, 1e-3);
    EXPECT_NEAR(origin.y, 90.0, 1e-3);
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    const jitnodes::Node& node = doc.network().node(place);
    EXPECT_NEAR(node.baseFloat("pivotX") + node.baseFloat("translateX"), 200.0, 1e-3) << "translate moves the pivot, which sits at the origin on the page";
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, before.x0, 1e-3) << "moving the pivot does not move the shape";

    doc.undoStack().undo();
    EXPECT_NEAR(doc.selectionOrigin(frame).x, before.x0 + 50.0, 1e-3) << "moving the pivot is one undo step, back to the middle";
    doc.undoStack().redo();
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 200.0, 1e-3);

    doc.setSelection({other});
    doc.setSelection({id});
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 200.0, 1e-3) << "the pivot is kept by the node, not reset to the middle";
    doc.transformObjects({id}, BLMatrix2D::make_translation(5.0, 0.0), "Move");
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 205.0, 1e-3) << "and it travels with the shape";

    doc.setSelectionOrigin(frame, BLPoint(210.0, 90.0), true);
    doc.setSelectionOrigin(frame, BLPoint(220.0, 95.0), true);
    doc.setSelectionOrigin(frame, BLPoint(230.0, 99.0), true);
    doc.finishSelectionOrigin();
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 230.0, 1e-3);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.selectionOrigin(frame).x, 205.0, 1e-3) << "a whole drag of the pivot is one undo step";
}

TEST(TransformNode, AChildFollowsItsParentAndOnlyThat) {
    SketchDocument doc;
    const std::size_t parent = doc.add(SketchObject::makeRect(0, 0, 100, 100)).id();
    const std::size_t child = doc.add(SketchObject::makeRect(200, 0, 50, 50)).id();
    auto placeOf = [&](std::size_t id) {
        const jitnodes::NodeId shape = doc.findById(id)->graphNode();
        for (jitnodes::NodeId n : doc.network().nodeIds()) {
            if (doc.network().node(n).typeName() == kTransformNodeType && doc.network().inputOf(n, 0) == shape) {
                return n;
            }
        }
        return jitnodes::invalidNodeId;
    };
    auto x0 = [&](std::size_t id) { return doc.findById(id)->geometryBounds().x0; };
    auto y0 = [&](std::size_t id) { return doc.findById(id)->geometryBounds().y0; };
    auto agrees = [&](std::size_t id) {
        const jitnodes::GeometryHandle out = doc.cook(placeOf(id));
        BLBox box = doc.findById(id)->geometryBounds();
        float lo = out->x()[0], hi = out->x()[0];
        for (std::size_t i = 0; i < out->pointCount(); ++i) {
            lo = std::min(lo, out->x()[i]);
            hi = std::max(hi, out->x()[i]);
        }
        return std::fabs(lo - box.x0) < 1e-2 && std::fabs(hi - box.x1) < 1e-2;
    };

    EXPECT_FALSE(doc.setParent(parent, parent));
    ASSERT_TRUE(doc.setParent(child, parent));
    EXPECT_FALSE(doc.setParent(parent, child)) << "a loop is refused";
    EXPECT_EQ(doc.network().inputOf(placeOf(child), 1), placeOf(parent)) << "the child's transform node hangs from the parent's";
    EXPECT_NEAR(x0(child), 200.0, 1e-3) << "parenting keeps where the child sits";
    EXPECT_TRUE(agrees(child));

    const BLMatrix2D localBefore = transformOf(doc.network().node(placeOf(child)));
    doc.transformObjects({parent}, BLMatrix2D::make_translation(30.0, 0.0), "Move");
    EXPECT_NEAR(x0(parent), 30.0, 1e-3);
    EXPECT_NEAR(x0(child), 230.0, 1e-3) << "moving the parent carries the child";
    EXPECT_TRUE(agrees(child));
    EXPECT_NEAR(transformOf(doc.network().node(placeOf(child))).m20, localBefore.m20, 1e-3) << "the child's own placement is unchanged";

    BLMatrix2D turn = BLMatrix2D::make_rotation(1.5707963267948966, 80.0, 50.0);
    doc.transformObjects({parent}, turn, "Rotate");
    const BLPoint expected = turn.map_point(230.0, 50.0);   // the child's old corner
    EXPECT_NEAR(doc.findById(child)->geometryBounds().x0 + doc.findById(child)->geometryBounds().x1,
        2.0 * turn.map_point(255.0, 25.0).x, 1e-2) << "the child turns about the parent's pivot";
    (void)expected;
    EXPECT_TRUE(agrees(child));
    EXPECT_TRUE(agrees(parent));

    doc.transformObjects({child}, BLMatrix2D::make_translation(0.0, 10.0), "Move");
    const double parentY = y0(parent);
    doc.transformObjects({child}, BLMatrix2D::make_translation(0.0, 5.0), "Move");
    EXPECT_NEAR(y0(parent), parentY, 1e-3) << "moving the child leaves the parent";
    EXPECT_TRUE(agrees(child));

    const double before = x0(child);
    doc.setGraphParam(placeOf(parent), "translateX", doc.network().node(placeOf(parent)).baseFloat("translateX") + 10.0f);
    EXPECT_NEAR(x0(child), before + 10.0, 1e-2) << "editing the parent's node in the graph moves the child";
    EXPECT_TRUE(agrees(child));
    doc.undoStack().undo();
    EXPECT_NEAR(x0(child), before, 1e-2);

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 2u);
    EXPECT_EQ(copy.object(1).parent(), copy.object(0).id()) << "the link is in the graph, so a saved file keeps it";
    EXPECT_NEAR(copy.object(1).geometryBounds().x0, x0(child), 1e-2);

    doc.undoStack().undo();
    doc.undoStack().undo();
    doc.undoStack().undo();
    doc.undoStack().undo();
    EXPECT_TRUE(doc.setParent(child, 0));
    EXPECT_EQ(doc.network().inputOf(placeOf(child), 1), jitnodes::invalidNodeId);
    doc.transformObjects({parent}, BLMatrix2D::make_translation(5.0, 0.0), "Move");
    EXPECT_NEAR(x0(child), 200.0, 1e-2) << "released, it stays";
}

TEST(TransformNode, ParentingTheSelectionIsOneUndoStep) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(100, 0, 10, 10)).id();
    const std::size_t c = doc.add(SketchObject::makeRect(200, 0, 10, 10)).id();

    doc.setSelection({a});
    EXPECT_FALSE(doc.canParentSelection());
    EXPECT_FALSE(doc.canReleaseParent());

    doc.setSelection({a, b, c});
    ASSERT_TRUE(doc.parentSelection());
    EXPECT_EQ(doc.findById(a)->parent(), c);
    EXPECT_EQ(doc.findById(b)->parent(), c) << "the last selected is the parent of all the others";
    EXPECT_EQ(doc.findById(c)->parent(), 0u);
    EXPECT_FALSE(doc.parentSelection()) << "nothing changes the second time";

    doc.transformObjects({c}, BLMatrix2D::make_translation(7.0, 0.0), "Move");
    EXPECT_NEAR(doc.findById(a)->geometryBounds().x0, 7.0, 1e-3);

    doc.undoStack().undo();
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(a)->parent(), 0u);
    EXPECT_EQ(doc.findById(b)->parent(), 0u) << "one undo frees them all";
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(b)->parent(), c);

    doc.setSelection({a});
    EXPECT_TRUE(doc.canReleaseParent());
    ASSERT_TRUE(doc.releaseParent());
    EXPECT_EQ(doc.findById(a)->parent(), 0u);
    EXPECT_EQ(doc.findById(b)->parent(), c) << "only the selection is released";
    doc.undoStack().undo();
    EXPECT_EQ(doc.findById(a)->parent(), c);

    doc.setSelection({c, a});
    EXPECT_FALSE(doc.parentSelection()) << "the parent cannot become its own child's child";
    EXPECT_EQ(doc.findById(c)->parent(), 0u);
}

namespace {
    std::vector<int> pointIds(const SketchDocument& doc, std::size_t objectId) {
        const jitnodes::GeometryHandle& curve = doc.network().node(doc.findById(objectId)->graphNode()).geometryParam("curve");
        const jitnodes::AttribArray* ids = curve->findAttrib(jitnodes::AttribClass::point, kPointIdAttrib);
        std::vector<int> result;
        for (std::size_t i = 0; ids != nullptr && i < ids->size(); ++i) {
            result.push_back(ids->ints()[i]);
        }
        return result;
    }

    std::vector<int> taggedPoints(const jitnodes::Geometry& geometry, const std::string& group) {
        std::vector<int> flags;
        const jitnodes::AttribArray* array = geometry.findAttrib(jitnodes::AttribClass::point, "group." + group);
        for (std::size_t i = 0; array != nullptr && i < array->size(); ++i) {
            flags.push_back(array->ints()[i]);
        }
        return flags;
    }

    jitnodes::NodeId addTag(SketchDocument& doc, std::size_t objectId, const std::map<std::string, std::string>& strings, float every = 0.0f) {
        jitnodes::Network& net = doc.network();
        const jitnodes::NodeId tag = net.addNode(kTagNodeType, net.uniqueName("tag"));
        net.connect(doc.findById(objectId)->graphNode(), tag, 0);
        for (const auto& [name, value] : strings) {
            net.node(tag).setString(name, value);
        }
        net.node(tag).setFloat("every", every);
        return tag;
    }
}

TEST(PointIds, PointsKeepTheirIdsWhileTheShapeIsEdited) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(0, 0, 40, 20)).id();
    EXPECT_EQ(pointIds(doc, id), (std::vector<int>{0, 1, 2, 3})) << "a new shape numbers its points";

    SketchObject* object = doc.findById(id);
    BLPath moved;
    moved.move_to(0, 0);
    moved.line_to(50, 0);
    moved.line_to(40, 20);
    moved.line_to(0, 20);
    moved.close();
    doc.setObjectOutline(*object, moved);
    doc.touch();
    EXPECT_EQ(pointIds(doc, id), (std::vector<int>{0, 1, 2, 3})) << "moving a point keeps the ids";

    BLPath fewer;
    fewer.move_to(0, 0);
    fewer.line_to(50, 0);
    fewer.line_to(0, 20);
    fewer.close();
    doc.setObjectOutline(*object, fewer);
    doc.touch();
    EXPECT_EQ(pointIds(doc, id), (std::vector<int>{0, 1, 3})) << "deleting the third point leaves the others their ids";

    BLPath more;
    more.move_to(0, 0);
    more.line_to(25, 0);
    more.line_to(50, 0);
    more.line_to(0, 20);
    more.close();
    doc.setObjectOutline(*object, more);
    doc.touch();
    const std::vector<int> ids = pointIds(doc, id);
    ASSERT_EQ(ids.size(), 4u);
    EXPECT_EQ(ids[0], 0);
    EXPECT_EQ(ids[2], 1);
    EXPECT_EQ(ids[3], 3);
    EXPECT_GT(ids[1], 3) << "the inserted point gets an id nobody had";

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_EQ(pointIds(copy, copy.object(0).id()), ids) << "a saved file keeps the ids";
}

TEST(PointTags, TheTagNodeSelectsPointsByIdRuleAndRegion) {
    SketchDocument doc;
    const std::size_t rect = doc.add(SketchObject::makeRect(0, 0, 40, 20)).id();

    jitnodes::NodeId tag = addTag(doc, rect, {{"group", "left"}, {"ids", "0,3"}});
    EXPECT_EQ(taggedPoints(*doc.cook(tag), "left"), (std::vector<int>{1, 0, 0, 1})) << "by id";
    EXPECT_EQ(doc.cook(tag)->pointCount(), 4u) << "the tag passes the shape through";

    tag = addTag(doc, rect, {{"group", "even"}}, 2.0f);
    EXPECT_EQ(taggedPoints(*doc.cook(tag), "even"), (std::vector<int>{1, 0, 1, 0})) << "every second point";

    tag = addTag(doc, rect, {{"group", "top"}, {"box", "-1 -1 41 1"}});
    EXPECT_EQ(taggedPoints(*doc.cook(tag), "top"), (std::vector<int>{1, 1, 0, 0})) << "inside a box";

    tag = addTag(doc, rect, {{"group", "g"}, {"ids", "0-1"}});
    const jitnodes::NodeId second = addTag(doc, rect, {{"group", "g"}, {"ids", "1-2"}, {"operation", "union"}});
    doc.network().connect(tag, second, 0);
    EXPECT_EQ(taggedPoints(*doc.cook(second), "g"), (std::vector<int>{1, 1, 1, 0})) << "union with the group already there";
    doc.network().node(second).setString("operation", "subtract");
    EXPECT_EQ(taggedPoints(*doc.cook(second), "g"), (std::vector<int>{1, 0, 0, 0})) << "subtract";
    doc.network().node(second).setString("operation", "intersect");
    EXPECT_EQ(taggedPoints(*doc.cook(second), "g"), (std::vector<int>{0, 1, 0, 0})) << "intersect";

    const std::size_t ellipse = doc.add(SketchObject::makeEllipse(100, 0, 40, 40)).id();
    tag = addTag(doc, ellipse, {{"group", "all"}, {"kind", "any"}}, 1.0f);
    const jitnodes::GeometryHandle curve = doc.cook(tag);
    ASSERT_EQ(curve->pointCount(), 12u) << "four curved segments: anchors and handles";
    int tagged = 0;
    for (int flag : taggedPoints(*curve, "all")) {
        tagged += flag;
    }
    EXPECT_EQ(tagged, 12) << "handles follow their anchors";
    doc.network().node(tag).setString("kind", "anchors");
    tagged = 0;
    for (int flag : taggedPoints(*doc.cook(tag), "all")) {
        tagged += flag;
    }
    EXPECT_EQ(tagged, 4);
    doc.network().node(tag).setString("kind", "corners");
    tagged = 0;
    for (int flag : taggedPoints(*doc.cook(tag), "all")) {
        tagged += flag;
    }
    EXPECT_EQ(tagged, 0) << "an ellipse is all smooth";
    doc.network().node(tag).setString("kind", "smooth");
    tagged = 0;
    for (int flag : taggedPoints(*doc.cook(tag), "all")) {
        tagged += flag;
    }
    EXPECT_EQ(tagged, 12);
}

TEST(PointTags, TaggingPointsOfAnObjectIsUndoable) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 40, 20)).id();
    const jitnodes::NodeId place = doc.transformNodeOf(id);

    EXPECT_EQ(doc.pointIdNear(id, BLPoint(50, 10), 1.0), 1) << "the top right corner is point 1";
    EXPECT_EQ(doc.pointIdNear(id, BLPoint(30, 20), 1.0), -1) << "nothing there";
    EXPECT_TRUE(doc.tagNames(id).empty());

    ASSERT_TRUE(doc.tagPoints(id, "pins", {1, 2}, true));
    EXPECT_EQ(doc.tagNames(id), (std::vector<std::string>{"pins"}));
    EXPECT_EQ(doc.taggedIds(id, "pins"), (std::vector<int>{1, 2}));
    EXPECT_EQ(doc.transformNodeOf(id), place) << "the tag sits in the chain without displacing the placement";
    EXPECT_EQ(taggedPoints(*doc.cook(place), "pins"), (std::vector<int>{0, 1, 1, 0}));
    EXPECT_EQ(doc.taggedPositions(id, "pins").size(), 2u);
    EXPECT_FALSE(doc.tagPoints(id, "pins", {1}, true)) << "already tagged: nothing to do";

    doc.transformObjects({id}, BLMatrix2D::make_translation(100.0, 0.0), "Move");
    EXPECT_NEAR(doc.taggedPositions(id, "pins")[0].x, 150.0, 1e-3) << "tagged points are reported where they are on the page";
    EXPECT_EQ(doc.pointIdNear(id, BLPoint(150, 10), 1.0), 1);
    doc.undoStack().undo();

    ASSERT_TRUE(doc.tagPoints(id, "pins", {1}, false));
    EXPECT_EQ(doc.taggedIds(id, "pins"), (std::vector<int>{2}));
    doc.undoStack().undo();
    EXPECT_EQ(doc.taggedIds(id, "pins"), (std::vector<int>{1, 2})) << "untagging is one undo step";

    ASSERT_TRUE(doc.tagPoints(id, "other", {0}, true));
    EXPECT_EQ(doc.tagNames(id).size(), 2u) << "tags chain; each keeps its own group";
    ASSERT_TRUE(doc.clearTag(id, "pins"));
    EXPECT_EQ(doc.tagNames(id), (std::vector<std::string>{"other"}));
    EXPECT_EQ(doc.transformNodeOf(id), place);
    doc.undoStack().undo();
    EXPECT_EQ(doc.taggedIds(id, "pins"), (std::vector<int>{1, 2})) << "clearing a tag is undoable";

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_EQ(copy.taggedIds(copy.object(0).id(), "pins"), (std::vector<int>{1, 2})) << "a saved file keeps the tags";
    EXPECT_NE(copy.transformNodeOf(copy.object(0).id()), jitnodes::invalidNodeId);
}

TEST(Expressions, FieldTextIsANumberOrAnExpression) {
    EXPECT_EQ(parseFieldEntry("  ").kind, FieldEntry::Kind::Nothing);
    EXPECT_EQ(parseFieldEntry("").kind, FieldEntry::Kind::Nothing);
    FieldEntry number = parseFieldEntry(" -12.5 ");
    EXPECT_EQ(number.kind, FieldEntry::Kind::Number);
    EXPECT_DOUBLE_EQ(number.number, -12.5);
    EXPECT_EQ(parseFieldEntry("1e-3").kind, FieldEntry::Kind::Number) << "scientific notation is still a number";
    FieldEntry plain = parseFieldEntry("sin(time) * 50");
    EXPECT_EQ(plain.kind, FieldEntry::Kind::Expression) << "text that is not a number is an expression";
    EXPECT_EQ(plain.expression, "sin(time) * 50");
    FieldEntry marked = parseFieldEntry("= frame / 2");
    EXPECT_EQ(marked.kind, FieldEntry::Kind::Expression);
    EXPECT_EQ(marked.expression, "frame / 2") << "the = is not part of the expression";
    FieldEntry cleared = parseFieldEntry("=");
    EXPECT_EQ(cleared.kind, FieldEntry::Kind::Expression);
    EXPECT_TRUE(cleared.expression.empty()) << "a lone = takes the expression away";
    EXPECT_EQ(parseFieldEntry("12abc").kind, FieldEntry::Kind::Expression) << "half a number is not a number";
}

TEST(Expressions, AnExpressionOnAPlacementFollowsTheTime) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 40, 20)).id();
    const std::size_t child = doc.add(SketchObject::makeRect(200, 10, 10, 10)).id();
    ASSERT_TRUE(doc.setParent(child, id));
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    auto left = [&](std::size_t object) { return doc.findById(object)->geometryBounds().x0; };

    EXPECT_EQ(doc.setGraphExpression(place, "translateX", "sin(time)*50"), "");
    doc.setTime(0.0);
    EXPECT_NEAR(left(id), 10.0, 1e-3);
    doc.setTime(1.5707963267948966);
    EXPECT_NEAR(left(id), 60.0, 1e-2) << "sin(pi/2) * 50 further along";
    EXPECT_NEAR(left(child), 250.0, 1e-2) << "a child of an animated parent follows it";
    doc.setTime(1.0);
    EXPECT_NEAR(left(id), 10.0 + 50.0 * std::sin(1.0), 1e-2);
    EXPECT_NEAR(doc.time(), 1.0, 1e-9);

    EXPECT_NE(doc.setGraphExpression(place, "translateX", "sin(("), "") << "a bad expression is refused with a message";
    EXPECT_EQ(doc.network().node(place).expression("translateX"), "sin(time)*50") << "and changes nothing";
    EXPECT_NEAR(left(id), 10.0 + 50.0 * std::sin(1.0), 1e-2);

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 2u);
    const jitnodes::NodeId copyPlace = copy.transformNodeOf(copy.object(0).id());
    EXPECT_EQ(copy.network().node(copyPlace).expression("translateX"), "sin(time)*50") << "a saved file keeps the expression";
    copy.setTime(1.5707963267948966);
    EXPECT_NEAR(copy.object(0).geometryBounds().x0, 60.0, 1e-2);

    doc.undoStack().undo();
    EXPECT_EQ(doc.network().node(place).expression("translateX"), "") << "undo takes the expression away";
    doc.setTime(2.0);
    EXPECT_NEAR(left(id), 10.0, 1e-3) << "and the object stops following the time";
    doc.undoStack().redo();
    EXPECT_NEAR(left(id), 10.0 + 50.0 * std::sin(2.0), 1e-2) << "redo brings it back, at the current time";

    EXPECT_EQ(doc.setGraphExpression(place, "translateX", ""), "");
    doc.setTime(0.5);
    EXPECT_NEAR(left(id), 10.0, 1e-3);
}

TEST(Keys, KeyingAParameterAnimatesTheObjectAndEditsMakeKeys) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 40, 20)).id();
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    auto left = [&](std::size_t object) { return doc.findById(object)->geometryBounds().x0; };

    doc.setTime(0.0);
    doc.setGraphKey(place, "translateX", 0.0f);
    doc.setTime(2.0);
    doc.setGraphKey(place, "translateX", 100.0f);
    EXPECT_TRUE(doc.hasKeyAtTime(place, "translateX"));
    doc.setTime(1.0);
    EXPECT_FALSE(doc.hasKeyAtTime(place, "translateX"));
    EXPECT_NEAR(left(id), 60.0, 1e-3) << "linear between the keys";

    doc.setGraphParam(place, "translateX", 20.0f);
    EXPECT_EQ(doc.network().node(place).keyframes("translateX").size(), 3u) << "editing a keyed parameter keys it at the current time";
    EXPECT_NEAR(left(id), 30.0, 1e-3);

    doc.undoStack().undo();
    EXPECT_EQ(doc.network().node(place).keyframes("translateX").size(), 2u);
    EXPECT_NEAR(left(id), 60.0, 1e-3);

    SketchDocument copy = roundTrip(doc);
    copy.setTime(1.0);
    EXPECT_NEAR(copy.object(0).geometryBounds().x0, 60.0, 1e-3) << "a saved file keeps the keys";

    doc.setTime(2.0);
    doc.removeGraphKey(place, "translateX");
    EXPECT_EQ(doc.network().node(place).keyframes("translateX").size(), 1u);
    doc.setTime(0.0);
    doc.removeGraphKey(place, "translateX");
    EXPECT_TRUE(doc.network().node(place).keyframes("translateX").empty());
    EXPECT_FALSE(doc.network().node(place).isAnimated("translateX"));
    doc.setTime(5.0);
    EXPECT_NEAR(left(id), 10.0, 1e-3) << "the last key's value stays as a plain number";
}

TEST(PointTags, MovePointsMovesOnlyTheTaggedPointsAndTheObjectFollows) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 40, 20)).id();
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_TRUE(doc.tagPoints(id, "pins", {1, 2}, true));

    const jitnodes::NodeId mover = doc.addMovePoints(id, "pins");
    ASSERT_NE(mover, jitnodes::invalidNodeId);
    EXPECT_EQ(doc.transformNodeOf(id), place);
    EXPECT_NEAR(doc.network().node(mover).baseFloat("pivotX"), 50.0, 1e-3) << "pivots about the middle of the points it moves";

    doc.setGraphParam(mover, "translateX", 15.0f);
    BLBox box = doc.findById(id)->geometryBounds();
    EXPECT_NEAR(box.x0, 10.0, 1e-3) << "the untagged left edge stays";
    EXPECT_NEAR(box.x1, 65.0, 1e-3) << "the tagged right edge moved";
    const jitnodes::GeometryHandle& shape = doc.network().node(doc.findById(id)->graphNode()).geometryParam("curve");
    EXPECT_NEAR(shape->x()[1], 50.0, 1e-3) << "the shape node keeps what was drawn";

    doc.transformObjects({id}, BLMatrix2D::make_translation(100.0, 0.0), "Move");
    box = doc.findById(id)->geometryBounds();
    EXPECT_NEAR(box.x0, 110.0, 1e-3);
    EXPECT_NEAR(box.x1, 165.0, 1e-3) << "moving a driven object keeps its deformation";
    EXPECT_NEAR(doc.network().node(doc.findById(id)->graphNode()).geometryParam("curve")->x()[1], 50.0, 1e-3) << "and still leaves the shape alone";

    doc.undoStack().undo();
    box = doc.findById(id)->geometryBounds();
    EXPECT_NEAR(box.x0, 10.0, 1e-3);
    EXPECT_NEAR(box.x1, 65.0, 1e-3);

    doc.setGraphString(mover, "group", "nobody");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1, 50.0, 1e-3) << "a group nobody is in moves nothing";
    doc.setGraphString(mover, "group", "pins");

    SketchDocument copy = roundTrip(doc);
    ASSERT_EQ(copy.objectCount(), 1u);
    EXPECT_NEAR(copy.object(0).geometryBounds().x1, 65.0, 1e-2) << "a saved file keeps the deformed outline";
    doc.setGraphParam(mover, "translateX", 0.0f);
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x1, 50.0, 1e-3);
}

TEST(PointTags, ATagBetweenAShapeAndItsTransformChangesNothingElse) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(10, 10, 40, 20)).id();
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    const jitnodes::NodeId shape = doc.findById(id)->graphNode();
    ASSERT_NE(place, jitnodes::invalidNodeId);

    jitnodes::Network& net = doc.network();
    const jitnodes::NodeId tag = net.addNode(kTagNodeType, net.uniqueName("tag"));
    net.node(tag).setString("group", "pins");
    net.node(tag).setString("ids", "1");
    net.connect(shape, tag, 0);
    net.connect(tag, place, 0);
    doc.touch();

    EXPECT_EQ(doc.transformNodeOf(id), place) << "the object still finds its placement node through the tag";
    EXPECT_EQ(doc.objectOfNode(tag) == 0 || doc.objectOfNode(place) == id, true);
    doc.transformObjects({id}, BLMatrix2D::make_translation(5.0, 0.0), "Move");
    EXPECT_NEAR(doc.findById(id)->geometryBounds().x0, 15.0, 1e-3);
    EXPECT_EQ(net.inputOf(place, 0), tag) << "syncing leaves the tag where the user put it";
    EXPECT_EQ(taggedPoints(*doc.cook(place), "pins"), (std::vector<int>{0, 1, 0, 0})) << "and the tag reaches the placed shape";
}

TEST(NodeSelection, GroupedObjectsAreSelectedAndPickedByTheirScopedNode) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 40, 40)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(100, 0, 40, 40)).id();
    doc.setGroupsOf(*doc.findById(a), {doc.newGroupId()});
    doc.add(SketchObject::makeRect(300, 0, 5, 5));   // any change runs the sync that moves a into its group's subnet
    ASSERT_NE(doc.findById(a)->graphScope(), 0u);

    const NodeRef placeA = doc.transformRefOf(a);
    ASSERT_TRUE(placeA.valid());
    EXPECT_NE(placeA.scope, 0u);
    EXPECT_EQ(doc.hitTestNode(BLPoint(20, 20), 0.0), placeA);
    EXPECT_EQ(doc.objectOfNode(placeA), a);

    doc.setSelectedNodes({placeA, doc.transformRefOf(b)});
    EXPECT_EQ(doc.selection(), (std::vector<std::size_t>{a, b}));
    EXPECT_EQ(doc.selectedNodes(), (std::vector<NodeRef>{placeA, doc.transformRefOf(b)}));
}

TEST(NodeSelection, SelectionAndPickingGoByNode) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 40, 40)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(100, 0, 40, 40)).id();
    const jitnodes::NodeId placeA = doc.transformNodeOf(a), placeB = doc.transformNodeOf(b);
    ASSERT_NE(placeA, jitnodes::invalidNodeId);
    ASSERT_NE(placeB, jitnodes::invalidNodeId);

    EXPECT_EQ(doc.hitTestNode(BLPoint(20, 20), 0.0), NodeRef(placeA));
    EXPECT_EQ(doc.hitTestNode(BLPoint(120, 20), 0.0), NodeRef(placeB));
    EXPECT_EQ(doc.hitTestNode(BLPoint(70, 20), 0.0), kNoNode);

    EXPECT_EQ(doc.objectOfNode(placeB), b);
    EXPECT_EQ(doc.objectOfNode(doc.findById(b)->graphNode()), b) << "the shape node belongs to the object too";
    EXPECT_EQ(doc.objectOfNode(jitnodes::invalidNodeId), 0u);

    doc.setSelectedNodes({NodeRef(placeB), NodeRef(placeA)});
    EXPECT_EQ(doc.selection(), (std::vector<std::size_t>{b, a}));
    EXPECT_EQ(doc.selectedNodes(), (std::vector<NodeRef>{NodeRef(placeB), NodeRef(placeA)}));

    // The outline picking reads is the nodes': a translate typed into the placement node moves what can be picked.
    doc.setGraphParam(placeA, "translateX", 200.0f);
    EXPECT_EQ(doc.hitTestNode(BLPoint(220, 20), 0.0), NodeRef(placeA));
    EXPECT_EQ(doc.hitTestNode(BLPoint(20, 20), 0.0), kNoNode);
    BLBox outline(0, 0, 0, 0);
    doc.placedOutline(*doc.findById(a)).get_bounding_box(&outline);
    EXPECT_NEAR(outline.x0, doc.findById(a)->geometryBounds().x0, 1e-2) << "the nodes and the object agree";

    EXPECT_TRUE(doc.selectInRect(BLBox(190, -5, 250, 50), false));
    EXPECT_EQ(doc.selection(), (std::vector<std::size_t>{a}));

    // The selection box and frame are measured on that outline too.
    BLBox selected(0, 0, 0, 0);
    ASSERT_TRUE(doc.selectionBounds(selected));
    EXPECT_NEAR(selected.x0, outline.x0, 1e-3);
    EXPECT_NEAR(selected.x1, outline.x1, 1e-3);
    SelectionFrame frame;
    ASSERT_TRUE(doc.selectionFrame(frame));
    EXPECT_NEAR(frame.local.x0, outline.x0, 1e-3);
    EXPECT_NEAR(frame.local.x1, outline.x1, 1e-3);
}

// The handles ask for the selection's frame and bounds on every mouse move of a drag (and on every frame of the timeline), so with
// thousands of objects in the graph that must stay far inside a frame's budget.
TEST(Selection, TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing) {
    SketchDocument doc;
    constexpr int kCount = 2000;
    for (int i = 0; i < kCount; ++i) {
        doc.add(SketchObject::makeRect(double(i % 50) * 30.0, double(i / 50) * 30.0, 20, 20));
    }
    doc.selectAll();
    ASSERT_EQ(doc.selection().size(), std::size_t(kCount));
    constexpr double kBudgetMs = 3.0;
    auto millisSince = [](std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    BLBox box(0, 0, 0, 0);
    SelectionFrame frame;

    // Resting: nothing edited, so the nodes' outline is the one measured.
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(doc.selectionBounds(box));
        ASSERT_TRUE(doc.selectionFrame(frame));
    }
    const double resting = millisSince(start) / 20;

    // Dragging: each mouse move puts the objects back and moves them again (CanvasView::applyToSelection), then the handles are measured.
    const std::vector<GeometryState> before = doc.snapshotSelection(true);
    double dragging = 0.0, placing = 0.0;
    for (int step = 1; step <= 20; ++step) {
        start = std::chrono::steady_clock::now();
        doc.placeObjects(before, BLMatrix2D::make_translation(step * 3.0, step * 2.0));
        placing += millisSince(start);
        start = std::chrono::steady_clock::now();
        ASSERT_TRUE(doc.selectionBounds(box));
        ASSERT_TRUE(doc.selectionFrame(frame));
        dragging += millisSince(start);
    }
    dragging /= 20;
    placing /= 20;
    EXPECT_NEAR(box.x0, 60.0, 1e-3) << "the handles follow the drag";
    start = std::chrono::steady_clock::now();
    doc.commitPlacement(before, "Move");
    const double committing = millisSince(start);
    start = std::chrono::steady_clock::now();
    doc.undoStack().undo();
    const double undoing = millisSince(start);
    EXPECT_NEAR(doc.findById(before.front().id)->geometryBounds().x0, 0.0, 1e-3);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(before.front().id)->geometryBounds().x0, 60.0, 1e-3);
    std::printf("drag step: placing %.2f ms; commit %.1f ms, undo %.1f ms\n", placing, committing, undoing);

    // Scrubbing the timeline: every frame re-cooks the graph, then the handles are measured.
    double cook = 0.0, measure = 0.0;
    for (int frameNo = 1; frameNo <= 20; ++frameNo) {
        start = std::chrono::steady_clock::now();
        doc.setTime(frameNo / 24.0);
        cook += millisSince(start);
        start = std::chrono::steady_clock::now();
        ASSERT_TRUE(doc.selectionBounds(box));
        ASSERT_TRUE(doc.selectionFrame(frame));
        measure += millisSince(start);
    }
    const double scrubbing = (cook + measure) / 20;

    std::printf("frame + bounds of %d objects: resting %.2f ms, dragging %.2f ms; timeline step %.2f ms (setTime %.2f, measuring %.2f)\n",
        kCount, resting, dragging, scrubbing, cook / 20, measure / 20);
    EXPECT_LT(resting, kBudgetMs);
    EXPECT_LT(dragging, kBudgetMs);
    EXPECT_LT(placing, 2 * kBudgetMs) << "writing 2000 transform nodes per mouse move";
    EXPECT_LT(scrubbing, 4 * kBudgetMs) << "includes re-cooking the graph";
}

// DG-03 S2: a placement edit writes the transform node and the object follows; nothing is left for the next sync to write.
TEST(Placement, AMoveWritesTheTransformNodeAndLeavesTheObjectInStep) {
    SketchDocument doc;
    SketchObject& parent = doc.add(SketchObject::makeRect(10, 10, 40, 20));
    SketchObject& child = doc.add(SketchObject::makeRect(100, 100, 10, 10));
    const std::size_t parentId = parent.id(), childId = child.id();
    ASSERT_TRUE(doc.setParent(childId, parentId));
    doc.setSelection({parentId});
    const std::vector<GeometryState> before = doc.snapshotSelection(true);
    ASSERT_EQ(before.size(), std::size_t(2)) << "the child comes along";
    const BLMatrix2D localBefore = transformOf(doc.network().node(doc.transformNodeOf(childId)));

    doc.placeObjects(before, BLMatrix2D::make_translation(5, 7), 1.0, 0.25);

    for (const std::size_t id : {parentId, childId}) {
        const SketchObject* object = doc.findById(id);
        EXPECT_TRUE(object->boundToNodes()) << "the node already holds the placement";
        const jitnodes::NodeId place = doc.transformNodeOf(id);
        ASSERT_NE(place, jitnodes::invalidNodeId);
        const BLMatrix2D world = worldTransformOf(doc.network(), place);
        for (int i = 0; i < 6; ++i) {
            EXPECT_NEAR(world.m[i], object->paintToPage().m[i], 1e-4) << "object " << id << " m" << i;
        }
        const BLBox placed = [&] { BLBox b; doc.placedOutline(*object).get_bounding_box(&b); return b; }();
        EXPECT_NEAR(placed.x0, object->geometryBounds().x0, 1e-3);
        EXPECT_NEAR(placed.y0, object->geometryBounds().y0, 1e-3);
    }
    EXPECT_NEAR(doc.findById(parentId)->geometryBounds().x0, 15.0, 1e-9);
    EXPECT_NEAR(doc.findById(childId)->geometryBounds().x0, 105.0, 1e-9);
    EXPECT_NEAR(doc.findById(parentId)->frameAngle(), 0.25, 1e-12);
    const BLMatrix2D childLocal = transformOf(doc.network().node(doc.transformNodeOf(childId)));
    EXPECT_NEAR(childLocal.m20, localBefore.m20, 1e-3) << "a child follows its parent, so its own placement is unchanged";
    EXPECT_NEAR(childLocal.m21, localBefore.m21, 1e-3);

    // Undo and redo write the nodes, and the objects follow.
    doc.commitPlacement(before, "Move");
    const jitnodes::NodeId parentPlace = doc.transformNodeOf(parentId);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(parentId)->geometryBounds().x0, 10.0, 1e-9);
    EXPECT_NEAR(doc.findById(parentId)->frameAngle(), 0.0, 1e-12);
    EXPECT_NEAR(worldTransformOf(doc.network(), parentPlace).m20, 0.0, 1e-4) << "the node is back where it was";
    EXPECT_NEAR(worldTransformOf(doc.network(), doc.transformNodeOf(childId)).m20, 0.0, 1e-4);
    doc.undoStack().redo();
    EXPECT_NEAR(doc.findById(childId)->geometryBounds().x0, 105.0, 1e-9);
    EXPECT_NEAR(worldTransformOf(doc.network(), parentPlace).m20, 5.0, 1e-4);
    EXPECT_TRUE(doc.findById(parentId)->boundToNodes());
}

// DG-03 S3: a curve edit rewrites the shape node's curve at once, keeping what the node holds for the points that stayed; undo and redo too.
TEST(Placement, ACurveEditWritesTheShapeNodeAndKeepsTheTags) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(20, 20, 100, 60)).id();
    doc.setSelection({id});
    ASSERT_TRUE(doc.tagPoints(id, "a", {1}, true));
    const std::vector<int> tagged = doc.taggedIds(id, "a");
    ASSERT_EQ(tagged.size(), std::size_t(1));
    auto width = [&] {
        BLBox box;
        doc.placedOutline(*doc.findById(id)).get_bounding_box(&box);
        return box.x1 - box.x0;
    };
    const std::vector<GeometryState> before = doc.snapshotSelection();
    SketchObject* object = doc.findById(id);

    const std::unique_ptr<SketchObject> wider(SketchObject::makeRect(20, 20, 160, 60));
    doc.setObjectOutline(*object, wider->geometry());
    EXPECT_TRUE(object->boundToNodes()) << "the node was written by the edit, not by the next sync";
    EXPECT_NEAR(width(), 160.0, 1e-3);
    const jitnodes::GeometryHandle& curve = doc.network().node(object->graphNode()).geometryParam("curve");
    ASSERT_TRUE(curve);
    BLPath drawn;
    ASSERT_TRUE(pathFromGeometry(*curve, drawn));
    BLBox drawnBox;
    drawn.get_bounding_box(&drawnBox);
    EXPECT_NEAR(drawnBox.x1 - drawnBox.x0, 160.0, 1e-3);
    EXPECT_EQ(doc.taggedIds(id, "a"), tagged) << "the point kept its id and its tag";

    doc.commitGeometry(before, "Edit Curve");
    doc.undoStack().undo();
    EXPECT_NEAR(width(), 100.0, 1e-3);
    EXPECT_TRUE(object->boundToNodes());
    doc.undoStack().redo();
    EXPECT_NEAR(width(), 160.0, 1e-3);
    EXPECT_EQ(doc.taggedIds(id, "a"), tagged);
}

// The pivot is where a transform node turns and scales about; changing it must not move the shape (reported from hand testing).
TEST(Placement, ChangingThePivotInTheTransformNodeLeavesTheShapeWhereItIs) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(20, 20, 100, 60)).id();
    doc.setSelection({id});
    // Scaled and turned, so the pivot takes part in the matrix.
    doc.transformObjects({id}, SketchDocument::scalingAbout(BLPoint(70, 50), 1.5, 0.5), "Resize");
    doc.transformObjects({id}, SketchDocument::rotationAbout(BLPoint(70, 50), 0.4), "Rotate");
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    const BLMatrix2D held = worldTransformOf(doc.network(), place);
    BLBox bounds;
    doc.selectionBounds(bounds);

    for (const char* name : {"pivotX", "pivotY"}) {
        for (const float value : {12.0f, -40.0f, 75.0f}) {
            const jitnodes::Node& placeNode = doc.network().node(place);
            const char* other = std::string(name) == "pivotX" ? "pivotY" : "pivotX";
            const double otherShown = shownParam(placeNode, other);
            doc.setGraphParam(place, name, value);
            EXPECT_NEAR(shownParam(placeNode, name), value, 1e-3);
            EXPECT_NEAR(shownParam(placeNode, other), otherShown, 1e-3) << "the other axis of the parent space stays put";
            const BLMatrix2D now = worldTransformOf(doc.network(), place);
            for (int i = 0; i < 6; ++i) {
                EXPECT_NEAR(now.m[i], held.m[i], 1e-3) << name << " = " << value << " moved m" << i;
            }
            BLBox after;
            doc.selectionBounds(after);
            EXPECT_NEAR(after.x0, bounds.x0, 1e-2) << name << " = " << value;
            EXPECT_NEAR(after.y0, bounds.y0, 1e-2);
            EXPECT_NEAR(after.x1, bounds.x1, 1e-2);
            EXPECT_NEAR(after.y1, bounds.y1, 1e-2);
        }
    }
    doc.undoStack().undo();
    EXPECT_NEAR(shownParam(doc.network().node(place), "pivotY"), -40.0, 1e-3) << "undo steps back one pivot edit";
    const BLMatrix2D undone = worldTransformOf(doc.network(), place);
    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(undone.m[i], held.m[i], 1e-3);
    }
}

TEST(Placement, ThePivotIsEditedInTheParentSpaceAndStoredInTheShapesOwn) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(20, 20, 100, 60)).id();
    doc.setSelection({id});
    doc.transformObjects({id}, SketchDocument::rotationAbout(BLPoint(70, 50), 0.8), "Rotate");
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    const jitnodes::Node& node = doc.network().node(place);
    const BLMatrix2D held = worldTransformOf(doc.network(), place);
    const BLPoint before(shownParam(node, "pivotX"), shownParam(node, "pivotY"));
    const BLPoint landed = held.map_point(node.baseFloat("pivotX"), node.baseFloat("pivotY"));
    EXPECT_NEAR(before.x, landed.x, 1e-3) << "the shown pivot is where the pivot lands";
    EXPECT_NEAR(before.y, landed.y, 1e-3);

    doc.setGraphParam(place, "pivotX", float(before.x + 30.0));
    const BLPoint after = held.map_point(node.baseFloat("pivotX"), node.baseFloat("pivotY"));
    EXPECT_NEAR(after.x, before.x + 30.0, 1e-2) << "pivotX moves along the parent's x axis";
    EXPECT_NEAR(after.y, before.y, 1e-2) << "and not along the turned axis";
    EXPECT_GT(std::fabs(node.baseFloat("pivotY") - 50.0f) + std::fabs(node.baseFloat("pivotX") - 70.0f), 1.0) << "the stored pivot is in the shape's space";
}

TEST(Grouping, GroupingTurnedAndParentedObjectsMakesOneGroupAndUndoes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(20, 20, 30, 120)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(60, 20, 30, 120)).id();
    const std::size_t c = doc.add(SketchObject::makeRect(100, 20, 30, 120)).id();
    doc.transformObjects({b}, SketchDocument::rotationAbout(BLPoint(75, 80), 0.9), "Rotate");
    ASSERT_TRUE(doc.setParent(c, a));
    doc.setSelection({a, b, c});
    ASSERT_TRUE(doc.groupSelection());
    EXPECT_EQ(doc.findById(a)->groups().size(), 1u);
    EXPECT_EQ(doc.findById(a)->groups(), doc.findById(b)->groups());
    EXPECT_EQ(doc.findById(a)->groups(), doc.findById(c)->groups());
    EXPECT_NE(doc.selectedGroup(), 0u) << "the whole selection is one group";
    const BLBox before = doc.findById(b)->geometryBounds();
    EXPECT_NEAR(doc.findById(b)->geometryBounds().x0, before.x0, 1e-9);
    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(a)->groups().empty());
    doc.undoStack().redo();
    EXPECT_EQ(doc.findById(a)->groups().size(), 1u);
}

TEST(Grouping, EachObjectsOwnFrameStaysOnTheObjectAfterItWasMoved) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(20, 20, 30, 120)).id();
    const std::size_t b = doc.add(SketchObject::makeRect(60, 20, 30, 120)).id();
    doc.transformObjects({a}, BLMatrix2D::make_translation(300, 200), "Move");
    doc.transformObjects({b}, SketchDocument::rotationAbout(BLPoint(75, 80), 0.9), "Rotate");
    for (std::size_t id : {a, b}) {
        const SketchObject& object = *doc.findById(id);
        const QuadFrame quad = doc.objectFrame(object).asQuad();
        const BLBox box = object.geometryBounds();
        for (const BLPoint& corner : quad.p) {
            EXPECT_GE(corner.x, box.x0 - 1e-6) << id;
            EXPECT_LE(corner.x, box.x1 + 1e-6) << id;
            EXPECT_GE(corner.y, box.y0 - 1e-6) << id;
            EXPECT_LE(corner.y, box.y1 + 1e-6) << id;
        }
    }
}

TEST(Placement, ResettingTheTransformNodeOfATurnedTextBringsTheTextBack) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeText("hello there", "", 40.0, BLPoint(100.0, 200.0))).id();
    const BLBox start = doc.findById(id)->bounds();
    doc.setSelection({id});
    doc.transformObjects({id}, SketchDocument::rotationAbout(BLPoint(150, 190), 0.6), "Rotate");
    ASSERT_GT(std::fabs(doc.findById(id)->bounds().y1 - start.y1), 5.0);
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    for (const char* name : {"translateX", "translateY", "rotation", "shear", "pivotX", "pivotY"}) {
        doc.setGraphParam(place, name, 0.0f);
    }
    doc.setGraphParam(place, "scaleX", 1.0f);
    doc.setGraphParam(place, "scaleY", 1.0f);
    const BLBox reset = doc.findById(id)->bounds();
    EXPECT_NEAR(reset.x0, start.x0, 0.5);
    EXPECT_NEAR(reset.y0, start.y0, 0.5);
    EXPECT_NEAR(reset.x1, start.x1, 0.5);
    EXPECT_NEAR(reset.y1, start.y1, 0.5);
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 0.0, 1e-6) << "the selection frame turns back with the node";
}

TEST(Placement, ResettingTheTransformNodeOfATurnedShapeTurnsItsFrameBack) {
    SketchDocument doc;
    const std::size_t id = doc.add(SketchObject::makeRect(20, 20, 100, 60)).id();
    doc.setSelection({id});
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    doc.setGraphParam(place, "rotation", 40.0f);
    ASSERT_GT(std::fabs(doc.findById(id)->frameAngle()), 0.5) << "typed into the node";
    for (const char* name : {"translateX", "translateY", "rotation", "shear", "pivotX", "pivotY"}) {
        doc.setGraphParam(place, name, 0.0f);
    }
    doc.setGraphParam(place, "scaleX", 1.0f);
    doc.setGraphParam(place, "scaleY", 1.0f);
    EXPECT_NEAR(doc.findById(id)->frameAngle(), 0.0, 1e-6);
}

// DG-03 S1: the perspective map of an object's paints lives on its transform node too, and follows graph edits and their undo.
TEST(PaintWarp, TheTransformNodeHoldsTheWarpAndFollowsGraphEdits) {
    SketchDocument doc;
    SketchObject& rect = doc.add(SketchObject::makeRect(20, 20, 100, 60));
    const std::size_t id = rect.id();
    const jitnodes::NodeId place = doc.transformNodeOf(id);
    ASSERT_NE(place, jitnodes::invalidNodeId);
    EXPECT_EQ(doc.network().node(place).getString("paintWarp"), "") << "never warped";

    const BLPoint from[4] = {{20, 20}, {120, 20}, {120, 80}, {20, 80}};
    const BLPoint to[4] = {{20, 20}, {120, 35}, {120, 65}, {20, 80}};
    const PaintWarp warp = PaintWarp::between(from, to);
    doc.setSelection({id});
    doc.warpObjects(doc.snapshotSelection(), [](const BLPoint& p) { return p; }, warp);
    EXPECT_NE(doc.network().node(place).getString("paintWarp"), "");
    EXPECT_FALSE(paintWarpDiffers(doc.network().node(place), doc.findById(id)->paintWarp()));

    doc.setGraphString(place, "paintWarp", "");   // typed into the node
    EXPECT_TRUE(doc.findById(id)->paintWarp().isIdentity()) << "the object follows the node";
    doc.undoStack().undo();
    EXPECT_FALSE(doc.findById(id)->paintWarp().isIdentity()) << "and its undo";
    EXPECT_NEAR(doc.findById(id)->paintWarp().m[1], warp.m[1], 1e-5);
}

TEST(PaintWarp, AGradientFollowsAPerspectiveWarpOfItsShape) {
    SketchDocument doc;
    SketchGradient gradient;
    gradient.stops.push_back({0.0, SketchColor::rgb(1, 0, 0)});
    gradient.stops.push_back({1.0, SketchColor::rgb(0, 0, 1)});
    SketchObject& rect = doc.add(SketchObject::makeRect(20, 20, 100, 60));
    rect.setStroke(SketchPaint::none());
    rect.setFill(SketchPaint::linear(gradient));
    const std::size_t id = rect.id();

    const BLPoint from[4] = {{20, 20}, {120, 20}, {120, 80}, {20, 80}};
    const BLPoint to[4] = {{20, 20}, {120, 35}, {120, 65}, {20, 80}};
    const PaintWarp page = PaintWarp::between(from, to);
    EXPECT_NEAR(page.map(from[1]).y, 35.0, 1e-6);
    EXPECT_NEAR(page.map(from[3]).x, 20.0, 1e-6);

    doc.setSelection({id});
    const std::vector<GeometryState> start = doc.snapshotSelection();
    doc.warpObjects(start, [&](const BLPoint& p) { return page.map(p); }, page);
    ASSERT_FALSE(doc.findById(id)->paintWarp().isIdentity());
    const BLBox frame = doc.findById(id)->paintFrameBounds();
    EXPECT_NEAR(frame.x1 - frame.x0, 100.0, 1e-3) << "the paint frame is the shape before the warp";
    EXPECT_NEAR(frame.y1 - frame.y0, 60.0, 1e-3);

    PaintWarp unwarp;
    ASSERT_TRUE(page.inverted(unwarp));
    double biggest = 0.0;
    for (const BLPoint& at : {BLPoint(30, 50), BLPoint(70, 50), BLPoint(100, 45), BLPoint(110, 55), BLPoint(60, 30)}) {
        const double t = (unwarp.map(at).x - 20.0) / 100.0;
        const std::uint32_t pixel = pixelAt(*doc.findById(id), 160, int(at.x), int(at.y));
        EXPECT_NEAR(double(pixel & 0xFFu), 255.0 * t, 10.0) << "blue rises along the pre-warp x at " << at.x << "," << at.y;
        biggest = std::max(biggest, std::fabs(t - (at.x - 20.0) / 100.0));
    }
    EXPECT_GT(biggest, 0.05) << "the warp really moves the colours, which a flat gradient would not";

    EXPECT_NE(svgFromDocument(doc).find("data-paint-warp"), std::string::npos) << "the SVG carries the warp";    SketchDocument viaSvg = roundTrip(doc);
    ASSERT_EQ(viaSvg.objectCount(), 1u);
    for (int i = 0; i < 9; ++i) {
        EXPECT_NEAR(viaSvg.object(0).paintWarp().m[i], page.m[i], 1e-4) << "SVG keeps the warp, entry " << i;
    }
    SketchDocument viaBinary;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(doc), viaBinary));
    ASSERT_EQ(viaBinary.objectCount(), 1u);
    EXPECT_TRUE(viaBinary.object(0).paintWarp() == page) << "so does the binary format";

    doc.placeObjects(doc.snapshotSelection(), BLMatrix2D::make_translation(10.0, 0.0));
    EXPECT_NEAR(double(pixelAt(*doc.findById(id), 160, 80, 50) & 0xFFu), 255.0 * ((unwarp.map(BLPoint(70, 50)).x - 20.0) / 100.0), 10.0)
        << "a move after the warp carries the colours with the shape";

    doc.commitGeometry(start, "Warp", doc.frameState());
    doc.undoStack().undo();
    EXPECT_TRUE(doc.findById(id)->paintWarp().isIdentity()) << "undoing the warp unbends the paint too";
    doc.undoStack().redo();
    EXPECT_TRUE(doc.findById(id)->paintWarp() == page);
}

TEST(PathNode, AThousandsObjectDocumentStaysResponsive) {
    SketchDocument doc;
    {
        SketchDocument::Batch batch(doc);
        for (int i = 0; i < 5000; ++i) {
            doc.add(SketchObject::makeRect(i % 100 * 12, i / 100 * 12, 10, 10));
        }
    }
    EXPECT_TRUE(pathNodesMatch(doc));
    const auto start = std::chrono::steady_clock::now();
    const std::size_t id = doc.object(0).id();
    doc.setSelection({id});
    for (int i = 0; i < 200; ++i) {
        const auto before = doc.snapshotSelection();
        doc.placeObjects(before, BLMatrix2D::make_translation(1.0, 0.0));
        doc.commitPlacement(before, "Move");
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("200 commits on 5000 objects: %.1f ms\n", ms);
    EXPECT_LT(ms, 2000.0);
    EXPECT_TRUE(pathNodesMatch(doc));
}

namespace {
// The fill, stroke and effect nodes that follow an object's shape node, bottom to top.
std::vector<jitnodes::NodeId> lookNodes(const SketchDocument& doc, std::size_t objectId) {
    const jitnodes::Network& net = doc.network();
    std::vector<jitnodes::NodeId> chain;
    jitnodes::NodeId at = doc.findById(objectId)->graphNode();
    for (jitnodes::NodeId id : net.nodeIds()) {
        if (net.node(id).typeName() == kTransformNodeType && net.inputOf(id, 0) == at) {
            at = id;   // the transform node sits between the shape and its looks
            break;
        }
    }
    for (bool found = true; found;) {
        found = false;
        for (jitnodes::NodeId id : net.nodeIds()) {
            if (isAppearanceNodeType(net.node(id).typeName()) && net.inputOf(id, 0) == at) {
                chain.push_back(id);
                at = id;
                found = true;
                break;
            }
        }
    }
    return chain;
}

// True when every object has one node per appearance item, each holding exactly that item's look.
bool looksMatch(const SketchDocument& doc) {
    std::size_t nodes = 0;
    for (jitnodes::NodeId id : doc.network().nodeIds()) {
        nodes += isAppearanceNodeType(doc.network().node(id).typeName()) ? 1 : 0;
    }
    std::size_t items = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& object = doc.object(i);
        const std::vector<jitnodes::NodeId> chain = lookNodes(doc, object.id());
        if (chain.size() != object.appearance().size()) {
            return false;
        }
        for (std::size_t k = 0; k < chain.size(); ++k) {
            if (paramsOf(doc.network().node(chain[k])) != paramsOf(object.appearance()[k])) {
                return false;
            }
        }
        items += chain.size();
    }
    return nodes == items;
}
}

TEST(AppearanceNode, EveryObjectHasAFillAndStrokeNodeChainedAfterItsShape) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.add(SketchObject::makeEllipse(40, 40, 8, 8));
    EXPECT_TRUE(looksMatch(doc));
    const std::vector<jitnodes::NodeId> chain = lookNodes(doc, a);
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(doc.network().node(chain[0]).typeName(), kFillNodeType);
    EXPECT_EQ(doc.network().node(chain[1]).typeName(), kStrokeNodeType);
    EXPECT_EQ(doc.network().node(doc.network().inputOf(chain[0], 0)).typeName(), kTransformNodeType);
    EXPECT_EQ(doc.network().inputOf(doc.network().inputOf(chain[0], 0), 0), doc.findById(a)->graphNode());
    EXPECT_EQ(doc.network().inputOf(chain[1], 0), chain[0]);
    EXPECT_EQ(doc.cook(chain[1])->primitiveCount(), 1u) << "the shape passes through the look nodes";
}

TEST(AppearanceNode, ChangingTheStrokeWidthWritesTheNodeAndUndoes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    const double original = doc.findById(a)->strokeWidth();
    doc.setSelection({a});
    const auto before = doc.snapshotStyle();
    doc.setSelectionStrokeWidth(7.5);
    ASSERT_TRUE(doc.commitStyle(before, "Stroke Width"));
    const jitnodes::NodeId stroke = lookNodes(doc, a)[1];
    EXPECT_EQ(doc.network().node(stroke).getFloat("width"), 7.5f);
    EXPECT_TRUE(looksMatch(doc));
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->strokeWidth(), original);
    EXPECT_EQ(doc.network().node(stroke).getFloat("width"), float(original));
    doc.undoStack().redo();
    EXPECT_EQ(doc.network().node(stroke).getFloat("width"), 7.5f);
    EXPECT_TRUE(looksMatch(doc));
}

TEST(AppearanceNode, EditingTheNodeChangesTheLookAndUndoes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.findById(a)->setStrokeWidth(0.1);
    doc.touch();
    const std::vector<jitnodes::NodeId> chain = lookNodes(doc, a);
    doc.setGraphParam(chain[1], "width", 9.0f);
    EXPECT_DOUBLE_EQ(doc.findById(a)->strokeWidth(), 9.0);
    doc.setGraphParam(chain[0], "opacity", 0.25f);
    EXPECT_DOUBLE_EQ(doc.findById(a)->appearance()[0].opacity, 0.25);
    EXPECT_TRUE(looksMatch(doc));
    doc.undoStack().undo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->appearance()[0].opacity, 1.0);
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(a)->strokeWidth(), 0.1, 1e-6);
    EXPECT_EQ(doc.findById(a)->strokeWidth(), double(0.1f)) << "a number that went through the graph is the node's float";
    EXPECT_TRUE(looksMatch(doc));
    doc.undoStack().redo();
    doc.undoStack().redo();
    EXPECT_DOUBLE_EQ(doc.findById(a)->strokeWidth(), 9.0);
}

TEST(AppearanceNode, EverythingALookHoldsSurvivesTheNode) {
    SketchGradient gradient = SketchGradient::fromColor(SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f));
    gradient.hasAxis = true;
    gradient.axisAspect = 2.0;
    gradient.spread = GradientSpread::Reflect;
    gradient.stops.push_back({0.5, SketchColor::hsb(0.3f, 0.5f, 0.7f, 0.5f)});
    gradient.sortStops();
    StrokeOptions options;
    options.cap = StrokeCap::Round;
    options.join = StrokeJoin::Bevel;
    options.dashes = {4.0, 2.0, 1.0};
    options.widths = {{0.0, 1.0, 1.0}, {0.5, 2.0, 0.5}};
    options.align = StrokeAlign::Inside;
    options.arrowEnd = ArrowStyle::Filled;
    SketchDocument doc;
    SketchObject& object = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    std::vector<AppearanceItem> items = {AppearanceItem::makeFill(SketchPaint::linear(gradient)), AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::gray(0.25f, 0.5f)), 3.5, options),
        AppearanceItem::makeEffect(EffectKind::OuterGlow)};
    items[0].opacity = 0.4;
    items[0].blendMode = BlendMode::Screen;
    items[1].visible = false;
    object.setAppearance(items);
    doc.touch();
    EXPECT_TRUE(looksMatch(doc));
    const std::vector<jitnodes::NodeId> chain = lookNodes(doc, object.id());
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(doc.network().node(chain[2]).typeName(), kEffectNodeType);

    // Reading a node back gives the item it was written from.
    const AppearanceLibrary none;
    for (std::size_t i = 0; i < items.size(); ++i) {
        AppearanceItem other = items[i];
        other.opacity = 0.9;
        other.strokeWidth = 1.0;
        const AppearanceItem rebuilt = itemFrom(paramsOf(doc.network().node(chain[i])), paramsOf(other), other, none);
        EXPECT_EQ(paramsOf(rebuilt), paramsOf(items[i])) << "item " << i;
    }
    doc.undoStack().clear();
    doc.setGraphParam(chain[0], "angle", 33.0f);
    EXPECT_DOUBLE_EQ(doc.findById(object.id())->appearance()[0].paint.gradient().angleDegrees, 33.0);
    EXPECT_EQ(doc.findById(object.id())->appearance()[0].paint.gradient().stops.size(), 3u);
    EXPECT_EQ(doc.findById(object.id())->appearance()[1].strokeOptions.dashes.size(), 3u);
    EXPECT_TRUE(looksMatch(doc));
}

TEST(AppearanceNode, PointGradientsAreATableOnTheNodeAndEditBothWays) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 100, 50)).id();
    doc.setSelection({a});
    const auto before = doc.snapshotStyle();
    SketchGradient gradient = redAndBluePoints();
    doc.setSelectionFill(SketchPaint::points(gradient));
    doc.commitStyle(before, "Points");
    EXPECT_TRUE(looksMatch(doc));
    const std::vector<jitnodes::NodeId> chain = lookNodes(doc, a);
    ASSERT_FALSE(chain.empty());
    jitnodes::Node& fill = doc.network().node(chain[0]);
    EXPECT_EQ(int(fill.baseFloat("paint")), 5);
    ASSERT_TRUE(fill.hasGeometryParam("points"));
    EXPECT_EQ(fill.geometryParam("points")->pointCount(), 2u);

    // Moving a point on the shape writes the node; the node's table reads back as the same points.
    gradient.points[0].x = 0.25;
    const auto move = doc.snapshotStyle();
    doc.setSelectionFill(SketchPaint::points(gradient));
    doc.commitStyle(move, "Move point");
    EXPECT_TRUE(looksMatch(doc));
    EXPECT_DOUBLE_EQ(doc.findById(a)->fill().gradient().points[0].x, 0.25);

    // Editing the node's table moves the point on the shape.
    auto table = std::make_shared<jitnodes::Geometry>(*doc.network().node(chain[0]).geometryParam("points"));
    table->pointAttrib("x")[1] = 0.75f;
    doc.setGraphGeometry(chain[0], "points", table);
    EXPECT_NEAR(doc.findById(a)->fill().gradient().points[1].x, 0.75, 1e-6);
    EXPECT_EQ(doc.findById(a)->fill().gradient().points.size(), 2u);
}

TEST(AppearanceNode, AddingAndRemovingItemsAddsAndRemovesNodes) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    ASSERT_TRUE(doc.addEffect(EffectKind::DropShadow));
    EXPECT_EQ(lookNodes(doc, a).size(), 3u);
    EXPECT_TRUE(looksMatch(doc));
    ASSERT_TRUE(doc.addAppearanceItem(AppearanceKind::Stroke));
    EXPECT_EQ(lookNodes(doc, a).size(), 4u);
    EXPECT_TRUE(looksMatch(doc));
    doc.undoStack().undo();
    doc.undoStack().undo();
    EXPECT_EQ(lookNodes(doc, a).size(), 2u);
    EXPECT_TRUE(looksMatch(doc));
    doc.undoStack().redo();
    EXPECT_EQ(lookNodes(doc, a).size(), 3u);
    EXPECT_TRUE(looksMatch(doc));
    doc.deleteSelection();
    EXPECT_TRUE(doc.network().nodeIds().empty()) << "an object's look nodes go with it";
    doc.undoStack().undo();
    EXPECT_TRUE(looksMatch(doc));
    EXPECT_EQ(lookNodes(doc, a).size(), 3u);
}

TEST(AppearanceNode, SurviveSvgSaveAndLoad) {
    SketchDocument doc;
    const std::size_t a = doc.add(SketchObject::makeRect(0, 0, 10, 10)).id();
    doc.setSelection({a});
    const auto before = doc.snapshotStyle();
    doc.setSelectionStrokeWidth(4.0);
    doc.setSelectionFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    doc.commitStyle(before, "Look");
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    EXPECT_TRUE(looksMatch(loaded));
    EXPECT_EQ(loaded.network().nodeIds().size(), doc.network().nodeIds().size());
    const std::size_t b = loaded.object(0).id();
    EXPECT_EQ(loaded.network().node(lookNodes(loaded, b)[1]).getFloat("width"), 4.0f);
    loaded.setGraphParam(lookNodes(loaded, b)[1], "width", 6.0f);
    EXPECT_DOUBLE_EQ(loaded.findById(b)->strokeWidth(), 6.0);
}

namespace {
GraphRow treeRow(std::size_t depth, const std::string& text, const std::string& summary = std::string()) {
    GraphRow row;
    row.depth = depth;
    row.text = text;
    row.summary = summary;
    return row;
}
}

TEST(GraphTree, PathsResolveThroughTheFlatRows) {
    GraphTreeModel model;
    model.setRows({treeRow(0, "a"), treeRow(1, "a1"), treeRow(2, "a1x"), treeRow(1, "a2"), treeRow(0, "b"), treeRow(1, "b1")});
    EXPECT_EQ(model.childCount({}), 2u);
    EXPECT_EQ(model.childCount({0}), 2u);
    EXPECT_EQ(model.childCount({0, 0}), 1u);
    EXPECT_EQ(model.childCount({1}), 1u);
    EXPECT_EQ(model.childCount({5}), 0u);
    ASSERT_NE(model.rowAt({0, 1}), nullptr);
    EXPECT_EQ(model.rowAt({0, 1})->text, "a2");
    EXPECT_EQ(model.rowAt({1, 0})->text, "b1");
    EXPECT_EQ(model.rowAt({2}), nullptr);
    for (std::size_t i = 0; i < model.rows().size(); ++i) {
        EXPECT_EQ(model.rowAt(model.pathOf(i))->text, model.rows()[i].text);
    }
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0, 0, 0})), "a1x");
    // a row cannot sit deeper than one below the row above it
    model.setRows({treeRow(0, "a"), treeRow(3, "deep")});
    EXPECT_EQ(model.rows()[1].depth, 1u);
    EXPECT_EQ(model.childCount({0}), 1u);
}

TEST(GraphTree, FlowShowsASharedInputOnceAndLinksAfter) {
    // 3 feeds 1 and 2; 1 and 2 feed 4
    FlowSource source;
    source.nodes = {1, 2, 3, 4};
    source.inputs = {{3}, {3}, {}, {1, 2}};
    const std::vector<GraphRow> rows = flowRows(source, [](GraphRow& row) { row.text = "n" + std::to_string(row.node); });
    ASSERT_EQ(rows.size(), 5u) << "4 is the only node nobody uses; 3 appears once in full and once as a link";
    EXPECT_EQ(rows[0].text, "n4");
    EXPECT_EQ(rows[1].text, "n1");
    EXPECT_EQ(rows[1].depth, 1u);
    EXPECT_EQ(rows[2].text, "n3");
    EXPECT_EQ(rows[2].depth, 2u);
    EXPECT_EQ(rows[2].kind, GraphRow::Kind::Node);
    EXPECT_EQ(rows[3].text, "n2");
    EXPECT_EQ(rows[4].text, "-> n3");
    EXPECT_EQ(rows[4].kind, GraphRow::Kind::Link);
}

TEST(GraphTree, FilterKeepsMatchesAndTheRowsAboveThem) {
    const std::vector<GraphRow> rows = {treeRow(0, "Sketch"), treeRow(1, "Face"), treeRow(2, "Head", "#F2C9A0"), treeRow(2, "Eye"), treeRow(1, "Title"), treeRow(0, "Background")};
    EXPECT_EQ(filterRows(rows, "").size(), rows.size());
    const std::vector<GraphRow> kept = filterRows(rows, "head");
    ASSERT_EQ(kept.size(), 3u);
    EXPECT_EQ(kept[0].text, "Sketch");
    EXPECT_TRUE(kept[0].dim);
    EXPECT_TRUE(kept[1].dim);
    EXPECT_EQ(kept[2].text, "Head");
    EXPECT_FALSE(kept[2].dim);
    EXPECT_EQ(filterRows(rows, "f2c9").size(), 3u) << "the summary text matches too";
    EXPECT_TRUE(filterRows(rows, "zzz").empty());
}

TEST(RadialLayout, SlotsRunClockwiseFromTheTop) {
    const newui::Point top = RadialLayout::slotCenter(0, 8);
    EXPECT_NEAR(top.x, 0.0, 0.01);
    EXPECT_LT(top.y, 0.0f);
    const newui::Point right = RadialLayout::slotCenter(2, 8);
    EXPECT_GT(right.x, 0.0f);
    EXPECT_NEAR(right.y, 0.0, 0.01);
}

TEST(RadialLayout, PointerFindsSlotHubOrOutside) {
    EXPECT_EQ(RadialLayout::slotAt(0, 0, 8), RadialLayout::hub);
    EXPECT_EQ(RadialLayout::slotAt(0, -100, 8), 0);
    EXPECT_EQ(RadialLayout::slotAt(100, 0, 8), 2);
    EXPECT_EQ(RadialLayout::slotAt(0, 100, 8), 4);
    EXPECT_EQ(RadialLayout::slotAt(-100, 0, 8), 6);
    EXPECT_EQ(RadialLayout::slotAt(1, -100, 8), 0);
    EXPECT_EQ(RadialLayout::slotAt(-1, -100, 8), 0);
    EXPECT_EQ(RadialLayout::slotAt(0, -500, 8), RadialLayout::outside);
    EXPECT_EQ(RadialLayout::slotAt(0, 100, 4), 2);
    EXPECT_EQ(RadialLayout::slotAt(0, 100, 6), 3);
}


// ---- Hold-key scrubbing and the workspace model ----

namespace {
    curvy::SketchObject& scrubRect(curvy::SketchDocument& doc) {
        curvy::SketchObject* rect = curvy::SketchObject::makeRect(0, 0, 100, 100);
        rect->setFill(curvy::SketchPaint::solid(curvy::SketchColor::hsb(0.5f, 0.8f, 0.6f)));
        rect->setStroke(curvy::SketchPaint::solid(curvy::SketchColor::rgb(0, 0, 0)));
        rect->setStrokeWidth(2.0);
        return doc.addUndoable(rect, "Draw");
    }
}

TEST(HoldScrub, PartAtTellsOutlineFromFill) {
    curvy::SketchDocument doc;
    const curvy::SketchObject& rect = scrubRect(doc);
    EXPECT_EQ(curvy::HoldScrub::partAt(rect, BLPoint(0, 50), 4.0), curvy::ScrubPart::Stroke);
    EXPECT_EQ(curvy::HoldScrub::partAt(rect, BLPoint(50, 50), 4.0), curvy::ScrubPart::Fill);
    EXPECT_EQ(curvy::HoldScrub::partAt(rect, BLPoint(200, 50), 4.0), curvy::ScrubPart::None);
}

TEST(HoldScrub, WidthFollowsTravelAndIsOneUndoStep) {
    curvy::SketchDocument doc;
    const std::size_t id = scrubRect(doc).id();
    curvy::HoldScrub scrub;
    ASSERT_TRUE(scrub.begin(doc, id, curvy::ScrubPart::Stroke, 'W', 0.0));
    scrub.update(doc, 25.0, false);
    scrub.update(doc, 50.0, false);
    EXPECT_NEAR(doc.findById(id)->strokeWidth(), 2.0 + 50.0 * 0.08, 1e-9);
    EXPECT_EQ(doc.undoStack().undoDescription(), "Draw");
    scrub.finish(doc, true);
    EXPECT_EQ(doc.undoStack().undoDescription(), "Scrub Width");
    doc.undoStack().undo();
    EXPECT_NEAR(doc.findById(id)->strokeWidth(), 2.0, 1e-9);
}

TEST(HoldScrub, CancelPutsTheValueBackWithoutAStep) {
    curvy::SketchDocument doc;
    const std::size_t id = scrubRect(doc).id();
    curvy::HoldScrub scrub;
    ASSERT_TRUE(scrub.begin(doc, id, curvy::ScrubPart::Fill, 'O', 0.0));
    scrub.update(doc, -100.0, false);
    EXPECT_LT(doc.findById(id)->activeItem(curvy::AppearanceKind::Fill).opacity, 1.0);
    scrub.finish(doc, false);
    EXPECT_NEAR(doc.findById(id)->activeItem(curvy::AppearanceKind::Fill).opacity, 1.0, 1e-9);
    EXPECT_EQ(doc.undoStack().undoDescription(), "Draw");
}

TEST(HoldScrub, ShiftSnapsAndHueKeepsSaturationAndBrightness) {
    curvy::SketchDocument doc;
    const std::size_t id = scrubRect(doc).id();
    curvy::HoldScrub scrub;
    ASSERT_TRUE(scrub.begin(doc, id, curvy::ScrubPart::Fill, 'H', 0.0));
    EXPECT_NEAR(scrub.startValue(), 180.0, 0.5);
    scrub.update(doc, 47.0, true);
    EXPECT_DOUBLE_EQ(scrub.value(), 240.0);
    const curvy::SketchColor hsb = doc.findById(id)->fill().color().convertedTo(curvy::ColorSpace::HSB);
    EXPECT_NEAR(hsb.component(0) * 360.0, 240.0, 0.5);
    EXPECT_NEAR(hsb.component(1), 0.8, 0.01);
    EXPECT_NEAR(hsb.component(2), 0.6, 0.01);
    scrub.finish(doc, true);
}

TEST(HoldScrub, ColorKeysNeedASolidPaint) {
    curvy::SketchDocument doc;
    curvy::SketchObject& rect = scrubRect(doc);
    rect.setFill(curvy::SketchPaint::none());
    EXPECT_EQ(curvy::HoldScrub::propFor(rect, curvy::ScrubPart::Fill, 'H'), nullptr);
    EXPECT_NE(curvy::HoldScrub::propFor(rect, curvy::ScrubPart::Fill, 'O'), nullptr);
    EXPECT_EQ(curvy::HoldScrub::propFor(rect, curvy::ScrubPart::Stroke, 'B'), nullptr);
}

TEST(WorkspaceModel, HintBarFollowsTheState) {
    curvy::WorkspaceModel model;
    EXPECT_EQ(model.hintTool(), "Nothing open");
    EXPECT_EQ(model.hintKeys(), "Tap Space to start");
    int changes = 0;
    model.onChanged += [&](newui::Model&) { ++changes; return newui::SyncReturn::Handled; };
    model.setDocument(true, "Untitled - A4");
    EXPECT_EQ(model.hintTool(), "Select");
    EXPECT_NE(model.hintKeys().find("hold R E P S L"), std::string::npos);
    model.setTool("Star", "S");
    EXPECT_NE(model.hintKeys().find("release S to stop"), std::string::npos);
    model.setTool("Select");
    model.setHover({1, curvy::ScrubPart::Stroke});
    EXPECT_NE(model.hintKeys().find("Outline: hold W Width"), std::string::npos);
    const curvy::ScrubProp* width = &curvy::HoldScrub::props(curvy::ScrubPart::Stroke)[0];
    model.setScrub(true, curvy::ScrubPart::Stroke, width, 4.0, 10, 10);
    EXPECT_NE(model.hintKeys().find("Outline Width 4.0 pt"), std::string::npos);
    EXPECT_GE(changes, 4);
}

TEST(WorkspaceModel, HintsFadeAfterThreeUses) {
    curvy::WorkspaceModel model;
    model.setDocument(true, "x");
    for (int i = 0; i < 3; ++i) { model.used("holdDraw"); model.used("pan"); model.used("ring"); }
    EXPECT_EQ(model.hintKeys().find("hold R E P S L"), std::string::npos);
}

TEST(Theme, TokensMeetTheContrastTargetsOfTheDarkModeNotes) {
    for (const curvy::Theme* theme : {&curvy::Theme::dark(), &curvy::Theme::light()}) {
        for (std::uint32_t background : {theme->bgApp, theme->bgSurface, theme->bgCanvas}) {
            EXPECT_GE(curvy::Theme::contrast(theme->textPrimary, background), 4.5);
            EXPECT_GE(curvy::Theme::contrast(theme->textMuted, background), 4.5);
            EXPECT_GE(curvy::Theme::contrast(theme->iconNeutral, background), 3.0);
        }
        EXPECT_GE(curvy::Theme::contrast(theme->accent, theme->bgApp), 3.0);
        EXPECT_GE(curvy::Theme::contrast(theme->onAccent, theme->accent), 4.5);
        EXPECT_GE(curvy::Theme::contrast(theme->highlight, theme->bgSurface), 4.5);
        EXPECT_GE(curvy::Theme::contrast(theme->textPrimary, theme->slot), 4.5);
    }
    EXPECT_NE(curvy::Theme::dark().bgApp, 0x000000u);
    EXPECT_NE(curvy::Theme::dark().textPrimary, 0xFFFFFFu);
}

// ---- Which property boxes the dock shows ----

TEST(DockContext, NothingSelectedShowsDefaultsForADrawingToolAndANoteOtherwise) {
    curvy::SketchDocument doc;
    const curvy::DockBoxes drawing = curvy::DockContext::boxesFor(doc, curvy::SketchTool::Star);
    EXPECT_TRUE(drawing.colour && drawing.stroke && drawing.opacity && !drawing.transform);
    EXPECT_EQ(drawing.caption, "New shapes");
    const curvy::DockBoxes select = curvy::DockContext::boxesFor(doc, curvy::SketchTool::Select);
    EXPECT_FALSE(select.any());
    EXPECT_EQ(select.caption, "Nothing selected");
    EXPECT_FALSE(curvy::DockContext::boxesFor(doc, curvy::SketchTool::Hand).any());
}

TEST(DockContext, ASelectedShapeShowsTransformColourStrokeAndOpacity) {
    curvy::SketchDocument doc;
    const std::size_t id = doc.addUndoable(curvy::SketchObject::makeRect(0, 0, 10, 10), "Draw").id();
    doc.setSelection({id});
    const curvy::DockBoxes boxes = curvy::DockContext::boxesFor(doc, curvy::SketchTool::Select);
    EXPECT_TRUE(boxes.transform && boxes.colour && boxes.stroke && boxes.opacity);
    EXPECT_TRUE(boxes.caption.empty());
    EXPECT_EQ(boxes, curvy::DockContext::boxesFor(doc, curvy::SketchTool::Star));   // a selection wins over the armed tool
}

TEST(DockContext, ATextOnlySelectionLeavesOutStroke) {
    curvy::SketchDocument doc;
    const std::size_t id = doc.addUndoable(curvy::SketchObject::makeText("Hi", "Segoe UI", 24.0, BLPoint(0, 20)), "Text").id();
    doc.setSelection({id});
    const curvy::DockBoxes boxes = curvy::DockContext::boxesFor(doc, curvy::SketchTool::Select);
    EXPECT_TRUE(boxes.transform && boxes.colour && boxes.opacity && !boxes.stroke);
}

TEST(DockContext, AGroupShowsOnlyTransformAndOpacity) {
    curvy::SketchDocument doc;
    const std::size_t a = doc.addUndoable(curvy::SketchObject::makeRect(0, 0, 10, 10), "Draw").id();
    const std::size_t b = doc.addUndoable(curvy::SketchObject::makeRect(20, 0, 10, 10), "Draw").id();
    doc.setSelection({a, b});
    ASSERT_TRUE(doc.groupSelection());
    const curvy::DockBoxes boxes = curvy::DockContext::boxesFor(doc, curvy::SketchTool::Select);
    EXPECT_TRUE(boxes.transform && boxes.opacity && !boxes.colour && !boxes.stroke);
}

TEST(Settings, RoundTripsThroughText) {
    curvy::Settings settings;
    settings.theme = curvy::Settings::ThemeChoice::Light;
    settings.spaceTapMs = 650;
    settings.exportFolder = "C:\out";
    curvy::Settings loaded;
    loaded.fromText(settings.toText());
    EXPECT_EQ(loaded.toText(), settings.toText());
}

TEST(Settings, BadValuesAreSkippedAndPagesReset) {
    curvy::Settings settings;
    settings.fromText("spaceTapMs=99999\nunknown=1\nscrubSensitivity=250\n");
    EXPECT_EQ(settings.spaceTapMs, 300);
    EXPECT_EQ(settings.scrubSensitivity, 250);
    settings.resetPage(curvy::SettingsPage::Pen);
    EXPECT_EQ(settings.spaceTapMs, 300);
}

TEST(SketchSvg, ToolDefaultsRoundTripAndAreOmittedWhenUnset) {
    SketchDocument doc;
    EXPECT_EQ(svgFromDocument(doc).find("data-tools"), std::string::npos);
    doc.setToolDefaults({7, 9});
    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(svgFromDocument(doc), loaded));
    EXPECT_EQ(loaded.toolDefaults().polygonSides, 7);
    EXPECT_EQ(loaded.toolDefaults().starPoints, 9);
}

TEST(Settings, ToolDefaultsAreRangeCheckedAndReset) {
    curvy::Settings settings;
    settings.fromText("polygonSides=99\nstarPoints=6\n");
    EXPECT_EQ(settings.polygonSides, 5);
    EXPECT_EQ(settings.starPoints, 6);
    settings.resetPage(curvy::SettingsPage::Drawing);
    EXPECT_EQ(settings.starPoints, 5);
}
