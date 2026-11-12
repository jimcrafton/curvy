#include "curvy/geometrytable.h"

#include <gtest/gtest.h>

namespace {

    jitnodes::GeometryHandle twoShapes() {
        auto geometry = std::make_shared<jitnodes::Geometry>();
        geometry->resizePoints(4);
        for (std::size_t i = 0; i < 4; ++i) {
            geometry->x()[i] = float(i);
            geometry->y()[i] = float(i) * 2.0f;
        }
        geometry->pointAttrib("width")[1] = 2.5f;
        geometry->addPrimitive({0, 3, true, jitnodes::PrimKind::polyline});
        geometry->addPrimitive({3, 1, false, jitnodes::PrimKind::cubic});
        geometry->attrib(jitnodes::AttribClass::primitive, "fill.r").floats()[0] = 0.5f;
        geometry->setDetail("pages", 3.0f);
        return geometry;
    }

}

TEST(GeometryTableTest, pointsHaveXYAndEveryPointAttribute) {
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Points);
    ASSERT_EQ(model.rowCount(), 4u);
    ASSERT_EQ(model.columnCount(), 3u);
    EXPECT_EQ(model.columnTitle(0), "x");
    EXPECT_EQ(model.columnTitle(2), "width");
    EXPECT_EQ(model.cellText(2, 1), "4.000");
    EXPECT_EQ(model.cellText(1, 2), "2.500");
    EXPECT_EQ(std::any_cast<std::string>(model.valueAt(3, 0)), "3.000");
    EXPECT_FALSE(model.valueAt(9, 0).has_value());
}

TEST(GeometryTableTest, primitivesShowTheirShapeAndAttributes) {
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Primitives);
    ASSERT_EQ(model.rowCount(), 2u);
    EXPECT_EQ(model.cellText(0, 2), "closed");
    EXPECT_EQ(model.cellText(1, 3), "cubic");
    EXPECT_EQ(model.cellText(0, 4), "0.500");
}

TEST(GeometryTableTest, detailValuesAreRows) {
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Detail);
    ASSERT_EQ(model.rowCount(), 1u);
    EXPECT_EQ(model.cellText(0, 0), "pages");
    EXPECT_EQ(model.cellText(0, 1), "3.000");
}

TEST(GeometryTableTest, theFilterKeepsOnlyTheColumnsWhoseNameMatches) {
    curvy::GeometryTableModel none(twoShapes(), curvy::GeometryTableModel::Kind::Points, "ZZZ");
    EXPECT_EQ(none.columnCount(), 0u);
    curvy::GeometryTableModel wide(twoShapes(), curvy::GeometryTableModel::Kind::Points, "WID");
    ASSERT_EQ(wide.columnCount(), 1u);
    EXPECT_EQ(wide.columnTitle(0), "width");
    EXPECT_EQ(wide.cellText(1, 0), "2.500");
}

TEST(GeometryTableTest, theFilterAlsoNarrowsTheBuiltInColumns) {
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Primitives, "COUNT");
    ASSERT_EQ(model.columnCount(), 1u);
    EXPECT_EQ(model.columnTitle(0), "count");
    EXPECT_EQ(model.cellText(0, 0), "3");
    curvy::GeometryTableModel xy(twoShapes(), curvy::GeometryTableModel::Kind::Points, "y");
    ASSERT_EQ(xy.columnCount(), 1u);
    EXPECT_EQ(xy.cellText(2, 0), "4.000");
}

TEST(GeometryTableTest, aNullGeometryIsEmptyAndTheTableIsReadOnly) {
    curvy::GeometryTableModel empty(nullptr, curvy::GeometryTableModel::Kind::Points);
    EXPECT_EQ(empty.rowCount(), 0u);
    EXPECT_EQ(empty.columnCount(), 0u);
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Points);
    EXPECT_THROW(model.setValueAt(0, 0, std::string("1")), std::runtime_error);
}

TEST(GeometryTableTest, copiedCellsAreTabSeparatedWithAHeader) {
    curvy::GeometryTableModel model(twoShapes(), curvy::GeometryTableModel::Kind::Points);
    EXPECT_EQ(model.asText(0, 1, 0, 1), "x\ty\n0.000\t0.000\n1.000\t2.000\n");
}

TEST(GeometryTableTest, aTenThousandPointTableAnswersAtOnce) {
    auto geometry = std::make_shared<jitnodes::Geometry>();
    geometry->resizePoints(10000);
    curvy::GeometryTableModel model(geometry, curvy::GeometryTableModel::Kind::Points);
    EXPECT_EQ(model.rowCount(), 10000u);
    EXPECT_EQ(model.cellText(9999, 0), "0.000");
}
