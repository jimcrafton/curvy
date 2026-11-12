#include "curvy/appearancenode.h"
#include "curvy/objectnode.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

using jitnodes::AttribClass;
using jitnodes::Network;
using jitnodes::NodeId;

namespace {

    class Appearance : public ::testing::Test {
    protected:
        void SetUp() override {
            curvy::registerObjectNodes();
            curvy::registerAppearanceNodes();
        }

        static float primValue(const jitnodes::Geometry& geometry, const char* name, std::size_t primitive) {
            const jitnodes::AttribArray* column = geometry.findAttrib(AttribClass::primitive, name);
            return column != nullptr ? column->floats()[primitive] : -1.0f;
        }

        // circle -> fill -> stroke
        struct Shape {
            NodeId circle, fill, stroke;
        };

        static Shape addShape(Network& net, float cx) {
            Shape shape;
            shape.circle = net.addNode("circle");
            net.node(shape.circle).setFloat("cx", cx);
            shape.fill = net.addNode(curvy::fillNodeType);
            shape.stroke = net.addNode(curvy::strokeNodeType);
            net.connect(shape.circle, shape.fill);
            net.connect(shape.fill, shape.stroke);
            return shape;
        }
    };

}

TEST_F(Appearance, aFillAndAStrokeStampTheirLookOntoEveryPrimitive) {
    Network net;
    const Shape shape = addShape(net, 0.0f);
    jitnodes::Node& fill = net.node(shape.fill);
    fill.setFloat("r", 1.0f);
    fill.setFloat("alpha", 0.5f);
    fill.setFloat("opacity", 0.5f);
    jitnodes::Node& stroke = net.node(shape.stroke);
    stroke.setFloat("g", 0.25f);
    stroke.setFloat("width", 3.0f);

    const auto geometry = net.cook(shape.stroke);
    ASSERT_EQ(geometry->primitiveCount(), 1u);
    EXPECT_FLOAT_EQ(primValue(*geometry, "fill.r", 0), 1.0f);
    EXPECT_FLOAT_EQ(primValue(*geometry, "fill.a", 0), 0.25f);
    EXPECT_FLOAT_EQ(primValue(*geometry, "fill.evenOdd", 0), 0.0f);
    EXPECT_FLOAT_EQ(primValue(*geometry, "stroke.g", 0), 0.25f);
    EXPECT_FLOAT_EQ(primValue(*geometry, "stroke.width", 0), 3.0f);
    EXPECT_EQ(geometry->pointCount(), net.cook(shape.circle)->pointCount());
}

TEST_F(Appearance, anInvisibleLookPassesItsInputOnUntouched) {
    Network net;
    const Shape shape = addShape(net, 0.0f);
    net.node(shape.fill).setFloat("visible", 0.0f);
    EXPECT_EQ(net.cook(shape.fill), net.cook(shape.circle));
    EXPECT_EQ(net.cook(shape.stroke)->findAttrib(AttribClass::primitive, "fill.r"), nullptr);
}

TEST_F(Appearance, theLookSurvivesTheTransformAndTheMergesAboveIt) {
    curvy::SketchDocument document;
    const curvy::NodeRef layer = document.addLayer();
    auto& object = static_cast<curvy::ObjectNode&>(document.node(layer));
    Network& inner = object.network();
    const Shape first = addShape(inner, 0.0f);
    const Shape second = addShape(inner, 5.0f);
    inner.node(first.fill).setFloat("r", 1.0f);
    inner.node(second.fill).setFloat("r", 0.5f);
    inner.connect(first.stroke, object.mergeId(), 0);
    inner.connect(second.stroke, object.mergeId(), 1);
    inner.node(object.xformId()).setFloat("tx", 100.0f);

    const auto geometry = document.cook(document.network().displayNode());
    ASSERT_EQ(geometry->primitiveCount(), 2u);
    EXPECT_FLOAT_EQ(primValue(*geometry, "fill.r", 0), 1.0f);
    EXPECT_FLOAT_EQ(primValue(*geometry, "fill.r", 1), 0.5f);
    EXPECT_FLOAT_EQ(geometry->x()[0], 101.0f);
}

TEST_F(Appearance, anEditCostsLittleWithAThousandStampedShapes) {
    Network net;
    const NodeId merge = net.addNode("merge");
    std::vector<Shape> shapes;
    for (int i = 0; i < 1000; ++i) {
        shapes.push_back(addShape(net, float(i)));
        net.connect(shapes.back().stroke, merge, i);
    }
    net.setDisplayNode(merge);
    net.cookDisplay();

    const int edits = 50;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < edits; ++i) {
        net.node(shapes[500].fill).setFloat("r", float(i % 2));
        net.markDirty(shapes[500].fill);
        net.cookDisplay();
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / edits;
    std::printf("  edit of one stamped shape among 1000: %.2f ms\n", ms);
    EXPECT_LT(ms, 50.0);
}
