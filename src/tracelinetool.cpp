#include "curvy/tracelinetool.h"

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <newui/keyboard_constants.h>

#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    void TraceLineTool::cancel() {
        if (start_.has_value() || preview_.has_value() || lastSnap_.snapped()) {
            start_.reset();
            preview_.reset();
            references_.clear();
            perpendicularTo_.reset();
            lastSnap_ = SnapResult();
            context_.canvas.style().markDirty();
        }
    }

    TraceItem TraceLineTool::itemTo(const BLPoint& to, std::uint32_t keys) const {
        const BLPoint from = start_.value_or(to);
        double dx = to.x - from.x, dy = to.y - from.y;
        if ((keys & kmShift) != 0) {
            constexpr double kStep = 3.14159265358979323846 / 4.0;
            const double length = std::hypot(dx, dy);
            const double angle = std::round(std::atan2(dy, dx) / kStep) * kStep;
            dx = length * std::cos(angle);
            dy = length * std::sin(angle);
        }
        return TraceItem::line(from.x, from.y, from.x + dx, from.y + dy);
    }

    BLPoint TraceLineTool::snapped(const ToolEvent& event) {
        lastSnap_ = SnapResult();
        if ((event.keys & kmCtrl) != 0) {
            return event.document;
        }
        const double zoom = double(context_.canvas.zoom());
        lastSnap_ = snapper_.snap(event.document, zoom);
        if (!lastSnap_.snapped()) {
            // Nothing to land on: line up with the x or y of another shape's point, or of this line's own start.
            std::vector<BLPoint> own;
            if (start_.has_value()) {
                own.push_back(*start_);
            }
            lastSnap_ = snapper_.align(event.document, zoom, own);
        }
        return lastSnap_.point;
    }

    bool TraceLineTool::inputDown(const ToolEvent& event) {
        if (event.secondary || !context_.documentOpen() || context_.graphShown() || context_.menuOpen()) {
            return false;
        }
        start_ = snapped(event);
        preview_.reset();
        references_.clear();
        perpendicularTo_.reset();
        if (lastSnap_.snapped()) {
            findReferences();
        }
        context_.canvas.style().markDirty();
        return true;
    }

    void TraceLineTool::findReferences() {
        const TraceLayer& trace = context_.document.trace();
        if (!trace.visible() || !start_.has_value()) {
            return;
        }
        for (const TraceItem& item : trace.items()) {
            for (const TraceSegment& segment : item.segments()) {
                const double dx = segment.b.x - segment.a.x, dy = segment.b.y - segment.a.y;
                const double length = std::hypot(dx, dy);
                const double along = ((start_->x - segment.a.x) * dx + (start_->y - segment.a.y) * dy) / (length == 0.0 ? 1.0 : length);
                const double off = length == 0.0 ? 1.0 : std::abs((start_->x - segment.a.x) * dy - (start_->y - segment.a.y) * dx) / length;
                if (length > 0.0 && off < 1e-4 && along > -1e-4 && along < length + 1e-4) {
                    references_.push_back(BLPoint(dx / length, dy / length));
                }
            }
        }
    }

    BLPoint TraceLineTool::endPoint(const ToolEvent& event) {
        perpendicularTo_.reset();
        if ((event.keys & kmShift) != 0 || !start_.has_value()) {
            lastSnap_ = SnapResult();   // a snap would pull the end off the 45 degree ray
            return (event.keys & kmShift) != 0 ? event.document : snapped(event);
        }
        const BLPoint end = snapped(event);
        if ((event.keys & kmCtrl) != 0) {
            return end;
        }
        // Landing on a corner, midpoint, centre or crossing is deliberate; only a weak snap (an edge, a guide or a line-up) gives way to the square lock.
        if (lastSnap_.snapped() && lastSnap_.kind != SnapResult::Kind::Edge && lastSnap_.kind != SnapResult::Kind::Guide &&
            lastSnap_.kind != SnapResult::Kind::Align) {
            return end;
        }
        const BLPoint raw(event.document.x - start_->x, event.document.y - start_->y);
        const double length = std::hypot(raw.x, raw.y);
        const double limit = std::sin(kPerpendicularDegrees * 3.14159265358979323846 / 180.0);
        if (length * double(context_.canvas.zoom()) < 4.0) {
            return end;
        }
        for (const BLPoint& reference : references_) {
            const double square = (raw.x * reference.x + raw.y * reference.y) / length;   // 0 when exactly perpendicular
            if (std::abs(square) < limit) {
                const double reach = raw.y * reference.x - raw.x * reference.y;   // how far along the perpendicular
                perpendicularTo_ = reference;
                const BLPoint onRay(start_->x - reference.y * reach, start_->y + reference.x * reach);
                // Two snaps at once: square to the edge, and where that ray meets another shape or a guide.
                const double sign = reach < 0.0 ? -1.0 : 1.0;
                lastSnap_ = snapper_.snapOnRay(*start_, BLPoint(-reference.y * sign, reference.x * sign), onRay, double(context_.canvas.zoom()));
                return lastSnap_.snapped() ? lastSnap_.point : onRay;
            }
        }
        return end;
    }

    void TraceLineTool::inputMove(const ToolEvent& event) {
        const BLPoint to = endPoint(event);
        if (start_.has_value()) {
            preview_ = itemTo(to, event.keys);
        }
        context_.canvas.style().markDirty();
    }

    void TraceLineTool::inputUp(const ToolEvent& event) {
        if (!start_.has_value()) {
            return;
        }
        const TraceItem item = itemTo(endPoint(event), event.keys);
        const double minimum = 2.0 / double(context_.canvas.zoom());   // a click is not a line
        cancel();
        if (std::hypot(item.width, item.height) < minimum) {
            return;
        }
        try {
            context_.document.addTraceItem(item);
            context_.say("Trace line added");
        } catch (const std::runtime_error& problem) {
            context_.say(problem.what());
        }
    }

    bool TraceLineTool::keyDown(const ToolKeyEvent& event) {
        if (event.vk == vkEscape && start_.has_value()) {
            cancel();
            return true;
        }
        return false;
    }

    void TraceLineTool::paintOverlay(BLContext& ctx) const {
        const BLPoint origin = context_.canvas.toView(BLPoint(0.0, 0.0));
        const double zoom = double(context_.canvas.zoom());
        if (preview_.has_value()) {
            TraceLayer::paintItem(ctx, *preview_, origin, zoom);
        }
        Snapper::paintMarker(ctx, lastSnap_, origin, zoom);
        if (perpendicularTo_.has_value() && start_.has_value() && preview_.has_value()) {
            const double length = std::hypot(preview_->width, preview_->height);
            if (length > 0.0) {
                const BLPoint a = *perpendicularTo_;
                const BLPoint b(preview_->width / length, preview_->height / length);
                constexpr double r = 9.0;
                const double x = origin.x + start_->x * zoom, y = origin.y + start_->y * zoom;
                BLPath mark;
                mark.move_to(x + a.x * r, y + a.y * r);
                mark.line_to(x + (a.x + b.x) * r, y + (a.y + b.y) * r);
                mark.line_to(x + b.x * r, y + b.y * r);
                ctx.set_stroke_width(1.5);
                ctx.set_stroke_style(BLRgba32(0xFF00C853u));
                ctx.stroke_path(mark);
            }
        }
    }

}
