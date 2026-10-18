#pragma once

#include <newui/subview.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // One line of the Layers panel: a layer, a group inside a layer, or an object.
    // @reflect ignore=true
    struct LayerRow {
        enum class Kind { Layer, Group, Object };
        Kind kind = Kind::Layer;
        std::size_t id = 0;            // the layer's id, the group's id or the object's id, by kind
        std::size_t layer = 0;         // the layer the row belongs to (for a layer row: itself)
        std::size_t depth = 0;         // 0 for a top-level layer, 1 for its sublayers and objects...
        std::string name;
        bool visible = true;           // the row's own eye / lock (the row is drawn dimmer when a layer above hides or locks it)
        bool locked = false;
        bool shown = true;             // effective: no layer above hides it
        bool editable = true;
        bool hasChildren = false;
        bool expanded = true;
        bool active = false;           // a layer row: the layer new objects go to
        bool holdsSelection = false;   // a layer row: some selected object is in it; a group or object row: it is selected
        bool reveal = false;           // scroll so that this row shows (set on the row of what was just activated / selected)
        std::uint32_t color = 0x4F80FF;   // the colour of its layer (0xRRGGBB)
        int swatchKind = 0;            // an object row: 0 nothing to paint, 1 a solid colour (swatch), 2 a gradient / pattern
        std::uint32_t swatch = 0;      // 0xRRGGBB
    };

    // The tree of the Layers panel: one 20 px row per layer, group and object, indented by depth, each with an eye, a lock, an open /
    // close triangle (layers and groups), a swatch, the name, and at the right a square that is filled with the layer's colour when
    // the selection holds the row (click it to move the selection into the layer, or to add the object to the selection). A strip of
    // the layer's colour runs down the left edge. Dragging a row onto another puts it above, below or (a layer's middle) inside it.
    // The tree only draws and reports what was done through `actions`; LayersPanel does the editing. Shows the rows that fit, scrolls
    // with the mouse wheel and to keep the row to reveal in sight.
    // @reflect category=basic
    class LayerTree : public newui::SubView {
    public:
        LayerTree();
        virtual ~LayerTree() {}

        static constexpr float kRowHeight = 20.0f;
        enum class Drop { Above, Inside, Below };

        struct Actions {
            std::function<void(const LayerRow&)> toggleVisible;
            std::function<void(const LayerRow&)> toggleLock;
            std::function<void(const LayerRow&)> toggleOpen;
            std::function<void(const LayerRow&, bool, bool)> activate;      // (row, with Ctrl, with Shift)
            std::function<void(const LayerRow&)> moveSelectionHere;
            std::function<void(const LayerRow&, const LayerRow&, Drop)> drop;   // (dragged row, target row, where)
        };
        void setActions(Actions actions) { actions_ = std::move(actions); }

        void setRows(std::vector<LayerRow> rows);
        const std::vector<LayerRow>& rows() const { return rows_; }
        // How many rows are showing at the current height, and the first of them (a position in rows()).
        std::size_t visibleRows() const;
        std::size_t firstRow() const { return scroll_; }
        // Scrolls by `rows` (negative = up), stopping at the ends; false when it could not move.
        bool scrollBy(int rows);
        // Where the parts of row `index` (a position in rows()) are, in the view's own coordinates - what a click hits.
        newui::Rect eyeRect(std::size_t index) const;
        newui::Rect lockRect(std::size_t index) const;
        newui::Rect triangleRect(std::size_t index) const;
        newui::Rect targetRect(std::size_t index) const;
        newui::Rect nameRect(std::size_t index) const;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn mouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn mouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn mouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn mouseWheel(newui::View& sender, const newui::Point& pt, float delta);
        // The row (an index into rows()) under the point, or rows().size().
        std::size_t rowAt(const newui::Point& pt) const;
        Drop zoneAt(const newui::Point& pt, std::size_t row) const;
        newui::Rect rowRect(std::size_t index) const;
        std::size_t maxScroll() const;

        std::vector<LayerRow> rows_;
        Actions actions_;
        std::size_t scroll_ = 0;           // the first row showing
        std::size_t pressed_ = 0;          // the row a press began on (rows_.size() = none)
        bool dragging_ = false;
        std::size_t dropRow_ = 0;
        Drop dropZone_ = Drop::Inside;
        bool ctrl_ = false;
        bool shift_ = false;
    };

}
