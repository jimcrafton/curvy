#pragma once

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace curvy {

    // A point of a trace shape that other things snap to.
    // @reflect ignore=true
    struct TraceSnapPoint {
        enum class Kind { Corner, Midpoint, Center };
        BLPoint point;
        Kind kind = Kind::Corner;
    };

    // One straight piece of a trace shape's outline.
    // @reflect ignore=true
    struct TraceSegment {
        BLPoint a;
        BLPoint b;
    };

    // One guide shape on the trace layer, in document points. Rect and Line exist so far; the other kinds (polygon, curve) add a Kind and
    // use the same fields. Not a node: the trace layer never goes through the graph.
    // A rect is (x, y) its top left corner and (width, height) its size, both positive. A line is (x, y) its start and (width, height) the
    // vector to its end (either may be negative or zero, not both), so the file entry and translating are the same for every kind.
    // @reflect ignore=true
    struct TraceItem {
        enum class Kind { Rect, Line };

        Kind kind = Kind::Rect;
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;

        static std::string kindName(Kind kind);
        // Rect for an unknown name, with `known` (if given) false.
        static Kind kindFromName(const std::string& name, bool* known = nullptr);

        // A rect between two opposite corners, whichever way round they are given.
        static TraceItem rect(double x1, double y1, double x2, double y2);
        // A line from the first point to the second.
        static TraceItem line(double x1, double y1, double x2, double y2);

        double endX() const { return x + width; }
        double endY() const { return y + height; }

        // True when every number is finite and the shape has a size (a line a length).
        bool valid() const;
        // How far the point is from the shape's outline (a rect's inside is not on it).
        double distanceTo(const BLPoint& point) const;
        BLBox bounds() const;
        // The outline in document points.
        BLPath path() const;
        // The same shape moved by (dx, dy) points.
        TraceItem translated(double dx, double dy) const;
        // What snapping reads: the points of the shape (a rect: 4 corners, 4 edge midpoints, the centre) and its straight outline pieces.
        std::vector<TraceSnapPoint> snapPoints() const;
        std::vector<TraceSegment> segments() const;

        bool operator==(const TraceItem& other) const;
        bool operator!=(const TraceItem& other) const { return !(*this == other); }
    };

    // The guide shapes drawn over the artwork: outlines only, one fixed colour and width, never rendered by the graph and never exported.
    // A plain value so the document can snapshot it for undo; SketchDocument owns the one that is saved and wraps every edit in an undo step.
    // @reflect ignore=true
    class TraceLayer {
    public:
        static constexpr std::uint32_t kColor = 0x00E040FB;   // the outlines, 0x00RRGGBB; their alpha is kLineOpacity
        static constexpr double kLineOpacity = 0.8;           // a little more solid than the sheet, so the lines read over it
        static constexpr std::uint32_t kSheetColor = 0x00FFFFFF;   // 0x00RRGGBB; the sheet's alpha comes from the layer's opacity
        static constexpr double kWidth = 1.0;                 // screen pixels
        static constexpr double kDefaultOpacity = 0.6;
        static constexpr double kMinOpacity = 0.05;

        const std::vector<TraceItem>& items() const { return items_; }
        bool visible() const { return visible_; }
        bool locked() const { return locked_; }
        void setVisible(bool visible) { visible_ = visible; }
        void setLocked(bool locked) { locked_ = locked; }
        // How opaque the layer's sheet is (1 = solid white), so the artwork shows through it like tracing paper. Clamped to kMinOpacity..1.
        double opacity() const { return opacity_; }
        void setOpacity(double opacity);

        std::size_t add(const TraceItem& item);
        void set(std::size_t index, const TraceItem& item);
        void remove(std::size_t index);
        void clear() { items_.clear(); }
        bool empty() const { return items_.empty(); }

        // The topmost item whose outline lies within `tolerance` document points of the point.
        // Returns items().size() for none.
        std::size_t hitTest(const BLPoint& point, double tolerance) const;

        // Washes `area` (the whole canvas) with the sheet: white at the layer's opacity. Nothing when the layer is hidden.
        void paintSheet(BLContext& ctx, const BLRect& area) const;

        // Strokes every item when the layer is visible; the page's top left is at `origin` in the context's pixels, `zoom` pixels per point.
        void paint(BLContext& ctx, const BLPoint& origin, double zoom) const;

        // Draws one item (also a tool's preview of the one being made).
        static void paintItem(BLContext& ctx, const TraceItem& item, const BLPoint& origin, double zoom);
        // The same in another colour and width (a selected shape).
        static void paintItemAs(BLContext& ctx, const TraceItem& item, const BLPoint& origin, double zoom, const BLRgba32& color, double width);

        bool operator==(const TraceLayer& other) const { return items_ == other.items_; }
        bool operator!=(const TraceLayer& other) const { return !(*this == other); }

    private:
        std::vector<TraceItem> items_;
        bool visible_ = true;
        bool locked_ = false;
        double opacity_ = kDefaultOpacity;
    };

}
