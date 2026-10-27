#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>
#include <map>

// Numeric transforms of the selection (the Transform panel) and Transform Again.

namespace curvy {

    namespace {
        constexpr double kMinSide = 1e-6;
    }

    // Remembering what the last move / resize / rotate did, so Transform Again can do it once more to whatever is selected.
    void SketchDocument::noteTransform(const BLMatrix2D& matrix, double angleDelta) {
        lastTransform_ = matrix;
        lastTransformAngle_ = angleDelta;
        hasLastTransform_ = true;
    }

    bool SketchDocument::transformAgain() {
        if (!hasLastTransform_) {
            return false;
        }
        std::vector<GeometryState> before = snapshotSelection(true);
        if (before.empty()) {
            return false;
        }
        FrameState frameBefore = frameState();
        placeObjects(before, lastTransform_, 1.0, lastTransformAngle_);
        quadFrame_.reset();
        groupAngle_.reset();
        commitPlacement(before, "Transform Again", frameBefore);
        return true;
    }

    BLMatrix2D SketchDocument::matrixForBox(const BLBox& from, const BLBox& to) {
        double oldW = from.x1 - from.x0, oldH = from.y1 - from.y0;
        double sx = oldW > kMinSide ? std::max(to.x1 - to.x0, kMinSide) / oldW : 1.0;
        double sy = oldH > kMinSide ? std::max(to.y1 - to.y0, kMinSide) / oldH : 1.0;
        BLMatrix2D m = BLMatrix2D::make_translation(-from.x0, -from.y0);
        m.post_scale(sx, sy);
        m.post_translate(to.x0, to.y0);
        return m;
    }

    void SketchDocument::previewTransform(const std::vector<GeometryState>& before, const BLMatrix2D& matrix, double angleDelta) {
        placeObjects(before, matrix, 1.0, angleDelta);
        quadFrame_.reset();
        groupAngle_.reset();
        touch();
    }

    bool SketchDocument::setSelectionBox(const BLBox& box) {
        BLBox now;
        if (!selectionBounds(now)) {
            return false;
        }
        double oldW = now.x1 - now.x0, oldH = now.y1 - now.y0;
        double newW = box.x1 - box.x0, newH = box.y1 - box.y0;
        if (newW < 0.0 || newH < 0.0 || !std::isfinite(newW) || !std::isfinite(newH) || !std::isfinite(box.x0) || !std::isfinite(box.y0)) {
            return false;
        }
        // A side that has no length (a straight line, say) cannot be scaled: it only moves.
        double sx = oldW > kMinSide ? std::max(newW, kMinSide) / oldW : 1.0;
        double sy = oldH > kMinSide ? std::max(newH, kMinSide) / oldH : 1.0;
        bool resized = std::fabs(sx - 1.0) > 1e-9 || std::fabs(sy - 1.0) > 1e-9;
        BLMatrix2D m = BLMatrix2D::make_translation(-now.x0, -now.y0);
        m.post_scale(sx, sy);
        m.post_translate(box.x0, box.y0);
        if (!resized && std::fabs(box.x0 - now.x0) < 1e-9 && std::fabs(box.y0 - now.y0) < 1e-9) {
            return false;   // nothing changes
        }
        transformObjects(selection_, m, resized ? "Resize" : "Move");
        return true;
    }

    BLMatrix2D SketchDocument::rotationAbout(const BLPoint& origin, double radians) {
        // make_rotation(angle, x, y) does not pivot about (x, y) in this Blend2D - build it.
        BLMatrix2D m = BLMatrix2D::make_translation(-origin.x, -origin.y);
        m.post_rotate(radians);
        m.post_translate(origin.x, origin.y);
        return m;
    }

    BLMatrix2D SketchDocument::scalingAbout(const BLPoint& origin, double sx, double sy) {
        BLMatrix2D m = BLMatrix2D::make_translation(-origin.x, -origin.y);
        m.post_scale(sx, sy);
        m.post_translate(origin.x, origin.y);
        return m;
    }

    BLMatrix2D SketchDocument::reflectionAcross(const BLPoint& origin, double axisRadians) {
        BLMatrix2D m = BLMatrix2D::make_translation(-origin.x, -origin.y);
        m.post_rotate(-axisRadians);   // the axis lies along x ...
        m.post_scale(1.0, -1.0);       // ... mirror across it ...
        m.post_rotate(axisRadians);    // ... and put it back
        m.post_translate(origin.x, origin.y);
        return m;
    }

    void SketchDocument::duplicateTransformed(const BLMatrix2D& matrix, double angleScale, double angleOffset, const std::string& description) {
        std::vector<std::unique_ptr<SketchObject>> copies;
        for (std::size_t id : selection_) {
            if (const SketchObject* source = findById(id)) {
                copies.emplace_back(source->clone());
                copies.back()->transform(matrix);
                copies.back()->setFrameAngle(angleScale * source->frameAngle() + angleOffset);
            }
        }
        addObjectsUndoable(std::move(copies), description);
    }

    namespace {
        // The selection as pieces that move together: a group is one piece. `box` is the piece's combined outline bounds.
        struct Piece {
            std::vector<std::size_t> members;
            BLBox box;
        };

        std::vector<Piece> piecesOf(SketchDocument& document, const std::vector<GeometryState>& selected) {
            std::vector<Piece> pieces;
            std::map<std::size_t, std::size_t> indexOf;   // group (or object) key -> index in pieces
            for (const GeometryState& entry : selected) {
                const SketchObject* object = document.findById(entry.id);
                if (object == nullptr) {
                    continue;
                }
                const std::size_t key = object->groups().empty() ? object->id() * 2 : object->groups().front() * 2 + 1;
                const BLBox b = object->geometryBounds();
                auto [it, fresh] = indexOf.emplace(key, pieces.size());
                if (fresh) {
                    pieces.push_back({{entry.id}, b});
                } else {
                    Piece& piece = pieces[it->second];
                    piece.members.push_back(entry.id);
                    piece.box = BLBox(std::min(piece.box.x0, b.x0), std::min(piece.box.y0, b.y0), std::max(piece.box.x1, b.x1), std::max(piece.box.y1, b.y1));
                }
            }
            return pieces;
        }

        const GeometryState* stateOf(const std::vector<GeometryState>& states, std::size_t id) {
            for (const GeometryState& state : states) {
                if (state.id == id) {
                    return &state;
                }
            }
            return nullptr;
        }

        double lowOf(const BLBox& b, bool horizontal) { return horizontal ? b.x0 : b.y0; }
        double highOf(const BLBox& b, bool horizontal) { return horizontal ? b.x1 : b.y1; }
    }

    bool SketchDocument::distributeSelection(AlignMode mode) {
        std::vector<GeometryState> before = snapshotSelection();
        std::vector<Piece> pieces = piecesOf(*this, before);
        if (pieces.size() < 3) {
            return false;
        }
        const bool horizontal = mode == AlignMode::Left || mode == AlignMode::Center || mode == AlignMode::Right;
        const bool low = mode == AlignMode::Left || mode == AlignMode::Top, high = mode == AlignMode::Right || mode == AlignMode::Bottom;
        auto key = [&](const BLBox& b) {
            return low ? lowOf(b, horizontal) : (high ? highOf(b, horizontal) : (lowOf(b, horizontal) + highOf(b, horizontal)) * 0.5);
        };
        std::stable_sort(pieces.begin(), pieces.end(), [&](const Piece& a, const Piece& b) { return key(a.box) < key(b.box); });
        const double first = key(pieces.front().box), last = key(pieces.back().box);
        FrameState frameBefore = frameState();
        std::vector<ObjectPlacement> placements;
        for (std::size_t i = 1; i + 1 < pieces.size(); ++i) {
            const double delta = first + (last - first) * double(i) / double(pieces.size() - 1) - key(pieces[i].box);
            if (std::fabs(delta) < 1e-9) {
                continue;
            }
            for (std::size_t id : pieces[i].members) {
                placements.push_back({stateOf(before, id), horizontal ? BLMatrix2D::make_translation(delta, 0.0) : BLMatrix2D::make_translation(0.0, delta)});
            }
        }
        if (placements.empty()) {
            return false;
        }
        placeObjects(placements);
        quadFrame_.reset();
        static const char* kNames[] = {"Distribute Left", "Distribute Horizontal Centers", "Distribute Right", "Distribute Top", "Distribute Vertical Centers", "Distribute Bottom"};
        commitPlacement(before, kNames[int(mode)], frameBefore);
        return true;
    }

    bool SketchDocument::distributeSpacing(bool horizontal, std::optional<double> gap) {
        std::vector<GeometryState> before = snapshotSelection();
        std::vector<Piece> pieces = piecesOf(*this, before);
        if (pieces.size() < (gap.has_value() ? 2u : 3u) || (gap.has_value() && !std::isfinite(*gap))) {
            return false;
        }
        std::stable_sort(pieces.begin(), pieces.end(), [&](const Piece& a, const Piece& b) { return lowOf(a.box, horizontal) < lowOf(b.box, horizontal); });
        double between = 0.0;
        if (gap.has_value()) {
            between = *gap;
        } else {
            double sizes = 0.0;
            for (const Piece& piece : pieces) {
                sizes += highOf(piece.box, horizontal) - lowOf(piece.box, horizontal);
            }
            const double span = highOf(pieces.back().box, horizontal) - lowOf(pieces.front().box, horizontal);
            between = (span - sizes) / double(pieces.size() - 1);
        }
        FrameState frameBefore = frameState();
        std::vector<ObjectPlacement> placements;
        double at = lowOf(pieces.front().box, horizontal);   // where the next piece starts
        for (const Piece& piece : pieces) {
            const double delta = at - lowOf(piece.box, horizontal);
            at += highOf(piece.box, horizontal) - lowOf(piece.box, horizontal) + between;
            if (std::fabs(delta) < 1e-9) {
                continue;
            }
            for (std::size_t id : piece.members) {
                placements.push_back({stateOf(before, id), horizontal ? BLMatrix2D::make_translation(delta, 0.0) : BLMatrix2D::make_translation(0.0, delta)});
            }
        }
        if (placements.empty()) {
            return false;
        }
        placeObjects(placements);
        quadFrame_.reset();
        commitPlacement(before, horizontal ? "Distribute Horizontal Spacing" : "Distribute Vertical Spacing", frameBefore);
        return true;
    }

    bool SketchDocument::rotateSelectionBy(double radians) {
        BLBox box;
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty() || !selectionBounds(box) || !std::isfinite(radians) || std::fabs(radians) < 1e-12) {
            return false;
        }
        FrameState frameBefore = frameState();
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        BLMatrix2D m = BLMatrix2D::make_translation(-cx, -cy);
        m.post_rotate(radians);
        m.post_translate(cx, cy);
        placeObjects(before, m, 1.0, radians);
        quadFrame_.reset();
        groupAngle_.reset();
        noteTransform(m, radians);
        commitPlacement(before, "Rotate", frameBefore);
        return true;
    }

}
