#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"
#include "testsupport.h"

#include <filesystem>

#include <gtest/gtest.h>

using curvy::NodeRef;
using curvy::SketchDocument;

namespace {

    class SketchObjectsTest : public ::testing::Test {
    protected:
        std::string nameOf(SketchDocument& document, const NodeRef& ref) { return document.node(ref).name(); }

        std::vector<std::string> namesOf(SketchDocument& document, const NodeRef& parent) {
            std::vector<std::string> names;
            for (const NodeRef& child : document.childrenOf(parent)) {
                names.push_back(nameOf(document, child));
            }
            return names;
        }

        SketchDocument document_;
    };

}

TEST_F(SketchObjectsTest, theStructuralNodesCannotBeDeletedOrRewired) {
    const NodeRef layer = document_.addLayer();
    const NodeRef fill = document_.addCircle(layer, 10.0f, 10.0f, 5.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const auto* object = dynamic_cast<const curvy::ObjectNode*>(&document_.node(layer));
    ASSERT_NE(object, nullptr);
    const std::uint32_t inside = document_.scopeOfSubnet(layer);
    const NodeRef xform(inside, object->xformId());
    const NodeRef merge(inside, object->mergeId());
    const NodeRef rootMerge(curvy::ObjectTree::rootMergeOf(document_.network()));
    const NodeRef render(document_.network().displayNode());
    const std::string lastStep = document_.undoStack().undoDescription();
    const std::string graph = document_.graphToText();

    for (const NodeRef& structural : {xform, merge, rootMerge, render}) {
        EXPECT_THROW(document_.removeGraphNode(structural), std::runtime_error);
    }
    EXPECT_THROW(document_.disconnectGraphNode(xform, 0), std::runtime_error);
    EXPECT_THROW(document_.disconnectGraphNode(render, 0), std::runtime_error);
    EXPECT_THROW(document_.connectGraphNodes(fill, xform, 0), std::runtime_error);
    EXPECT_THROW(document_.connectGraphNodes(layer, NodeRef(inside, fill.id), 0), std::runtime_error);   // across scopes
    EXPECT_EQ(document_.graphToText(), graph);
    EXPECT_EQ(document_.undoStack().undoDescription(), lastStep);
}

TEST_F(SketchObjectsTest, shapesInsideAnObjectCanStillBeWiredAndDeletedWithUndo) {
    const NodeRef layer = document_.addLayer();
    const NodeRef fill = document_.addCircle(layer, 10.0f, 10.0f, 5.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const std::string graph = document_.graphToText();

    document_.disconnectGraphNode(fill, 0);
    EXPECT_EQ(document_.undoStack().undoDescription(), "Disconnect Nodes");
    document_.undoStack().undo();
    EXPECT_EQ(document_.graphToText(), graph);

    document_.removeGraphNode(fill);
    EXPECT_FALSE(document_.hasNode(fill));
    document_.undoStack().undo();
    EXPECT_TRUE(document_.hasNode(fill));
}

TEST_F(SketchObjectsTest, layersGetNumberedNamesAndStackInOrder) {
    const NodeRef a = document_.addLayer();
    const NodeRef b = document_.addLayer();
    const NodeRef named = document_.addLayer("Sky");
    EXPECT_EQ(nameOf(document_, a), "Layer1");
    EXPECT_EQ(nameOf(document_, b), "Layer2");
    EXPECT_TRUE(document_.node(named).hasTag(curvy::layerTag));
    EXPECT_EQ(namesOf(document_, curvy::kNoNode), (std::vector<std::string>{"Layer1", "Layer2", "Sky"}));
    EXPECT_EQ(document_.cook(document_.network().displayNode())->pointCount(), 0u);
}

TEST_F(SketchObjectsTest, addingIsOneUndoStepAndUndoRemovesTheRootMergeAndRenderToo) {
    document_.addLayer();
    EXPECT_EQ(document_.network().nodeCount(), 3u);
    const jitnodes::NodeId render = curvy::ObjectTree::rootRenderOf(document_.network());
    ASSERT_NE(render, jitnodes::invalidNodeId);
    EXPECT_EQ(document_.network().displayNode(), render);
    EXPECT_EQ(document_.network().inputOf(render, 0), curvy::ObjectTree::rootMergeOf(document_.network()));
    document_.undoStack().undo();
    EXPECT_EQ(document_.network().nodeCount(), 0u);
    document_.undoStack().redo();
    EXPECT_EQ(document_.childrenOf().size(), 1u);
}

TEST_F(SketchObjectsTest, groupsGoInsideLayersAndGroupsAndNothingElse) {
    const NodeRef layer = document_.addLayer();
    const NodeRef outer = document_.addGroup(layer);
    const NodeRef inner = document_.addGroup(outer);
    EXPECT_EQ(namesOf(document_, layer), (std::vector<std::string>{"Group1"}));
    EXPECT_EQ(namesOf(document_, outer), (std::vector<std::string>{"Group1"}));
    EXPECT_TRUE(document_.node(inner).hasTag(curvy::groupTag));
    EXPECT_THROW(document_.addGroup(curvy::kNoNode), std::runtime_error);
}

TEST_F(SketchObjectsTest, reorderingLayersChangesTheOrderAndUndoes) {
    const NodeRef a = document_.addLayer("A");
    const NodeRef b = document_.addLayer("B");
    const NodeRef c = document_.addLayer("C");
    document_.reorderLayers({c, a, b});
    EXPECT_EQ(namesOf(document_, curvy::kNoNode), (std::vector<std::string>{"C", "A", "B"}));
    document_.undoStack().undo();
    EXPECT_EQ(namesOf(document_, curvy::kNoNode), (std::vector<std::string>{"A", "B", "C"}));
    EXPECT_THROW(document_.reorderLayers({a, b}), std::runtime_error);
}

TEST_F(SketchObjectsTest, aGroupMovesToAnotherParentKeepingItsContents) {
    const NodeRef one = document_.addLayer("One");
    const NodeRef two = document_.addLayer("Two");
    const NodeRef group = document_.addGroup(one, "G");
    document_.addGroup(group, "Inside");

    const NodeRef moved = document_.moveGroup(group, two, 0);
    EXPECT_TRUE(document_.childrenOf(one).empty());
    EXPECT_EQ(namesOf(document_, two), (std::vector<std::string>{"G"}));
    EXPECT_EQ(namesOf(document_, moved), (std::vector<std::string>{"Inside"}));
    EXPECT_TRUE(document_.node(moved).hasTag(curvy::groupTag));

    document_.undoStack().undo();
    EXPECT_EQ(namesOf(document_, one), (std::vector<std::string>{"G"}));
    EXPECT_TRUE(document_.childrenOf(two).empty());
}

TEST_F(SketchObjectsTest, movingWithinTheSameParentReordersAndCyclesAreRefused) {
    const NodeRef layer = document_.addLayer();
    const NodeRef a = document_.addGroup(layer, "A");
    document_.addGroup(layer, "B");
    document_.moveGroup(a, layer, 1);
    EXPECT_EQ(namesOf(document_, layer), (std::vector<std::string>{"B", "A"}));

    const NodeRef inner = document_.addGroup(a, "Inner");
    EXPECT_THROW(document_.moveGroup(a, inner, 0), std::runtime_error);
    EXPECT_THROW(document_.moveGroup(a, a, 0), std::runtime_error);
    EXPECT_THROW(document_.moveGroup(layer, layer, 0), std::runtime_error);
}

TEST_F(SketchObjectsTest, theSampleLayersAreBuiltAsOneStep) {
    document_.buildSampleLayers();
    const std::vector<NodeRef> layers = document_.childrenOf();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(namesOf(document_, layers[0]), (std::vector<std::string>{"Group1", "Group2"}));
    EXPECT_EQ(namesOf(document_, layers[1]), (std::vector<std::string>{"Group3"}));
    EXPECT_EQ(namesOf(document_, layers[2]), (std::vector<std::string>{"GroupA"}));

    document_.undoStack().undo();
    EXPECT_EQ(document_.network().nodeCount(), 0u);
}

TEST_F(SketchObjectsTest, theArtworkIsTheCookedShapesOfEveryLayerWithTheirFills) {
    EXPECT_EQ(document_.artwork(), nullptr);
    document_.buildSampleLayers();
    const jitnodes::GeometryHandle artwork = document_.artwork();
    ASSERT_NE(artwork, nullptr);
    EXPECT_EQ(artwork->primitiveCount(), 4u);
    const auto* alpha = artwork->findAttrib(jitnodes::AttribClass::primitive, "fill.a");
    ASSERT_NE(alpha, nullptr);
    EXPECT_NEAR(alpha->floats()[3], 0.8f, 1e-6f);
}

TEST_F(SketchObjectsTest, aCircleIsOneUndoStepAndDrawsInItsLayer) {
    const NodeRef layer = document_.addLayer();
    document_.addCircle(layer, 10.0f, 20.0f, 5.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_NE(document_.artwork(), nullptr);
    EXPECT_EQ(document_.artwork()->primitiveCount(), 1u);
    document_.undoStack().undo();
    EXPECT_EQ(document_.artwork()->primitiveCount(), 0u);
}

TEST_F(SketchObjectsTest, aDamagedObjectIsFlaggedOnOpenAndOnlyRepairedWhenAsked) {
    const NodeRef layer = document_.addLayer("Damaged");
    auto& object = static_cast<curvy::ObjectNode&>(document_.node(layer));
    object.network().removeNode(object.xformId());
    const std::string file = curvytest::tempFile("curvy_test_damaged.curvy").u8string();
    ASSERT_TRUE(document_.save(file));

    SketchDocument opened;
    ASSERT_TRUE(opened.load(file));
    ASSERT_EQ(opened.objectProblems().size(), 1u);
    EXPECT_NE(opened.objectProblems()[0].text.find("Damaged"), std::string::npos);
    EXPECT_FALSE(static_cast<curvy::ObjectNode&>(opened.node(opened.objectProblems()[0].object)).validate().empty());

    opened.repairObjectProblems();
    EXPECT_TRUE(opened.objectProblems().empty());
    EXPECT_TRUE(static_cast<curvy::ObjectNode&>(opened.node(layer)).validate().empty());
    opened.undoStack().undo();
    EXPECT_FALSE(static_cast<curvy::ObjectNode&>(opened.node(layer)).validate().empty());

    SketchDocument sound;
    sound.addLayer();
    ASSERT_TRUE(sound.save(file));
    SketchDocument reopened;
    ASSERT_TRUE(reopened.load(file));
    EXPECT_TRUE(reopened.objectProblems().empty());
    std::filesystem::remove(file);
}
