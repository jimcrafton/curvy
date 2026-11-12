#include "curvy/sketchdocument.h"

#include "curvy/appearancenode.h"
#include "curvy/objectnode.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>

#include <algorithm>
#include <stdexcept>

namespace curvy {

    using jitnodes::Network;
    using jitnodes::NodeId;

    NodeRef SketchDocument::addLayer(const std::string& name) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        Network& root = network();
        const NodeRef ref(0, ObjectTree::addObject(root, ObjectTree::ensureRootMerge(root), layerTag, name));
        commitGraphEdit(before, "Add Layer");
        return ref;
    }

    NodeRef SketchDocument::addGroup(const NodeRef& parent, const std::string& name) {
        ObjectNode* owner = hasNode(parent) ? dynamic_cast<ObjectNode*>(&node(parent)) : nullptr;
        if (owner == nullptr) {
            throw std::runtime_error("a group goes into a layer or another group");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeRef ref(scopeOfSubnet(parent), ObjectTree::addObject(owner->network(), owner->mergeId(), groupTag, name));
        commitGraphEdit(before, "Add Group");
        return ref;
    }

    std::vector<NodeRef> SketchDocument::childrenOf(const NodeRef& parent) {
        std::vector<NodeRef> children;
        Network* net = nullptr;
        NodeId merge = jitnodes::invalidNodeId;
        std::uint32_t scope = 0;
        if (!parent.valid()) {
            net = &network();
            merge = ObjectTree::rootMergeOf(*net);
        } else if (hasNode(parent)) {
            if (auto* owner = dynamic_cast<ObjectNode*>(&node(parent))) {
                net = &owner->network();
                merge = owner->mergeId();
                scope = scopeOfSubnet(parent);
            }
        }
        if (net == nullptr || merge == jitnodes::invalidNodeId) {
            return children;
        }
        for (const NodeId id : ObjectTree::mergeInputs(*net, merge)) {
            if (ObjectTree::objectAt(*net, id) != nullptr) {
                children.emplace_back(scope, id);
            }
        }
        return children;
    }

    void SketchDocument::reorderChildren(const NodeRef& parent, const std::vector<NodeRef>& order) {
        const std::vector<NodeRef> current = childrenOf(parent);
        std::vector<NodeRef> a = current;
        std::vector<NodeRef> b = order;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) {
            throw std::runtime_error("the new order is not a permutation of the children");
        }
        if (current == order) {
            return;
        }
        Network* net = &network();
        NodeId merge = ObjectTree::rootMergeOf(*net);
        if (parent.valid()) {
            ObjectNode& owner = static_cast<ObjectNode&>(node(parent));
            net = &owner.network();
            merge = owner.mergeId();
        }
        std::vector<NodeId> ids;
        for (const NodeRef& ref : order) {
            ids.push_back(ref.id);
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        ObjectTree::setMergeInputs(*net, merge, ids);
        commitGraphEdit(before, parent.valid() ? "Reorder Groups" : "Reorder Layers");
    }

    NodeRef SketchDocument::moveGroup(const NodeRef& group, const NodeRef& parent, std::size_t index) {
        if (!hasNode(group) || !node(group).hasTag(groupTag)) {
            throw std::runtime_error("only a group can be moved");
        }
        ObjectNode* target = hasNode(parent) ? dynamic_cast<ObjectNode*>(&node(parent)) : nullptr;
        if (target == nullptr) {
            throw std::runtime_error("a group goes into a layer or another group");
        }
        for (NodeRef up = parent; up.valid(); up = parentOfScope(up.scope)) {
            if (up == group) {
                throw std::runtime_error("a group cannot go into itself");
            }
        }

        std::vector<NodeRef> siblings = childrenOf(parent);
        const auto present = std::find(siblings.begin(), siblings.end(), group);
        if (present != siblings.end()) {
            siblings.erase(present);
            siblings.insert(siblings.begin() + std::ptrdiff_t((std::min)(index, siblings.size())), group);
            reorderChildren(parent, siblings);
            return group;
        }

        const jitnodes::GraphSnapshot before = snapshotGraph();
        const std::uint32_t targetScope = scopeOfSubnet(parent);
        Network& from = networkOf(group.scope);
        Network& into = target->network();
        const NodeId pasted = jitnodes::pasteNodes(into, {jitnodes::copyNode(from, group.id)}).front();
        from.removeNode(group.id);
        std::vector<NodeId> order = ObjectTree::mergeInputs(into, target->mergeId());
        order.insert(order.begin() + std::ptrdiff_t((std::min)(index, order.size())), pasted);
        ObjectTree::setMergeInputs(into, target->mergeId(), order);
        commitGraphEdit(before, "Move Group");
        return NodeRef(targetScope, pasted);
    }

    void SketchDocument::findObjectProblems(const Network& net, std::vector<NodeId>& path) {
        for (const NodeId id : net.nodeIds()) {
            const jitnodes::Node& node = net.node(id);
            if (const auto* object = dynamic_cast<const ObjectNode*>(&node)) {
                const std::string problem = object->validate();
                if (!problem.empty()) {
                    objectProblems_.push_back({NodeRef(scopeOf(path), id), node.name() + ": " + problem});
                }
            }
            if (const Network* child = node.childNetwork()) {
                if (!node.isAssetInstance()) {
                    path.push_back(id);
                    findObjectProblems(*child, path);
                    path.pop_back();
                }
            }
        }
    }

    void SketchDocument::repairObjectProblems() {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        for (const ObjectProblem& problem : objectProblems_) {
            if (hasNode(problem.object)) {
                if (auto* object = dynamic_cast<ObjectNode*>(&node(problem.object))) {
                    object->repair();
                }
            }
        }
        objectProblems_.clear();
        commitGraphEdit(before, "Repair Objects");
    }

    void SketchDocument::buildSampleLayers() {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        Network& root = network();
        const NodeId rootMerge = ObjectTree::ensureRootMerge(root);
        float shift = 0.0f;
        const auto layer = [&](const char* name, std::initializer_list<const char*> groups) {
            ObjectNode& object = *ObjectTree::objectAt(root, ObjectTree::addObject(root, rootMerge, layerTag, name));
            for (const char* group : groups) {
                ObjectNode& child = *ObjectTree::objectAt(object.network(), ObjectTree::addObject(object.network(), object.mergeId(), groupTag, group));
                const float hue = shift / 5.0f;
                ObjectTree::addCircle(child.network(), child.mergeId(), 150.0f + shift * 60.0f, 200.0f + shift * 50.0f, 90.0f,
                                      newui::Color(0.9f - hue * 0.7f, 0.3f + hue * 0.4f, 0.2f + hue * 0.7f, 0.8f));
                shift += 1.0f;
            }
        };
        layer("Layer1", {"Group1", "Group2"});
        layer("Layer2", {"Group3"});
        layer("Layer3", {"GroupA"});
        commitGraphEdit(before, "Add Sample Layers");
    }

    NodeRef SketchDocument::addCircle(const NodeRef& object, float cx, float cy, float radius, const newui::Color& fill) {
        ObjectNode* owner = hasNode(object) ? dynamic_cast<ObjectNode*>(&node(object)) : nullptr;
        if (owner == nullptr) {
            throw std::runtime_error("a circle goes into a layer or a group");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeId look = ObjectTree::addCircle(owner->network(), owner->mergeId(), cx, cy, radius, fill);
        commitGraphEdit(before, "Add Circle");
        return NodeRef(scopeOfSubnet(object), look);
    }

    jitnodes::GeometryHandle SketchDocument::artwork() {
        NodeId end = network().displayNode();
        if (end == jitnodes::invalidNodeId || !network().hasNode(end)) {
            end = ObjectTree::rootRenderOf(network());
        }
        if (end == jitnodes::invalidNodeId) {
            end = ObjectTree::rootMergeOf(network());
        }
        if (end == jitnodes::invalidNodeId) {
            return nullptr;
        }
        try {
            return network().cook(end);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

}
