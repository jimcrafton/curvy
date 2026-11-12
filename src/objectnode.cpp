#include "curvy/objectnode.h"

#include "curvy/appearancenode.h"
#include "curvy/rendernode.h"

#include <jitnodes/graph/network.h>

#include <memory>
#include <mutex>

namespace curvy {

    using jitnodes::NodeId;

    ObjectNode::ObjectNode() : SubnetNode(objectNodeType, 0) {
        declareFloat("locked", 0.0f);
        repair();
    }

    NodeId ObjectNode::findByTag(const char* tag, int& count) const {
        NodeId found = jitnodes::invalidNodeId;
        count = 0;
        for (const NodeId id : network().nodeIds()) {
            if (network().node(id).hasTag(tag)) {
                if (count == 0) {
                    found = id;
                }
                ++count;
            }
        }
        return found;
    }

    NodeId ObjectNode::xformId() const {
        int count = 0;
        return findByTag(objectXformTag, count);
    }

    NodeId ObjectNode::mergeId() const {
        int count = 0;
        return findByTag(objectMergeTag, count);
    }

    std::string ObjectNode::validate() const {
        int xforms = 0;
        int merges = 0;
        const NodeId xform = findByTag(objectXformTag, xforms);
        const NodeId merge = findByTag(objectMergeTag, merges);
        if (xforms != 1) {
            return xforms == 0 ? "the object has no xform" : "the object has more than one xform";
        }
        if (merges != 1) {
            return merges == 0 ? "the object has no merge" : "the object has more than one merge";
        }
        if (network().displayNode() != xform) {
            return "the xform is not the display node";
        }
        if (network().inputOf(xform, 0) != merge) {
            return "the merge does not feed the xform";
        }
        return {};
    }

    bool ObjectNode::repair() {
        if (validate().empty()) {
            return false;
        }
        jitnodes::Network& net = network();
        int count = 0;
        NodeId xform = findByTag(objectXformTag, count);
        if (xform == jitnodes::invalidNodeId) {
            xform = net.addNode("transform", "xform");
            net.node(xform).addTag(objectXformTag);
        }
        NodeId merge = findByTag(objectMergeTag, count);
        if (merge == jitnodes::invalidNodeId) {
            merge = net.addNode("merge", "merge");
            net.node(merge).addTag(objectMergeTag);
        }
        net.connect(merge, xform);
        net.setDisplayNode(xform);
        return true;
    }

    ObjectNode* ObjectTree::objectAt(jitnodes::Network& network, NodeId id) {
        return network.hasNode(id) ? dynamic_cast<ObjectNode*>(&network.node(id)) : nullptr;
    }

    std::vector<NodeId> ObjectTree::mergeInputs(const jitnodes::Network& network, NodeId merge) {
        std::vector<NodeId> inputs;
        const int count = network.node(merge).inputCount();
        for (int i = 0; i < count; ++i) {
            const NodeId from = network.inputOf(merge, i);
            if (from != jitnodes::invalidNodeId) {
                inputs.push_back(from);
            }
        }
        return inputs;
    }

    void ObjectTree::setMergeInputs(jitnodes::Network& network, NodeId merge, const std::vector<NodeId>& order) {
        for (int i = network.node(merge).inputCount() - 1; i >= 0; --i) {
            if (network.inputOf(merge, i) != jitnodes::invalidNodeId) {
                network.disconnect(merge, i);
            }
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            network.connect(order[i], merge, int(i));
        }
    }

    NodeId ObjectTree::rootMergeOf(const jitnodes::Network& network) {
        for (const NodeId id : network.nodeIds()) {
            if (network.node(id).hasTag(rootMergeTag)) {
                return id;
            }
        }
        return jitnodes::invalidNodeId;
    }

    NodeId ObjectTree::rootRenderOf(const jitnodes::Network& network) {
        for (const NodeId id : network.nodeIds()) {
            if (network.node(id).hasTag(rootRenderTag)) {
                return id;
            }
        }
        return jitnodes::invalidNodeId;
    }

    NodeId ObjectTree::ensureRootMerge(jitnodes::Network& network) {
        NodeId merge = rootMergeOf(network);
        if (merge == jitnodes::invalidNodeId) {
            merge = network.addNode("merge", "layers");
            network.node(merge).addTag(rootMergeTag);
        }
        if (rootRenderOf(network) == jitnodes::invalidNodeId) {
            const NodeId render = network.addNode(renderNodeType, network.uniqueName("render"));
            network.node(render).addTag(rootRenderTag);
            network.connect(merge, render);
            network.setDisplayNode(render);
        }
        return merge;
    }

    NodeId ObjectTree::addObject(jitnodes::Network& network, NodeId merge, const char* role, const std::string& name) {
        const std::string fallback = std::string(role) == layerTag ? "Layer1" : "Group1";
        const NodeId id = network.addNode(objectNodeType, network.uniqueName(name.empty() ? fallback : name));
        network.node(id).addTag(role);
        std::vector<NodeId> order = mergeInputs(network, merge);
        order.push_back(id);
        setMergeInputs(network, merge, order);
        return id;
    }

    NodeId ObjectTree::addCircle(jitnodes::Network& network, NodeId merge, float cx, float cy, float radius, const newui::Color& fill) {
        const NodeId circle = network.addNode("circle", network.uniqueName("circle"));
        jitnodes::Node& circleNode = network.node(circle);
        circleNode.setFloat("count", 96.0f);
        circleNode.setFloat("cx", cx);
        circleNode.setFloat("cy", cy);
        circleNode.setFloat("radius", radius);
        const NodeId look = network.addNode(fillNodeType, network.uniqueName("fill"));
        jitnodes::Node& lookNode = network.node(look);
        lookNode.setFloat("r", fill.r);
        lookNode.setFloat("g", fill.g);
        lookNode.setFloat("b", fill.b);
        lookNode.setFloat("alpha", fill.a);
        network.connect(circle, look);
        std::vector<NodeId> order = mergeInputs(network, merge);
        order.push_back(look);
        setMergeInputs(network, merge, order);
        return look;
    }

    void registerObjectNodes() {
        static std::once_flag once;
        std::call_once(once, [] {
            jitnodes::NodeRegistry::instance().registerType(objectNodeType, [] { return std::make_unique<ObjectNode>(); });
        });
    }

}
