#pragma once

#include "curvy/backgroundtask.h"
#include "curvy/colorspace.h"
#include "curvy/graphdocument.h"
#include "curvy/swatchpalettes.h"
#include "curvy/viewstate.h"

#include <newui/color.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // A vector document: the graph of GraphDocument plus the page it is drawn on. For now the page is the whole of it - no artboard list,
    // objects or colour mode yet.
    // @reflect ignore=true
    class SketchDocument : public GraphDocument {
    public:
        SketchDocument() = default;

        // The page size in points (1/72 in) and the pixel density used when exporting or previewing.
        float pageWidth() const { return pageWidth_; }
        float pageHeight() const { return pageHeight_; }
        float ppi() const { return ppi_; }
        // Sizes are made at least 1 point. Not undoable (it is the document's setup, like New).
        void setPageSize(float width, float height);
        void setPpi(float ppi);

        // The page's fill: opaque white by default. A color with alpha 0 is a transparent page, drawn as a checkerboard.
        const newui::Color& pageColor() const { return pageColor_; }
        void setPageColor(const newui::Color& color);
        bool pageTransparent() const { return pageColor_.a <= 0.0f; }

        // RGB or CMYK; other spaces are ignored.
        ColorSpace colorMode() const { return colorMode_; }
        void setColorMode(ColorSpace mode);

        const std::string& name() const { return name_; }
        void setName(const std::string& name);

        // ---- Swatches and palettes ----
        // A document keeps at least one palette (a new one starts with DefaultPalette), one of them active. Every edit below is one undo step and throws
        // std::runtime_error when refused (a bad index, a blank or taken palette name, deleting the last palette). Undo and redo keep the active palette
        // when it still exists. Palette names are unique.
        const std::vector<Palette>& palettes() const { return palettes_; }
        std::size_t activePalette() const { return activePalette_; }
        // Not an undo step and not a change of the document (it is where the user is looking), but it is saved with the file.
        void setActivePalette(std::size_t index);
        std::size_t addPalette(const std::string& name);
        std::size_t duplicatePalette(std::size_t index);
        void renamePalette(std::size_t index, const std::string& name);
        void deletePalette(std::size_t index);
        std::size_t addSwatch(std::size_t palette, const Swatch& swatch);
        // Keeps the swatch's name when `swatch` has none, and its link. Replacing a linked swatch recolours every colour row tied to it
        // (SwatchLinks::tag) in the same undo step.
        void replaceSwatch(std::size_t palette, std::size_t index, const Swatch& swatch);
        void renameSwatch(std::size_t palette, std::size_t index, const std::string& name);
        // Deleting a linked swatch unlinks its colour rows (they keep their colours), in the same undo step.
        void deleteSwatch(std::size_t palette, std::size_t index);
        // Makes the swatch linked (it gets a new number; no colour row is tied to it yet) or plain again (its colour rows are unlinked and keep
        // their colours), as one undo step. The swatches added, saved from Recent or copied by Duplicate are always plain.
        void setSwatchLinked(std::size_t palette, std::size_t index, bool linked);
        // Puts the swatch at position `to` (counted after it is taken out).
        void moveSwatch(std::size_t palette, std::size_t from, std::size_t to);

        // The colours applied lately, newest first, without repeats (at most kRecentLimit). Held for the session: not saved, not an undo step and
        // not a change of the document.
        // Called after the palettes, the active palette or the recent colours changed by an edit, undo or redo (on the caller's thread). Not called by
        // New or a load, which may run on another thread: whoever shows the palettes reads them again when it opens. One listener: the colour card's
        // swatch strip.
        std::function<void()> onPalettesChanged;

        static constexpr std::size_t kRecentLimit = 24;
        const std::vector<Swatch>& recentColors() const { return recent_; }
        void noteRecentColor(const Swatch& color);

        // Where the user was looking (the canvas zoom and pan, whether the graph view was up, which level of it, its zoom and pan, the selection),
        // saved with the file in the optional "view" section and given back by a load. Not an edit: not undoable, does not mark the document
        // modified. The workspace sets it just before a save and applies it after an open or a New; a document without it opens fitted.
        const ViewState& viewState() const { return viewState_; }
        void setViewState(ViewState state) { viewState_ = std::move(state); }

        // Replaces everything with an empty, unmodified document: no graph, no undo history, a blank page of this size.
        void newBlank(const std::string& name, float width, float height, float ppi = 72.0f, ColorSpace colorMode = ColorSpace::RGB,
                      const newui::Color& pageColor = newui::Color(1.0f, 1.0f, 1.0f, 1.0f));

        // ---- Layers and groups ----
        // Both are Object subnets (see docs/v2-objects-plan.md). Layers hang off the root merge, groups off another object's merge; the order of
        // a merge's inputs is the z-order, first at the bottom. Each call is one undo step and throws std::runtime_error when it is refused.
        // A blank name gets the next free "Layer1" / "Group1".
        NodeRef addLayer(const std::string& name = "");
        NodeRef addGroup(const NodeRef& parent, const std::string& name = "");
        // The layers (parent = kNoNode) or the groups of an object, bottom first.
        std::vector<NodeRef> childrenOf(const NodeRef& parent = kNoNode);
        // Moves a group under `parent` (a layer or another group, not itself or something inside it) at position `index` among its children
        // (clamped). The node gets a new id; expressions that refer to it by name are not followed.
        NodeRef moveGroup(const NodeRef& group, const NodeRef& parent, std::size_t index);
        // `order` is a permutation of the children of `parent`.
        void reorderChildren(const NodeRef& parent, const std::vector<NodeRef>& order);
        void reorderLayers(const std::vector<NodeRef>& order) { reorderChildren(kNoNode, order); }
        // Layer1 (Group1, Group2), Layer2 (Group3), Layer3 (GroupA), no shapes; as one undo step.
        void buildSampleLayers();

        // ---- Node operations of the graph view ----
        // Each is one undo step and throws std::runtime_error when refused.
        // Bypass skips the node (its first input passes through); the structural nodes of layers, groups and the document are refused.
        void setNodeBypassed(const NodeRef& node, bool bypassed);
        // Freezing cooks the node and keeps what it made, so nothing upstream is cooked for it until it is unfrozen.
        void setNodeFrozen(const NodeRef& node, bool frozen);
        // Shows this node's output on the canvas; only the document's own graph (scope 0) can show another node.
        void setNodeDisplayed(const NodeRef& node);
        // Puts the node and everything that feeds it into a new group that takes its place (what used the node uses the group). Refused when
        // something inside also feeds a node outside, or for layers, groups and structural nodes. Returns the group.
        NodeRef groupNode(const NodeRef& node);
        // The reverse: the group's contents move into the group's network and take its place; the group's own transform must be plain.
        // Returns what now stands where the group was, bottom first.
        std::vector<NodeRef> ungroup(const NodeRef& group);

        // Adds a filled circle (a circle node into a fill node, wired into the object's merge on top of what is there) as one undo step. Only
        // a layer's or group's own merge is touched, so reorderChildren on that object would drop the shape - shapes need their own reorder.
        NodeRef addCircle(const NodeRef& object, float cx, float cy, float radius, const newui::Color& fill);

        // What the document draws: the cooked result of the layers' merge, with each primitive carrying its fill.* / stroke.* attributes.
        // Null when there is no layer yet or the graph fails to cook. Not const because cooking caches in the network.
        jitnodes::GeometryHandle artwork();

        // A layer or group whose structure is not what the document expects (a missing or doubled xform or merge, a merge that does not feed
        // the xform), found when a file loads. Loading changes nothing: the user decides whether to repair, because the odd structure may be
        // deliberate.
        struct ObjectProblem {
            NodeRef object;
            std::string text;
        };
        const std::vector<ObjectProblem>& objectProblems() const { return objectProblems_; }
        // Repairs every flagged object as one undo step and clears the flags.
        void repairObjectProblems();

        // True while the document loads or saves itself, on whatever thread. Its own setters and newBlank throw std::runtime_error then.
        bool isLocked() const { return locked_.load(); }

        // Where load() and save() report how far they are and look for a cancel; a cancelled load leaves the document as it was and a
        // cancelled save writes nothing (both return false). Null (the default) means neither.
        void setTaskProgress(TaskProgress* progress) { taskProgress_ = progress; }
        // Why the last load or save refused (empty when it gave no reason); read after it has finished.
        const std::string& lastError() const { return lastError_; }

    protected:
        // The nodes that hold a layer, a group and the document together (a layer's or group's own transform and merge, the root merge and the
        // render node) cannot be deleted, and the transform's and the render node's input is fixed. Wiring into a merge stays free: that is how shapes join.
        std::string refuseGraphEdit(const NodeRef& node, bool removing) const override;
        // The tags that mark layers, groups and the document's own nodes cannot be added to or taken off a node: the document finds them by tag.
        std::string refuseTagEdit(const NodeRef& node, const std::vector<std::string>& tags) const override;
        // The .curvy file (format version 3): meta, then the page and other non-node information, the optional view, then the node graph.
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

    private:
        // Locks the document for as long as it lives, so readFromFile / writeToFile set it first and release it on the way out.
        class LockGuard {
        public:
            explicit LockGuard(SketchDocument& document) : document_(document) { document_.locked_.store(true); }
            ~LockGuard() { document_.locked_.store(false); }
            LockGuard(const LockGuard&) = delete;
            LockGuard& operator=(const LockGuard&) = delete;

        private:
            SketchDocument& document_;
        };

        void requireUnlocked() const;
        void install(const std::string& name, float width, float height, float ppi, ColorSpace colorMode, const newui::Color& pageColor);
        void changed();
        // Runs `edit` (which changes palettes_ and must not throw after it has changed anything) and records the before and after as one undo step.
        // `withGraph`: the edit also changes nodes (linked swatches), and undo and redo restore the graph with the palettes.
        void editPalettes(const std::string& description, const std::function<void()>& edit, bool withGraph = false);
        // Every node of every network, subnets included.
        void forEachNode(const std::function<void(jitnodes::Network&, jitnodes::NodeId)>& visit);
        // Gives the colour rows tied to swatch `link` the swatch's colour (`color` null: drops the tie and keeps their colours).
        void applyLink(std::uint32_t link, const Swatch* color);
        Palette& paletteAt(std::size_t index);
        Swatch& swatchAt(std::size_t palette, std::size_t index);
        void clampActivePalette();
        void palettesChanged() {
            if (onPalettesChanged) {
                onPalettesChanged();
            }
        }
        void findObjectProblems(const jitnodes::Network& network, std::vector<jitnodes::NodeId>& path);

        std::vector<ObjectProblem> objectProblems_;
        std::atomic<bool> locked_{false};
        TaskProgress* taskProgress_ = nullptr;
        std::string lastError_;

        std::string name_ = "Untitled";
        float pageWidth_ = 612.0f;
        float pageHeight_ = 792.0f;
        float ppi_ = 72.0f;
        ColorSpace colorMode_ = ColorSpace::RGB;
        newui::Color pageColor_{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<Palette> palettes_ = {DefaultPalette::make()};
        std::size_t activePalette_ = 0;
        std::vector<Swatch> recent_;
        ViewState viewState_;
    };

}
