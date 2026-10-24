#pragma once

#include "curvy/canvasview.h"
#include "curvy/dockcontroller.h"
#include "curvy/graphtree.h"
#include "curvy/sketchdocument.h"
#include "curvy/workspacemodel.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/frame.h>

#include <newui/segmentedcontrol.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace curvy {

    // The graph dock (docs/ui-design.md "Graph tree"; layout from docs/mockups/graphDock.html -> Resources/graphDock.newui): the node graph as a
    // tree, docked to the right edge next to the property dock and off by default. Two views of the one graph:
    //   Document - the layers, groups and objects of the document, each object being the node that holds its curves
    //   Flow     - the nodes nobody uses, each with the nodes that feed it below it
    // Each row has an icon for its kind of node and, for a solid fill or stroke, a colour chip. Clicking a row selects the object on the
    // canvas and the box under the tree shows that node's parameters: the words, font and size of a text, the numbers of a live shape, the
    // curves of a path (as "N anchors, closed, W x H" with Edit, Copy and Paste of the path data). Every edit is one undo step through
    // GraphDocument. A filter field keeps the rows whose name or summary matches.
    //
    // A newui::Controller like the other docks: it observes the model, the document and the views, and keeps no state of its own besides
    // what the view needs (the open rows, the filter, the selected row).
    // @reflect ignore=true
    class GraphDock : public newui::Controller {
    public:
        enum class Mode { Document, Flow };

        GraphDock(newui::Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document, DockController& docks);
        ~GraphDock() override = default;
        GraphDock(const GraphDock&) = delete;
        GraphDock& operator=(const GraphDock&) = delete;

        // Shown or hidden as the model says, and placed beside the property dock.
        void sync();
        void restyle();
        // Rebuilds the rows when the document, the selection, the filter or the view changed since the last time.
        void refresh();

        Mode mode() const { return mode_; }
        void setMode(Mode mode);
        void setFilter(const std::string& filter);

        newui::SubView* dock() const { return dock_; }
        newui::TreeView* tree() const { return tree_; }
        // The rows as shown (after the filter).
        const std::vector<GraphRow>& rows() const;
        // The row of a document object in the tree; selects it and shows its node. False when it is not listed (filtered out, or in the
        // other view).
        bool selectObject(std::size_t objectId);
        jitnodes::NodeId detailNode() const { return detailNode_; }
        // The parameter box, for tests: the text of row `index` (caption and value) and whether it is shown.
        std::string detailCaption(std::size_t index) const;
        std::string detailValue(std::size_t index) const;
        bool detailShown(std::size_t index) const;
        // Types into row `index` as if the user had, and applies it (what Return does).
        void setDetailValue(std::size_t index, const std::string& text);
        std::string title() const;

        static constexpr std::size_t kDetailRows = 6;

    private:
        struct DetailRow {
            newui::Label* caption = nullptr;
            newui::TextField* field = nullptr;
            std::function<void(const std::string&)> apply;
        };
        void layout();
        void buildDetail();
        void layoutDetail();
        void selectionChangedInTree();
        void showDetail(const GraphRow* row);
        void setRow(std::size_t index, const std::string& caption, const std::string& value, std::function<void(const std::string&)> apply);
        void clearDetail();
        std::vector<GraphRow> buildRows() const;
        std::vector<GraphRow> documentRows() const;
        std::vector<GraphRow> flowRowsFromGraph() const;
        void describe(GraphRow& row, jitnodes::NodeId node) const;
        std::size_t signature() const;
        std::size_t objectOfNode(jitnodes::NodeId node) const;
        void expandAll();

        newui::Frame& frame_;
        CanvasView& canvas_;
        WorkspaceModel& model_;
        SketchDocument& document_;
        DockController& docks_;
        newui::SubView* dock_ = nullptr;
        newui::TreeView* tree_ = nullptr;
        newui::SegmentedControl* modeControl_ = nullptr;
        newui::TextField* filterField_ = nullptr;
        newui::SubView* detail_ = nullptr;
        newui::Label* detailTitle_ = nullptr;
        newui::Label* detailKind_ = nullptr;
        newui::Label* info_ = nullptr;
        newui::Label* status_ = nullptr;
        std::shared_ptr<GraphTreeController> controller_;
        std::vector<DetailRow> detailRows_;
        std::vector<newui::Button*> pathButtons_;
        Mode mode_ = Mode::Document;
        std::string filter_;
        std::vector<GraphRow> rows_;              // as shown
        std::set<std::string> collapsed_;         // keys of the rows the user closed (everything starts open)
        jitnodes::NodeId detailNode_ = 0;
        std::size_t detailObject_ = 0;
        std::size_t lastSignature_ = 0;
        bool built_ = false;
        bool updating_ = false;
        bool shown_ = false;
        float shownRight_ = -1.0f;
        bool havePropertyDock_ = false;
    };

}
