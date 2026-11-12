#include "curvy/objectnode.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>

#include <gtest/gtest.h>

using curvy::ObjectNode;
using jitnodes::Network;
using jitnodes::NodeId;

namespace {

    class ObjectNodeTest : public ::testing::Test {
    protected:
        void SetUp() override { curvy::registerObjectNodes(); }

        static ObjectNode& object(Network& net, NodeId id) { return static_cast<ObjectNode&>(net.node(id)); }

        // An object holding one line of `count` points in its merge.
        static NodeId addLineTo(ObjectNode& obj, float count) {
            Network& inner = obj.network();
            const NodeId line = inner.addNode("line");
            inner.node(line).setFloat("count", count);
            inner.connect(line, obj.mergeId(), 0);
            return line;
        }
    };

}

TEST_F(ObjectNodeTest, aNewObjectHasAMergeWiredIntoItsDisplayedXform) {
    Network net;
    const NodeId id = net.addNode(curvy::objectNodeType);
    ObjectNode& obj = object(net, id);

    EXPECT_TRUE(obj.validate().empty());
    EXPECT_EQ(obj.network().nodeIds().size(), 2u);
    EXPECT_TRUE(obj.network().node(obj.xformId()).hasTag(curvy::objectXformTag));
    EXPECT_TRUE(obj.network().node(obj.mergeId()).hasTag(curvy::objectMergeTag));
    EXPECT_EQ(obj.network().displayNode(), obj.xformId());
    EXPECT_EQ(obj.network().inputOf(obj.xformId(), 0), obj.mergeId());
}

TEST_F(ObjectNodeTest, anEmptyObjectCooksToNothingAndItsContentsFollowTheXform) {
    Network net;
    const NodeId id = net.addNode(curvy::objectNodeType);
    ObjectNode& obj = object(net, id);
    EXPECT_EQ(net.cook(id)->pointCount(), 0u);

    addLineTo(obj, 3.0f);
    obj.network().node(obj.xformId()).setFloat("tx", 10.0f);
    const auto geo = net.cook(id);
    ASSERT_EQ(geo->pointCount(), 3u);
    EXPECT_FLOAT_EQ(geo->x()[0], 10.0f);
}

TEST_F(ObjectNodeTest, anObjectRoundTripsThroughSaveAndLoad) {
    Network net;
    const NodeId id = net.addNode(curvy::objectNodeType, "Layer1");
    ObjectNode& obj = object(net, id);
    obj.addTag("layer");
    obj.setFloat("locked", 1.0f);
    addLineTo(obj, 4.0f);

    Network loaded;
    jitnodes::loadNetwork(loaded, jitnodes::saveNetwork(net));
    ObjectNode& copy = object(loaded, id);
    EXPECT_TRUE(copy.validate().empty());
    EXPECT_TRUE(copy.hasTag("layer"));
    EXPECT_EQ(copy.getFloat("locked"), 1.0f);
    EXPECT_EQ(copy.network().nodeIds().size(), 3u);
    EXPECT_EQ(loaded.cook(id)->pointCount(), 4u);
}

TEST_F(ObjectNodeTest, anObjectSurvivesUndoStyleSnapshots) {
    Network net;
    const NodeId id = net.addNode(curvy::objectNodeType);
    const jitnodes::GraphSnapshot before = jitnodes::snapshotNetwork(net);
    addLineTo(object(net, id), 2.0f);
    const jitnodes::GraphSnapshot after = jitnodes::snapshotNetwork(net, &before);

    jitnodes::restoreSnapshot(net, before, &after);
    EXPECT_TRUE(object(net, id).validate().empty());
    EXPECT_EQ(net.cook(id)->pointCount(), 0u);
    jitnodes::restoreSnapshot(net, after, &before);
    EXPECT_TRUE(object(net, id).validate().empty());
    EXPECT_EQ(net.cook(id)->pointCount(), 2u);
}

TEST_F(ObjectNodeTest, aFileWithAMissingXformIsRepairedOnLoad) {
    Network net;
    const NodeId id = net.addNode(curvy::objectNodeType);
    ObjectNode& obj = object(net, id);
    addLineTo(obj, 2.0f);
    obj.network().removeNode(obj.xformId());
    EXPECT_FALSE(obj.validate().empty());

    Network loaded;
    jitnodes::loadNetwork(loaded, jitnodes::saveNetwork(net));
    EXPECT_FALSE(object(loaded, id).validate().empty());
    EXPECT_TRUE(object(loaded, id).repair());
    EXPECT_TRUE(object(loaded, id).validate().empty());
    EXPECT_EQ(loaded.cook(id)->pointCount(), 2u);
    EXPECT_FALSE(object(loaded, id).repair());
}

TEST_F(ObjectNodeTest, nestedObjectsCookThroughEachOther) {
    Network net;
    const NodeId outerId = net.addNode(curvy::objectNodeType, "Layer1");
    ObjectNode& outer = object(net, outerId);
    const NodeId innerId = outer.network().addNode(curvy::objectNodeType, "Group1");
    outer.network().connect(innerId, outer.mergeId(), 0);
    ObjectNode& inner = static_cast<ObjectNode&>(outer.network().node(innerId));
    addLineTo(inner, 5.0f);
    EXPECT_EQ(net.cook(outerId)->pointCount(), 5u);

    inner.network().disconnect(inner.xformId(), 0);
    EXPECT_FALSE(inner.validate().empty());
    EXPECT_TRUE(inner.repair());
    EXPECT_TRUE(inner.validate().empty());
    EXPECT_EQ(net.cook(outerId)->pointCount(), 5u);
}
