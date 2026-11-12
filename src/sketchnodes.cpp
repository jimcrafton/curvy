#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"
#include "curvy/rendernode.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace curvy {

    using jitnodes::Network;
    using jitnodes::NodeId;

    // Where each node of `net` is used: (consumer, input index) pairs per node.
    class NodeUsers {
    public:
        explicit NodeUsers(const Network& net) {
            for (const NodeId id : net.nodeIds()) {
                for (int i = 0; i < net.node(id).inputCount(); ++i) {
                    const NodeId from = net.inputOf(id, i);
                    if (from != jitnodes::invalidNodeId) {
                        users_[from].emplace_back(id, i);
                    }
                }
            }
        }

        const std::vector<std::pair<NodeId, int>>& of(NodeId id) const {
            static const std::vector<std::pair<NodeId, int>> none;
            const auto found = users_.find(id);
            return found == users_.end() ? none : found->second;
        }

        // The nodes that hold a layer, a group or the document together.
        static bool isStructural(const jitnodes::Node& node) {
            return node.hasTag(objectXformTag) || node.hasTag(objectMergeTag) || node.hasTag(rootMergeTag) || node.hasTag(rootRenderTag);
        }

    private:
        std::map<NodeId, std::vector<std::pair<NodeId, int>>> users_;
    };

    std::string SketchDocument::refuseGraphEdit(const NodeRef& ref, bool removing) const {
        const jitnodes::Node& target = node(ref);
        if (removing) {
            return NodeUsers::isStructural(target)
                ? "a layer's or group's own transform and merge, and the document's merge and render, cannot be deleted" : std::string();
        }
        return target.hasTag(objectXformTag) || target.hasTag(rootRenderTag)
            ? "the input of a layer's or group's transform and of the document's render is fixed" : std::string();
    }

    std::string SketchDocument::refuseTagEdit(const NodeRef& ref, const std::vector<std::string>& tags) const {
        const std::set<std::string> reserved = {objectXformTag, objectMergeTag, layerTag, groupTag, rootMergeTag, rootRenderTag};
        const auto reservedOf = [&](const std::vector<std::string>& all) {
            std::set<std::string> found;
            for (const std::string& tag : all) {
                if (reserved.count(tag) != 0) {
                    found.insert(tag);
                }
            }
            return found;
        };
        return reservedOf(node(ref).tags()) == reservedOf(tags)
            ? std::string() : "the tags layer, group, object:xform, object:merge, document:merge and document:render mark what the document relies on";
    }

    void SketchDocument::setNodeBypassed(const NodeRef& ref, bool bypassed) {
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        if (NodeUsers::isStructural(node(ref))) {
            throw std::runtime_error("a layer's or group's own transform and merge, and the document's merge and render, cannot be bypassed");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        node(ref).setBypassed(bypassed);
        commitGraphEdit(before, bypassed ? "Bypass Node" : "Enable Node");
    }

    void SketchDocument::setNodeFrozen(const NodeRef& ref, bool frozen) {
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        if (frozen) {
            networkOf(ref.scope).freezeNode(ref.id);
        } else {
            node(ref).setFrozenGeometry(nullptr);
        }
        commitGraphEdit(before, frozen ? "Freeze Node" : "Unfreeze Node");
    }

    void SketchDocument::setNodeDisplayed(const NodeRef& ref) {
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        if (ref.scope != 0) {
            throw std::runtime_error("the display node of a layer or group is its own transform; only the document's graph can show another node");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network().setDisplayNode(ref.id);
        commitGraphEdit(before, "Display Node");
    }

    NodeRef SketchDocument::groupNode(const NodeRef& ref) {
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        Network& net = networkOf(ref.scope);
        const NodeUsers users(net);

        // The node and everything that feeds it become the group's contents.
        std::vector<NodeId> members;
        std::set<NodeId> seen;
        std::vector<NodeId> pending{ref.id};
        while (!pending.empty()) {
            const NodeId id = pending.back();
            pending.pop_back();
            if (!seen.insert(id).second) {
                continue;
            }
            members.push_back(id);
            for (int i = 0; i < net.node(id).inputCount(); ++i) {
                const NodeId from = net.inputOf(id, i);
                if (from != jitnodes::invalidNodeId) {
                    pending.push_back(from);
                }
            }
        }
        for (const NodeId id : members) {
            const jitnodes::Node& member = net.node(id);
            if (NodeUsers::isStructural(member) || dynamic_cast<const ObjectNode*>(&member) != nullptr) {
                throw std::runtime_error("layers, groups and their own transform and merge cannot be put in a group this way");
            }
            if (id != ref.id) {
                for (const auto& [consumer, index] : users.of(id)) {
                    if (seen.count(consumer) == 0) {
                        throw std::runtime_error("'" + member.name() + "' also feeds a node outside the group");
                    }
                    (void)index;
                }
            }
        }

        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeId group = net.addNode(objectNodeType, net.uniqueName("Group1"));
        net.node(group).addTag(groupTag);
        auto& object = static_cast<ObjectNode&>(net.node(group));

        std::vector<jitnodes::NodeClip> clips;
        for (const NodeId id : members) {
            clips.push_back(jitnodes::copyNode(net, id));
        }
        const std::vector<NodeId> pasted = jitnodes::pasteNodes(object.network(), clips);
        const auto at = std::find(members.begin(), members.end(), ref.id);
        const NodeId pastedTarget = pasted[std::size_t(at - members.begin())];
        object.network().connect(pastedTarget, object.mergeId(), 0);

        const std::vector<std::pair<NodeId, int>> consumers = users.of(ref.id);
        const bool wasDisplay = net.displayNode() == ref.id;
        for (const NodeId id : members) {
            net.removeNode(id);
        }
        for (const auto& [consumer, index] : consumers) {
            if (seen.count(consumer) == 0) {
                net.connect(group, consumer, index);
            }
        }
        if (wasDisplay) {
            net.setDisplayNode(group);
        }
        commitGraphEdit(before, "Group");
        return NodeRef(ref.scope, group);
    }

    std::vector<NodeRef> SketchDocument::ungroup(const NodeRef& ref) {
        if (!hasNode(ref) || !node(ref).hasTag(groupTag)) {
            throw std::runtime_error("only a group can be ungrouped");
        }
        auto& object = static_cast<ObjectNode&>(node(ref));
        Network& inner = object.network();
        Network& net = networkOf(ref.scope);

        const jitnodes::Node& xform = inner.node(object.xformId());
        const char* moves[] = {"sx", "sy"};
        bool plain = true;
        for (const char* name : {"tx", "ty", "rot", "skew"}) {
            plain = plain && xform.getFloat(name) == 0.0f;
        }
        for (const char* name : moves) {
            plain = plain && xform.getFloat(name) == 1.0f;
        }
        if (!plain) {
            throw std::runtime_error("the group has a transform; reset it first so the contents keep their place");
        }

        const std::vector<NodeId> outputs = ObjectTree::mergeInputs(inner, object.mergeId());
        const std::vector<std::pair<NodeId, int>> consumers = NodeUsers(net).of(ref.id);
        for (const auto& [consumer, index] : consumers) {
            (void)index;
            if (outputs.size() > 1 && net.node(consumer).typeName() != "merge") {
                throw std::runtime_error("the group holds several things and what it feeds takes only one");
            }
        }

        const jitnodes::GraphSnapshot before = snapshotGraph();
        std::vector<NodeId> contents;
        for (const NodeId id : inner.nodeIds()) {
            if (id != object.xformId() && id != object.mergeId()) {
                contents.push_back(id);
            }
        }
        std::vector<jitnodes::NodeClip> clips;
        for (const NodeId id : contents) {
            clips.push_back(jitnodes::copyNode(inner, id));
        }
        const std::vector<NodeId> pasted = jitnodes::pasteNodes(net, clips);
        const auto mapped = [&](NodeId old) {
            return pasted[std::size_t(std::find(contents.begin(), contents.end(), old) - contents.begin())];
        };
        std::vector<NodeId> replacement;
        for (const NodeId out : outputs) {
            replacement.push_back(mapped(out));
        }

        for (const auto& [consumer, index] : consumers) {
            if (net.node(consumer).typeName() == "merge") {
                std::vector<NodeId> order;
                for (int i = 0; i < net.node(consumer).inputCount(); ++i) {
                    const NodeId from = net.inputOf(consumer, i);
                    if (from == ref.id) {
                        order.insert(order.end(), replacement.begin(), replacement.end());
                    } else if (from != jitnodes::invalidNodeId) {
                        order.push_back(from);
                    }
                }
                ObjectTree::setMergeInputs(net, consumer, order);
            } else if (!replacement.empty()) {
                net.connect(replacement.front(), consumer, index);
            }
        }
        if (net.displayNode() == ref.id && !replacement.empty()) {
            net.setDisplayNode(replacement.front());
        }
        net.removeNode(ref.id);
        commitGraphEdit(before, "Ungroup");

        std::vector<NodeRef> result;
        for (const NodeId id : replacement) {
            result.emplace_back(ref.scope, id);
        }
        return result;
    }

}
