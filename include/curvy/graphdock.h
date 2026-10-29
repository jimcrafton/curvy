#pragma once

#include "curvy/canvasview.h"
#include "curvy/dockcontroller.h"
#include "curvy/expressionfield.h"
#include "curvy/graphtree.h"
#include "curvy/paramgrid.h"
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
        ~GraphDock() override;
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
        // Selects the row with this key (a Fill, Stroke or Effect row as well as an object's) and the object it belongs to.
        bool selectRow(const std::string& key);
        NodeRef detailNode() const { return detailNode_; }
        // The parameter box, for tests: the text of row `index` (caption and value) and whether it is shown.
        std::string detailCaption(std::size_t index) const;
        std::string detailValue(std::size_t index) const;
        // The names in the Inputs (or Used by) list of the box.
        std::vector<std::string> detailLinks(bool inputs) const;
        // Opens the Colour card on the colour in row `index` of the box (what clicking its swatch does): the object is selected, the
        // fill or stroke the row stands for is made the active one, and the card is put beside the dock. False when that row has no colour.
        bool editDetailColour(std::size_t index);
        // Selects the row of this node (and its object); false when it has none.
        bool selectNode(const NodeRef& node);
        bool selectNode(jitnodes::NodeId node) { return selectNode(NodeRef(node)); }
        bool detailShown(std::size_t index) const;
        // Types into row `index` as if the user had, and applies it (what Return does).
        void setDetailValue(std::size_t index, const std::string& text);
        std::string title() const;

        static constexpr std::size_t kDetailRows = 9;
        static constexpr std::size_t kLinkRows = 3;

    private:
        // What a row of the parameter box does; how it looks and what it shows is the matching row of grid_.
        struct DetailRow {
            using Kind = ParamGrid::Kind;
            Kind kind = Kind::Text;
            std::function<void(const std::string&)> apply;
            // Dragging the caption scrubs the number: the node and parameter it changes, within [minimum, maximum]; `scale` is how many
            // times the box's number is the parameter (a percent slider shows 100 times its parameter).
            bool scrub = false;
            NodeRef node;
            std::string param;
            double minimum = 0.0, maximum = 0.0, scale = 1.0;
            double unit = 1.0;                  // the plain drag step of the box's number
        };
        // One line of the Inputs and Used by lists: where it goes, and the node it names.
        struct LinkRow {
            newui::Label* index = nullptr;
            newui::Label* name = nullptr;
            newui::Label* go = nullptr;
            NodeRef node;
        };
        void layout();
        void buildDetail();
        void layoutDetail();
        void selectionChangedInTree();
        void showDetail(const GraphRow* row);
        void setRow(std::size_t index, const std::string& caption, const std::string& value, std::function<void(const std::string&)> apply);
        // One of `options`: a click steps to the next (Shift+click the previous) and `apply` gets its name.
        void setChoiceRow(std::size_t index, const std::string& caption, const std::string& current, std::vector<std::string> options, std::function<void(const std::string&)> apply);
        void setColourRow(std::size_t index, const std::string& caption, const SketchColor& color, std::function<void(const std::string&)> apply);
        // `value` is 0..1; `apply` gets the percent as text.
        void setSliderRow(std::size_t index, const std::string& caption, double value, std::function<void(const std::string&)> apply);
        void showLinks(const NodeRef& id);
        // Gives a Fill or Stroke node another kind of paint (none, solid, linear, radial, pattern), carrying what the current paint has
        // so the look does not vanish: a colour becomes a gradient from that colour, and back. One undo step.
        void setPaintKind(const NodeRef& id, int kind);
        void setGradientRow(std::size_t index, const std::string& caption, const SketchPaint& paint, std::function<void(const std::string&)> apply);
        void scrubRow(std::size_t index, const NodeRef& id, const std::string& param, double minimum, double maximum, double scale = 1.0);
        // The text of a detail row was entered: a number or expression for a parameter row, otherwise the row's own apply.
        void commitDetail(std::size_t index, const std::string& text);
        // Ctrl+K in a parameter box: puts a key at the current time with the shown value, or takes the key there away.
        void toggleKey(std::size_t index);
        // A parameter row shows "=expression" (in the expression colour) while one drives the parameter, otherwise its number.
        void showParam(DetailRow& row);
        void clearDetail();
        // Puts a transform node back to doing nothing, its pivot in the middle of its shape.
        void resetTransform(const NodeRef& id);
        // The numbers of the shown node change while the shape is dragged on the canvas; the rows follow without rebuilding the tree.
        void refreshDetailValues();
        std::vector<GraphRow> buildRows() const;
        std::vector<GraphRow> documentRows() const;
        std::vector<GraphRow> flowRowsFromGraph() const;
        void describe(GraphRow& row, const NodeRef& node) const;
        void describeLook(GraphRow& row, const NodeRef& node) const;
        // The appearance item a Fill, Stroke or Effect node stands for, and the object it belongs to; null when the node has no object.
        const AppearanceItem* itemOfNode(const NodeRef& node, std::size_t* objectId = nullptr) const;
        void lookRows(const jitnodes::Node& node, const NodeRef& id, std::size_t& at);
        std::size_t signature() const;
        std::size_t objectOfNode(NodeRef node) const;
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
        ParamGrid* grid_ = nullptr;
        newui::Label* detailTitle_ = nullptr;
        newui::Label* detailKind_ = nullptr;
        newui::Label* info_ = nullptr;
        newui::Label* status_ = nullptr;
        std::shared_ptr<GraphTreeController> controller_;
        std::vector<DetailRow> detailRows_;
        std::vector<newui::Button*> pathButtons_;
        newui::Button* resetButton_ = nullptr;
        std::vector<LinkRow> inputRows_, userRows_;
        std::shared_ptr<jitnodes::GraphSnapshot> scrubBefore_;   // the graph when the drag started
        newui::Label* inputsHeader_ = nullptr;
        newui::Label* usersHeader_ = nullptr;
        newui::Label* noInputs_ = nullptr;
        newui::Label* noUsers_ = nullptr;
        std::vector<NodeRef> inputNodes_, userNodes_;
        Mode mode_ = Mode::Document;
        std::string filter_;
        std::vector<GraphRow> rows_;              // as shown
        std::set<std::string> collapsed_;         // keys of the rows the user closed (everything starts open)
        NodeRef detailNode_;
        std::size_t detailObject_ = 0;
        std::string selectedKey_;                  // the row the user clicked, kept while its object stays selected
        std::size_t lastSignature_ = 0;
        bool built_ = false;
        bool updating_ = false;
        bool shown_ = false;
        float shownRight_ = -1.0f;
        bool havePropertyDock_ = false;
        newui::Connections connections_;
    };

}
