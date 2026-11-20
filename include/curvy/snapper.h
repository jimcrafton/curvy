#pragma once

#include "curvy/tracelayer.h"

#include <blend2d/blend2d.h>

#include <cstddef>
#include <vector>

namespace curvy {

    class SketchDocument;

    // Where a point landed when it was snapped.
    // @reflect ignore=true
    struct SnapResult {
        enum class Kind { None, Corner, Midpoint, Center, Intersection, Edge, Guide, Align };

        BLPoint point;
        Kind kind = Kind::None;
        // For Align: the snap points this one lines up with, painted as dotted lines to them (one when it lines up on one axis, two on both).
        BLPoint reference;
        BLPoint reference2;
        int references = 0;
        bool snapped() const { return kind != Kind::None; }
        // "none", "corner", "midpoint", "center", "intersection", "edge", "guide" or "align": what scripts see.
        static const char* kindName(Kind kind);
    };

    // Pulls a point (or a set of moving points) onto the document's guide geometry: the shapes of the trace layer (their corners, edge midpoints
    // and centres, then the nearest point of an edge) and the ruler guides. It only reads the document and keeps no state, so any tool can make
    // one and ask. Nothing hidden snaps: a hidden trace layer or hidden guides are skipped.
    //
    // Order: a point target (corner, midpoint, centre, or the crossing of two shapes' edges) within reach wins outright, the nearest one first. Otherwise the point goes onto the
    // nearest edge, and then onto a guide on each axis (a vertical guide sets x, a horizontal one sets y; a guide wins over an edge on its own
    // axis). Reach is kPixels on screen, so it is kPixels / zoom document points.
    // @reflect ignore=true
    class Snapper {
    public:
        static constexpr double kPixels = 8.0;

        explicit Snapper(const SketchDocument& document) : document_(document) {}

        // `exclude` names trace shapes that do not attract (the ones being moved).
        SnapResult snap(const BLPoint& point, double zoom, const std::vector<std::size_t>& exclude = {}) const;

        // Lines a free point up with the x and/or y of the corners, midpoints and centres of the visible trace shapes (and of `extra`, such as
        // the start of the line being drawn) within reach: Align when either axis matched, else None. For a point nothing else snapped.
        SnapResult align(const BLPoint& point, double zoom, const std::vector<BLPoint>& extra = {}, const std::vector<std::size_t>& exclude = {}) const;

        // For a point that is held on a ray (a line locked square to an edge): `point` is where the pointer put it on the ray from `origin` in
        // unit direction `direction`. It is pulled to where the ray crosses a trace shape's edge or a ruler guide ahead of the origin, when that is
        // within reach of `point`; Intersection for an edge, Guide for a guide. It also lines up with the x or y of any corner, midpoint or
        // centre of a trace shape (Align, with `reference` set): the spot on the ray at that point's x or that point's y. The result stays on
        // the ray; not snapped returns `point`.
        SnapResult snapOnRay(const BLPoint& origin, const BLPoint& direction, const BLPoint& point, double zoom, const std::vector<std::size_t>& exclude = {}) const;

        // For a shape that is being dragged: `points` are its snap points where it started, `delta` how far it has been dragged. Returns the
        // delta corrected by the pull of one of the points (the delta itself when none feels any), and where that point landed. A point that
        // lands on a corner, midpoint or centre beats one that only slides onto an edge or a guide; among equals the smallest pull wins.
        struct MoveResult {
            BLPoint delta;
            SnapResult hit;
        };
        MoveResult snapMove(const std::vector<BLPoint>& points, const BLPoint& delta, double zoom, const std::vector<std::size_t>& exclude = {}) const;
        // The same question answered by line-up (see align): the moved point that has to be pulled least to line up with another shape's point
        // decides, on the axes it matches. For a drag that snapMove left alone.
        MoveResult alignMove(const std::vector<BLPoint>& points, const BLPoint& delta, double zoom, const std::vector<std::size_t>& exclude = {}) const;

        // A small mark at the snapped point (nothing for None): a square on a corner, a diamond on a midpoint, a ring on a centre, a cross on an
        // edge or a guide. `origin` is the page's top left in the context's pixels.
        static void paintMarker(BLContext& ctx, const SnapResult& result, const BLPoint& origin, double zoom);

    private:
        const SketchDocument& document_;
    };

}
