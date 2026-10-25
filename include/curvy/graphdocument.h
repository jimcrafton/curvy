#pragma once

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>
#include <newui/models.h>
#include <newui/undostack.h>

#include <memory>
#include <map>
#include <string>

namespace curvy {

    // A newui Document that owns a jitnodes Network and the one undo stack shared by graph and (in derived classes) other edits.
    // It knows nothing of sketch types, so it is testable alone.
    class GraphDocument : public newui::Document {
    public:
        GraphDocument();

        newui::UndoStack& undoStack() { return undoStack_; }
        jitnodes::Network& network() { return *network_; }
        const jitnodes::Network& network() const { return *network_; }

        // Each of these is one undo step.
        jitnodes::NodeId addGraphNode(const std::string& type, const std::string& name = {});
        void removeGraphNode(jitnodes::NodeId id);
        void connectGraphNodes(jitnodes::NodeId from, jitnodes::NodeId to, int inputIndex = 0);
        void setGraphParam(jitnodes::NodeId id, const std::string& name, float value);
        // Several float parameters of a node as one undo step.
        void setGraphFloats(jitnodes::NodeId id, const std::map<std::string, float>& values, const std::string& description);
        void setGraphString(jitnodes::NodeId id, const std::string& name, const std::string& value);
        void setGraphGeometry(jitnodes::NodeId id, const std::string& name, jitnodes::GeometryHandle geometry);
        void renameGraphNode(jitnodes::NodeId id, const std::string& name);
        // Gives a float parameter an expression (empty text clears it, back to its stored value) as one undo step. Returns the parser's message
        // when the expression is not valid, and changes nothing then.
        std::string setGraphExpression(jitnodes::NodeId id, const std::string& name, const std::string& expression);

        // Keys a float parameter at the current time with `value` (making it keyed if it was not); one undo step. Editing a keyed parameter
        // (setGraphParam, setGraphFloatLive) does the same. Removing the last key leaves the parameter a plain number at the value it had.
        void setGraphKey(jitnodes::NodeId id, const std::string& name, float value);
        void removeGraphKey(jitnodes::NodeId id, const std::string& name);
        bool hasKeyAtTime(jitnodes::NodeId id, const std::string& name) const;

        // The time (seconds) expressions and keys are evaluated at. Moving it is not an undo step; everything that depends on it follows.
        double time() const { return network_->time(); }
        void setTime(double seconds);

        // For a drag: edit the network directly, then record the finished edit from the snapshot taken first.
        jitnodes::GraphSnapshot snapshotGraph() const { return jitnodes::snapshotNetwork(*network_); }
        void commitGraphEdit(const jitnodes::GraphSnapshot& before, const std::string& description);
        // One step of such a drag: sets the parameter and lets everything that depends on the graph follow, without an undo step.
        void setGraphFloatLive(jitnodes::NodeId id, const std::string& name, float value);

        // Makes the graph match `target` with no undo step, for derived documents that fold graph changes into their own steps.
        void restoreGraph(const jitnodes::GraphSnapshot& target);

        jitnodes::GeometryHandle cook(jitnodes::NodeId id) { return network_->cook(id); }

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
        // One thread: documents are created freely (clipboard, symbols) and live shapes are small. Held by pointer so documents stay movable.
        std::unique_ptr<jitnodes::Network> network_ = std::make_unique<jitnodes::Network>(1);
    };

}
