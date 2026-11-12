#pragma once

#include "curvy/noderef.h"
#include "curvy/sketchdocument.h"
#include "curvy/viewstate.h"

#include <newui/subview.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // The node graph of the document drawn as boxes and wires, one network at a time: the root's, or the contents of a subnet (a layer or group)
    // entered with a double-click. Nodes with a saved position sit there, the rest are laid out in columns by how far they are from the
    // sources. The wheel zooms at the pointer, a drag on the background pans, a click selects, a drag on a node moves it (the first move saves
    // the position of every node shown, so the others stay put; one undo step per drag). Backspace goes back up out of a subnet.
    // Shift or Ctrl+click adds a node to the selection or takes it out, Shift+drag on the background selects the boxes it touches, a drag of a
    // selected node moves them all and Delete deletes them (one undo step). A drag from an output dot to an input dot wires them; a drag from a
    // wired input dot takes the wire off, to drop on another input dot or in the open to unwire.
    // @reflect category=basic
    class GraphView : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        GraphView();
        ~GraphView() override = default;

        // Not owned; null shows nothing. The owner registers the view with the document (Model::addView) so edits repaint it.
        // @reflect ignore=true
        void setDocument(SketchDocument* document);

        // The network shown: scope 0 is the root.
        std::uint32_t scope() const { return scope_; }
        // Shows the contents of a subnet node; false when the node is not a subnet of the shown network.
        // @reflect ignore=true
        bool enter(const NodeRef& subnet);
        // Back up one level; false at the root.
        bool exit();
        // @reflect ignore=true
        const NodeRef& selected() const { return selected_; }
        // Selects just this node (an invalid ref clears the selection).
        // @reflect ignore=true
        void select(const NodeRef& node);
        // Every selected node of the shown network, the one the panel shows (selected()) last.
        // @reflect ignore=true
        const std::vector<NodeRef>& selection() const { return selection_; }
        // Deletes the selected nodes; false when none, or when the document refuses (onMessage then says why).
        bool deleteSelected();
        // "root > Layer1 > Group1" for the shown network.
        std::string path() const;
        // The names that make up path(): "root" first, the shown subnet last; empty without a document.
        std::vector<std::string> levelNames() const;
        // Goes back up to level (0 is the root, an index into levelNames()); false when already at or above it.
        bool goToLevel(std::size_t level);
        // One line about the selected node, for the hint bar.
        // @reflect ignore=true
        std::string describeSelected() const;

        // The status-line text for now: the selected node and how to enter or leave a subnet.
        std::string hint() const;

        // Fired when the selection changes or a subnet is entered or left.
        std::function<void()> onSelectionChanged;
        // A short sentence for the status line, such as why an edit was refused.
        std::function<void(const std::string&)> onMessage;
        // Asked first with every key; true when it was used (the owner handles the ring keys and Tab here).
        std::function<bool(std::uint32_t keyMask, std::uint32_t vkCode)> onKeyFilter;
        // Tab asks to go back to the canvas.
        std::function<void()> onCloseRequested;
        // Fired when the zoom or pan changed, so the toolbar's zoom label can follow.
        std::function<void()> onViewChanged;
        double zoom() const { return zoom_; }

        // The level shown, the zoom and pan, and the selection, for the file's "view" section (open is the owner's to fill in). restore() puts them
        // back, ignoring whatever no longer exists (a level or a node the document has not got): it falls back to the root, and to fitting.
        // @reflect ignore=true
        GraphViewState state() const;
        // @reflect ignore=true
        void restore(const GraphViewState& state);

        // What the minimap needs, all in graph units: the document, the shown boxes (x, y, w, h), the part of the graph in view, and a way to
        // recentre on a graph point.
        // @reflect ignore=true
        SketchDocument* document() const { return document_; }
        // @reflect ignore=true
        std::vector<BLRect> nodeRects() const;
        // @reflect ignore=true
        BLRect visibleRect() const;
        void centerOn(double graphX, double graphY);

        void fit();
        void paint(BLContext& ctx) override;

    private:
        struct Box {
            jitnodes::NodeId id = jitnodes::invalidNodeId;
            double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
            bool placed = false;   // while laying out: x and y are final
        };

        void fitView();
        std::vector<Box> boxes(const jitnodes::Network& network) const;
        BLPoint toView(double x, double y) const;
        BLPoint toGraph(const newui::Point& viewPoint) const;
        const Box* boxAt(const std::vector<Box>& all, const BLPoint& graphPoint) const;
        void endDrag();
        // A dot of a box: the output (input < 0) or input number `input`.
        struct Port {
            jitnodes::NodeId id = jitnodes::invalidNodeId;
            int input = -1;
            bool valid() const { return id != jitnodes::invalidNodeId; }
        };
        BLPoint portPoint(const Box& box, int input) const;
        Port portAt(const std::vector<Box>& all, const BLPoint& graphPoint, bool inputsOnly) const;
        void endWire(const newui::Point& pt);
        void endMarquee();
        void say(const std::string& text) const;
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        // The second press of a double-click arrives here, not as a mouse down.
        newui::SyncReturn handleMouseDoubleClick(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleSizeChanged(newui::View& sender, const newui::Size& size);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        void paintNode(BLContext& ctx, const jitnodes::Network& network, const Box& box) const;

        SketchDocument* document_ = nullptr;
        std::uint32_t scope_ = 0;
        NodeRef selected_;
        std::vector<NodeRef> selection_;
        double zoom_ = 1.0, panX_ = 0.0, panY_ = 0.0;
        bool fitted_ = false;
        bool panning_ = false;
        newui::Point last_;
        // A left press on a node: dragId_ is it, dragMoved_ once the pointer left the click tolerance (then dragBefore_ holds the graph as it was).
        jitnodes::NodeId dragId_ = jitnodes::invalidNodeId;
        bool dragMoved_ = false;
        newui::Point dragPress_;
        std::map<jitnodes::NodeId, BLPoint> dragOrigins_;
        // A wire being dragged from the output of wireSource_; wireOldTo_ and wireOldInput_ say which input it was taken off, when it was.
        bool wiring_ = false;
        jitnodes::NodeId wireSource_ = jitnodes::invalidNodeId;
        jitnodes::NodeId wireOldTo_ = jitnodes::invalidNodeId;
        int wireOldInput_ = -1;
        newui::Point wireEnd_;
        bool marquee_ = false;
        newui::Point marqueeStart_;
        std::optional<jitnodes::GraphSnapshot> dragBefore_;
    };

}
