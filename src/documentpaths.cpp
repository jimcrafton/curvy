#include "curvy/sketchdocument.h"

#include "curvy/pathops.h"
#include "curvy/shapetools.h"

#include <algorithm>
#include <cmath>

// Object > Path: Join, Reverse Direction, Simplify and Outline Stroke - commands that rewrite the outlines of the selected objects.

namespace curvy {

    namespace {

        bool editable(const SketchObject& object) {
            return object.isVisible() && !object.isLocked() && !object.isText();
        }

        double apart(const BLPoint& a, const BLPoint& b) {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        // The same outline walked the other way: anchors in reverse order, each one's two handles swapped.
        void reverseSubPath(SubPath& sub) {
            std::reverse(sub.anchors.begin(), sub.anchors.end());
            for (PenAnchor& anchor : sub.anchors) {
                std::swap(anchor.in, anchor.out);
                std::swap(anchor.hasIn, anchor.hasOut);
            }
        }

        BLStrokeCap strokeCapOf(StrokeCap cap) {
            return cap == StrokeCap::Round ? BL_STROKE_CAP_ROUND : (cap == StrokeCap::Square ? BL_STROKE_CAP_SQUARE : BL_STROKE_CAP_BUTT);
        }

        BLStrokeJoin strokeJoinOf(StrokeJoin join) {
            return join == StrokeJoin::Round ? BL_STROKE_JOIN_ROUND : (join == StrokeJoin::Bevel ? BL_STROKE_JOIN_BEVEL : BL_STROKE_JOIN_MITER_BEVEL);
        }
    }

    // ---- Reverse Direction ----

    bool SketchDocument::reverseSelection() {
        std::vector<GeometryState> before = snapshotSelection();
        FrameState frameBefore = frameState();
        bool any = false;
        for (std::size_t id : selection_) {
            SketchObject* object = findById(id);
            if (object == nullptr || !editable(*object)) {
                continue;
            }
            std::vector<SubPath> subs = pathToSubPaths(object->geometry());
            if (subs.empty()) {
                continue;
            }
            for (SubPath& sub : subs) {
                reverseSubPath(sub);
            }
            setObjectOutline(*object, subPathsToPath(subs));
            any = true;
        }
        if (!any) {
            return false;
        }
        commitGeometry(before, "Reverse Direction", frameBefore);
        return true;
    }

    // ---- Simplify ----

    bool SketchDocument::simplifySelection(double tolerance) {
        std::vector<GeometryState> before = snapshotSelection();
        FrameState frameBefore = frameState();
        bool any = false;
        for (std::size_t id : selection_) {
            SketchObject* object = findById(id);
            if (object == nullptr || !editable(*object)) {
                continue;
            }
            std::vector<bool> closed;
            std::vector<std::vector<BLPoint>> lines = flattenToLines(object->geometry(), closed, 0.02);
            std::vector<SubPath> subs;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::vector<BLPoint> points = lines[i];
                if (closed[i] && !points.empty()) {
                    points.push_back(points.front());   // walk all the way round
                }
                std::vector<PenAnchor> anchors = fitCurve(points, tolerance);
                if (anchors.size() < 2) {
                    continue;
                }
                SubPath sub;
                sub.closed = closed[i];
                if (sub.closed && apart(anchors.front().point, anchors.back().point) < 1e-6) {
                    anchors.front().in = anchors.back().in;   // the last anchor is the first again: one anchor with both handles
                    anchors.front().hasIn = anchors.back().hasIn;
                    anchors.pop_back();
                }
                sub.anchors = std::move(anchors);
                subs.push_back(std::move(sub));
            }
            if (subs.empty()) {
                continue;
            }
            setObjectOutline(*object, subPathsToPath(subs));
            any = true;
        }
        if (!any) {
            return false;
        }
        commitGeometry(before, "Simplify", frameBefore);
        return true;
    }

    // ---- Join ----

    bool SketchDocument::joinSelection() {
        // The selected objects that are one open outline each.
        struct Piece {
            SketchObject* object;
            SubPath sub;
        };
        std::vector<Piece> pieces;   // bottom to top
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!isSelected(object->id()) || !editable(*object)) {
                continue;
            }
            std::vector<SubPath> subs = pathToSubPaths(object->geometry());
            if (subs.size() == 1 && !subs[0].closed && subs[0].anchors.size() >= 2) {
                pieces.push_back(Piece{object.get(), subs[0]});
            }
        }
        if (pieces.empty()) {
            return false;
        }
        if (pieces.size() == 1) {
            // One open path: its ends are joined, closing it.
            std::vector<GeometryState> before = snapshotSelection();
            FrameState frameBefore = frameState();
            SubPath sub = pieces[0].sub;
            if (apart(sub.anchors.front().point, sub.anchors.back().point) < 1e-3) {
                sub.anchors.front().in = sub.anchors.back().in;   // the ends already meet: they become one anchor
                sub.anchors.front().hasIn = sub.anchors.back().hasIn;
                sub.anchors.pop_back();
            }
            sub.closed = true;
            setObjectOutline(*pieces[0].object, subPathsToPath({sub}));
            commitGeometry(before, "Join", frameBefore);
            return true;
        }
        // Several: chained into one path, each next piece being the one whose end is nearest the chain's end (turned round when its far
        // end is the nearer). Ends that meet become one anchor; ends that do not are joined by a straight segment.
        std::vector<PenAnchor> chain = pieces[0].sub.anchors;
        std::vector<bool> used(pieces.size(), false);
        used[0] = true;
        for (std::size_t joined = 1; joined < pieces.size(); ++joined) {
            double best = 1e300;
            std::size_t bestPiece = 0;
            bool reverse = false;
            for (std::size_t i = 1; i < pieces.size(); ++i) {
                if (used[i]) { continue; }
                double toStart = apart(chain.back().point, pieces[i].sub.anchors.front().point);
                double toEnd = apart(chain.back().point, pieces[i].sub.anchors.back().point);
                if (toStart < best) { best = toStart; bestPiece = i; reverse = false; }
                if (toEnd < best) { best = toEnd; bestPiece = i; reverse = true; }
            }
            used[bestPiece] = true;
            SubPath next = pieces[bestPiece].sub;
            if (reverse) {
                reverseSubPath(next);
            }
            std::size_t from = 0;
            if (best < 1e-3) {
                chain.back().out = next.anchors.front().out;   // the meeting anchor keeps its in-handle and takes the next piece's out-handle
                chain.back().hasOut = next.anchors.front().hasOut;
                from = 1;
            }
            chain.insert(chain.end(), next.anchors.begin() + std::ptrdiff_t(from), next.anchors.end());
        }
        SubPath whole;
        whole.anchors = std::move(chain);
        Replacement replacement;
        for (const Piece& piece : pieces) {
            replacement.remove.push_back(piece.object->id());
        }
        replacement.add.emplace_back(pieces.back().object->cloneWithGeometry(SketchObjectType::Path, subPathsToPath({whole})));
        replacement.atTop = true;
        std::vector<Replacement> all;
        all.push_back(std::move(replacement));
        replaceUndoable(std::move(all), "Join");
        return true;
    }

    // ---- Outline Stroke ----

    bool SketchDocument::outlineStroke() {
        std::vector<Replacement> replacements;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!isSelected(object->id()) || !editable(*object)) {
                continue;
            }
            const AppearanceItem& stroke = object->activeItem(AppearanceKind::Stroke);
            if (stroke.kind != AppearanceKind::Stroke || !stroke.visible || stroke.paint.isNone() || stroke.strokeWidth <= 0.0) {
                continue;
            }
            BLStrokeOptions options;
            options.width = stroke.strokeWidth;
            options.start_cap = options.end_cap = uint8_t(strokeCapOf(stroke.strokeOptions.cap));
            options.join = uint8_t(strokeJoinOf(stroke.strokeOptions.join));
            options.miter_limit = stroke.strokeOptions.miterLimit;
            BLPath outline;
            if (outline.add_stroked_path(object->geometry(), options, BLApproximationOptions()) != BL_SUCCESS || outline.size() == 0) {
                continue;
            }
            Replacement replacement;
            replacement.remove.push_back(object->id());
            // What was the stroke is now a filled shape (wearing the stroke's paint); the fill, if any, stays as its own object beneath it.
            bool hasFill = false;
            std::vector<AppearanceItem> rest;
            for (const AppearanceItem& item : object->appearance()) {
                if (item.kind == AppearanceKind::Fill) {
                    rest.push_back(item);
                    hasFill = hasFill || (item.visible && !item.paint.isNone());
                }
            }
            if (hasFill && object->isClosed()) {
                SketchObject* below = object->cloneWithGeometry(SketchObjectType::Path, object->geometry());
                below->setAppearance(rest);
                replacement.add.emplace_back(below);
            }
            SketchObject* ring = object->cloneWithGeometry(SketchObjectType::Path, outline);
            AppearanceItem fill = AppearanceItem::makeFill(stroke.paint);
            fill.opacity = stroke.opacity;
            fill.blendMode = stroke.blendMode;
            ring->setAppearance({fill});
            ring->setName("Outlined Stroke");
            replacement.add.emplace_back(ring);
            replacements.push_back(std::move(replacement));
        }
        if (replacements.empty()) {
            return false;
        }
        replaceUndoable(std::move(replacements), "Outline Stroke");
        return true;
    }

}
