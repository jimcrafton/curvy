#include "curvy/graphdocument.h"

#include "curvy/pathnode.h"

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
        network_->node(id).setFloat(name, value);
        network_->markDirty(id);
        commitGraphEdit(before, "Change " + name);
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
