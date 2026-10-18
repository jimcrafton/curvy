#pragma once

#include "curvy/layertree.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <map>
#include <set>

namespace curvy {

    // The Layers panel of sketch.newui: the tree (LayerTree) of layers, the groups and objects in them, with New Layer / New Sublayer /
    // Delete buttons, a field for the name of the active layer (or of the one selected object) and a drop-down for the layer's colour.
    // The tree reports clicks and drags; this turns them into the document's undoable edits, and rebuilds the tree's rows whenever
    // the document or the selection changes. Groups are listed closed until opened (the panel remembers which are open).
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed
    // through a plain reference and must outlive it.
    // @reflect ignore=true
    class LayersPanel : public newui::Controller {
    public:
        LayersPanel(newui::RootView& root, SketchDocument& document);
        ~LayersPanel() override = default;
        LayersPanel(const LayersPanel&) = delete;
        LayersPanel& operator=(const LayersPanel&) = delete;

        // Rebuilds the rows and the name field from the document.
        void refresh();

    private:
        void addLayerRows(std::size_t parent, std::vector<LayerRow>& rows) const;
        void addObjectRows(const Layer& layer, std::size_t depth, std::vector<LayerRow>& rows) const;
        // What the rows depend on, as one number: when it is the same as at the last refresh the rows would come out the same (a drag
        // that only moves outlines changes nothing here), so the tree is left alone. Cheap: no allocation.
        std::size_t signature() const;
        // Per group: whether all its members are visible / locked / selected (filled once per refresh instead of per group row).
        struct GroupState { bool visible = true, locked = true, selected = true; };
        mutable std::map<std::size_t, GroupState> groupStates_;
        // The objects a row stands for: itself, or the members of its group.
        std::vector<std::size_t> idsOf(const LayerRow& row) const;
        void select(const LayerRow& row, bool toggle, bool add);
        void drop(const LayerRow& source, const LayerRow& target, LayerTree::Drop where);
        void dropLayer(std::size_t source, std::size_t target, LayerTree::Drop where);

        newui::RootView& root_;
        SketchDocument& document_;
        LayerTree* tree_ = nullptr;
        newui::TextField* name_ = nullptr;
        newui::DropDownList* color_ = nullptr;
        std::set<std::size_t> openGroups_;       // the groups shown open
        bool focusObject_ = false;               // the name field edits the selected object (the last click was on an object)
        std::vector<std::size_t> lastSelection_;
        std::size_t lastSignature_ = 0;
        bool rowsBuilt_ = false;
        std::size_t lastActive_ = 0;
        bool updating_ = false;
    };

}
