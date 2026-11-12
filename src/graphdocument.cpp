#include "curvy/graphdocument.h"

#include "curvy/appearancenode.h"
#include "curvy/objectnode.h"
#include "curvy/rendernode.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace curvy {

    GraphDocument::GraphDocument() {
        registerObjectNodes();
        registerAppearanceNodes();
        registerRenderNode();
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
        if (!path.empty()) {
            scopeOf(std::vector<jitnodes::NodeId>(path.begin(), path.end() - 1));   // parentOfScope needs the enclosing scope to exist
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
        jitnodes::Network* net = const_cast<jitnodes::Network*>(static_cast<const GraphDocument*>(this)->findNetwork(scope));
        // A subnet normally takes its time from its node's cook, which an object's own cook inside the subnet (a layer or group) never goes
        // through. The parent is not told: the root already marks the subnet nodes that depend on the time when its own time moves.
        if (net != nullptr && net != network_.get() && (net->time() != network_->time() || net->fps() != network_->fps())) {
            const bool notify = net->setPropagateToParent(false);
            net->setFps(network_->fps());
            net->setTime(network_->time());
            net->setPropagateToParent(notify);
        }
        return net;
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

    jitnodes::GraphSnapshot GraphDocument::snapshotGraph() const {
        jitnodes::GraphSnapshot snapshot = jitnodes::snapshotNetwork(*network_, lastSnapshot_.get());
        lastSnapshot_ = std::make_shared<const jitnodes::GraphSnapshot>(snapshot);
        return snapshot;
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
            if (node.keyframes(name).empty()) {
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

    void GraphDocument::setGraphPositionsLive(std::uint32_t scope, const std::map<jitnodes::NodeId, std::pair<float, float>>& positions) {
        jitnodes::Network& net = networkOf(scope);
        for (const auto& [id, at] : positions) {
            net.node(id).setPosition(at.first, at.second);
        }
        updateAllViews();
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
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        if (const std::string why = refuseGraphEdit(ref, true); !why.empty()) {
            throw std::runtime_error(why);
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        networkOf(ref.scope).removeNode(ref.id);
        commitGraphEdit(before, "Delete Node");
    }

    void GraphDocument::removeGraphNodes(const std::vector<NodeRef>& refs) {
        for (const NodeRef& ref : refs) {
            if (!hasNode(ref)) {
                throw std::runtime_error("no such node");
            }
            if (const std::string why = refuseGraphEdit(ref, true); !why.empty()) {
                throw std::runtime_error(why);
            }
        }
        if (refs.empty()) {
            return;
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        for (const NodeRef& ref : refs) {
            if (networkOf(ref.scope).hasNode(ref.id)) {   // removing a subnet takes its contents with it
                networkOf(ref.scope).removeNode(ref.id);
            }
        }
        commitGraphEdit(before, refs.size() == 1 ? "Delete Node" : "Delete Nodes");
    }

    void GraphDocument::moveGraphWire(const NodeRef& oldTo, int oldIndex, const NodeRef& newTo, int newIndex) {
        if (oldTo.scope != newTo.scope) {
            throw std::runtime_error("nodes can only be connected inside one subnet");
        }
        if (!hasNode(oldTo) || !hasNode(newTo) || oldIndex < 0 || oldIndex >= node(oldTo).inputCount() || newIndex < 0) {
            throw std::runtime_error("no such input");
        }
        for (const NodeRef& ref : {oldTo, newTo}) {
            if (const std::string why = refuseGraphEdit(ref, false); !why.empty()) {
                throw std::runtime_error(why);
            }
        }
        jitnodes::Network& net = networkOf(oldTo.scope);
        const jitnodes::NodeId from = net.inputOf(oldTo.id, oldIndex);
        if (from == jitnodes::invalidNodeId) {
            throw std::runtime_error("that input is not wired");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        try {
            net.disconnect(oldTo.id, oldIndex);
            net.connect(from, newTo.id, newIndex);
        } catch (...) {
            restoreGraph(before);   // a cycle or a bad input: leave the graph as it was
            throw;
        }
        commitGraphEdit(before, "Move Wire");
    }

    void GraphDocument::disconnectGraphNode(const NodeRef& to, int inputIndex) {
        if (!hasNode(to) || inputIndex < 0 || inputIndex >= node(to).inputCount()) {
            throw std::runtime_error("no such input");
        }
        if (const std::string why = refuseGraphEdit(to, false); !why.empty()) {
            throw std::runtime_error(why);
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        networkOf(to.scope).disconnect(to.id, inputIndex);
        commitGraphEdit(before, "Disconnect Nodes");
    }

    void GraphDocument::connectGraphNodes(const NodeRef& from, const NodeRef& to, int inputIndex) {
        if (from.scope != to.scope) {
            throw std::runtime_error("nodes can only be connected inside one subnet");
        }
        if (!hasNode(from) || !hasNode(to)) {
            throw std::runtime_error("no such node");
        }
        if (const std::string why = refuseGraphEdit(to, false); !why.empty()) {
            throw std::runtime_error(why);
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

    void GraphDocument::setGraphTags(const NodeRef& ref, const std::vector<std::string>& tags) {
        if (!hasNode(ref)) {
            throw std::runtime_error("no such node");
        }
        if (node(ref).tags() == tags) {
            return;
        }
        if (const std::string why = refuseTagEdit(ref, tags); !why.empty()) {
            throw std::runtime_error(why);
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        node(ref).setTags(tags);
        networkOf(ref.scope).markDirty(ref.id);
        commitGraphEdit(before, "Change Tags");
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
