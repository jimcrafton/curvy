#pragma once

#include "curvy/graphdocument.h"
#include "curvy/noderef.h"

#include <newui/delegate.h>

#include <vector>

namespace curvy {

    // What is selected in the document, shared by every view of it (the graph view, the canvas, the panels): a list of nodes, one of them the
    // primary, and what the primary means in the graph (the object it belongs to, its fill, its stroke ...). A view only turns its own input into
    // NodeRefs and calls set/add/toggle; it repaints from onChanged, so a pick in one view shows in the others. The model reads the graph and never
    // edits it. Nodes that are deleted (or whose edit is undone away) leave the selection when the document fires onGraphChanged.
    // The document must outlive the model, or setDocument(nullptr) is called first.
    // @reflect ignore=true
    class SelectionModel {
    public:
        SelectionModel() = default;
        explicit SelectionModel(GraphDocument* document) { setDocument(document); }
        ~SelectionModel();
        SelectionModel(const SelectionModel&) = delete;
        SelectionModel& operator=(const SelectionModel&) = delete;

        // Clears the selection (silently when it was empty, onChanged otherwise).
        void setDocument(GraphDocument* document);
        GraphDocument* document() const { return document_; }

        const std::vector<NodeRef>& items() const { return items_; }
        // The one the panels show; kNoNode when nothing is selected.
        const NodeRef& primary() const { return primary_; }
        bool empty() const { return items_.empty(); }
        bool contains(const NodeRef& node) const;

        // Each returns whether the selection changed, and fires onChanged once when it did. An invalid or unknown node is not selected.
        bool set(const NodeRef& node);                      // just this one (an invalid ref clears)
        bool clear();
        bool add(const NodeRef& node);                      // joins the selection and becomes the primary
        bool addAll(const std::vector<NodeRef>& nodes);     // a marquee: the last new one becomes the primary
        bool remove(const NodeRef& node);
        bool toggle(const NodeRef& node);
        bool setPrimary(const NodeRef& node);               // must already be selected; the selection itself stays
        bool replace(const std::vector<NodeRef>& nodes);    // the last one is the primary; unknown nodes are skipped
        // Selects the node that made the shapes with this key (what a click on the canvas hands over; see CanvasView::shapeAt). A key that
        // names no node clears the selection, unless `additive`, which then changes nothing; additive joins the selection like add().
        bool selectShapeKey(const std::string& key, bool additive = false);
        // A marquee or a select-by result: the nodes that made these shapes, combined with the selection. Replace with no shapes clears;
        // Add joins (the last new one becomes the primary); Subtract removes them. One onChanged at most.
        enum class Combine { Replace, Add, Subtract };
        bool selectShapeKeys(const std::vector<std::string>& keys, Combine how);
        // Replaces the selection with the nearest group or layer around the primary; false (nothing changes) at the top level.
        bool stepUp();
        // True when the node that made the shape, or a subnet around it (a layer or group), is selected: what the canvas highlights.
        bool isShapeKeySelected(const std::string& key) const;
        // Drops the nodes the document no longer has (done by itself on onGraphChanged).
        bool prune();

        // The parts of an object, found by walking the chain around a node: upstream along input 0 and downstream through the first consumer,
        // stopping at a merge. A graph that was built by hand may lack a part or have two; find() then gives the one nearest the merge (the one
        // whose stamp wins) and count() says how many there are. kNoNode: the part is missing.
        enum class Role { Object, Layer, ObjectXform, LocalTransform, Fill, Stroke, Shape };
        NodeRef find(Role role) const { return find(role, primary_); }
        int count(Role role) const { return count(role, primary_); }
        NodeRef find(Role role, const NodeRef& from) const;
        int count(Role role, const NodeRef& from) const;

        NodeRef object() const { return find(Role::Object); }
        NodeRef layer() const { return find(Role::Layer); }
        NodeRef objectXform() const { return find(Role::ObjectXform); }
        NodeRef localTransform() const { return find(Role::LocalTransform); }
        NodeRef fill() const { return find(Role::Fill); }
        NodeRef stroke() const { return find(Role::Stroke); }
        NodeRef shape() const { return find(Role::Shape); }

        newui::Delegate<SelectionModel> onChanged;

    private:
        bool known(const NodeRef& node) const;
        void changed();
        // The object a node belongs to: itself when it is an object, else the nearest enclosing one.
        NodeRef objectOf(const NodeRef& node) const;
        // The nearest object that contains `node` (not the node itself).
        NodeRef enclosing(const NodeRef& node) const;
        // The nodes around `from` in flow order, from the source to the node before the merge.
        std::vector<NodeRef> chainOf(const NodeRef& from) const;

        bool documentAlive() const { return document_ != nullptr && !documentLife_.expired(); }
        void disconnect();

        GraphDocument* document_ = nullptr;
        std::weak_ptr<void> documentLife_;
        newui::Connection connection_;
        std::vector<NodeRef> items_;
        NodeRef primary_;
    };

}
