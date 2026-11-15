#include "curvy/appearancenode.h"
#include "curvy/graphview.h"
#include "curvy/objectnode.h"
#include "curvy/selectionmodel.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

namespace {

    using curvy::NodeRef;
    using Role = curvy::SelectionModel::Role;

    // A layer with one circle (circle -> fill -> the layer's merge).
    struct OneShape {
        curvy::SketchDocument document;
        NodeRef layer, fill;
        OneShape() {
            layer = document.addLayer();
            fill = document.addCircle(layer, 0.0f, 0.0f, 10.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
        }
        NodeRef add(const char* type, std::uint32_t scope, const NodeRef& after) {
            const NodeRef node = document.addGraphNode(type, "", scope);
            document.connectGraphNodes(after, node);
            return node;
        }
    };

}

TEST(SelectionModelTest, setAddToggleAndRemoveKeepThePrimary) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    int fired = 0;
    sel.onChanged += [&fired](curvy::SelectionModel&) {
        ++fired;
        return newui::SyncReturn::Ignored;
    };
    EXPECT_TRUE(sel.set(s.layer));
    EXPECT_FALSE(sel.set(s.layer));   // the same again changes nothing
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(sel.add(s.fill));
    EXPECT_EQ(sel.primary(), s.fill);
    EXPECT_EQ(sel.items().size(), 2u);
    EXPECT_TRUE(sel.setPrimary(s.layer));
    EXPECT_EQ(sel.primary(), s.layer);
    EXPECT_EQ(sel.items().size(), 2u);
    EXPECT_TRUE(sel.toggle(s.layer));
    EXPECT_EQ(sel.primary(), s.fill);
    EXPECT_TRUE(sel.toggle(s.fill));
    EXPECT_TRUE(sel.empty());
    EXPECT_EQ(fired, 5);
}

TEST(SelectionModelTest, anUnknownNodeIsNeverSelected) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    EXPECT_FALSE(sel.add(NodeRef()));
    EXPECT_FALSE(sel.add(NodeRef(0, 9999)));
    EXPECT_TRUE(sel.set(s.layer));
    EXPECT_TRUE(sel.set(NodeRef(0, 9999)));   // clears: it is the same as set of nothing
    EXPECT_TRUE(sel.empty());
}

TEST(SelectionModelTest, addAllNotifiesOnceAndTheLastNewOneIsThePrimary) {
    OneShape s;
    const NodeRef other = s.document.addLayer();
    curvy::SelectionModel sel(&s.document);
    int fired = 0;
    sel.onChanged += [&fired](curvy::SelectionModel&) {
        ++fired;
        return newui::SyncReturn::Ignored;
    };
    EXPECT_TRUE(sel.addAll({s.layer, other}));
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(sel.primary(), other);
    EXPECT_FALSE(sel.addAll({s.layer, other}));
    EXPECT_EQ(fired, 1);
}

TEST(SelectionModelTest, deletedNodesLeaveTheSelection) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    sel.add(s.layer);
    sel.add(s.fill);
    int fired = 0;
    sel.onChanged += [&fired](curvy::SelectionModel&) {
        ++fired;
        return newui::SyncReturn::Ignored;
    };
    s.document.removeGraphNode(s.fill);
    EXPECT_EQ(sel.items(), std::vector<NodeRef>{s.layer});
    EXPECT_EQ(sel.primary(), s.layer);
    EXPECT_EQ(fired, 1);
}

TEST(SelectionModelTest, anObjectSeesItsOwnPartsFromAShapeInside) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    sel.set(s.fill);
    EXPECT_EQ(sel.object(), s.layer);
    EXPECT_EQ(sel.layer(), s.layer);
    EXPECT_EQ(sel.fill(), s.fill);
    EXPECT_EQ(s.document.node(sel.shape()).typeName(), "circle");
    ASSERT_TRUE(sel.objectXform().valid());
    EXPECT_TRUE(s.document.node(sel.objectXform()).hasTag(curvy::objectXformTag));
    EXPECT_FALSE(sel.stroke().valid());
    EXPECT_FALSE(sel.localTransform().valid());
    EXPECT_EQ(sel.count(Role::Stroke), 0);

    sel.set(sel.shape());   // selecting the circle finds the same fill downstream
    EXPECT_EQ(sel.fill(), s.fill);
}

TEST(SelectionModelTest, aSelectedLayerHasNoFillOfItsOwn) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    sel.set(s.layer);
    EXPECT_EQ(sel.object(), s.layer);
    EXPECT_TRUE(sel.objectXform().valid());
    EXPECT_FALSE(sel.fill().valid());
    EXPECT_FALSE(sel.shape().valid());
}

TEST(SelectionModelTest, aGroupInsideALayerIsTheObjectAndTheLayerIsFoundAbove) {
    OneShape s;
    const NodeRef group = s.document.addGroup(s.layer);
    const NodeRef fill = s.document.addCircle(group, 5.0f, 5.0f, 3.0f, newui::Color(0.0f, 1.0f, 0.0f, 1.0f));
    curvy::SelectionModel sel(&s.document);
    sel.set(fill);
    EXPECT_EQ(sel.object(), group);
    EXPECT_EQ(sel.layer(), s.layer);
    sel.set(group);
    EXPECT_EQ(sel.layer(), s.layer);
}

TEST(SelectionModelTest, theChainIsFollowedPastABranchThatAlsoFeedsTheMerge) {
    OneShape s;
    const std::uint32_t scope = s.fill.scope;
    const NodeRef stroke = s.add(curvy::strokeNodeType, scope, s.fill);
    const NodeRef transform = s.add("transform", scope, stroke);
    curvy::SelectionModel sel(&s.document);
    sel.set(s.fill);
    EXPECT_EQ(sel.stroke(), stroke);
    EXPECT_EQ(sel.localTransform(), transform);
    sel.set(transform);   // and upstream from the other end
    EXPECT_EQ(sel.fill(), s.fill);
    EXPECT_EQ(s.document.node(sel.shape()).typeName(), "circle");
}

TEST(SelectionModelTest, twoFillsAreCountedAndTheNearestToTheMergeWins) {
    OneShape s;
    const NodeRef second = s.add(curvy::fillNodeType, s.fill.scope, s.fill);
    curvy::SelectionModel sel(&s.document);
    sel.set(s.fill);
    EXPECT_EQ(sel.count(Role::Fill), 2);
    EXPECT_EQ(sel.fill(), second);
}

TEST(SelectionModelTest, nothingSelectedMeansNothingFound) {
    OneShape s;
    curvy::SelectionModel sel(&s.document);
    EXPECT_FALSE(sel.object().valid());
    EXPECT_FALSE(sel.fill().valid());
    EXPECT_EQ(sel.count(Role::Fill), 0);
}

TEST(SelectionModelTest, theGraphViewAndAnotherViewShareOneSelection) {
    OneShape s;
    curvy::SelectionModel shared(&s.document);
    curvy::GraphView view;
    view.setSelectionModel(&shared);
    view.setDocument(&s.document);
    int fired = 0;
    view.onSelectionChanged = [&fired]() { ++fired; };

    shared.set(s.layer);   // another view picked it
    EXPECT_EQ(view.selected(), s.layer);
    EXPECT_EQ(fired, 1);

    view.select(s.fill);   // and the other way round
    EXPECT_EQ(shared.primary(), s.fill);
    EXPECT_EQ(fired, 2);
}

TEST(SelectionModelTest, theViewKeepsItsOwnSelectionUntilGivenAShared) {
    OneShape s;
    curvy::GraphView view;
    view.setDocument(&s.document);
    view.select(s.layer);
    EXPECT_EQ(view.selection().size(), 1u);
    EXPECT_EQ(view.selectionModel().primary(), s.layer);
}
