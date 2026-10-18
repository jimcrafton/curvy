#include "curvy/shapetools.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace curvy {

    std::vector<BLPoint> regularPolygonPoints(const BLPoint& center, double radius, int sides, double firstAngle) {
        sides = std::max(3, sides);
        std::vector<BLPoint> points;
        const double kTwoPi = 6.283185307179586;
        for (int i = 0; i < sides; ++i) {
            double a = firstAngle + kTwoPi * double(i) / double(sides);
            points.push_back(BLPoint(center.x + radius * std::cos(a), center.y + radius * std::sin(a)));
        }
        return points;
    }

    std::vector<BLPoint> starPoints(const BLPoint& center, double outer, double innerRatio, int points, double firstAngle) {
        points = std::max(3, points);
        std::vector<BLPoint> result;
        const double kTwoPi = 6.283185307179586;
        for (int i = 0; i < points * 2; ++i) {
            double radius = i % 2 == 0 ? outer : outer * innerRatio;
            double a = firstAngle + kTwoPi * double(i) / double(points * 2);
            result.push_back(BLPoint(center.x + radius * std::cos(a), center.y + radius * std::sin(a)));
        }
        return result;
    }

    namespace {
        // Distance from p to the segment a-b.
        double distanceToSegment(const BLPoint& p, const BLPoint& a, const BLPoint& b) {
            double dx = b.x - a.x, dy = b.y - a.y;
            double lengthSquared = dx * dx + dy * dy;
            if (lengthSquared < 1e-12) {
                return std::hypot(p.x - a.x, p.y - a.y);
            }
            double t = std::max(0.0, std::min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared));
            return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
        }

        void simplifyRange(const std::vector<BLPoint>& points, std::size_t first, std::size_t last, double tolerance, std::vector<bool>& keep) {
            if (last <= first + 1) {
                return;
            }
            double worst = 0.0;
            std::size_t worstIndex = first;
            for (std::size_t i = first + 1; i < last; ++i) {
                double d = distanceToSegment(points[i], points[first], points[last]);
                if (d > worst) {
                    worst = d;
                    worstIndex = i;
                }
            }
            if (worst > tolerance) {
                keep[worstIndex] = true;
                simplifyRange(points, first, worstIndex, tolerance, keep);
                simplifyRange(points, worstIndex, last, tolerance, keep);
            }
        }
    }

    std::vector<BLPoint> simplifyPolyline(const std::vector<BLPoint>& points, double tolerance) {
        if (points.size() < 3) {
            return points;
        }
        std::vector<bool> keep(points.size(), false);
        keep.front() = true;
        keep.back() = true;
        simplifyRange(points, 0, points.size() - 1, tolerance, keep);
        std::vector<BLPoint> result;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (keep[i]) {
                result.push_back(points[i]);
            }
        }
        return result;
    }

    BLPath smoothPath(const std::vector<BLPoint>& points) {
        BLPath path;
        if (points.empty()) {
            return path;
        }
        path.move_to(points[0].x, points[0].y);
        if (points.size() == 1) {
            return path;
        }
        if (points.size() == 2) {
            path.line_to(points[1].x, points[1].y);
            return path;
        }
        // Catmull-Rom: the curve from p[i] to p[i+1] has control points p[i] + (p[i+1] - p[i-1]) / 6 and
        // p[i+1] - (p[i+2] - p[i]) / 6, with the end points standing in for their missing neighbours.
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const BLPoint& p0 = points[i == 0 ? 0 : i - 1];
            const BLPoint& p1 = points[i];
            const BLPoint& p2 = points[i + 1];
            const BLPoint& p3 = points[i + 2 < points.size() ? i + 2 : i + 1];
            path.cubic_to(p1.x + (p2.x - p0.x) / 6.0, p1.y + (p2.y - p0.y) / 6.0,
                p2.x - (p3.x - p1.x) / 6.0, p2.y - (p3.y - p1.y) / 6.0, p2.x, p2.y);
        }
        return path;
    }

    namespace {
        bool apart(const BLPoint& a, const BLPoint& b) {
            return std::hypot(a.x - b.x, a.y - b.y) > 1e-9;
        }
    }

    std::vector<SubPath> pathToSubPaths(const BLPath& path) {
        std::vector<SubPath> result;
        const BLPoint* v = path.vertex_data();
        const std::uint8_t* cmd = path.command_data();
        const std::size_t n = path.size();
        auto finish = [&result](SubPath& sub, bool closed) {
            if (sub.anchors.empty()) {
                return;
            }
            sub.closed = closed;
            // A closing segment that returns to the first point: the first anchor takes its handle.
            if (closed && sub.anchors.size() > 1) {
                const PenAnchor& last = sub.anchors.back();
                PenAnchor& first = sub.anchors.front();
                if (std::hypot(last.point.x - first.point.x, last.point.y - first.point.y) < 1e-6) {
                    first.in = last.in;
                    first.hasIn = last.hasIn;
                    sub.anchors.pop_back();
                }
            }
            result.push_back(sub);
            sub = SubPath();
        };
        SubPath current;
        std::size_t i = 0;
        while (i < n) {
            switch (cmd[i]) {
                case BL_PATH_CMD_MOVE: {
                    finish(current, false);
                    PenAnchor a;
                    a.point = v[i];
                    current.anchors.push_back(a);
                    ++i;
                    break;
                }
                case BL_PATH_CMD_ON: {
                    PenAnchor a;
                    a.point = v[i];
                    current.anchors.push_back(a);
                    ++i;
                    break;
                }
                case BL_PATH_CMD_QUAD: {
                    if (i + 1 >= n || current.anchors.empty()) { i = n; break; }
                    PenAnchor& prev = current.anchors.back();
                    const BLPoint q = v[i], end = v[i + 1];
                    prev.out = BLPoint(prev.point.x + 2.0 / 3.0 * (q.x - prev.point.x), prev.point.y + 2.0 / 3.0 * (q.y - prev.point.y));
                    prev.hasOut = apart(prev.out, prev.point);
                    PenAnchor a;
                    a.point = end;
                    a.in = BLPoint(end.x + 2.0 / 3.0 * (q.x - end.x), end.y + 2.0 / 3.0 * (q.y - end.y));
                    a.hasIn = apart(a.in, a.point);
                    current.anchors.push_back(a);
                    i += 2;
                    break;
                }
                case BL_PATH_CMD_CUBIC: {
                    if (i + 2 >= n || current.anchors.empty()) { i = n; break; }
                    PenAnchor& prev = current.anchors.back();
                    prev.out = v[i];
                    prev.hasOut = apart(prev.out, prev.point);   // a handle sitting on its anchor (a pen corner) is no handle
                    PenAnchor a;
                    a.point = v[i + 2];
                    a.in = v[i + 1];
                    a.hasIn = apart(a.in, a.point);
                    current.anchors.push_back(a);
                    i += 3;
                    break;
                }
                case BL_PATH_CMD_CLOSE:
                    finish(current, true);
                    ++i;
                    break;
                case BL_PATH_CMD_CONIC: {   // not produced by this app: a straight line to its end point
                    if (i + 2 < n) {
                        PenAnchor a;
                        a.point = v[i + 2];
                        current.anchors.push_back(a);
                    }
                    i += 3;
                    break;
                }
                default:
                    ++i;
                    break;
            }
        }
        finish(current, false);
        return result;
    }

    BLPath subPathsToPath(const std::vector<SubPath>& subPaths) {
        BLPath path;
        for (const SubPath& sub : subPaths) {
            path.add_path(penPath(sub.anchors, sub.closed));
        }
        return path;
    }

    BLPath penPath(const std::vector<PenAnchor>& anchors, bool closed) {
        BLPath path;
        if (anchors.empty()) {
            return path;
        }
        path.move_to(anchors[0].point.x, anchors[0].point.y);
        auto segment = [&path](const PenAnchor& from, const PenAnchor& to) {
            if (!from.hasOut && !to.hasIn) {
                path.line_to(to.point.x, to.point.y);
                return;
            }
            BLPoint c1 = from.hasOut ? from.out : from.point;
            BLPoint c2 = to.hasIn ? to.in : to.point;
            path.cubic_to(c1.x, c1.y, c2.x, c2.y, to.point.x, to.point.y);
        };
        for (std::size_t i = 1; i < anchors.size(); ++i) {
            segment(anchors[i - 1], anchors[i]);
        }
        if (closed && anchors.size() > 1) {
            segment(anchors.back(), anchors.front());
            path.close();
        }
        return path;
    }

    // ---- Recognising regular shapes ----

    namespace {
        constexpr double kTwoPi = 6.283185307179586;
        constexpr double kPi = 3.141592653589793;

        // The smallest signed angle that turns `from` into `to`, in -pi..pi.
        double angleBetween(double from, double to) {
            double d = std::fmod(to - from, kTwoPi);
            if (d > kPi) { d -= kTwoPi; }
            if (d < -kPi) { d += kTwoPi; }
            return d;
        }
    }

    bool analyzeRegularShape(const std::vector<BLPoint>& corners, RegularShape& out) {
        const std::size_t n = corners.size();
        if (n < 3) {
            return false;
        }
        BLPoint center(0.0, 0.0);
        for (const BLPoint& p : corners) {
            center.x += p.x;
            center.y += p.y;
        }
        center.x /= double(n);
        center.y /= double(n);
        std::vector<double> radius(n), angle(n);
        for (std::size_t i = 0; i < n; ++i) {
            radius[i] = std::hypot(corners[i].x - center.x, corners[i].y - center.y);
            angle[i] = std::atan2(corners[i].y - center.y, corners[i].x - center.x);
            if (radius[i] < 1e-6) {
                return false;
            }
        }
        auto closeTo = [](double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance * std::max(std::fabs(a), std::fabs(b)); };
        // Equal steps of 2 pi / count, clockwise on screen (increasing angle in y-down space), every `stride` corners.
        auto evenSteps = [&](std::size_t count, std::size_t stride, std::size_t first) {
            const double expected = kTwoPi / double(count);
            for (std::size_t k = 0; k < count; ++k) {
                double step = angleBetween(angle[(first + k * stride) % n], angle[(first + (k + 1) * stride) % n]);
                if (std::fabs(step - expected) > 2e-3) {
                    return false;
                }
            }
            return true;
        };
        // A polygon: every corner on one circle.
        bool onePolygon = true;
        for (std::size_t i = 1; i < n; ++i) {
            onePolygon = onePolygon && closeTo(radius[i], radius[0], 2e-3);
        }
        if (onePolygon && evenSteps(n, 1, 0)) {
            out = RegularShape();
            out.star = false;
            out.points = int(n);
            out.center = center;
            out.outer = radius[0];
            out.firstAngle = angle[0];
            return true;
        }
        // A star: alternating outer and inner corners, the outer ones evenly spaced.
        if (n >= 6 && n % 2 == 0) {
            for (std::size_t first = 0; first < 2; ++first) {   // which parity holds the outer corners
                const double outerRadius = radius[first], innerRadius = radius[1 - first];
                bool alternates = true;
                for (std::size_t i = 0; i < n && alternates; ++i) {
                    alternates = closeTo(radius[i], (i % 2 == first) ? outerRadius : innerRadius, 2e-3);
                }
                if (alternates && outerRadius > innerRadius * 1.0005 && evenSteps(n / 2, 2, first)) {
                    out = RegularShape();
                    out.star = true;
                    out.points = int(n / 2);
                    out.center = center;
                    out.outer = outerRadius;
                    out.inner = innerRadius;
                    out.firstAngle = angle[first];
                    return true;
                }
            }
        }
        return false;
    }

    BLPath regularShapePath(const RegularShape& shape) {
        std::vector<BLPoint> points = shape.star ? starPoints(shape.center, shape.outer, shape.ratio(), shape.points, shape.firstAngle)
                                                 : regularPolygonPoints(shape.center, shape.outer, shape.points, shape.firstAngle);
        BLPath path;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (i == 0) { path.move_to(points[i].x, points[i].y); } else { path.line_to(points[i].x, points[i].y); }
        }
        path.close();
        return path;
    }

    bool analyzeRoundRect(const BLPath& path, double frameAngle, RoundRectShape& out) {
        std::vector<SubPath> subs = pathToSubPaths(path);
        if (subs.size() != 1 || !subs[0].closed || (subs[0].anchors.size() != 4 && subs[0].anchors.size() != 8)) {
            return false;
        }
        const double c = std::cos(-frameAngle), s = std::sin(-frameAngle);
        std::vector<BLPoint> local;
        for (const PenAnchor& a : subs[0].anchors) {
            local.push_back(BLPoint(a.point.x * c - a.point.y * s, a.point.x * s + a.point.y * c));
        }
        BLBox box(local[0].x, local[0].y, local[0].x, local[0].y);
        for (const BLPoint& p : local) {
            box.x0 = std::min(box.x0, p.x);
            box.y0 = std::min(box.y0, p.y);
            box.x1 = std::max(box.x1, p.x);
            box.y1 = std::max(box.y1, p.y);
        }
        const double w = box.x1 - box.x0, h = box.y1 - box.y0;
        if (w < 1e-6 || h < 1e-6) {
            return false;
        }
        const double eps = 1e-3 * std::max(w, h);
        double radius = -1.0;
        for (const BLPoint& p : local) {
            bool onVertical = std::fabs(p.x - box.x0) < eps || std::fabs(p.x - box.x1) < eps;
            bool onHorizontal = std::fabs(p.y - box.y0) < eps || std::fabs(p.y - box.y1) < eps;
            if (!onVertical && !onHorizontal) {
                return false;   // an anchor inside the box: not a rounded rectangle
            }
            if (std::fabs(p.y - box.y0) < eps) {   // on the top edge: the left-most one starts the corner arc
                double distance = p.x - box.x0;
                if (radius < 0.0 || distance < radius) {
                    radius = distance;
                }
            }
        }
        if (radius < 0.0) {
            return false;
        }
        RoundRectShape candidate;
        candidate.box = box;
        candidate.radius = radius < eps ? 0.0 : radius;
        candidate.angle = frameAngle;
        // The corners must really be circular arcs of that radius (an ellipse has anchors on the same edges but a different
        // outline): rebuild it and require the same anchors.
        std::vector<SubPath> rebuilt = pathToSubPaths(roundRectPath(candidate));
        if (rebuilt.size() != 1 || rebuilt[0].anchors.size() != subs[0].anchors.size()) {
            return false;
        }
        for (const PenAnchor& a : subs[0].anchors) {
            bool matched = false;
            for (const PenAnchor& b : rebuilt[0].anchors) {
                matched = matched || (std::hypot(a.point.x - b.point.x, a.point.y - b.point.y) < eps * 2.0 &&
                                      a.hasIn == b.hasIn && a.hasOut == b.hasOut);
            }
            if (!matched) {
                return false;
            }
        }
        out = candidate;
        return true;
    }

    BLPath roundRectPath(const RoundRectShape& shape) {
        BLPath path;
        const double w = shape.box.x1 - shape.box.x0, h = shape.box.y1 - shape.box.y0;
        const double radius = std::max(0.0, std::min(shape.radius, std::min(w, h) * 0.5));
        if (radius <= 0.0) {
            path.add_rect(BLRect(shape.box.x0, shape.box.y0, w, h));
        } else {
            path.add_round_rect(BLRoundRect(shape.box.x0, shape.box.y0, w, h, radius));
        }
        path.transform(BLMatrix2D::make_rotation(shape.angle));
        return path;
    }

    // ---- Arcs ----

    BLPath arcPath(const ArcShape& arc) {
        BLPath path;
        const double sweep = std::max(-kTwoPi, std::min(kTwoPi, arc.sweep));
        if (arc.radius <= 0.0 || std::fabs(sweep) < 1e-9) {
            return path;
        }
        const int segments = std::max(1, int(std::ceil(std::fabs(sweep) / (kPi / 2.0) - 1e-9)));
        const double theta = sweep / double(segments);
        const double kappa = 4.0 / 3.0 * std::tan(theta / 4.0);
        BLPoint first(arc.center.x + arc.radius * std::cos(arc.start), arc.center.y + arc.radius * std::sin(arc.start));
        path.move_to(first.x, first.y);
        for (int i = 0; i < segments; ++i) {
            const double a0 = arc.start + theta * double(i), a1 = a0 + theta;
            const BLPoint p0(arc.center.x + arc.radius * std::cos(a0), arc.center.y + arc.radius * std::sin(a0));
            const BLPoint p3(arc.center.x + arc.radius * std::cos(a1), arc.center.y + arc.radius * std::sin(a1));
            path.cubic_to(p0.x - kappa * arc.radius * std::sin(a0), p0.y + kappa * arc.radius * std::cos(a0),
                p3.x + kappa * arc.radius * std::sin(a1), p3.y - kappa * arc.radius * std::cos(a1), p3.x, p3.y);
        }
        return path;
    }

    bool analyzeArc(const BLPath& path, ArcShape& out) {
        std::vector<SubPath> subs = pathToSubPaths(path);
        if (subs.size() != 1 || subs[0].closed || subs[0].anchors.size() < 2 || subs[0].anchors.size() > 6) {
            return false;
        }
        const std::vector<PenAnchor>& a = subs[0].anchors;
        // Samples along the curve: every anchor and the middle of every segment (every segment must be a curve).
        std::vector<BLPoint> samples;
        for (std::size_t i = 0; i < a.size(); ++i) {
            samples.push_back(a[i].point);
            if (i + 1 < a.size()) {
                if (!a[i].hasOut || !a[i + 1].hasIn) {
                    return false;
                }
                samples.push_back(BLPoint((a[i].point.x + 3.0 * a[i].out.x + 3.0 * a[i + 1].in.x + a[i + 1].point.x) / 8.0,
                    (a[i].point.y + 3.0 * a[i].out.y + 3.0 * a[i + 1].in.y + a[i + 1].point.y) / 8.0));
            }
        }
        // The circle through the first sample, the middle one and the last.
        const BLPoint p = samples.front(), q = samples[samples.size() / 2], r = samples.back();
        const double d = 2.0 * (p.x * (q.y - r.y) + q.x * (r.y - p.y) + r.x * (p.y - q.y));
        if (std::fabs(d) < 1e-9) {
            return false;   // a straight run
        }
        const double pp = p.x * p.x + p.y * p.y, qq = q.x * q.x + q.y * q.y, rr = r.x * r.x + r.y * r.y;
        BLPoint center((pp * (q.y - r.y) + qq * (r.y - p.y) + rr * (p.y - q.y)) / d, (pp * (r.x - q.x) + qq * (p.x - r.x) + rr * (q.x - p.x)) / d);
        const double radius = std::hypot(p.x - center.x, p.y - center.y);
        if (radius < 1e-6) {
            return false;
        }
        double sweep = 0.0;
        double previous = std::atan2(samples[0].y - center.y, samples[0].x - center.x);
        const double first = previous;
        double direction = 0.0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (std::fabs(std::hypot(samples[i].x - center.x, samples[i].y - center.y) - radius) > 0.004 * radius) {
                return false;
            }
            if (i == 0) {
                continue;
            }
            const double angle = std::atan2(samples[i].y - center.y, samples[i].x - center.x);
            const double step = angleBetween(previous, angle);
            if (direction == 0.0) {
                direction = step < 0.0 ? -1.0 : 1.0;
            }
            if (step * direction <= 0.0 || std::fabs(step) > kPi * 0.6) {
                return false;   // doubling back, or a jump too big for a quarter-turn segment's halves
            }
            sweep += step;
            previous = angle;
        }
        if (std::fabs(sweep) > kTwoPi + 1e-3) {
            return false;
        }
        out.center = center;
        out.radius = radius;
        out.start = first;
        out.sweep = sweep;
        return true;
    }

    // ---- Curve fitting (the pencil) ----

    namespace {
        struct Vec {
            double x = 0.0, y = 0.0;
        };
        Vec operator+(const Vec& a, const Vec& b) { return Vec{a.x + b.x, a.y + b.y}; }
        Vec operator-(const Vec& a, const Vec& b) { return Vec{a.x - b.x, a.y - b.y}; }
        Vec operator*(const Vec& a, double s) { return Vec{a.x * s, a.y * s}; }
        double dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y; }
        double length(const Vec& a) { return std::sqrt(dot(a, a)); }
        Vec unit(const Vec& a) {
            double l = length(a);
            return l > 1e-12 ? a * (1.0 / l) : Vec{0.0, 0.0};
        }
        Vec vec(const BLPoint& p) { return Vec{p.x, p.y}; }

        typedef Vec Bezier[4];

        Vec bezierAt(const Bezier& b, double t) {
            double s = 1.0 - t;
            return b[0] * (s * s * s) + b[1] * (3.0 * s * s * t) + b[2] * (3.0 * s * t * t) + b[3] * (t * t * t);
        }
        Vec bezierDerivative(const Bezier& b, double t) {
            double s = 1.0 - t;
            return (b[1] - b[0]) * (3.0 * s * s) + (b[2] - b[1]) * (6.0 * s * t) + (b[3] - b[2]) * (3.0 * t * t);
        }
        Vec bezierSecond(const Bezier& b, double t) {
            return (b[2] - b[1] * 2.0 + b[0]) * (6.0 * (1.0 - t)) + (b[3] - b[2] * 2.0 + b[1]) * (6.0 * t);
        }

        std::vector<double> chordLength(const std::vector<Vec>& d, std::size_t first, std::size_t last) {
            std::vector<double> u(last - first + 1, 0.0);
            for (std::size_t i = first + 1; i <= last; ++i) {
                u[i - first] = u[i - first - 1] + length(d[i] - d[i - 1]);
            }
            double total = u.back();
            for (double& value : u) {
                value = total > 0.0 ? value / total : 0.0;
            }
            return u;
        }

        // The cubic from d[first] to d[last] with the given end tangents whose handle lengths best fit the samples (least squares).
        void generateBezier(const std::vector<Vec>& d, std::size_t first, std::size_t last, const std::vector<double>& u, const Vec& tangent1,
            const Vec& tangent2, Bezier& out) {
            const std::size_t count = last - first + 1;
            double c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                double t = u[i], s = 1.0 - t;
                double b0 = s * s * s, b1 = 3.0 * t * s * s, b2 = 3.0 * t * t * s, b3 = t * t * t;
                Vec a0 = tangent1 * b1, a1 = tangent2 * b2;
                c00 += dot(a0, a0);
                c01 += dot(a0, a1);
                c11 += dot(a1, a1);
                Vec tmp = d[first + i] - (d[first] * (b0 + b1) + d[last] * (b2 + b3));
                x0 += dot(a0, tmp);
                x1 += dot(a1, tmp);
            }
            double det = c00 * c11 - c01 * c01;
            double alphaLeft = det == 0.0 ? 0.0 : (x0 * c11 - x1 * c01) / det;
            double alphaRight = det == 0.0 ? 0.0 : (c00 * x1 - c01 * x0) / det;
            double segment = length(d[last] - d[first]);
            double epsilon = 1e-6 * segment;
            out[0] = d[first];
            out[3] = d[last];
            // A handle longer than the chord it belongs to swings the curve out past its end and back (a small hook at a corner): treat
            // that, like a negative or zero length, as a failed fit and use the usual third of the chord.
            if (alphaLeft < epsilon || alphaRight < epsilon || alphaLeft > segment || alphaRight > segment) {
                out[1] = d[first] + tangent1 * (segment / 3.0);
                out[2] = d[last] + tangent2 * (segment / 3.0);
            } else {
                out[1] = d[first] + tangent1 * alphaLeft;
                out[2] = d[last] + tangent2 * alphaRight;
            }
        }

        // The largest squared distance from a sample to the curve, and which sample it is.
        double maxError(const std::vector<Vec>& d, std::size_t first, std::size_t last, const Bezier& b, const std::vector<double>& u, std::size_t& split) {
            double worst = 0.0;
            split = (last - first + 1) / 2 + first;
            for (std::size_t i = first + 1; i < last; ++i) {
                Vec diff = bezierAt(b, u[i - first]) - d[i];
                double distance = dot(diff, diff);
                if (distance >= worst) {
                    worst = distance;
                    split = i;
                }
            }
            return worst;
        }

        // One Newton-Raphson step moving each parameter toward the closest point of the curve.
        std::vector<double> reparameterize(const std::vector<Vec>& d, std::size_t first, std::size_t last, const std::vector<double>& u, const Bezier& b) {
            std::vector<double> better(u.size());
            for (std::size_t i = 0; i < u.size(); ++i) {
                Vec diff = bezierAt(b, u[i]) - d[first + i];
                Vec d1 = bezierDerivative(b, u[i]), d2 = bezierSecond(b, u[i]);
                double numerator = dot(diff, d1);
                double denominator = dot(d1, d1) + dot(diff, d2);
                better[i] = denominator == 0.0 ? u[i] : std::max(0.0, std::min(1.0, u[i] - numerator / denominator));
            }
            return better;
        }

        // The direction in which the polyline leaves d[from] toward d[toward]: the principal axis of the samples within `reach` of the
        // end (a least-squares line), pointing along the stroke.
        Vec endTangent(const std::vector<Vec>& d, std::size_t from, std::size_t toward, double tolerance) {
            const bool forward = toward > from;
            const std::size_t span = forward ? toward - from : from - toward;
            const std::size_t cap = std::max<std::size_t>(2, span / 3);
            std::vector<Vec> near;
            near.push_back(d[from]);
            for (std::size_t k = 1; k <= cap; ++k) {
                Vec p = forward ? d[from + k] : d[from - k];
                near.push_back(p);
                if (length(p - d[from]) > 6.0 * tolerance && near.size() >= 4) {
                    break;
                }
            }
            Vec mean{0.0, 0.0};
            for (const Vec& p : near) { mean = mean + p; }
            mean = mean * (1.0 / double(near.size()));
            double sxx = 0.0, sxy = 0.0, syy = 0.0;
            for (const Vec& p : near) {
                Vec q = p - mean;
                sxx += q.x * q.x;
                sxy += q.x * q.y;
                syy += q.y * q.y;
            }
            double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
            Vec axis{std::cos(angle), std::sin(angle)};
            Vec along = near.back() - d[from];
            return dot(axis, along) >= 0.0 ? axis : axis * -1.0;
        }

        // Of the samples within `reach` of d[around], the one farthest from the straight line between the samples `reach` before and
        // after - the tip of the turn, even when the corner has been rounded a little by smoothing.
        std::size_t apexNear(const std::vector<Vec>& d, std::size_t around, std::size_t reach) {
            const std::size_t from = around > reach ? around - reach : 0;
            const std::size_t to = std::min(d.size() - 1, around + reach);
            Vec a = d[from], b = d[to];
            Vec ab = b - a;
            double abLength = length(ab);
            std::size_t best = around;
            double bestDistance = -1.0;
            for (std::size_t i = from; i <= to; ++i) {
                Vec ap = d[i] - a;
                double distance = abLength > 1e-12 ? std::fabs(ap.x * ab.y - ap.y * ab.x) / abLength : length(ap);
                if (distance > bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }
            return best;
        }

        void fitCubic(const std::vector<Vec>& d, std::size_t first, std::size_t last, const Vec& tangent1, const Vec& tangent2, double errorSquared,
            std::vector<std::array<Vec, 4>>& out, int depth) {
            if (last - first == 1) {
                double dist = length(d[last] - d[first]) / 3.0;
                out.push_back({d[first], d[first] + tangent1 * dist, d[last] + tangent2 * dist, d[last]});
                return;
            }
            std::vector<double> u = chordLength(d, first, last);
            Bezier b;
            generateBezier(d, first, last, u, tangent1, tangent2, b);
            std::size_t split = 0;
            double error = maxError(d, first, last, b, u, split);
            if (error < errorSquared) {
                out.push_back({b[0], b[1], b[2], b[3]});
                return;
            }
            if (error < errorSquared * 16.0) {   // close: a few refinements of the parameters may be enough
                for (int pass = 0; pass < 4; ++pass) {
                    u = reparameterize(d, first, last, u, b);
                    generateBezier(d, first, last, u, tangent1, tangent2, b);
                    error = maxError(d, first, last, b, u, split);
                    if (error < errorSquared) {
                        out.push_back({b[0], b[1], b[2], b[3]});
                        return;
                    }
                }
            }
            if (depth > 40 || split <= first || split >= last) {
                out.push_back({b[0], b[1], b[2], b[3]});
                return;
            }
            // Split where the fit is worst, with one smooth tangent at the join.
            Vec center = unit(d[split - 1] - d[split + 1]);
            fitCubic(d, first, split, tangent1, center, errorSquared, out, depth + 1);
            fitCubic(d, split, last, center * -1.0, tangent2, errorSquared, out, depth + 1);
        }
    }

    std::vector<PenAnchor> fitCurve(const std::vector<BLPoint>& points, double tolerance) {
        // Drop repeated samples.
        std::vector<Vec> d;
        for (const BLPoint& p : points) {
            Vec v = vec(p);
            if (d.empty() || length(v - d.back()) > 1e-9) {
                d.push_back(v);
            }
        }
        std::vector<PenAnchor> anchors;
        auto anchorAt = [](const Vec& v) {
            PenAnchor a;
            a.point = BLPoint(v.x, v.y);
            return a;
        };
        if (d.empty()) {
            return anchors;
        }
        if (d.size() == 1) {
            anchors.push_back(anchorAt(d[0]));
            return anchors;
        }
        if (d.size() == 2) {
            anchors.push_back(anchorAt(d[0]));
            anchors.push_back(anchorAt(d[1]));
            return anchors;
        }

        // Sharp corners: where the direction coming in and the direction going out (a few samples either side) differ by 60 degrees or
        // more, and nowhere nearer a sharper one - the line is cut there and each part fitted on its own.
        const std::size_t span = 4;
        std::vector<std::size_t> cuts;
        cuts.push_back(0);
        if (d.size() > 2 * span + 1) {
            double bestTurn = 0.0;
            std::size_t bestAt = 0;
            for (std::size_t i = span; i + span < d.size(); ++i) {
                Vec in = unit(d[i] - d[i - span]), out = unit(d[i + span] - d[i]);
                double turn = std::acos(std::max(-1.0, std::min(1.0, dot(in, out))));
                bool corner = turn >= kPi / 3.0;
                if (corner && turn > bestTurn) {
                    bestTurn = turn;
                    bestAt = i;
                }
                if (!corner && bestAt != 0) {   // the run of corner-like samples ended: keep its apex
                    std::size_t apex = apexNear(d, bestAt, span);
                    if (apex > cuts.back() + span) {
                        cuts.push_back(apex);
                    }
                    bestTurn = 0.0;
                    bestAt = 0;
                }
            }
            if (bestAt != 0 && bestAt + span < d.size()) {
                std::size_t apex = apexNear(d, bestAt, span);
                if (apex > cuts.back() + span) {
                    cuts.push_back(apex);
                }
            }
        }
        cuts.push_back(d.size() - 1);

        const double errorSquared = tolerance * tolerance;
        anchors.push_back(anchorAt(d[cuts[0]]));
        for (std::size_t c = 0; c + 1 < cuts.size(); ++c) {
            const std::size_t first = cuts[c], last = cuts[c + 1];
            // End tangents: the direction of a straight line fitted to the first (and last) few samples, up to a few tolerances along and
            // at most a third of the way, so the jitter of a hand drawn line does not decide which way the curve leaves its end.
            Vec tangent1 = endTangent(d, first, last, tolerance);
            Vec tangent2 = endTangent(d, last, first, tolerance);
            std::vector<std::array<Vec, 4>> curves;
            fitCubic(d, first, last, tangent1, tangent2, errorSquared, curves, 0);
            for (const std::array<Vec, 4>& b : curves) {
                anchors.back().out = BLPoint(b[1].x, b[1].y);
                anchors.back().hasOut = true;
                PenAnchor next = anchorAt(b[3]);
                next.in = BLPoint(b[2].x, b[2].y);
                next.hasIn = true;
                anchors.push_back(next);
            }
        }
        return anchors;
    }

    std::vector<BLPoint> smoothPolyline(const std::vector<BLPoint>& points, int passes) {
        std::vector<BLPoint> current = points;
        for (int pass = 0; pass < passes && current.size() > 2; ++pass) {
            std::vector<BLPoint> next = current;
            for (std::size_t i = 1; i + 1 < current.size(); ++i) {
                next[i] = BLPoint((current[i - 1].x + 2.0 * current[i].x + current[i + 1].x) / 4.0, (current[i - 1].y + 2.0 * current[i].y + current[i + 1].y) / 4.0);
            }
            current = next;
        }
        return current;
    }

}
