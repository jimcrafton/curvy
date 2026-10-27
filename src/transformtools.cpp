#include "curvy/canvasview.h"

#include <newui/keyboard_constants.h>

#include <algorithm>
#include <cmath>

// The Rotate, Scale and Reflect tools. They share one gesture: the selection's transform origin (the marker in its frame) is the center
// of the change, a click moves the marker, and a drag from anywhere changes the selection live and is one undo step when let go.

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kMinFactor = 0.01;   // a drag cannot collapse the selection to nothing (or flip it by crossing the origin)

        double clampFactor(double factor) {
            return std::fabs(factor) < kMinFactor ? (factor < 0.0 ? -kMinFactor : kMinFactor) : factor;
        }
    }

    // A press with a transform tool: a selection must already exist (with none, a click on an object selects it so the tool has
    // something to act on).
    void CanvasView::beginTransformTool() {
        if (document_->selection().empty()) {
            if (SketchObject* hit = document_->hitTest(dragStartDoc_, 4.0 / double(zoom_))) {
                document_->setSelection(document_->withGroupMembers({hit->id()}));
            }
            return;
        }
        if (!document_->selectionFrame(dragFrame_)) {
            return;
        }
        dragBefore_ = document_->snapshotSelection(true);
        dragFrameBefore_ = document_->frameState();
        dragOrigin_ = document_->selectionOrigin(dragFrame_);
        dragMatrix_ = BLMatrix2D::make_identity();
        dragAngle_ = 0.0;
        toolAngleScale_ = 1.0;
        toolAngleOffset_ = 0.0;
        dragMoved_ = false;
        drag_ = Drag::TransformTool;
    }

    void CanvasView::applyTransformTool(const BLMatrix2D& matrix, double angleScale, double angleOffset) {
        dragMatrix_ = matrix;
        toolAngleScale_ = angleScale;
        toolAngleOffset_ = angleOffset;
        document_->placeObjects(dragBefore_, matrix, angleScale, angleOffset);
        if (document_->selection().size() > 1) {
            const bool plain = angleScale == 1.0 && angleOffset == 0.0;
            document_->setGroupAngle(plain ? dragFrameBefore_.groupAngle : std::optional<double>(angleScale * dragFrame_.angle + angleOffset));
        }
        if (dragFrameBefore_.quad.has_value()) {
            document_->setQuadFrame(dragFrameBefore_.quad->transformed(matrix));
        }
        document_->touch();
    }

    void CanvasView::updateTransformTool(std::uint32_t /*keys*/) {
        if (!dragMoved_ && std::hypot((dragNowDoc_.x - dragStartDoc_.x) * double(zoom_), (dragNowDoc_.y - dragStartDoc_.y) * double(zoom_)) < 3.0) {
            return;   // still a click
        }
        const BLPoint o = dragOrigin_;
        switch (tool_) {
            case SketchTool::Rotate: {
                double angle = std::atan2(dragNowDoc_.y - o.y, dragNowDoc_.x - o.x) - std::atan2(dragStartDoc_.y - o.y, dragStartDoc_.x - o.x);
                if (dragShift_) {
                    angle = std::round(angle / (kPi / 12.0)) * (kPi / 12.0);
                }
                dragMoved_ = true;
                dragAngle_ = angle;
                applyTransformTool(SketchDocument::rotationAbout(o, angle), 1.0, angle);
                break;
            }
            case SketchTool::Scale: {
                const double r0 = std::hypot(dragStartDoc_.x - o.x, dragStartDoc_.y - o.y);
                if (r0 < 1e-6) {
                    return;   // pressed on the origin: no scale to measure from
                }
                double sx, sy;
                if (dragShift_) {   // each axis follows the pointer on its own
                    const double ax = dragStartDoc_.x - o.x, ay = dragStartDoc_.y - o.y;
                    sx = std::fabs(ax) > 1e-6 ? (dragNowDoc_.x - o.x) / ax : 1.0;
                    sy = std::fabs(ay) > 1e-6 ? (dragNowDoc_.y - o.y) / ay : 1.0;
                } else {
                    sx = sy = std::hypot(dragNowDoc_.x - o.x, dragNowDoc_.y - o.y) / r0;
                }
                dragMoved_ = true;
                dragAngle_ = 0.0;
                applyTransformTool(SketchDocument::scalingAbout(o, clampFactor(sx), clampFactor(sy)), 1.0, 0.0);
                break;
            }
            case SketchTool::Reflect: {
                double axis = std::atan2(dragNowDoc_.y - o.y, dragNowDoc_.x - o.x);
                if (dragShift_) {
                    axis = std::round(axis / (kPi / 4.0)) * (kPi / 4.0);
                }
                dragMoved_ = true;
                dragAngle_ = 0.0;
                applyTransformTool(SketchDocument::reflectionAcross(o, axis), -1.0, 2.0 * axis);   // a mirrored turned box turns the other way
                break;
            }
            default:
                break;
        }
        style().markDirty();
    }

    void CanvasView::finishTransformTool(std::uint32_t keys) {
        if (!dragMoved_) {
            SelectionFrame frame;
            if (document_->selectionFrame(frame)) {
                document_->setSelectionOrigin(frame, snapPoint(dragStartDoc_));   // a click only moves the origin
            }
            return;
        }
        const char* what = tool_ == SketchTool::Rotate ? "Rotate" : (tool_ == SketchTool::Scale ? "Scale" : "Reflect");
        if ((keys & kmAlt) != 0) {
            // Alt: the originals go back and the transformed copies are added instead.
            const BLMatrix2D matrix = dragMatrix_;
            const double scale = toolAngleScale_, offset = toolAngleOffset_;
            document_->previewTransform(dragBefore_, BLMatrix2D::make_identity(), 0.0);
            document_->setGroupAngle(dragFrameBefore_.groupAngle);
            document_->setQuadFrame(dragFrameBefore_.quad);
            document_->duplicateTransformed(matrix, scale, offset, std::string(what) + " Copy");
            return;
        }
        document_->commitPlacement(dragBefore_, what, dragFrameBefore_);
        document_->noteTransform(dragMatrix_, dragAngle_);
    }

    // The line from the origin to the pointer; for Reflect the whole axis, which is the thing being chosen.
    void CanvasView::paintTransformTool(BLContext& ctx) const {
        if (drag_ != Drag::TransformTool || !dragMoved_) {
            return;
        }
        const BLPoint o = toView(dragOrigin_);
        const BLPoint p = toView(dragNowDoc_);
        BLPath line;
        if (tool_ == SketchTool::Reflect) {
            const double len = std::hypot(p.x - o.x, p.y - o.y);
            if (len < 1e-6) {
                return;
            }
            const double dx = (p.x - o.x) / len, dy = (p.y - o.y) / len;
            line.move_to(o.x - dx * 4000.0, o.y - dy * 4000.0);
            line.line_to(o.x + dx * 4000.0, o.y + dy * 4000.0);
        } else {
            line.move_to(o.x, o.y);
            line.line_to(p.x, p.y);
        }
        ctx.set_stroke_style(BLRgba32(0xFF1E6FE0u));
        ctx.set_stroke_width(1.0);
        ctx.stroke_path(line);
    }

}
