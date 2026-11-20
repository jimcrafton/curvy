#include "curvy/traceselecttool.h"

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"
#include "curvy/theme.h"

#include <newui/keyboard_constants.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kHitPixels = 4.0;
        constexpr double kDragPixels = 3.0;   // a press that moves less than this is a click
    }

    void TraceSelectTool::activate() {
        selected_.clear();
        mode_ = Mode::Idle;
        context_.canvas.style().markDirty();
    }

    void TraceSelectTool::cancel() {
        if (mode_ != Mode::Idle) {
            mode_ = Mode::Idle;
            lastSnap_ = SnapResult();
            context_.canvas.style().markDirty();
        }
    }

    void TraceSelectTool::dropStale() {
        const std::size_t count = context_.document.trace().items().size();
        selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [count](std::size_t index) { return index >= count; }), selected_.end());
    }

    std::vector<std::size_t> TraceSelectTool::selection() const {
        const std::size_t count = context_.document.trace().items().size();
        std::vector<std::size_t> result;
        for (const std::size_t index : selected_) {
            if (index < count) {
                result.push_back(index);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool TraceSelectTool::selected(std::size_t index) const {
        return std::find(selected_.begin(), selected_.end(), index) != selected_.end();
    }

    std::size_t TraceSelectTool::hitAt(const BLPoint& document) const {
        return context_.document.trace().hitTest(document, kHitPixels / double(context_.canvas.zoom()));
    }

    BLBox TraceSelectTool::marqueeBox() const {
        return BLBox((std::min)(pressAt_.x, pointer_.x), (std::min)(pressAt_.y, pointer_.y), (std::max)(pressAt_.x, pointer_.x),
                     (std::max)(pressAt_.y, pointer_.y));
    }

    bool TraceSelectTool::inputDown(const ToolEvent& event) {
        if (event.secondary || !context_.documentOpen() || context_.graphShown() || context_.menuOpen()) {
            return false;
        }
        dropStale();
        const bool additive = (event.keys & kmShift) != 0;
        pressAt_ = pointer_ = event.document;
        const std::size_t hit = hitAt(event.document);
        if (hit < context_.document.trace().items().size()) {
            if (additive) {
                if (selected(hit)) {
                    selected_.erase(std::find(selected_.begin(), selected_.end(), hit));
                    mode_ = Mode::Idle;
                    context_.canvas.style().markDirty();
                    return true;
                }
                selected_.push_back(hit);
            } else if (!selected(hit)) {
                selected_ = {hit};
            }
            mode_ = Mode::Pressed;
        } else {
            if (!additive) {
                selected_.clear();
            }
            mode_ = Mode::Marquee;
        }
        context_.canvas.style().markDirty();
        return true;
    }

    BLPoint TraceSelectTool::dragDelta(const ToolEvent& event) {
        const BLPoint travel(event.document.x - pressAt_.x, event.document.y - pressAt_.y);
        lastSnap_ = SnapResult();
        if ((event.keys & kmCtrl) != 0) {
            return travel;
        }
        const std::vector<std::size_t> chosen = selection();
        std::vector<BLPoint> points;
        for (const std::size_t index : chosen) {
            for (const TraceSnapPoint& point : context_.document.trace().items()[index].snapPoints()) {
                points.push_back(point.point);
            }
        }
        const double zoom = double(context_.canvas.zoom());
        Snapper::MoveResult result = snapper_.snapMove(points, travel, zoom, chosen);
        if (!result.hit.snapped()) {
            result = snapper_.alignMove(points, travel, zoom, chosen);
        }
        lastSnap_ = result.hit;
        return result.delta;
    }

    void TraceSelectTool::inputMove(const ToolEvent& event) {
        if (mode_ == Mode::Idle) {
            return;
        }
        pointer_ = event.document;
        if (mode_ == Mode::Pressed) {
            const double distance = std::hypot(pointer_.x - pressAt_.x, pointer_.y - pressAt_.y) * double(context_.canvas.zoom());
            if (distance >= kDragPixels) {
                mode_ = Mode::Moving;
            }
        }
        if (mode_ == Mode::Moving) {
            delta_ = dragDelta(event);
        }
        if (mode_ != Mode::Pressed) {
            context_.canvas.style().markDirty();
        }
    }

    void TraceSelectTool::finishMarquee() {
        const BLBox box = marqueeBox();
        const std::vector<TraceItem>& items = context_.document.trace().items();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const BLBox bounds = items[i].bounds();
            if (bounds.x0 >= box.x0 && bounds.y0 >= box.y0 && bounds.x1 <= box.x1 && bounds.y1 <= box.y1 && !selected(i)) {
                selected_.push_back(i);
            }
        }
    }

    void TraceSelectTool::inputUp(const ToolEvent& event) {
        if (mode_ == Mode::Idle) {
            return;
        }
        pointer_ = event.document;
        const Mode mode = mode_;
        mode_ = Mode::Idle;
        lastSnap_ = SnapResult();
        if (mode == Mode::Moving) {
            const BLPoint delta = dragDelta(event);
            try {
                context_.document.moveTraceItems(selection(), delta.x, delta.y);
            } catch (const std::runtime_error& problem) {
                context_.say(problem.what());
            }
        } else if (mode == Mode::Marquee) {
            finishMarquee();
        }
        context_.canvas.style().markDirty();
    }

    bool TraceSelectTool::deleteSelection() {
        const std::vector<std::size_t> chosen = selection();
        if (chosen.empty()) {
            return false;
        }
        try {
            context_.document.removeTraceItems(chosen);
        } catch (const std::runtime_error& problem) {
            context_.say(problem.what());
            return false;
        }
        selected_.clear();
        context_.say(chosen.size() == 1 ? "Trace shape deleted" : std::to_string(chosen.size()) + " trace shapes deleted");
        context_.canvas.style().markDirty();
        return true;
    }

    bool TraceSelectTool::keyDown(const ToolKeyEvent& event) {
        if (event.vk == vkEscape) {
            if (mode_ != Mode::Idle) {
                cancel();
                return true;
            }
            if (!selected_.empty()) {
                selected_.clear();
                context_.canvas.style().markDirty();
                return true;
            }
            return false;
        }
        if ((event.vk == vkDelete || event.vk == vkBackSpace) && mode_ == Mode::Idle) {
            return deleteSelection();
        }
        return false;
    }

    void TraceSelectTool::paintOverlay(BLContext& ctx) const {
        const BLPoint origin = context_.canvas.toView(BLPoint(0.0, 0.0));
        const double zoom = double(context_.canvas.zoom());
        const BLRgba32 accent(Theme::argb(Theme::current().accent));
        const std::vector<TraceItem>& items = context_.document.trace().items();
        const bool moving = mode_ == Mode::Moving;
        for (const std::size_t index : selection()) {
            const TraceItem& item = items[index];
            TraceLayer::paintItemAs(ctx, moving ? item.translated(delta_.x, delta_.y) : item, origin, zoom, accent, 2.0);
        }
        Snapper::paintMarker(ctx, lastSnap_, origin, zoom);
        if (mode_ == Mode::Marquee) {
            const BLBox box = marqueeBox();
            const BLRect rect(origin.x + box.x0 * zoom, origin.y + box.y0 * zoom, (box.x1 - box.x0) * zoom, (box.y1 - box.y0) * zoom);
            ctx.set_fill_style(BLRgba32((Theme::argb(Theme::current().accent) & 0x00FFFFFFu) | 0x28000000u));
            ctx.fill_rect(rect);
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(accent);
            ctx.stroke_rect(rect);
        }
    }

}
