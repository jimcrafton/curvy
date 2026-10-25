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
        void writeFloat(jitnodes::Network& network, jitnodes::NodeId id, const std::string& name, float value) {
            jitnodes::Node& node = network.node(id);
            if (node.keyframes(name).empty()) {
                node.setFloat(name, value);
            } else {
                putKey(node, name, network.time(), value);
            }
            network.markDirty(id);
        }
    }

    void GraphDocument::setGraphFloatLive(jitnodes::NodeId id, const std::string& name, float value) {
        writeFloat(*network_, id, name, value);
        graphChanged();
    }

    void GraphDocument::setGraphKey(jitnodes::NodeId id, const std::string& name, float value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        putKey(network_->node(id), name, network_->time(), value);
        network_->markDirty(id);
        commitGraphEdit(before, "Set Key " + name);
    }

    void GraphDocument::removeGraphKey(jitnodes::NodeId id, const std::string& name) {
        jitnodes::Node& node = network_->node(id);
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
        network_->markDirty(id);
        commitGraphEdit(before, "Remove Key " + name);
    }

    bool GraphDocument::hasKeyAtTime(jitnodes::NodeId id, const std::string& name) const {
        for (const jitnodes::Keyframe& key : network_->node(id).keyframes(name)) {
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

    jitnodes::NodeId GraphDocument::addGraphNode(const std::string& type, const std::string& name) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const jitnodes::NodeId id = network_->addNode(type, name);
        commitGraphEdit(before, "Add " + type);
        return id;
    }

    void GraphDocument::removeGraphNode(jitnodes::NodeId id) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network_->removeNode(id);
        commitGraphEdit(before, "Delete Node");
    }

    void GraphDocument::connectGraphNodes(jitnodes::NodeId from, jitnodes::NodeId to, int inputIndex) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network_->connect(from, to, inputIndex);
        commitGraphEdit(before, "Connect Nodes");
    }

    void GraphDocument::setGraphParam(jitnodes::NodeId id, const std::string& name, float value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        writeFloat(*network_, id, name, value);
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::setGraphFloats(jitnodes::NodeId id, const std::map<std::string, float>& values, const std::string& description) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        for (const auto& [name, value] : values) {
            network_->node(id).setFloat(name, value);
        }
        commitGraphEdit(before, description);
    }

    std::string GraphDocument::setGraphExpression(jitnodes::NodeId id, const std::string& name, const std::string& expression) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        try {
            network_->node(id).setExpression(name, expression);
        } catch (const std::exception& error) {
            return error.what();
        }
        network_->markDirty(id);
        commitGraphEdit(before, expression.empty() ? "Clear Expression" : "Set Expression");
        return {};
    }

    void GraphDocument::setTime(double seconds) {
        if (seconds == network_->time()) {
            return;
        }
        timeChanged(network_->setTime(seconds));
    }

    void GraphDocument::setGraphString(jitnodes::NodeId id, const std::string& name, const std::string& value) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network_->node(id).setString(name, value);
        network_->markDirty(id);
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::setGraphGeometry(jitnodes::NodeId id, const std::string& name, jitnodes::GeometryHandle geometry) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network_->node(id).setGeometryParam(name, std::move(geometry));
        commitGraphEdit(before, "Change " + name);
    }

    void GraphDocument::renameGraphNode(jitnodes::NodeId id, const std::string& name) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        network_->renameNode(id, name);
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
