#include "curvy/objectnode.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

namespace {

    class SketchNodesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            document_.buildSampleLayers();
            layer_ = document_.childrenOf()[0];
            group_ = document_.childrenOf(layer_)[0];
        }

        curvy::NodeRef find(const curvy::NodeRef& object, const std::string& type) {
            const std::uint32_t scope = document_.scopeOfSubnet(object);
            for (const jitnodes::NodeId id : document_.networkOf(scope).nodeIds()) {
                if (document_.networkOf(scope).node(id).typeName() == type) {
                    return curvy::NodeRef(scope, id);
                }
            }
            return curvy::NodeRef();
        }

        std::size_t circles() { return document_.artwork() == nullptr ? 0 : document_.artwork()->primitiveCount(); }

        curvy::SketchDocument document_;
        curvy::NodeRef layer_, group_;
    };

}

TEST_F(SketchNodesTest, bypassingAFillStopsItsLookAndUndoes) {
    const curvy::NodeRef fill = find(group_, "curvyFill");
    ASSERT_TRUE(fill.valid());
    document_.setNodeBypassed(fill, true);
    EXPECT_TRUE(document_.node(fill).bypassed());
    const auto* alpha = document_.artwork()->findAttrib(jitnodes::AttribClass::primitive, "fill.a");
    ASSERT_NE(alpha, nullptr);   // the other circles still have theirs
    document_.undoStack().undo();
    EXPECT_FALSE(document_.node(fill).bypassed());
}

TEST_F(SketchNodesTest, structuralNodesCannotBeBypassed) {
    const curvy::NodeRef xform(document_.scopeOfSubnet(group_), static_cast<curvy::ObjectNode&>(document_.node(group_)).xformId());
    EXPECT_THROW(document_.setNodeBypassed(xform, true), std::runtime_error);
}

TEST_F(SketchNodesTest, freezingKeepsTheResultAndUnfreezingReleasesIt) {
    const curvy::NodeRef fill = find(group_, "curvyFill");
    document_.setNodeFrozen(fill, true);
    EXPECT_TRUE(document_.node(fill).frozen());
    document_.setNodeFrozen(fill, false);
    EXPECT_FALSE(document_.node(fill).frozen());
}

TEST_F(SketchNodesTest, displayingALayerShowsOnlyItsShapes) {
    EXPECT_EQ(circles(), 4u);
    document_.setNodeDisplayed(layer_);
    EXPECT_EQ(circles(), 2u);   // Layer1 holds Group1 and Group2
    document_.undoStack().undo();
    EXPECT_EQ(circles(), 4u);
    EXPECT_THROW(document_.setNodeDisplayed(find(group_, "curvyFill")), std::runtime_error);
}

TEST_F(SketchNodesTest, groupingAShapeChainWrapsItAndUngroupingGivesItBack) {
    const curvy::NodeRef fill = find(group_, "curvyFill");
    const std::size_t before = document_.networkOf(fill.scope).nodeCount();

    const curvy::NodeRef inner = document_.groupNode(fill);
    EXPECT_TRUE(document_.node(inner).hasTag(curvy::groupTag));
    EXPECT_EQ(document_.networkOf(fill.scope).nodeCount(), before - 2 + 1);   // the circle and the fill went inside, a group took their place
    EXPECT_EQ(circles(), 4u);
    EXPECT_EQ(document_.childrenOf(group_).size(), 1u);

    const std::vector<curvy::NodeRef> back = document_.ungroup(inner);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(document_.networkOf(fill.scope).nodeCount(), before);
    EXPECT_EQ(document_.node(back.front()).typeName(), "curvyFill");
    EXPECT_EQ(circles(), 4u);

    document_.undoStack().undo();
    EXPECT_EQ(document_.childrenOf(group_).size(), 1u);
    document_.undoStack().undo();
    EXPECT_EQ(document_.networkOf(fill.scope).nodeCount(), before);
}

TEST_F(SketchNodesTest, aLayerOrAGroupCannotBeGroupedAndAMovedGroupCannotBeUngroupedWithATransform) {
    EXPECT_THROW(document_.groupNode(group_), std::runtime_error);
    const curvy::NodeRef xform(document_.scopeOfSubnet(group_), static_cast<curvy::ObjectNode&>(document_.node(group_)).xformId());
    EXPECT_THROW(document_.groupNode(xform), std::runtime_error);
    EXPECT_THROW(document_.ungroup(layer_), std::runtime_error);

    document_.setGraphFloats(xform, {{"tx", 10.0f}}, "Move");
    EXPECT_THROW(document_.ungroup(group_), std::runtime_error);
}

TEST_F(SketchNodesTest, ungroupingAGroupOfSeveralShapesFillsTheLayersMerge) {
    const curvy::NodeRef extra = document_.addGroup(layer_, "Pair");
    auto& pair = static_cast<curvy::ObjectNode&>(document_.node(extra));
    curvy::ObjectTree::addCircle(pair.network(), pair.mergeId(), 5.0f, 5.0f, 3.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    curvy::ObjectTree::addCircle(pair.network(), pair.mergeId(), 9.0f, 9.0f, 3.0f, newui::Color(0.0f, 1.0f, 0.0f, 1.0f));
    document_.graphReplaced();
    EXPECT_EQ(circles(), 6u);

    const std::vector<curvy::NodeRef> out = document_.ungroup(extra);
    EXPECT_EQ(out.size(), 2u);
    EXPECT_EQ(circles(), 6u);
    EXPECT_EQ(document_.childrenOf(layer_).size(), 2u);   // Group1 and Group2 remain
}
