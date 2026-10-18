#include "curvy/canvasview.h"

#include <algorithm>
#include <cmath>

// The Scissors, Knife and Eraser tools: the canvas only gathers the pointer's path and shows it; what the cut does to the artwork is the
// document's job (SketchDocument::scissorsCut / knifeCut / erase), as one undo step each.

namespace curvy {

    using namespace newui;

    void CanvasView::setEraserSize(double size) {
        eraserSize_ = std::max(2.0, std::min(200.0, size));
        style().markDirty();
    }

    // The pointer moved with the Knife or Eraser down: another point of its path (not every pixel).
    void CanvasView::cutMove(const BLPoint& point) {
        double gap = 1.5 / double(zoom_);
        if (cutPoints_.empty() || std::hypot(point.x - cutPoints_.back().x, point.y - cutPoints_.back().y) >= gap) {
            cutPoints_.push_back(point);
        }
        dragMoved_ = true;
        style().markDirty();
    }

    // The Knife was let go: the stroke, straightened a little, divides the shapes it crossed.
    void CanvasView::finishKnife() {
        std::vector<BLPoint> stroke = simplifyPolyline(cutPoints_, 1.5 / double(zoom_));
        cutPoints_.clear();
        double length = 0.0;
        for (std::size_t i = 1; i < stroke.size(); ++i) {
            length += std::hypot(stroke[i].x - stroke[i - 1].x, stroke[i].y - stroke[i - 1].y);
        }
        if (stroke.size() < 2 || length * double(zoom_) < 4.0) {
            return;   // a click, not a cut
        }
        document_->knifeCut(stroke);
    }

    // The Eraser was let go: what the pen passed over is gone (a click rubs out a round spot).
    void CanvasView::finishErase() {
        std::vector<BLPoint> stroke = simplifyPolyline(cutPoints_, 0.5 / double(zoom_));
        cutPoints_.clear();
        if (!stroke.empty()) {
            document_->erase(stroke, eraserSize_);
        }
    }

    // While the Knife is down, a thin red line along the cut; while the Eraser is the tool, its pen (a ring at the pointer) and, as it
    // is dragged, the swath it will take.
    void CanvasView::paintCutOverlay(BLContext& ctx) const {
        ctx.save();
        if (drag_ == Drag::Knife && cutPoints_.size() >= 2) {
            BLPath path;
            BLPoint first = toView(cutPoints_[0]);
            path.move_to(first.x, first.y);
            for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
                BLPoint p = toView(cutPoints_[i]);
                path.line_to(p.x, p.y);
            }
            ctx.set_stroke_width(1.5);
            ctx.set_stroke_style(BLRgba32(0xFFD02020u));
            ctx.stroke_path(path);
        }
        if (tool_ == SketchTool::Eraser) {
            const double radius = eraserSize_ * double(zoom_) * 0.5;
            if (drag_ == Drag::Erase && !cutPoints_.empty()) {
                BLPath path;
                BLPoint first = toView(cutPoints_[0]);
                path.move_to(first.x, first.y);
                for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
                    BLPoint p = toView(cutPoints_[i]);
                    path.line_to(p.x, p.y);
                }
                ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
                ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
                ctx.set_stroke_width(radius * 2.0);
                ctx.set_stroke_style(BLRgba32(0x40D02020u));
                if (cutPoints_.size() == 1) {
                    ctx.set_fill_style(BLRgba32(0x40D02020u));
                    ctx.fill_circle(BLCircle(first.x, first.y, radius));
                } else {
                    ctx.stroke_path(path);
                }
            }
            // The pen itself: a ring (white inside dark, so it shows on any color).
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(BLRgba32(0xFFFFFFFFu));
            ctx.stroke_circle(BLCircle(double(lastPointer_.x), double(lastPointer_.y), radius + 1.0));
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.stroke_circle(BLCircle(double(lastPointer_.x), double(lastPointer_.y), radius));
        }
        ctx.restore();
    }


    // ---- Shape Builder ----

    // The pointer moved: every cell the path passes over (sampled along each step, so a fast drag misses none) is picked up.
    void CanvasView::shapeBuildMove(const BLPoint& point) {
        BLPoint from = cutPoints_.empty() ? point : cutPoints_.back();
        double length = std::hypot(point.x - from.x, point.y - from.y);
        int steps = std::max(1, int(std::ceil(length * double(zoom_) / 2.0)));
        for (int i = 1; i <= steps; ++i) {
            BLPoint p(from.x + (point.x - from.x) * double(i) / double(steps), from.y + (point.y - from.y) * double(i) / double(steps));
            for (std::size_t c = 0; c < shapeModel_.cells.size(); ++c) {
                if (!shapeHit_[c] && pointInLoops(p, shapeModel_.cells[c].region)) {
                    shapeHit_[c] = true;
                }
            }
        }
        cutPoints_.push_back(point);
        dragMoved_ = true;
        style().markDirty();
    }

    void CanvasView::finishShapeBuild() {
        std::vector<std::size_t> picked;
        for (std::size_t c = 0; c < shapeHit_.size(); ++c) {
            if (shapeHit_[c]) {
                picked.push_back(c);
            }
        }
        const bool erase = shapeErase_;
        cutPoints_.clear();
        shapeHit_.clear();
        style().markDirty();
        if (!picked.empty()) {
            document_->shapeBuild(picked, erase);
        }
    }

    // The cells picked up so far tinted (blue to join, red to erase), and the path of the pointer.
    void CanvasView::paintShapeBuilder(BLContext& ctx) const {
        ctx.save();
        ctx.set_fill_style(BLRgba32(shapeErase_ ? 0x66D02020u : 0x663B82F6u));
        for (std::size_t c = 0; c < shapeModel_.cells.size() && c < shapeHit_.size(); ++c) {
            if (!shapeHit_[c]) {
                continue;
            }
            BLPath path;
            for (const Loop& loop : shapeModel_.cells[c].region) {
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    BLPoint p = toView(loop[i]);
                    if (i == 0) { path.move_to(p.x, p.y); } else { path.line_to(p.x, p.y); }
                }
                path.close();
            }
            ctx.fill_path(path);
        }
        if (cutPoints_.size() >= 2) {
            BLPath line;
            BLPoint first = toView(cutPoints_[0]);
            line.move_to(first.x, first.y);
            for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
                BLPoint p = toView(cutPoints_[i]);
                line.line_to(p.x, p.y);
            }
            ctx.set_stroke_width(1.5);
            ctx.set_stroke_style(BLRgba32(shapeErase_ ? 0xFFD02020u : 0xFF3B82F6u));
            ctx.stroke_path(line);
        }
        ctx.restore();
    }


    // The lasso so far: a dashed outline closing back to where it began, lightly tinted.
    void CanvasView::paintLasso(BLContext& ctx) const {
        if (cutPoints_.size() < 2) {
            return;
        }
        BLPath path;
        BLPoint first = toView(cutPoints_[0]);
        path.move_to(first.x, first.y);
        for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
            BLPoint p = toView(cutPoints_[i]);
            path.line_to(p.x, p.y);
        }
        path.close();
        ctx.save();
        ctx.set_fill_style(BLRgba32(0x203B82F6u));
        ctx.fill_path(path);
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(BLRgba32(0xFF3B82F6u));
        ctx.stroke_path(path);
        ctx.restore();
    }

}
