#include "curvy/tracerecttool.h"

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <newui/keyboard_constants.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    void TraceRectTool::cancel() {
        if (start_.has_value() || preview_.has_value() || lastSnap_.snapped()) {
            start_.reset();
            preview_.reset();
            lastSnap_ = SnapResult();
            context_.canvas.style().markDirty();
        }
    }

    TraceItem TraceRectTool::itemTo(const BLPoint& to, std::uint32_t keys) const {
        const BLPoint from = start_.value_or(to);
        double dx = to.x - from.x, dy = to.y - from.y;
        if ((keys & kmShift) != 0) {
            const double side = (std::max)(std::abs(dx), std::abs(dy));
            dx = dx < 0.0 ? -side : side;
            dy = dy < 0.0 ? -side : side;
        }
        if ((keys & kmAlt) != 0) {
            return TraceItem::rect(from.x - dx, from.y - dy, from.x + dx, from.y + dy);
        }
        return TraceItem::rect(from.x, from.y, from.x + dx, from.y + dy);
    }

    BLPoint TraceRectTool::snapped(const ToolEvent& event) {
        lastSnap_ = SnapResult();
        if ((event.keys & kmCtrl) != 0) {
            return event.document;
        }
        const double zoom = double(context_.canvas.zoom());
        lastSnap_ = snapper_.snap(event.document, zoom);
        if (!lastSnap_.snapped()) {
            std::vector<BLPoint> own;
            if (start_.has_value()) {
                own.push_back(*start_);
            }
            lastSnap_ = snapper_.align(event.document, zoom, own);
        }
        return lastSnap_.point;
    }

    bool TraceRectTool::inputDown(const ToolEvent& event) {
        if (event.secondary || !context_.documentOpen() || context_.graphShown() || context_.menuOpen()) {
            return false;
        }
        start_ = snapped(event);
        preview_.reset();
        context_.canvas.style().markDirty();
        return true;
    }

    void TraceRectTool::inputMove(const ToolEvent& event) {
        const BLPoint to = snapped(event);
        if (!start_.has_value()) {
            context_.canvas.style().markDirty();   // the mark follows the pointer before the press too
            return;
        }
        preview_ = itemTo(to, event.keys);
        context_.canvas.style().markDirty();
    }

    void TraceRectTool::inputUp(const ToolEvent& event) {
        if (!start_.has_value()) {
            return;
        }
        const TraceItem item = itemTo(snapped(event), event.keys);
        const double minimum = 2.0 / double(context_.canvas.zoom());   // a click is not a rectangle
        cancel();
        if (item.width < minimum || item.height < minimum) {
            return;
        }
        try {
            context_.document.addTraceItem(item);
            context_.say("Trace rectangle added");
        } catch (const std::runtime_error& problem) {
            context_.say(problem.what());
        }
    }

    bool TraceRectTool::keyDown(const ToolKeyEvent& event) {
        if (event.vk == vkEscape && start_.has_value()) {
            cancel();
            return true;
        }
        return false;
    }

    void TraceRectTool::paintOverlay(BLContext& ctx) const {
        const BLPoint origin = context_.canvas.toView(BLPoint(0.0, 0.0));
        const double zoom = double(context_.canvas.zoom());
        if (preview_.has_value()) {
            TraceLayer::paintItem(ctx, *preview_, origin, zoom);
        }
        Snapper::paintMarker(ctx, lastSnap_, origin, zoom);
    }

}
