#include "curvy/sketchobject.h"
#include "curvy/sketchdocument.h"

#include "curvy/pathflatten.h"
#include "curvy/pathops.h"
#include "curvy/brush.h"
#include "curvy/textwarp.h"
#include "curvy/textoutline.h"

#include <newui/shapes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace curvy {

    SketchPaint SketchPaint::solid(const SketchColor& color) {
        SketchPaint paint;
        paint.kind_ = Kind::Solid;
        paint.color_ = color;
        return paint;
    }

    SketchPaint SketchPaint::linear(const SketchGradient& gradient) {
        SketchPaint paint;
        paint.kind_ = Kind::Linear;
        paint.gradient_ = gradient;
        paint.color_ = gradient.stops.empty() ? SketchColor() : gradient.stops.front().color;
        return paint;
    }

    SketchPaint SketchPaint::pattern(const std::shared_ptr<const SymbolDef>& def, double scale, double angleDegrees) {
        SketchPaint paint;
        paint.kind_ = Kind::Pattern;
        paint.color_ = SketchColor::gray(0.5f);   // what color-only callers (and other viewers) see
        paint.pattern_ = def;
        paint.patternId_ = def ? def->id : 0;
        paint.patternScale_ = scale;
        paint.patternAngle_ = angleDegrees;
        return paint;
    }

    SketchPaint SketchPaint::patternRef(std::size_t id, double scale, double angleDegrees) {
        SketchPaint paint = pattern(nullptr, scale, angleDegrees);
        paint.patternId_ = id;
        return paint;
    }

    const BLImage& SymbolDef::tile(int width, int height) const {
        for (const auto& entry : tiles) {
            if (std::get<0>(entry) == width && std::get<1>(entry) == height) {
                return std::get<2>(entry);
            }
        }
        BLImage image;
        if (image.create(width, height, BL_FORMAT_PRGB32) == BL_SUCCESS) {
            BLContext ctx(image);
            ctx.clear_all();
            ctx.scale(double(width) / (box.x1 - box.x0), double(height) / (box.y1 - box.y0));
            ctx.translate(-box.x0, -box.y0);
            if (content) {
                content->paint(ctx);
            }
            ctx.end();
        }
        if (tiles.size() >= 6) {
            tiles.erase(tiles.begin());
        }
        tiles.emplace_back(width, height, image);
        return std::get<2>(tiles.back());
    }

    SketchPaint SketchPaint::radial(const SketchGradient& gradient) {
        SketchPaint paint = linear(gradient);
        paint.kind_ = Kind::Radial;
        return paint;
    }

    SketchGradient SketchGradient::fromColor(const SketchColor& color) {
        SketchGradient gradient;
        gradient.stops.push_back({0.0, color});
        gradient.stops.push_back({1.0, SketchColor::rgb(1.0f, 1.0f, 1.0f, color.alpha())});
        return gradient;
    }

    void SketchGradient::sortStops() {
        std::stable_sort(stops.begin(), stops.end(),
            [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
    }

    SketchColor SketchGradient::colorAt(double t) const {
        if (stops.empty()) {
            return SketchColor::rgb(0.0f, 0.0f, 0.0f, 0.0f);
        }
        if (t <= stops.front().offset) {
            return stops.front().color;
        }
        if (t >= stops.back().offset) {
            return stops.back().color;
        }
        for (std::size_t i = 1; i < stops.size(); ++i) {
            if (t <= stops[i].offset) {
                const GradientStop& lo = stops[i - 1];
                const GradientStop& hi = stops[i];
                double span = hi.offset - lo.offset;
                float f = span > 1e-12 ? float((t - lo.offset) / span) : 0.0f;
                BLRgba32 a = lo.color.toRgba32();
                BLRgba32 b = hi.color.toRgba32();
                auto mix = [f](std::uint32_t x, std::uint32_t y) { return (float(x) + (float(y) - float(x)) * f) / 255.0f; };
                return SketchColor::rgb(mix(a.r(), b.r()), mix(a.g(), b.g()), mix(a.b(), b.b()), mix(a.a(), b.a()));
            }
        }
        return stops.back().color;
    }

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        // A box side as a divisor: a flat object (a horizontal line) still maps fractions to something.
        double span(double lo, double hi) { return hi - lo < 1e-9 ? 1.0 : hi - lo; }
    }

    void SketchGradient::axisPoints(const BLBox& box, bool radial, BLPoint& start, BLPoint& end) const {
        double w = span(box.x0, box.x1), h = span(box.y0, box.y1);
        if (hasAxis) {
            start = BLPoint(box.x0 + axisX1 * w, box.y0 + axisY1 * h);
            end = BLPoint(box.x0 + axisX2 * w, box.y0 + axisY2 * h);
            return;
        }
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        if (radial) {
            start = BLPoint(cx, cy);
            end = BLPoint(cx + std::max(box.x1 - box.x0, box.y1 - box.y0) * 0.5, cy);
            return;
        }
        double angle = angleDegrees * kPi / 180.0;
        double dx = std::cos(angle), dy = std::sin(angle);
        // Half the box's extent along the gradient direction, so 0 and 1 land on the box's edges.
        double half = (std::fabs((box.x1 - box.x0) * dx) + std::fabs((box.y1 - box.y0) * dy)) * 0.5;
        start = BLPoint(cx - dx * half, cy - dy * half);
        end = BLPoint(cx + dx * half, cy + dy * half);
    }

    BLMatrix2D SketchGradient::axisMatrix(const BLPoint& start, const BLPoint& end, bool radial) const {
        BLMatrix2D m = BLMatrix2D::make_identity();
        double aspect = radial ? axisAspect : 1.0;
        if (!hasAxis || (aspect == 1.0 && axisSkew == 0.0)) {
            return m;
        }
        // In the frame of the axis (x along it): squash across it, then lean x by skew * y.
        double angle = std::atan2(end.y - start.y, end.x - start.x);
        m = BLMatrix2D::make_translation(-start.x, -start.y);
        m.post_rotate(-angle);
        m.post_scale(1.0, aspect);
        m.post_transform(BLMatrix2D(1.0, 0.0, axisSkew, 1.0, 0.0, 0.0));
        m.post_rotate(angle);
        m.post_translate(start.x, start.y);
        return m;
    }

    void SketchGradient::setAxis(const BLBox& box, const BLPoint& start, const BLPoint& end) {
        double w = span(box.x0, box.x1), h = span(box.y0, box.y1);
        hasAxis = true;
        axisX1 = (start.x - box.x0) / w;
        axisY1 = (start.y - box.y0) / h;
        axisX2 = (end.x - box.x0) / w;
        axisY2 = (end.y - box.y0) / h;
        double dx = end.x - start.x, dy = end.y - start.y;
        if (std::hypot(dx, dy) > 1e-9) {
            angleDegrees = std::atan2(dy, dx) * 180.0 / kPi;
        }
    }

    void SketchGradient::setAngle(const BLBox& box, double degrees) {
        if (hasAxis) {
            BLPoint a, b;
            axisPoints(box, false, a, b);
            double length = std::hypot(b.x - a.x, b.y - a.y);
            double mx = (a.x + b.x) * 0.5, my = (a.y + b.y) * 0.5;
            double radians = degrees * kPi / 180.0;
            double dx = std::cos(radians) * length * 0.5, dy = std::sin(radians) * length * 0.5;
            setAxis(box, BLPoint(mx - dx, my - dy), BLPoint(mx + dx, my + dy));
        }
        angleDegrees = degrees;
    }

    void SketchGradient::reverse() {
        for (GradientStop& stop : stops) {
            stop.offset = 1.0 - stop.offset;
        }
        std::reverse(stops.begin(), stops.end());
    }

    namespace {
        // Applies a paint to ctx as the fill or stroke style over `box` (the object's geometry bounds),
        // returning false when there is nothing to paint.
        bool setPaintStyle(BLContext& ctx, const SketchPaint& paint, const BLBox& box, bool asStroke) {
            if (paint.isNone()) {
                return false;
            }
            double w = box.x1 - box.x0, h = box.y1 - box.y0;
            if (paint.isPattern()) {
                const SymbolDef* def = paint.patternDef().get();
                const double tw = def != nullptr ? def->box.x1 - def->box.x0 : 0.0, th = def != nullptr ? def->box.y1 - def->box.y0 : 0.0;
                const double scale = paint.patternScale();
                if (def == nullptr || !def->content || !(tw > 0.0) || !(th > 0.0) || !(scale > 0.0)) {
                    return false;
                }
                // The tile is drawn at the device's resolution and repeated; its size in pixels is a whole number, so the period is exact.
                BLMatrix2D m = ctx.final_transform();
                const double device = std::sqrt(std::fabs(m.m00 * m.m11 - m.m01 * m.m10));
                const int pw = std::max(1, std::min(2048, int(std::lround(tw * scale * device)))), ph = std::max(1, std::min(2048, int(std::lround(th * scale * device))));
                BLMatrix2D place = BLMatrix2D::make_scaling(tw * scale / double(pw), th * scale / double(ph));
                if (paint.patternAngle() != 0.0) {
                    place.post_transform(BLMatrix2D::make_rotation(paint.patternAngle() * 3.14159265358979323846 / 180.0));
                }
                BLPattern pattern(def->tile(pw, ph), BL_EXTEND_MODE_REPEAT, place);
                if (asStroke) { ctx.set_stroke_style(pattern); } else { ctx.set_fill_style(pattern); }
                return true;
            }
            if (!paint.isGradient() || paint.gradient().stops.empty() || (w < 1e-6 && h < 1e-6)) {
                BLRgba32 color = paint.color().toRgba32();
                if (asStroke) { ctx.set_stroke_style(color); } else { ctx.set_fill_style(color); }
                return true;
            }

            const SketchGradient& g = paint.gradient();
            bool radial = paint.kind() != SketchPaint::Kind::Linear;
            BLPoint start, end;
            g.axisPoints(box, radial, start, end);
            BLGradient gradient;
            if (!radial) {
                gradient = BLGradient(BLLinearGradientValues(start.x, start.y, end.x, end.y));
            } else {
                double radius = std::hypot(end.x - start.x, end.y - start.y);
                gradient = BLGradient(BLRadialGradientValues(start.x, start.y, start.x, start.y, radius > 1e-9 ? radius : 1e-9));
            }
            if (g.hasAxis && (g.axisSkew != 0.0 || (radial && g.axisAspect != 1.0))) {
                gradient.set_transform(g.axisMatrix(start, end, radial));
            }
            for (const GradientStop& stop : g.stops) {
                gradient.add_stop(std::min(1.0, std::max(0.0, stop.offset)), stop.color.toRgba32());
            }
            gradient.set_extend_mode(g.spread == GradientSpread::Repeat ? BL_EXTEND_MODE_REPEAT
                : (g.spread == GradientSpread::Reflect ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD));
            if (asStroke) { ctx.set_stroke_style(gradient); } else { ctx.set_fill_style(gradient); }
            return true;
        }

        BLStrokeCap blCap(StrokeCap cap) {
            return cap == StrokeCap::Round ? BL_STROKE_CAP_ROUND : (cap == StrokeCap::Square ? BL_STROKE_CAP_SQUARE : BL_STROKE_CAP_BUTT);
        }

        BLStrokeJoin blJoin(StrokeJoin join) {
            // SVG's miter falls back to a bevel past the miter limit.
            return join == StrokeJoin::Round ? BL_STROKE_JOIN_ROUND : (join == StrokeJoin::Bevel ? BL_STROKE_JOIN_BEVEL : BL_STROKE_JOIN_MITER_BEVEL);
        }

        // `path` cut into dashes (this Blend2D's stroker does not dash): the pattern is dash, gap, dash, gap...
        // (an odd count repeats, as in SVG) and starts over on every subpath. Each dash is an open polyline, so
        // stroking the result applies caps to every dash. A pattern with no length leaves the path whole.
        BLPath dashedPath(const BLPath& path, const std::vector<double>& dashes) {
            std::vector<double> pattern = dashes;
            if (pattern.size() % 2 == 1) {
                pattern.insert(pattern.end(), dashes.begin(), dashes.end());
            }
            double period = 0.0;
            for (double d : pattern) { period += d; }
            if (pattern.empty() || period < 1e-6) {
                return path;
            }
            BLPath out;
            for (Polyline& line : flattenPath(path)) {
                if (line.closed) {
                    line.points.push_back(line.points.front());
                }
                std::size_t index = 0;
                double left = pattern[0];
                bool on = true;
                bool open = false;  // a dash is in progress
                for (std::size_t i = 1; i < line.points.size(); ++i) {
                    BLPoint a = line.points[i - 1], b = line.points[i];
                    double length = std::hypot(b.x - a.x, b.y - a.y);
                    double at = 0.0;
                    if (on && !open && length > 0.0) {
                        out.move_to(a);
                        open = true;
                    }
                    while (length - at > left) {
                        at += left;
                        BLPoint cut(a.x + (b.x - a.x) * at / length, a.y + (b.y - a.y) * at / length);
                        if (on) {
                            out.line_to(cut);
                            open = false;
                        } else {
                            out.move_to(cut);
                            open = true;
                        }
                        on = !on;
                        index = (index + 1) % pattern.size();
                        left = pattern[index];
                        if (left <= 0.0 && period < 1e-6) { break; }
                    }
                    left -= length - at;
                    if (on && open) {
                        out.line_to(b);
                    }
                }
            }
            return out;
        }

        // `path` with its corners rounded: each vertex where the outline turns by more than ~11 degrees gets a quadratic
        // fillet of about `radius` (shortened to fit half of each neighbouring segment). Curves are flattened first, so
        // their gentle bends are left alone and only real corners change.
        BLPath roundedPath(const BLPath& path, double radius) {
            BLPath out;
            if (radius <= 0.0) {
                return path;
            }
            for (Polyline& line : flattenPath(path)) {
                std::vector<BLPoint> pts;
                for (const BLPoint& p : line.points) {
                    if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > 1e-9) { pts.push_back(p); }
                }
                bool closed = line.closed && pts.size() > 2;
                if (closed && std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) <= 1e-9) { pts.pop_back(); }
                std::size_t n = pts.size();
                if (n < 2) { continue; }
                struct Corner { BLPoint in, out, at; bool rounded = false; };
                std::vector<Corner> corners(n);
                for (std::size_t i = 0; i < n; ++i) {
                    corners[i].at = corners[i].in = corners[i].out = pts[i];
                    if (!closed && (i == 0 || i + 1 == n)) { continue; }
                    const BLPoint& prev = pts[(i + n - 1) % n];
                    const BLPoint& next = pts[(i + 1) % n];
                    double l1 = std::hypot(pts[i].x - prev.x, pts[i].y - prev.y), l2 = std::hypot(next.x - pts[i].x, next.y - pts[i].y);
                    BLPoint u1((pts[i].x - prev.x) / l1, (pts[i].y - prev.y) / l1), u2((next.x - pts[i].x) / l2, (next.y - pts[i].y) / l2);
                    double cross = u1.x * u2.y - u1.y * u2.x, dot = u1.x * u2.x + u1.y * u2.y;
                    double turn = std::atan2(std::fabs(cross), dot);  // 0 = straight on
                    if (turn < 0.2) { continue; }
                    double d = std::min(radius * std::tan(turn * 0.5), std::min(l1, l2) * 0.5);
                    corners[i].in = BLPoint(pts[i].x - u1.x * d, pts[i].y - u1.y * d);
                    corners[i].out = BLPoint(pts[i].x + u2.x * d, pts[i].y + u2.y * d);
                    corners[i].rounded = true;
                }
                auto emitCorner = [&](const Corner& c) {
                    out.line_to(c.in);
                    if (c.rounded) { out.quad_to(c.at, c.out); }
                };
                out.move_to(corners[0].out);
                for (std::size_t i = 1; i < n; ++i) { emitCorner(corners[i]); }
                if (closed) {
                    emitCorner(corners[0]);
                    out.close();
                }
            }
            return out;
        }

        // A repeatable pseudo-random number in [-1, 1) from a seed and two indices (splitmix64 mixing) - the same inputs always give the
        // same number, which is what keeps a roughened outline still while it is moved or repainted.
        double noise(std::uint64_t seed, std::uint64_t a, std::uint64_t b) {
            std::uint64_t z = seed * 0x9E3779B97F4A7C15ull + a * 0xBF58476D1CE4E5B9ull + b * 0x94D049BB133111EBull;
            z ^= z >> 30;
            z *= 0xBF58476D1CE4E5B9ull;
            z ^= z >> 27;
            z *= 0x94D049BB133111EBull;
            z ^= z >> 31;
            return double(z >> 11) / double(1ull << 52) - 1.0;
        }

        // The corners of a flattened outline without repeated points; a closed outline loses its repeated first point too.
        std::vector<BLPoint> cleanPoints(const Polyline& line, bool& closed) {
            std::vector<BLPoint> pts;
            for (const BLPoint& p : line.points) {
                if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > 1e-9) { pts.push_back(p); }
            }
            closed = line.closed && pts.size() > 2;
            if (closed && std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) <= 1e-9) { pts.pop_back(); }
            return pts;
        }

        // Roughen: every outline is cut into pieces about `detail` long and each point (the old corners too) is moved by up to `size`
        // in x and y. Curves come out as polylines - the wobble hides it.
        BLPath roughenPath(const BLPath& path, double size, double detail, double seed) {
            if (size <= 0.0) {
                return path;
            }
            detail = std::max(0.5, detail);
            const std::uint64_t s = std::uint64_t(std::llround(seed));
            BLPath out;
            std::uint64_t lineIndex = 0;
            for (Polyline& line : flattenPath(path)) {
                bool closed = false;
                std::vector<BLPoint> pts = cleanPoints(line, closed);
                std::size_t n = pts.size();
                if (n < 2) { continue; }
                std::vector<BLPoint> dense;
                std::size_t segments = closed ? n : n - 1;
                for (std::size_t i = 0; i < segments; ++i) {
                    const BLPoint& a = pts[i];
                    const BLPoint& b = pts[(i + 1) % n];
                    int pieces = int(std::min(2000.0, std::max(1.0, std::ceil(std::hypot(b.x - a.x, b.y - a.y) / detail))));
                    for (int k = 0; k < pieces; ++k) {
                        double t = double(k) / pieces;
                        dense.push_back(BLPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
                    }
                }
                if (!closed) { dense.push_back(pts.back()); }
                for (std::size_t j = 0; j < dense.size(); ++j) {
                    dense[j].x += noise(s, lineIndex, 2 * j) * size;
                    dense[j].y += noise(s, lineIndex, 2 * j + 1) * size;
                }
                out.move_to(dense[0]);
                for (std::size_t j = 1; j < dense.size(); ++j) { out.line_to(dense[j]); }
                if (closed) { out.close(); }
                ++lineIndex;
            }
            return out;
        }

        // Offset Path: every closed outline moves `distance` toward its outside (away from the fill; negative = inward) - each corner
        // slides along its mitre, bevelled where the mitre would run past 4 x distance. Open paths become a rounded outline `distance`
        // either side. Curves are flattened first. A very concave shape offset a long way can overlap itself (no clean-up of that).
        BLPath offsetPath(const BLPath& path, double distance) {
            if (distance == 0.0) {
                return path;
            }
            std::vector<Polyline> lines = flattenPath(path);
            // Which side of its edges the fill is on follows from the biggest outline (the outer one; holes run the other way).
            double bestArea = 0.0;
            for (Polyline& line : lines) {
                bool closed = false;
                std::vector<BLPoint> pts = cleanPoints(line, closed);
                if (!closed) { continue; }
                double area = 0.0;
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    const BLPoint& a = pts[i];
                    const BLPoint& b = pts[(i + 1) % pts.size()];
                    area += a.x * b.y - b.x * a.y;
                }
                if (std::fabs(area) > std::fabs(bestArea)) { bestArea = area; }
            }
            const double side = bestArea >= 0.0 ? 1.0 : -1.0;
            BLPath out;
            for (Polyline& line : lines) {
                bool closed = false;
                std::vector<BLPoint> pts = cleanPoints(line, closed);
                std::size_t n = pts.size();
                if (n < 2) { continue; }
                if (!closed) {
                    BLPath open;
                    open.move_to(pts[0]);
                    for (std::size_t i = 1; i < n; ++i) { open.line_to(pts[i]); }
                    BLStrokeOptions stroke;
                    stroke.width = 2.0 * std::fabs(distance);
                    stroke.start_cap = stroke.end_cap = BL_STROKE_CAP_ROUND;
                    stroke.join = BL_STROKE_JOIN_ROUND;
                    BLPath outline;
                    if (outline.add_stroked_path(open, stroke, BLApproximationOptions()) == BL_SUCCESS) { out.add_path(outline); }
                    continue;
                }
                auto normalOf = [&](std::size_t from, std::size_t to) {
                    double dx = pts[to].x - pts[from].x, dy = pts[to].y - pts[from].y, len = std::hypot(dx, dy);
                    return BLPoint(side * dy / len, -side * dx / len);
                };
                std::vector<BLPoint> moved;
                for (std::size_t i = 0; i < n; ++i) {
                    BLPoint n1 = normalOf((i + n - 1) % n, i), n2 = normalOf(i, (i + 1) % n);
                    double dot = n1.x * n2.x + n1.y * n2.y;
                    double stretch = 2.0 / (1.0 + dot);   // (mitre length / distance) squared
                    if (1.0 + dot < 1e-6 || stretch > 16.0) {
                        moved.push_back(BLPoint(pts[i].x + n1.x * distance, pts[i].y + n1.y * distance));
                        moved.push_back(BLPoint(pts[i].x + n2.x * distance, pts[i].y + n2.y * distance));
                    } else {
                        double k = distance / (1.0 + dot);
                        moved.push_back(BLPoint(pts[i].x + (n1.x + n2.x) * k, pts[i].y + (n1.y + n2.y) * k));
                    }
                }
                out.move_to(moved[0]);
                for (std::size_t i = 1; i < moved.size(); ++i) { out.line_to(moved[i]); }
                out.close();
            }
            return out;
        }

        // `path` after the geometry effect `item` (Round Corners, Offset Path or Roughen).
        BLPath applyGeometryEffect(const BLPath& path, const AppearanceItem& item) {
            switch (item.effect) {
                case EffectKind::RoundCorners: return roundedPath(path, item.radius);
                case EffectKind::OffsetPath: return offsetPath(path, item.radius);
                case EffectKind::Roughen: return roughenPath(path, item.radius, item.offsetX, item.offsetY);
                default: return path;
            }
        }

        // True when `point` lies in `path` stroked `width` wide with the options (width 0 never hits).
        bool strokeHit(const BLPath& path, double width, const StrokeOptions& options, const BLPoint& point) {
            if (width <= 0.0) {
                return false;
            }
            BLStrokeOptions stroke;
            stroke.width = width;
            stroke.start_cap = stroke.end_cap = uint8_t(blCap(options.cap));
            stroke.join = uint8_t(blJoin(options.join));
            stroke.miter_limit = options.miterLimit;
            BLPath outline;
            if (outline.add_stroked_path(path, stroke, BLApproximationOptions()) != BL_SUCCESS) {
                return false;
            }
            return outline.hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN;
        }

        // The stroke of `path` as a filled outline whose half-width follows `points` (see WidthPoint). Each subpath gets
        // two edges offset along the averaged vertex normals (mitered up to a limit); open ones are joined with the
        // chosen cap, closed ones become a ring (two opposed loops, filled non-zero).
        BLPath widthOutline(const BLPath& path, double baseHalf, const std::vector<WidthPoint>& points, StrokeCap cap) {
            const double kPi = 3.14159265358979323846;
            BLPath out;
            for (Polyline& line : flattenPath(path)) {
                std::vector<BLPoint> pts;
                for (const BLPoint& p : line.points) {
                    if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > 1e-9) { pts.push_back(p); }
                }
                bool closed = line.closed && pts.size() > 2;
                if (closed && std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) <= 1e-9) { pts.pop_back(); }
                std::size_t n = pts.size();
                if (n < 2) { continue; }
                std::vector<double> at(n, 0.0);
                for (std::size_t i = 1; i < n; ++i) { at[i] = at[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y); }
                double total = at[n - 1] + (closed ? std::hypot(pts[0].x - pts[n - 1].x, pts[0].y - pts[n - 1].y) : 0.0);
                auto dirOf = [&](std::size_t a, std::size_t b) {
                    double dx = pts[b].x - pts[a].x, dy = pts[b].y - pts[a].y, len = std::hypot(dx, dy);
                    return BLPoint(dx / len, dy / len);
                };
                std::vector<BLPoint> left(n), right(n);
                BLPoint startDir = dirOf(0, 1), endDir = dirOf(n - 2, n - 1);
                for (std::size_t i = 0; i < n; ++i) {
                    bool hasPrev = i > 0 || closed, hasNext = i + 1 < n || closed;
                    BLPoint d0 = hasPrev ? dirOf(i > 0 ? i - 1 : n - 1, i) : dirOf(i, i + 1);
                    BLPoint d1 = hasNext ? dirOf(i, i + 1 < n ? i + 1 : 0) : dirOf(i - 1, i);
                    BLPoint normal(-(d0.y + d1.y), d0.x + d1.x);
                    double len = std::hypot(normal.x, normal.y);
                    BLPoint n1(-d1.y, d1.x);
                    double miter = 1.0;
                    if (len < 1e-9) {
                        normal = n1;  // a 180 degree turn: just use the outgoing side
                    } else {
                        normal = BLPoint(normal.x / len, normal.y / len);
                        miter = 1.0 / std::max(normal.x * n1.x + normal.y * n1.y, 0.25);
                    }
                    double t = total > 0.0 ? at[i] / total : 0.0;
                    double wl = baseHalf * widthScaleAt(points, t, true) * miter, wr = baseHalf * widthScaleAt(points, t, false) * miter;
                    left[i] = BLPoint(pts[i].x + normal.x * wl, pts[i].y + normal.y * wl);
                    right[i] = BLPoint(pts[i].x - normal.x * wr, pts[i].y - normal.y * wr);
                }
                if (closed) {
                    out.move_to(left[0]);
                    for (std::size_t i = 1; i < n; ++i) { out.line_to(left[i]); }
                    out.close();
                    out.move_to(right[n - 1]);
                    for (std::size_t i = n - 1; i > 0; --i) { out.line_to(right[i - 1]); }
                    out.close();
                    continue;
                }
                auto capPoints = [&](std::size_t i, const BLPoint& dir, bool atEnd) {
                    // From the left edge round to the right at the end; from the right round to the left at the start.
                    BLPoint c = pts[i];
                    BLPoint nrm(-dir.y, dir.x);
                    if (!atEnd) { nrm = BLPoint(-nrm.x, -nrm.y); }
                    BLPoint a = atEnd ? left[i] : right[i], b = atEnd ? right[i] : left[i];
                    double ra = std::hypot(a.x - c.x, a.y - c.y), rb = std::hypot(b.x - c.x, b.y - c.y);
                    BLPoint outward = atEnd ? dir : BLPoint(-dir.x, -dir.y);
                    double r = (ra + rb) * 0.5;
                    if (cap == StrokeCap::Round) {
                        const int steps = 12;
                        for (int k = 1; k < steps; ++k) {
                            double ang = kPi * k / steps, co = std::cos(ang), si = std::sin(ang);
                            double rr = co > 0.0 ? ra : rb;
                            out.line_to(c.x + nrm.x * co * rr + outward.x * si * r, c.y + nrm.y * co * rr + outward.y * si * r);
                        }
                    } else if (cap == StrokeCap::Square) {
                        out.line_to(a.x + outward.x * ra, a.y + outward.y * ra);
                        out.line_to(b.x + outward.x * rb, b.y + outward.y * rb);
                    }
                };
                out.move_to(left[0]);
                for (std::size_t i = 1; i < n; ++i) { out.line_to(left[i]); }
                capPoints(n - 1, endDir, true);
                for (std::size_t i = n; i > 0; --i) { out.line_to(right[i - 1]); }
                capPoints(0, startDir, false);
                out.close();
            }
            return out;
        }

        // One arrowhead of `style` at `tip`, pointing along `dir` (a unit vector), `length` long, as a closed outline (the stroke width is
        // used by the chevron, which is a stroked polyline).
        BLPath arrowhead(ArrowStyle style, const BLPoint& tip, const BLPoint& dir, double length, double width) {
            const BLPoint side(-dir.y, dir.x);
            auto at = [&](double along, double across) {
                return BLPoint(tip.x + dir.x * along + side.x * across, tip.y + dir.y * along + side.y * across);
            };
            BLPath head;
            if (style == ArrowStyle::Filled) {
                head.move_to(at(0.0, 0.0));
                head.line_to(at(-length, length * 0.6));
                head.line_to(at(-length, -length * 0.6));
                head.close();
            } else if (style == ArrowStyle::Dot) {
                const BLPoint center = at(-length * 0.45, 0.0);
                head.add_circle(BLCircle(center.x, center.y, length * 0.45));
            } else if (style == ArrowStyle::Simple) {
                BLPath chevron;
                chevron.move_to(at(-length, length * 0.6));
                chevron.line_to(at(0.0, 0.0));
                chevron.line_to(at(-length, -length * 0.6));
                BLStrokeOptions stroke;
                stroke.width = std::max(width, 1.0);
                stroke.start_cap = stroke.end_cap = BL_STROKE_CAP_ROUND;
                stroke.join = BL_STROKE_JOIN_ROUND;
                head.add_stroked_path(chevron, stroke, BLApproximationOptions());
            }
            return head;
        }

        // A stroke that is more than a plain Blend2D stroke - aligned inside / outside a closed outline, or with arrowheads on the ends of
        // its open paths - as one filled outline (non-zero). The body is the plain / dashed / width-profile stroke; inside and outside
        // keep only the half of a double-width stroke on that side of the shape. False when nothing is drawn.
        bool decoratedStroke(const BLPath& path, double width, const StrokeOptions& options, BLPath& out) {
            out.clear();
            std::vector<bool> closed;
            const std::vector<std::vector<BLPoint>> lines = flattenToLines(path, closed);
            bool allClosed = !lines.empty();
            for (bool c : closed) { allClosed = allClosed && c; }
            const bool aligned = options.align != StrokeAlign::Center && allClosed && options.widths.empty();
            BLPath body;
            if (!options.widths.empty()) {
                body = widthOutline(path, width * 0.5, options.widths, options.cap);
            } else {
                BLStrokeOptions stroke;
                stroke.width = aligned ? width * 2.0 : width;
                stroke.start_cap = stroke.end_cap = uint8_t(blCap(options.cap));
                stroke.join = uint8_t(blJoin(options.join));
                stroke.miter_limit = options.miterLimit;
                const BLPath& source = options.dashes.empty() ? path : dashedPath(path, options.dashes);
                if (body.add_stroked_path(source, stroke, BLApproximationOptions()) != BL_SUCCESS) {
                    return false;
                }
            }
            Loops loops = flattenToLoops(body);
            if (aligned) {
                loops = combineLoops(loops, flattenToLoops(path), options.align == StrokeAlign::Inside ? BoolOp::Intersect : BoolOp::Subtract);
            }
            // Arrowheads: at the very start of the first open subpath and the very end of the last one.
            const double length = std::max(width, 1.0) * 4.0 * std::max(options.arrowScale, 1.0) / 100.0;
            auto direction = [&](const std::vector<BLPoint>& line, bool atEnd, BLPoint& tip, BLPoint& dir) {
                if (line.size() < 2) { return false; }
                tip = atEnd ? line.back() : line.front();
                for (std::size_t i = 1; i < line.size(); ++i) {
                    const BLPoint& other = atEnd ? line[line.size() - 1 - i] : line[i];
                    const double dx = tip.x - other.x, dy = tip.y - other.y, d = std::sqrt(dx * dx + dy * dy);
                    if (d >= length * 0.3 || i + 1 == line.size()) {
                        if (d < 1e-9) { return false; }
                        dir = BLPoint(dx / d, dy / d);   // pointing out of the path
                        return true;
                    }
                }
                return false;
            };
            std::size_t firstOpen = lines.size(), lastOpen = lines.size();
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!closed[i]) {
                    if (firstOpen == lines.size()) { firstOpen = i; }
                    lastOpen = i;
                }
            }
            auto addHead = [&](ArrowStyle style, std::size_t index, bool atEnd) {
                BLPoint tip, dir;
                if (style == ArrowStyle::None || index >= lines.size() || !direction(lines[index], atEnd, tip, dir)) {
                    return;
                }
                const BLPath head = arrowhead(style, tip, dir, length, width);
                if (head.size() > 0) {
                    loops = combineLoops(loops, flattenToLoops(head), BoolOp::Union);
                }
            };
            addHead(options.arrowStart, firstOpen, false);
            addHead(options.arrowEnd, lastOpen, true);
            out = loopsToPath(loops);
            return out.size() > 0;
        }

        // The first subpath, flattened and measured: cumulative length at each vertex.
        bool firstSubpath(const BLPath& path, std::vector<BLPoint>& pts, std::vector<double>& at) {
            std::vector<Polyline> lines = flattenPath(path);
            if (lines.empty()) { return false; }
            Polyline& line = lines.front();
            pts.clear();
            for (const BLPoint& p : line.points) {
                if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > 1e-9) { pts.push_back(p); }
            }
            if (line.closed) { pts.push_back(pts.front()); }
            if (pts.size() < 2) { return false; }
            at.assign(pts.size(), 0.0);
            for (std::size_t i = 1; i < pts.size(); ++i) { at[i] = at[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y); }
            return at.back() > 0.0;
        }

        // Strokes `path` with the gradient running along its length: the stroke is cut into short butt-ended
        // pieces, each a solid color picked from the gradient at its position. Pieces overlap their successor a
        // little so no seams show, and an opaque stroke gets a disc at every vertex so bends stay filled (which
        // rounds the joins - the other stroke settings have no say here yet).
        void strokeAlongPath(BLContext& ctx, const BLPath& path, double width, const SketchGradient& gradient, bool opaque) {
            std::vector<Polyline> lines = flattenPath(path);
            double total = 0.0;
            for (Polyline& line : lines) {
                if (line.closed) {
                    line.points.push_back(line.points.front());
                }
                for (std::size_t i = 1; i < line.points.size(); ++i) {
                    total += std::hypot(line.points[i].x - line.points[i - 1].x, line.points[i].y - line.points[i - 1].y);
                }
            }
            if (total < 1e-9) {
                return;
            }
            auto colorAtLength = [&](double length) {
                double t = length / total;
                if (gradient.spread == GradientSpread::Repeat) {
                    t -= std::floor(t);
                } else if (gradient.spread == GradientSpread::Reflect) {
                    t = std::fabs(t - 2.0 * std::floor(t * 0.5) - 1.0);
                    t = 1.0 - t;
                }
                return gradient.colorAt(t).toRgba32();
            };
            double step = std::max(total / 400.0, 0.5);
            double overlap = 0.35;
            ctx.set_stroke_width(width);
            double done = 0.0;
            for (const Polyline& line : lines) {
                for (std::size_t i = 1; i < line.points.size(); ++i) {
                    BLPoint a = line.points[i - 1], b = line.points[i];
                    double length = std::hypot(b.x - a.x, b.y - a.y);
                    if (length < 1e-9) {
                        continue;
                    }
                    double ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
                    int pieces = std::max(1, int(std::ceil(length / step)));
                    for (int k = 0; k < pieces; ++k) {
                        double s0 = length * k / pieces, s1 = length * (k + 1) / pieces;
                        bool last = k == pieces - 1 && i + 1 == line.points.size();
                        double s1x = last ? s1 : s1 + overlap;
                        BLPath piece;
                        piece.move_to(a.x + ux * s0, a.y + uy * s0);
                        piece.line_to(a.x + ux * s1x, a.y + uy * s1x);
                        ctx.set_stroke_style(colorAtLength(done + (s0 + s1) * 0.5));
                        ctx.stroke_path(piece);
                    }
                    if (opaque) {  // the vertex at the start of this segment
                        ctx.set_fill_style(colorAtLength(done));
                        ctx.fill_circle(BLCircle(a.x, a.y, width * 0.5));
                    }
                    done += length;
                }
                if (opaque && !line.points.empty()) {
                    ctx.set_fill_style(colorAtLength(done));
                    if (line.closed) {
                        ctx.fill_circle(BLCircle(line.points.back().x, line.points.back().y, width * 0.5));
                    }
                }
            }
        }
    }

    SketchObject::SketchObject(SketchObjectType type, const BLPath& path) : type_(type), path_(path) {
    }

    SketchObject* SketchObject::makeLine(double x1, double y1, double x2, double y2) {
        BLPath path;
        path.move_to(x1, y1);
        path.line_to(x2, y2);
        SketchObject* object = new SketchObject(SketchObjectType::Line, path);
        object->setFill(SketchPaint::none());
        return object;
    }

    SketchObject* SketchObject::makeRect(double x, double y, double width, double height) {
        BLPath path;
        path.add_rect(BLRect(x, y, width, height));
        return new SketchObject(SketchObjectType::Rect, path);
    }

    SketchObject* SketchObject::makeRoundRect(double x, double y, double width, double height, double radius) {
        BLPath path;
        path.add_round_rect(BLRoundRect(x, y, width, height, radius));
        return new SketchObject(SketchObjectType::RoundRect, path);
    }

    SketchObject* SketchObject::makeEllipse(double cx, double cy, double rx, double ry) {
        BLPath path;
        path.add_ellipse(BLEllipse(cx, cy, rx, ry));
        return new SketchObject(SketchObjectType::Ellipse, path);
    }

    SketchObject* SketchObject::makeSymbolInstance(const std::shared_ptr<const SymbolDef>& def, const BLMatrix2D& placement) {
        const BLBox& b = def->box;
        BLPoint corners[4] = {placement.map_point(b.x0, b.y0), placement.map_point(b.x1, b.y0), placement.map_point(b.x1, b.y1), placement.map_point(b.x0, b.y1)};
        BLPath path;
        path.move_to(corners[0]);
        path.line_to(corners[1]);
        path.line_to(corners[2]);
        path.line_to(corners[3]);
        path.close();
        SketchObject* object = new SketchObject(SketchObjectType::Symbol, path);
        object->symbol_ = def;
        object->name_ = def->name;
        object->setFill(SketchPaint::none());
        object->setStroke(SketchPaint::none());
        return object;
    }

    BLMatrix2D SketchObject::symbolMatrix() const {
        BLPathView view = path_.view();
        if (!symbol_ || view.size < 4) {
            return BLMatrix2D::make_identity();
        }
        const BLBox& b = symbol_->box;
        const double u = b.x1 - b.x0, v = b.y1 - b.y0;
        if (!(u > 0.0) || !(v > 0.0)) {
            return BLMatrix2D::make_identity();
        }
        const BLPoint p0 = view.vertex_data[0], p1 = view.vertex_data[1], p3 = view.vertex_data[3];
        const double a = (p1.x - p0.x) / u, bb = (p1.y - p0.y) / u, c = (p3.x - p0.x) / v, d = (p3.y - p0.y) / v;
        return BLMatrix2D(a, bb, c, d, p0.x - (a * b.x0 + c * b.y0), p0.y - (bb * b.x0 + d * b.y0));
    }

    SketchObject* SketchObject::makeImageObject(const std::shared_ptr<const ImageData>& image, const BLMatrix2D& placement) {
        BLPoint corners[4] = {placement.map_point(0.0, 0.0), placement.map_point(double(image->width), 0.0),
            placement.map_point(double(image->width), double(image->height)), placement.map_point(0.0, double(image->height))};
        BLPath path;
        path.move_to(corners[0]);
        path.line_to(corners[1]);
        path.line_to(corners[2]);
        path.line_to(corners[3]);
        path.close();
        SketchObject* object = new SketchObject(SketchObjectType::Image, path);
        object->image_ = image;
        object->name_ = image->name;
        object->setFill(SketchPaint::none());
        object->setStroke(SketchPaint::none());
        return object;
    }

    BLMatrix2D SketchObject::imageMatrix() const {
        BLPathView view = path_.view();
        if (!image_ || view.size < 4 || image_->width <= 0 || image_->height <= 0) {
            return BLMatrix2D::make_identity();
        }
        const double u = double(image_->width), v = double(image_->height);
        const BLPoint p0 = view.vertex_data[0], p1 = view.vertex_data[1], p3 = view.vertex_data[3];
        return BLMatrix2D((p1.x - p0.x) / u, (p1.y - p0.y) / u, (p3.x - p0.x) / v, (p3.y - p0.y) / v, p0.x, p0.y);
    }

    void SketchObject::drawImage(BLContext& ctx) const {
        if (!image_ || image_->image.is_empty()) {
            return;
        }
        ctx.save();
        const BLMatrix2D place = imageMatrix();
        ctx.apply_transform(place);
        // How big a picture pixel is on the device now.
        const BLMatrix2D total = ctx.final_transform();
        const double shown = std::sqrt(std::fabs(total.m00 * total.m11 - total.m01 * total.m10));
        double extra = 1.0;
        const BLImage& level = image_->levelFor(shown, extra);
        ctx.scale(extra, extra);
        ctx.set_pattern_quality(BL_PATTERN_QUALITY_BILINEAR);
        ctx.blit_image(BLPoint(0.0, 0.0), level);
        ctx.restore();
    }

    void SketchObject::drawSymbol(BLContext& ctx) const {
        if (!symbol_ || !symbol_->content) {
            return;
        }
        ctx.save();
        ctx.apply_transform(symbolMatrix());
        symbol_->content->paint(ctx);
        ctx.restore();
    }

    SketchObject* SketchObject::makePolygon(const std::vector<BLPoint>& points) {
        if (points.size() < 3) {
            throw std::runtime_error("SketchObject::makePolygon: need at least 3 points");
        }
        BLPath path;
        path.move_to(points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            path.line_to(points[i]);
        }
        path.close();
        return new SketchObject(SketchObjectType::Polygon, path);
    }

    SketchObject* SketchObject::makePolyline(const std::vector<BLPoint>& points) {
        if (points.size() < 2) {
            throw std::runtime_error("SketchObject::makePolyline: need at least 2 points");
        }
        BLPath path;
        path.move_to(points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            path.line_to(points[i]);
        }
        SketchObject* object = new SketchObject(SketchObjectType::Polyline, path);
        object->setFill(SketchPaint::none());
        return object;
    }

    SketchObject* SketchObject::make(SketchObjectType type, const BLPath& path) {
        return new SketchObject(type, path);
    }

    SketchObject* SketchObject::makePath(const BLPath& path) {
        return new SketchObject(SketchObjectType::Path, path);
    }

    SketchObject* SketchObject::makeText(const std::string& text, const std::string& fontName, double fontSize, const BLPoint& origin) {
        SketchObject* object = new SketchObject(SketchObjectType::Text, BLPath());
        object->setTextState(text, fontName, fontSize, BLMatrix2D::make_translation(origin.x, origin.y));
        object->setStroke(SketchPaint::none());   // text is a filled shape; a stroke can be added in the Appearance panel
        return object;
    }

    void SketchObject::rebuildText() {
        BLPath outline = textOutline(text_, fontName_, fontSize_, textStyle_);
        if (textWarp_.active() && outline.size() > 0) {
            BLBox box(0, 0, 0, 0);
            outline.get_bounding_box(&box);
            // The height the warp stretches: from just above the capitals to just under the baseline of the last line.
            const std::size_t lines = std::size_t(std::count(text_.begin(), text_.end(), char(10))) + 1;
            const double lineHeight = textLineHeight(fontName_, fontSize_, textStyle_);
            warpFrame_ = BLBox(box.x0, -0.78 * fontSize_, box.x1, double(lines - 1) * lineHeight + 0.22 * fontSize_);
            TextWarper warper(textWarp_, warpFrame_.x0, warpFrame_.x1, warpFrame_.y0, warpFrame_.y1);
            if (warper.valid()) {
                outline = warper.apply(outline);
            }
        }
        outline.transform(textMatrix_);
        path_ = outline;
    }

    void SketchObject::setTextWarp(const TextWarp& warp) {
        textWarp_ = warp;
        rebuildText();
    }

    BLPoint SketchObject::mapTextPoint(double x, double y) const {
        BLPoint p(x, y);
        if (textWarp_.active()) {
            TextWarper warper(textWarp_, warpFrame_.x0, warpFrame_.x1, warpFrame_.y0, warpFrame_.y1);
            if (warper.valid()) {
                p = warper.map(x, y);
            }
        }
        return textMatrix_.map_point(p);
    }

    void SketchObject::setText(const std::string& text) {
        text_ = text;
        rebuildText();
    }

    void SketchObject::setFontName(const std::string& fontName) {
        fontName_ = fontName;
        rebuildText();
    }

    void SketchObject::setFontSize(double size) {
        if (size <= 0.0) {
            throw std::runtime_error("SketchObject::setFontSize: size must be positive");
        }
        fontSize_ = size;
        rebuildText();
    }

    void SketchObject::setTextStyle(const TextStyle& style) {
        textStyle_ = style;
        rebuildText();
    }

    void SketchObject::setTextAll(const std::string& text, const std::string& fontName, double fontSize, const TextStyle& style) {
        if (fontSize <= 0.0) {
            throw std::runtime_error("SketchObject::setTextAll: size must be positive");
        }
        text_ = text;
        fontName_ = fontName;
        fontSize_ = fontSize;
        textStyle_ = style;
        rebuildText();
    }

    void SketchObject::setTextState(const std::string& text, const std::string& fontName, double fontSize, const BLMatrix2D& matrix) {
        if (fontSize <= 0.0) {
            throw std::runtime_error("SketchObject::setTextState: size must be positive");
        }
        text_ = text;
        fontName_ = fontName;
        fontSize_ = fontSize;
        textMatrix_ = matrix;
        rebuildText();
    }

    std::size_t activeIndexOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored) {
        if (stored < items.size() && items[stored].kind == kind) {
            return stored;
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].kind == kind) {
                return i;
            }
        }
        return items.size();
    }

    const AppearanceItem& activeItemOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored) {
        static const AppearanceItem kNoFill = [] { AppearanceItem item; item.kind = AppearanceKind::Fill; item.paint = SketchPaint::none(); return item; }();
        static const AppearanceItem kNoStroke = [] { AppearanceItem item; item.kind = AppearanceKind::Stroke; item.paint = SketchPaint::none(); return item; }();
        std::size_t index = activeIndexOf(items, kind, stored);
        if (index < items.size()) {
            return items[index];
        }
        static const AppearanceItem kNoEffect = [] { AppearanceItem item = AppearanceItem::makeEffect(EffectKind::RoundCorners); item.visible = false; return item; }();
        return kind == AppearanceKind::Fill ? kNoFill : (kind == AppearanceKind::Stroke ? kNoStroke : kNoEffect);
    }

    std::size_t SketchObject::activeIndex(AppearanceKind kind) const {
        return activeIndexOf(items_, kind, storedActive(kind));
    }

    AppearanceItem* SketchObject::activeItemPtr(AppearanceKind kind) {
        if (kind != AppearanceKind::Effect) {
            return &primary(kind);
        }
        std::size_t index = activeIndex(kind);
        return index < items_.size() ? &items_[index] : nullptr;
    }

    void SketchObject::setActiveIndex(std::size_t index) {
        if (index >= items_.size()) {
            return;
        }
        AppearanceKind kind = items_[index].kind;
        (kind == AppearanceKind::Fill ? activeFill_ : (kind == AppearanceKind::Stroke ? activeStroke_ : activeEffect_)) = index;
    }

    AppearanceItem& SketchObject::primary(AppearanceKind kind) {
        std::size_t index = activeIndex(kind);
        if (index < items_.size()) {
            return items_[index];
        }
        AppearanceItem item;
        item.kind = kind;
        item.paint = SketchPaint::none();
        if (kind == AppearanceKind::Fill) {
            items_.insert(items_.begin(), item);
            activeFill_ = 0;
            ++activeStroke_;
            return items_.front();
        }
        items_.push_back(item);
        activeStroke_ = items_.size() - 1;
        return items_.back();
    }

    void SketchObject::setAppearance(const std::vector<AppearanceItem>& items) {
        items_ = items;
        primary(AppearanceKind::Fill);
        primary(AppearanceKind::Stroke);
    }

    void SketchObject::setStrokeWidth(double width) {
        if (width < 0.0) {
            throw std::runtime_error("SketchObject::setStrokeWidth: width must not be negative");
        }
        primary(AppearanceKind::Stroke).strokeWidth = width;
    }

    void SketchObject::setOpacity(double opacity) {
        if (opacity < 0.0 || opacity > 1.0) {
            throw std::runtime_error("SketchObject::setOpacity: opacity must be within 0..1");
        }
        opacity_ = opacity;
    }

    bool SketchObject::isClosed() const {
        return type_ != SketchObjectType::Line && type_ != SketchObjectType::Polyline;
    }

    void SketchObject::transform(const BLMatrix2D& matrix) {
        path_.transform(matrix);
        if (type_ == SketchObjectType::Text) {
            textMatrix_.post_transform(matrix);   // later edits of the words land where the object now is
        }
    }

    BLBox SketchObject::geometryBounds() const {
        BLBox box(0, 0, 0, 0);
        path_.get_bounding_box(&box);
        return box;
    }

    void SketchObject::warp(const std::function<BLPoint(const BLPoint&)>& map) {
        BLPathView view = path_.view();
        std::vector<uint8_t> commands(view.command_data, view.command_data + view.size);
        std::vector<BLPoint> vertices(view.vertex_data, view.vertex_data + view.size);
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if (commands[i] == BL_PATH_CMD_CLOSE || commands[i] == BL_PATH_CMD_WEIGHT) {
                continue;
            }
            BLPoint mapped = map(vertices[i]);
            path_.set_vertex_at(i, BL_PATH_CMD_PRESERVE, mapped.x, mapped.y);
        }
    }

    BLBox SketchObject::frameBounds() const {
        if (frameAngle_ == 0.0) {
            return geometryBounds();
        }
        BLPath rotated = path_;
        rotated.transform(BLMatrix2D::make_rotation(-frameAngle_));
        BLBox box(0, 0, 0, 0);
        rotated.get_bounding_box(&box);
        return box;
    }

    SketchObject* SketchObject::clone() const {
        SketchObject* copy = new SketchObject(type_, path_);
        copy->name_ = name_;
        copy->visible_ = visible_;
        copy->locked_ = locked_;
        copy->layer_ = layer_;
        copy->layerHidden_ = layerHidden_;
        copy->layerLocked_ = layerLocked_;
        copy->items_ = items_;
        copy->activeFill_ = activeFill_;
        copy->activeStroke_ = activeStroke_;
        copy->activeEffect_ = activeEffect_;
        copy->opacity_ = opacity_;
        copy->blendMode_ = blendMode_;
        copy->groups_ = groups_;
        copy->frameAngle_ = frameAngle_;
        copy->text_ = text_;
        copy->fontName_ = fontName_;
        copy->fontSize_ = fontSize_;
        copy->textMatrix_ = textMatrix_;
        copy->textStyle_ = textStyle_;
        copy->textWarp_ = textWarp_;
        copy->warpFrame_ = warpFrame_;
        copy->symbol_ = symbol_;
        copy->image_ = image_;
        return copy;
    }

    SketchObject* SketchObject::cloneWithGeometry(SketchObjectType type, const BLPath& path) const {
        if (type == SketchObjectType::Text) {
            throw std::runtime_error("SketchObject::cloneWithGeometry: a Text object cannot be given a new outline");
        }
        SketchObject* copy = new SketchObject(type, path);
        copy->name_ = name_;
        copy->visible_ = visible_;
        copy->locked_ = locked_;
        copy->layer_ = layer_;
        copy->layerHidden_ = layerHidden_;
        copy->layerLocked_ = layerLocked_;
        copy->items_ = items_;
        copy->activeFill_ = activeFill_;
        copy->activeStroke_ = activeStroke_;
        copy->activeEffect_ = activeEffect_;
        copy->opacity_ = opacity_;
        copy->blendMode_ = blendMode_;
        copy->groups_ = groups_;
        return copy;
    }

    double widthScaleAt(const std::vector<WidthPoint>& points, double t, bool leftSide) {
        if (points.empty()) { return 1.0; }
        auto side = [leftSide](const WidthPoint& p) { return leftSide ? p.left : p.right; };
        if (t <= points.front().t) { return side(points.front()); }
        if (t >= points.back().t) { return side(points.back()); }
        for (std::size_t i = 1; i < points.size(); ++i) {
            if (t <= points[i].t) {
                double span = points[i].t - points[i - 1].t;
                double u = span > 1e-12 ? (t - points[i - 1].t) / span : 1.0;
                double ease = 0.5 - 0.5 * std::cos(3.14159265358979323846 * u);
                return side(points[i - 1]) + (side(points[i]) - side(points[i - 1])) * ease;
            }
        }
        return side(points.back());
    }

    bool SketchObject::pathSample(double t, BLPoint& point, BLPoint& normal) const {
        std::vector<BLPoint> pts;
        std::vector<double> at;
        if (!firstSubpath(path_, pts, at)) { return false; }
        double target = std::min(1.0, std::max(0.0, t)) * at.back();
        std::size_t i = 1;
        while (i + 1 < pts.size() && at[i] < target) { ++i; }
        double span = at[i] - at[i - 1];
        double u = span > 1e-12 ? (target - at[i - 1]) / span : 0.0;
        point = BLPoint(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * u, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u);
        double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y, len = std::hypot(dx, dy);
        normal = len > 0.0 ? BLPoint(-dy / len, dx / len) : BLPoint(0.0, 1.0);
        return true;
    }

    bool SketchObject::nearestOnPath(const BLPoint& point, double& t, double& distance) const {
        std::vector<BLPoint> pts;
        std::vector<double> at;
        if (!firstSubpath(path_, pts, at)) { return false; }
        distance = 1e300;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y, len2 = dx * dx + dy * dy;
            double u = len2 > 0.0 ? ((point.x - pts[i - 1].x) * dx + (point.y - pts[i - 1].y) * dy) / len2 : 0.0;
            u = std::min(1.0, std::max(0.0, u));
            double d = std::hypot(point.x - (pts[i - 1].x + dx * u), point.y - (pts[i - 1].y + dy * u));
            if (d < distance) {
                distance = d;
                t = (at[i - 1] + (at[i] - at[i - 1]) * u) / at.back();
            }
        }
        return true;
    }

    namespace {
        struct BlendInfo { const char* name; const char* css; BLCompOp op; };
        const BlendInfo kBlends[] = {
            {"Normal", "normal", BL_COMP_OP_SRC_OVER}, {"Multiply", "multiply", BL_COMP_OP_MULTIPLY},
            {"Screen", "screen", BL_COMP_OP_SCREEN}, {"Overlay", "overlay", BL_COMP_OP_OVERLAY},
            {"Darken", "darken", BL_COMP_OP_DARKEN}, {"Lighten", "lighten", BL_COMP_OP_LIGHTEN},
            {"Color Dodge", "color-dodge", BL_COMP_OP_COLOR_DODGE}, {"Color Burn", "color-burn", BL_COMP_OP_COLOR_BURN},
            {"Hard Light", "hard-light", BL_COMP_OP_HARD_LIGHT}, {"Soft Light", "soft-light", BL_COMP_OP_SOFT_LIGHT},
            {"Difference", "difference", BL_COMP_OP_DIFFERENCE}, {"Exclusion", "exclusion", BL_COMP_OP_EXCLUSION},
        };
    }

    const char* blendModeName(BlendMode mode) { return kBlends[std::size_t(mode)].name; }
    const char* blendModeCss(BlendMode mode) { return kBlends[std::size_t(mode)].css; }
    BlendMode blendModeFromCss(const std::string& value) {
        for (std::size_t i = 0; i < sizeof(kBlends) / sizeof(kBlends[0]); ++i) {
            if (value == kBlends[i].css) { return BlendMode(i); }
        }
        return BlendMode::Normal;
    }

    BLBox SketchObject::bounds() const {
        BLBox box(0, 0, 0, 0);
        path_.get_bounding_box(&box);
        double half = 0.0;
        for (const AppearanceItem& item : items_) {
            if (item.kind != AppearanceKind::Stroke || !item.visible || item.paint.isNone()) {
                continue;
            }
            double h = item.strokeWidth * 0.5;
            if (item.strokeOptions.brush) {
                h = item.strokeOptions.brush->reach(item.strokeWidth);
            } else if (!item.strokeOptions.widths.empty()) {
                double widest = 0.0;
                for (const WidthPoint& p : item.strokeOptions.widths) { widest = std::max(widest, std::max(p.left, p.right)); }
                h *= std::max(widest, 1.0);
            }
            if (item.strokeOptions.align == StrokeAlign::Outside) {
                h *= 2.0;
            }
            if (item.strokeOptions.arrowStart != ArrowStyle::None || item.strokeOptions.arrowEnd != ArrowStyle::None) {
                h = std::max(h, std::max(item.strokeWidth, 1.0) * 4.0 * std::max(item.strokeOptions.arrowScale, 1.0) / 100.0 * 1.3);
            }
            half = std::max(half, h);
        }
        return BLBox(box.x0 - half, box.y0 - half, box.x1 + half, box.y1 + half);
    }

    bool SketchObject::hitTest(const BLPoint& point, double tolerance) const {
        BLBox box = bounds();
        if (point.x < box.x0 - tolerance || point.x > box.x1 + tolerance ||
            point.y < box.y0 - tolerance || point.y > box.y1 + tolerance) {
            return false;
        }
        if (type_ == SketchObjectType::Symbol || type_ == SketchObjectType::Image) {
            return path_.hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN;
        }
        if (isClosed() && path_.hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN) {
            for (const AppearanceItem& item : items_) {
                if (item.kind == AppearanceKind::Fill && item.visible && !item.paint.isNone()) {
                    return true;
                }
            }
        }
        // Stroke: stroke the outline at (width + tolerance) and test that area - for every visible stroke, or, when
        // there is none, just the tolerance around the outline.
        bool any = false;
        for (const AppearanceItem& item : items_) {
            if (item.kind == AppearanceKind::Stroke && item.visible && !item.paint.isNone() && item.strokeWidth > 0.0) {
                any = true;
                const double covered = item.strokeOptions.brush ? item.strokeOptions.brush->reach(item.strokeWidth) * 2.0 : item.strokeWidth;
                if (strokeHit(path_, covered + tolerance * 2.0, item.strokeOptions, point)) {
                    return true;
                }
            }
        }
        return !any && tolerance > 0.0 && strokeHit(path_, tolerance * 2.0, StrokeOptions(), point);
    }

    namespace {
        // Fills (and strokes) the outlines of brush artwork with their own paints.
        void paintBrushParts(BLContext& ctx, const std::vector<BrushPart>& parts) {
            for (const BrushPart& part : parts) {
                BLBox box(0, 0, 0, 0);
                part.path.get_bounding_box(&box);
                if (part.closed && setPaintStyle(ctx, part.fill, box, false)) {
                    ctx.fill_path(part.path);
                }
                if (part.strokeWidth > 0.0 && setPaintStyle(ctx, part.stroke, box, true)) {
                    ctx.set_stroke_width(part.strokeWidth);
                    ctx.stroke_path(part.path);
                }
            }
        }

        // A stroke drawn by a brush: the nib swept along the path in the stroke paint, or the artwork bent or scattered along it.
        void paintBrushStroke(BLContext& ctx, const BLPath& path, const AppearanceItem& item, double alpha, const BLBox& box) {
            const BrushDef& brush = *item.strokeOptions.brush;
            const double width = item.strokeWidth;
            if (brush.kind == BrushKind::Calligraphic) {
                if (setPaintStyle(ctx, item.paint, box, false)) {
                    ctx.set_global_alpha(alpha);
                    ctx.fill_path(brush.nibOutline(path, width));
                }
                return;
            }
            auto draw = [&](BLContext& target) {
                target.set_global_alpha(1.0);
                if (brush.kind == BrushKind::Art) {
                    paintBrushParts(target, brush.bendAlong(path, width));
                } else {
                    const std::vector<BrushPart>& parts = brush.parts();
                    for (const BLMatrix2D& matrix : brush.placements(path, width)) {
                        target.save();
                        target.apply_transform(matrix);
                        paintBrushParts(target, parts);
                        target.restore();
                    }
                }
            };
            if (alpha >= 1.0) {
                draw(ctx);
            } else {
                const double reach = brush.reach(width);
                paintIsolated(ctx, BLBox(box.x0 - reach, box.y0 - reach, box.x1 + reach, box.y1 + reach), alpha, BlendMode::Normal, draw);
            }
        }

        // One stroke item painted over the object's outline (box = the outline's bounds, for gradients).
        void paintStrokeItem(BLContext& ctx, const BLPath& path, const AppearanceItem& item, double alpha, const BLBox& box) {
            const SketchPaint& stroke = item.paint;
            const StrokeOptions& options = item.strokeOptions;
            double width = item.strokeWidth;
            if (width <= 0.0) {
                return;
            }
            if (options.brush) {
                paintBrushStroke(ctx, path, item, alpha, box);
            } else if (options.decorated()) {
                BLPath outline;
                if (decoratedStroke(path, width, options, outline) && setPaintStyle(ctx, stroke, box, false)) {
                    ctx.set_global_alpha(alpha);
                    ctx.fill_path(outline);
                }
            } else if (!options.widths.empty() && setPaintStyle(ctx, stroke, box, false)) {
                ctx.set_global_alpha(alpha);
                ctx.fill_path(widthOutline(path, width * 0.5, options.widths, options.cap));
            } else if (stroke.isGradient() && stroke.gradient().alongStroke && !stroke.gradient().stops.empty()) {
                bool opaque = alpha >= 1.0;
                for (const GradientStop& stop : stroke.gradient().stops) {
                    opaque = opaque && stop.color.alpha() >= 1.0f;
                }
                ctx.set_global_alpha(alpha);
                strokeAlongPath(ctx, path, width, stroke.gradient(), opaque);
            } else if (setPaintStyle(ctx, stroke, box, true)) {
                ctx.set_stroke_width(width);
                ctx.set_stroke_caps(blCap(options.cap));
                ctx.set_stroke_join(blJoin(options.join));
                ctx.set_stroke_miter_limit(options.miterLimit);
                ctx.set_global_alpha(alpha);
                if (options.dashes.empty()) {
                    ctx.stroke_path(path);
                } else {
                    ctx.stroke_path(dashedPath(path, options.dashes));
                }
            }
        }
    }

    namespace {
        // An offscreen layer at the context's device resolution: `toLayer` maps document space into its pixels.
        struct DeviceLayer {
            BLImage image;
            int left = 0, top = 0;  // where the layer sits on the target (device pixels)
            BLMatrix2D toLayer;
            double scale = 1.0;     // layer pixels per document unit
        };

        bool makeLayer(BLContext& ctx, const BLBox& userBounds, DeviceLayer& layer) {
            BLMatrix2D m = ctx.final_transform();
            BLPoint corners[4] = {m.map_point(userBounds.x0, userBounds.y0), m.map_point(userBounds.x1, userBounds.y0),
                m.map_point(userBounds.x1, userBounds.y1), m.map_point(userBounds.x0, userBounds.y1)};
            double x0 = corners[0].x, x1 = corners[0].x, y0 = corners[0].y, y1 = corners[0].y;
            for (const BLPoint& c : corners) {
                x0 = std::min(x0, c.x); x1 = std::max(x1, c.x); y0 = std::min(y0, c.y); y1 = std::max(y1, c.y);
            }
            // Device pixels, a little generous for antialiasing, and never past what the target can show.
            layer.left = std::max(0, int(std::floor(x0)) - 2);
            layer.top = std::max(0, int(std::floor(y0)) - 2);
            int right = std::min(int(ctx.target_width()), int(std::ceil(x1)) + 2), bottom = std::min(int(ctx.target_height()), int(std::ceil(y1)) + 2);
            if (right <= layer.left || bottom <= layer.top) {
                return false;
            }
            if (layer.image.create(right - layer.left, bottom - layer.top, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return false;
            }
            layer.toLayer = m;
            layer.toLayer.post_translate(-double(layer.left), -double(layer.top));
            layer.scale = std::sqrt(std::fabs(m.m00 * m.m11 - m.m01 * m.m10));
            return true;
        }

        void compositeLayer(BLContext& ctx, const DeviceLayer& layer, double opacity, BlendMode mode) {
            ctx.save();
            BLMatrix2D toDevice = ctx.meta_transform();
            toDevice.invert();
            ctx.set_transform(toDevice);  // user space = device pixels, so the layer lands on whole pixels
            ctx.set_comp_op(kBlends[std::size_t(mode)].op);
            ctx.set_global_alpha(opacity);
            ctx.blit_image(BLPointI(layer.left, layer.top), layer.image);
            ctx.restore();
        }

        // Applies a pixel effect to everything drawn into the layer so far.
        void applyEffect(DeviceLayer& layer, const AppearanceItem& item) {
            BLImageData data;
            if (layer.image.get_data(&data) != BL_SUCCESS) {
                return;
            }
            int w = data.size.w, h = data.size.h;
            uint8_t* pixels = static_cast<uint8_t*>(data.pixel_data);
            float blur = float(item.softness * layer.scale);
            if (item.effect == EffectKind::GaussianBlur) {
                newui::shapes::boxBlur3(pixels, w, h, data.stride, blur);
                return;
            }
            if (item.effect == EffectKind::InnerShadow) {
                // The opposite of a silhouette: everything the shape does NOT cover, tinted and blurred (so its edge bleeds inward),
                // moved by the offset, and kept only where the shape is - laid on top with the effect's blend mode and amount.
                BLImage inverse, shade;
                if (inverse.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS || shade.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                    return;
                }
                BLImageData id, sd;
                inverse.get_data(&id);
                shade.get_data(&sd);
                BLRgba32 color = item.color.toRgba32();
                for (int y = 0; y < h; ++y) {
                    const uint32_t* from = reinterpret_cast<const uint32_t*>(pixels + std::ptrdiff_t(y) * data.stride);
                    uint32_t* to = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(id.pixel_data) + std::ptrdiff_t(y) * id.stride);
                    for (int x = 0; x < w; ++x) {
                        uint32_t a = (255u - (from[x] >> 24)) * color.a() / 255u;
                        to[x] = (a << 24) | ((color.r() * a / 255u) << 16) | ((color.g() * a / 255u) << 8) | (color.b() * a / 255u);
                    }
                }
                newui::shapes::boxBlur3(static_cast<uint8_t*>(id.pixel_data), w, h, id.stride, blur);
                BLPoint move = layer.toLayer.map_vector(item.offsetX, item.offsetY);
                int dx = int(std::lround(move.x)), dy = int(std::lround(move.y));
                for (int y = 0; y < h; ++y) {
                    const uint32_t* cover = reinterpret_cast<const uint32_t*>(pixels + std::ptrdiff_t(y) * data.stride);
                    int sy = std::min(h - 1, std::max(0, y - dy));
                    const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(id.pixel_data) + std::ptrdiff_t(sy) * id.stride);
                    uint32_t* to = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(sd.pixel_data) + std::ptrdiff_t(y) * sd.stride);
                    for (int x = 0; x < w; ++x) {
                        uint32_t c = row[std::min(w - 1, std::max(0, x - dx))];
                        uint32_t k = cover[x] >> 24;   // how much of the shape is here
                        to[x] = ((((c >> 24) & 255u) * k / 255u) << 24) | ((((c >> 16) & 255u) * k / 255u) << 16) |
                            ((((c >> 8) & 255u) * k / 255u) << 8) | ((c & 255u) * k / 255u);
                    }
                }
                BLContext oc(layer.image);
                oc.set_comp_op(kBlends[std::size_t(item.blendMode)].op);
                oc.set_global_alpha(item.opacity);
                oc.blit_image(BLPointI(0, 0), shade);
                oc.end();
                return;
            }
            // Drop shadow / outer glow: the silhouette of what is there, tinted, blurred, offset, and put behind it.
            BLImage mask;
            if (mask.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return;
            }
            BLImageData md;
            mask.get_data(&md);
            BLRgba32 tint = item.color.toRgba32();
            for (int y = 0; y < h; ++y) {
                const uint32_t* from = reinterpret_cast<const uint32_t*>(pixels + std::ptrdiff_t(y) * data.stride);
                uint32_t* to = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(md.pixel_data) + std::ptrdiff_t(y) * md.stride);
                for (int x = 0; x < w; ++x) {
                    uint32_t a = (from[x] >> 24) * tint.a() / 255u;
                    to[x] = (a << 24) | ((tint.r() * a / 255u) << 16) | ((tint.g() * a / 255u) << 8) | (tint.b() * a / 255u);
                }
            }
            newui::shapes::boxBlur3(static_cast<uint8_t*>(md.pixel_data), w, h, md.stride, blur);
            BLPoint shift = item.effect == EffectKind::DropShadow ? layer.toLayer.map_vector(item.offsetX, item.offsetY) : BLPoint(0, 0);
            BLImage out;
            if (out.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return;
            }
            {
                BLContext oc(out);
                oc.clear_all();
                oc.set_comp_op(kBlends[std::size_t(item.blendMode)].op);
                oc.set_global_alpha(item.opacity);
                oc.blit_image(BLPointI(int(std::lround(shift.x)), int(std::lround(shift.y))), mask);
                oc.set_comp_op(BL_COMP_OP_SRC_OVER);
                oc.set_global_alpha(1.0);
                oc.blit_image(BLPointI(0, 0), layer.image);
                oc.end();
            }
            layer.image = out;
        }
    }

    void paintIsolated(BLContext& ctx, const BLBox& userBounds, double opacity, BlendMode mode, const std::function<void(BLContext&)>& content) {
        DeviceLayer layer;
        if (!makeLayer(ctx, userBounds, layer)) {
            return;
        }
        {
            BLContext lc(layer.image);
            lc.clear_all();
            lc.set_transform(layer.toLayer);
            content(lc);
            lc.end();
        }
        compositeLayer(ctx, layer, opacity, mode);
    }

    void paintClipped(BLContext& ctx, const BLBox& userBounds, const BLPath& mask, const std::function<void(BLContext&)>& content) {
        DeviceLayer layer;
        if (!makeLayer(ctx, userBounds, layer)) {
            return;
        }
        BLImage cover;
        if (cover.create(layer.image.width(), layer.image.height(), BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return;
        }
        {
            BLContext lc(layer.image);
            lc.clear_all();
            lc.set_transform(layer.toLayer);
            content(lc);
            lc.end();
            BLContext mc(cover);
            mc.clear_all();
            mc.set_transform(layer.toLayer);
            mc.set_fill_style(BLRgba32(0xFFFFFFFFu));
            mc.fill_path(mask);
            mc.end();
        }
        BLImageData ld, cd;
        layer.image.get_data(&ld);
        cover.get_data(&cd);
        for (int y = 0; y < ld.size.h; ++y) {
            uint32_t* to = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(ld.pixel_data) + std::ptrdiff_t(y) * ld.stride);
            const uint32_t* from = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(cd.pixel_data) + std::ptrdiff_t(y) * cd.stride);
            for (int x = 0; x < ld.size.w; ++x) {
                uint32_t k = from[x] >> 24;
                uint32_t c = to[x];
                to[x] = k == 255u ? c : (((((c >> 24) & 255u) * k + 127u) / 255u) << 24) | (((((c >> 16) & 255u) * k + 127u) / 255u) << 16) |
                    (((((c >> 8) & 255u) * k + 127u) / 255u) << 8) | ((((c & 255u) * k + 127u) / 255u));
            }
        }
        compositeLayer(ctx, layer, 1.0, BlendMode::Normal);
    }

    BLPath SketchObject::itemOutline(std::size_t index) const {
        BLPath shown = path_;
        for (std::size_t j = index + 1; j < items_.size(); ++j) {
            if (items_[j].kind == AppearanceKind::Effect && isGeometryEffect(items_[j].effect) && items_[j].visible) {
                shown = applyGeometryEffect(shown, items_[j]);
            }
        }
        return shown;
    }

    bool SketchObject::hasPixelEffects() const {
        for (const AppearanceItem& item : items_) {
            if (item.kind == AppearanceKind::Effect && item.visible && !isGeometryEffect(item.effect)) {
                return true;
            }
        }
        return false;
    }

    bool SketchObject::strokeOutline(std::size_t index, BLPath& out) const {
        if (index >= items_.size() || items_[index].kind != AppearanceKind::Stroke) {
            return false;
        }
        const AppearanceItem& item = items_[index];
        if (item.strokeWidth <= 0.0 || item.strokeOptions.brush) {
            return false;
        }
        BLPath shown = itemOutline(index);
        out.clear();
        if (item.strokeOptions.decorated()) {
            return decoratedStroke(shown, item.strokeWidth, item.strokeOptions, out);
        }
        if (!item.strokeOptions.widths.empty()) {
            out = widthOutline(shown, item.strokeWidth * 0.5, item.strokeOptions.widths, item.strokeOptions.cap);
            return out.size() > 0;
        }
        BLStrokeOptions stroke;
        stroke.width = item.strokeWidth;
        stroke.start_cap = stroke.end_cap = uint8_t(blCap(item.strokeOptions.cap));
        stroke.join = uint8_t(blJoin(item.strokeOptions.join));
        stroke.miter_limit = item.strokeOptions.miterLimit;
        const BLPath& source = item.strokeOptions.dashes.empty() ? shown : dashedPath(shown, item.strokeOptions.dashes);
        return out.add_stroked_path(source, stroke, BLApproximationOptions()) == BL_SUCCESS && out.size() > 0;
    }

    void SketchObject::drawItem(BLContext& ctx, std::size_t index, const BLBox& box) const {
        const AppearanceItem& item = items_[index];
        if (type_ == SketchObjectType::Image) {
            // The picture stands where the first fill would be drawn.
            if (item.kind == AppearanceKind::Fill && index == activeIndexOf(items_, AppearanceKind::Fill, 0)) {
                drawImage(ctx);
            }
            return;
        }
        if (type_ == SketchObjectType::Symbol) {
            // The artwork stands where the first fill would be drawn; strokes and fills of an instance paint nothing.
            if (item.kind == AppearanceKind::Fill && index == activeIndexOf(items_, AppearanceKind::Fill, 0)) {
                drawSymbol(ctx);
            }
            return;
        }
        // Geometry effects above this item (later in the stack) reshape its outline, in stack order.
        BLPath shown = path_;
        for (std::size_t j = index + 1; j < items_.size(); ++j) {
            if (items_[j].kind == AppearanceKind::Effect && isGeometryEffect(items_[j].effect) && items_[j].visible) {
                shown = applyGeometryEffect(shown, items_[j]);
            }
        }
        ctx.save();
        ctx.set_comp_op(kBlends[std::size_t(item.blendMode)].op);
        if (item.kind == AppearanceKind::Fill) {
            if (isClosed() && setPaintStyle(ctx, item.paint, box, false)) {
                ctx.set_global_alpha(item.opacity);
                ctx.fill_path(shown);
            }
        } else {
            paintStrokeItem(ctx, shown, item, item.opacity, box);
        }
        ctx.restore();
    }

    void SketchObject::paintItems(BLContext& ctx) const {
        ctx.save();
        BLBox box = geometryBounds();
        for (std::size_t i = 0; i < items_.size(); ++i) {  // bottom to top
            if (items_[i].visible && items_[i].kind != AppearanceKind::Effect) {
                drawItem(ctx, i, box);
            }
        }
        ctx.restore();
    }

    void SketchObject::paintWithEffects(BLContext& ctx) const {
        DeviceLayer layer;
        if (!makeLayer(ctx, paintBounds(), layer)) {
            return;
        }
        BLBox box = geometryBounds();
        BLContext lc(layer.image);
        lc.clear_all();
        lc.set_transform(layer.toLayer);
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const AppearanceItem& item = items_[i];
            if (!item.visible) {
                continue;
            }
            if (item.kind != AppearanceKind::Effect) {
                drawItem(lc, i, box);
            } else if (!isGeometryEffect(item.effect)) {
                lc.end();  // the effect works on the pixels drawn so far
                applyEffect(layer, item);
                lc.begin(layer.image);
                lc.set_transform(layer.toLayer);
            }
        }
        lc.end();
        compositeLayer(ctx, layer, opacity_, blendMode_);
    }

    double SketchObject::effectPad() const {
        double pad = 0.0;
        for (const AppearanceItem& item : items_) {
            if (item.kind != AppearanceKind::Effect || !item.visible) {
                continue;
            }
            switch (item.effect) {
                case EffectKind::DropShadow: pad += item.softness * newui::shapes::kBlurPadFactor + std::max(std::fabs(item.offsetX), std::fabs(item.offsetY)); break;
                case EffectKind::OuterGlow:
                case EffectKind::GaussianBlur: pad += item.softness * newui::shapes::kBlurPadFactor; break;
                case EffectKind::InnerShadow:   // cast inside the shape: no reach
                case EffectKind::RoundCorners: break;
                case EffectKind::OffsetPath: pad += 4.0 * std::max(0.0, item.radius); break;   // a mitre reaches up to 4 x the distance
                case EffectKind::Roughen: pad += item.radius; break;
            }
        }
        return pad;
    }

    BLBox SketchObject::paintBounds() const {
        BLBox box = bounds();
        double pad = effectPad();
        return BLBox(box.x0 - pad, box.y0 - pad, box.x1 + pad, box.y1 + pad);
    }

    const char* effectName(EffectKind kind) {
        static const char* kNames[] = {"Drop Shadow", "Outer Glow", "Gaussian Blur", "Round Corners", "Inner Shadow", "Offset Path", "Roughen"};
        return kNames[std::size_t(kind)];
    }

    void SketchObject::paint(BLContext& ctx) const {
        if (!isVisible()) {
            return;
        }
        bool pixelEffects = false;
        for (const AppearanceItem& item : items_) {
            pixelEffects = pixelEffects || (item.kind == AppearanceKind::Effect && item.visible && !isGeometryEffect(item.effect));
        }
        if (pixelEffects) {
            paintWithEffects(ctx);
        } else if (opacity_ >= 1.0 && blendMode_ == BlendMode::Normal) {
            paintItems(ctx);
        } else {
            paintIsolated(ctx, paintBounds(), opacity_, blendMode_, [this](BLContext& layer) { paintItems(layer); });
        }
    }

}
