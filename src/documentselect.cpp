#include "curvy/sketchdocument.h"

#include "curvy/pathops.h"

#include <algorithm>
#include <cmath>

// Selecting by what objects look like (Select Same, the Magic Wand) or by drawing round them (the Lasso). These only change the selection:
// they are not undo steps.

namespace curvy {

    namespace {
        bool selectable(const SketchObject& object) {
            return object.isVisible() && !object.isLocked();
        }

        bool segmentTouchesRect(BLPoint a, BLPoint b, const BLBox& r) {
            double t0 = 0.0, t1 = 1.0;
            const double d[2] = {b.x - a.x, b.y - a.y};
            const double lo[2] = {r.x0 - a.x, r.y0 - a.y}, hi[2] = {r.x1 - a.x, r.y1 - a.y};
            for (int i = 0; i < 2; ++i) {
                if (d[i] == 0.0) {
                    if (lo[i] > 0.0 || hi[i] < 0.0) {
                        return false;
                    }
                    continue;
                }
                double u = lo[i] / d[i], v = hi[i] / d[i];
                if (u > v) {
                    std::swap(u, v);
                }
                t0 = std::max(t0, u);
                t1 = std::min(t1, v);
                if (t0 > t1) {
                    return false;
                }
            }
            return true;
        }

        // Two paints match when they are of the same kind and, for solid colors, no component of red, green or blue is further apart than
        // `tolerance` (0..1); gradients must be identical.
        bool paintsMatch(const SketchPaint& a, const SketchPaint& b, double tolerance) {
            if (a.kind() != b.kind()) {
                return false;
            }
            if (a.isNone()) {
                return true;
            }
            if (a.isGradient() || a.isPoints()) {
                return a == b;
            }
            float ar, ag, ab, br, bg, bb;
            a.color().toRgb(ar, ag, ab);
            b.color().toRgb(br, bg, bb);
            return std::fabs(double(ar - br)) <= tolerance && std::fabs(double(ag - bg)) <= tolerance && std::fabs(double(ab - bb)) <= tolerance &&
                   std::fabs(double(a.color().alpha() - b.color().alpha())) <= tolerance;
        }

        bool matches(const SketchObject& a, const SketchObject& b, SketchDocument::SameKind kind) {
            switch (kind) {
                case SketchDocument::SameKind::FillColor: return paintsMatch(a.fill(), b.fill(), 1e-4);
                case SketchDocument::SameKind::StrokeColor: return paintsMatch(a.stroke(), b.stroke(), 1e-4);
                case SketchDocument::SameKind::StrokeWeight: return std::fabs(a.strokeWidth() - b.strokeWidth()) < 1e-6;
                case SketchDocument::SameKind::Opacity: return std::fabs(a.opacity() - b.opacity()) < 1e-6;
                case SketchDocument::SameKind::BlendMode: return a.blendMode() == b.blendMode();
                case SketchDocument::SameKind::ObjectType: return a.type() == b.type();
            }
            return false;
        }
    }

    bool SketchDocument::selectSame(SameKind kind) {
        std::vector<const SketchObject*> references;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                references.push_back(object);
            }
        }
        if (references.empty()) {
            return false;
        }
        std::vector<std::size_t> ids;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!selectable(*object)) {
                continue;
            }
            for (const SketchObject* reference : references) {
                if (matches(*object, *reference, kind)) {
                    ids.push_back(object->id());
                    break;
                }
            }
        }
        setSelection(withGroupMembers(ids));
        return true;
    }

    // The Magic Wand: every object whose fill is like `source`'s (its stroke when it has no fill) - colors within `tolerance` of each other.
    bool SketchDocument::selectSimilar(std::size_t sourceId, bool add, double tolerance) {
        const SketchObject* source = findById(sourceId);
        if (source == nullptr || !selectable(*source)) {
            return false;
        }
        const bool byFill = !source->fill().isNone() && source->isClosed();
        std::vector<std::size_t> ids = add ? selection_ : std::vector<std::size_t>();
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!selectable(*object) || std::find(ids.begin(), ids.end(), object->id()) != ids.end()) {
                continue;
            }
            const bool same = byFill ? paintsMatch(object->fill(), source->fill(), tolerance) && !object->fill().isNone()
                                     : paintsMatch(object->stroke(), source->stroke(), tolerance);
            if (same) {
                ids.push_back(object->id());
            }
        }
        setSelection(withGroupMembers(ids));
        return true;
    }

    // The Lasso: the objects the freehand polygon touches - an outline point inside it, or a corner of it on the object.
    bool SketchDocument::selectInPolygon(const std::vector<BLPoint>& polygon, bool add) {
        std::vector<std::size_t> ids = add ? selection_ : std::vector<std::size_t>();
        if (polygon.size() >= 2) {
            Loops lasso{polygon};
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                if (!selectable(*object) || std::find(ids.begin(), ids.end(), object->id()) != ids.end()) {
                    continue;
                }
                bool hit = false;
                std::vector<bool> closed;
                for (const std::vector<BLPoint>& line : flattenToLines(object->geometry(), closed, 0.5)) {
                    for (const BLPoint& p : line) {
                        hit = hit || pointInLoops(p, lasso);
                    }
                }
                for (std::size_t i = 0; !hit && i < polygon.size(); ++i) {
                    hit = object->hitTest(polygon[i], 0.5);
                }
                if (hit) {
                    ids.push_back(object->id());
                }
            }
        }
        setSelection(withGroupMembers(ids));
        return !ids.empty();
    }

    bool SketchDocument::selectInRect(const BLBox& rect, bool add) {
        std::vector<std::size_t> ids = add ? selection_ : std::vector<std::size_t>();
        const BLPoint corners[4] = {BLPoint(rect.x0, rect.y0), BLPoint(rect.x1, rect.y0), BLPoint(rect.x1, rect.y1), BLPoint(rect.x0, rect.y1)};
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!selectable(*object) || std::find(ids.begin(), ids.end(), object->id()) != ids.end()) {
                continue;
            }
            bool hit = false;
            std::vector<bool> closed;
            for (const std::vector<BLPoint>& line : flattenToLines(object->geometry(), closed, 0.5)) {
                for (std::size_t i = 0; !hit && i < line.size(); ++i) {
                    hit = segmentTouchesRect(line[i], line[i > 0 ? i - 1 : 0], rect);
                }
                if (!hit && line.size() > 2) {
                    hit = segmentTouchesRect(line.back(), line.front(), rect) && object->isClosed();
                }
            }
            // The rectangle may sit wholly inside a filled shape.
            for (std::size_t i = 0; !hit && i < 4; ++i) {
                hit = object->hitTest(corners[i], 0.0);
            }
            if (!hit) {
                hit = object->hitTest(BLPoint((rect.x0 + rect.x1) * 0.5, (rect.y0 + rect.y1) * 0.5), 0.0);
            }
            if (hit) {
                ids.push_back(object->id());
            }
        }
        setSelection(withGroupMembers(ids));
        return !ids.empty();
    }

}
