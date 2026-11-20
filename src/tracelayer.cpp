#include "curvy/tracelayer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace curvy {

    std::string TraceItem::kindName(Kind kind) {
        return kind == Kind::Line ? "line" : "rect";
    }

    TraceItem::Kind TraceItem::kindFromName(const std::string& name, bool* known) {
        if (known != nullptr) {
            *known = name == "rect" || name == "line";
        }
        return name == "line" ? Kind::Line : Kind::Rect;
    }

    TraceItem TraceItem::line(double x1, double y1, double x2, double y2) {
        TraceItem item;
        item.kind = Kind::Line;
        item.x = x1;
        item.y = y1;
        item.width = x2 - x1;
        item.height = y2 - y1;
        return item;
    }

    TraceItem TraceItem::rect(double x1, double y1, double x2, double y2) {
        TraceItem item;
        item.kind = Kind::Rect;
        item.x = (std::min)(x1, x2);
        item.y = (std::min)(y1, y2);
        item.width = std::abs(x2 - x1);
        item.height = std::abs(y2 - y1);
        return item;
    }

    bool TraceItem::valid() const {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)) {
            return false;
        }
        if (kind == Kind::Line) {
            return width != 0.0 || height != 0.0;
        }
        return width > 0.0 && height > 0.0;
    }

    BLBox TraceItem::bounds() const {
        if (kind == Kind::Line) {
            return BLBox((std::min)(x, endX()), (std::min)(y, endY()), (std::max)(x, endX()), (std::max)(y, endY()));
        }
        return BLBox(x, y, x + width, y + height);
    }

    BLPath TraceItem::path() const {
        BLPath path;
        if (kind == Kind::Line) {
            path.move_to(x, y);
            path.line_to(endX(), endY());
        } else {
            path.add_rect(BLRect(x, y, width, height));
        }
        return path;
    }

    double TraceItem::distanceTo(const BLPoint& point) const {
        double best = std::numeric_limits<double>::infinity();
        for (const TraceSegment& segment : segments()) {
            const double dx = segment.b.x - segment.a.x, dy = segment.b.y - segment.a.y;
            const double lengthSquared = dx * dx + dy * dy;
            const double t = lengthSquared == 0.0 ? 0.0 : std::clamp(((point.x - segment.a.x) * dx + (point.y - segment.a.y) * dy) / lengthSquared, 0.0, 1.0);
            best = (std::min)(best, std::hypot(point.x - (segment.a.x + t * dx), point.y - (segment.a.y + t * dy)));
        }
        return best;
    }

    TraceItem TraceItem::translated(double dx, double dy) const {
        TraceItem moved = *this;
        moved.x += dx;
        moved.y += dy;
        return moved;
    }

    std::vector<TraceSnapPoint> TraceItem::snapPoints() const {
        if (kind == Kind::Line) {
            using LineKind = TraceSnapPoint::Kind;
            return {{BLPoint(x, y), LineKind::Corner}, {BLPoint(endX(), endY()), LineKind::Corner},
                    {BLPoint(x + width / 2.0, y + height / 2.0), LineKind::Midpoint}};
        }
        const double right = x + width, bottom = y + height, midX = x + width / 2.0, midY = y + height / 2.0;
        using Kind = TraceSnapPoint::Kind;
        return {{BLPoint(x, y), Kind::Corner},         {BLPoint(right, y), Kind::Corner},      {BLPoint(right, bottom), Kind::Corner},
                {BLPoint(x, bottom), Kind::Corner},    {BLPoint(midX, y), Kind::Midpoint},     {BLPoint(right, midY), Kind::Midpoint},
                {BLPoint(midX, bottom), Kind::Midpoint}, {BLPoint(x, midY), Kind::Midpoint},   {BLPoint(midX, midY), Kind::Center}};
    }

    std::vector<TraceSegment> TraceItem::segments() const {
        if (kind == Kind::Line) {
            return {{BLPoint(x, y), BLPoint(endX(), endY())}};
        }
        const double right = x + width, bottom = y + height;
        return {{BLPoint(x, y), BLPoint(right, y)}, {BLPoint(right, y), BLPoint(right, bottom)},
                {BLPoint(right, bottom), BLPoint(x, bottom)}, {BLPoint(x, bottom), BLPoint(x, y)}};
    }

    bool TraceItem::operator==(const TraceItem& other) const {
        return kind == other.kind && x == other.x && y == other.y && width == other.width && height == other.height;
    }

    std::size_t TraceLayer::add(const TraceItem& item) {
        if (!item.valid()) {
            throw std::runtime_error("the trace shape needs finite numbers and a size");
        }
        items_.push_back(item);
        return items_.size() - 1;
    }

    void TraceLayer::set(std::size_t index, const TraceItem& item) {
        if (index >= items_.size()) {
            throw std::runtime_error("no such trace shape");
        }
        if (!item.valid()) {
            throw std::runtime_error("the trace shape needs finite numbers and a size");
        }
        items_[index] = item;
    }

    void TraceLayer::remove(std::size_t index) {
        if (index >= items_.size()) {
            throw std::runtime_error("no such trace shape");
        }
        items_.erase(items_.begin() + std::ptrdiff_t(index));
    }

    std::size_t TraceLayer::hitTest(const BLPoint& point, double tolerance) const {
        for (std::size_t i = items_.size(); i-- > 0;) {
            if (items_[i].distanceTo(point) <= tolerance) {
                return i;
            }
        }
        return items_.size();
    }

    void TraceLayer::setOpacity(double opacity) {
        if (!std::isfinite(opacity)) {
            throw std::runtime_error("the trace opacity must be a finite number");
        }
        opacity_ = std::clamp(opacity, kMinOpacity, 1.0);
    }

    void TraceLayer::paintSheet(BLContext& ctx, const BLRect& area) const {
        if (!visible_) {
            return;
        }
        const std::uint32_t alpha = std::uint32_t(std::lround(opacity_ * 255.0));
        ctx.set_fill_style(BLRgba32(kSheetColor | (alpha << 24)));
        ctx.fill_rect(area);
    }

    void TraceLayer::paintItem(BLContext& ctx, const TraceItem& item, const BLPoint& origin, double zoom) {
        paintItemAs(ctx, item, origin, zoom, BLRgba32(kColor | (std::uint32_t(std::lround(kLineOpacity * 255.0)) << 24)), kWidth);
    }

    void TraceLayer::paintItemAs(BLContext& ctx, const TraceItem& item, const BLPoint& origin, double zoom, const BLRgba32& color, double width) {
        if (item.kind == TraceItem::Kind::Line) {
            ctx.set_stroke_width(width);
            ctx.set_stroke_style(color);
            ctx.stroke_line(BLPoint(origin.x + item.x * zoom, origin.y + item.y * zoom),
                            BLPoint(origin.x + item.endX() * zoom, origin.y + item.endY() * zoom));
            return;
        }
        const double left = std::floor(origin.x + item.x * zoom) + 0.5;
        const double top = std::floor(origin.y + item.y * zoom) + 0.5;
        const double right = std::floor(origin.x + (item.x + item.width) * zoom) + 0.5;
        const double bottom = std::floor(origin.y + (item.y + item.height) * zoom) + 0.5;
        ctx.set_stroke_width(width);
        ctx.set_stroke_style(color);
        ctx.stroke_rect(BLRect(left, top, right - left, bottom - top));
    }

    void TraceLayer::paint(BLContext& ctx, const BLPoint& origin, double zoom) const {
        if (!visible_) {
            return;
        }
        for (const TraceItem& item : items_) {
            paintItem(ctx, item, origin, zoom);
        }
    }

}
