// CanvasView: the drawing tools beyond the rectangle / ellipse / line drags in canvasview.cpp - rounded rectangle,
// polygon, star, pencil and pen. Their geometry is in shapetools.h; this file is the interaction: what a drag
// previews, what a finished drag adds to the document, and the keys that adjust or end it.

#include "curvy/canvasview.h"
#include "curvy/shapetools.h"

#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kStarInnerRatio = 0.5;

        BLBox boxOf(const BLPoint& a, const BLPoint& b) {
            return BLBox(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        }
    }

    // ---- settings ----

    void CanvasView::setCornerRadius(double radius) {
        const double before = cornerRadius_;
        cornerRadius_ = std::max(0.0, radius);
        style().markDirty();
        if (cornerRadius_ != before) {
            onToolDefaultsChanged(*this);
        }
    }

    void CanvasView::setStarInnerPercent(int percent) {
        const int before = starInner_;
        starInner_ = std::max(10, std::min(90, percent));
        style().markDirty();
        if (starInner_ != before) {
            onToolDefaultsChanged(*this);
        }
    }

    void CanvasView::setPencilSmoothing(int value) {
        const int before = pencilSmoothing_;
        pencilSmoothing_ = std::max(0, std::min(100, value));
        if (pencilSmoothing_ != before) {
            onToolDefaultsChanged(*this);
        }
    }

    void CanvasView::setPolygonSides(int sides) {
        const int before = polygonSides_;
        polygonSides_ = std::max(3, std::min(60, sides));
        style().markDirty();
        if (polygonSides_ != before) {
            onToolDefaultsChanged(*this);
        }
    }

    void CanvasView::setArcSweep(double radians) {
        arcSweep_ = std::max(kPi / 12.0, std::min(2.0 * kPi, radians));
        style().markDirty();
    }

    void CanvasView::setStarPoints(int points) {
        const int before = starPoints_;
        starPoints_ = std::max(3, std::min(40, points));
        style().markDirty();
        if (starPoints_ != before) {
            onToolDefaultsChanged(*this);
        }
    }

    // A new object gets the document's default look (a path with no fill and no stroke would be invisible, so
    // that case gets a plain black stroke).
    void CanvasView::addDrawn(SketchObject* object, const char* name, const char* undoName, bool filled) {
        object->setName(name);
        object->setFill(filled ? document_->defaultFill() : SketchPaint::none());   // an open line is not filled
        SketchPaint stroke = document_->defaultStroke();
        if (stroke.isNone() && (object->type() == SketchObjectType::Path || object->type() == SketchObjectType::Polyline)) {
            stroke = SketchPaint::solid(SketchColor::gray(0.0f));
        }
        object->setStroke(stroke);
        object->setStrokeWidth(document_->defaultStrokeWidth());
        object->setStrokeOptions(document_->defaultStrokeOptions());
        document_->addUndoable(object, undoName);
    }

    // ---- rounded rectangle, polygon and star (a drag, like the rectangle) ----

    // The outline a drag from dragStartDoc_ to `to` makes, in document units: polygon and star grow from the center
    // (the first corner follows the drag, or points up with Shift), a rounded rectangle is a box (square with Shift).
    BLPath CanvasView::shapeOutline(const BLPoint& to, bool shift, bool& valid) const {
        BLPath path;
        valid = false;
        BLPoint a = dragStartDoc_;
        if (tool_ == SketchTool::RoundRect) {
            double w = to.x - a.x, h = to.y - a.y;
            if (shift) {
                double side = std::max(std::fabs(w), std::fabs(h));
                w = w < 0 ? -side : side;
                h = h < 0 ? -side : side;
            }
            BLBox box = boxOf(a, BLPoint(a.x + w, a.y + h));
            double bw = box.x1 - box.x0, bh = box.y1 - box.y0;
            if (bw < 2.0 || bh < 2.0) {
                return path;
            }
            double radius = std::min(cornerRadius_, std::min(bw, bh) * 0.5);
            path.add_round_rect(BLRoundRect(box.x0, box.y0, bw, bh, radius));
            valid = true;
            return path;
        }
        double radius = std::hypot(to.x - a.x, to.y - a.y);
        if (radius < 2.0) {
            return path;
        }
        if (tool_ == SketchTool::Circle) {
            path.add_circle(BLCircle(a.x, a.y, radius));
            valid = true;
            return path;
        }
        double first = shift ? -kPi / 2.0 : std::atan2(to.y - a.y, to.x - a.x);
        if (tool_ == SketchTool::Arc) {
            ArcShape arc;
            arc.center = a;
            arc.radius = radius;
            arc.start = first;
            arc.sweep = arcSweep_;
            path = arcPath(arc);
            valid = true;
            return path;
        }
        std::vector<BLPoint> points = tool_ == SketchTool::Star ? curvy::starPoints(a, radius, double(starInner_) / 100.0, starPoints_, first)
                                                                 : regularPolygonPoints(a, radius, polygonSides_, first);
        path.move_to(points[0].x, points[0].y);
        for (std::size_t i = 1; i < points.size(); ++i) {
            path.line_to(points[i].x, points[i].y);
        }
        path.close();
        valid = true;
        return path;
    }

    void CanvasView::finishShapeDraw(bool shift) {
        bool valid = false;
        BLPath outline = shapeOutline(dragNowDoc_, shift, valid);
        if (!valid) {
            return;
        }
        if (tool_ == SketchTool::RoundRect) {
            BLBox box;
            outline.get_bounding_box(&box);
            double bw = box.x1 - box.x0, bh = box.y1 - box.y0;
            addDrawn(SketchObject::makeRoundRect(box.x0, box.y0, bw, bh, std::min(cornerRadius_, std::min(bw, bh) * 0.5)), "Rounded Rectangle", "Draw Rounded Rectangle");
            return;
        }
        if (tool_ == SketchTool::Circle) {
            BLBox box;
            outline.get_bounding_box(&box);
            double r = (box.x1 - box.x0) * 0.5;
            addDrawn(SketchObject::makeEllipse(box.x0 + r, box.y0 + r, r, r), "Circle", "Draw Circle");
            return;
        }
        if (tool_ == SketchTool::Arc) {
            addDrawn(SketchObject::makePath(outline), "Arc", "Draw Arc", false);   // an open arc is a line: no fill
            return;
        }
        // Polygon and star are polygons: read the corners back out of the outline.
        std::vector<BLPoint> corners;
        const BLPoint* vertices = outline.vertex_data();
        const std::uint8_t* commands = outline.command_data();
        for (std::size_t i = 0; i < outline.size(); ++i) {
            if (commands[i] == BL_PATH_CMD_MOVE || commands[i] == BL_PATH_CMD_ON) {
                corners.push_back(vertices[i]);
            }
        }
        addDrawn(SketchObject::makePolygon(corners), tool_ == SketchTool::Star ? "Star" : "Polygon",
            tool_ == SketchTool::Star ? "Draw Star" : "Draw Polygon");
    }

    void CanvasView::paintShapePreview(BLContext& ctx) const {
        bool valid = false;
        BLPath outline = shapeOutline(dragNowDoc_, dragShift_, valid);
        if (!valid) {
            return;
        }
        BLMatrix2D toScreen(double(zoom_), 0.0, 0.0, double(zoom_), toView(BLPoint(0.0, 0.0)).x, toView(BLPoint(0.0, 0.0)).y);
        outline.transform(toScreen);
        ctx.stroke_path(outline);
    }

    // Up / Down while dragging a polygon, star or rounded rectangle changes how many sides / points, or the radius.
    bool CanvasView::adjustShapeParameter(int direction) {
        if (drag_ != Drag::Draw) {
            return false;
        }
        switch (tool_) {
            case SketchTool::Polygon: setPolygonSides(polygonSides_ + direction); return true;
            case SketchTool::Star: setStarPoints(starPoints_ + direction); return true;
            case SketchTool::RoundRect: setCornerRadius(cornerRadius_ + 2.0 * direction); return true;
            case SketchTool::Arc: setArcSweep(arcSweep_ + direction * kPi / 12.0); return true;   // 15 degrees a press
            default: return false;
        }
    }

    // ---- pencil: a freehand drag, smoothed when released ----

    void CanvasView::pencilMove(const BLPoint& point) {
        double gap = 1.5 / double(zoom_);
        if (pencilPoints_.empty() || std::hypot(point.x - pencilPoints_.back().x, point.y - pencilPoints_.back().y) >= gap) {
            pencilPoints_.push_back(point);
        }
        dragMoved_ = true;
        style().markDirty();
    }

    void CanvasView::finishPencil() {
        std::vector<BLPoint> points = std::move(pencilPoints_);
        pencilPoints_.clear();
        if (points.size() < 3) {
            return;
        }
        double length = 0.0;
        for (std::size_t i = 1; i < points.size(); ++i) {
            length += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        }
        if (length * double(zoom_) < 4.0) {
            return;   // a click, not a stroke
        }
        // Fit as few curves as keep the line within about 2.5 screen pixels of what was drawn: the result has only the points it needs,
        // with handles that follow the stroke (sharp corners stay sharp).
        // A hand shakes: smooth the samples lightly first (the two ends stay put), so the fit follows the stroke rather than the tremor.
        points = smoothPolyline(points, 2);
        // The smoothing option moves the fit tolerance from 0.5 to 5.5 screen pixels; the default 40 is the 2.5 it always was.
        std::vector<PenAnchor> anchors = fitCurve(points, (0.5 + 0.05 * double(pencilSmoothing_)) / double(zoom_));
        if (anchors.size() < 2) {
            return;
        }
        if (tool_ == SketchTool::Paintbrush) {
            // The Paintbrush draws the same line, stroked by the active brush: a weight of 1 shows artwork at its own size, a nib wants a real
            // weight.
            SketchObject* object = SketchObject::makePath(penPath(anchors, false));
            object->setName("Brush Path");
            object->setFill(SketchPaint::none());
            object->setStroke(document_->defaultStroke().isNone() ? SketchPaint::solid(SketchColor::gray(0.0f)) : document_->defaultStroke());
            StrokeOptions options = document_->defaultStrokeOptions();
            options.brush = document_->findBrush(document_->activeBrush());
            object->setStrokeOptions(options);
            object->setStrokeWidth(options.brush && options.brush->kind != BrushKind::Calligraphic ? 1.0 : std::max(2.0, document_->defaultStrokeWidth()));
            document_->addUndoable(object, "Draw with Paintbrush");
            return;
        }
        addDrawn(SketchObject::makePath(penPath(anchors, false)), "Pencil Path", "Draw with Pencil", false);
    }

    void CanvasView::paintPencilPreview(BLContext& ctx) const {
        if (pencilPoints_.size() < 2) {
            return;
        }
        BLPath path;
        BLPoint first = toView(pencilPoints_[0]);
        path.move_to(first.x, first.y);
        for (std::size_t i = 1; i < pencilPoints_.size(); ++i) {
            BLPoint p = toView(pencilPoints_[i]);
            path.line_to(p.x, p.y);
        }
        ctx.stroke_path(path);
    }

    // ---- pen: click for a corner, drag for a smooth point, click the first point to close ----

    namespace {
        constexpr double kPenHit = 6.0;   // view pixels
    }

    bool CanvasView::penBegin(const Point& pt, std::uint32_t keys) {
        BLPoint doc = snapPoint(toDocument(pt));
        BLPoint view = toView(doc);
        auto closeTo = [&view](const BLPoint& docPoint, const CanvasView& self) {
            BLPoint v = self.toView(docPoint);
            return std::fabs(v.x - view.x) <= kPenHit && std::fabs(v.y - view.y) <= kPenHit;
        };
        if (penAnchors_.size() >= 3 && closeTo(penAnchors_.front().point, *this)) {
            finishPen(true);   // back on the first point closes the shape
            return true;
        }
        if (penAnchors_.size() >= 2 && closeTo(penAnchors_.back().point, *this)) {
            finishPen(false);  // a second click on the last point (a double-click) ends the open path
            return true;
        }
        (void) keys;
        PenAnchor anchor;
        anchor.point = doc;
        penAnchors_.push_back(anchor);
        drag_ = Drag::PenHandle;
        dragStartDoc_ = doc;
        style().markDirty();
        return true;
    }

    // While the button is down after placing a point, the drag pulls its handles out: the forward one follows the
    // pointer, the backward one is its mirror (Alt breaks the mirror: only the forward handle moves).
    void CanvasView::penDrag(const BLPoint& pointerDoc, bool alt) {
        if (penAnchors_.empty()) {
            return;
        }
        PenAnchor& anchor = penAnchors_.back();
        double dx = pointerDoc.x - anchor.point.x, dy = pointerDoc.y - anchor.point.y;
        if (std::hypot(dx, dy) * double(zoom_) < 3.0) {
            anchor.hasIn = anchor.hasOut = false;   // not dragged far enough: still a corner
        } else {
            anchor.out = pointerDoc;
            anchor.hasOut = true;
            if (!alt) {
                anchor.in = BLPoint(anchor.point.x - dx, anchor.point.y - dy);
                anchor.hasIn = true;
            }
        }
        style().markDirty();
    }

    void CanvasView::finishPen(bool close) {
        std::vector<PenAnchor> anchors = std::move(penAnchors_);
        penAnchors_.clear();
        drag_ = Drag::None;
        style().markDirty();
        if (anchors.size() < 2) {
            return;
        }
        BLPath path = penPath(anchors, close);
        SketchObject* object = SketchObject::makePath(path);
        addDrawn(object, "Path", close ? "Draw Closed Path" : "Draw Path", close);
    }

    void CanvasView::cancelPen() {
        penAnchors_.clear();
        if (drag_ == Drag::PenHandle) {
            drag_ = Drag::None;
        }
        style().markDirty();
    }

    // Backspace takes back the last point.
    void CanvasView::penUndoLast() {
        if (!penAnchors_.empty()) {
            penAnchors_.pop_back();
        }
        style().markDirty();
    }

    void CanvasView::paintPenOverlay(BLContext& ctx) const {
        if (penAnchors_.empty()) {
            return;
        }
        // The path so far, plus the segment that follows the pointer.
        std::vector<PenAnchor> preview = penAnchors_;
        if (drag_ != Drag::PenHandle) {
            PenAnchor next;
            next.point = snapPoint(dragNowDoc_);
            preview.push_back(next);
        }
        BLPath path = penPath(preview, false);
        BLMatrix2D toScreen(double(zoom_), 0.0, 0.0, double(zoom_), toView(BLPoint(0.0, 0.0)).x, toView(BLPoint(0.0, 0.0)).y);
        path.transform(toScreen);
        ctx.stroke_path(path);
        // Anchors as small squares (the first one larger, it closes the path), the last one's handles as lines and dots.
        for (std::size_t i = 0; i < penAnchors_.size(); ++i) {
            BLPoint v = toView(penAnchors_[i].point);
            double half = i == 0 ? 4.0 : 3.0;
            ctx.set_fill_style(i + 1 == penAnchors_.size() ? BLRgba32(0xFF1E6FE0u) : BLRgba32(0xFFFFFFFFu));
            ctx.fill_rect(BLRect(std::floor(v.x) - half, std::floor(v.y) - half, half * 2.0 + 1.0, half * 2.0 + 1.0));
            ctx.stroke_rect(BLRect(std::floor(v.x) - half + 0.5, std::floor(v.y) - half + 0.5, half * 2.0, half * 2.0));
        }
        const PenAnchor& last = penAnchors_.back();
        for (int side = 0; side < 2; ++side) {
            bool has = side == 0 ? last.hasIn : last.hasOut;
            if (!has) {
                continue;
            }
            BLPoint a = toView(last.point), h = toView(side == 0 ? last.in : last.out);
            BLPath line;
            line.move_to(a.x, a.y);
            line.line_to(h.x, h.y);
            ctx.stroke_path(line);
            ctx.set_fill_style(BLRgba32(0xFF1E6FE0u));
            ctx.fill_circle(BLCircle(h.x, h.y, 3.0));
        }
    }

    // ---- direct select ----

    namespace {
        bool collinearOpposite(const PenAnchor& a) {
            if (!a.hasIn || !a.hasOut) {
                return false;
            }
            double ix = a.in.x - a.point.x, iy = a.in.y - a.point.y, ox = a.out.x - a.point.x, oy = a.out.y - a.point.y;
            double li = std::hypot(ix, iy), lo = std::hypot(ox, oy);
            if (li < 1e-9 || lo < 1e-9) {
                return false;
            }
            return std::fabs(ix * oy - iy * ox) < 1e-3 * li * lo && (ix * ox + iy * oy) < 0.0;
        }
    }

    bool CanvasView::directHit(const Point& pt, DirectRef& out) const {
        const double reach = kHandleHit + 2.0;
        auto closeTo = [&pt, reach](const BLPoint& v) {
            return std::fabs(double(pt.x) - v.x) <= reach && std::fabs(double(pt.y) - v.y) <= reach;
        };
        const std::vector<std::size_t>& selection = document_->selection();
        for (std::size_t k = selection.size(); k-- > 0;) {
            const SketchObject* object = document_->findById(selection[k]);
            if (object == nullptr) {
                continue;
            }
            std::vector<SubPath> subs = pathToSubPaths(object->geometry());
            // The handles of the picked anchor win over anchors underneath them.
            if (directSelected_.valid && directSelected_.object == object->id() && directSelected_.sub < subs.size() &&
                directSelected_.anchor < subs[directSelected_.sub].anchors.size()) {
                const PenAnchor& a = subs[directSelected_.sub].anchors[directSelected_.anchor];
                for (int part = 1; part <= 2; ++part) {
                    bool has = part == 1 ? a.hasIn : a.hasOut;
                    if (has && closeTo(toView(part == 1 ? a.in : a.out))) {
                        out = directSelected_;
                        out.part = part;
                        return true;
                    }
                }
            }
            for (std::size_t s = 0; s < subs.size(); ++s) {
                for (std::size_t i = 0; i < subs[s].anchors.size(); ++i) {
                    if (closeTo(toView(subs[s].anchors[i].point))) {
                        out.object = object->id();
                        out.sub = s;
                        out.anchor = i;
                        out.part = 0;
                        out.valid = true;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void CanvasView::directMouseDown(const Point& pt, std::uint32_t keys) {
        DirectRef ref;
        if (directHit(pt, ref)) {
            SketchObject* object = document_->findById(ref.object);
            std::vector<SubPath> subs = object != nullptr ? pathToSubPaths(object->geometry()) : std::vector<SubPath>();
            if (ref.sub >= subs.size() || ref.anchor >= subs[ref.sub].anchors.size()) {
                return;
            }
            if (ref.part == 0) {
                directSelected_ = ref;
                const PenAnchor& anchor = subs[ref.sub].anchors[ref.anchor];
                if ((keys & kmAlt) != 0 && !anchor.hasIn && !anchor.hasOut) {
                    ref.part = 3;   // Alt-drag on a corner pulls its handles out
                }
            }
            directDragRef_ = ref;
            directPaths_ = std::move(subs);
            directOriginal_ = directPaths_[ref.sub].anchors[ref.anchor];
            dragBefore_ = document_->snapshotSelection();
            dragFrameBefore_ = document_->frameState();
            drag_ = Drag::DirectEdit;
            dragMoved_ = false;
            style().markDirty();
            return;
        }
        // Not on a point: pick the object under the pointer (which shows its anchors), or nothing.
        directSelected_ = DirectRef();
        SketchObject* hit = document_->hitTest(dragStartDoc_, 4.0 / double(zoom_));
        if (hit != nullptr) {
            document_->setSelection({hit->id()});
        } else if ((keys & kmShift) == 0) {
            document_->clearSelection();
        }
        style().markDirty();
    }

    void CanvasView::directDrag(const BLPoint& pointerDoc, bool alt) {
        SketchObject* object = document_->findById(directDragRef_.object);
        if (object == nullptr || directDragRef_.sub >= directPaths_.size() ||
            directDragRef_.anchor >= directPaths_[directDragRef_.sub].anchors.size()) {
            return;
        }
        dragMoved_ = true;
        PenAnchor& a = directPaths_[directDragRef_.sub].anchors[directDragRef_.anchor];
        const PenAnchor& o = directOriginal_;
        double dx = pointerDoc.x - dragStartDoc_.x, dy = pointerDoc.y - dragStartDoc_.y;
        switch (directDragRef_.part) {
            case 0:   // the anchor moves with its handles
                a.point = BLPoint(o.point.x + dx, o.point.y + dy);
                a.in = BLPoint(o.in.x + dx, o.in.y + dy);
                a.out = BLPoint(o.out.x + dx, o.out.y + dy);
                break;
            case 3:   // a corner becomes a smooth point: the out handle follows the pointer, the in handle is its mirror
                a.out = pointerDoc;
                a.in = BLPoint(2.0 * a.point.x - pointerDoc.x, 2.0 * a.point.y - pointerDoc.y);
                a.hasIn = a.hasOut = true;
                break;
            default: {
                bool out = directDragRef_.part == 2;
                (out ? a.out : a.in) = pointerDoc;
                // A smooth point stays smooth: the other handle keeps its length and turns to stay opposite (Alt breaks that).
                if (!alt && collinearOpposite(o)) {
                    const BLPoint& otherOriginal = out ? o.in : o.out;
                    double length = std::hypot(otherOriginal.x - o.point.x, otherOriginal.y - o.point.y);
                    double vx = a.point.x - pointerDoc.x, vy = a.point.y - pointerDoc.y;
                    double vl = std::hypot(vx, vy);
                    if (vl > 1e-9) {
                        (out ? a.in : a.out) = BLPoint(a.point.x + vx / vl * length, a.point.y + vy / vl * length);
                    }
                }
                break;
            }
        }
        object->setGeometry(subPathsToPath(directPaths_));
        document_->touch();
        style().markDirty();
    }

    void CanvasView::finishDirectDrag() {
        directPaths_.clear();
        if (dragMoved_) {
            document_->commitGeometry(dragBefore_, directDragRef_.part == 0 ? "Move Anchor Point" : "Edit Curve", dragFrameBefore_);
        }
        dragBefore_.clear();
        style().markDirty();
    }

    // Delete with an anchor picked removes it (and the outline, or the whole object, if too little would be left).
    bool CanvasView::deleteDirectAnchor() {
        if (!directSelected_.valid) {
            return false;
        }
        SketchObject* object = document_->findById(directSelected_.object);
        if (object == nullptr) {
            directSelected_ = DirectRef();
            return false;
        }
        std::vector<SubPath> subs = pathToSubPaths(object->geometry());
        if (directSelected_.sub >= subs.size() || directSelected_.anchor >= subs[directSelected_.sub].anchors.size()) {
            directSelected_ = DirectRef();
            return false;
        }
        SubPath& sub = subs[directSelected_.sub];
        sub.anchors.erase(sub.anchors.begin() + std::ptrdiff_t(directSelected_.anchor));
        if (sub.anchors.size() < (sub.closed ? 3u : 2u)) {
            subs.erase(subs.begin() + std::ptrdiff_t(directSelected_.sub));
        }
        directSelected_ = DirectRef();
        if (subs.empty()) {
            document_->deleteSelection();
            return true;
        }
        std::vector<GeometryState> before = document_->snapshotSelection();
        FrameState frameBefore = document_->frameState();
        object->setGeometry(subPathsToPath(subs));
        document_->touch();
        document_->commitGeometry(before, "Delete Anchor Point", frameBefore);
        style().markDirty();
        return true;
    }

    void CanvasView::paintDirectOverlay(BLContext& ctx) const {
        const BLRgba32 accent(0xFF1E6FE0u);
        for (std::size_t id : document_->selection()) {
            const SketchObject* object = document_->findById(id);
            if (object == nullptr) {
                continue;
            }
            std::vector<SubPath> subs = pathToSubPaths(object->geometry());
            for (std::size_t s = 0; s < subs.size(); ++s) {
                for (std::size_t i = 0; i < subs[s].anchors.size(); ++i) {
                    const PenAnchor& a = subs[s].anchors[i];
                    BLPoint v = toView(a.point);
                    bool picked = directSelected_.valid && directSelected_.object == id && directSelected_.sub == s && directSelected_.anchor == i;
                    if (picked) {
                        // Its handles: a line from the anchor to each, a round knob at the end.
                        for (int part = 1; part <= 2; ++part) {
                            if (!(part == 1 ? a.hasIn : a.hasOut)) {
                                continue;
                            }
                            BLPoint h = toView(part == 1 ? a.in : a.out);
                            BLPath line;
                            line.move_to(v.x, v.y);
                            line.line_to(h.x, h.y);
                            ctx.set_stroke_style(accent);
                            ctx.stroke_path(line);
                            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                            ctx.fill_circle(BLCircle(h.x, h.y, 3.5));
                            ctx.stroke_circle(BLCircle(h.x, h.y, 3.5));
                        }
                    }
                    BLRect box(std::floor(v.x) - 3.0, std::floor(v.y) - 3.0, 7.0, 7.0);
                    ctx.set_fill_style(picked ? accent : BLRgba32(0xFFFFFFFFu));
                    ctx.set_stroke_style(accent);
                    ctx.fill_rect(box);
                    ctx.stroke_rect(BLRect(box.x + 0.5, box.y + 0.5, box.w - 1.0, box.h - 1.0));
                }
            }
        }
    }

    // ---- live shape handles ----

    namespace {
        constexpr double kLiveReach = 7.0;         // view pixels
        constexpr double kPointsPerPixel = 14.0;   // a drag of this many pixels adds or removes one point / side
        constexpr double kRadiusHandleInset = 14.0;   // view pixels: where the corner handle sits relative to the arc

        double wrapSweep(double diff) {   // into (0, 2 pi]
            diff = std::fmod(diff, 2.0 * kPi);
            if (diff < 0.0) { diff += 2.0 * kPi; }
            if (diff < 1e-3) { diff = 2.0 * kPi; }
            return diff;
        }
    }

    bool CanvasView::liveShape(LiveShape& out) const {
        if (document_ == nullptr || tool_ != SketchTool::Select || document_->selection().size() != 1) {
            return false;
        }
        const SketchObject* object = document_->findById(document_->selection().front());
        if (object == nullptr || object->isLocked() || !object->isVisible()) {
            return false;
        }
        out = LiveShape();
        out.object = object->id();
        if (object->type() == SketchObjectType::Polygon) {
            std::vector<SubPath> subs = pathToSubPaths(object->geometry());
            if (subs.size() != 1 || !subs[0].closed) {
                return false;
            }
            std::vector<BLPoint> corners;
            for (const PenAnchor& a : subs[0].anchors) {
                if (a.hasIn || a.hasOut) {
                    return false;
                }
                corners.push_back(a.point);
            }
            if (!analyzeRegularShape(corners, out.regular)) {
                return false;
            }
            out.kind = out.regular.star ? LiveKind::Star : LiveKind::Polygon;
            return true;
        }
        if (object->type() == SketchObjectType::RoundRect) {
            if (!analyzeRoundRect(object->geometry(), object->frameAngle(), out.rect)) {
                return false;
            }
            out.kind = LiveKind::RoundRect;
            return true;
        }
        if (object->type() == SketchObjectType::Path && analyzeArc(object->geometry(), out.arc)) {
            out.kind = LiveKind::Arc;
            return true;
        }
        return false;
    }

    BLPoint CanvasView::liveHandlePoint(const LiveShape& shape, int handle) const {
        switch (handle) {
            case kLivePoints: {   // inside the shape, opposite its first corner
                const RegularShape& r = shape.regular;
                return toView(BLPoint(r.center.x - 0.28 * r.outer * std::cos(r.firstAngle), r.center.y - 0.28 * r.outer * std::sin(r.firstAngle)));
            }
            case kLiveInner: {    // on the first inner corner
                const RegularShape& r = shape.regular;
                double a = r.firstAngle + kPi / double(r.points);
                return toView(BLPoint(r.center.x + r.inner * std::cos(a), r.center.y + r.inner * std::sin(a)));
            }
            case kLiveRadius: {   // inside the top-left corner, on the diagonal, a fixed distance past the arc
                const RoundRectShape& r = shape.rect;
                double off = kRadiusHandleInset / double(zoom_);
                double lx = r.box.x0 + r.radius + off, ly = r.box.y0 + r.radius + off;
                double c = std::cos(r.angle), s = std::sin(r.angle);
                return toView(BLPoint(lx * c - ly * s, lx * s + ly * c));
            }
            case kLiveArcStart:
                return toView(shape.arc.startPoint());
            case kLiveArcEnd:
                return toView(shape.arc.endPoint());
            default:
                return BLPoint(0.0, 0.0);
        }
    }

    int CanvasView::liveHandleAt(const Point& pt, const LiveShape& shape) const {
        int handles[3] = {kLiveNone, kLiveNone, kLiveNone};
        switch (shape.kind) {
            case LiveKind::Star: handles[0] = kLiveInner; handles[1] = kLivePoints; break;
            case LiveKind::Polygon: handles[0] = kLivePoints; break;
            case LiveKind::RoundRect: handles[0] = kLiveRadius; break;
            case LiveKind::Arc: handles[0] = kLiveArcEnd; handles[1] = kLiveArcStart; break;
            default: return kLiveNone;
        }
        for (int handle : handles) {
            if (handle == kLiveNone) {
                continue;
            }
            BLPoint p = liveHandlePoint(shape, handle);
            if (std::fabs(double(pt.x) - p.x) <= kLiveReach && std::fabs(double(pt.y) - p.y) <= kLiveReach) {
                return handle;
            }
        }
        return kLiveNone;
    }

    void CanvasView::beginLiveDrag(int handle, const LiveShape& shape, const Point& pt) {
        liveStart_ = shape;
        liveHandle_ = handle;
        liveStartX_ = pt.x;
        dragBefore_ = document_->snapshotSelection();
        dragFrameBefore_ = document_->frameState();
        dragMoved_ = false;
        switch (handle) {
            case kLivePoints: liveWhat_ = shape.kind == LiveKind::Star ? "Change Star Points" : "Change Polygon Sides"; break;
            case kLiveInner: liveWhat_ = "Change Star Inner Radius"; break;
            case kLiveRadius: liveWhat_ = "Change Corner Radius"; break;
            default: liveWhat_ = "Change Arc"; break;
        }
        liveLabel_.clear();
        drag_ = Drag::LiveShape;
        style().markDirty();
    }

    void CanvasView::updateLiveDrag(const BLPoint& pointerDoc, const Point& pointerView) {
        SketchObject* object = document_->findById(liveStart_.object);
        if (object == nullptr) {
            return;
        }
        char text[64];
        BLPath path;
        switch (liveHandle_) {
            case kLivePoints: {
                RegularShape shape = liveStart_.regular;
                int limit = shape.star ? 40 : 60;
                shape.points = std::max(3, std::min(limit, liveStart_.regular.points + int(std::lround((double(pointerView.x) - double(liveStartX_)) / kPointsPerPixel))));
                path = regularShapePath(shape);
                std::snprintf(text, sizeof(text), shape.star ? "%d points" : "%d sides", shape.points);
                break;
            }
            case kLiveInner: {
                RegularShape shape = liveStart_.regular;
                double ratio = std::hypot(pointerDoc.x - shape.center.x, pointerDoc.y - shape.center.y) / shape.outer;
                ratio = std::max(0.05, std::min(0.95, ratio));
                shape.inner = ratio * shape.outer;
                path = regularShapePath(shape);
                std::snprintf(text, sizeof(text), "inner %d%%", int(std::lround(ratio * 100.0)));
                break;
            }
            case kLiveRadius: {
                RoundRectShape shape = liveStart_.rect;
                double c = std::cos(-shape.angle), s = std::sin(-shape.angle);
                double lx = pointerDoc.x * c - pointerDoc.y * s, ly = pointerDoc.x * s + pointerDoc.y * c;
                double along = ((lx - shape.box.x0) + (ly - shape.box.y0)) * 0.5 - kRadiusHandleInset / double(zoom_);
                double limit = std::min(shape.box.x1 - shape.box.x0, shape.box.y1 - shape.box.y0) * 0.5;
                shape.radius = std::max(0.0, std::min(limit, along));
                path = roundRectPath(shape);
                std::snprintf(text, sizeof(text), "corner %.0f", shape.radius);
                break;
            }
            default: {   // an end of an arc follows the pointer's angle around the center
                ArcShape arc = liveStart_.arc;
                double angle = std::atan2(pointerDoc.y - arc.center.y, pointerDoc.x - arc.center.x);
                double direction = arc.sweep < 0.0 ? -1.0 : 1.0;
                if (liveHandle_ == kLiveArcStart) {
                    double end = arc.start + arc.sweep;   // the other end stays where it is
                    arc.start = angle;
                    arc.sweep = wrapSweep((end - angle) * direction) * direction;
                } else {
                    arc.sweep = wrapSweep((angle - arc.start) * direction) * direction;
                }
                path = arcPath(arc);
                std::snprintf(text, sizeof(text), "%.0f deg", std::fabs(arc.sweep) * 180.0 / kPi);
                break;
            }
        }
        liveLabel_ = text;
        object->setGeometry(path);
        dragMoved_ = true;
        document_->touch();
        style().markDirty();
    }

    void CanvasView::finishLiveDrag() {
        if (dragMoved_) {
            document_->commitGeometry(dragBefore_, liveWhat_, dragFrameBefore_);
        }
        dragBefore_.clear();
        liveHandle_ = kLiveNone;
        liveLabel_.clear();
        style().markDirty();
    }

    void CanvasView::paintLiveOverlay(BLContext& ctx) const {
        LiveShape shape;
        if ((drag_ != Drag::None && drag_ != Drag::LiveShape)) {
            return;
        }
        bool dragging = drag_ == Drag::LiveShape;
        if (dragging) {
            if (document_->findById(liveStart_.object) == nullptr || !liveShape(shape)) {
                shape = liveStart_;   // the outline changes shape while dragging; fall back to what was recognised
            }
        } else if (!liveShape(shape)) {
            return;
        }
        const BLRgba32 accent(0xFF1E6FE0u);
        auto knob = [&](int handle, bool diamond) {
            BLPoint p = liveHandlePoint(shape, handle);
            ctx.set_stroke_style(BLRgba32(0xFFFFFFFFu));
            ctx.set_stroke_width(2.0);
            ctx.set_fill_style(accent);
            if (diamond) {
                BLPath d;
                d.move_to(p.x, p.y - 6.0);
                d.line_to(p.x + 6.0, p.y);
                d.line_to(p.x, p.y + 6.0);
                d.line_to(p.x - 6.0, p.y);
                d.close();
                ctx.stroke_path(d);
                ctx.fill_path(d);
            } else {
                ctx.stroke_circle(BLCircle(p.x, p.y, 5.0));
                ctx.fill_circle(BLCircle(p.x, p.y, 5.0));
            }
            ctx.set_stroke_width(1.0);
        };
        auto guide = [&](const BLPoint& from, const BLPoint& to) {
            BLPath line;
            line.move_to(from.x, from.y);
            line.line_to(to.x, to.y);
            BLArray<double> dashes;
            dashes.append(3.0);
            dashes.append(3.0);
            ctx.set_stroke_dash_array(dashes);
            ctx.set_stroke_style(accent);
            ctx.stroke_path(line);
            ctx.set_stroke_dash_array(BLArray<double>());
        };
        ctx.save();
        switch (shape.kind) {
            case LiveKind::Star:
                guide(toView(shape.regular.center), liveHandlePoint(shape, kLiveInner));
                knob(kLiveInner, false);
                knob(kLivePoints, true);
                break;
            case LiveKind::Polygon:
                knob(kLivePoints, true);
                break;
            case LiveKind::RoundRect: {
                const RoundRectShape& r = shape.rect;
                double c = std::cos(r.angle), s = std::sin(r.angle);
                guide(toView(BLPoint(r.box.x0 * c - r.box.y0 * s, r.box.x0 * s + r.box.y0 * c)), liveHandlePoint(shape, kLiveRadius));
                knob(kLiveRadius, false);
                break;
            }
            case LiveKind::Arc:
                guide(toView(shape.arc.center), liveHandlePoint(shape, kLiveArcStart));
                guide(toView(shape.arc.center), liveHandlePoint(shape, kLiveArcEnd));
                knob(kLiveArcStart, false);
                knob(kLiveArcEnd, false);
                break;
            default:
                break;
        }
        // While dragging, the value sits next to the handle in a small dark label.
        if (dragging && !liveLabel_.empty()) {
            BLPoint at = liveHandlePoint(shape, liveHandle_);
            Font font = FontManager::getSystemFont(SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont != nullptr && blFont->is_valid()) {
                BLGlyphBuffer glyphs;
                glyphs.set_utf8_text(liveLabel_.c_str(), liveLabel_.size());
                blFont->shape(glyphs);
                BLTextMetrics metrics;
                blFont->get_text_metrics(glyphs, metrics);
                double w = metrics.advance.x + 12.0, h = 20.0;
                ctx.set_fill_style(BLRgba32(0xE01A1A1Au));
                ctx.fill_round_rect(BLRoundRect(at.x + 10.0, at.y - h - 6.0, w, h, 3.0));
                ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                ctx.fill_glyph_run(BLPoint(at.x + 16.0, at.y - 12.0), *blFont, glyphs.glyph_run());
            }
        }
        ctx.restore();
    }

}
