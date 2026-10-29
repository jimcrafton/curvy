#pragma once

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>
#include "curvy/noderef.h"
#include <newui/models.h>
#include <newui/undostack.h>

#include <memory>
#include <map>
#include <string>
#include <vector>

namespace curvy {

    // A newui Document that owns a jitnodes Network and the one undo stack shared by graph and (in derived classes) other edits.
    // It knows nothing of sketch types, so it is testable alone.
    class GraphDocument : public newui::Document {
    public:
        GraphDocument();

        newui::UndoStack& undoStack() { return undoStack_; }
        jitnodes::Network& network() { return *network_; }
        const jitnodes::Network& network() const { return *network_; }

        // Scopes: the networks of the document. Scope 0 is the root; every subnet that holds nodes has one, named by the path of subnet ids from the root.
        // A scope number is never reused, so a NodeRef survives an undo that recreates its subnet under the same id.
        std::uint32_t scopeOf(const std::vector<jitnodes::NodeId>& path);
        std::uint32_t scopeOfSubnet(const NodeRef& subnet) { return scopeOf(childPath(subnet)); }
        std::uint32_t scopeCount() const { return std::uint32_t(scopePaths_.size()); }
        const std::vector<jitnodes::NodeId>& scopePath(std::uint32_t scope) const { return scopePaths_.at(scope); }
        // Null when a subnet on the path is gone.
        jitnodes::Network* findNetwork(std::uint32_t scope);
        const jitnodes::Network* findNetwork(std::uint32_t scope) const;
        jitnodes::Network& networkOf(std::uint32_t scope);
        const jitnodes::Network& networkOf(std::uint32_t scope) const;
        jitnodes::Node& node(const NodeRef& ref) { return networkOf(ref.scope).node(ref.id); }
        const jitnodes::Node& node(const NodeRef& ref) const { return networkOf(ref.scope).node(ref.id); }
        bool hasNode(const NodeRef& ref) const;
        // The scope a subnet node's contents live in.
        NodeRef parentOfScope(std::uint32_t scope) const;

        // Each of these is one undo step. New nodes go into `scope`.
        NodeRef addGraphNode(const std::string& type, const std::string& name = {}, std::uint32_t scope = 0);
        void removeGraphNode(const NodeRef& ref);
        // Both ends are in one scope.
        void connectGraphNodes(const NodeRef& from, const NodeRef& to, int inputIndex = 0);
        void setGraphParam(const NodeRef& ref, const std::string& name, float value);
        // Several float parameters of a node as one undo step.
        void setGraphFloats(const NodeRef& ref, const std::map<std::string, float>& values, const std::string& description);
        void setGraphString(const NodeRef& ref, const std::string& name, const std::string& value);
        void setGraphGeometry(const NodeRef& ref, const std::string& name, jitnodes::GeometryHandle geometry);
        void renameGraphNode(const NodeRef& ref, const std::string& name);
        // Gives a float parameter an expression (empty text clears it, back to its stored value) as one undo step. Returns the parser's message
        // when the expression is not valid, and changes nothing then.
        std::string setGraphExpression(const NodeRef& ref, const std::string& name, const std::string& expression);

        // Keys a float parameter at the current time with `value` (making it keyed if it was not); one undo step. Editing a keyed parameter
        // (setGraphParam, setGraphFloatLive) does the same. Removing the last key leaves the parameter a plain number at the value it had.
        void setGraphKey(const NodeRef& ref, const std::string& name, float value);
        void removeGraphKey(const NodeRef& ref, const std::string& name);
        bool hasKeyAtTime(const NodeRef& ref, const std::string& name) const;

        // Root-scope shortcuts for code that only deals with the document's top graph; code that can meet a node inside a subnet uses the NodeRef forms.
        void removeGraphNode(jitnodes::NodeId id) { removeGraphNode(NodeRef(id)); }
        void connectGraphNodes(jitnodes::NodeId from, jitnodes::NodeId to, int inputIndex = 0) { connectGraphNodes(NodeRef(from), NodeRef(to), inputIndex); }
        void setGraphParam(jitnodes::NodeId id, const std::string& name, float value) { setGraphParam(NodeRef(id), name, value); }
        void setGraphFloats(jitnodes::NodeId id, const std::map<std::string, float>& values, const std::string& description) { setGraphFloats(NodeRef(id), values, description); }
        void setGraphString(jitnodes::NodeId id, const std::string& name, const std::string& value) { setGraphString(NodeRef(id), name, value); }
        void setGraphGeometry(jitnodes::NodeId id, const std::string& name, jitnodes::GeometryHandle geometry) { setGraphGeometry(NodeRef(id), name, std::move(geometry)); }
        void renameGraphNode(jitnodes::NodeId id, const std::string& name) { renameGraphNode(NodeRef(id), name); }
        std::string setGraphExpression(jitnodes::NodeId id, const std::string& name, const std::string& expression) { return setGraphExpression(NodeRef(id), name, expression); }
        void setGraphKey(jitnodes::NodeId id, const std::string& name, float value) { setGraphKey(NodeRef(id), name, value); }
        void removeGraphKey(jitnodes::NodeId id, const std::string& name) { removeGraphKey(NodeRef(id), name); }
        bool hasKeyAtTime(jitnodes::NodeId id, const std::string& name) const { return hasKeyAtTime(NodeRef(id), name); }
        void setGraphFloatLive(jitnodes::NodeId id, const std::string& name, float value) { setGraphFloatLive(NodeRef(id), name, value); }
        jitnodes::GeometryHandle cook(jitnodes::NodeId id) { return cook(NodeRef(id)); }

        // The time (seconds) expressions and keys are evaluated at. Moving it is not an undo step; everything that depends on it follows.
        double time() const { return network_->time(); }
        void setTime(double seconds);

        // For a drag: edit the network directly, then record the finished edit from the snapshot taken first.
        jitnodes::GraphSnapshot snapshotGraph() const { return jitnodes::snapshotNetwork(*network_); }
        void commitGraphEdit(const jitnodes::GraphSnapshot& before, const std::string& description);
        // One step of such a drag: sets the parameter and lets everything that depends on the graph follow, without an undo step.
        void setGraphFloatLive(const NodeRef& ref, const std::string& name, float value);

        // Makes the graph match `target` with no undo step, for derived documents that fold graph changes into their own steps.
        void restoreGraph(const jitnodes::GraphSnapshot& target);

        jitnodes::GeometryHandle cook(const NodeRef& ref) { return networkOf(ref.scope).cook(ref.id); }

        std::string graphToText() const { return jitnodes::saveNetwork(*network_, true, true); }
        // Leaves the current graph untouched and returns false when the text is not a valid graph.
        bool graphFromText(const std::string& text);
        // Replaces the graph with an empty one without an undo step (a load, New).
        void clearGraph();

    protected:
        // The graph changed (an edit, undo or redo); derived documents refresh what depends on it.
        virtual void graphChanged();
        // The time moved; derived documents refresh what shows time-dependent values. `graphAffected`: some node depends on time.
        virtual void timeChanged(bool graphAffected) { (void)graphAffected; }

        // A document of only a graph is saved as the graph text; derived documents with another format override these.
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

        newui::UndoStack undoStack_;

    private:
        std::vector<jitnodes::NodeId> childPath(const NodeRef& subnet) const;
        std::vector<std::vector<jitnodes::NodeId>> scopePaths_{std::vector<jitnodes::NodeId>{}};
        std::map<std::vector<jitnodes::NodeId>, std::uint32_t> scopeIndex_{{std::vector<jitnodes::NodeId>{}, 0}};

        // One thread: documents are created freely (clipboard, symbols) and live shapes are small. Held by pointer so documents stay movable.
        std::unique_ptr<jitnodes::Network> network_ = std::make_unique<jitnodes::Network>(1);
    };

}
