#include "curvy/snapper.h"

#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace curvy {

    namespace {

        bool excluded(const std::vector<std::size_t>& exclude, std::size_t index) {
            return std::find(exclude.begin(), exclude.end(), index) != exclude.end();
        }

        BLPoint nearestOnSegment(const TraceSegment& segment, const BLPoint& point) {
            const double dx = segment.b.x - segment.a.x, dy = segment.b.y - segment.a.y;
            const double lengthSquared = dx * dx + dy * dy;
            if (lengthSquared == 0.0) {
                return segment.a;
            }
            const double t = std::clamp(((point.x - segment.a.x) * dx + (point.y - segment.a.y) * dy) / lengthSquared, 0.0, 1.0);
            return BLPoint(segment.a.x + t * dx, segment.a.y + t * dy);
        }

        double distance(const BLPoint& a, const BLPoint& b) {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        // Where two segments cross; false for parallel ones and for segments that miss each other.
        bool crossing(const TraceSegment& p, const TraceSegment& q, BLPoint& at) {
            const double rx = p.b.x - p.a.x, ry = p.b.y - p.a.y, sx = q.b.x - q.a.x, sy = q.b.y - q.a.y;
            const double denominator = rx * sy - ry * sx;
            if (denominator == 0.0) {
                return false;
            }
            const double t = ((q.a.x - p.a.x) * sy - (q.a.y - p.a.y) * sx) / denominator;
            const double u = ((q.a.x - p.a.x) * ry - (q.a.y - p.a.y) * rx) / denominator;
            if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) {
                return false;
            }
            at = BLPoint(p.a.x + t * rx, p.a.y + t * ry);
            return true;
        }

        SnapResult::Kind kindOf(TraceSnapPoint::Kind kind) {
            switch (kind) {
                case TraceSnapPoint::Kind::Midpoint:
                    return SnapResult::Kind::Midpoint;
                case TraceSnapPoint::Kind::Center:
                    return SnapResult::Kind::Center;
                default:
                    return SnapResult::Kind::Corner;
            }
        }

    }

    const char* SnapResult::kindName(Kind kind) {
        switch (kind) {
            case Kind::Corner:
                return "corner";
            case Kind::Midpoint:
                return "midpoint";
            case Kind::Center:
                return "center";
            case Kind::Intersection:
                return "intersection";
            case Kind::Edge:
                return "edge";
            case Kind::Guide:
                return "guide";
            case Kind::Align:
                return "align";
            default:
                return "none";
        }
    }

    SnapResult Snapper::snap(const BLPoint& point, double zoom, const std::vector<std::size_t>& exclude) const {
        SnapResult result;
        result.point = point;
        if (!(zoom > 0.0)) {
            return result;
        }
        const double reach = kPixels / zoom;
        const TraceLayer& trace = document_.trace();

        if (trace.visible()) {
            double best = reach;
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (const TraceSnapPoint& target : trace.items()[i].snapPoints()) {
                    const double d = distance(point, target.point);
                    if (d <= best) {
                        best = d;
                        result.point = target.point;
                        result.kind = kindOf(target.kind);
                    }
                }
            }
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (std::size_t j = i + 1; j < trace.items().size(); ++j) {
                    if (excluded(exclude, j)) {
                        continue;
                    }
                    for (const TraceSegment& first : trace.items()[i].segments()) {
                        for (const TraceSegment& second : trace.items()[j].segments()) {
                            BLPoint at;
                            if (crossing(first, second, at) && distance(point, at) <= best) {
                                best = distance(point, at);
                                result.point = at;
                                result.kind = SnapResult::Kind::Intersection;
                            }
                        }
                    }
                }
            }
            if (result.snapped()) {
                return result;
            }
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (const TraceSegment& segment : trace.items()[i].segments()) {
                    const BLPoint onEdge = nearestOnSegment(segment, point);
                    const double d = distance(point, onEdge);
                    if (d <= best) {
                        best = d;
                        result.point = onEdge;
                        result.kind = SnapResult::Kind::Edge;
                    }
                }
            }
        }

        if (document_.guidesVisible()) {
            std::optional<double> x, y;
            double bestX = reach, bestY = reach;
            for (const Guide& guide : document_.guides()) {
                const double d = std::abs((guide.vertical ? point.x : point.y) - guide.position);
                if (guide.vertical && d <= bestX) {
                    bestX = d;
                    x = guide.position;
                } else if (!guide.vertical && d <= bestY) {
                    bestY = d;
                    y = guide.position;
                }
            }
            if (x.has_value()) {
                result.point.x = *x;
                result.kind = SnapResult::Kind::Guide;
            }
            if (y.has_value()) {
                result.point.y = *y;
                result.kind = SnapResult::Kind::Guide;
            }
        }
        return result;
    }

    SnapResult Snapper::align(const BLPoint& point, double zoom, const std::vector<BLPoint>& extra, const std::vector<std::size_t>& exclude) const {
        SnapResult result;
        result.point = point;
        if (!(zoom > 0.0)) {
            return result;
        }
        const double reach = kPixels / zoom;
        double bestX = reach, bestY = reach;
        std::optional<BLPoint> matchX, matchY;
        const auto consider = [&](const BLPoint& target) {
            if (std::abs(point.x - target.x) <= bestX) {
                bestX = std::abs(point.x - target.x);
                matchX = target;
            }
            if (std::abs(point.y - target.y) <= bestY) {
                bestY = std::abs(point.y - target.y);
                matchY = target;
            }
        };
        const TraceLayer& trace = document_.trace();
        if (trace.visible()) {
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (const TraceSnapPoint& target : trace.items()[i].snapPoints()) {
                    consider(target.point);
                }
            }
        }
        for (const BLPoint& target : extra) {
            consider(target);
        }
        if (matchX.has_value()) {
            result.point.x = matchX->x;
            result.reference = *matchX;
            result.references = 1;
            result.kind = SnapResult::Kind::Align;
        }
        if (matchY.has_value()) {
            result.point.y = matchY->y;
            (result.references == 0 ? result.reference : result.reference2) = *matchY;
            ++result.references;
            result.kind = SnapResult::Kind::Align;
        }
        return result;
    }

    SnapResult Snapper::snapOnRay(const BLPoint& origin, const BLPoint& direction, const BLPoint& point, double zoom,
                                  const std::vector<std::size_t>& exclude) const {
        SnapResult result;
        result.point = point;
        if (!(zoom > 0.0)) {
            return result;
        }
        double best = kPixels / zoom;
        const auto consider = [&](const BLPoint& at, SnapResult::Kind kind, const BLPoint* lineUp = nullptr) {
            const bool ahead = (at.x - origin.x) * direction.x + (at.y - origin.y) * direction.y > 1e-3;   // not the origin itself
            const double d = distance(point, at);
            // A line-up only wins when strictly nearer, so a crossing or a guide at the same spot keeps its own mark.
            if (ahead && (lineUp != nullptr ? d < best : d <= best)) {
                best = d;
                result.point = at;
                result.kind = kind;
                result.references = 0;
                if (lineUp != nullptr) {
                    result.reference = *lineUp;
                    result.references = 1;
                }
            }
        };
        const TraceLayer& trace = document_.trace();
        if (trace.visible()) {
            const TraceSegment ray{origin, BLPoint(origin.x + direction.x * 1e6, origin.y + direction.y * 1e6)};
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (const TraceSegment& segment : trace.items()[i].segments()) {
                    BLPoint at;
                    if (crossing(ray, segment, at)) {
                        consider(at, SnapResult::Kind::Intersection);
                    }
                }
            }
        }
        if (document_.guidesVisible()) {
            for (const Guide& guide : document_.guides()) {
                const double along = guide.vertical ? direction.x : direction.y;
                if (along == 0.0) {
                    continue;
                }
                const double t = (guide.position - (guide.vertical ? origin.x : origin.y)) / along;
                consider(BLPoint(origin.x + direction.x * t, origin.y + direction.y * t), SnapResult::Kind::Guide);
            }
        }
        if (trace.visible()) {
            for (std::size_t i = 0; i < trace.items().size(); ++i) {
                if (excluded(exclude, i)) {
                    continue;
                }
                for (const TraceSnapPoint& target : trace.items()[i].snapPoints()) {
                    if (direction.y != 0.0) {
                        const double t = (target.point.y - origin.y) / direction.y;
                        consider(BLPoint(origin.x + direction.x * t, target.point.y), SnapResult::Kind::Align, &target.point);
                    }
                    if (direction.x != 0.0) {
                        const double t = (target.point.x - origin.x) / direction.x;
                        consider(BLPoint(target.point.x, origin.y + direction.y * t), SnapResult::Kind::Align, &target.point);
                    }
                }
            }
        }
        return result;
    }

    Snapper::MoveResult Snapper::snapMove(const std::vector<BLPoint>& points, const BLPoint& delta, double zoom,
                                          const std::vector<std::size_t>& exclude) const {
        MoveResult result;
        result.delta = delta;
        double smallest = 0.0;
        bool onPoint = false;
        for (const BLPoint& start : points) {
            const BLPoint moved(start.x + delta.x, start.y + delta.y);
            const SnapResult hit = snap(moved, zoom, exclude);
            if (!hit.snapped()) {
                continue;
            }
            const BLPoint pull(hit.point.x - moved.x, hit.point.y - moved.y);
            const double size = std::hypot(pull.x, pull.y);
            // Landing on a point (corner, midpoint, centre) beats sliding along an edge or a guide, however short that slide is.
            const bool point = hit.kind != SnapResult::Kind::Edge && hit.kind != SnapResult::Kind::Guide;
            const bool better = !result.hit.snapped() || (point && !onPoint) || (point == onPoint && size < smallest);
            if (better) {
                onPoint = point;
                smallest = size;
                result.delta = BLPoint(delta.x + pull.x, delta.y + pull.y);
                result.hit = hit;
            }
        }
        return result;
    }

    Snapper::MoveResult Snapper::alignMove(const std::vector<BLPoint>& points, const BLPoint& delta, double zoom,
                                           const std::vector<std::size_t>& exclude) const {
        MoveResult result;
        result.delta = delta;
        double smallest = 0.0;
        for (const BLPoint& start : points) {
            const BLPoint moved(start.x + delta.x, start.y + delta.y);
            const SnapResult hit = align(moved, zoom, {}, exclude);
            if (!hit.snapped()) {
                continue;
            }
            const BLPoint pull(hit.point.x - moved.x, hit.point.y - moved.y);
            const double size = std::hypot(pull.x, pull.y);
            if (!result.hit.snapped() || size < smallest) {
                smallest = size;
                result.delta = BLPoint(delta.x + pull.x, delta.y + pull.y);
                result.hit = hit;
            }
        }
        return result;
    }

    void Snapper::paintMarker(BLContext& ctx, const SnapResult& result, const BLPoint& origin, double zoom) {
        if (!result.snapped()) {
            return;
        }
        const double x = origin.x + result.point.x * zoom, y = origin.y + result.point.y * zoom;
        constexpr double r = 5.0;
        ctx.set_stroke_width(1.5);
        ctx.set_stroke_style(BLRgba32(0xFF00C853u));
        if (result.kind == SnapResult::Kind::Align) {
            ctx.set_stroke_width(1.0);
            for (int i = 0; i < result.references && i < 2; ++i) {
                const BLPoint& reference = i == 0 ? result.reference : result.reference2;
                const double rx = origin.x + reference.x * zoom, ry = origin.y + reference.y * zoom;
                const double length = std::hypot(x - rx, y - ry);
                for (double at = 0.0; length > 0.0 && at < length; at += 8.0) {
                    const double from = at / length, to = (std::min)(at + 4.0, length) / length;
                    ctx.stroke_line(BLPoint(rx + (x - rx) * from, ry + (y - ry) * from), BLPoint(rx + (x - rx) * to, ry + (y - ry) * to));
                }
            }
            ctx.set_stroke_width(1.5);
        }
        switch (result.kind) {
            case SnapResult::Kind::Corner:
                ctx.stroke_rect(BLRect(x - r, y - r, 2.0 * r, 2.0 * r));
                break;
            case SnapResult::Kind::Midpoint: {
                BLPath diamond;
                diamond.move_to(x, y - r);
                diamond.line_to(x + r, y);
                diamond.line_to(x, y + r);
                diamond.line_to(x - r, y);
                diamond.close();
                ctx.stroke_path(diamond);
                break;
            }
            case SnapResult::Kind::Center:
                ctx.stroke_circle(BLCircle(x, y, r));
                break;
            case SnapResult::Kind::Intersection:
                ctx.stroke_circle(BLCircle(x, y, r));
                ctx.stroke_line(BLPoint(x - r, y - r), BLPoint(x + r, y + r));
                ctx.stroke_line(BLPoint(x - r, y + r), BLPoint(x + r, y - r));
                break;
            default:
                ctx.stroke_line(BLPoint(x - r, y - r), BLPoint(x + r, y + r));
                ctx.stroke_line(BLPoint(x - r, y + r), BLPoint(x + r, y - r));
                break;
        }
    }

}
