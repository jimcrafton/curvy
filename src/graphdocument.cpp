#include "curvy/graphdocument.h"

#include "curvy/pathnode.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace curvy {

    GraphDocument::GraphDocument() {
        registerCurvyNodes();
    }

    void GraphDocument::graphChanged() {
        markModified();
        updateAllViews();
    }

    std::vector<jitnodes::NodeId> GraphDocument::childPath(const NodeRef& subnet) const {
        std::vector<jitnodes::NodeId> path = scopePaths_.at(subnet.scope);
        path.push_back(subnet.id);
        return path;
    }

    std::uint32_t GraphDocument::scopeOf(const std::vector<jitnodes::NodeId>& path) {
        const auto found = scopeIndex_.find(path);
        if (found != scopeIndex_.end()) {
            return found->second;
        }
        const std::uint32_t scope = static_cast<std::uint32_t>(scopePaths_.size());
        scopePaths_.push_back(path);
        scopeIndex_.emplace(path, scope);
        return scope;
    }

    const jitnodes::Network* GraphDocument::findNetwork(std::uint32_t scope) const {
        if (scope >= scopePaths_.size()) {
            return nullptr;
        }
        const jitnodes::Network* net = network_.get();
        for (jitnodes::NodeId id : scopePaths_[scope]) {
            if (!net->hasNode(id)) {
                return nullptr;
            }
            net = net->node(id).childNetwork();
            if (net == nullptr) {
                return nullptr;
            }
        }
        return net;
    }

    jitnodes::Network* GraphDocument::findNetwork(std::uint32_t scope) {
        return const_cast<jitnodes::Network*>(static_cast<const GraphDocument*>(this)->findNetwork(scope));
    }

    jitnodes::Network& GraphDocument::networkOf(std::uint32_t scope) {
        jitnodes::Network* net = findNetwork(scope);
        if (net == nullptr) {
            throw std::runtime_error("the subnet of scope " + std::to_string(scope) + " does not exist");
        }
        return *net;
    }

    const jitnodes::Network& GraphDocument::networkOf(std::uint32_t scope) const {
        const jitnodes::Network* net = findNetwork(scope);
        if (net == nullptr) {
            throw std::runtime_error("the subnet of scope " + std::to_string(scope) + " does not exist");
        }
        return *net;
    }

    bool GraphDocument::hasNode(const NodeRef& ref) const {
        const jitnodes::Network* net = findNetwork(ref.scope);
        return net != nullptr && ref.valid() && net->hasNode(ref.id);
    }

    NodeRef GraphDocument::parentOfScope(std::uint32_t scope) const {
        const std::vector<jitnodes::NodeId>& path = scopePaths_.at(scope);
        if (path.empty()) {
            return kNoNode;
        }
        const std::vector<jitnodes::NodeId> up(path.begin(), path.end() - 1);
        return NodeRef(scopeIndex_.at(up), path.back());
    }

    void GraphDocument::commitGraphEdit(const jitnodes::GraphSnapshot& before, const std::string& description) {
        jitnodes::GraphSnapshot after = jitnodes::snapshotNetwork(*network_, &before);
        if (jitnodes::sameGraph(before, after)) {
            return;
        }
        auto b = std::make_shared<const jitnodes::GraphSnapshot>(before);
        auto a = std::make_shared<const jitnodes::GraphSnapshot>(std::move(after));
        auto applied = std::make_shared<bool>(true);   // push() runs doIt at once, and the edit is already in the network
        undoStack_.push({description,
            [this, a, b, applied]() {
                if (*applied) {
                    *applied = false;
                } else {
                    jitnodes::restoreSnapshot(*network_, *a, b.get());
                }
                graphChanged();
            },
            [this, a, b]() { jitnodes::restoreSnapshot(*network_, *b, a.get()); graphChanged(); }});
    }

    namespace {
        constexpr double kKeyTolerance = 1e-6;

        void putKey(jitnodes::Node& node, const std::string& name, double time, float value) {
            std::vector<jitnodes::Keyframe> keys = node.keyframes(name);
            for (jitnodes::Keyframe& key : keys) {
                if (std::abs(key.time - time) < kKeyTolerance) {
                    key.value = value;
                    node.setKeyframes(name, std::move(keys));
                    return;
                }
            }
            keys.push_back({time, value, jitnodes::KeyInterp::linear});
            node.setKeyframes(name, std::move(keys));
        }

        // A keyed parameter takes an edit as a key at the current time, any other as its stored value.
        void writeFloat(jitnodes::Network& network, jitnodes::NodeId id, const std::string& name, float value, double time) {
            jitnodes::Node& node = network.node(id);
            if (pivotShownInParent(node, name)) {
                // The pivot is where the node turns and scales about, not where the shape sits: moving it leaves the shape where it is.
                setPivotInParent(node, name, value);
            } else if (node.keyframes(name).empty()) {
                node.setFloat(name, value);
            } else {
                putKey(node, name, time, value);
            }
            network.markDirty(id);
        }
    }

    void GraphDocument::setGraphFloatLive(const NodeRef& ref, const std::string& name, float value) {
        writeFloat(networkOf(ref.scope), ref.id, name, value, network_->time());
        graphChanged();
    }

    void GraphDocument::setGraphKey(const NodeRef& ref, const std::string& name, float value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        jitnodes::Network& net = networkOf(ref.scope);
        putKey(net.node(ref.id), name, network_->time(), value);
        net.markDirty(ref.id);
        commitGraphEdit(before, "Set Key " + name);
    }

    void GraphDocument::removeGraphKey(const NodeRef& ref, const std::string& name) {
        jitnodes::Network& net = networkOf(ref.scope);
        jitnodes::Node& node = net.node(ref.id);
        std::vector<jitnodes::Keyframe> keys = node.keyframes(name);
        const double time = network_->time();
        const auto at = std::find_if(keys.begin(), keys.end(), [&](const jitnodes::Keyframe& key) { return std::abs(key.time - time) < kKeyTolerance; });
        if (at == keys.end()) {
            return;
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        keys.erase(at);
        if (keys.empty()) {
            // The last key goes: the parameter keeps the value it had, as a plain number.
            const float held = jitnodes::evaluateKeyframes(node.keyframes(name), time);
            node.clearAnimation(name);
            node.setFloat(name, held);
        } else {
            node.setKeyframes(name, std::move(keys));
        }
        net.markDirty(ref.id);
        commitGraphEdit(before, "Remove Key " + name);
    }

    bool GraphDocument::hasKeyAtTime(const NodeRef& ref, const std::string& name) const {
        for (const jitnodes::Keyframe& key : node(ref).keyframes(name)) {
            if (std::abs(key.time - network_->time()) < kKeyTolerance) {
                return true;
            }
        }
        return false;
    }

    void GraphDocument::restoreGraph(const jitnodes::GraphSnapshot& target) {
        jitnodes::restoreSnapshot(*network_, target);
        graphChanged();
    }

    NodeRef GraphDocument::addGraphNode(const std::string& type, const std::string& name, std::uint32_t scope) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeRef ref(scope, networkOf(scope).addNode(type, name));
        commitGraphEdit(before, "Add " + type);
        return ref;
    }

    void GraphDocument::removeGraphNode(const NodeRef& ref) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        networkOf(ref.scope).removeNode(ref.id);
        commitGraphEdit(before, "Delete Node");
    }

    void GraphDocument::connectGraphNodes(const NodeRef& from, const NodeRef& to, int inputIndex) {
        if (from.scope != to.scope) {
            throw std::runtime_error("nodes can only be connected inside one subnet");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        networkOf(to.scope).connect(from.id, to.id, inputIndex);
        commitGraphEdit(before, "Connect Nodes");
    }

    void GraphDocument::setGraphParam(const NodeRef& ref, const std::string& name, float value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        writeFloat(networkOf(ref.scope), ref.id, name, value, network_->time());
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::setGraphFloats(const NodeRef& ref, const std::map<std::string, float>& values, const std::string& description) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        for (const auto& [name, value] : values) {
            node(ref).setFloat(name, value);
        }
        commitGraphEdit(before, description);
    }

    std::string GraphDocument::setGraphExpression(const NodeRef& ref, const std::string& name, const std::string& expression) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        try {
            node(ref).setExpression(name, expression);
        } catch (const std::exception& error) {
            return error.what();
        }
        networkOf(ref.scope).markDirty(ref.id);
        commitGraphEdit(before, expression.empty() ? "Clear Expression" : "Set Expression");
        return {};
    }

    void GraphDocument::setTime(double seconds) {
        if (seconds == network_->time()) {
            return;
        }
        timeChanged(network_->setTime(seconds));
    }

    void GraphDocument::setGraphString(const NodeRef& ref, const std::string& name, const std::string& value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        node(ref).setString(name, value);
        networkOf(ref.scope).markDirty(ref.id);
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::setGraphGeometry(const NodeRef& ref, const std::string& name, jitnodes::GeometryHandle geometry) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        node(ref).setGeometryParam(name, std::move(geometry));
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::renameGraphNode(const NodeRef& ref, const std::string& name) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        networkOf(ref.scope).renameNode(ref.id, name);
        commitGraphEdit(before, "Rename Node");
    }

    bool GraphDocument::graphFromText(const std::string& text) {
        try {
            jitnodes::loadNetwork(*network_, text);
        } catch (const std::exception&) {
            return false;
        }
        graphChanged();
        return true;
    }

    bool GraphDocument::readFromFile(const std::string& path) {
        try {
            jitnodes::loadNetworkFromFile(*network_, path);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    bool GraphDocument::writeToFile(const std::string& path) {
        try {
            jitnodes::saveNetworkToFile(*network_, path);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    void GraphDocument::clearGraph() {
        network_->clear();
    }

}
